// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace Vrr13 {
// Live readiness protection, independent of the persisted calibration prior.
// Time-bounded 100 ms buckets include successful observations. The default
// retains the historical three-second p99 policy for exact replay; current
// presets may use up to five minutes for their quality history.
// Source timing is removed before observation; smoothing advance is an explicit
// additional deadline cost rather than an error in the FIFO source model.
// Revision 4 treats shortfalls through 1 ms as noise, requires over half the
// live samples for 1-2 ms pressure, and boosts immediately beyond 2 ms.
class RecentReadiness {
public:
    explicit RecentReadiness(uint64_t windowUs = 3000000,
                             uint64_t targetPerMillion = 990000,
                             bool thresholdedMissPolicy = false)
        : m_WindowUs(std::max<uint64_t>(100000, std::min<uint64_t>(300000000, windowUs))),
          m_TargetPerMillion(std::max<uint64_t>(1, std::min<uint64_t>(1000000, targetPerMillion))),
          m_Buckets(m_WindowUs / BucketUs),
          m_ThresholdedMissPolicy(thresholdedMissPolicy)
    {}

    void observe(uint64_t raw, uint64_t advance, uint64_t applied, uint64_t at) {
        if (!at || (m_Last && at < m_Last)) return;
        expire(at);
        if (!m_First || (m_Last && at - m_Last >= m_WindowUs)) m_First = at;
        const uint64_t required = std::min<uint64_t>(100000,
            std::min<uint64_t>(100000, raw) + std::min<uint64_t>(100000, advance));
        auto& bucket = m_Buckets[(at / BucketUs) % m_Buckets.size()];
        bucket.tick = at / BucketUs;
        ++bucket.counts[(required + BinUs - 1) / BinUs];
        ++m_Weights[(required + BinUs - 1) / BinUs];
        ++m_Count;
        m_Last = at;
        const uint64_t shortfall = required > applied ? required - applied : 0;
        if (shortfall > 1000) {
            ++bucket.over1ms;
            ++m_Over1ms;
        }
        const bool boostMiss = m_ThresholdedMissPolicy ?
            shortfall > 2000 : shortfall >= 1000;
        if (boostMiss) {
            // A new miss renews protection; reading an old tail never does.
            if (!m_LastMiss || at - m_LastMiss >= HoldUs) m_Boost = 0;
            m_Boost = std::max(m_Boost, required);
            m_LastMiss = at;
        }
    }

    uint64_t demand(uint64_t at) {
        expire(at);
        uint64_t accumulated = 0, quantile = 0;
        if (m_Count) {
            const auto rank = m_Count - m_Count * (1000000 - m_TargetPerMillion) / 1000000;
            for (std::size_t i = 0; i < m_Weights.size(); ++i) {
                accumulated += m_Weights[i];
                if (accumulated >= rank) { quantile = i * BinUs; break; }
            }
        }
        return std::max(quantile,
            m_LastMiss && at >= m_LastMiss && at - m_LastMiss < HoldUs ? m_Boost : 0);
    }

    bool allowsGrowth(uint64_t at, uint64_t applied) {
        expire(at);
        if (!m_ThresholdedMissPolicy) return true;
        if (m_LastMiss && at >= m_LastMiss && at - m_LastMiss < HoldUs &&
                m_Boost > applied) return true;
        // Small misses are pressure only when they dominate the live window.
        return m_Over1ms > m_Count / 2;
    }

    bool canRelease(uint64_t at) const {
        // Silence or an excluded overload episode is not clean evidence.
        return m_Count >= 32 && m_First && at >= m_First && at - m_First >= HoldUs &&
            at >= m_Last && at - m_Last <= 250000 &&
            (!m_LastMiss || (at >= m_LastMiss && at - m_LastMiss >= HoldUs));
    }
private:
    static constexpr uint64_t BucketUs = 100000,
        HoldUs = 2000000, BinUs = 250;
    struct Bucket {
        uint64_t tick = 0;
        std::array<uint32_t, 401> counts{};
        uint64_t over1ms = 0;
    };
    void expire(uint64_t at) {
        const auto tick = at / BucketUs;
        for (auto& bucket : m_Buckets) {
            if (tick >= bucket.tick && tick - bucket.tick >= m_Buckets.size()) {
                for (std::size_t i = 0; i < m_Weights.size(); ++i) {
                    m_Weights[i] -= bucket.counts[i];
                    m_Count -= bucket.counts[i];
                }
                m_Over1ms -= bucket.over1ms;
                bucket = {};
                bucket.tick = tick;
            }
        }
    }
    uint64_t m_WindowUs, m_TargetPerMillion;
    std::vector<Bucket> m_Buckets;
    std::array<uint64_t, 401> m_Weights{};
    uint64_t m_Count = 0, m_Over1ms = 0, m_First = 0, m_Last = 0,
             m_LastMiss = 0, m_Boost = 0;
    bool m_ThresholdedMissPolicy = false;
};
}
