// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include "intervalbuffer.h"

namespace Vrr13 {
// A rolling thirty-second outcome window, with 100 ms bucket resolution.
// Counts are snapshots, never cumulative counters to be summed across windows.
// Raw severity and the conditional mean of late frames remain available.
// Revision 4 keeps its historical score; V2 reports the mean-miss curve.
class ReadinessWindow {
public:
    struct Snapshot {
        uint64_t atUs = 0;
        uint64_t samples = 0;
        uint64_t misses = 0;
        uint64_t over1ms = 0;
        uint64_t over2ms = 0;
        uint64_t dropped = 0;
        uint64_t lateFrames = 0;
        uint64_t lateTotalUs = 0;
        bool meanMissPolicy = false;
        bool intervalPolicy = false;
        IntervalBuffer::Stats interval;
    };

    void record(uint64_t atUs, uint64_t latenessUs, bool dropped)
    {
        if (!atUs) return;
        // Producers on different threads can reach the lock in reverse order.
        m_LastUs = std::max(m_LastUs, atUs);
        const uint64_t tick = m_LastUs / BucketUs;
        auto& bucket = m_Buckets[tick % m_Buckets.size()];
        if (bucket.tick != tick) bucket = Bucket{tick, {}};
        ++bucket.counts.samples;
        bucket.counts.misses += dropped || latenessUs != 0;
        // A dropped frame is a miss, but has no measured lateness magnitude.
        bucket.counts.over1ms += !dropped && latenessUs > 1000;
        bucket.counts.over2ms += !dropped && latenessUs > 2000;
        bucket.counts.dropped += dropped;
        if (!dropped && latenessUs) {
            ++bucket.counts.lateFrames;
            bucket.counts.lateTotalUs += std::min<uint64_t>(latenessUs, 1000000);
        }
    }

    Snapshot snapshot(uint64_t nowUs = 0,
                      bool thresholdedMissPolicy = false,
                      bool meaningfulMissesOnly = false) const
    {
        Snapshot result;
        if (!m_LastUs) return result;
        result.atUs = std::max(nowUs, m_LastUs);
        const auto tick = result.atUs / BucketUs;
        for (const auto& bucket : m_Buckets) {
            if (tick >= bucket.tick && tick - bucket.tick < m_Buckets.size()) {
                result.samples += bucket.counts.samples;
                result.misses += bucket.counts.misses;
                result.over1ms += bucket.counts.over1ms;
                result.over2ms += bucket.counts.over2ms;
                result.dropped += bucket.counts.dropped;
                result.lateFrames += bucket.counts.lateFrames;
                result.lateTotalUs += bucket.counts.lateTotalUs;
            }
        }
        if (meaningfulMissesOnly) {
            result.meanMissPolicy = true;
            result.misses = result.dropped + result.over2ms;
        }
        else if (thresholdedMissPolicy) {
            const uint64_t softMisses = result.over1ms - result.over2ms;
            result.misses = result.dropped + result.over2ms;
            if (result.over1ms > result.samples / 2) {
                result.misses += softMisses;
            }
        }
        return result;
    }

    static double meanMissUs(const Snapshot& s) {
        return s.lateFrames ? double(s.lateTotalUs) / s.lateFrames : 0.0;
    }
    static double meanMissScore(const Snapshot& s) {
        // A diagnostic curve, not a probability of noticing stutter. Exactly
        // 100 through 1 ms; continuous above it, without an individual-frame cliff.
        const double excess = std::max(0.0, meanMissUs(s) - 1000.0) / 3000.0;
        return 100.0 / (1.0 + excess * excess);
    }

private:
    static constexpr uint64_t BucketUs = 100000;
    struct Bucket { uint64_t tick = 0; Snapshot counts; };
    std::array<Bucket, 300> m_Buckets{};
    uint64_t m_LastUs = 0;
};
}
