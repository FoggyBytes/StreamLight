// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once
#include "reserve.h"
#include "smoothnessfeedback.h"
#include "presentationtiming.h"
#include "recentreadiness.h"
#include <array>

namespace Vrr13 {
// All times here are microseconds on the same monotonic clock as the worker.
// Model FIFO service without intentional pacing or swapchain acquisition waits.
class ReadinessPrediction {
public:
    struct Probe {
        uint64_t decoded = 0, expected = 0, period = 0, typical = 0;
        uint64_t applied = 0, headroom = 0, guard = 0, decoderQueue = 0;
        bool eligible = false;
        uint64_t smoothingAdvance = 0;
        bool accountReadinessSlack = false;
    };
    void observe(Reserve& reserve, const Probe& p, uint64_t work, uint64_t scheduler,
                 RecentReadiness* recent = nullptr) {
        if (!p.eligible) { reset(); return; }
        work = std::min<uint64_t>(work, 100000);
        scheduler = std::min<uint64_t>(scheduler, 100000);
        const auto backlog = m_Ready > p.decoded ? m_Ready - p.decoded : 0;
        if (!backlog && p.decoderQueue <= p.period) flush(reserve, recent);
        const auto expectedBacklog = !m_Overloaded && m_Expected > p.expected ? m_Expected - p.expected : 0;
        m_Expected = p.expected + std::min<uint64_t>(1000000, expectedBacklog + p.typical);
        m_Ready = p.decoded + std::min<uint64_t>(1000000, (m_Overloaded ? 0 : backlog) + scheduler + work);
        if (m_Overloaded) return;
        const uint64_t required = m_Ready > m_Expected ? m_Ready - m_Expected : 0;
        // Preserve early-ready slack until after advancing the deadline. A
        // frame ready 5 ms early needs no reserve for a 3 ms advance. Clamping
        // the raw residual first would incorrectly request another 3 ms.
        const auto slack = p.accountReadinessSlack && m_Expected > m_Ready ?
            m_Expected - m_Ready : 0;
        Sample s{required, p.applied, p.headroom, p.guard, p.decoded,
            p.smoothingAdvance - std::min(p.smoothingAdvance, slack)};
        if (backlog || m_Count || work + scheduler > p.period || p.decoderQueue > p.period) {
            if (!m_Start) m_Start = p.decoded;
            if (m_Count == m_Samples.size() || p.decoded - m_Start >= 2000000) {
                m_Count = 0; m_Overloaded = true;
            }
            else m_Samples[m_Count++] = s;
        }
        else record(reserve, s, recent);
    }
    void reset() { *this = ReadinessPrediction{}; }
private:
    struct Sample { uint64_t required, applied, headroom, guard, at, advance; };
    static void record(Reserve& reserve, const Sample& s, RecentReadiness* recent) {
        const auto ns = [](uint64_t us) { return int64_t(std::min<uint64_t>(us, INT64_MAX / 1000)) * 1000; };
        reserve.observe(ns(s.required), ns(s.applied + s.headroom), ns(s.at), ns(s.guard + s.headroom));
        if (recent) recent->observe(s.required, s.advance, s.applied, s.at);
    }
    void flush(Reserve& reserve, RecentReadiness* recent) {
        for (size_t i = 0; i < m_Count; ++i) record(reserve, m_Samples[i], recent);
        m_Count = 0; m_Start = 0; m_Overloaded = false;
    }
    std::array<Sample, 512> m_Samples{};
    size_t m_Count = 0;
    uint64_t m_Ready = 0, m_Expected = 0, m_Start = 0;
    bool m_Overloaded = false;
};

struct PresentationObservation {
    SmoothnessFeedback::Sample smoothness;
    PresentationTimeKind timeKind = PresentationTimeKind::Unavailable;
    bool submitted = false, idValid = false, sampleValid = false, dxgi = false;
    bool latched = false;
    uint64_t id = 0, submission = 0, ready = 0, deadline = 0;
    uint64_t sampleId = 0, sampleTime = 0, observed = 0;
    uint64_t presentRefresh = 0, syncRefresh = 0, uncertainty = 0;
    int64_t timelineShift = 0; // Replay moves measured service latency with its submission.
};

// Match refresh identity before learning latency. SyncQPCTime alone is not
// the timestamp of the PresentCount accompanying it. Missing evidence stays missing.
class PresentationPrediction {
public:
    void observe(const PresentationObservation& o, bool requireDisplayEvents = true) {
        observe(o, [](const SmoothnessFeedback::Sample&, uint64_t) {}, requireDisplayEvents);
    }
    template<class Observer>
    void observe(const PresentationObservation& o, Observer&& onPresentation,
                 bool requireDisplayEvents = true) {
        if (o.submitted && o.idValid) {
            if (m_HaveMode && o.latched != m_Latched) reset();
            m_HaveMode = true; m_Latched = o.latched;
            m_Pending[m_Next++ % m_Pending.size()] = {o.id, o.submission, o.ready, o.deadline, 0, o.timelineShift, o.smoothness};
        }
        // Preserve submissions for delayed display events, but never promote
        // a refresh reference to a display instant in the live policy. The
        // legacy path is explicit and exists only to reproduce old captures.
        if ((requireDisplayEvents && o.timeKind != PresentationTimeKind::DisplayEvent) ||
            !o.sampleValid || !o.sampleTime || o.sampleTime > o.observed ||
            o.observed - o.sampleTime > 100000 || o.uncertainty > 500) return;
        const bool useRefreshReference = o.dxgi &&
            o.timeKind != PresentationTimeKind::DisplayEvent;
        if (useRefreshReference) {
            m_Anchors[m_NextAnchor++ % m_Anchors.size()] = {o.syncRefresh, o.sampleTime};
            for (auto& p : m_Pending) if (p.at && p.id == o.sampleId) p.refresh = o.presentRefresh;
        }
        for (size_t i = 0; i < m_Pending.size(); ++i) {
            auto& p = m_Pending[(m_Next + i) % m_Pending.size()];
            if (!p.at) continue;
            uint64_t presented = 0;
            if (useRefreshReference && p.refresh) {
                for (const auto& a : m_Anchors) if (a.sequence == p.refresh) presented = a.at;
            }
            else if (!useRefreshReference && p.id == o.sampleId) presented = o.sampleTime;
            const auto ready = std::max(p.at, p.ready);
            if (presented && presented >= ready && presented <= o.observed &&
                o.observed - presented <= 100000 && presented - ready <= 100000) {
                m_Latencies[m_NextLatency++ % m_Latencies.size()] = presented - ready;
                m_Count = std::min(m_Count + 1, m_Latencies.size());
                const auto shifted = [](uint64_t time, int64_t shift) {
                    return shift >= 0 ? time + uint64_t(shift) : time - std::min(time, uint64_t(-shift));
                };
                m_LastScanout = std::max(m_LastScanout, shifted(presented, p.shift));
                m_Observed = shifted(o.observed, o.timelineShift);
                ++m_Measured;
                m_Misses += presented > p.deadline ? presented - p.deadline > 3000 : p.deadline - presented > 3000;
                auto sample = p.smoothness;
                sample.at = shifted(presented, p.shift);
                sample.uncertainty = o.uncertainty;
                onPresentation(sample, m_Observed);
                p = {};
            }
            else if (o.observed >= p.at && o.observed - p.at > 100000) p = {};
        }
    }
    uint64_t lead(uint64_t now) const {
        if (!fresh(now) || !m_Count) return 0;
        auto values = m_Latencies;
        std::sort(values.begin(), values.begin() + m_Count);
        return values[(m_Count - 1) / 2];
    }
    uint64_t floor(uint64_t now, uint64_t interval, uint64_t guard) const {
        return fresh(now) ? m_LastScanout + interval + guard : 0;
    }
    uint64_t measured() const { return m_Measured; }
    uint64_t misses() const { return m_Misses; }
    void reset() { *this = PresentationPrediction{}; }
private:
    bool fresh(uint64_t now) const {
        return m_LastScanout && now >= m_LastScanout && now - m_LastScanout <= 100000 &&
            now >= m_Observed && now - m_Observed <= 100000;
    }
    struct Pending { uint64_t id = 0, at = 0, ready = 0, deadline = 0, refresh = 0; int64_t shift = 0; SmoothnessFeedback::Sample smoothness; };
    struct Anchor { uint64_t sequence = 0, at = 0; };
    std::array<Pending, 128> m_Pending{};
    std::array<Anchor, 128> m_Anchors{};
    std::array<uint64_t, 128> m_Latencies{};
    size_t m_Next = 0, m_NextAnchor = 0, m_NextLatency = 0, m_Count = 0;
    uint64_t m_LastScanout = 0, m_Observed = 0, m_Measured = 0, m_Misses = 0;
    bool m_HaveMode = false, m_Latched = false;
};
}
