// Imported from Nonary/moonlight-qt, branch master at f96b0524 (14/10/2026), by Chase Payne.
// GPLv3, the same licence as StreamLight. The body is verbatim: only this note was added,
// so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace PyroWaveLink {
constexpr int minimumKbps = 5000;
constexpr int maximumKbps = 3000000;
constexpr int durationMs = 2000;
constexpr int windowMs = 100;
constexpr int windowCount = durationMs / windowMs;

// PyroWave turns lost detail into blur: the receiver releases frames on time
// without it and the pacer does not buffer for it, so under 2% loss is usable
// (one bad 100 ms window above 5% is bunched, visible loss). Packets the host
// could not send count as lost; below 98% sent, the host itself is overloaded.
// A 0.1% limit failed a whole 1.25 Gbps step for 384 unsent packets and
// collapsed calibration to 480 Mbps.
constexpr double lossLimitPercent = 2.0;
constexpr double windowLossLimitPercent = 5.0;
constexpr double minimumSentShare = 0.98;
constexpr double frameLossLimitPercent = 10.0;

struct Result {
    int requestedKbps = 0;
    uint32_t expected = 0;
    uint32_t sent = 0;
    uint32_t received = 0;
    double senderMs = 0;
    double lossPercent = 100;
    double worstWindowLossPercent = 100;
    double delayP99Ms = 0;
    double delayGrowthMs = 0;
    uint32_t frames = 0;
    uint32_t damagedFrames = 0;
    uint32_t excessiveLossFrames = 0;
    uint32_t missingTailFrames = 0;
    double worstFrameLossPercent = 100;

    double damagedFramePercent() const { return frames ? 100.0 * damagedFrames / frames : 100; }
    double excessiveLossFramePercent() const { return frames ? 100.0 * excessiveLossFrames / frames : 100; }
    double missingTailFramePercent() const { return frames ? 100.0 * missingTailFrames / frames : 100; }
    bool kernelArrivalTimestamps = false;
    double receiverReadDelayP99Ms = 0;
    // Host diagnostics (Vibeshine): packets retried after a full send buffer
    // and the last send error. Older hosts report neither.
    uint32_t hostSendRetries = 0;
    int hostLastSendError = 0;

    bool hostSentEnough() const {
        return expected > 0 && sent <= expected && sent >= expected * minimumSentShare;
    }

    const char* failureReason() const {
        if (!expected) return "no probe completed";
        if (!hostSentEnough()) return "host did not send enough probe packets";
        if (!received) return "no UDP packets received";
        if (received > sent) return "invalid received packet count";
        if (!std::isfinite(senderMs) || !std::isfinite(lossPercent) ||
            !std::isfinite(worstWindowLossPercent) || !std::isfinite(delayP99Ms) ||
            !std::isfinite(delayGrowthMs)) return "invalid timing measurement";
        if (senderMs < durationMs * 0.98 || senderMs > durationMs * 1.02) return "host probe missed its sending duration";
        if (lossPercent >= lossLimitPercent) return "packet loss exceeds the limit";
        if (worstWindowLossPercent >= windowLossLimitPercent) return "bursty packet loss exceeds the limit";
        if (delayP99Ms > 4.0) return "packet delivery variation exceeds 4 ms";
        if (delayGrowthMs > 2.0) return "packet delivery delay grows by more than 2 ms";
        return "stable";
    }

    // Capacity qualification retains loss, sender pacing and queue-growth
    // limits. Transit jitter is a separate smoothness warning, not proof that
    // the codec/device is unsupported or that lowering bitrate will help.
    bool capacityQualified() const {
        return hostSentEnough() && received > 0 && received <= sent &&
               std::isfinite(delayP99Ms) && std::isfinite(delayGrowthMs) &&
               senderMs >= durationMs * 0.98 && senderMs <= durationMs * 1.02 &&
               lossPercent < lossLimitPercent && worstWindowLossPercent < windowLossLimitPercent &&
               delayGrowthMs <= 2.0;
    }

    bool stable() const {
        return capacityQualified() && delayP99Ms <= 4.0;
    }
};

// Unique sequence IDs make reordering harmless and prevent duplicate packets
// from hiding loss. Delay is relative to the minimum transit in this run, so
// host/client clock offsets never enter the grade.
inline void summarize(Result& result, const std::vector<int64_t>& arrivalsUs, int burstFps = 0,
                      uint32_t burstGroupPackets = 1)
{
    result.frames = result.damagedFrames = result.excessiveLossFrames = result.missingTailFrames = 0;
    result.worstFrameLossPercent = 100;
    if (burstFps > 0 && result.expected > 0) {
        // Match the burst-v1 host's sequence partitions, not arrival-time
        // buckets. Reordering cannot move a packet to another frame, and an
        // entirely absent frame or final tail still contributes its full loss.
        const uint32_t frames = (std::max)(uint32_t(1), uint32_t(uint64_t(burstFps) * durationMs / 1000));
        result.worstFrameLossPercent = 0;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const uint32_t begin = uint64_t(result.expected) * frame / frames;
            const uint32_t end = uint64_t(result.expected) * (frame + 1) / frames;
            if (begin == end) continue;
            // The tail is the final 1 ms send group, including interior holes
            // when the group's final packet survived. Match host partitioning.
            const uint32_t group = (std::max)(uint32_t(1), burstGroupPackets);
            const uint32_t tailBegin = begin + (end - begin - 1) / group * group;
            uint32_t missing = 0;
            bool missingTail = false;
            for (uint32_t seq = begin; seq < end; ++seq) {
                if (seq >= arrivalsUs.size() || arrivalsUs[seq] < 0) {
                    ++missing;
                    missingTail |= seq >= tailBegin;
                }
            }
            const double loss = 100.0 * missing / (end - begin);
            ++result.frames;
            result.damagedFrames += missing != 0;
            result.excessiveLossFrames += loss > frameLossLimitPercent;
            result.missingTailFrames += missingTail;
            result.worstFrameLossPercent = (std::max)(result.worstFrameLossPercent, loss);
        }
    }
    std::vector<double> transit;
    double first = 0, last = 0;
    int firstCount = 0, lastCount = 0;
    result.received = 0;
    result.worstWindowLossPercent = 0;
    for (int window = 0; window < windowCount; ++window) {
        const uint32_t begin = uint64_t(result.expected) * window / windowCount;
        const uint32_t end = uint64_t(result.expected) * (window + 1) / windowCount;
        uint32_t count = 0;
        for (uint32_t seq = begin; seq < end && seq < arrivalsUs.size(); ++seq) {
            if (arrivalsUs[seq] < 0) continue;
            ++count;
            // Sender uses this same 1 ms packet-group schedule.
            const auto tick = (uint64_t(seq + 1) * durationMs + result.expected - 1) / result.expected - 1;
            const double delta = arrivalsUs[seq] / 1000.0 - tick;
            transit.push_back(delta);
            if (window == 0) { first += delta; ++firstCount; }
            if (window == windowCount - 1) { last += delta; ++lastCount; }
        }
        result.received += count;
        if (end > begin) result.worstWindowLossPercent = (std::max)(result.worstWindowLossPercent,
            100.0 * (end - begin - count) / (end - begin));
    }
    result.lossPercent = result.expected ? 100.0 * (result.expected - result.received) / result.expected : 100;
    if (transit.empty()) return;
    std::sort(transit.begin(), transit.end());
    result.delayP99Ms = transit[(transit.size() - 1) * 99 / 100] - transit.front();
    result.delayGrowthMs = firstCount && lastCount ? last / lastCount - first / firstCount : INFINITY;
}

inline int rounded(double kbps) { return int(kbps / minimumKbps) * minimumKbps; }

// First test the requested rate; grow until a failure, then refine that bracket.
// A final 5% margin is measured twice afresh. Failed confirmation lowers the
// bracket, so a noisy/high-loss path can never inherit an earlier passing rate.
template<class Probe, class Cancelled>
Result search(int targetKbps, int capKbps, Probe probe, Cancelled cancelled,
              bool requireLowJitter = true)
{
    const auto qualified = [requireLowJitter](const Result& result) {
        return requireLowJitter ? result.stable() : result.capacityQualified();
    };
    const int cap = (std::min)(maximumKbps, rounded(capKbps));
    if (cap < minimumKbps) return {};
    int low = 0, high = cap + minimumKbps;
    int next = std::clamp(rounded(targetKbps), minimumKbps, cap);
    Result best;
    for (int attempt = 0; attempt < 40 && !cancelled(); ++attempt) {
        const auto result = probe(next);
        best = result;
        if (cancelled()) return {};
        if (qualified(result)) {
            best = result;
            low = next;
            if (low == cap || high - low <= minimumKbps) break;
            next = high <= cap ? rounded((low + high) / 2.0) :
                (std::min)(cap, (std::max)(low + minimumKbps, rounded(low * 1.25)));
        }
        else {
            high = next;
            if (high - low <= minimumKbps) break;
            next = (std::max)(minimumKbps, rounded((low + high) / 2.0));
        }
    }
    if (!low) return best;
    next = (std::max)(minimumKbps, rounded(low * 0.95));
    for (int attempt = 0; attempt < 32 && !cancelled(); ++attempt) {
        best = probe(next);
        if (cancelled()) return {};
        if (qualified(best)) {
            best = probe(next);
            if (!cancelled() && qualified(best)) return best;
        }
        if (next == minimumKbps) break;
        next = (std::max)(minimumKbps, rounded(next * 0.8));
    }
    return cancelled() ? Result{} : best;
}

// Frame pacing. The capacity probe spreads each millisecond's share evenly, so
// it never shows whether the receive path can absorb a whole frame arriving
// back-to-back. Frame-shaped probes (NvHTTP::probePyroWaveUdp burstFps) send
// the same packets as video frames on the stream pacer's 1 ms groups. A USB
// 2.5GbE dock lost whole 12-16 packet USB transfers above ~2.2 Gbps although
// its link reported 2.5 Gbps; the host then paces at the pace found here.
constexpr int paceStepKbps = 50000;
// Speed calibration primarily prevents repeated loss in the final send group.
// Small interior holes are allowed within the aggregate limits, but no frame
// may lose more than 10% of its total packets.
constexpr double paceLossPercent = lossLimitPercent;
constexpr double paceWindowLossPercent = windowLossLimitPercent;
constexpr double paceTailFrameLimitPercent = 2.0;

// Only loss and the host's sending duration grade a frame-shaped probe: its
// delivery timing is intentionally bursty, not the capacity probe's schedule.
inline bool paceMeasured(const Result& result)
{
    return result.hostSentEnough() && result.received > 0 && result.received <= result.sent &&
           std::isfinite(result.senderMs) && std::isfinite(result.lossPercent) &&
           std::isfinite(result.worstWindowLossPercent) &&
           result.senderMs >= durationMs * 0.98 && result.senderMs <= durationMs * 1.02 &&
           result.frames > 0 && std::isfinite(result.worstFrameLossPercent);
}

inline bool paceQualified(const Result& result)
{
    return paceMeasured(result) &&
           result.lossPercent < paceLossPercent && result.worstWindowLossPercent < paceWindowLossPercent &&
           result.excessiveLossFrames == 0 && result.worstFrameLossPercent <= frameLossLimitPercent &&
           result.missingTailFramePercent() < paceTailFrameLimitPercent;
}

// Rank usable fallback measurements by violated limits, then the tail loss
// this search targets, severe frame damage, worst frame damage and total loss.
// Invalid/unfinished probes must never become a usable fallback.
inline bool betterPaceMeasurement(const Result& a, const Result& b)
{
    const auto violations = [](const Result& r) {
        return int(r.lossPercent >= paceLossPercent) + int(r.worstWindowLossPercent >= paceWindowLossPercent) +
               int(r.excessiveLossFrames != 0 || r.worstFrameLossPercent > frameLossLimitPercent) +
               int(r.missingTailFramePercent() >= paceTailFrameLimitPercent);
    };
    if (violations(a) != violations(b)) return violations(a) < violations(b);
    if (a.missingTailFramePercent() != b.missingTailFramePercent())
        return a.missingTailFramePercent() < b.missingTailFramePercent();
    if (a.excessiveLossFramePercent() != b.excessiveLossFramePercent())
        return a.excessiveLossFramePercent() < b.excessiveLossFramePercent();
    if (a.worstFrameLossPercent != b.worstFrameLossPercent)
        return a.worstFrameLossPercent < b.worstFrameLossPercent;
    if (a.lossPercent != b.lossPercent) return a.lossPercent < b.lossPercent;
    return a.worstWindowLossPercent < b.worstWindowLossPercent;
}

struct PaceResult {
    int paceKbps = 0;      // Zero: not determined; the host keeps its default.
    bool qualified = false; // Both fresh confirmations pass aggregate and frame loss limits.
    Result probe;          // Measurement behind paceKbps.
};

// Highest pace, between floorKbps (a frame must still fit the frame interval)
// and the link. The link is tried first. Below it, a bisected pass leaves a
// 5% margin that must pass twice afresh, stepping down on failure. If that
// fails, test every remaining 50 Mbps step: loss need not be monotonic. Only
// after exhausting those alternatives return an explicitly unqualified best
// measurement (faster on ties), freshly measured twice at the fallback pace.
template<class Probe, class Cancelled>
PaceResult searchPace(int linkKbps, int floorKbps, Probe probe, Cancelled cancelled)
{
    const auto stepDown = [](double kbps) { return int(kbps / paceStepKbps) * paceStepKbps; };
    const int cap = stepDown(linkKbps);
    if (cap < paceStepKbps) return {};
    const int floor = std::clamp(int(std::ceil(double(floorKbps) / paceStepKbps)) * paceStepKbps,
                                 paceStepKbps, cap);
    std::vector<PaceResult> measurements;
    const auto measure = [&](int pace) {
        const Result result = probe(pace);
        const auto previous = std::find_if(measurements.begin(), measurements.end(),
            [pace](const PaceResult& r) { return r.paceKbps == pace; });
        // Keep the worse observation at each pace, including failed fresh
        // confirmations, instead of letting a lucky initial run win fallback.
        if (previous == measurements.end()) measurements.push_back({pace, false, result});
        else if (!paceMeasured(result) || (paceMeasured(previous->probe) &&
                 betterPaceMeasurement(previous->probe, result))) previous->probe = result;
        return result;
    };
    const auto passes = [&](int pace) {
        const auto result = measure(pace);
        return paceQualified(result);
    };
    const auto confirmed = [&](int pace) -> PaceResult {
        Result last;
        for (int run = 0; run < 2; ++run) {
            if (cancelled()) return {};
            last = measure(pace);
            if (!paceQualified(last)) return {};
        }
        return {pace, true, last};
    };
    if (passes(cap)) {
        if (cancelled()) return {};
        const auto atLink = confirmed(cap);
        if (atLink.qualified || cancelled()) return cancelled() ? PaceResult{} : atLink;
    }
    if (cancelled()) return {};
    int low = 0, high = cap;
    if (floor < cap) {
        if (passes(floor)) low = floor;
        for (int attempt = 0; low && attempt < 16 && high - low > paceStepKbps && !cancelled(); ++attempt) {
            const int next = (std::max)(low + paceStepKbps, stepDown((low + high) / 2.0));
            if (next >= high) break;
            if (passes(next)) low = next;
            else high = next;
        }
    }
    if (cancelled()) return {};
    for (int pace = (std::max)(floor, stepDown(low * 0.95)); low && !cancelled();
         pace = (std::max)(floor, stepDown(pace * 0.9))) {
        const auto result = confirmed(pace);
        if (result.qualified) return result;
        if (pace == floor) break;
    }
    // A failed floor says nothing about intermediate speeds. Probe the whole
    // remaining range before deciding every tested speed still has damage.
    for (int pace = cap - paceStepKbps; pace >= floor && !cancelled(); pace -= paceStepKbps) {
        const auto previous = std::find_if(measurements.begin(), measurements.end(),
            [pace](const PaceResult& r) { return r.paceKbps == pace; });
        if (previous != measurements.end() && !paceQualified(previous->probe)) continue;
        if (previous != measurements.end() || passes(pace)) {
            const auto result = confirmed(pace);
            if (result.qualified) return result;
        }
    }
    if (cancelled()) return {};
    PaceResult best;
    for (const auto& result : measurements) {
        if (paceMeasured(result.probe) && (!best.paceKbps || betterPaceMeasurement(result.probe, best.probe) ||
            (!betterPaceMeasurement(best.probe, result.probe) && result.paceKbps > best.paceKbps))) best = result;
    }
    if (!best.paceKbps) return {};
    bool passed = true;
    for (int run = 0; run < 2; ++run) {
        if (cancelled()) return {};
        const auto result = measure(best.paceKbps);
        // Do not publish a fallback if its final measurements are invalid.
        if (!paceMeasured(result)) return {};
        passed &= paceQualified(result);
        if (run == 0 || betterPaceMeasurement(best.probe, result)) best.probe = result;
    }
    best.qualified = passed;
    return cancelled() ? PaceResult{} : best;
}
} // namespace PyroWaveLink
