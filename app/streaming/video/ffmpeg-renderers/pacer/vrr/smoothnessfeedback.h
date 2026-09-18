// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once
#include "reserve.h"

namespace Vrr13 {
// Correct the protection estimate using interval errors, not absolute lateness.
// Separate instances score submission timing and confirmed native presentation.
// Neither can manufacture successes for a missing or nonconsecutive frame.
class SmoothnessFeedback {
public:
    static constexpr uint64_t ToleranceUs = 3000;
    struct Sample {
        uint64_t frame = 0, at = 0, intended = 0;
        uint64_t buffer = 0, headroom = 0, uncertainty = 0;
        bool eligible = false;
    };
    // Return demand only for a newly confirmed interval miss. Consumers that
    // gate adaptation on events must not replay an old histogram tail.
    uint64_t observe(const Sample& s, uint64_t observed, bool strictThreshold = false) {
        if (!s.eligible || !s.at || !s.intended || observed < s.at) {
            breakSequence();
            return 0;
        }
        if (m_HavePrevious && s.frame <= m_Previous.frame) return 0;
        const auto previous = m_Previous;
        const bool adjacent = m_HavePrevious && s.frame == previous.frame + 1 &&
            s.at > previous.at && s.intended > previous.intended &&
            s.at - previous.at < 1000000 && s.intended - previous.intended < 1000000;
        m_Previous = s; m_HavePrevious = true;
        if (!adjacent) return 0;
        const auto actual = s.at - previous.at;
        const auto intended = s.intended - previous.intended;
        const auto error = actual > intended ? actual - intended : intended - actual;
        const auto uncertainty = std::min<uint64_t>(1000, s.uncertainty + previous.uncertainty);
        // An ambiguous threshold crossing is unavailable evidence, not success.
        if (uncertainty && (strictThreshold ?
                error + uncertainty > ToleranceUs && error <= ToleranceUs + uncertainty :
                error + uncertainty >= ToleranceUs && error < ToleranceUs + uncertainty)) return 0;
        const bool missed = strictThreshold ? error > ToleranceUs + uncertainty :
                                             error >= ToleranceUs + uncertainty;
        ++m_ObservedIntervals;
        // Stretch: the current frame needs protection. Catch-up: attribute it
        // to the preceding late frame, using that frame's original buffer and
        // headroom. Delayed feedback must not repeatedly add to today's buffer.
        const auto& delayed = actual >= intended ? s : previous;
        const uint64_t available = std::min<uint64_t>(100000, delayed.buffer + delayed.headroom);
        const uint64_t demand = missed ? std::min<uint64_t>(200000,
            available + (error - uncertainty - ToleranceUs + 1)) : 0;
        m_LastObserved = std::max(m_LastObserved, observed);
        m_Demand.observe(ns(demand), ns(available), ns(m_LastObserved), ns(delayed.headroom));
        return demand;
    }
    uint64_t protectionUs() const {
        // Successful intervals enter as zero demand, so the percentile and
        // recent-miss boost use the actual 99.95% denominator.
        return uint64_t(m_Demand.target(100000000, 0, 0) / 1000);
    }
    uint64_t samples() const { return m_Demand.validationFrames(); }
    uint64_t lastObservedUs() const { return m_LastObserved; }
    uint64_t misses() const { return m_Demand.misses(); }
    uint64_t observedIntervals() const { return m_ObservedIntervals; }
    bool canRelease() const { return !samples() || m_Demand.canRelease(); }
    void breakSequence() { m_HavePrevious = false; }
    void reset() { *this = SmoothnessFeedback{}; }
private:
    static int64_t ns(uint64_t us) { return int64_t(std::min<uint64_t>(us, INT64_MAX / 1000)) * 1000; }
    Reserve m_Demand; // Zero-tolerance demand histogram, independent of readiness profiles.
    Sample m_Previous;
    uint64_t m_LastObserved = 0;
    uint64_t m_ObservedIntervals = 0;
    bool m_HavePrevious = false;
};
}
