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
// One definition for buffering and reporting: mean absolute client-added
// interval error, including zero-error intervals, over the last second. The
// caller selects the profile tolerance; the quality score retains the
// preset's longer history independently of that one-second detection average.
class IntervalBuffer {
public:
    static constexpr uint64_t ToleranceUs = 500;
    static constexpr uint64_t ScoreBucketUs = 100000;
    static constexpr size_t MaximumScoreBuckets = 3000; // five minutes
    struct Sample {
        uint64_t frame = 0, intended = 0, submitted = 0, deadline = 0, ready = 0, buffer = 0;
        bool valid = false, absorbable = false;
    };
    struct Stats {
        double averageErrorUs = 0;
        uint64_t evaluatedUs = 0, failedUs = 0;
        double weightedLossUs = 0;
        uint64_t toleranceUs = ToleranceUs;
        bool severityWeighted = false;
        bool averageValid = false;
        double lossFraction() const {
            return evaluatedUs ? std::clamp(
                (severityWeighted ? weightedLossUs : double(failedUs)) / evaluatedUs,
                0.0, 1.0) : 0.0;
        }
        double qualityPercent() const { return 100.0 * (1.0 - lossFraction()); }
    };
    void observe(const Sample& s, uint64_t minimum, uint64_t maximum,
                 uint64_t hold, uint64_t releaseRate,
                 bool severityWeighted = false, uint64_t targetPerMillion = 990000,
        uint64_t toleranceUs = 500,
                 uint64_t scoreWindowUs = 30000000) {
        m_Stats.toleranceUs = toleranceUs;
        m_Stats.severityWeighted = severityWeighted;
        minimum = std::min(minimum, maximum);
        if (!m_Initialized) { m_Target = s.buffer; m_Initialized = true; }
        m_Target = std::clamp(m_Target, minimum, maximum);
        const auto previous = m_Previous;
        const bool adjacent = m_HavePrevious && s.valid && s.frame == previous.frame + 1 &&
            s.submitted > previous.submitted && s.intended > previous.intended &&
            s.submitted - previous.submitted < 1000000 && s.intended - previous.intended < 1000000;
        if (!adjacent) breakSequence();
        m_Previous = s;
        m_HavePrevious = s.valid && s.submitted && s.intended;
        updateScore(s.submitted, scoreWindowUs);
        if (!adjacent) return;

        const auto actual = s.submitted - previous.submitted;
        const auto intended = s.intended - previous.intended;
        const auto error = std::max(actual, intended) - std::min(actual, intended);
        const auto tick = s.submitted / 10000;
        auto& bucket = m_Window[tick % m_Window.size()];
        if (bucket.tick != tick) bucket = Bucket{tick};
        ++bucket.samples;
        bucket.total += error;
        if (!m_First) m_First = s.submitted;
        uint64_t samples = 0, total = 0;
        for (const auto& b : m_Window) {
            if (tick >= b.tick && tick - b.tick < m_Window.size()) {
                samples += b.samples; total += b.total;
            }
        }
        m_Stats.averageErrorUs = samples ? double(total) / samples : 0;
        m_Stats.averageValid = samples >= 2 && s.submitted - m_First >= 1000000;
        if (!m_Stats.averageValid) return;
        const bool pressure = total > samples * toleranceUs;
        // Weight the score by evaluated time, not frame rate. Attribute the
        // preceding interval to its evaluated one-second mean; gaps are unknown.
        auto& score = m_Score[(s.submitted / 100000) % m_Score.size()];
        if (score.tick != s.submitted / 100000) score = ScoreBucket{s.submitted / 100000};
        score.evaluated += actual;
        if (pressure) score.failed += actual;
        // Revision 7 measures severity rather than treating a tiny crossing as
        // a completely failed interval. Keep sub-microsecond loss in double so
        // Smooth's 99.99% target is not biased by per-frame rounding.
        const double excessUs = std::max(0.0, m_Stats.averageErrorUs - toleranceUs);
        const double loss = std::min(1.0, excessUs / intended);
        if (severityWeighted) score.weightedLoss += actual * loss;
        updateScore(s.submitted, scoreWindowUs);

        const double allowedLoss = (1000000 - std::min<uint64_t>(targetPerMillion, 1000000)) / 1000000.0;
        const bool belowTarget = m_Stats.lossFraction() > allowedLoss;
        const bool currentPressure = severityWeighted ? loss > allowedLoss : pressure;
        // Old score debt holds protection, but cannot authorize another attack
        // without current, attributable error outside the preset's allowance.
        const bool holdProtection = currentPressure || (severityWeighted && belowTarget);
        if (holdProtection) {
            m_LastPressure = s.submitted;
            if (severityWeighted) m_ReleaseFraction = 0;
        }
        const auto& delayed = actual >= intended ? s : previous;
        const auto lateness = delayed.ready > delayed.deadline ? delayed.ready - delayed.deadline : 0;
        const bool freshError = severityWeighted ? error > toleranceUs : error != 0;
        const bool grow = currentPressure && (!severityWeighted || belowTarget);
        if (grow && freshError && delayed.absorbable && lateness &&
                (!m_LastAttack || s.submitted - m_LastAttack >= 250000)) {
            const auto excess = severityWeighted ?
                uint64_t(std::ceil(std::min(250.0, std::max(0.0, excessUs - allowedLoss * intended)))) :
                (total - samples * toleranceUs + samples - 1) / samples;
            const auto freshExcess = severityWeighted ? error - toleranceUs : error;
            const auto increase = std::min({uint64_t(250), excess, lateness,
                severityWeighted ? freshExcess : uint64_t(250)});
            const auto base = std::min(delayed.buffer, maximum);
            m_Target = std::max(m_Target, base + std::min(increase, maximum - base));
            m_LastAttack = s.submitted;
            m_ReleaseFraction = 0;
        }
        else if (!holdProtection && s.absorbable && s.submitted - m_First >= hold &&
                (!m_LastPressure || s.submitted - m_LastPressure >= hold)) {
            m_ReleaseFraction += std::min<uint64_t>(actual, 100000) * releaseRate;
            const auto release = m_ReleaseFraction / 1000000;
            m_ReleaseFraction %= 1000000;
            m_Target -= std::min(m_Target - minimum, release);
        }
    }
    uint64_t demand(uint64_t applied) const { return m_Initialized ? m_Target : applied; }
    Stats stats() const { return m_Stats; }
    void breakSequence() {
        m_Window = {}; m_First = m_ReleaseFraction = 0; m_HavePrevious = false;
        m_Stats.averageErrorUs = 0; m_Stats.averageValid = false;
    }
    void reset() { *this = IntervalBuffer{}; }
private:
    struct Bucket { uint64_t tick = 0, samples = 0, total = 0; };
    struct ScoreBucket {
        uint64_t tick = 0, evaluated = 0, failed = 0;
        double weightedLoss = 0;
    };
    void updateScore(uint64_t at, uint64_t windowUs) {
        m_Stats.evaluatedUs = m_Stats.failedUs = 0;
        m_Stats.weightedLossUs = 0;
        const uint64_t windowBuckets = std::clamp<uint64_t>(
            (windowUs + ScoreBucketUs - 1) / ScoreBucketUs,
            1, MaximumScoreBuckets);
        const uint64_t tick = at / ScoreBucketUs;
        for (const auto& b : m_Score) {
            if (tick >= b.tick && tick - b.tick < windowBuckets) {
                m_Stats.evaluatedUs += b.evaluated; m_Stats.failedUs += b.failed;
                m_Stats.weightedLossUs += b.weightedLoss;
            }
        }
    }
    std::array<Bucket, 100> m_Window{};
    // Keep the long score history off the controller's stack. The controller
    // is instantiated in several independent workers and test fixtures.
    std::vector<ScoreBucket> m_Score = std::vector<ScoreBucket>(MaximumScoreBuckets);
    Sample m_Previous;
    Stats m_Stats;
    uint64_t m_Target = 0, m_First = 0, m_LastAttack = 0, m_LastPressure = 0, m_ReleaseFraction = 0;
    bool m_HavePrevious = false, m_Initialized = false;
};
}
