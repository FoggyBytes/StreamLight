// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. CHANGED: std::max/std::min are
// parenthesised — (std::max)(...) — because windows.h defines max/min as macros in the TUs
// that include this one, which Nonary's build never hits. Nothing else was touched.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// PyroWave frames that lose optional detail packets are released by the
// receive thread after a packet-silence interval. That interval only guesses
// whether a packet is lost or still in flight, so on its own it can make a
// frame with all of its critical data miss its playout slot. The VRR pacer
// publishes the frame's on-time reassembly bound here; moonlight-common-c asks
// for it through LiSetVideoReassemblyDeadlineCallback() and shortens the
// silence interval only when waiting longer would make the frame late.
namespace VrrReceiveDeadline {

// Low 32 bits: RTP timestamp of the anchor frame. High 32 bits: the low 32
// bits of its reassembly deadline on the LiGetMicroseconds() clock. Zero means
// nothing is published. A single word keeps the receive thread lock-free.
inline std::atomic<uint64_t> g_Anchor {0};

// An anchor this far from the queried frame belongs to another source epoch
// (a host clock jump or a new stream) and says nothing about its slot.
constexpr int64_t kMaximumExtrapolationTicks = 90000;

inline uint64_t pack(uint32_t rtpTimestamp, uint64_t deadlineUs)
{
    const uint64_t packed = (uint64_t(uint32_t(deadlineUs)) << 32) | rtpTimestamp;
    // Zero is reserved for "unpublished"; one microsecond does not matter.
    return packed != 0 ? packed : uint64_t(1) << 32;
}

inline void publish(uint32_t rtpTimestamp, uint64_t deadlineUs)
{
    g_Anchor.store(pack(rtpTimestamp, deadlineUs), std::memory_order_release);
}

// Latest decode start that still leaves the frame's decode and preparation
// before its target, in the same packed form as g_Anchor. A decode hold may
// only extend to this bound; the reassembly deadline above does not include
// preparation and must not be used for it.
inline std::atomic<uint64_t> g_HoldAnchor {0};

inline void publishHold(uint32_t rtpTimestamp, uint64_t deadlineUs)
{
    g_HoldAnchor.store(pack(rtpTimestamp, deadlineUs), std::memory_order_release);
}

inline void clear()
{
    g_Anchor.store(0, std::memory_order_release);
    g_HoldAnchor.store(0, std::memory_order_release);
}

// The deadline is stored truncated, so it is recovered relative to nowUs; it
// is always within seconds of now. Frames are extrapolated from the anchor at
// the 90 kHz RTP rate, which is how the pacer maps their source slots.
inline uint64_t deadlineUs(uint64_t anchor, uint32_t rtpTimestamp, uint64_t nowUs)
{
    if (anchor == 0) {
        return 0;
    }
    const int64_t ticks = int32_t(rtpTimestamp - uint32_t(anchor));
    if (ticks > kMaximumExtrapolationTicks || ticks < -kMaximumExtrapolationTicks) {
        return 0;
    }
    const int64_t anchorFromNowUs = int32_t(uint32_t(anchor >> 32) - uint32_t(nowUs));
    const int64_t result = int64_t(nowUs) + anchorFromNowUs + ticks * 100 / 9;
    return result > 0 ? uint64_t(result) : 1;
}

inline uint64_t deadlineUs(uint32_t rtpTimestamp, uint64_t nowUs)
{
    return deadlineUs(g_Anchor.load(std::memory_order_acquire), rtpTimestamp, nowUs);
}

// A PyroWave decode already running on the GPU when a frame is presented can
// delay that frame's flip. On capture 20260926-133637 (890M) flips landed
// 1.1 ms after Present when the next decode was submitted after the Present
// call returned, but 2.6 ms (p90 3.9 ms) when it was already running. How much
// a given GPU suffers varies; the rule does not: submit the next decode after
// our Present call. The pacer opens a window from (its target - this
// machine's learned decode GPU time) and closes it when the Present call
// returns. The decoder holds only when the frame can still decode and
// prepare before its own target after the expected close (target + learned
// Present-call duration).
// Low 32 bits: window start, high 32 bits: expected close, both the low 32
// bits of LiGetMicroseconds() time. Zero means no window.
inline std::atomic<uint64_t> g_PresentWindow {0};

// Safety bound on any hold if the close is never signalled (a dropped frame).
constexpr uint64_t kMaximumDecodeHoldUs = 4000;

inline void publishPresentWindow(uint64_t startUs, uint64_t expectedCloseUs)
{
    const uint64_t packed = (uint64_t(uint32_t(expectedCloseUs)) << 32) | uint32_t(startUs);
    g_PresentWindow.store(packed != 0 ? packed : 1, std::memory_order_release);
}

// Called when the Present call returns: waiting decodes may go.
inline void clearPresentWindow()
{
    g_PresentWindow.store(0, std::memory_order_release);
}

struct DecodeHold {
    uint64_t window = 0;   // the window this hold waits on; 0 = don't hold
    uint64_t limitUs = 0;  // never hold past this
};

// Whether a decode of the frame with this RTP timestamp should wait for the
// open window to close. The window and deadline are rebuilt relative to nowUs,
// as both are always within seconds of it.
inline DecodeHold decodeHold(uint64_t window, uint64_t deadlineAnchor,
                             uint32_t rtpTimestamp, uint64_t nowUs)
{
    if (window == 0) {
        return {};
    }
    const int64_t startUs = int64_t(nowUs) + int32_t(uint32_t(window) - uint32_t(nowUs));
    const int64_t closeUs = int64_t(nowUs) + int32_t(uint32_t(window >> 32) - uint32_t(nowUs));
    if (int64_t(nowUs) < startUs || int64_t(nowUs) >= closeUs ||
            uint64_t(closeUs) - nowUs > kMaximumDecodeHoldUs) {
        return {};
    }
    // Without a known slot the hold could make the frame late; don't guess.
    const uint64_t deadline = deadlineUs(deadlineAnchor, rtpTimestamp, nowUs);
    if (deadline == 0 || uint64_t(closeUs) > deadline) {
        return {};
    }
    return { window, (std::min)(deadline, nowUs + kMaximumDecodeHoldUs) };
}

inline DecodeHold decodeHold(uint32_t rtpTimestamp, uint64_t nowUs)
{
    return decodeHold(g_PresentWindow.load(std::memory_order_acquire),
                      g_HoldAnchor.load(std::memory_order_acquire),
                      rtpTimestamp, nowUs);
}

inline bool decodeHoldReleased(const DecodeHold& hold, uint64_t nowUs)
{
    return nowUs >= hold.limitUs ||
           g_PresentWindow.load(std::memory_order_acquire) != hold.window;
}

// A high percentile of a recent duration, learned on this machine: reassembly
// to decode complete (the receive deadline's lead), decode GPU time and the
// Present call (the decode hold window).
class RecentDuration {
public:
    static constexpr size_t kWindow = 128;
    static constexpr size_t kMinimumSamples = 32;
    static constexpr size_t kPercentilePerMille = 950;

    void observeGpuCompletion(uint64_t startUs, uint64_t decoderOutputUs,
                              uint64_t completionUs, bool decoderOutputComplete = false)
    {
        // An async decoder's output is submission, not completion. If the
        // worker found the image already ready, it has no new completion
        // timestamp: preserve observed history instead of learning CPU cost.
        if (completionUs > decoderOutputUs ||
                (decoderOutputComplete && completionUs == decoderOutputUs)) {
            observe(startUs, completionUs);
        }
    }

    void observe(uint64_t reassembledUs, uint64_t decodeCompleteUs)
    {
        if (reassembledUs == 0 || decodeCompleteUs < reassembledUs) {
            return;
        }
        m_Samples[m_Next] = decodeCompleteUs - reassembledUs;
        m_Next = (m_Next + 1) % kWindow;
        m_Count = (std::min)(m_Count + 1, kWindow);
    }

    bool ready() const
    {
        return m_Count >= kMinimumSamples;
    }

    uint64_t percentileUs() const
    {
        if (m_Count == 0) {
            return 0;
        }
        std::array<uint64_t, kWindow> sorted = m_Samples;
        const size_t index = (m_Count - 1) * kPercentilePerMille / 1000;
        std::nth_element(sorted.begin(), sorted.begin() + index, sorted.begin() + m_Count);
        return sorted[index];
    }

private:
    std::array<uint64_t, kWindow> m_Samples {};
    size_t m_Next = 0;
    size_t m_Count = 0;
};

} // namespace VrrReceiveDeadline
