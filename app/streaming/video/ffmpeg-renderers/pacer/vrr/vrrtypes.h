// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

// The VRR pacing path deliberately keeps its data contract separate from
// FFmpeg's PTS/DTS fields.  Those fields are decoder-owned and are still used
// by the legacy pacing path, while these values describe the frame as it
// crossed the decoder/pacer boundary.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

// Three waiting frames plus the one owned by preparation/presentation.
// Both admission and the delay budget use this same ownership contract.
constexpr size_t VrrMaximumQueuedFrames = 3;
// Largest waiting-frame capacity any timing profile may select (Smooth). The
// decoder surface pool reserves the difference beyond the classic pacer.
constexpr size_t VrrLargestQueuedFrames = 4;

extern "C" {
#include <libavutil/frame.h>
}

struct VrrSessionConfig {
    int displayRefreshHz = 0;
    int streamRateHz = 0;
    bool allowAdditionalQueuedFrame = false;
    // A session preference resolved into the recorded controller parameters.
    // All presets retain jitter buffering and display-spacing protection.
    bool smoothFrameTiming = true;
    // Linux readiness boundaries differ from D3D11. Resolve into trace parameters.
    bool readinessHitchFeedback = false;
    // Historical near-ceiling checkbox, retained for old tests and captures.
    bool latencyFix = false;
    // 0: Smooth, 1: Balanced Target, 2: Low Latency, across all VRR rates.
    // The IDs are persisted and therefore remain stable across label changes.
    int latencyMode = 0;
    // Session-native A/B choice, recorded separately from controller timing.
    bool allowTearing = true;
    std::string calibrationKey;
    std::string calibrationPath;
};

// A move-only frame record.  Decoder completion is captured while the
// corresponding DECODE_UNIT is still available.  A raw RTP timestamp of zero
// is valid; timestampValid is intentionally separate to make that explicit.
class PacedFrame {
public:
    PacedFrame() = default;

    PacedFrame(AVFrame* frame,
               int frameNumber,
               uint32_t rtpTimestamp,
               bool timestampValid,
               uint64_t decoderOutputUs) :
        m_Frame(frame),
        m_FrameNumber(frameNumber),
        m_RtpTimestamp(rtpTimestamp),
        m_TimestampValid(timestampValid),
        m_DecoderOutputUs(decoderOutputUs),
        m_DecodeCompleteUs(decoderOutputUs)
    {
    }

    PacedFrame(PacedFrame&&) noexcept = default;
    PacedFrame& operator=(PacedFrame&&) noexcept = default;

    AVFrame* frame() const
    {
        return m_Frame.get();
    }

    AVFrame* release()
    {
        return m_Frame.release();
    }

    // Preserve the decode-complete boundary independently of decoder output.
    // Captured controller policy selects which boundary anchors source time.
    void noteGpuReadyUs(uint64_t gpuReadyUs)
    {
        if (gpuReadyUs > m_DecodeCompleteUs) {
            m_DecodeCompleteUs = gpuReadyUs;
        }
    }

    // The worker may have to block on a decoder/backend completion primitive
    // after this frame leaves the decoder. Keep that service time separate
    // from both immutable decoder output and the conservative completion
    // bound: the controller uses each for a different purpose.
    void noteDecodeSyncWaitUs(uint64_t waitUs)
    {
        m_DecodeSyncWaitUs = waitUs;
    }

    explicit operator bool() const
    {
        return m_Frame != nullptr;
    }

    int frameNumber() const
    {
        return m_FrameNumber;
    }

    uint32_t rtpTimestamp() const
    {
        return m_RtpTimestamp;
    }

    bool timestampValid() const
    {
        return m_TimestampValid;
    }

    uint64_t decodeCompleteUs() const
    {
        return m_DecodeCompleteUs;
    }

    uint64_t decoderOutputUs() const
    {
        return m_DecoderOutputUs;
    }

    void setDecoderOutputComplete(bool complete)
    {
        m_DecoderOutputComplete = complete;
    }

    bool decoderOutputComplete() const
    {
        return m_DecoderOutputComplete;
    }

    uint64_t decodeSyncWaitUs() const
    {
        return m_DecodeSyncWaitUs;
    }

    // Pre-decode timeline of the same frame, all on the LiGetMicroseconds()
    // clock: first packet received from the network, complete frame
    // reassembled (queued for the decoder), and packet handed to the decoder.
    // They exist so a late decode-complete can be attributed to the network,
    // the depacketizer, or the decoder instead of inferred. Zero means the
    // producer did not supply them.
    void setDeliveryTimeline(uint64_t receiveUs,
                             uint64_t reassembledUs,
                             uint64_t decodeSubmitUs)
    {
        m_ReceiveUs = receiveUs;
        m_ReassembledUs = reassembledUs;
        m_DecodeSubmitUs = decodeSubmitUs;
    }

    uint64_t receiveUs() const
    {
        return m_ReceiveUs;
    }

    uint64_t reassembledUs() const
    {
        return m_ReassembledUs;
    }

    uint64_t decodeSubmitUs() const
    {
        return m_DecodeSubmitUs;
    }

    // Time the decoder deliberately held this frame before decodeSubmitUs so
    // its GPU decode would not delay the previous frame's flip. It is part of
    // reassembled -> submit but is neither decoder backlog nor decode cost.
    void setDecodeHoldUs(uint64_t holdUs)
    {
        m_DecodeHoldUs = holdUs;
    }

    uint64_t decodeHoldUs() const
    {
        return m_DecodeHoldUs;
    }

    // Reassembled -> decode submission, excluding a deliberate hold.
    uint64_t decoderQueueUs() const
    {
        if (!m_ReassembledUs || m_DecodeSubmitUs < m_ReassembledUs) {
            return 0;
        }
        const uint64_t queueUs = m_DecodeSubmitUs - m_ReassembledUs;
        return queueUs - (std::min)(queueUs, m_DecodeHoldUs);
    }

    void setDecodeBoundary(uint64_t decodeBoundary)
    {
        m_DecodeBoundary = decodeBoundary;
    }

    uint64_t decodeBoundary() const
    {
        return m_DecodeBoundary;
    }

private:
    struct FrameDeleter {
        void operator()(AVFrame* frame) const
        {
            av_frame_free(&frame);
        }
    };

    std::unique_ptr<AVFrame, FrameDeleter> m_Frame;
    int m_FrameNumber = -1;
    uint32_t m_RtpTimestamp = 0;
    bool m_TimestampValid = false;
    bool m_DecoderOutputComplete = true;
    uint64_t m_DecoderOutputUs = 0;
    uint64_t m_DecodeCompleteUs = 0;
    uint64_t m_DecodeSyncWaitUs = 0;
    uint64_t m_ReceiveUs = 0;
    uint64_t m_ReassembledUs = 0;
    uint64_t m_DecodeSubmitUs = 0;
    uint64_t m_DecodeHoldUs = 0;
    uint64_t m_DecodeBoundary = 0;
};
