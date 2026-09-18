// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace Vrr13 {
// Historical revision-5 queue. Average measured lateness among missed frames in
// the last second; successful frames establish coverage but do not dilute it.
class MeanMissBuffer {
public:
    void observe(uint64_t at, uint64_t lateness, uint64_t applied, bool eligible,
                 uint64_t minimum, uint64_t maximum, uint64_t hold, uint64_t releaseRate) {
        minimum = std::min(minimum, maximum);
        if (!m_Initialized) { m_Target = applied; m_Initialized = true; }
        m_Target = std::clamp(m_Target, minimum, maximum);
        if (!eligible || !at || (m_Last && at <= m_Last)) { breakSequence(); return; }
        if (m_Last && at - m_Last > 250000) breakSequence();
        const auto elapsed = m_Last ? std::min<uint64_t>(at - m_Last, 100000) : 0;
        m_Last = at;
        if (!m_First) m_First = at;
        const auto tick = at / 10000;
        auto& b = m_Buckets[tick % m_Buckets.size()];
        if (b.tick != tick) b = Bucket{tick};
        ++b.samples;
        if (lateness) { ++b.misses; b.total += std::min<uint64_t>(lateness, 1000000); }
        uint64_t samples = 0, misses = 0, total = 0;
        for (const auto& bucket : m_Buckets) {
            if (tick >= bucket.tick && tick - bucket.tick < m_Buckets.size()) {
                samples += bucket.samples; misses += bucket.misses; total += bucket.total;
            }
        }
        // Compare the rational mean before integer rounding at the 1 ms boundary.
        const bool pressure = misses && total > misses * 1000;
        if (pressure) m_LastPressure = at;
        if (pressure && lateness && samples >= 32 && at - m_First >= 800000 &&
                (!m_LastAttack || at - m_LastAttack >= 250000)) {
            const auto excess = (total - misses * 1000 + misses - 1) / misses;
            const auto increase = std::min<uint64_t>(250, excess);
            const auto base = std::min(applied, maximum);
            m_Target = std::max(m_Target, base + std::min(increase, maximum - base));
            m_LastAttack = at;
            m_ReleaseFraction = 0;
        }
        else if (!pressure && samples >= 32 && at - m_First >= hold &&
                 (!m_LastPressure || at - m_LastPressure >= hold)) {
            m_ReleaseFraction += elapsed * releaseRate;
            const auto release = m_ReleaseFraction / 1000000;
            m_ReleaseFraction %= 1000000;
            m_Target -= std::min(m_Target - minimum, release);
        }
    }
    uint64_t demand(uint64_t applied) const { return m_Initialized ? m_Target : applied; }
    void breakSequence() { m_Buckets = {}; m_First = m_Last = m_ReleaseFraction = 0; }
    void reset() { *this = MeanMissBuffer{}; }
private:
    struct Bucket { uint64_t tick = 0, samples = 0, misses = 0, total = 0; };
    std::array<Bucket, 100> m_Buckets{};
    uint64_t m_Target = 0, m_First = 0, m_Last = 0, m_LastAttack = 0, m_LastPressure = 0;
    uint64_t m_ReleaseFraction = 0;
    bool m_Initialized = false;
};
}
