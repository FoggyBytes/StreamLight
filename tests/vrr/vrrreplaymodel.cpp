#include "vrrreplaymodel.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool vrrDecodeReadinessOrderValid(uint64_t decoderOutputUs, uint64_t readyUs,
                                  uint64_t arrivalUs, uint64_t dequeueUs,
                                  uint64_t decisionUs, uint64_t decodeWaitUs,
                                  bool decisionValid,
                                  bool readinessExcludesQueue)
{
    if (!decoderOutputUs || decoderOutputUs > arrivalUs || readyUs < decoderOutputUs)
        return false;
    if (decisionValid && readyUs > decisionUs) return false;
    if (readinessExcludesQueue) {
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        const uint64_t expectedReadyUs = decodeWaitUs > 200 ?
            decoderOutputUs + std::min(maximum - decoderOutputUs, decodeWaitUs) :
            decoderOutputUs;
        return decisionValid ? readyUs == expectedReadyUs :
            readyUs == decoderOutputUs && decodeWaitUs == 0;
    }
    // Readiness may move past queue admission only through the worker's GPU
    // wait. Retired/evicted frames never visited that wait and keep CPU output.
    return readyUs <= arrivalUs ||
        (decisionValid && decodeWaitUs > 200 && dequeueUs >= arrivalUs &&
         readyUs >= dequeueUs && decodeWaitUs <= readyUs - dequeueUs);
}

uint64_t vrrBusyWorkerDecisionUs(uint64_t arrivalUs, uint64_t recordedDecisionUs,
                                uint64_t simulatedPreviousSubmissionUs,
                                uint64_t postSubmissionGapUs,
                                uint64_t learnedIdleLatencyUs)
{
    const uint64_t observedIdleLatencyUs = recordedDecisionUs >= arrivalUs ?
        recordedDecisionUs - arrivalUs : 0;
    const uint64_t idleFloorUs = arrivalUs +
        std::min(observedIdleLatencyUs, learnedIdleLatencyUs);
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    const uint64_t busyDecisionUs = simulatedPreviousSubmissionUs +
        std::min(maximum - simulatedPreviousSubmissionUs, postSubmissionGapUs);
    return std::max(idleFloorUs, busyDecisionUs);
}

namespace {

uint64_t saturatingAdd(uint64_t left, uint64_t right)
{
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    return right > maximum - left ? maximum : left + right;
}

uint64_t saturatingMultiply(uint64_t value, uint64_t multiplier)
{
    if (value == 0 || multiplier == 0) {
        return 0;
    }
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    return value > maximum / multiplier ? maximum : value * multiplier;
}

VrrRasterPhaseState classifyPhase(uint64_t phaseUs, uint64_t periodUs,
                                  uint64_t activeUs, uint64_t uncertaintyUs)
{
    if (periodUs == 0 || activeUs == 0 || phaseUs >= periodUs) {
        return VrrRasterPhaseState::Unclassified;
    }

    const uint64_t boundedUncertainty = std::min(uncertaintyUs, periodUs / 2);
    const bool nearStart = phaseUs <= boundedUncertainty;
    const bool nearEnd = periodUs - phaseUs <= boundedUncertainty;
    const uint64_t activeDistance = phaseUs >= activeUs ?
        phaseUs - activeUs : activeUs - phaseUs;
    if (nearStart || nearEnd || activeDistance <= boundedUncertainty) {
        return VrrRasterPhaseState::BoundaryUncertain;
    }
    return phaseUs < activeUs ?
        VrrRasterPhaseState::Active : VrrRasterPhaseState::Inactive;
}

VrrRasterPhaseState classifyNonRepeatingPhase(
    uint64_t elapsedUs, uint64_t periodUs, uint64_t activeUs,
    uint64_t uncertaintyUs)
{
    if (periodUs == 0 || activeUs == 0) {
        return VrrRasterPhaseState::Unclassified;
    }

    const uint64_t boundedUncertainty = std::min(
        uncertaintyUs, periodUs / 2);
    const uint64_t activeDistance = elapsedUs >= activeUs ?
        elapsedUs - activeUs : activeUs - elapsedUs;
    if (elapsedUs <= boundedUncertainty ||
            activeDistance <= boundedUncertainty) {
        return VrrRasterPhaseState::BoundaryUncertain;
    }
    return elapsedUs < activeUs ?
        VrrRasterPhaseState::Active : VrrRasterPhaseState::Inactive;
}

bool couldBeActive(VrrRasterPhaseState state)
{
    return state == VrrRasterPhaseState::Active ||
        state == VrrRasterPhaseState::BoundaryUncertain;
}

uint64_t scanoutPositionPpm(uint64_t phaseUs, uint64_t activeUs)
{
    if (activeUs == 0) {
        return 0;
    }
    const uint64_t boundedPhaseUs = std::min(phaseUs, activeUs);
    return saturatingMultiply(boundedPhaseUs, 1000000ULL) / activeUs;
}

uint64_t modularPhase(uint64_t sampleUs, uint64_t originUs,
                      uint64_t periodUs)
{
    if (sampleUs >= originUs) {
        return (sampleUs - originUs) % periodUs;
    }
    const uint64_t reversePhase = (originUs - sampleUs) % periodUs;
    return reversePhase == 0 ? 0 : periodUs - reversePhase;
}

uint64_t refreshBoundariesCrossed(uint64_t phaseUs, uint64_t elapsedUs,
                                  uint64_t periodUs)
{
    const uint64_t wholePeriods = elapsedUs / periodUs;
    const uint64_t remainderUs = elapsedUs % periodUs;
    return saturatingAdd(
        wholePeriods,
        phaseUs >= periodUs - remainderUs && remainderUs != 0 ? 1 : 0);
}

uint64_t addCircularPhase(uint64_t phase, uint64_t offset, uint64_t period)
{
    offset %= period;
    if (offset == 0) {
        return phase;
    }
    return phase >= period - offset ?
        phase - (period - offset) : phase + offset;
}

uint64_t subtractCircularPhase(uint64_t phase, uint64_t offset,
                               uint64_t period)
{
    offset %= period;
    return phase >= offset ? phase - offset : period - (offset - phase);
}

uint64_t circularDistance(uint64_t left, uint64_t right, uint64_t periodUs)
{
    const uint64_t direct = left >= right ? left - right : right - left;
    return std::min(direct, periodUs - direct);
}

uint64_t greatestCommonDivisor(uint64_t left, uint64_t right)
{
    while (right != 0) {
        const uint64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

uint64_t relativeErrorPpm(long double observed, long double expected)
{
    if (!(observed > 0.0L) || !(expected > 0.0L) ||
            !std::isfinite(observed) || !std::isfinite(expected)) {
        return std::numeric_limits<uint64_t>::max();
    }
    const long double errorPpm =
        std::fabs(observed - expected) / expected * 1000000.0L;
    if (!std::isfinite(errorPpm) ||
            errorPpm >= static_cast<long double>(
                std::numeric_limits<uint64_t>::max())) {
        return std::numeric_limits<uint64_t>::max();
    }
    return static_cast<uint64_t>(std::ceil(errorPpm));
}

void refreshDeltaBounds(uint64_t phase, uint64_t elapsed, uint64_t period,
                        uint64_t priorUncertainty,
                        uint64_t currentUncertainty, uint64_t central,
                        uint64_t& minimum, uint64_t& maximum)
{
    priorUncertainty = std::min(priorUncertainty, period / 2);
    currentUncertainty = std::min(currentUncertainty, period / 2);
    const uint64_t combinedUncertainty =
        saturatingAdd(priorUncertainty, currentUncertainty);

    // For the fewest possible refreshes, place the prior transition as late
    // and the current transition as early as their uncertainty permits. If
    // those windows overlap, zero elapsed time is a valid conservative bound.
    const uint64_t minimumElapsed =
        elapsed > combinedUncertainty ?
            elapsed - combinedUncertainty : 0;
    const uint64_t latestPriorPhase =
        addCircularPhase(phase, priorUncertainty, period);
    minimum = refreshBoundariesCrossed(
        latestPriorPhase, minimumElapsed, period);

    // For the most possible refreshes, place the prior transition as early
    // and the current transition as late. Saturation is intentionally
    // conservative for impossible uint64_t-scale inputs.
    const uint64_t maximumElapsed =
        saturatingAdd(elapsed, combinedUncertainty);
    const uint64_t earliestPriorPhase =
        subtractCircularPhase(phase, priorUncertainty, period);
    maximum = refreshBoundariesCrossed(
        earliestPriorPhase, maximumElapsed, period);

    // Keep the central estimate inside the reported envelope even at exact
    // circular-boundary endpoints.
    minimum = std::min(minimum, central);
    maximum = std::max(maximum, central);
}

uint64_t periodicProtectionWaitUs(
    uint64_t phaseUs, uint64_t periodUs, uint64_t activeUs,
    uint64_t uncertaintyUs, VrrRasterPhaseState state)
{
    if (!couldBeActive(state)) {
        return 0;
    }
    const uint64_t boundedUncertainty = std::min(
        uncertaintyUs, periodUs / 2);
    const uint64_t conservativeActiveEndUs = std::min(
        periodUs, saturatingAdd(activeUs, boundedUncertainty));
    if (phaseUs <= conservativeActiveEndUs) {
        return conservativeActiveEndUs - phaseUs;
    }
    if (periodUs - phaseUs <= boundedUncertainty) {
        return saturatingAdd(
            periodUs - phaseUs, conservativeActiveEndUs);
    }
    return 0;
}

bool qpcTickDeltaToMicroseconds(uint64_t tickDelta, uint64_t frequency,
                                uint64_t& microseconds)
{
    if (frequency == 0) {
        return false;
    }
    const uint64_t wholeSeconds = tickDelta / frequency;
    const uint64_t remainderTicks = tickDelta % frequency;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    if (wholeSeconds > maximum / 1000000ULL ||
            remainderTicks > maximum / 1000000ULL) {
        return false;
    }
    const uint64_t wholeUs = wholeSeconds * 1000000ULL;
    const uint64_t remainderUs =
        remainderTicks * 1000000ULL / frequency;
    if (remainderUs > maximum - wholeUs) {
        return false;
    }
    microseconds = wholeUs + remainderUs;
    return true;
}

bool qpcTickDeltaToMicrosecondsCeiling(
    uint64_t tickDelta, uint64_t frequency, uint64_t& microseconds)
{
    if (!qpcTickDeltaToMicroseconds(
            tickDelta, frequency, microseconds)) {
        return false;
    }
    const uint64_t remainderTicks = tickDelta % frequency;
    if (remainderTicks == 0 ||
            (remainderTicks * 1000000ULL) % frequency == 0) {
        return true;
    }
    if (microseconds == std::numeric_limits<uint64_t>::max()) {
        return false;
    }
    ++microseconds;
    return true;
}

bool microsecondsToPicoseconds(uint64_t microseconds, uint64_t& picoseconds)
{
    constexpr uint64_t picosecondsPerMicrosecond = 1000000ULL;
    if (microseconds >
            std::numeric_limits<uint64_t>::max() /
                picosecondsPerMicrosecond) {
        return false;
    }
    picoseconds = microseconds * picosecondsPerMicrosecond;
    return true;
}

uint64_t picosecondsToMicrosecondsRounded(uint64_t picoseconds)
{
    constexpr uint64_t picosecondsPerMicrosecond = 1000000ULL;
    return picoseconds / picosecondsPerMicrosecond +
        (picoseconds % picosecondsPerMicrosecond >=
                picosecondsPerMicrosecond / 2 ? 1ULL : 0ULL);
}

uint64_t picosecondsToMicrosecondsCeiling(uint64_t picoseconds)
{
    constexpr uint64_t picosecondsPerMicrosecond = 1000000ULL;
    return picoseconds / picosecondsPerMicrosecond +
        (picoseconds % picosecondsPerMicrosecond != 0 ? 1ULL : 0ULL);
}

struct ResolvedScanoutTiming {
    bool valid = false;
    bool activeScanoutClamped = false;
    bool phaseWindowInvalid = false;
    uint64_t periodUs = 0;
    uint64_t periodPs = 0;
    uint64_t activeUs = 0;
    uint64_t activePs = 0;
    uint64_t syncToActivePs = 0;
    uint64_t uncertaintyPs = 0;
};

ResolvedScanoutTiming resolveScanoutTiming(
    uint64_t recordedDisplayPeriodUs,
    const VrrReplayDisplayParameters& parameters)
{
    ResolvedScanoutTiming result;
    result.periodUs = parameters.scanoutPeriodUs != 0 ?
        parameters.scanoutPeriodUs : recordedDisplayPeriodUs;
    result.periodPs = parameters.scanoutPeriodPs;
    if (result.periodPs == 0) {
        if (result.periodUs == 0 ||
                !microsecondsToPicoseconds(
                    result.periodUs, result.periodPs)) {
            return result;
        }
    }
    if (result.periodUs == 0) {
        result.periodUs =
            picosecondsToMicrosecondsRounded(result.periodPs);
    }
    if (result.periodUs == 0 || result.periodPs == 0) {
        return result;
    }

    if (parameters.activeScanoutPs != 0) {
        result.activePs = parameters.activeScanoutPs;
        result.activeUs = parameters.activeScanoutUs != 0 ?
            parameters.activeScanoutUs :
            picosecondsToMicrosecondsRounded(result.activePs);
    }
    else if (parameters.activeScanoutUs != 0) {
        result.activeUs = parameters.activeScanoutUs;
        if (!microsecondsToPicoseconds(
                parameters.activeScanoutUs, result.activePs)) {
            return result;
        }
    }
    else {
        const uint64_t scaledActivePs = saturatingMultiply(
            result.periodPs, parameters.activeScanoutPercent);
        if (scaledActivePs == std::numeric_limits<uint64_t>::max()) {
            return result;
        }
        result.activePs = std::max<uint64_t>(
            1000000ULL, scaledActivePs / 100);
        result.activeUs = std::max<uint64_t>(
            1, picosecondsToMicrosecondsRounded(result.activePs));
    }
    result.activeScanoutClamped = result.activePs > result.periodPs;
    result.activePs = std::min(result.activePs, result.periodPs);
    result.activeUs = std::min(
        result.activeUs,
        picosecondsToMicrosecondsRounded(result.periodPs));

    if (!microsecondsToPicoseconds(
            parameters.syncToActiveScanoutUs,
            result.syncToActivePs) ||
            !microsecondsToPicoseconds(
                parameters.phaseUncertaintyUs,
                result.uncertaintyPs)) {
        return result;
    }
    result.uncertaintyPs = std::min(
        result.uncertaintyPs, result.periodPs / 2);
    // SyncQPCTime is a stable DXGI phase reference, but it is not guaranteed
    // to coincide with the start of physical active scanout. Allow the active
    // interval to wrap across that reference in the periodic hypothesis. The
    // ideal VRR hypothesis remains non-repeating and can begin active scanout
    // after the reference even when it extends past one nominal period.
    result.phaseWindowInvalid =
        result.syncToActivePs >= result.periodPs;
    result.valid = true;
    return result;
}

} // namespace

void VrrFreeRunningRefreshTracker::reset()
{
    m_HaveBaseline = false;
    m_Period = 0;
    m_PriorTransitionTime = 0;
    m_Phase = 0;
    m_PhaseUncertainty = 0;
}

VrrFreeRunningRefreshResult VrrFreeRunningRefreshTracker::observe(
    uint64_t transitionTime, uint64_t period,
    bool phaseReferenceValid, uint64_t phaseReference,
    uint64_t phaseUncertainty)
{
    VrrFreeRunningRefreshResult result;
    if (period == 0 ||
            (phaseReferenceValid && phaseReference >= period)) {
        reset();
        return result;
    }

    if (m_HaveBaseline &&
            (transitionTime < m_PriorTransitionTime ||
             period != m_Period)) {
        result.timeRegression =
            transitionTime < m_PriorTransitionTime;
        result.periodChanged = period != m_Period;
        reset();
    }

    if (!m_HaveBaseline) {
        if (!phaseReferenceValid) {
            return result;
        }
        m_HaveBaseline = true;
        m_Period = period;
        m_PriorTransitionTime = transitionTime;
        m_Phase = phaseReference;
        m_PhaseUncertainty = std::min(
            phaseUncertainty, period / 2);
        result.baselineEstablished = true;
        result.propagatedPhase = m_Phase;
        return result;
    }

    const uint64_t elapsed =
        transitionTime - m_PriorTransitionTime;
    result.refreshDelta =
        refreshBoundariesCrossed(m_Phase, elapsed, m_Period);
    refreshDeltaBounds(
        m_Phase, elapsed, m_Period, m_PhaseUncertainty,
        phaseUncertainty, result.refreshDelta,
        result.refreshDeltaLower, result.refreshDeltaUpper);
    const uint64_t remainder = elapsed % m_Period;
    m_Phase = addCircularPhase(m_Phase, remainder, m_Period);
    m_PhaseUncertainty = std::min(
        phaseUncertainty, m_Period / 2);
    m_PriorTransitionTime = transitionTime;

    result.compared = true;
    result.propagatedPhase = m_Phase;
    result.scanoutAnomalyLower =
        result.refreshDeltaUpper == 0 ? 1 : 0;
    result.scanoutAnomaly =
        result.refreshDelta == 0 ? 1 : 0;
    result.scanoutAnomalyUpper =
        result.refreshDeltaLower == 0 ? 1 : 0;
    result.repeatedRefreshLower =
        result.refreshDeltaLower > 1 ?
            result.refreshDeltaLower - 1 : 0;
    result.repeatedRefresh =
        result.refreshDelta > 1 ? result.refreshDelta - 1 : 0;
    result.repeatedRefreshUpper =
        result.refreshDeltaUpper > 1 ?
            result.refreshDeltaUpper - 1 : 0;
    if (phaseReferenceValid) {
        result.phaseReferenceCompared = true;
        result.phaseReferenceDifference = circularDistance(
            m_Phase, phaseReference, m_Period);
    }
    return result;
}

void VrrRawQpcTranslationTracker::reset()
{
    m_HaveTranslatedSample = false;
    m_Frequency = 0;
    m_PriorRawTicks = 0;
    m_PriorTranslatedUs = 0;
}

VrrRawQpcTranslationResult VrrRawQpcTranslationTracker::observe(
    uint64_t rawTicks, uint64_t frequency, bool translatedValid,
    uint64_t translatedUs, uint64_t toleranceUs)
{
    VrrRawQpcTranslationResult result;
    if (m_Frequency == 0) {
        m_Frequency = frequency;
    }
    else if (frequency != m_Frequency) {
        result.frequencyMismatch = true;
    }

    if (!translatedValid) {
        return result;
    }
    if (!m_HaveTranslatedSample) {
        result.baselineEstablished = true;
    }
    else {
        result.compared = true;
        bool translationValid =
            !result.frequencyMismatch &&
            rawTicks >= m_PriorRawTicks &&
            translatedUs >= m_PriorTranslatedUs;
        uint64_t expectedElapsedUs = 0;
        if (translationValid) {
            translationValid = qpcTickDeltaToMicroseconds(
                rawTicks - m_PriorRawTicks, frequency,
                expectedElapsedUs);
        }
        if (translationValid) {
            const uint64_t translatedElapsedUs =
                translatedUs - m_PriorTranslatedUs;
            const uint64_t differenceUs =
                translatedElapsedUs >= expectedElapsedUs ?
                    translatedElapsedUs - expectedElapsedUs :
                    expectedElapsedUs - translatedElapsedUs;
            translationValid = differenceUs <= toleranceUs;
        }
        result.translationMismatch = !translationValid;
    }

    m_HaveTranslatedSample = true;
    m_PriorRawTicks = rawTicks;
    m_PriorTranslatedUs = translatedUs;
    return result;
}

VrrQpcCorrelationResult evaluateVrrQpcCorrelation(
    uint64_t rawTicks, uint64_t frequency, uint64_t translatedTimeUs,
    uint64_t referenceTicks, uint64_t referenceTimeUs,
    uint64_t bracketSpanTicks, uint64_t toleranceUs)
{
    VrrQpcCorrelationResult result;
    if (frequency == 0 || rawTicks == 0 || referenceTicks == 0) {
        return result;
    }

    const uint64_t deltaTicks = rawTicks >= referenceTicks ?
        rawTicks - referenceTicks : referenceTicks - rawTicks;
    uint64_t deltaUs = 0;
    if (!qpcTickDeltaToMicroseconds(
            deltaTicks, frequency, deltaUs)) {
        return result;
    }
    if (rawTicks >= referenceTicks) {
        if (deltaUs >
                std::numeric_limits<uint64_t>::max() -
                    referenceTimeUs) {
            return result;
        }
        result.expectedTimeUs = referenceTimeUs + deltaUs;
    }
    else {
        if (deltaUs > referenceTimeUs) {
            return result;
        }
        result.expectedTimeUs = referenceTimeUs - deltaUs;
    }
    result.translated = true;
    const uint64_t differenceUs =
        translatedTimeUs >= result.expectedTimeUs ?
            translatedTimeUs - result.expectedTimeUs :
            result.expectedTimeUs - translatedTimeUs;
    result.matches = differenceUs <= toleranceUs;

    const uint64_t halfSpanTicks =
        bracketSpanTicks / 2 +
        (bracketSpanTicks % 2);
    result.uncertaintyValid = qpcTickDeltaToMicrosecondsCeiling(
        halfSpanTicks, frequency, result.halfSpanUncertaintyUs);
    return result;
}

bool vrrPeriodPicosecondsFromRefreshRational(
    uint64_t refreshNumerator, uint64_t refreshDenominator,
    uint64_t& periodPicoseconds)
{
    periodPicoseconds = 0;
    if (refreshNumerator == 0 || refreshDenominator == 0) {
        return false;
    }
    const uint64_t divisor = greatestCommonDivisor(
        refreshNumerator, refreshDenominator);
    refreshNumerator /= divisor;
    refreshDenominator /= divisor;
    constexpr uint64_t picosecondsPerSecond = 1000000000000ULL;
    if (refreshDenominator >
            std::numeric_limits<uint64_t>::max() /
                picosecondsPerSecond) {
        return false;
    }
    const uint64_t scaledDenominator =
        refreshDenominator * picosecondsPerSecond;
    periodPicoseconds = scaledDenominator / refreshNumerator;
    const uint64_t remainder =
        scaledDenominator % refreshNumerator;
    if (remainder >=
            refreshNumerator / 2 + refreshNumerator % 2) {
        if (periodPicoseconds ==
                std::numeric_limits<uint64_t>::max()) {
            return false;
        }
        ++periodPicoseconds;
    }
    return periodPicoseconds != 0;
}

bool vrrActiveScanoutPicosecondsFromSignal(
    uint64_t refreshNumerator, uint64_t refreshDenominator,
    uint64_t activeWidth, uint64_t activeHeight,
    uint64_t totalWidth, uint64_t totalHeight,
    uint64_t& activeScanoutPicoseconds)
{
    activeScanoutPicoseconds = 0;
    if (activeWidth == 0 || activeHeight == 0 ||
            totalWidth == 0 || totalHeight == 0 ||
            activeWidth > totalWidth ||
            activeHeight > totalHeight) {
        return false;
    }

    uint64_t periodPicoseconds = 0;
    if (!vrrPeriodPicosecondsFromRefreshRational(
            refreshNumerator, refreshDenominator,
            periodPicoseconds)) {
        return false;
    }

    const uint64_t maximum =
        std::numeric_limits<uint64_t>::max();
    if (totalHeight > maximum / totalWidth ||
            activeHeight - 1 > maximum / totalWidth) {
        return false;
    }
    const uint64_t totalPixelClocks =
        totalWidth * totalHeight;
    const uint64_t priorActiveLineClocks =
        (activeHeight - 1) * totalWidth;
    if (activeWidth > maximum - priorActiveLineClocks) {
        return false;
    }
    // Starting at the first active pixel, each complete intervening scan line
    // consumes totalWidth clocks. The visible interval ends at the last active
    // pixel of the final active line, before that line's trailing horizontal
    // blank. Using activeHeight/totalHeight alone incorrectly includes the
    // final horizontal blank and overstates the tear-exposure window.
    const uint64_t activePixelClocks =
        priorActiveLineClocks + activeWidth;
    const uint64_t wholeClocks =
        periodPicoseconds / totalPixelClocks;
    if (wholeClocks > maximum / activePixelClocks) {
        return false;
    }
    uint64_t activePicoseconds =
        wholeClocks * activePixelClocks;
    const uint64_t periodRemainder =
        periodPicoseconds % totalPixelClocks;
    if (periodRemainder >
            maximum / activePixelClocks) {
        return false;
    }
    const uint64_t fractionalNumerator =
        periodRemainder * activePixelClocks;
    const uint64_t fractionalPicoseconds =
        fractionalNumerator / totalPixelClocks;
    if (fractionalPicoseconds >
            maximum - activePicoseconds) {
        return false;
    }
    activePicoseconds += fractionalPicoseconds;
    const uint64_t fractionalRemainder =
        fractionalNumerator % totalPixelClocks;
    if (fractionalRemainder >=
            totalPixelClocks / 2 +
                totalPixelClocks % 2) {
        if (activePicoseconds == maximum) {
            return false;
        }
        ++activePicoseconds;
    }

    activeScanoutPicoseconds = activePicoseconds;
    return activeScanoutPicoseconds != 0;
}

bool vrrActiveScanoutMicrosecondsFromSignal(
    uint64_t refreshNumerator, uint64_t refreshDenominator,
    uint64_t activeWidth, uint64_t activeHeight,
    uint64_t totalWidth, uint64_t totalHeight,
    uint64_t& activeScanoutMicroseconds)
{
    activeScanoutMicroseconds = 0;
    uint64_t activeScanoutPicoseconds = 0;
    if (!vrrActiveScanoutPicosecondsFromSignal(
            refreshNumerator, refreshDenominator,
            activeWidth, activeHeight,
            totalWidth, totalHeight,
            activeScanoutPicoseconds)) {
        return false;
    }
    activeScanoutMicroseconds =
        picosecondsToMicrosecondsRounded(activeScanoutPicoseconds);
    return activeScanoutMicroseconds != 0;
}

bool vrrRefreshRationalsEqual(
    uint64_t leftNumerator, uint64_t leftDenominator,
    uint64_t rightNumerator, uint64_t rightDenominator)
{
    if (leftNumerator == 0 || leftDenominator == 0 ||
            rightNumerator == 0 || rightDenominator == 0) {
        return false;
    }
    const uint64_t leftDivisor = greatestCommonDivisor(
        leftNumerator, leftDenominator);
    const uint64_t rightDivisor = greatestCommonDivisor(
        rightNumerator, rightDenominator);
    return leftNumerator / leftDivisor ==
            rightNumerator / rightDivisor &&
        leftDenominator / leftDivisor ==
        rightDenominator / rightDivisor;
}

VrrRasterSyncAnchorMergeResult evaluateVrrRasterSyncAnchorMerge(
    bool havePrevious,
    uint64_t previousRefreshSequence, uint64_t previousTimeUs,
    uint64_t refreshSequence, uint64_t timeUs,
    uint64_t recordedDisplayPeriodUs,
    uint64_t timestampJitterToleranceUs,
    uint64_t minimumIntervalToleranceUs)
{
    VrrRasterSyncAnchorMergeResult result;
    if (!havePrevious) {
        result.accepted = true;
        result.status = VrrRasterSyncAnchorMergeStatus::Appended;
        return result;
    }
    if (refreshSequence < previousRefreshSequence) {
        result.status =
            VrrRasterSyncAnchorMergeStatus::SequenceRegression;
        return result;
    }
    if (refreshSequence == previousRefreshSequence) {
        const uint64_t timestampDifferenceUs =
            timeUs >= previousTimeUs ?
                timeUs - previousTimeUs :
                previousTimeUs - timeUs;
        if (timestampDifferenceUs >
                timestampJitterToleranceUs) {
            result.status =
                VrrRasterSyncAnchorMergeStatus::
                    SameRefreshTimestampMismatch;
            return result;
        }
        result.accepted = true;
        result.replacesPrevious = true;
        result.status =
            VrrRasterSyncAnchorMergeStatus::ReplacedSameRefresh;
        return result;
    }
    if (timeUs <= previousTimeUs) {
        result.status =
            VrrRasterSyncAnchorMergeStatus::NonadvancingTimestamp;
        return result;
    }
    const uint64_t refreshDelta =
        refreshSequence - previousRefreshSequence;
    const uint64_t meanIntervalUs =
        (timeUs - previousTimeUs) / refreshDelta;
    if (recordedDisplayPeriodUs != 0 &&
            saturatingAdd(
                meanIntervalUs,
                minimumIntervalToleranceUs) <
                recordedDisplayPeriodUs) {
        result.status =
            VrrRasterSyncAnchorMergeStatus::
                ImplausiblyShortInterval;
        return result;
    }
    result.accepted = true;
    result.status = VrrRasterSyncAnchorMergeStatus::Appended;
    return result;
}

VrrRasterScanLineAudit evaluateVrrRasterScanLine(
    bool observedInVerticalBlank,
    VrrRasterPhaseState predictedState,
    bool predictedPhaseValid, uint64_t predictedPhasePs,
    uint64_t periodPs, uint64_t phaseUncertaintyPs,
    uint64_t activeHeight, uint64_t totalHeight,
    uint64_t observedScanLine)
{
    VrrRasterScanLineAudit result;
    result.observedScanLine = observedScanLine;
    if (observedInVerticalBlank ||
            predictedState != VrrRasterPhaseState::Active ||
            !predictedPhaseValid ||
            periodPs == 0 ||
            predictedPhasePs >= periodPs ||
            activeHeight == 0 ||
            totalHeight == 0 ||
            activeHeight > totalHeight ||
            observedScanLine >= activeHeight) {
        return result;
    }

    const uint64_t maximum =
        std::numeric_limits<uint64_t>::max();
    if (predictedPhasePs > maximum / totalHeight ||
            phaseUncertaintyPs > maximum / totalHeight) {
        return result;
    }
    result.predictedScanLine =
        predictedPhasePs * totalHeight / periodPs;
    if (result.predictedScanLine >= activeHeight) {
        return result;
    }

    const uint64_t uncertaintyLineNumerator =
        phaseUncertaintyPs * totalHeight;
    result.toleranceLines =
        uncertaintyLineNumerator / periodPs +
        (uncertaintyLineNumerator % periodPs != 0 ? 1ULL : 0ULL);
    // The integer scan-line index changes at a line boundary. Include one
    // line for that quantization after the caller has already included the
    // timestamp and native-query bracket uncertainty in phaseUncertaintyPs.
    if (result.toleranceLines != maximum) {
        ++result.toleranceLines;
    }
    result.absoluteErrorLines =
        result.predictedScanLine >= result.observedScanLine ?
            result.predictedScanLine - result.observedScanLine :
            result.observedScanLine - result.predictedScanLine;
    const uint64_t maximumSigned = static_cast<uint64_t>(
        std::numeric_limits<int64_t>::max());
    if (result.observedScanLine >= result.predictedScanLine) {
        result.signedErrorLines =
            result.absoluteErrorLines > maximumSigned ?
                std::numeric_limits<int64_t>::max() :
                static_cast<int64_t>(result.absoluteErrorLines);
    }
    else {
        result.signedErrorLines =
            result.absoluteErrorLines > maximumSigned ?
                std::numeric_limits<int64_t>::min() :
                -static_cast<int64_t>(result.absoluteErrorLines);
    }
    result.comparable = true;
    result.matches =
        result.absoluteErrorLines <= result.toleranceLines;
    return result;
}

bool addVrrRasterScanLineScaleSample(
    VrrRasterScanLineScaleInference& inference,
    bool observedInVerticalBlank, uint64_t observedScanLine,
    uint64_t activeHeight, uint64_t totalHeight)
{
    if (!inference.valid ||
            activeHeight == 0 ||
            totalHeight == 0 ||
            activeHeight > totalHeight) {
        inference.valid = false;
        return false;
    }
    const uint64_t lineLimit =
        observedInVerticalBlank ? totalHeight : activeHeight;
    const uint64_t requiredScale =
        observedScanLine / lineLimit + 1;
    inference.scale = std::max(inference.scale, requiredScale);
    inference.maximumObservedScanLine =
        std::max(
            inference.maximumObservedScanLine,
            observedScanLine);
    ++inference.samples;
    return true;
}

bool normalizeVrrRasterScanLine(
    uint64_t observedScanLine, uint64_t scale,
    uint64_t& normalizedScanLine)
{
    if (scale == 0) {
        normalizedScanLine = 0;
        return false;
    }
    normalizedScanLine = observedScanLine / scale;
    return true;
}

VrrDisplaySignalConsistency evaluateVrrDisplaySignalConsistency(
    uint64_t pixelRateHz,
    uint64_t hsyncNumerator, uint64_t hsyncDenominator,
    uint64_t vsyncNumerator, uint64_t vsyncDenominator,
    uint64_t totalWidth, uint64_t totalHeight,
    uint64_t tolerancePpm)
{
    VrrDisplaySignalConsistency result;
    if (pixelRateHz == 0 ||
            hsyncNumerator == 0 || hsyncDenominator == 0 ||
            vsyncNumerator == 0 || vsyncDenominator == 0 ||
            totalWidth == 0 || totalHeight == 0) {
        return result;
    }

    const long double pixelRate =
        static_cast<long double>(pixelRateHz);
    const long double hsync =
        static_cast<long double>(hsyncNumerator) /
        static_cast<long double>(hsyncDenominator);
    const long double vsync =
        static_cast<long double>(vsyncNumerator) /
        static_cast<long double>(vsyncDenominator);
    const long double expectedHsync =
        pixelRate / static_cast<long double>(totalWidth);
    const long double expectedVsyncFromHsync =
        hsync / static_cast<long double>(totalHeight);
    const long double expectedVsyncFromPixelRate =
        pixelRate /
        (static_cast<long double>(totalWidth) *
         static_cast<long double>(totalHeight));

    result.inputsValid =
        std::isfinite(hsync) && std::isfinite(vsync) &&
        std::isfinite(expectedHsync) &&
        std::isfinite(expectedVsyncFromHsync) &&
        std::isfinite(expectedVsyncFromPixelRate);
    if (!result.inputsValid) {
        return result;
    }
    result.pixelRateToHsyncErrorPpm =
        relativeErrorPpm(hsync, expectedHsync);
    result.hsyncToVsyncErrorPpm =
        relativeErrorPpm(vsync, expectedVsyncFromHsync);
    result.pixelRateToVsyncErrorPpm =
        relativeErrorPpm(vsync, expectedVsyncFromPixelRate);
    result.withinTolerance =
        result.pixelRateToHsyncErrorPpm <= tolerancePpm &&
        result.hsyncToVsyncErrorPpm <= tolerancePpm &&
        result.pixelRateToVsyncErrorPpm <= tolerancePpm;
    return result;
}

VrrGpuCompletionBounds evaluateVrrGpuCompletionBounds(
    uint64_t preparationStartUs, uint64_t preparationEndUs,
    uint64_t signalStartUs,
    uint64_t pollStartUs, uint64_t pollEndUs,
    uint64_t fenceValue, uint64_t pollCompletedValue,
    bool completedBeforeWait,
    uint64_t waitStartUs, uint64_t waitReturnUs)
{
    VrrGpuCompletionBounds result;
    result.fenceRelationshipValid =
        fenceValue != 0 &&
        pollCompletedValue !=
            std::numeric_limits<uint64_t>::max() &&
        pollCompletedValue <= fenceValue &&
        fenceValue - pollCompletedValue <= 1 &&
        completedBeforeWait ==
            (pollCompletedValue >= fenceValue);
    if (preparationStartUs == 0 ||
            preparationEndUs < preparationStartUs ||
            signalStartUs < preparationStartUs ||
            pollStartUs < signalStartUs ||
            pollEndUs < pollStartUs ||
            waitStartUs < pollEndUs ||
            waitReturnUs < waitStartUs ||
            waitReturnUs > preparationEndUs ||
            !result.fenceRelationshipValid) {
        return result;
    }

    result.lowerBoundUs = completedBeforeWait ?
        signalStartUs : pollStartUs;
    result.upperBoundUs = completedBeforeWait ?
        pollEndUs : waitReturnUs;
    if (result.upperBoundUs < result.lowerBoundUs) {
        return result;
    }
    result.uncertaintyUs =
        result.upperBoundUs - result.lowerBoundUs;
    result.valid = true;
    return result;
}

VrrGpuReadyOperationAudit evaluateVrrGpuReadyOperation(
    bool attempted,
    bool signalResultValid, int64_t signalResult,
    bool setEventResultValid, int64_t setEventResult,
    bool waitResultValid, uint64_t waitResult,
    bool timingValid, uint64_t signalStartUs, uint64_t fenceValue)
{
    VrrGpuReadyOperationAudit result;
    // Signal() and SetEventOnCompletion() follow HRESULT semantics in the
    // production path: any nonnegative result advances to the next stage.
    // Strict diagnostic success remains narrower and accepts only S_OK.
    result.signalSucceeded =
        attempted && signalResultValid && signalResult >= 0;
    result.setEventSucceeded =
        result.signalSucceeded &&
        setEventResultValid &&
        setEventResult >= 0;
    result.waitSucceeded =
        result.setEventSucceeded &&
        waitResultValid &&
        waitResult == 0;
    result.relationshipValid =
        signalResultValid == attempted &&
        setEventResultValid == result.signalSucceeded &&
        waitResultValid == result.setEventSucceeded &&
        timingValid == result.waitSucceeded &&
        (!attempted || (signalStartUs != 0 && fenceValue != 0));
    result.exactSuccess =
        result.relationshipValid &&
        attempted &&
        signalResult == 0 &&
        setEventResult == 0 &&
        waitResult == 0 &&
        timingValid;
    return result;
}

VrrGpuReadyStageTimingAudit evaluateVrrGpuReadyStageTiming(
    uint64_t preparationStartUs, uint64_t preparationEndUs,
    bool attempted, bool signalSucceeded, bool setEventSucceeded,
    uint64_t signalStartUs, uint64_t signalEndUs,
    uint64_t flushStartUs, uint64_t flushEndUs,
    uint64_t setEventStartUs, uint64_t setEventEndUs,
    uint64_t pollStartUs, uint64_t pollEndUs,
    uint64_t waitStartUs, uint64_t waitReturnUs)
{
    VrrGpuReadyStageTimingAudit result;
    const bool signalTimingPresent =
        signalStartUs != 0 || signalEndUs != 0;
    const bool flushTimingPresent =
        flushStartUs != 0 || flushEndUs != 0;
    const bool setEventTimingPresent =
        setEventStartUs != 0 || setEventEndUs != 0;
    const bool pollWaitTimingPresent =
        pollStartUs != 0 || pollEndUs != 0 ||
        waitStartUs != 0 || waitReturnUs != 0;
    if (!attempted) {
        result.relationshipValid =
            !signalTimingPresent &&
            !flushTimingPresent &&
            !setEventTimingPresent &&
            !pollWaitTimingPresent;
        return result;
    }

    const bool preparationValid =
        preparationStartUs != 0 &&
        preparationEndUs >= preparationStartUs;
    const bool signalTimingValid =
        signalStartUs != 0 &&
        signalEndUs >= signalStartUs &&
        signalStartUs >= preparationStartUs &&
        signalEndUs <= preparationEndUs;
    if (!signalSucceeded) {
        result.relationshipValid =
            preparationValid &&
            signalTimingValid &&
            !flushTimingPresent &&
            !setEventTimingPresent &&
            !pollWaitTimingPresent;
        return result;
    }

    const bool flushTimingValid =
        flushStartUs != 0 &&
        flushEndUs >= flushStartUs &&
        flushStartUs >= signalEndUs &&
        flushEndUs <= preparationEndUs;
    const bool setEventTimingValid =
        setEventStartUs != 0 &&
        setEventEndUs >= setEventStartUs &&
        setEventStartUs >= flushEndUs &&
        setEventEndUs <= preparationEndUs;
    if (!setEventSucceeded) {
        result.relationshipValid =
            preparationValid &&
            signalTimingValid &&
            flushTimingValid &&
            setEventTimingValid &&
            !pollWaitTimingPresent;
        return result;
    }

    const bool pollWaitTimingValid =
        pollStartUs != 0 &&
        pollEndUs >= pollStartUs &&
        pollStartUs >= setEventEndUs &&
        waitStartUs >= pollEndUs &&
        waitReturnUs >= waitStartUs &&
        waitReturnUs <= preparationEndUs;
    result.relationshipValid =
        preparationValid &&
        signalTimingValid &&
        flushTimingValid &&
        setEventTimingValid &&
        pollWaitTimingValid;
    return result;
}

VrrPresenterSubmissionAudit evaluateVrrPresenterSubmission(
    bool presented,
    bool presenterTimeValid, uint64_t presenterTimeUs,
    uint64_t operationStartUs, uint64_t operationEndUs,
    bool recordedPresenterTimeUsed,
    uint64_t recordedSubmissionBoundaryUs)
{
    VrrPresenterSubmissionAudit result;
    result.expectedPresenterTimeUsed =
        presented &&
        presenterTimeValid &&
        operationStartUs <= operationEndUs &&
        presenterTimeUs >= operationStartUs &&
        presenterTimeUs <= operationEndUs;
    result.expectedSubmissionBoundaryUs = !presented ? 0 :
        (result.expectedPresenterTimeUsed ?
            presenterTimeUs : operationStartUs);
    result.relationshipValid =
        recordedPresenterTimeUsed ==
            result.expectedPresenterTimeUsed &&
        recordedSubmissionBoundaryUs ==
            result.expectedSubmissionBoundaryUs;
    return result;
}

VrrSpacingCorrectionAudit evaluateVrrSpacingCorrection(
    bool normalPresentationLifecycle,
    uint64_t spacingDeficitUs, uint64_t spacingGuardFeedbackUs,
    bool spacingCorrected, bool correctionWaitPresent)
{
    VrrSpacingCorrectionAudit result;
    result.relationshipValid =
        spacingCorrected == (spacingDeficitUs != 0) &&
        spacingGuardFeedbackUs <= spacingDeficitUs &&
        (spacingGuardFeedbackUs == 0 ||
         normalPresentationLifecycle) &&
        correctionWaitPresent ==
            (spacingGuardFeedbackUs != 0);
    return result;
}

VrrSpacingLifecycleTimingAudit evaluateVrrSpacingLifecycleTiming(
    bool normalPresentationLifecycle, bool hadPriorSubmission,
    uint64_t priorSubmissionUs, uint64_t displayPeriodUs,
    uint64_t targetUs, uint64_t earliestSubmissionBeforeCleanUs,
    uint64_t earliestSubmissionAfterFeedbackUs,
    uint64_t spacingCheckUs, uint64_t presentationFloorUs,
    uint64_t spacingRecheckUs, uint64_t spacingDeficitUs,
    uint64_t spacingGuardFeedbackUs, bool spacingCorrected,
    uint64_t spacingCorrectedFloorUs,
    uint64_t correctionWaitStartUs, uint64_t correctionWaitEndUs,
    uint64_t presentOperationStartUs)
{
    VrrSpacingLifecycleTimingAudit result;
    if (!normalPresentationLifecycle) {
        result.relationshipValid =
            spacingRecheckUs == 0 &&
            spacingCorrectedFloorUs == 0;
        return result;
    }

    result.expectedPresentationFloorUs =
        std::max(targetUs, earliestSubmissionBeforeCleanUs);
    if (earliestSubmissionBeforeCleanUs != 0 &&
            spacingCheckUs < earliestSubmissionBeforeCleanUs) {
        result.expectedFirstCheckDeficitUs =
            earliestSubmissionBeforeCleanUs - spacingCheckUs;
    }
    if (hadPriorSubmission) {
        const uint64_t minimumUntornUs =
            saturatingAdd(priorSubmissionUs, displayPeriodUs);
        if (spacingRecheckUs < minimumUntornUs) {
            result.expectedRecheckDeficitUs =
                minimumUntornUs - spacingRecheckUs;
        }
    }
    result.expectedSpacingDeficitUs = std::max(
        result.expectedFirstCheckDeficitUs,
        result.expectedRecheckDeficitUs);
    const bool expectedCorrectionWait =
        result.expectedRecheckDeficitUs != 0;
    const bool correctionWaitValid = expectedCorrectionWait ?
        correctionWaitStartUs >= spacingRecheckUs &&
            correctionWaitEndUs >= correctionWaitStartUs &&
            correctionWaitEndUs >= earliestSubmissionAfterFeedbackUs :
        correctionWaitStartUs == 0 &&
            correctionWaitEndUs == 0;
    const bool correctedFloorValid = expectedCorrectionWait ?
        // A latched policy can intentionally disable its software floor.
        // The worker still records the recheck deficit and a wait at zero;
        // require the controller's actual floor, including that valid zero.
        spacingCorrectedFloorUs ==
            earliestSubmissionAfterFeedbackUs :
        spacingCorrectedFloorUs == 0;
    const uint64_t finalSpacingBoundaryUs = expectedCorrectionWait ?
        correctionWaitEndUs : spacingRecheckUs;
    result.relationshipValid =
        spacingCheckUs != 0 &&
        presentationFloorUs ==
            result.expectedPresentationFloorUs &&
        spacingRecheckUs >= spacingCheckUs &&
        spacingRecheckUs >= presentationFloorUs &&
        spacingDeficitUs ==
            result.expectedSpacingDeficitUs &&
        spacingGuardFeedbackUs ==
            result.expectedRecheckDeficitUs &&
        spacingCorrected ==
            (result.expectedSpacingDeficitUs != 0) &&
        correctedFloorValid &&
        correctionWaitValid &&
        presentOperationStartUs >= finalSpacingBoundaryUs;
    return result;
}

VrrPostPresentQueryTimingAudit evaluateVrrPostPresentQueryTiming(
    bool expectedTiming,
    bool submissionIdQueryResultValid,
    bool frameStatsQueryResultValid,
    uint64_t priorObservationEndUs,
    uint64_t submissionIdQueryStartUs,
    uint64_t submissionIdQueryEndUs,
    uint64_t frameStatsQueryStartUs,
    uint64_t frameStatsQueryEndUs,
    uint64_t presentOperationEndUs)
{
    VrrPostPresentQueryTimingAudit result;
    result.expectedTiming = expectedTiming;
    if (!expectedTiming) {
        result.relationshipValid =
            submissionIdQueryStartUs == 0 &&
            submissionIdQueryEndUs == 0 &&
            frameStatsQueryStartUs == 0 &&
            frameStatsQueryEndUs == 0;
        return result;
    }

    result.relationshipValid =
        submissionIdQueryResultValid &&
        frameStatsQueryResultValid &&
        priorObservationEndUs != 0 &&
        submissionIdQueryStartUs >= priorObservationEndUs &&
        submissionIdQueryEndUs >= submissionIdQueryStartUs &&
        frameStatsQueryStartUs >= submissionIdQueryEndUs &&
        frameStatsQueryEndUs >= frameStatsQueryStartUs &&
        presentOperationEndUs >= frameStatsQueryEndUs;
    return result;
}

VrrRasterPhaseResult evaluateVrrRasterPhase(
    bool presented, bool latched, uint64_t submissionUs,
    uint64_t prePresentSyncSampleUs, uint64_t recordedDisplayPeriodUs,
    uint64_t sourcePeriodUs, const VrrReplayDisplayParameters& parameters)
{
    VrrRasterPhaseResult result;
    if (!presented) {
        return result;
    }
    result.modeledTransitionUs = saturatingAdd(
        submissionUs, parameters.presentTransportUs);
    if (latched) {
        result.envelope = VrrRasterEnvelopeClass::LatchedSuppressed;
        return result;
    }

    const ResolvedScanoutTiming timing = resolveScanoutTiming(
        recordedDisplayPeriodUs, parameters);
    result.resolvedScanoutPeriodUs = timing.periodUs;
    result.resolvedScanoutPeriodPs = timing.periodPs;
    result.resolvedActiveScanoutUs = timing.activeUs;
    result.resolvedActiveScanoutPs = timing.activePs;
    result.resolvedSyncToActiveScanoutUs =
        parameters.syncToActiveScanoutUs;
    result.resolvedPhaseUncertaintyPs = timing.uncertaintyPs;
    result.activeScanoutClamped = timing.activeScanoutClamped;
    result.scanoutPhaseWindowInvalid = timing.phaseWindowInvalid;
    if (!timing.valid || timing.phaseWindowInvalid) {
        return result;
    }

    const uint64_t automaticMaxAgeUs = std::max(
        saturatingMultiply(result.resolvedScanoutPeriodUs, 8),
        saturatingMultiply(sourcePeriodUs, 2));
    result.resolvedAnchorMaxAgeUs = parameters.anchorMaxAgeUs != 0 ?
        parameters.anchorMaxAgeUs : automaticMaxAgeUs;

    const uint64_t modeledSubmissionUs = result.modeledTransitionUs;
    if (prePresentSyncSampleUs == 0 ||
            modeledSubmissionUs < prePresentSyncSampleUs) {
        return result;
    }
    result.anchorTimeUs = prePresentSyncSampleUs;
    result.anchorAgeUs = modeledSubmissionUs - prePresentSyncSampleUs;
    if (result.resolvedAnchorMaxAgeUs != 0 &&
            result.anchorAgeUs > result.resolvedAnchorMaxAgeUs) {
        return result;
    }

    uint64_t anchorAgePs = 0;
    if (!microsecondsToPicoseconds(
            result.anchorAgeUs, anchorAgePs)) {
        return result;
    }
    result.freeRunningPhasePs = modularPhase(
        anchorAgePs, timing.syncToActivePs, timing.periodPs);
    result.freeRunningPhaseValid = true;
    result.freeRunningPhaseUs =
        result.freeRunningPhasePs / 1000000ULL;
    result.freeRunning = classifyPhase(
        result.freeRunningPhasePs, timing.periodPs,
        timing.activePs, timing.uncertaintyPs);

    // In the ideal VRR flip-following envelope, the raster performs one
    // active scan after the sampled v-blank and then remains in an extended
    // blank until the next transition. There is deliberately no modulo and
    // no artificial boundary at the nominal period.
    uint64_t vrrLockedPhasePs = 0;
    if (anchorAgePs < timing.syncToActivePs) {
        const uint64_t leadPs =
            timing.syncToActivePs - anchorAgePs;
        result.vrrLocked =
            leadPs <= timing.uncertaintyPs ?
                VrrRasterPhaseState::BoundaryUncertain :
                VrrRasterPhaseState::Inactive;
        if (result.vrrLocked ==
                VrrRasterPhaseState::BoundaryUncertain) {
            const uint64_t waitPs = saturatingAdd(
                leadPs,
                std::min(
                    timing.periodPs,
                    saturatingAdd(
                        timing.activePs,
                        timing.uncertaintyPs)));
            result.vrrLockedWaitUs =
                picosecondsToMicrosecondsCeiling(waitPs);
        }
    }
    else {
        vrrLockedPhasePs =
            anchorAgePs - timing.syncToActivePs;
        result.vrrLockedPhaseValid = true;
        result.vrrLockedPhasePs = vrrLockedPhasePs;
        result.vrrLocked = classifyNonRepeatingPhase(
            vrrLockedPhasePs, timing.periodPs,
            timing.activePs, timing.uncertaintyPs);
    }

    if (couldBeActive(result.vrrLocked) &&
            anchorAgePs >= timing.syncToActivePs) {
        const uint64_t conservativeActiveEndPs = std::min(
            timing.periodPs,
            saturatingAdd(
                timing.activePs, timing.uncertaintyPs));
        if (vrrLockedPhasePs <= conservativeActiveEndPs) {
            result.vrrLockedWaitUs =
                picosecondsToMicrosecondsCeiling(
                    conservativeActiveEndPs -
                        vrrLockedPhasePs);
        }
    }
    if (couldBeActive(result.vrrLocked) &&
            anchorAgePs >= timing.syncToActivePs &&
            vrrLockedPhasePs <= timing.activePs) {
        result.vrrLockedScanoutPositionValid = true;
        result.vrrLockedScanoutPositionPpm = scanoutPositionPpm(
            vrrLockedPhasePs, timing.activePs);
    }
    result.freeRunningWaitUs = picosecondsToMicrosecondsCeiling(
        periodicProtectionWaitUs(
            result.freeRunningPhasePs, timing.periodPs,
            timing.activePs, timing.uncertaintyPs,
            result.freeRunning));
    if (couldBeActive(result.freeRunning) &&
            result.freeRunningPhasePs <= timing.activePs) {
        result.freeRunningScanoutPositionValid = true;
        result.freeRunningScanoutPositionPpm = scanoutPositionPpm(
            result.freeRunningPhasePs, timing.activePs);
    }

    if (result.vrrLocked == VrrRasterPhaseState::Active &&
            result.freeRunning == VrrRasterPhaseState::Active) {
        result.envelope = VrrRasterEnvelopeClass::CertainActive;
    }
    else if (couldBeActive(result.vrrLocked) ||
             couldBeActive(result.freeRunning)) {
        result.envelope = VrrRasterEnvelopeClass::PossibleActive;
    }
    else if (result.vrrLocked == VrrRasterPhaseState::Inactive &&
             result.freeRunning == VrrRasterPhaseState::Inactive) {
        result.envelope = VrrRasterEnvelopeClass::InactiveInBothModels;
    }
    return result;
}

const char* vrrRasterPhaseStateName(VrrRasterPhaseState state)
{
    switch (state) {
    case VrrRasterPhaseState::Active:
        return "active";
    case VrrRasterPhaseState::Inactive:
        return "inactive";
    case VrrRasterPhaseState::BoundaryUncertain:
        return "boundary_uncertain";
    case VrrRasterPhaseState::Unclassified:
    default:
        return "unclassified";
    }
}

VrrExactRefreshPhaseClass evaluateVrrExactRefreshPhase(
    bool latched, uint64_t submissionUs, uint64_t exactRefreshUs,
    uint64_t recordedDisplayPeriodUs, uint64_t sourcePeriodUs,
    const VrrReplayDisplayParameters& parameters)
{
    if (latched) {
        return VrrExactRefreshPhaseClass::LatchedSuppressed;
    }
    const ResolvedScanoutTiming timing = resolveScanoutTiming(
        recordedDisplayPeriodUs, parameters);
    if (!timing.valid || timing.phaseWindowInvalid ||
            exactRefreshUs == 0 ||
            parameters.syncToActiveScanoutUs >
                std::numeric_limits<uint64_t>::max() -
                    exactRefreshUs) {
        return VrrExactRefreshPhaseClass::Unclassified;
    }
    const uint64_t maxAgeUs = parameters.anchorMaxAgeUs != 0 ?
        parameters.anchorMaxAgeUs :
        std::max(saturatingMultiply(timing.periodUs, 8),
                 saturatingMultiply(sourcePeriodUs, 2));
    const uint64_t modeledSubmissionUs = saturatingAdd(
        submissionUs, parameters.presentTransportUs);
    const uint64_t anchorDistanceUs =
        modeledSubmissionUs >= exactRefreshUs ?
            modeledSubmissionUs - exactRefreshUs :
            exactRefreshUs - modeledSubmissionUs;
    if (maxAgeUs != 0 && anchorDistanceUs > maxAgeUs) {
        return VrrExactRefreshPhaseClass::Unclassified;
    }
    const uint64_t activeScanoutOriginUs =
        exactRefreshUs + parameters.syncToActiveScanoutUs;
    if (modeledSubmissionUs < activeScanoutOriginUs) {
        const uint64_t leadUs =
            activeScanoutOriginUs - modeledSubmissionUs;
        uint64_t leadPs = 0;
        if (!microsecondsToPicoseconds(leadUs, leadPs)) {
            return VrrExactRefreshPhaseClass::Unclassified;
        }
        return leadPs <= timing.uncertaintyPs ?
            VrrExactRefreshPhaseClass::BoundaryUncertain :
            VrrExactRefreshPhaseClass::BeforeActiveScanout;
    }
    const uint64_t ageUs =
        modeledSubmissionUs - activeScanoutOriginUs;
    uint64_t agePs = 0;
    if (!microsecondsToPicoseconds(ageUs, agePs)) {
        return VrrExactRefreshPhaseClass::Unclassified;
    }
    const uint64_t activeDistancePs = agePs >= timing.activePs ?
        agePs - timing.activePs : timing.activePs - agePs;
    if (agePs <= timing.uncertaintyPs ||
            activeDistancePs <= timing.uncertaintyPs) {
        return VrrExactRefreshPhaseClass::BoundaryUncertain;
    }
    return agePs < timing.activePs ?
        VrrExactRefreshPhaseClass::Active :
        VrrExactRefreshPhaseClass::AfterActiveScanout;
}

VrrSubmissionAdvanceResult applyVrrSubmissionAdvance(
    uint64_t nominalSubmissionUs, uint64_t readinessFloorUs,
    uint64_t requestedAdvanceUs)
{
    VrrSubmissionAdvanceResult result;
    const uint64_t boundedNominalUs = std::max(
        nominalSubmissionUs, readinessFloorUs);
    const uint64_t advancedSubmissionUs =
        boundedNominalUs > requestedAdvanceUs ?
            boundedNominalUs - requestedAdvanceUs : 0;
    result.submissionUs = std::max(
        readinessFloorUs, advancedSubmissionUs);
    result.appliedAdvanceUs = boundedNominalUs - result.submissionUs;
    result.clampedByReadiness =
        result.appliedAdvanceUs < requestedAdvanceUs;
    return result;
}

VrrRasterProbeOverheadRemovalResult
evaluateVrrRasterProbeOverheadRemoval(
    bool enabled, bool beforeQueryResultValid, int64_t beforeQueryResult,
    uint64_t beforeQueryStartUs, uint64_t beforeQueryEndUs,
    bool nativePresentTimingValid, uint64_t nativePresentStartUs,
    uint64_t nominalSubmissionUs, uint64_t submissionFloorUs)
{
    VrrRasterProbeOverheadRemovalResult result;
    result.requested = enabled;
    result.submissionUs = nominalSubmissionUs;
    result.evidenceAvailable =
        beforeQueryResultValid &&
        beforeQueryResult == 0 &&
        beforeQueryStartUs != 0 &&
        beforeQueryStartUs <= beforeQueryEndUs &&
        nativePresentTimingValid &&
        beforeQueryEndUs <= nativePresentStartUs;
    if (!result.evidenceAvailable) {
        return result;
    }

    result.measuredProbeDurationUs =
        beforeQueryEndUs - beforeQueryStartUs;
    if (!enabled) {
        return result;
    }

    const VrrSubmissionAdvanceResult removal =
        applyVrrSubmissionAdvance(
            nominalSubmissionUs, submissionFloorUs,
            result.measuredProbeDurationUs);
    result.submissionUs = removal.submissionUs;
    result.appliedRemovalUs = removal.appliedAdvanceUs;
    result.clampedBySubmissionFloor =
        removal.clampedByReadiness;
    return result;
}

VrrWakeDelayInjectionResult evaluateVrrWakeDelayInjection(
    uint64_t waitEntryUs, uint64_t deadlineUs,
    uint64_t baseActiveWaitUs, uint64_t additionalWakeLeadUs,
    uint64_t maximumAdditionalWakeLeadUs,
    uint64_t requestedDelayUs)
{
    VrrWakeDelayInjectionResult result;
    result.deadlineInFuture = deadlineUs > waitEntryUs;
    if (result.deadlineInFuture) {
        const uint64_t boundedAdditionalWakeLeadUs = std::min(
            additionalWakeLeadUs, maximumAdditionalWakeLeadUs);
        const uint64_t activeWaitUs = saturatingAdd(
            baseActiveWaitUs, boundedAdditionalWakeLeadUs);
        result.coarseSleepExpected =
            deadlineUs - waitEntryUs > activeWaitUs;
    }
    result.appliedDelayUs =
        result.coarseSleepExpected ? requestedDelayUs : 0;
    result.suppressedDelayUs =
        requestedDelayUs - result.appliedDelayUs;
    // Without a recorded coarse-return timestamp there is no evidence for
    // how much early-wake margin absorbed the fault. Preserve historical
    // replay behavior by treating the eligible request as execution delay;
    // replay-grade readiness uses evaluateVrrRecordedWakeDelayInjection().
    result.executionDelayUs = result.appliedDelayUs;
    result.simulatedFinalUs = saturatingAdd(
        deadlineUs, result.executionDelayUs);
    return result;
}

VrrWaitLifecycleAudit evaluateVrrWaitLifecycle(
    const VrrWaitLifecycleEvidence& evidence,
    uint64_t baseActiveWaitUs,
    uint64_t maximumAdditionalWakeLeadUs)
{
    VrrWaitLifecycleAudit result;
    const uint64_t boundedAdditionalWakeLeadUs = std::min(
        evidence.additionalWakeLeadUs,
        maximumAdditionalWakeLeadUs);
    const uint64_t expectedActiveWaitUs = saturatingAdd(
        baseActiveWaitUs, boundedAdditionalWakeLeadUs);
    const bool deadlineInFuture =
        evidence.deadlineUs > evidence.initialNowUs;
    result.coarseSleepExpected =
        deadlineInFuture &&
        evidence.deadlineUs - evidence.initialNowUs >
            expectedActiveWaitUs;
    const bool coarseSleepPresent =
        evidence.coarseSleepCount != 0;
    const bool coarsePayloadClear =
        coarseSleepPresent ||
        (evidence.coarseSleepRequestedUs == 0 &&
         evidence.coarseSleepRequestedWakeUs == 0 &&
         evidence.coarseSleepReturnUs == 0 &&
         !evidence.coarseSleepClockStalled);
    bool coarseRelationshipValid = coarsePayloadClear;
    uint64_t expectedSchedulerDelayUs = 0;
    if (coarseSleepPresent) {
        const uint64_t expectedRequestedWakeUs =
            evidence.deadlineUs - expectedActiveWaitUs;
        const uint64_t expectedFirstRequestUs =
            expectedRequestedWakeUs - evidence.initialNowUs;
        const uint64_t minimumRequestedTotalUs = saturatingAdd(
            expectedFirstRequestUs, evidence.coarseSleepCount - 1);
        const uint64_t maximumRequestedTotalUs = saturatingMultiply(
            expectedFirstRequestUs, evidence.coarseSleepCount);
        if (evidence.coarseSleepReturnUs >
                evidence.coarseSleepRequestedWakeUs) {
            const uint64_t coarseOvershootUs =
                evidence.coarseSleepReturnUs -
                    evidence.coarseSleepRequestedWakeUs;
            expectedSchedulerDelayUs =
                coarseOvershootUs > baseActiveWaitUs ?
                    coarseOvershootUs - baseActiveWaitUs : 0;
        }
        const bool coarseStallRelationshipValid =
            !evidence.coarseSleepClockStalled ||
            (evidence.coarseSleepCount >= 2 &&
             evidence.coarseSleepReturnUs <
                 evidence.coarseSleepRequestedWakeUs);
        const bool coarseCompletionRelationshipValid =
            evidence.coarseSleepClockStalled ||
            evidence.coarseSleepReturnUs >=
                evidence.coarseSleepRequestedWakeUs;
        coarseRelationshipValid =
            evidence.coarseSleepRequestedUs >=
                minimumRequestedTotalUs &&
            evidence.coarseSleepRequestedUs <=
                maximumRequestedTotalUs &&
            evidence.coarseSleepRequestedWakeUs ==
                expectedRequestedWakeUs &&
            evidence.coarseSleepReturnUs >=
                evidence.initialNowUs &&
            evidence.finalNowUs >=
                evidence.coarseSleepReturnUs &&
            coarseStallRelationshipValid &&
            coarseCompletionRelationshipValid;
    }

    const bool expectedActiveWaitEntered =
        deadlineInFuture &&
        (!coarseSleepPresent ||
         evidence.coarseSleepReturnUs < evidence.deadlineUs);
    const bool activePayloadClear =
        evidence.activeWaitEntered ||
        (evidence.activeWaitStartUs == 0 &&
         evidence.activeWaitLimitUs == 0 &&
         evidence.activeWaitYieldCount == 0 &&
         !evidence.activeWaitClockStalled &&
         !evidence.activeWaitYieldLimitReached);
    bool activeRelationshipValid = activePayloadClear;
    if (evidence.activeWaitEntered) {
        const uint64_t expectedActiveStartUs =
            coarseSleepPresent ?
                evidence.coarseSleepReturnUs :
                evidence.initialNowUs;
        const uint64_t expectedActiveLimitUs =
            saturatingAdd(
                expectedActiveStartUs, expectedActiveWaitUs);
        const bool legacyYieldLimitReached =
            evidence.activeWaitYieldCount >= 4096 &&
            evidence.finalNowUs < evidence.deadlineUs &&
            evidence.finalNowUs < expectedActiveLimitUs;
        const bool yieldTerminationValid =
            !evidence.activeWaitYieldLimitReached ||
            (evidence.activeWaitYieldLimitReached &&
             legacyYieldLimitReached);
        activeRelationshipValid =
            evidence.activeWaitStartUs == expectedActiveStartUs &&
            evidence.activeWaitLimitUs == expectedActiveLimitUs &&
            evidence.finalNowUs >= evidence.activeWaitStartUs &&
            (!evidence.activeWaitClockStalled ||
             evidence.activeWaitYieldCount >= 64) &&
            yieldTerminationValid;
    }

    result.completedDeadline =
        evidence.finalNowUs >= evidence.deadlineUs;
    const bool finalSourceRelationshipValid =
        evidence.activeWaitEntered ||
        (coarseSleepPresent ?
            evidence.finalNowUs == evidence.coarseSleepReturnUs :
            evidence.finalNowUs == evidence.initialNowUs);
    result.relationshipValid =
        evidence.callEntryUs != 0 &&
        evidence.deadlineUs != 0 &&
        evidence.initialNowUs >= evidence.callEntryUs &&
        evidence.finalNowUs >= evidence.initialNowUs &&
        evidence.activeWaitUs == expectedActiveWaitUs &&
        evidence.deadlineAlreadyElapsed == !deadlineInFuture &&
        coarseSleepPresent == result.coarseSleepExpected &&
        evidence.schedulerDelayValid == coarseSleepPresent &&
        evidence.schedulerDelayUs == expectedSchedulerDelayUs &&
        coarseRelationshipValid &&
        evidence.activeWaitEntered == expectedActiveWaitEntered &&
        activeRelationshipValid &&
        finalSourceRelationshipValid;
    result.cleanCompletion =
        result.relationshipValid &&
        result.completedDeadline &&
        !evidence.coarseSleepClockStalled &&
        !evidence.activeWaitClockStalled &&
        !evidence.activeWaitYieldLimitReached;
    return result;
}

VrrWakeDelayInjectionResult evaluateVrrRecordedWakeDelayInjection(
    const VrrWaitLifecycleEvidence& recorded,
    uint64_t candidateCallEntryUs,
    uint64_t candidateDeadlineUs,
    uint64_t candidateAdditionalWakeLeadUs,
    uint64_t baseActiveWaitUs,
    uint64_t maximumAdditionalWakeLeadUs,
    uint64_t requestedDelayUs)
{
    VrrWakeDelayInjectionResult result;
    const uint64_t initialCallOffsetUs =
        recorded.initialNowUs >= recorded.callEntryUs ?
            recorded.initialNowUs - recorded.callEntryUs : 0;
    const uint64_t candidateInitialUs = saturatingAdd(
        candidateCallEntryUs, initialCallOffsetUs);
    result.deadlineInFuture =
        candidateDeadlineUs > candidateInitialUs;
    const uint64_t boundedAdditionalWakeLeadUs = std::min(
        candidateAdditionalWakeLeadUs,
        maximumAdditionalWakeLeadUs);
    const uint64_t activeWaitUs = saturatingAdd(
        baseActiveWaitUs, boundedAdditionalWakeLeadUs);
    result.coarseSleepExpected =
        result.deadlineInFuture &&
        candidateDeadlineUs - candidateInitialUs > activeWaitUs;

    const bool recordedDeadlineInFuture =
        recorded.deadlineUs > recorded.initialNowUs;
    const bool recordedCoarseSleep =
        recordedDeadlineInFuture &&
        recorded.coarseSleepCount != 0 &&
        recorded.coarseSleepRequestedWakeUs != 0 &&
        recorded.coarseSleepReturnUs != 0;
    const bool sameWaitPath =
        result.deadlineInFuture == recordedDeadlineInFuture &&
        result.coarseSleepExpected == recordedCoarseSleep;
    result.recordedPathMatchesCandidate = sameWaitPath;
    if (!result.deadlineInFuture) {
        result.baselineFinalUs = candidateInitialUs;
    }
    else if (sameWaitPath) {
        result.usedRecordedFinalResidual = true;
        if (recorded.finalNowUs >= recorded.deadlineUs) {
            result.baselineFinalUs = saturatingAdd(
                candidateDeadlineUs,
                recorded.finalNowUs - recorded.deadlineUs);
        }
        else {
            const uint64_t earlyByUs =
                recorded.deadlineUs - recorded.finalNowUs;
            result.baselineFinalUs =
                candidateDeadlineUs > earlyByUs ?
                    candidateDeadlineUs - earlyByUs : 0;
        }
        result.baselineFinalUs = std::max(
            result.baselineFinalUs, candidateInitialUs);
    }
    else {
        // A candidate that changes active/coarse/elapsed paths has no
        // captured OS residual for that path. Use an ideal deadline return
        // rather than leaking an unrelated captured wake into it.
        result.baselineFinalUs = candidateDeadlineUs;
    }

    if (!result.coarseSleepExpected) {
        result.simulatedFinalUs = result.baselineFinalUs;
        result.suppressedDelayUs = requestedDelayUs;
        return result;
    }

    const uint64_t candidateRequestedWakeUs =
        candidateDeadlineUs - activeWaitUs;
    uint64_t baselineCoarseReturnUs = candidateRequestedWakeUs;
    if (recordedCoarseSleep) {
        result.usedRecordedCoarseTelemetry = true;
        if (recorded.coarseSleepReturnUs >=
                recorded.coarseSleepRequestedWakeUs) {
            baselineCoarseReturnUs = saturatingAdd(
                candidateRequestedWakeUs,
                recorded.coarseSleepReturnUs -
                    recorded.coarseSleepRequestedWakeUs);
        }
        else {
            const uint64_t earlyByUs =
                recorded.coarseSleepRequestedWakeUs -
                    recorded.coarseSleepReturnUs;
            baselineCoarseReturnUs =
                candidateRequestedWakeUs > earlyByUs ?
                    candidateRequestedWakeUs - earlyByUs : 0;
        }
    }
    baselineCoarseReturnUs = std::max(
        baselineCoarseReturnUs, candidateInitialUs);
    result.baselineFinalUs = std::max(
        result.baselineFinalUs, baselineCoarseReturnUs);
    result.appliedDelayUs = requestedDelayUs;
    result.simulatedCoarseReturnUs = saturatingAdd(
        baselineCoarseReturnUs, requestedDelayUs);
    result.simulatedFinalUs = std::max(
        result.baselineFinalUs, result.simulatedCoarseReturnUs);
    result.executionDelayUs =
        result.simulatedFinalUs - result.baselineFinalUs;
    result.absorbedDelayUs =
        requestedDelayUs - result.executionDelayUs;
    result.schedulerDelayValid = true;
    if (result.simulatedCoarseReturnUs > candidateRequestedWakeUs) {
        const uint64_t coarseOvershootUs =
            result.simulatedCoarseReturnUs -
                candidateRequestedWakeUs;
        result.schedulerDelayUs =
            coarseOvershootUs > baseActiveWaitUs ?
                coarseOvershootUs - baseActiveWaitUs : 0;
    }
    return result;
}

VrrDxgiCapabilityAudit evaluateVrrDxgiCapability(
    bool dxgiPresentAttempt,
    bool featureQueryResultValid, int64_t featureQueryResult,
    bool featureAllowsTearing, bool tearingSupported,
    bool descriptorQueryResultValid, int64_t descriptorQueryResult,
    uint64_t swapChainFlags, uint64_t swapChainSwapEffect,
    bool recordedSwapChainAllowsTearing,
    bool fullscreenQueryResultValid, int64_t fullscreenQueryResult,
    bool fullscreenExclusive, uint64_t windowFlags,
    bool recordedBorderlessFlipModel)
{
    // Numeric values are part of the serialized Windows contract:
    // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING, both flip-model swap effects, and
    // SDL_WINDOW_FULLSCREEN_DESKTOP from the SDL2 ABI used by Moonlight.
    constexpr uint64_t kSwapChainAllowTearing = 0x00000800ULL;
    constexpr uint64_t kFlipSequential = 3;
    constexpr uint64_t kFlipDiscard = 4;
    constexpr uint64_t kFullscreenDesktop = 0x00001001ULL;

    VrrDxgiCapabilityAudit result;
    result.declarationsMatchBackend =
        featureQueryResultValid == dxgiPresentAttempt &&
        descriptorQueryResultValid == dxgiPresentAttempt &&
        fullscreenQueryResultValid == dxgiPresentAttempt;

    if (!dxgiPresentAttempt) {
        result.relationshipsValid =
            result.declarationsMatchBackend &&
            featureQueryResult == 0 &&
            !featureAllowsTearing &&
            !tearingSupported &&
            descriptorQueryResult == 0 &&
            swapChainFlags == 0 &&
            swapChainSwapEffect == 0 &&
            !recordedSwapChainAllowsTearing &&
            fullscreenQueryResult == 0 &&
            !fullscreenExclusive &&
            windowFlags == 0 &&
            !recordedBorderlessFlipModel;
        return result;
    }

    result.featureQuerySucceeded =
        featureQueryResultValid && featureQueryResult == 0;
    result.descriptorQuerySucceeded =
        descriptorQueryResultValid && descriptorQueryResult == 0;
    result.fullscreenQuerySucceeded =
        fullscreenQueryResultValid && fullscreenQueryResult == 0;
    result.flipModel =
        result.descriptorQuerySucceeded &&
        (swapChainSwapEffect == kFlipSequential ||
         swapChainSwapEffect == kFlipDiscard);
    result.borderlessWindow =
        result.fullscreenQuerySucceeded &&
        !fullscreenExclusive &&
        (windowFlags & kFullscreenDesktop) == kFullscreenDesktop;
    result.swapChainAllowsTearing =
        result.descriptorQuerySucceeded &&
        (swapChainFlags & kSwapChainAllowTearing) != 0;

    // The renderer deliberately uses SUCCEEDED() for the feature query but
    // strict replay readiness separately demands exact S_OK. Descriptor and
    // fullscreen payloads are zero-initialized and only retained on S_OK.
    const bool expectedTearingSupported =
        featureQueryResultValid &&
        featureQueryResult >= 0 &&
        featureAllowsTearing;
    const bool failedDescriptorPayloadClear =
        result.descriptorQuerySucceeded ||
        (swapChainFlags == 0 && swapChainSwapEffect == 0);
    const bool failedFullscreenPayloadClear =
        result.fullscreenQuerySucceeded || !fullscreenExclusive;
    const bool expectedBorderlessFlipModel =
        result.flipModel && result.borderlessWindow;
    result.relationshipsValid =
        result.declarationsMatchBackend &&
        tearingSupported == expectedTearingSupported &&
        recordedSwapChainAllowsTearing ==
            result.swapChainAllowsTearing &&
        recordedBorderlessFlipModel ==
            expectedBorderlessFlipModel &&
        failedDescriptorPayloadClear &&
        failedFullscreenPayloadClear;
    result.exactEligible =
        result.relationshipsValid &&
        result.featureQuerySucceeded &&
        featureAllowsTearing &&
        tearingSupported &&
        result.descriptorQuerySucceeded &&
        result.swapChainAllowsTearing &&
        result.flipModel &&
        result.fullscreenQuerySucceeded &&
        result.borderlessWindow &&
        recordedBorderlessFlipModel;
    return result;
}

bool vrrPeriodicInjectionSelected(
    uint64_t scheduledFrameNumber,
    uint64_t everyFrames,
    uint64_t phaseFrames,
    uint64_t burstFrames)
{
    if (everyFrames == 0 || burstFrames == 0 ||
            phaseFrames >= everyFrames ||
            burstFrames > everyFrames) {
        return false;
    }
    const uint64_t framePhase =
        scheduledFrameNumber % everyFrames;
    const uint64_t distanceFromSelectedPhase =
        (framePhase + everyFrames - phaseFrames) %
            everyFrames;
    return distanceFromSelectedPhase < burstFrames;
}

bool vrrRasterEnvelopeContradictsExactRefresh(
    VrrRasterEnvelopeClass envelope, VrrExactRefreshPhaseClass exact)
{
    if (envelope == VrrRasterEnvelopeClass::Unclassified ||
            exact == VrrExactRefreshPhaseClass::Unclassified) {
        return false;
    }
    if (exact == VrrExactRefreshPhaseClass::LatchedSuppressed) {
        return envelope != VrrRasterEnvelopeClass::LatchedSuppressed;
    }
    if (envelope == VrrRasterEnvelopeClass::LatchedSuppressed) {
        return true;
    }
    return (exact == VrrExactRefreshPhaseClass::Active &&
            envelope == VrrRasterEnvelopeClass::InactiveInBothModels) ||
        ((exact == VrrExactRefreshPhaseClass::BeforeActiveScanout ||
          exact == VrrExactRefreshPhaseClass::AfterActiveScanout) &&
         envelope == VrrRasterEnvelopeClass::CertainActive);
}

const char* vrrRasterEnvelopeClassName(VrrRasterEnvelopeClass value)
{
    switch (value) {
    case VrrRasterEnvelopeClass::LatchedSuppressed:
        return "latched_suppressed";
    case VrrRasterEnvelopeClass::CertainActive:
        return "certain_active";
    case VrrRasterEnvelopeClass::PossibleActive:
        return "possible_active";
    case VrrRasterEnvelopeClass::InactiveInBothModels:
        return "inactive_in_both_models";
    case VrrRasterEnvelopeClass::Unclassified:
    default:
        return "unclassified";
    }
}

const char* vrrExactRefreshPhaseClassName(VrrExactRefreshPhaseClass value)
{
    switch (value) {
    case VrrExactRefreshPhaseClass::LatchedSuppressed:
        return "latched_suppressed";
    case VrrExactRefreshPhaseClass::BeforeActiveScanout:
        return "before_active_scanout";
    case VrrExactRefreshPhaseClass::Active:
        return "active";
    case VrrExactRefreshPhaseClass::BoundaryUncertain:
        return "boundary_uncertain";
    case VrrExactRefreshPhaseClass::AfterActiveScanout:
        return "after_active_scanout";
    case VrrExactRefreshPhaseClass::Unclassified:
    default:
        return "unclassified";
    }
}
