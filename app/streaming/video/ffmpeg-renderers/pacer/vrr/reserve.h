// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Vrr13 {
// Five-minute VRR13 smoothed-slot readiness-error model. One-second buckets expire whole oldest
// seconds (the represented window is 299..300 seconds). Allocation is confined
// to construction, profile IO and checkpoints, never frame observations.
class Reserve {
public:
    explicit Reserve(int version = 13) : m_Version(version) {}
    int version() const { return m_Version; }
    int64_t tolerance() const { return m_Version >= 14 && m_Version != 19 ? 3000000 : 0; }
    using Time = int64_t;
    static constexpr Time Second = 1000000000, Window = 300 * Second;
    static constexpr Time Bin = 250000, MissTolerance = 0;
    static constexpr size_t Bins = 401, Seconds = 300;
    static constexpr size_t ProfileWords = Bins + 4;
    static constexpr size_t MaxStateWords = ProfileWords + 9 + Seconds * (5 + 2 * Bins);
    static constexpr uint64_t MaxSamples = 30000000; // Corrupt-cache bound, 100k frames/sec.

    void observe(Time required, Time applied, Time at = -1, Time guard = 0) {
        // The default clock is only for standalone model tests. The controller
        // always supplies monotonic observation timestamps, independent of FPS.
        if (at < 0) at = m_LastAt ? m_LastAt + Second / 120 : Second;
        if (at <= 0 || (m_LastAt && at < m_LastAt)) return;
        if (!m_FirstAt || (m_LastAt && at - m_LastAt >= Window)) m_FirstAt = at;
        const Time second = at / Second;
        if (!m_LastAt || second != m_LastAt / Second) {
            for (auto& b : m_Buckets) {
                if (b.second >= 0 && b.second <= second - Time(Seconds)) {
                    for (size_t i = 0; i < Bins; ++i) m_Weights[i] -= b.counts[i];
                    m_WindowMisses -= b.misses; m_Total -= b.total;
                    b = Bucket{};
                }
            }
        }
        auto& b = m_Buckets[size_t(second % Seconds)];
        b.second = second;
        // Preserve bounded arithmetic even if a malformed replay supplies an
        // impossible number of observations at one timestamp.
        if (m_Total >= MaxSamples) return;
        // Applied is the full available budget (queue buffer + recovery
        // headroom). Guard also includes non-queued headroom for cache accounting.
        // All valid raw errors enter the histogram, including successes.
        const bool missed = required > applied && (m_Version >= 15 ?
            required - applied >= tolerance() : required - applied > tolerance());
        required = std::clamp(required, Time(0), Time(Bins - 1) * Bin);
        const size_t bin = size_t((required + Bin - 1) / Bin);
        // Neither the separate guard nor recovery headroom is cached as queue delay.
        b.applied = std::max(b.applied, std::clamp(applied - guard, Time(0), Time(Bins - 1) * Bin));
        ++b.counts[bin]; ++b.total; ++m_Weights[bin]; ++m_Total;
        b.misses += missed; m_WindowMisses += missed;
        ++m_Observations;
        m_LastAt = at;
        // A recent miss cannot be diluted away by five minutes of good history.
        if (missed) { m_Trusted = false; m_Boost = std::max(m_Boost, required); m_LastMiss = at; }
        else if (reliable() && (!m_LastMiss || at - m_LastMiss >= 60 * Second))
            m_Boost = std::max<Time>(0, m_Boost - 20000);
    }
    uint64_t evidence() const { return m_Total; }
    Time duration() const { return m_FirstAt ? std::min(Window, m_LastAt - m_FirstAt) : 0; }
    double coverage() const { return m_Total ? 100.0 * (1.0 - double(m_WindowMisses) / m_Total) : 0; }
    uint64_t misses() const { return m_WindowMisses; }
    uint64_t validationFrames() const { return m_Total; }
    bool reliable() const {
        return (duration() >= Window || (m_Trusted && duration() >= 60 * Second)) &&
            m_Total && m_WindowMisses * 2000 <= m_Total;
    }
    bool canRelease() const {
        // Retention is five minutes; adaptation starts after a short clean
        // validation run. Cached samples inform the estimate, not live coverage.
        return warmed() &&
            (!m_LastMiss || m_LastAt - m_LastMiss >= 2 * Second);
    }
    Time successfulBuffer() const {
        if (duration() < Window || !reliable()) return 0;
        Time result = 0;
        for (const auto& b : m_Buckets) result = std::max(result, b.applied);
        return result;
    }
    Time sessionStart() const { return m_FirstAt; }
    bool failing() const { return m_Total && m_WindowMisses * 2000 > m_Total; }
    unsigned successes() const { return m_Successes; }
    Time provenBuffer() const { return m_ProvenBuffer; }
    Time common() const {
        // Preload the cached five-minute histogram as prior observations. Since
        // the cache stores a histogram rather than individual timestamps, age
        // its mass uniformly while live samples replace it over five minutes.
        auto weights = m_Weights;
        const Time remaining = Window - duration();
        for (size_t i = 0; i < Bins; ++i)
            weights[i] += m_Cached[i] * uint64_t(remaining) / uint64_t(Window);
        return quantile(weights);
    }
    Time target(Time boostLimit, Time frameInterval = Second / 120, Time headroom = 0) const {
        const Time usual = common();
        const Time protection = std::max<Time>(0, usual +
            (failing() && m_LastAt - m_LastMiss < 2 * Second ? std::clamp(m_Boost - usual, Time(0), boostLimit) : 0) - (m_Version == 14 ? tolerance() : 0) - std::max<Time>(0, headroom));
        // A later miss can hold release, but cannot reinstate cold-start padding.
        const Time startup = warmed() ? 0 : m_Trusted ? m_ProvenBuffer : frameInterval;
        return std::max(startup, protection);
    }
    Time boost() const { return failing() && m_LastAt - m_LastMiss < 2 * Second ? std::max<Time>(0, m_Boost - common()) : 0; }
    uint64_t observations() const { return m_Observations; }
    // Offline age reduces the amount of cached evidence, preserving tail shape.
    void age(unsigned days) {
        for (auto& weight : m_Cached) weight >>= std::min(days, 14u);
        if (days) { m_Trusted = false; m_Successes = 0; }
    }
    uint64_t cachedEvidence() const {
        uint64_t n = 0; for (auto w : m_Cached) n += w; return n;
    }
    std::vector<int64_t> profile() const {
        // Save a complete window when available. Short interrupted sessions
        // keep the more protective cached distribution instead of erasing it.
        const bool retain = duration() < Window &&
            (quantile(m_Cached) > quantile(m_Weights) ||
             (quantile(m_Cached) == quantile(m_Weights) && cachedEvidence() > m_Total));
        const auto& weights = retain ? m_Cached : m_Weights;
        std::vector<int64_t> out{m_Version, retain ? m_CachedDuration : duration(), m_Successes, m_ProvenBuffer};
        out.insert(out.end(), weights.begin(), weights.end());
        return out;
    }
    bool loadProfile(const std::vector<int64_t>& words) {
        if (words.size() != ProfileWords || words[0] != m_Version || words[1] < 0 || words[1] > Window || words[2] < 0 || words[2] > 1000 ||
            words[3] < 0 || words[3] > Time(Bins - 1) * Bin) return false;
        uint64_t total = 0;
        for (size_t i = 4; i < words.size(); ++i) {
            if (words[i] < 0 || uint64_t(words[i]) > MaxSamples) return false;
            total += uint64_t(words[i]);
        }
        if (total > MaxSamples) return false;
        Reserve restored(m_Version);
        restored.m_CachedDuration = words[1];
        restored.m_Successes = unsigned(words[2]); restored.m_ProvenBuffer = words[3];
        restored.m_Trusted = words[2] >= 3 && words[1] == Window && total > 0;
        for (size_t i = 0; i < Bins; ++i) restored.m_Cached[i] = uint64_t(words[i + 4]);
        *this = std::move(restored);
        return true;
    }
    std::vector<int64_t> checkpoint() const {
        // Unlike a cache, exact replay includes the live bucket expiration times,
        // achieved coverage and transient pressure. Sparse bins keep ordinary
        // captures compact while retaining exact counters for every second.
        std::vector<int64_t> out{m_Version, m_CachedDuration, m_Successes, m_ProvenBuffer};
        out.insert(out.end(), m_Cached.begin(), m_Cached.end());
        out.insert(out.end(), {m_FirstAt, m_LastAt, m_LastMiss, m_Boost,
            int64_t(m_Observations), int64_t(m_Total), int64_t(m_WindowMisses), Time(Seconds), m_Trusted});
        for (const auto& b : m_Buckets) {
            size_t nonzero = 0; for (auto n : b.counts) nonzero += n != 0;
            out.insert(out.end(), {b.second, b.total, b.misses, b.applied, int64_t(nonzero)});
            for (size_t i = 0; i < Bins; ++i) if (b.counts[i]) out.insert(out.end(), {Time(i), b.counts[i]});
        }
        return out;
    }
    bool restore(const std::vector<int64_t>& words) {
        if (words.size() < ProfileWords + 9 + Seconds * 5 || words.size() > MaxStateWords) return false;
        Reserve r(m_Version);
        if (!r.loadProfile({words.begin(), words.begin() + ProfileWords})) return false;
        size_t i = ProfileWords;
        r.m_FirstAt = words[i++]; r.m_LastAt = words[i++]; r.m_LastMiss = words[i++]; r.m_Boost = words[i++];
        if (r.m_FirstAt < 0 || r.m_LastAt < r.m_FirstAt || r.m_LastMiss < 0 || r.m_LastMiss > r.m_LastAt ||
            r.m_Boost < 0 || r.m_Boost > Time(Bins - 1) * Bin || words[i] < 0) return false;
        r.m_Observations = uint64_t(words[i++]);
        if (words[i] < 0 || uint64_t(words[i]) > MaxSamples || words[i + 1] < 0 || words[i + 1] > words[i]) return false;
        const uint64_t expectedTotal = uint64_t(words[i++]), expectedMisses = uint64_t(words[i++]);
        if (words[i++] != Time(Seconds) || (words[i] != 0 && words[i] != 1)) return false;
        r.m_Trusted = bool(words[i++]);
        if (r.m_Trusted && (r.m_Successes < 3 || r.m_CachedDuration != Window)) return false;
        for (size_t index = 0; index < Seconds; ++index) {
            if (i + 5 > words.size()) return false;
            auto& b = r.m_Buckets[index]; b.second = words[i++];
            const Time total = words[i++], misses = words[i++], applied = words[i++], count = words[i++];
            if (applied < 0 || applied > Time(Bins - 1) * Bin || total < 0 || uint64_t(total) > MaxSamples || misses < 0 || misses > total || count < 0 || count > Time(Bins) ||
                i + size_t(count) * 2 > words.size() || b.second < -1 ||
                (b.second == -1 && (total || misses || applied || count)) ||
                (b.second >= 0 && (b.second % Seconds != index || b.second > r.m_LastAt / Second ||
                    b.second <= r.m_LastAt / Second - Time(Seconds)))) return false;
            uint64_t sum = 0; Time previous = -1;
            for (Time k = 0; k < count; ++k) {
                Time bin = words[i++], n = words[i++];
                if (bin <= previous || bin >= Time(Bins) || n <= 0 || uint64_t(n) > MaxSamples) return false;
                previous = bin; b.counts[size_t(bin)] = uint32_t(n);
                r.m_Weights[size_t(bin)] += uint64_t(n); sum += uint64_t(n);
            }
            if (sum != uint64_t(total)) return false;
            b.total = uint32_t(total); b.misses = uint32_t(misses); b.applied = applied;
            r.m_Total += sum; r.m_WindowMisses += b.misses;
        }
        if (i != words.size() || r.m_Total != expectedTotal || r.m_WindowMisses != expectedMisses ||
            r.m_Observations < r.m_Total || (!r.m_FirstAt && (r.m_LastAt || r.m_Observations))) return false;
        *this = std::move(r);
        return true;
    }
private:
    int m_Version;
    bool warmed() const { return m_Total >= 32 && duration() >= 2 * Second; }
    static Time quantile(const std::array<uint64_t, Bins>& weights) {
        uint64_t total = 0, cumulative = 0;
        for (auto n : weights) total += n;
        if (!total) return 0;
        // Exact empirical 99.95th percentile (nearest rank), over all samples.
        // Do not replace it with a maximum or a percentile of misses alone.
        const uint64_t allowance = total / 2000;
        for (size_t i = 0; i < Bins; ++i) {
            cumulative += weights[i];
            if (cumulative >= total - allowance) return Time(i) * Bin;
        }
        return Time(Bins - 1) * Bin;
    }
    struct Bucket {
        Time second = -1;
        std::array<uint32_t, Bins> counts{};
        uint32_t total = 0, misses = 0;
        Time applied = 0;
    };
    std::vector<Bucket> m_Buckets = std::vector<Bucket>(Seconds);
    std::array<uint64_t, Bins> m_Weights{}, m_Cached{};
    Time m_FirstAt = 0, m_LastAt = 0, m_LastMiss = 0, m_Boost = 0, m_CachedDuration = 0;
    unsigned m_Successes = 0;
    Time m_ProvenBuffer = 0;
    bool m_Trusted = false;
    uint64_t m_Observations = 0, m_Total = 0, m_WindowMisses = 0;
};
}
