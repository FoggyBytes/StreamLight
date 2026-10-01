// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
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
        uint64_t serialService = 0, decoderQueue = 0;
    };
    // Observation only. This explains the request made after an outcome;
    // the controller applies that request to subsequent frames. In particular,
    // rejected growth is not retained as debt or fed back into demand().
    enum class Action : uint8_t {
        Learning, SequenceBreak, Grow, Capped, CurrentPressure, HistoryHold,
        RecoveryHold, Release, Minimum, NotAbsorbable, Cooldown, NoFreshMiss, LimitChange
    };
    static const char* actionName(Action action) {
        switch (action) {
        case Action::Learning: return "learning";
        case Action::SequenceBreak: return "sequence break";
        case Action::Grow: return "late-work growth";
        case Action::Capped: return "growth capped";
        case Action::CurrentPressure: return "current error hold";
        case Action::HistoryHold: return "history hold";
        case Action::RecoveryHold: return "clean-time hold";
        case Action::Release: return "releasing";
        case Action::Minimum: return "minimum";
        case Action::NotAbsorbable: return "work not absorbable";
        case Action::Cooldown: return "growth cooldown";
        case Action::NoFreshMiss: return "no fresh late work";
        case Action::LimitChange: return "limit changed";
        }
        return "unknown";
    }
    struct Update {
        uint64_t atUs = 0, frame = 0, attributedFrame = 0;
        uint64_t beforeUs = 0, requestedUs = 0, minimumUs = 0, maximumUs = 0;
        uint64_t intervalErrorUs = 0, latenessUs = 0;
        uint64_t attemptedIncreaseUs = 0, clippedIncreaseUs = 0;
        uint64_t holdRemainingUs = 0, cooldownRemainingUs = 0;
        Action action = Action::Learning;
    };
    struct Stats {
        double averageErrorUs = 0;
        uint64_t evaluatedUs = 0, failedUs = 0;
        double weightedLossUs = 0;
        uint64_t toleranceUs = ToleranceUs;
        bool severityWeighted = false;
        bool averageValid = false;
        bool serviceOverloaded = false; // Qualified one-second workload, diagnostic only.
        bool initialCalibrationComplete = false;
        uint64_t calibrationCoverageUs = 0, calibrationSamples = 0;
        Update update;
        uint64_t lastGrowthAtUs = 0, lastGrowthUs = 0;
        uint64_t lastClippedAtUs = 0, lastClippedUs = 0;
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
                 uint64_t scoreWindowUs = 30000000,
                 uint64_t initialWarmupUs = 1000000,
                 size_t initialMinimumSamples = 2,
                 uint64_t recentPressureRelease = 0,
                 uint64_t serialServiceGate = 0,
                 bool holdRenewsBelowTargetOnly = false) {
        m_Stats.toleranceUs = toleranceUs;
        m_Stats.severityWeighted = severityWeighted;
        minimum = std::min(minimum, maximum);
        if (!m_Initialized) { m_Target = s.buffer; m_Initialized = true; }
        const uint64_t beforeUs = m_Target;
        m_Target = std::clamp(m_Target, minimum, maximum);
        const uint64_t boundedUs = m_Target;
        const auto previous = m_Previous;
        const bool adjacent = m_HavePrevious && s.valid && s.frame == previous.frame + 1 &&
            s.submitted > previous.submitted && s.intended > previous.intended &&
            s.submitted - previous.submitted < 1000000 && s.intended - previous.intended < 1000000;
        if (recentPressureRelease >= 3 && s.submitted) {
            // Keep this clock independent of m_Previous: an invalid sample may
            // replace m_Previous with a zero timestamp before the next frame.
            const bool unobservedGap = m_LastObservedSubmittedUs &&
                (s.submitted <= m_LastObservedSubmittedUs ||
                 s.submitted - m_LastObservedSubmittedUs >= 1000000);
            const bool prolongedIneligible = m_LastCleanEvidenceUs &&
                s.submitted > m_LastCleanEvidenceUs &&
                s.submitted - m_LastCleanEvidenceUs >= 2000000;
            if (unobservedGap || prolongedIneligible) {
                m_CleanEvidenceUs = 0;
                m_LastCleanEvidenceUs = 0;
            }
            m_LastObservedSubmittedUs = s.submitted;
        }
        if (!adjacent) {
            // A short dropped-frame or phase break does not erase already
            // observed clean recovery; requalification still starts afresh.
            breakSequence();
        }
        auto& update = m_Stats.update;
        update = {};
        update.atUs = s.submitted;
        update.frame = s.frame;
        update.beforeUs = beforeUs;
        update.requestedUs = m_Target;
        if (beforeUs != boundedUs) update.action = Action::LimitChange;
        update.minimumUs = minimum;
        update.maximumUs = maximum;
        m_Previous = s;
        m_HavePrevious = s.valid && s.submitted && s.intended;
        if (!adjacent) {
            updateScore(s.submitted, scoreWindowUs);
            if (beforeUs == boundedUs) update.action = Action::SequenceBreak;
            return;
        }

        const auto actual = s.submitted - previous.submitted;
        const auto intended = s.intended - previous.intended;
        const auto error = std::max(actual, intended) - std::min(actual, intended);
        update.intervalErrorUs = error;
        const auto tick = s.submitted / 10000;
        auto& bucket = m_Window[tick % m_Window.size()];
        if (bucket.tick != tick) bucket = Bucket{tick};
        ++bucket.samples;
        bucket.total += error;
        bucket.service += s.serialService;
        bucket.decoderQueue += s.decoderQueue;
        bucket.intended += intended;
        if (!m_First) {
            m_First = s.submitted;
            // Only a fresh session may use the shorter calibration window.
            // Keep this sequence's threshold stable after it qualifies, and
            // never rearm it on a source-rate/phase break or dropped frame.
            m_SequenceWarmupUs = m_Stats.initialCalibrationComplete ? 1000000 : initialWarmupUs;
            m_SequenceMinimumSamples = m_Stats.initialCalibrationComplete ? 2 : initialMinimumSamples;
        }
        ++m_SequenceSamples;
        uint64_t samples = 0, total = 0, service = 0, decoderQueue = 0, intendedTime = 0;
        for (const auto& b : m_Window) {
            if (tick >= b.tick && tick - b.tick < m_Window.size()) {
                samples += b.samples; total += b.total;
                service += b.service; decoderQueue += b.decoderQueue; intendedTime += b.intended;
            }
        }
        m_Stats.averageErrorUs = samples ? double(total) / samples : 0;
        m_Stats.calibrationSamples = m_SequenceSamples;
        m_Stats.calibrationCoverageUs = s.submitted - m_First;
        m_Stats.averageValid = samples >= 2 && m_SequenceSamples >= m_SequenceMinimumSamples &&
            m_Stats.calibrationCoverageUs >= m_SequenceWarmupUs;
        if (!m_Stats.averageValid) {
            updateScore(s.submitted, scoreWindowUs);
            return;
        }
        m_Stats.initialCalibrationComplete = true;
        m_Stats.serviceOverloaded = service > intendedTime || decoderQueue > intendedTime;
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
        const bool historicalPressure = severityWeighted && belowTarget;
        const bool historyHolds = !recentPressureRelease && historicalPressure;
        const auto& delayed = actual >= intended ? s : previous;
        const auto lateness = delayed.ready > delayed.deadline ? delayed.ready - delayed.deadline : 0;
        const bool freshError = severityWeighted ? error > toleranceUs : error != 0;
        const bool windowAbsorbable = service <= intendedTime && decoderQueue <= intendedTime;
        const bool delayedAbsorbable = delayed.absorbable &&
            (!serialServiceGate ||
             (serialServiceGate >= 2 ? windowAbsorbable :
              (delayed.serialService <= intended && delayed.decoderQueue <= intended)));
        // Revision 2 applies growth's causal evidence to hold renewal too.
        // Native presentation/scheduler jitter after readiness, or sustained
        // service overload, cannot be repaired by retaining standing delay.
        // Keep the normal hold between attributable misses; do not change the
        // long quality score, attack qualification, or gradual release rate.
        const bool pressureHolds = currentPressure &&
            (recentPressureRelease < 2 || (freshError && delayedAbsorbable && lateness));
        const bool holdProtection = pressureHolds ||
            historyHolds;
        // While the score still meets the target, a small dip pauses release
        // for that frame without restarting the clean-time hold.
        const bool renewsHold = holdProtection &&
            (!holdRenewsBelowTargetOnly || !severityWeighted || belowTarget);
        if (renewsHold) {
            m_LastPressure = s.submitted;
            if (severityWeighted) m_ReleaseFraction = 0;
        }
        // Revision 3 earns release time only from qualified, absorbable
        // adjacent observations. The qualification window still restarts on
        // every break, while earned recovery survives short gaps. This avoids
        // keeping a raised delay forever when a frame is missed every few
        // seconds, without counting the unknown gap as clean playback.
        if (recentPressureRelease >= 3) {
            if (renewsHold || !s.absorbable || !windowAbsorbable) {
                m_CleanEvidenceUs = 0;
                m_LastCleanEvidenceUs = 0;
            }
            else if (!holdProtection) {
                m_CleanEvidenceUs = std::min<uint64_t>(
                    hold, m_CleanEvidenceUs + std::min<uint64_t>(actual, 100000));
                m_LastCleanEvidenceUs = s.submitted;
            }
        }
        update.attributedFrame = delayed.frame;
        update.latenessUs = lateness;
        const auto remaining = [](uint64_t elapsed, uint64_t duration) {
            return elapsed < duration ? duration - elapsed : 0;
        };
        update.holdRemainingUs = std::max(
            recentPressureRelease >= 3 ? remaining(m_CleanEvidenceUs, hold) :
                remaining(s.submitted - m_First, hold),
            m_LastPressure ? remaining(s.submitted - m_LastPressure, hold) : 0);
        update.cooldownRemainingUs = m_LastAttack ?
            remaining(s.submitted - m_LastAttack, 250000) : 0;
        update.action = pressureHolds ? Action::CurrentPressure :
            historyHolds ? Action::HistoryHold : Action::RecoveryHold;
        const bool grow = currentPressure && (!severityWeighted || belowTarget);
        // Revision 1 mistook every slow frame for sustained overload. A
        // jitter buffer can cover a transient dependency stall when subsequent
        // frames recover. Judge capacity over the same qualified one-second
        // window as interval pressure, not the single late/catch-up pair.
        // Sequence breaks discard this evidence, preventing stale headroom
        // from authorizing growth across missing frames or source epochs.
        if (grow && freshError && delayedAbsorbable && lateness &&
                (!m_LastAttack || s.submitted - m_LastAttack >= 250000)) {
            const auto excess = severityWeighted ?
                uint64_t(std::ceil(std::min(250.0, std::max(0.0, excessUs - allowedLoss * intended)))) :
                (total - samples * toleranceUs + samples - 1) / samples;
            const auto freshExcess = severityWeighted ? error - toleranceUs : error;
            const auto increase = std::min({uint64_t(250), excess, lateness,
                severityWeighted ? freshExcess : uint64_t(250)});
            const auto base = std::min(delayed.buffer, maximum);
            update.attemptedIncreaseUs = increase;
            update.clippedIncreaseUs = increase - std::min(increase, maximum - base);
            if (update.clippedIncreaseUs) {
                m_Stats.lastClippedAtUs = s.submitted;
                m_Stats.lastClippedUs = update.clippedIncreaseUs;
            }
            m_Target = std::max(m_Target, base + std::min(increase, maximum - base));
            update.action = update.clippedIncreaseUs ? Action::Capped :
                m_Target > boundedUs ? Action::Grow : Action::CurrentPressure;
            if (m_Target > boundedUs) {
                m_Stats.lastGrowthAtUs = s.submitted;
                m_Stats.lastGrowthUs = m_Target - boundedUs;
            }
            m_LastAttack = s.submitted;
            m_ReleaseFraction = 0;
        }
        else if (!holdProtection && s.absorbable &&
                (recentPressureRelease >= 3 ?
                    (windowAbsorbable && m_CleanEvidenceUs >= hold) :
                    s.submitted - m_First >= hold) &&
                (!m_LastPressure || s.submitted - m_LastPressure >= hold)) {
            m_ReleaseFraction += std::min<uint64_t>(actual, 100000) * releaseRate;
            const auto release = m_ReleaseFraction / 1000000;
            m_ReleaseFraction %= 1000000;
            m_Target -= std::min(m_Target - minimum, release);
            update.action = m_Target == minimum ? Action::Minimum : Action::Release;
        }
        else if (grow) {
            update.action = !delayedAbsorbable ? Action::NotAbsorbable :
                !freshError || !lateness ? Action::NoFreshMiss : Action::Cooldown;
        }
        else if (!holdProtection &&
                 !(s.absorbable &&
                   (!serialServiceGate || (serialServiceGate >= 2 ? windowAbsorbable :
                    (s.serialService <= intended &&
                     s.decoderQueue <= intended))))) {
            update.action = Action::NotAbsorbable;
        }
        if (beforeUs != boundedUs) update.action = Action::LimitChange;
        update.requestedUs = m_Target;
    }
    uint64_t demand(uint64_t applied) const { return m_Initialized ? m_Target : applied; }
    // Replace the standing target at a source epoch and protect it for one
    // hold from the given time, as if pressure had just been observed.
    void restoreTarget(uint64_t target, uint64_t atUs) {
        if (!m_Initialized) return;
        m_Target = target;
        m_LastPressure = atUs;
        m_ReleaseFraction = 0;
    }
    Stats stats() const { return m_Stats; }
    void breakSequence() {
        m_Window = {}; m_First = m_ReleaseFraction = m_SequenceSamples = 0; m_HavePrevious = false;
        m_Stats.averageErrorUs = 0; m_Stats.averageValid = false;
        m_Stats.calibrationCoverageUs = m_Stats.calibrationSamples = 0;
        m_Stats.update = {};
        m_Stats.update.action = Action::SequenceBreak;
    }
    void reset() { *this = IntervalBuffer{}; }
private:
    struct Bucket {
        uint64_t tick = 0, samples = 0, total = 0;
        uint64_t service = 0, decoderQueue = 0, intended = 0;
    };
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
    uint64_t m_CleanEvidenceUs = 0, m_LastObservedSubmittedUs = 0, m_LastCleanEvidenceUs = 0;
    uint64_t m_SequenceWarmupUs = 1000000;
    uint64_t m_SequenceSamples = 0;
    size_t m_SequenceMinimumSamples = 2;
    bool m_HavePrevious = false, m_Initialized = false;
};
}
