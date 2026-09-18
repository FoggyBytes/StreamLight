#pragma once

#include "vrrreplayconfig.h"

#include <cstdint>

// Historical captures omit the permission field and therefore keep the old
// tearing-permitted native contract. The A/B option changes only permission;
// it does not turn an unlatched scheduling decision into a latched one.
inline bool vrrDxgiPresentParametersValid(bool parametersDeclared,
                                         bool nativeDxgiPresentAttempt,
                                         bool latchedPresent,
                                         uint64_t syncInterval,
                                         uint64_t flags,
                                         bool sessionAllowTearing = true)
{
    constexpr uint64_t allowTearingFlag = 0x00000200ULL;
    const uint64_t expectedFlags = !latchedPresent && sessionAllowTearing ?
        allowTearingFlag : 0;
    return parametersDeclared == nativeDxgiPresentAttempt &&
        (!parametersDeclared ||
         ((syncInterval == 0 || (latchedPresent && syncInterval == 1)) &&
          flags == expectedFlags));
}

// Reconstruct a busy worker's next decision without allowing an older idle
// estimate to overrule this row's observed, faster readiness.
uint64_t vrrBusyWorkerDecisionUs(uint64_t arrivalUs, uint64_t recordedDecisionUs,
                                uint64_t simulatedPreviousSubmissionUs,
                                uint64_t postSubmissionGapUs,
                                uint64_t learnedIdleLatencyUs);

bool vrrDecodeReadinessOrderValid(uint64_t decoderOutputUs, uint64_t readyUs,
                                  uint64_t arrivalUs, uint64_t dequeueUs,
                                  uint64_t decisionUs, uint64_t decodeWaitUs,
                                  bool decisionValid,
                                  bool readinessExcludesQueue = false);

enum class VrrRasterPhaseState {
    Unclassified,
    Active,
    Inactive,
    BoundaryUncertain,
};

enum class VrrRasterEnvelopeClass {
    Unclassified,
    LatchedSuppressed,
    CertainActive,
    PossibleActive,
    InactiveInBothModels,
};

enum class VrrExactRefreshPhaseClass {
    Unclassified,
    LatchedSuppressed,
    BeforeActiveScanout,
    Active,
    BoundaryUncertain,
    AfterActiveScanout,
};

struct VrrRasterPhaseResult {
    VrrRasterEnvelopeClass envelope =
        VrrRasterEnvelopeClass::Unclassified;
    VrrRasterPhaseState vrrLocked =
        VrrRasterPhaseState::Unclassified;
    VrrRasterPhaseState freeRunning =
        VrrRasterPhaseState::Unclassified;
    uint64_t resolvedScanoutPeriodUs = 0;
    uint64_t resolvedScanoutPeriodPs = 0;
    uint64_t resolvedActiveScanoutUs = 0;
    uint64_t resolvedActiveScanoutPs = 0;
    uint64_t resolvedSyncToActiveScanoutUs = 0;
    uint64_t resolvedPhaseUncertaintyPs = 0;
    uint64_t resolvedAnchorMaxAgeUs = 0;
    bool activeScanoutClamped = false;
    bool scanoutPhaseWindowInvalid = false;
    uint64_t modeledTransitionUs = 0;
    uint64_t anchorTimeUs = 0;
    uint64_t anchorAgeUs = 0;
    bool vrrLockedPhaseValid = false;
    uint64_t vrrLockedPhasePs = 0;
    bool freeRunningPhaseValid = false;
    uint64_t freeRunningPhaseUs = 0;
    uint64_t freeRunningPhasePs = 0;
    uint64_t vrrLockedWaitUs = 0;
    uint64_t freeRunningWaitUs = 0;
    bool vrrLockedScanoutPositionValid = false;
    uint64_t vrrLockedScanoutPositionPpm = 0;
    bool freeRunningScanoutPositionValid = false;
    uint64_t freeRunningScanoutPositionPpm = 0;
};

struct VrrRasterScanLineAudit {
    bool comparable = false;
    bool matches = false;
    uint64_t predictedScanLine = 0;
    uint64_t observedScanLine = 0;
    uint64_t absoluteErrorLines = 0;
    int64_t signedErrorLines = 0;
    uint64_t toleranceLines = 0;
};

struct VrrRasterScanLineScaleInference {
    bool valid = true;
    uint64_t scale = 1;
    uint64_t samples = 0;
    uint64_t maximumObservedScanLine = 0;
};

enum class VrrRasterSyncAnchorMergeStatus {
    Appended,
    ReplacedSameRefresh,
    SequenceRegression,
    SameRefreshTimestampMismatch,
    NonadvancingTimestamp,
    ImplausiblyShortInterval,
};

struct VrrRasterSyncAnchorMergeResult {
    bool accepted = false;
    bool replacesPrevious = false;
    VrrRasterSyncAnchorMergeStatus status =
        VrrRasterSyncAnchorMergeStatus::SequenceRegression;
};

struct VrrSubmissionAdvanceResult {
    uint64_t submissionUs = 0;
    uint64_t appliedAdvanceUs = 0;
    bool clampedByReadiness = false;
};

struct VrrRasterProbeOverheadRemovalResult {
    bool requested = false;
    bool evidenceAvailable = false;
    uint64_t measuredProbeDurationUs = 0;
    uint64_t submissionUs = 0;
    uint64_t appliedRemovalUs = 0;
    bool clampedBySubmissionFloor = false;
};

struct VrrWakeDelayInjectionResult {
    bool deadlineInFuture = false;
    bool coarseSleepExpected = false;
    bool recordedPathMatchesCandidate = false;
    bool usedRecordedFinalResidual = false;
    bool usedRecordedCoarseTelemetry = false;
    uint64_t appliedDelayUs = 0;
    uint64_t suppressedDelayUs = 0;
    uint64_t absorbedDelayUs = 0;
    uint64_t executionDelayUs = 0;
    uint64_t baselineFinalUs = 0;
    uint64_t simulatedFinalUs = 0;
    uint64_t simulatedCoarseReturnUs = 0;
    uint64_t schedulerDelayUs = 0;
    bool schedulerDelayValid = false;
};

struct VrrWaitLifecycleEvidence {
    uint64_t callEntryUs = 0;
    uint64_t deadlineUs = 0;
    uint64_t additionalWakeLeadUs = 0;
    uint64_t initialNowUs = 0;
    uint64_t finalNowUs = 0;
    uint64_t activeWaitUs = 0;
    uint64_t coarseSleepCount = 0;
    uint64_t coarseSleepRequestedUs = 0;
    uint64_t coarseSleepRequestedWakeUs = 0;
    uint64_t coarseSleepReturnUs = 0;
    bool coarseSleepClockStalled = false;
    bool activeWaitEntered = false;
    uint64_t activeWaitStartUs = 0;
    uint64_t activeWaitLimitUs = 0;
    uint64_t activeWaitYieldCount = 0;
    bool activeWaitClockStalled = false;
    bool activeWaitYieldLimitReached = false;
    uint64_t schedulerDelayUs = 0;
    bool schedulerDelayValid = false;
    bool deadlineAlreadyElapsed = false;
};

struct VrrWaitLifecycleAudit {
    bool coarseSleepExpected = false;
    bool relationshipValid = false;
    bool completedDeadline = false;
    bool cleanCompletion = false;
};

struct VrrDxgiCapabilityAudit {
    bool declarationsMatchBackend = false;
    bool featureQuerySucceeded = false;
    bool descriptorQuerySucceeded = false;
    bool fullscreenQuerySucceeded = false;
    bool flipModel = false;
    bool borderlessWindow = false;
    bool swapChainAllowsTearing = false;
    bool relationshipsValid = false;
    bool exactEligible = false;
};

struct VrrRawQpcTranslationResult {
    bool baselineEstablished = false;
    bool compared = false;
    bool frequencyMismatch = false;
    bool translationMismatch = false;
};

struct VrrQpcCorrelationResult {
    bool translated = false;
    uint64_t expectedTimeUs = 0;
    bool matches = false;
    bool uncertaintyValid = false;
    uint64_t halfSpanUncertaintyUs = 0;
};

struct VrrDisplaySignalConsistency {
    bool inputsValid = false;
    uint64_t pixelRateToHsyncErrorPpm = 0;
    uint64_t hsyncToVsyncErrorPpm = 0;
    uint64_t pixelRateToVsyncErrorPpm = 0;
    bool withinTolerance = false;
};

struct VrrGpuCompletionBounds {
    bool valid = false;
    bool fenceRelationshipValid = false;
    uint64_t lowerBoundUs = 0;
    uint64_t upperBoundUs = 0;
    uint64_t uncertaintyUs = 0;
};

struct VrrGpuReadyOperationAudit {
    bool signalSucceeded = false;
    bool setEventSucceeded = false;
    bool waitSucceeded = false;
    bool relationshipValid = false;
    bool exactSuccess = false;
};

struct VrrGpuReadyStageTimingAudit {
    bool relationshipValid = false;
};

struct VrrPresenterSubmissionAudit {
    bool expectedPresenterTimeUsed = false;
    uint64_t expectedSubmissionBoundaryUs = 0;
    bool relationshipValid = false;
};

struct VrrSpacingCorrectionAudit {
    bool relationshipValid = false;
};

struct VrrSpacingLifecycleTimingAudit {
    uint64_t expectedPresentationFloorUs = 0;
    uint64_t expectedFirstCheckDeficitUs = 0;
    uint64_t expectedRecheckDeficitUs = 0;
    uint64_t expectedSpacingDeficitUs = 0;
    bool relationshipValid = false;
};

struct VrrPostPresentQueryTimingAudit {
    bool expectedTiming = false;
    bool relationshipValid = false;
};

struct VrrFreeRunningRefreshResult {
    bool baselineEstablished = false;
    bool compared = false;
    bool timeRegression = false;
    bool periodChanged = false;
    bool phaseReferenceCompared = false;
    uint64_t propagatedPhase = 0;
    uint64_t phaseReferenceDifference = 0;
    uint64_t refreshDeltaLower = 0;
    uint64_t refreshDelta = 0;
    uint64_t refreshDeltaUpper = 0;
    uint64_t scanoutAnomalyLower = 0;
    uint64_t scanoutAnomaly = 0;
    uint64_t scanoutAnomalyUpper = 0;
    uint64_t repeatedRefreshLower = 0;
    uint64_t repeatedRefresh = 0;
    uint64_t repeatedRefreshUpper = 0;
};

// Propagates a fixed/free-running refresh phase through candidate transition
// times. It is deliberately a named counterfactual hypothesis, not a claim
// about a VRR panel. A captured sync anchor seeds each epoch; subsequent
// candidate phases come only from elapsed time so recorded VRR transitions do
// not leak into the counterfactual clock. All time, period, phase, and
// uncertainty arguments use the same caller-selected unit; replay uses
// picoseconds to keep long-segment integer-period drift below the trace
// clock's microsecond resolution.
class VrrFreeRunningRefreshTracker {
public:
    void reset();

    VrrFreeRunningRefreshResult observe(
        uint64_t transitionTime, uint64_t period,
        bool phaseReferenceValid, uint64_t phaseReference,
        uint64_t phaseUncertainty);

private:
    bool m_HaveBaseline = false;
    uint64_t m_Period = 0;
    uint64_t m_PriorTransitionTime = 0;
    uint64_t m_Phase = 0;
    uint64_t m_PhaseUncertainty = 0;
};

// Independently checks that translated SyncQPCTime deltas agree with the raw
// QPC ticks and frequency captured in the trace. Epoch resets deliberately
// establish a new baseline rather than comparing across a display transition.
class VrrRawQpcTranslationTracker {
public:
    void reset();

    VrrRawQpcTranslationResult observe(
        uint64_t rawTicks, uint64_t frequency, bool translatedValid,
        uint64_t translatedUs, uint64_t toleranceUs);

private:
    bool m_HaveTranslatedSample = false;
    uint64_t m_Frequency = 0;
    uint64_t m_PriorRawTicks = 0;
    uint64_t m_PriorTranslatedUs = 0;
};

VrrQpcCorrelationResult evaluateVrrQpcCorrelation(
    uint64_t rawTicks, uint64_t frequency, uint64_t translatedTimeUs,
    uint64_t referenceTicks, uint64_t referenceTimeUs,
    uint64_t bracketSpanTicks, uint64_t toleranceUs);

bool vrrPeriodPicosecondsFromRefreshRational(
    uint64_t refreshNumerator, uint64_t refreshDenominator,
    uint64_t& periodPicoseconds);

bool vrrActiveScanoutMicrosecondsFromSignal(
    uint64_t refreshNumerator, uint64_t refreshDenominator,
    uint64_t activeWidth, uint64_t activeHeight,
    uint64_t totalWidth, uint64_t totalHeight,
    uint64_t& activeScanoutMicroseconds);

bool vrrActiveScanoutPicosecondsFromSignal(
    uint64_t refreshNumerator, uint64_t refreshDenominator,
    uint64_t activeWidth, uint64_t activeHeight,
    uint64_t totalWidth, uint64_t totalHeight,
    uint64_t& activeScanoutPicoseconds);

bool vrrRefreshRationalsEqual(
    uint64_t leftNumerator, uint64_t leftDenominator,
    uint64_t rightNumerator, uint64_t rightDenominator);

bool addVrrRasterScanLineScaleSample(
    VrrRasterScanLineScaleInference& inference,
    bool observedInVerticalBlank, uint64_t observedScanLine,
    uint64_t activeHeight, uint64_t totalHeight);

bool normalizeVrrRasterScanLine(
    uint64_t observedScanLine, uint64_t scale,
    uint64_t& normalizedScanLine);

VrrRasterScanLineAudit evaluateVrrRasterScanLine(
    bool observedInVerticalBlank,
    VrrRasterPhaseState predictedState,
    bool predictedPhaseValid, uint64_t predictedPhasePs,
    uint64_t periodPs, uint64_t phaseUncertaintyPs,
    uint64_t activeHeight, uint64_t totalHeight,
    uint64_t observedScanLine);

VrrRasterSyncAnchorMergeResult evaluateVrrRasterSyncAnchorMerge(
    bool havePrevious,
    uint64_t previousRefreshSequence, uint64_t previousTimeUs,
    uint64_t refreshSequence, uint64_t timeUs,
    uint64_t recordedDisplayPeriodUs,
    uint64_t timestampJitterToleranceUs,
    uint64_t minimumIntervalToleranceUs);

VrrDisplaySignalConsistency evaluateVrrDisplaySignalConsistency(
    uint64_t pixelRateHz,
    uint64_t hsyncNumerator, uint64_t hsyncDenominator,
    uint64_t vsyncNumerator, uint64_t vsyncDenominator,
    uint64_t totalWidth, uint64_t totalHeight,
    uint64_t tolerancePpm);

VrrGpuCompletionBounds evaluateVrrGpuCompletionBounds(
    uint64_t preparationStartUs, uint64_t preparationEndUs,
    uint64_t signalStartUs,
    uint64_t pollStartUs, uint64_t pollEndUs,
    uint64_t fenceValue, uint64_t pollCompletedValue,
    bool completedBeforeWait,
    uint64_t waitStartUs, uint64_t waitReturnUs);

VrrGpuReadyOperationAudit evaluateVrrGpuReadyOperation(
    bool attempted,
    bool signalResultValid, int64_t signalResult,
    bool setEventResultValid, int64_t setEventResult,
    bool waitResultValid, uint64_t waitResult,
    bool timingValid, uint64_t signalStartUs, uint64_t fenceValue);

VrrGpuReadyStageTimingAudit evaluateVrrGpuReadyStageTiming(
    uint64_t preparationStartUs, uint64_t preparationEndUs,
    bool attempted, bool signalSucceeded, bool setEventSucceeded,
    uint64_t signalStartUs, uint64_t signalEndUs,
    uint64_t flushStartUs, uint64_t flushEndUs,
    uint64_t setEventStartUs, uint64_t setEventEndUs,
    uint64_t pollStartUs, uint64_t pollEndUs,
    uint64_t waitStartUs, uint64_t waitReturnUs);

VrrPresenterSubmissionAudit evaluateVrrPresenterSubmission(
    bool presented,
    bool presenterTimeValid, uint64_t presenterTimeUs,
    uint64_t operationStartUs, uint64_t operationEndUs,
    bool recordedPresenterTimeUsed,
    uint64_t recordedSubmissionBoundaryUs);

VrrSpacingCorrectionAudit evaluateVrrSpacingCorrection(
    bool normalPresentationLifecycle,
    uint64_t spacingDeficitUs, uint64_t spacingGuardFeedbackUs,
    bool spacingCorrected, bool correctionWaitPresent);

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
    uint64_t presentOperationStartUs);

VrrPostPresentQueryTimingAudit evaluateVrrPostPresentQueryTiming(
    bool expectedTiming,
    bool submissionIdQueryResultValid,
    bool frameStatsQueryResultValid,
    uint64_t priorObservationEndUs,
    uint64_t submissionIdQueryStartUs,
    uint64_t submissionIdQueryEndUs,
    uint64_t frameStatsQueryStartUs,
    uint64_t frameStatsQueryEndUs,
    uint64_t presentOperationEndUs);

VrrRasterPhaseResult evaluateVrrRasterPhase(
    bool presented, bool latched, uint64_t submissionUs,
    uint64_t prePresentSyncSampleUs, uint64_t recordedDisplayPeriodUs,
    uint64_t sourcePeriodUs, const VrrReplayDisplayParameters& parameters);

VrrExactRefreshPhaseClass evaluateVrrExactRefreshPhase(
    bool latched, uint64_t submissionUs, uint64_t exactRefreshUs,
    uint64_t recordedDisplayPeriodUs, uint64_t sourcePeriodUs,
    const VrrReplayDisplayParameters& parameters);

VrrSubmissionAdvanceResult applyVrrSubmissionAdvance(
    uint64_t nominalSubmissionUs, uint64_t readinessFloorUs,
    uint64_t requestedAdvanceUs);

VrrRasterProbeOverheadRemovalResult
evaluateVrrRasterProbeOverheadRemoval(
    bool enabled, bool beforeQueryResultValid, int64_t beforeQueryResult,
    uint64_t beforeQueryStartUs, uint64_t beforeQueryEndUs,
    bool nativePresentTimingValid, uint64_t nativePresentStartUs,
    uint64_t nominalSubmissionUs, uint64_t submissionFloorUs);

VrrWakeDelayInjectionResult evaluateVrrWakeDelayInjection(
    uint64_t waitEntryUs, uint64_t deadlineUs,
    uint64_t baseActiveWaitUs, uint64_t additionalWakeLeadUs,
    uint64_t maximumAdditionalWakeLeadUs,
    uint64_t requestedDelayUs);

VrrWaitLifecycleAudit evaluateVrrWaitLifecycle(
    const VrrWaitLifecycleEvidence& evidence,
    uint64_t baseActiveWaitUs,
    uint64_t maximumAdditionalWakeLeadUs);

VrrWakeDelayInjectionResult evaluateVrrRecordedWakeDelayInjection(
    const VrrWaitLifecycleEvidence& recorded,
    uint64_t candidateCallEntryUs,
    uint64_t candidateDeadlineUs,
    uint64_t candidateAdditionalWakeLeadUs,
    uint64_t baseActiveWaitUs,
    uint64_t maximumAdditionalWakeLeadUs,
    uint64_t requestedDelayUs);

VrrDxgiCapabilityAudit evaluateVrrDxgiCapability(
    bool dxgiPresentAttempt,
    bool featureQueryResultValid, int64_t featureQueryResult,
    bool featureAllowsTearing, bool tearingSupported,
    bool descriptorQueryResultValid, int64_t descriptorQueryResult,
    uint64_t swapChainFlags, uint64_t swapChainSwapEffect,
    bool recordedSwapChainAllowsTearing,
    bool fullscreenQueryResultValid, int64_t fullscreenQueryResult,
    bool fullscreenExclusive, uint64_t windowFlags,
    bool recordedBorderlessFlipModel);

bool vrrPeriodicInjectionSelected(
    uint64_t scheduledFrameNumber,
    uint64_t everyFrames,
    uint64_t phaseFrames,
    uint64_t burstFrames);

bool vrrRasterEnvelopeContradictsExactRefresh(
    VrrRasterEnvelopeClass envelope, VrrExactRefreshPhaseClass exact);

const char* vrrRasterPhaseStateName(VrrRasterPhaseState state);
const char* vrrRasterEnvelopeClassName(VrrRasterEnvelopeClass value);
const char* vrrExactRefreshPhaseClassName(VrrExactRefreshPhaseClass value);
