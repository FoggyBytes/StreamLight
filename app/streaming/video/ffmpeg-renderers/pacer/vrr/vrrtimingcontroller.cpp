// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "vrrtimingcontroller.h"
#include "../../../../vrrratepolicy.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace {

constexpr uint64_t kMicrosecondsPerSecond = 1000000ULL;
constexpr uint64_t kRtpClockRate = 90000ULL;
constexpr uint64_t kQ16One = 1ULL << 16;
constexpr uint64_t kQ16Half = kQ16One >> 1;
// These base values also serve historical replay policies. Live sessions
// resolve the interval-quality controller and preset caps below; the rolling
// quality target, rather than a calibration percentile, owns reserve changes.
constexpr uint64_t kFixedPlayoutDelayUs = 3000;
constexpr uint64_t kPlayoutStartUs = 6000;
constexpr uint64_t kPlayoutMinimumUs = 1000;
constexpr uint64_t kPlayoutMaximumUs = 8000;
// Smooth is intentionally allowed to retain more protection than the other
// profiles. Keep it below the three-frame ownership limit while allowing the
// requested extra padding to be observable at ordinary stream rates.
constexpr uint64_t kSmoothPlayoutMaximumUs = 24000;
constexpr uint64_t kSmoothPlayoutCapSourcePeriodPerMille = 3000;
// Smooth's tighter cadence target is intentionally a separate policy value;
// keep the historical default below unchanged for old captures and direct
// IntervalBuffer callers.
constexpr uint64_t kSmoothIntervalToleranceUs = 200;
// The whole reservoir tail: the delay covers the largest lateness seen in
// the last thousand admitted frames plus the margin, so a late present is
// something the exclusions below deliberately left out (a host stall and
// the frames bunched behind it), never ordinary jitter.
constexpr uint64_t kPlayoutPercentilePerMille = 1000;
constexpr uint64_t kPlayoutBurstExclusionPerMille = 750;
// Smooth frame timing blends the predicted slot equally with the raw mapped
// timestamp. Track gradual source-rate changes with a ten-percent period EMA
// and cap positive retiming at 2 ms. Reuse the existing playout headroom for
// early retiming; the adjustment cap is not a bound on total client latency.
// Unchecked sessions retain timestamp-following playout. Schema defaults and
// explicit captured parameters preserve historical replay behavior.
constexpr uint64_t kPlayoutSmoothingGainPerMille = 500;
constexpr uint64_t kPlayoutSmoothingPeriodAlphaPerMille = 100;
constexpr uint64_t kPlayoutSmoothingMaxLagUs = 2000;
// Retired metronome playout, kept reachable for replay. It advances the
// presented slot by the fitted source period, corrects phase toward the mapped
// sender clock by a bounded step, and moves a frame that cannot make its tick
// to the next tick rather than presenting it early.
constexpr uint64_t kPlayoutStartPeriodPerMille = 950;
constexpr uint64_t kPlayoutMaximumPeriodPerMille = 950;
constexpr uint64_t kPlayoutMetronomeSnapPerMille = 3000;
// A provisional cadence segment after a major departure must span this much
// sender time before its fit replaces the source rate. Three long stamps
// used to be enough, so a four-frame host hitch became a 30 Hz source and
// every rate-dependent term followed it there and back.
constexpr uint64_t kRateCandidateMinimumUs = 200000;
// Preparation acquires the next swapchain image, and on a compositor that
// releases the previous image only when it flips, an acquire that follows
// the previous present too closely blocks for up to several display periods.
// Preparation that starts this long after the previous present did not
// block; the minimum lead keeps enough time for the preparation itself.
constexpr uint64_t kRenderStartAfterSubmissionUs = 6000;
constexpr uint64_t kRenderStartMinimumLeadUs = 2500;
// With the decoder's GPU work synced before preparation, the learned lead
// collapses to the 0.6 ms render and no longer covers the sporadic 2 to 3 ms
// renders; the floor keeps that headroom.
constexpr uint64_t kRenderLeadFloorUs = 3000;
// Consecutive frames that must map more than a period into the future before
// the sender clock is considered to have jumped. One early outlier used to
// re-seed the mapping on itself and make every following frame late.
constexpr uint64_t kPlayoutOffsetReseedFrames = 3;

uint64_t clampUnsigned(uint64_t value, uint64_t low, uint64_t high)
{
    if (high < low) {
        high = low;
    }
    return std::max(low, std::min(value, high));
}

template<typename T>
T percentile(const std::deque<T>& values, unsigned int requestedPercentile)
{
    if (values.empty()) {
        return 0;
    }
    std::vector<T> ordered(values.begin(), values.end());
    std::sort(ordered.begin(), ordered.end());
    const unsigned int percentileValue = std::min(100U, requestedPercentile);
    const size_t rank = std::max<size_t>(
        1, (ordered.size() * percentileValue + 99) / 100);
    return ordered[rank - 1];
}

template<typename T>
void appendBounded(std::deque<T>& values, T value, size_t limit)
{
    while (values.size() >= limit) {
        values.pop_front();
    }
    values.push_back(value);
}

uint64_t intervalQualityWindowUs(const VrrTimingParameters& parameters)
{
    // Revision 7 originally captured playoutReadinessWindowUs for the
    // readiness estimator, while IntervalBuffer used a fixed 30-second score
    // history. Identify the new preset tuples here so old revision-7 traces
    // keep their exact replay behavior without adding a trace-schema field.
    if (parameters.playoutOnTimeTargetPerMillion == 990000 &&
        parameters.playoutReadinessWindowUs == 60000000) {
        return 60000000;
    }
    if (parameters.playoutOnTimeTargetPerMillion == 995000 &&
        parameters.playoutReadinessWindowUs == 120000000) {
        return 120000000;
    }
    if (parameters.playoutOnTimeTargetPerMillion == 999900 &&
        parameters.playoutReadinessWindowUs == 300000000) {
        return 300000000;
    }
    return 30000000;
}

uint64_t intervalQualityToleranceUs(const VrrTimingParameters& parameters)
{
    // Revision 7 originally used the shared 500 us default. Identify only
    // the current Smooth tuple so existing revision-7 traces replay exactly;
    // explicit revision 8 retains its historical 250 us tolerance.
    if (parameters.playoutResponsiveBuffer == 8) {
        return 250;
    }
    if (parameters.playoutResponsiveBuffer >= 7 &&
        parameters.playoutOnTimeTargetPerMillion == 999900 &&
        parameters.playoutReadinessWindowUs == 300000000) {
        return kSmoothIntervalToleranceUs;
    }
    return Vrr13::IntervalBuffer::ToleranceUs;
}

} // namespace

VrrTimingParameters vrrTimingParametersForSession(
    const VrrSessionConfig& config)
{
    // Present on a tracked source cadence plus a learned delay. The readiness
    // reserve, its per-frame slewing, and every phase re-anchor are off on this
    // path: they each moved the target between frames the source had spaced
    // evenly. Preparation readiness is a separate, bounded head start: it
    // moves renderStart earlier without moving the presentation deadline.
    // Explicit parameters keep older policies replayable.
    VrrTimingParameters parameters;
    // Mode zero is the new Smooth profile. Captured parameters retain their
    // own defaults for exact replay.
    const int latencyMode = config.latencyMode >= 0 && config.latencyMode <= 2 ?
        config.latencyMode : 1;
    parameters.latencyFixEnabled = config.latencyFix || latencyMode != 0 ? 1 : 0;
    parameters.latencyFixAllRates = latencyMode != 0 ? 1 : 0;
    parameters.latencyFixDelayPeriodPerMille = latencyMode == 2 ? 0 : 500;
    parameters.playoutDelayCapSourcePeriodPerMille = config.latencyFix ? 0 :
        latencyMode == 2 ? 500 : latencyMode == 1 ? 1000 :
        kSmoothPlayoutCapSourcePeriodPerMille;
    // The nominal 116 Hz period was shorter than the measured ~99 Hz source
    // in the deep capture, so it clipped the queue exactly when GPU stalls
    // needed more room. New live sessions use the fitted source period;
    // captured policies retain the old nominal-period behavior by default.
    parameters.playoutDelayCapUsesObservedPeriod = 1;
    parameters.playoutCapacityTelemetry = 1;
    parameters.playoutGpuReadinessAdaptation = 1;
    parameters.playoutPredictionOnly = 1;
    // Every normal VRR session uses the interval-quality queue. Historical
    // policies remain selectable only through explicit diagnostic parameters.
    parameters.playoutResponsiveBuffer = config.readinessHitchFeedback ? 0 : 7;
    // Retain earned protection between bursts instead of repeatedly shedding
    // it and reacquiring it. Explicit captured values preserve older release.
    parameters.playoutMeanMissHoldUs = latencyMode == 2 ? 6000000 : latencyMode == 1 ? 8000000 : 10000000;
    parameters.playoutMeanMissReleaseUsPerSecond = latencyMode == 0 ? 50 :
        latencyMode == 2 ? 125 : 100;
    parameters.playoutOnTimeTargetPerMillion = latencyMode == 2 ? 990000 :
        latencyMode == 1 ? 995000 : 999900;
    parameters.playoutReadinessWindowUs = latencyMode == 2 ? 60000000 :
        latencyMode == 1 ? 120000000 : 300000000;
    parameters.playoutReadinessHitchThresholdUs = config.readinessHitchFeedback ? 2000 : 0;
    parameters.playoutNativeHitchAdaptation = 0;
    // Display observations remain diagnostics; the queue uses interval quality.
    parameters.playoutRequireDisplayEvents = 1;
    parameters.playoutSubmissionEstimateFallback = 1;
    parameters.playoutReadinessDrivenAdaptation = 1;
    parameters.playoutStableSmoothnessReference = 1;
    parameters.renderStartPreserveLearnedLead = 1;
    parameters.playoutPredictionEnabled = 1;
    parameters.playoutSmoothnessFeedbackEnabled = 1;
    parameters.playoutDelayMarginUs = parameters.playoutResponsiveBuffer ? 500 : 3000;
    parameters.playoutDelayAttackUs = 500;
    parameters.playoutAdaptiveOnly = 0;
    parameters.playoutPerFrameLatch = 2;
    parameters.playoutRateProtectionEnabled = 0;
    parameters.playoutHistoryEnabled = 1;
    parameters.timestampPlayoutEnabled = 1;
    // Do not let an ineligible desktop/cadence transition poison the next
    // game's clock floor. Keep the applied phase and recover gradually from
    // fresh cadence evidence, without reseeding the retained playout buffer.
    parameters.playoutOffsetCadenceGate = 1;
    // Match the former 20 us/frame at 120 FPS in elapsed time. At 60 FPS the
    // mapper may now correct 40 us/frame instead of taking twice as long.
    // A per-observation cap prevents gaps from buying a large phase jump.
    parameters.playoutOffsetSlewUsPerSecond = 2400;
    // Local GPU and worker backlog must not age the sender-clock model or buy
    // a larger correction. The unwrapped RTP timeline supplies elapsed time;
    // decode completion still supplies the readiness offset being corrected.
    parameters.playoutOffsetSourceClock = 1;
    parameters.playoutOffsetMaximumStepUs = 100;
    parameters.playoutDelayAdaptive = 1;
    parameters.sourcePlayoutDelayUs = kFixedPlayoutDelayUs;
    parameters.playoutDelayStartUs = kPlayoutStartUs;
    parameters.playoutDelayMinimumUs = kPlayoutMinimumUs;
    parameters.playoutDelayMaximumUs = latencyMode == 0 ?
        kSmoothPlayoutMaximumUs : 16000;
    parameters.playoutDelayPercentilePerMille = kPlayoutPercentilePerMille;
    parameters.playoutBurstExclusionPerMille = kPlayoutBurstExclusionPerMille;
    // The independent smoothing preference trades timestamp fidelity for
    // steadier cadence. The timing profile still bounds adaptive buffering.
    parameters.playoutSmoothingGainPerMille =
        kPlayoutSmoothingGainPerMille;
    parameters.playoutSmoothingPeriodAlphaPerMille =
        kPlayoutSmoothingPeriodAlphaPerMille;
    parameters.playoutSmoothingMaxLagUs = kPlayoutSmoothingMaxLagUs;
    parameters.playoutMetronomeEnabled = 0;
    parameters.playoutDelayStartPeriodPerMille = kPlayoutStartPeriodPerMille;
    // A slower desktop/source must not expand the configured-rate ceiling.
    // Source-relative preset caps still impose their smaller limit. Captured
    // parameters retain their recorded limit for exact replay.
    parameters.playoutDelayMaximumPeriodPerMille = 0;
    parameters.playoutSmoothingSnapPerMille = kPlayoutMetronomeSnapPerMille;
    parameters.playoutOffsetReseedFrames = kPlayoutOffsetReseedFrames;
    parameters.playoutDelaySlewAcrossBands = 1;
    // Preparing on arrival is replay-only: on the Vulkan desktop path the
    // preparation blocks in the swapchain when it follows a present too
    // closely, so the worker sat busy and the next frame aged past the
    // stale limit.
    parameters.playoutPrepareOnArrival = 0;
    parameters.renderStartAfterSubmissionUs = kRenderStartAfterSubmissionUs;
    parameters.renderStartMinimumLeadUs = kRenderStartMinimumLeadUs;
    parameters.renderLeadFloorUs = kRenderLeadFloorUs;
    parameters.rateCandidateMinimumUs = kRateCandidateMinimumUs;
    parameters.playoutStallBurstExclusion = 1;
    parameters.latchedFloorDisabled = 1;
    parameters.pacingLatencyQueueModeExtra = 0;
    if (!config.smoothFrameTiming) {
        // Preserve the mapped RTP intervals instead of regularizing the
        // source cadence. Keep the adaptive delay, readiness constraints,
        // and display-spacing floor in effect. Disable both the production
        // gain smoother and the replay-compatible metronome.
        parameters.playoutMetronomeEnabled = 0;
        parameters.playoutSmoothingGainPerMille = 0;
    }
    return parameters;
}

VrrTimingController::VrrTimingController(const VrrSessionConfig& config,
                                         bool canLatchPresentation) :
    VrrTimingController(config, canLatchPresentation, VrrTimingParameters {})
{
}

VrrTimingController::VrrTimingController(const VrrSessionConfig& config,
                                         bool canLatchPresentation,
                                         const VrrTimingParameters& parameters) :
    m_Config(config),
    m_Parameters(parameters),
    m_CanLatchPresentation(canLatchPresentation)
{
    reset();
}

void VrrTimingController::reset()
{
    m_DisplayPeriodUs = periodForRate(m_Config.displayRefreshHz, 16667);
    m_ConfiguredStreamPeriodQ16 = periodForRateQ16(
        m_Config.streamRateHz, m_DisplayPeriodUs * kQ16One);
    m_ConfiguredStreamPeriodUs = std::max<uint64_t>(
        1, roundedQ16(m_ConfiguredStreamPeriodQ16));
    m_BaseGuardUs = clampUnsigned(
        m_DisplayPeriodUs / m_Parameters.baseGuardDivisor,
        m_Parameters.minimumGuardUs,
        m_Parameters.maximumBaseGuardUs);

    m_HaveLastSubmission = false;
    m_LastSubmissionUs = 0;
    m_CleanSpacingFrames = 0;
    m_PhaseErrorFrames = 0;
    clearTimeline(false);
}

void VrrTimingController::rebase()
{
    clearTimeline(true);
}

void VrrTimingController::clearTimeline(bool retainLearnedBudgets)
{
    const uint64_t previousReadinessDemandUs = m_ReadinessDemandUs;
    const uint64_t previousAppliedReadinessReserveUs =
        m_AppliedReadinessReserveUs;
    const bool previousReadinessModelValid = m_ReadinessModelValid;
    const uint64_t previousRenderBaselineUs = m_RenderBaselineUs;
    const uint64_t previousRenderLeadUs = m_RenderLeadUs;
    const uint64_t previousRenderWakeLeadUs = m_RenderWakeLeadUs;
    const uint64_t previousTargetWakeLeadUs = m_TargetWakeLeadUs;
    const uint64_t previousGpuReadinessLeadUs = m_GpuReadinessLeadUs;
    const uint64_t previousGuardUs = m_GuardUs;

    m_SourcePeriodUsQ16 = m_ConfiguredStreamPeriodQ16;
    m_SourcePeriodUs = std::max<uint64_t>(
        1, roundedQ16(m_SourcePeriodUsQ16));
    m_LatencyFixActive = false;
    updateLatencyFixState();
    m_MetronomePeriodUsQ16 = m_ConfiguredStreamPeriodQ16;
    m_LatchedPresentation = m_Parameters.playoutAdaptiveOnly ? false :
        m_Parameters.playoutRateProtectionEnabled ?
        rateProtectedPresentation() : m_CanLatchPresentation && m_SourcePeriodUs <
        saturatingAdd(m_DisplayPeriodUs,
                      latchedPresentationHeadroomUs());
    m_ReadinessBudgetUs = 0;
    m_ReadinessPhaseUs = 0;
    m_ReadinessDemandUs = retainLearnedBudgets ?
        previousReadinessDemandUs : m_Parameters.coldStartReadinessDemandUs;
    m_AppliedReadinessReserveUs = retainLearnedBudgets &&
            m_Parameters.retainReadinessOnPhaseReset != 0 ?
        previousAppliedReadinessReserveUs :
        m_Parameters.coldStartReadinessDemandUs;
    m_ReadinessModelValid = retainLearnedBudgets &&
        previousReadinessModelValid;
    if (timestampPlayoutEnabled()) {
        // The fixed playout delay is the whole buffer. No learned reserve is
        // applied or reported on top of it.
        m_ReadinessDemandUs = 0;
        m_AppliedReadinessReserveUs = 0;
        m_ReadinessModelValid = false;
    }
    resetPlayoutOffsets();
    m_WorkloadEpisode.reset();
    m_ReadinessPrediction.reset();
    m_CadenceStableSinceUs = 0;
    m_PreviousSmoothingIntervalUs = 0;
    m_ReadinessFeedback.reset();
    m_MeanMissBuffer.breakSequence();
    m_IntervalBuffer.breakSequence();
    m_PresentationPrediction.reset();
    m_SubmissionSmoothness.breakSequence();
    m_NativeSmoothness.breakSequence();
    m_FeedbackModeValid = false;
    // Learned per-band delays survive a source-phase rebase like the other
    // learned budgets; only a full reset discards them.
    if (!retainLearnedBudgets) {
        m_MeanMissBuffer.reset();
        m_IntervalBuffer.reset();
        m_SubmissionSmoothness.reset();
        m_NativeSmoothness.reset();
        m_RequestedPlayoutDelayUs = 0;
        m_UnclampedRequestedPlayoutDelayUs = 0;
        m_GpuReadinessLeadUs = 0;
        m_RecentReadiness = m_Parameters.playoutResponsiveBuffer >= 3 ?
            Vrr13::RecentReadiness(m_Parameters.playoutReadinessWindowUs,
                                  m_Parameters.playoutOnTimeTargetPerMillion,
                                  m_Parameters.playoutResponsiveBuffer >= 4) :
            Vrr13::RecentReadiness{};
        m_PlayoutHistory = Vrr13::Reserve(m_Parameters.playoutResponsiveBuffer ? 20 :
            m_Parameters.playoutReadinessHitchThresholdUs ? 19 :
            m_Parameters.playoutPredictionOnly ? 18 :
            m_Parameters.playoutNativeHitchAdaptation ? 17 :
            m_Parameters.playoutReadinessDrivenAdaptation ? 16 :
            m_Parameters.playoutSmoothnessFeedbackEnabled ? 15 : m_Parameters.playoutPredictionEnabled ? 14 : 13);
        m_LastHistoryArrivalUs = 0;
        m_PlayoutBands.clear();
        m_PlayoutBandValid = false;
        m_AppliedPlayoutDelayUs = 0;
        m_AppliedPlayoutDelayValid = false;
    }
    m_HaveLastDecodeComplete = false;
    m_LastDecodeCompleteUs = 0;
    resetCadenceSmoothing();
    m_SmoothedPeriodUs = 0;
    m_MotionResiduals.clear();
    m_FutureProjectionFrames = 0;
    m_BurstExclusionFrames = 0;
    m_HaveTimeline = false;
    m_SourceTimeUs = 0;
    m_SourceTimeUsQ16 = 0;
    m_SourceFrameOrdinal = 0;
    m_UnwrappedRtpTicks = 0;
    m_LastFrameNumber = -1;
    m_HaveLastFrameNumber = false;
    m_LastRtpTimestamp = 0;
    m_LastTimestampValid = false;
    m_RtpConversionRemainder = 0;
    m_FrameConversionRemainder = 0;
    m_LastCadenceUsedRtp = false;
    m_PhaseErrorFrames = 0;
    m_CadenceStabilityLatchFramesRemaining = 0;

    m_CadenceSamples.clear();
    m_RateCandidateSamples.clear();
    m_ReadyOffsets.clear();
    m_PreparationDurations.clear();
    m_RenderSchedulerDelays.clear();
    m_TargetSchedulerDelays.clear();
    // A phase rebase invalidates sample timestamps, but the bounded reserve
    // itself is still useful immediately in the new source epoch.
    m_GpuReadinessSamples.clear();
    m_LastGpuReadinessUpdateUs = 0;
    m_Pending = PendingFrame {};

    if (retainLearnedBudgets) {
        m_RenderBaselineUs = std::min(previousRenderBaselineUs,
                                      m_SourcePeriodUs);
        m_RenderLeadUs = clampUnsigned(previousRenderLeadUs,
                                       renderLeadFloorUs(),
                                       renderLeadCeilingUs());
        m_RenderWakeLeadUs = std::min(previousRenderWakeLeadUs,
                                      m_Parameters.maximumRenderWakeLeadUs);
        m_TargetWakeLeadUs = std::min(previousTargetWakeLeadUs,
                                      m_Parameters.maximumTargetWakeLeadUs);
        m_GpuReadinessLeadUs = std::min(previousGpuReadinessLeadUs,
                                        gpuReadinessCeilingUs());
        m_GuardUs = clampUnsigned(previousGuardUs,
                                  m_BaseGuardUs,
                                  guardCeilingUs());
    }
    else {
        // Until measurements arrive, treat the historical 1 ms lead as
        // unavoidable render work rather than pacing latency insurance.
        m_RenderBaselineUs = std::min(m_Parameters.renderLeadFloorUs,
                                      m_SourcePeriodUs);
        m_RenderLeadUs = clampUnsigned(m_Parameters.renderLeadFloorUs,
                                       renderLeadFloorUs(),
                                       renderLeadCeilingUs());
        m_RenderWakeLeadUs = 0;
        m_TargetWakeLeadUs = 0;
        m_GpuReadinessLeadUs = 0;
        m_GuardUs = m_BaseGuardUs;
    }
    clampReadinessReserveToPolicy();
}

void VrrTimingController::initializeTimeline(const PacedFrame& frame)
{
    m_HaveTimeline = true;
    anchorSourceTime(frame.decodeCompleteUs());
    m_SourceFrameOrdinal = 0;
    m_UnwrappedRtpTicks = 0;
    m_LastFrameNumber = frame.frameNumber();
    m_HaveLastFrameNumber = frame.frameNumber() >= 0;
    m_LastRtpTimestamp = frame.rtpTimestamp();
    m_LastTimestampValid = frame.timestampValid();
    m_CadenceSamples.clear();
    m_RateCandidateSamples.clear();
    if (frame.timestampValid()) {
        m_CadenceSamples.push_back(CadenceSample {});
    }
}

VrrTimingDecision VrrTimingController::schedule(const PacedFrame& frame,
                                                 uint64_t nowUs)
{
    m_Pending = PendingFrame {};

    CadenceObservation cadence;
    bool rebased = false;
    if (!m_HaveTimeline) {
        initializeTimeline(frame);
        rebased = true;
    }
    else {
        const bool frameNumberReset =
            m_HaveLastFrameNumber && frame.frameNumber() >= 0 &&
            frame.frameNumber() <= m_LastFrameNumber;

        if (frameNumberReset) {
            rebase();
            initializeTimeline(frame);
            rebased = true;
        }
        else {
            cadence = observeCadence(frame);
            if (cadence.needsRebase) {
                rebase();
                initializeTimeline(frame);
                rebased = true;
            }
            else {
                const uint64_t maximum = std::numeric_limits<uint64_t>::max();
                const uint64_t projectedMovementQ16 =
                    cadence.frameDelta != 0 &&
                    m_SourcePeriodUsQ16 > maximum / cadence.frameDelta ?
                        maximum : m_SourcePeriodUsQ16 * cadence.frameDelta;
                m_SourceTimeUsQ16 = saturatingAdd(m_SourceTimeUsQ16,
                                                   projectedMovementQ16);
                m_SourceTimeUs = roundedQ16(m_SourceTimeUsQ16);

                if (cadence.phaseDiscontinuity) {
                    // A large cadence transition or isolated source gap is a
                    // local phase event, not a reason to forget the learned
                    // rate. Anchor the live one-slot path to the ready frame
                    // while the cumulative estimator confirms or abandons its
                    // provisional segment.
                    anchorSourceTime(frame.decodeCompleteUs());
                }

                m_LastFrameNumber = frame.frameNumber();
                m_HaveLastFrameNumber = frame.frameNumber() >= 0;
                m_LastRtpTimestamp = frame.rtpTimestamp();
                m_LastTimestampValid = frame.timestampValid();
            }
        }
    }

    updateLatencyFixState();

    // Timestamp playout: the target is the sender timestamp mapped into the
    // local clock plus one constant delay. The mapping offset is the windowed
    // minimum of decode-complete minus RTP time. Live sessions age samples
    // and bound correction by elapsed observation time, independent of FPS;
    // historical captures retain per-frame slewing. Steady-state corrections
    // only move adjacent targets by the bounded step. Nothing below re-anchors on
    // a late or early frame: a late frame simply clamps to "now" and the next
    // frame returns to its own slot.
    const bool timestampPlayout = timestampPlayoutEnabled() &&
        frame.timestampValid() && (rebased || cadence.usedRtpTimestamp);
    m_TimestampPlayoutActive = timestampPlayout;
    const uint64_t rtpUs = timestampPlayout ?
        rtpTicksToUs(m_UnwrappedRtpTicks) : 0;
    int64_t readyOffsetUs = 0;
    int64_t smoothingUs = 0;
    uint64_t missedTicks = 0;
    uint64_t delayBeforeUs = 0;
    const uint64_t leadUs = saturatingAdd(m_Parameters.playoutPredictionEnabled ? typicalRenderUs() : m_RenderLeadUs,
                                          m_Parameters.presentationSafetyUs);
    if (timestampPlayout) {
        const int64_t offsetUs = signedDifference(frame.decodeCompleteUs(),
                                                  rtpUs);
        const bool timeBasedOffset =
            m_Parameters.playoutOffsetSlewUsPerSecond != 0;
        const uint64_t offsetClockUs = timeBasedOffset ?
            (m_Parameters.playoutOffsetSourceClock != 0 ? rtpUs : nowUs) :
            frame.decodeCompleteUs();
        const int64_t appliedOffsetUs = observePlayoutOffset(
            offsetClockUs,
            offsetUs, rebased || cadence.eligible, cadence.phaseDiscontinuity);
        anchorSourceTime(addSigned(rtpUs, appliedOffsetUs));
        readyOffsetUs = signedDifference(frame.decodeCompleteUs(),
                                         m_SourceTimeUs);
        m_ReadinessBudgetUs = 0;
        m_ReadinessPhaseUs = 0;
        m_PhaseErrorFrames = 0;
        // The smoothed slot is decided against the delay in force before this
        // frame's lateness is admitted; the delay moves at most a few
        // microseconds per frame so the difference is immaterial. The
        // calibrator then sees lateness against the slot actually used, so
        // a schedule that runs ahead of a late-stamped frame is paid for by
        // the delay rather than by a late present.
        delayBeforeUs = effectivePlayoutDelayUs();
        const uint64_t rawBasisUs = saturatingAdd(m_SourceTimeUs,
                                                  delayBeforeUs);
        int64_t remainingDebtUs = 0;
        if (metronomeEnabled()) {
            smoothingUs = metronomeAdjustUs(
                cadence, rebased, rawBasisUs, delayBeforeUs, nowUs,
                missedTicks, remainingDebtUs);
        }
        else {
            smoothingUs = cadenceSmoothingAdjustUs(
                cadence, rebased, rawBasisUs, delayBeforeUs);
        }
        // The calibrator sees lateness against the slot the schedule is
        // trying to reach, not the slot it currently occupies: while the
        // tick still carries lag from a late frame, on-time arrivals would
        // otherwise look early and the cushion would release until frames
        // were late all the time.
        updatePlayoutDelay(frame, cadence, rebased,
                           readyOffsetUs - (smoothingUs - remainingDebtUs),
                           nowUs);
    }
    else {
        resetCadenceSmoothing();
        readyOffsetUs = signedDifference(frame.decodeCompleteUs(),
                                         m_SourceTimeUs);
        if (!rebased && !cadence.phaseDiscontinuity && cadence.eligible) {
            const int64_t ceilingUs =
                static_cast<int64_t>(readinessCeilingUs());
            if (readyOffsetUs < -ceilingUs) {
                // A frame that is ready well before the old slower clock must
                // not wait behind an obsolete cutscene cadence. The display
                // floor and latched near-refresh mode still bound how quickly
                // it can submit.
                anchorSourceTime(frame.decodeCompleteUs());
                readyOffsetUs = 0;
                cadence.phaseDiscontinuity = true;
                cadence.eligible = false;
                m_PhaseErrorFrames = 0;
            }
            else if (readyOffsetUs > ceilingUs) {
                ++m_PhaseErrorFrames;
            }
            else {
                m_PhaseErrorFrames = 0;
            }

            if (!cadence.phaseDiscontinuity &&
                    m_PhaseErrorFrames >= m_Parameters.phaseErrorFrames) {
                // A bounded readiness reserve cannot repay a sustained source
                // phase error. Re-anchor locally while retaining the
                // cumulative cadence fit, rather than repeatedly rebasing the
                // whole model.
                anchorSourceTime(frame.decodeCompleteUs());
                readyOffsetUs = 0;
                cadence.phaseDiscontinuity = true;
                cadence.eligible = false;
                m_PhaseErrorFrames = 0;
            }
        }
        else {
            m_PhaseErrorFrames = 0;
        }
        if (rebased || cadence.sourceRateChanged ||
            cadence.phaseDiscontinuity) {
            // A new source epoch or local phase recovery is anchored by the
            // first directly observed ready offset. Cadence history is
            // retained for the phase-only cases above.
            m_ReadyOffsets.clear();
            const int64_t ceilingUs =
                static_cast<int64_t>(readinessCeilingUs());
            m_ReadinessPhaseUs = std::max(
                -ceilingUs, std::min(readyOffsetUs, ceilingUs));
            // A source-phase reset must not acquire a standing reserve in one
            // cadence-breaking jump. Start on the observed phase and let
            // clean arrival evidence build or release the reserve smoothly.
            const bool retainReserve =
                m_Parameters.retainReadinessOnPhaseReset != 0;
            applyReadinessBudget(retainReserve, retainReserve);
        }
    }

    // The metronome tick was placed against the delay in force when the raw
    // slot was mapped; applying the calibrator's newer value here would put
    // its slew on the presented interval. It reaches the schedule through
    // the next frame's raw slot instead.
    const uint64_t proposedPlayoutDelayUs = timestampPlayout ?
        (metronomeEnabled() ? delayBeforeUs : effectivePlayoutDelayUs()) :
        m_Parameters.sourcePlayoutDelayUs;
    const uint64_t playoutDelayUs =
        std::min(proposedPlayoutDelayUs, playoutDelayCapUs());
    const uint64_t renderOffsetUs = m_Parameters.playoutPredictionEnabled ? typicalRenderUs() : m_RenderLeadUs;
    const uint64_t compositorLeadUs = m_Parameters.playoutPredictionEnabled &&
        !m_Parameters.playoutPredictionOnly ? m_PresentationPrediction.lead(nowUs) : 0;
    uint64_t targetUs = saturatingAdd(
        addSigned(addSigned(m_SourceTimeUs, m_ReadinessBudgetUs),
                  smoothingUs),
        saturatingAdd(
            playoutDelayUs,
            saturatingAdd(renderOffsetUs,
                          m_Parameters.presentationSafetyUs)));
    const uint64_t originalTargetUs = targetUs;
    targetUs = std::max(
        targetUs,
        saturatingAdd(nowUs,
                      saturatingAdd(renderOffsetUs,
                                    m_Parameters.presentationSafetyUs)));

    // This is a live, one-slot path. An unconfirmed RTP/frame jump may
    // describe already-skipped content, never hundreds of milliseconds that
    // the client should wait again. Reseed poisoned playout phase without
    // discarding the cumulative cadence model.
    const uint64_t maximumDirectTargetUs = saturatingAdd(
        saturatingAdd(nowUs,
                      static_cast<uint64_t>(std::max<int64_t>(smoothingUs, 0))),
        saturatingAdd(
            std::max(m_ConfiguredStreamPeriodUs, m_SourcePeriodUs),
            saturatingAdd(
                playoutDelayUs,
                saturatingAdd(m_RenderLeadUs,
                              m_Parameters.presentationSafetyUs))));
    bool reseedPhase = targetUs > maximumDirectTargetUs;
    if (reseedPhase && timestampPlayout) {
        // Under timestamp playout one early outlier is not a clock jump.
        // The frame simply waits for its slot; only a run of frames that
        // all map into the future re-seeds the mapping.
        const uint64_t requiredFrames = std::max<uint64_t>(
            1, m_Parameters.playoutOffsetReseedFrames);
        ++m_FutureProjectionFrames;
        reseedPhase = m_FutureProjectionFrames >= requiredFrames;
    }
    else {
        m_FutureProjectionFrames = 0;
    }
    if (reseedPhase) {
        m_FutureProjectionFrames = 0;
        smoothingUs = 0;
        missedTicks = 0;
        resetCadenceSmoothing();
        // The metronome restarts on the re-seeded slot; the grid tick this
        // frame was given is not a basis to owe the jump against.
        m_Pending.hasSmoothedBasis = false;
        // Do not clear cadence history when a faster source makes the old
        // playout phase point into the future. Reseed phase from this already
        // decoded frame and let the cumulative fit heal the rate.
        anchorSourceTime(frame.decodeCompleteUs());
        m_ReadyOffsets.clear();
        m_ReadinessBudgetUs = 0;
        m_ReadinessPhaseUs = 0;
        if (timestampPlayout) {
            // The frame is more than a source period earlier than the mapped
            // clock predicts: the sender clock jumped. Re-seed the offset on
            // this frame rather than making it wait out a stale mapping.
            resetPlayoutOffsets();
            const bool timeBasedOffset =
                m_Parameters.playoutOffsetSlewUsPerSecond != 0;
            const uint64_t offsetClockUs = timeBasedOffset ?
                (m_Parameters.playoutOffsetSourceClock != 0 ? rtpUs : nowUs) :
                frame.decodeCompleteUs();
            observePlayoutOffset(
                offsetClockUs,
                signedDifference(frame.decodeCompleteUs(), rtpUs));
        }
        readyOffsetUs = 0;
        cadence.phaseDiscontinuity = true;
        cadence.eligible = false;
        m_PhaseErrorFrames = 0;
        targetUs = saturatingAdd(
            std::max(frame.decodeCompleteUs(), nowUs),
            saturatingAdd(
                playoutDelayUs,
                saturatingAdd(m_RenderLeadUs,
                              m_Parameters.presentationSafetyUs)));
    }

    if (m_Parameters.playoutAdaptiveOnly != 0) {
        m_LatchedPresentation = false;
    }
    else if (m_Parameters.playoutRateProtectionEnabled != 0) {
        // Stay protected throughout the near-refresh source-rate range,
        // including the first frame. Late CPU/GPU work must not briefly
        // switch this range back to an immediate tearing present.
        m_LatchedPresentation = rateProtectedPresentation();
    }
    else if (m_Parameters.playoutPerFrameLatch != 0) {
        // Judge this frame's planned submission. Revision 1 omitted the
        // headroom that VRR12 required beyond the display period and guard:
        // at 116/120 its 188 us remaining slack was incorrectly enough to
        // permit tearing. Restore that margin and full exit hysteresis without
        // adding it to the playout buffer or imposing a source-rate band.
        // Explicit revision 1 remains unchanged for historical exact replay.
        const uint64_t safetyHeadroomUs = m_Parameters.playoutPerFrameLatch >= 2 ?
            (m_LatchedPresentation ? latchedPresentationExitHeadroomUs() :
                                     latchedPresentationHeadroomUs()) : 0;
        const uint64_t safeAdaptiveUs = saturatingAdd(m_LastSubmissionUs,
            saturatingAdd(saturatingAdd(m_DisplayPeriodUs, m_GuardUs),
                          safetyHeadroomUs));
        m_LatchedPresentation = m_CanLatchPresentation && m_HaveLastSubmission &&
                                targetUs < safeAdaptiveUs;
    }
    const uint64_t unflooredTargetUs = targetUs;
    if (m_Parameters.playoutPredictionEnabled && !m_Parameters.playoutPredictionOnly &&
        !m_LatchedPresentation) {
        const auto scanoutFloor = m_PresentationPrediction.floor(nowUs, m_DisplayPeriodUs, m_GuardUs);
        targetUs = std::max(targetUs, scanoutFloor > compositorLeadUs ? scanoutFloor - compositorLeadUs : 0);
    }
    targetUs = std::max(targetUs, earliestSubmissionUs());
    const uint64_t presentationFloorPushUs = targetUs - unflooredTargetUs;
    // GPU readiness is learned independently from CPU/render preparation.
    // Keep it out of targetUs: it is an earlier start opportunity, not extra
    // presentation latency. The source-period bound prevents the worker from
    // preparing so early that one frame can occupy the whole pacing window.
    const uint64_t gpuReadinessLeadUs = this->gpuReadinessLeadUs();
    const uint64_t totalLeadUs = std::min(
        m_SourcePeriodUs,
        saturatingAdd(saturatingAdd(m_RenderLeadUs,
                                    m_RenderWakeLeadUs),
                      gpuReadinessLeadUs));
    uint64_t renderStartUs = targetUs > totalLeadUs ?
        targetUs - totalLeadUs : 0;
    if (timestampPlayout && m_Parameters.playoutPrepareOnArrival != 0) {
        // The playout delay already holds the frame for a whole cushion
        // before its slot; preparing it the moment it arrives spends that
        // cushion on the renderer too. Only for renderers whose preparation
        // never blocks on the previous present.
        const uint64_t arrivalLeadUs = saturatingAdd(totalLeadUs,
                                                     playoutDelayUs);
        renderStartUs = targetUs > arrivalLeadUs ?
            targetUs - arrivalLeadUs : 0;
    }
    if (m_Parameters.renderStartAfterSubmissionUs != 0 && m_HaveLastSubmission) {
        const uint64_t earliestStartUs = saturatingAdd(
            m_LastSubmissionUs, m_Parameters.renderStartAfterSubmissionUs);
        // Acquisition spacing may use spare lead, but must not squeeze the
        // learned preparation budget back to a fixed 2.5 ms. Increasing the
        // playout buffer cannot repair that loss of rendering opportunity.
        const uint64_t minimumLeadUs = m_Parameters.renderStartPreserveLearnedLead ?
            std::max(m_Parameters.renderStartMinimumLeadUs, totalLeadUs) :
            m_Parameters.renderStartMinimumLeadUs;
        const uint64_t latestStartUs =
            targetUs > minimumLeadUs ? targetUs - minimumLeadUs : 0;
        if (earliestStartUs > renderStartUs) {
            renderStartUs = std::min(earliestStartUs,
                                     std::max(renderStartUs, latestStartUs));
        }
    }

    VrrTimingDecision decision;
    decision.frameNumber = uint64_t(frame.frameNumber());
    decision.smoothnessProtectionUs = m_Parameters.playoutPredictionOnly ?
        m_SubmissionSmoothness.protectionUs() :
        m_Parameters.playoutNativeHitchAdaptation ?
            activeSmoothnessFeedback(nowUs).protectionUs() :
            std::max(m_SubmissionSmoothness.protectionUs(),
                     m_NativeSmoothness.protectionUs());
    decision.requestedPlayoutDelayUs =
        m_Parameters.playoutCapacityTelemetry != 0 ?
            m_UnclampedRequestedPlayoutDelayUs : m_RequestedPlayoutDelayUs;
    decision.playoutCapacityLimited =
        m_Parameters.playoutCapacityTelemetry != 0 &&
        m_UnclampedRequestedPlayoutDelayUs > playoutDelayMaximumUs();
    decision.submissionSmoothnessSamples = m_SubmissionSmoothness.samples();
    decision.submissionSmoothnessMisses = m_SubmissionSmoothness.misses();
    decision.nativeSmoothnessSamples = m_NativeSmoothness.samples();
    decision.nativeSmoothnessMisses = m_NativeSmoothness.misses();
    decision.originalScanoutUs = saturatingAdd(originalTargetUs, compositorLeadUs);
    decision.predictedScanoutUs = saturatingAdd(targetUs, compositorLeadUs);
    decision.compositorLeadUs = compositorLeadUs;
    decision.recoveryHeadroomUs = m_Parameters.playoutPredictionEnabled ? recoveryHeadroomUs() : 0;
    decision.originalTargetUs = originalTargetUs;
    decision.sourceTimeUs = m_SourceTimeUs;
    decision.sourceIntervalUs = cadence.intervalUs;
    decision.sourcePeriodUs = m_SourcePeriodUs;
    decision.readyOffsetUs = readyOffsetUs;
    decision.readinessBudgetUs = m_ReadinessBudgetUs;
    decision.playoutDelayUs = playoutDelayUs;
    decision.cadenceSmoothingUs = smoothingUs;
    decision.missedTicks = missedTicks;
    decision.renderStartUs = renderStartUs;
    decision.targetUs = targetUs;
    decision.presentationFloorPushUs = presentationFloorPushUs;
    decision.guardUs = m_GuardUs;
    decision.headroomUs = headroomUs();
    decision.timingBudgetUs = timingBudgetUs();
    decision.renderLeadUs = m_RenderLeadUs;
    decision.renderWakeLeadUs = m_RenderWakeLeadUs;
    decision.gpuReadinessLeadUs = gpuReadinessLeadUs;
    decision.targetWakeLeadUs = m_TargetWakeLeadUs;
    const uint64_t learnedHeadroomUs = decision.headroomUs;
    const bool cadenceUnstable = rebased || !cadence.eligible ||
        cadence.sourceRateChanged || cadence.phaseDiscontinuity;
    bool cadenceLatchActive = false;
    if (m_Parameters.cadenceStabilityLatchFrames != 0) {
        if (cadenceUnstable) {
            // Immediate tearing presents are only safe after the source phase
            // has remained coherent. A source hitch followed by a decoder
            // burst can otherwise queue several adaptive presents into one
            // scanout interval, overwriting frames and producing a visible
            // fluidity break even though the display has ample rate headroom.
            m_CadenceStabilityLatchFramesRemaining =
                m_Parameters.cadenceStabilityLatchFrames;
            cadenceLatchActive = true;
        }
        else if (m_CadenceStabilityLatchFramesRemaining != 0) {
            cadenceLatchActive = true;
            --m_CadenceStabilityLatchFramesRemaining;
        }
    }
    if (m_Parameters.playoutAdaptiveOnly != 0) {
        m_LatchedPresentation = false;
    }
    else if (m_Parameters.playoutRateProtectionEnabled != 0 ||
            m_Parameters.playoutPerFrameLatch != 0) {
        // Selected before applying the software floor above. Native latching
        // carries the frame to the next scanout only when this slot needs it.
    }
    else if (!m_CanLatchPresentation) {
        m_LatchedPresentation = false;
    }
    else if (cadenceLatchActive) {
        m_LatchedPresentation = true;
    }
    else if (m_LatchedPresentation) {
        // Production requires the full exit threshold so small guard or
        // cadence fluctuations cannot bounce a borderline stream between
        // adaptive and latched presentation. The base-guard shortcut remains
        // parameterized only to reproduce captures made under the legacy
        // absolute/scaled latch policies.
        if (learnedHeadroomUs >=
                latchedPresentationExitHeadroomUs() ||
            (m_Parameters.latchedPresentationBaseGuardExit != 0 &&
             m_GuardUs == m_BaseGuardUs &&
             learnedHeadroomUs >=
                latchedPresentationHeadroomUs())) {
            m_LatchedPresentation = false;
        }
    }
    else if (learnedHeadroomUs <
             latchedPresentationHeadroomUs()) {
        m_LatchedPresentation = true;
    }
    decision.latchedPresentation = m_LatchedPresentation;
    decision.usedRtpTimestamp = cadence.usedRtpTimestamp;
    decision.cadenceEligible = !rebased && cadence.eligible;
    decision.sourceRateChanged = !rebased && cadence.sourceRateChanged;
    decision.phaseDiscontinuity = !rebased && cadence.phaseDiscontinuity;
    decision.rebased = rebased;

    m_Pending.valid = true;
    m_Pending.smoothness = smoothnessSample(decision);
    m_Pending.smoothness.intended = decision.originalTargetUs;
    // Follow the host and deliberate judder correction. Buffer/render-budget
    // changes do not redefine the intended motion or conceal client errors.
    m_Pending.intervalIntendedUs = addSigned(decision.sourceTimeUs, decision.cadenceSmoothingUs);
    m_Pending.intervalValid = decision.usedRtpTimestamp && !decision.rebased && !decision.phaseDiscontinuity;
    // Timestamp playout never feeds the learned readiness reserve.
    m_Pending.cadenceEligible = decision.cadenceEligible && !timestampPlayout;
    m_Pending.readyOffsetUs = readyOffsetUs;
    if (m_Parameters.playoutPredictionEnabled) {
        const uint64_t stall = std::max(m_Parameters.playoutStallExclusionUs, scaledPerMille(m_SourcePeriodUs, 1500));
        // Padding must cover readiness relative to the cadence we actually
        // schedule, with the padding itself removed. Comparing only with raw
        // RTP time misses the extra readiness requirement of an earlier
        // smoothed slot. Intentional worker waits are excluded by the FIFO
        // readiness model; late native presentation is not readiness work.
        const uint64_t unpaddedSlotUs = m_Parameters.playoutReadinessDrivenAdaptation &&
            !m_Parameters.playoutResponsiveBuffer ?
            addSigned(m_SourceTimeUs, smoothingUs) : m_SourceTimeUs;
        m_Pending.prediction = {frame.decodeCompleteUs(), unpaddedSlotUs, m_SourcePeriodUs,
            typicalRenderUs(), playoutDelayUs,
            m_Parameters.playoutReadinessDrivenAdaptation ? 0 : recoveryHeadroomUs(),
            m_Parameters.playoutDelayMarginUs,
            frame.reassembledUs() && frame.decodeSubmitUs() >= frame.reassembledUs() ? frame.decodeSubmitUs() - frame.reassembledUs() : 0,
            timestampPlayout && !rebased && cadence.eligible && !cadence.phaseDiscontinuity &&
            cadence.frameDelta == 1 && cadence.intervalUs <= stall &&
            cadence.intervalUs >= scaledPerMille(m_SourcePeriodUs, m_Parameters.playoutBurstExclusionPerMille)};
        if (m_Parameters.playoutResponsiveBuffer) {
            // RTP spacing owns source timing even while the rate fit catches up.
            // Only actual delivery/FIFO lateness enters the raw predictor.
            auto& probe = m_Pending.prediction;
            probe.period = std::max(m_ConfiguredStreamPeriodUs, cadence.intervalUs);
            probe.eligible = timestampPlayout && !rebased && cadence.usedRtpTimestamp &&
                cadence.frameDelta == 1 && !cadence.phaseDiscontinuity;
            probe.smoothingAdvance = smoothingUs < 0 ? uint64_t(-smoothingUs) : 0;
            probe.accountReadinessSlack = m_Parameters.playoutResponsiveBuffer >= 2;
            if (m_Parameters.playoutResponsiveBuffer >= 3) {
                // A host stall or catch-up burst is not steady delivery jitter.
                // Its real output effect is still counted by readiness telemetry.
                probe.eligible = probe.eligible && cadence.intervalUs <=
                    std::max<uint64_t>(25000, m_SourcePeriodUs * 3 / 2) &&
                    cadence.intervalUs >= m_SourcePeriodUs / 2;
            }
        }
    }
    m_LastDecodeCompleteUs = frame.decodeCompleteUs();
    m_HaveLastDecodeComplete = true;
    if (timestampPlayout && metronomeEnabled()) {
        // The slot this frame occupies becomes the schedule basis only when
        // the frame is presented (noteSubmission), so a dropped or cancelled
        // frame frees its tick for the successor. A floor wait that pushed
        // the target past the tick is the slot actually used.
        const uint64_t basisUs = targetUs > leadUs ? targetUs - leadUs : 0;
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        const uint64_t basisUsQ16 = basisUs > maximum / kQ16One ?
            maximum : basisUs * kQ16One;
        if (!m_Pending.hasSmoothedBasis) {
            m_Pending.smoothedBasisUsQ16 = basisUsQ16;
            m_Pending.phaseDebtUs = 0;
            m_Pending.phaseResidualEmaUs = 0;
            m_Pending.basisMappingUs = m_AppliedPlayoutOffsetUs +
                static_cast<int64_t>(delayBeforeUs);
        }
        else if (basisUs > roundedQ16(m_Pending.smoothedBasisUsQ16)) {
            // A late arrival or a floor wait pushed the slot past the tick.
            // The schedule continues from the slot used and owes the
            // difference, which it pays back one bounded step at a time.
            m_Pending.phaseDebtUs += static_cast<int64_t>(
                basisUs - roundedQ16(m_Pending.smoothedBasisUsQ16));
            m_Pending.smoothedBasisUsQ16 = basisUsQ16;
        }
        m_Pending.hasSmoothedBasis = true;
        m_Pending.smoothedBasisOrdinal = m_SourceFrameOrdinal;
    }
    else if (timestampPlayout &&
            m_Parameters.playoutSmoothingGainPerMille != 0) {
        // VRR14 smooths the source schedule independently of readiness. Feeding
        // a late execution time back into that clock carries delay into later
        // frames instead of letting the available recovery headroom drain it.
        // Old captures retain VRR13's execution-anchored smoothing.
        const uint64_t basis = m_Parameters.playoutPredictionEnabled ? originalTargetUs : targetUs;
        m_LastSmoothedBasisUs = basis > leadUs ? basis - leadUs : 0;
        m_HaveSmoothedBasis = true;
    }
    else {
        resetCadenceSmoothing();
    }
    return decision;
}

void VrrTimingController::resetCadenceSmoothing()
{
    m_HaveSmoothedBasis = false;
    m_LastSmoothedBasisUs = 0;
    m_LastSmoothedBasisUsQ16 = 0;
    m_LastSmoothedBasisOrdinal = 0;
    m_PhaseDebtUs = 0;
    m_PhaseResidualEmaUs = 0;
    m_LastBasisMappingUs = 0;
}

bool VrrTimingController::metronomeEnabled() const
{
    return m_Parameters.playoutMetronomeEnabled != 0;
}

uint64_t VrrTimingController::motionThresholdUs(uint64_t periodUs) const
{
    const uint64_t floorUs = m_Parameters.playoutMotionFloorUs;
    const uint64_t ceilingUs = std::max(
        floorUs,
        scaledPerMille(periodUs,
                       m_Parameters.playoutMotionCeilingPeriodPerMille));
    if (m_MotionResiduals.size() < m_Parameters.playoutMotionMinimumSamples) {
        // Until the jitter bound is known, treat every deviation as noise
        // rather than re-anchoring the grid on a first-frame guess.
        return ceilingUs;
    }
    const uint64_t observedUs = percentile(
        m_MotionResiduals, m_Parameters.playoutMotionPercentile);
    return clampUnsigned(
        scaledPerMille(observedUs, m_Parameters.playoutMotionGainPerMille),
        floorUs, ceilingUs);
}

int64_t VrrTimingController::metronomeAdjustUs(
    const CadenceObservation& cadence, bool rebased, uint64_t rawBasisUs,
    uint64_t playoutDelayUs, uint64_t earliestBasisUs,
    uint64_t& missedTicks, int64_t& remainingDebtUs)
{
    missedTicks = 0;
    remainingDebtUs = 0;
    if (rebased || cadence.sourceRateChanged || cadence.phaseDiscontinuity ||
            !cadence.eligible || cadence.intervalUs == 0) {
        // A new epoch, a confirmed rate change, or too little cadence
        // history: present on the raw slot and restart the metronome there.
        resetCadenceSmoothing();
        return 0;
    }
    const uint64_t periodUs = std::max<uint64_t>(1, m_SourcePeriodUs);
    if (cadence.intervalUs > periodUs * 5 / 2) {
        // The content itself stalled. Show the stall rather than smear it,
        // and restart the metronome on this frame's slot.
        resetCadenceSmoothing();
        return 0;
    }
    if (!m_HaveSmoothedBasis) {
        return 0;
    }

    // Advance from the last presented slot by one fitted period per source
    // frame since it, so a locally skipped or dropped frame keeps the grid
    // anchored to the content.
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    const uint64_t periodQ16 = std::max(
        std::max<uint64_t>(kQ16One, m_ConfiguredStreamPeriodQ16),
        m_MetronomePeriodUsQ16);
    const uint64_t frames = m_SourceFrameOrdinal > m_LastSmoothedBasisOrdinal ?
        m_SourceFrameOrdinal - m_LastSmoothedBasisOrdinal : 1;
    const uint64_t advanceQ16 = frames > maximum / periodQ16 ?
        maximum : periodQ16 * frames;
    uint64_t tickQ16 = saturatingAdd(m_LastSmoothedBasisUsQ16, advanceQ16);

    const int64_t errorUs = signedDifference(roundedQ16(tickQ16), rawBasisUs);
    const uint64_t magnitudeUs = static_cast<uint64_t>(
        errorUs < 0 ? -errorUs : errorUs);
    const uint64_t snapUs = scaledPerMille(
        periodUs, m_Parameters.playoutSmoothingSnapPerMille);
    if (magnitudeUs > snapUs) {
        // The mapping moved by several periods (a confirmed clock re-seed or
        // a stall the drop policy did not fully shed). Re-phase here.
        resetCadenceSmoothing();
        return 0;
    }

    if (m_Parameters.playoutMotionDeadbandEnabled != 0) {
        // The stamp's deviation from the grid, excluding lag the schedule
        // already knows it owes. Beyond the learned jitter bound it is the
        // host reporting a change in frame timing: present on the stamp and
        // restart the grid there so the next frames are spaced from it.
        const int64_t deviationUs = errorUs - m_PhaseDebtUs;
        const uint64_t deviationMagnitudeUs = static_cast<uint64_t>(
            deviationUs < 0 ? -deviationUs : deviationUs);
        const uint64_t thresholdUs = motionThresholdUs(periodUs);
        if (deviationMagnitudeUs > thresholdUs) {
            // Not admitted to the jitter bound: a run of real timing changes
            // (a rate change the fit has yet to absorb) must not widen the
            // band until the grid treats them as noise. The grid's period
            // was wrong too, so it restarts on the current fit rather than
            // on the long filter that was still averaging the old rate.
            resetCadenceSmoothing();
            m_MetronomePeriodUsQ16 = m_SourcePeriodUsQ16;
            const uint64_t rawQ16 = rawBasisUs > maximum / kQ16One ?
                maximum : rawBasisUs * kQ16One;
            const uint64_t earliestRawQ16 =
                earliestBasisUs > maximum / kQ16One ?
                    maximum : earliestBasisUs * kQ16One;
            if (rawQ16 < earliestRawQ16) {
                missedTicks = (earliestRawQ16 - rawQ16) / periodQ16;
            }
            return 0;
        }
        appendBounded(m_MotionResiduals, deviationMagnitudeUs,
                      std::max<size_t>(1, m_Parameters.playoutMotionWindowFrames));
    }

    const uint64_t stepCeilingUs = std::max(
        m_Parameters.playoutPhaseStepMinimumUs,
        scaledPerMille(periodUs, m_Parameters.playoutPhaseStepPeriodPerMille));
    const uint64_t divisor = std::max<uint64_t>(
        1, m_Parameters.playoutPhaseStepDivisor);

    // Known movement of the clock mapping since the basis was committed
    // (offset tracking, delay attack or release) is owed to the schedule
    // exactly, in the opposite sense to lag the schedule took on itself.
    const int64_t mappingUs = m_AppliedPlayoutOffsetUs >
            std::numeric_limits<int64_t>::max() -
                static_cast<int64_t>(playoutDelayUs) ?
        std::numeric_limits<int64_t>::max() :
        m_AppliedPlayoutOffsetUs + static_cast<int64_t>(playoutDelayUs);
    int64_t debtUs = m_PhaseDebtUs - (mappingUs - m_LastBasisMappingUs);

    // Pay the debt first: exact, noise-free, and bounded per frame.
    const int64_t ceilingUs = static_cast<int64_t>(stepCeilingUs);
    const int64_t paidUs = std::max(-ceilingUs, std::min(debtUs, ceilingUs));
    debtUs -= paidUs;
    int64_t correctionUs = -paidUs;

    // Then track the slow residual with a long filter and a deadband, so
    // per-frame stamp wobble never becomes a step while clock drift and
    // fit error still do.
    const int64_t residualUs = errorUs - m_PhaseDebtUs;
    const int64_t windowFrames = static_cast<int64_t>(std::max<uint64_t>(
        1, m_Parameters.playoutPhaseResidualWindowFrames));
    int64_t emaUs = m_PhaseResidualEmaUs +
        (residualUs - m_PhaseResidualEmaUs) / windowFrames;
    const int64_t deadbandUs = static_cast<int64_t>(
        m_Parameters.playoutPhaseDeadbandUs);
    if (emaUs > deadbandUs || emaUs < -deadbandUs) {
        const uint64_t emaMagnitudeUs = static_cast<uint64_t>(
            emaUs < 0 ? -emaUs : emaUs);
        const uint64_t residualStepUs = std::min(
            clampUnsigned(emaMagnitudeUs / divisor,
                          m_Parameters.playoutPhaseStepMinimumUs,
                          stepCeilingUs),
            emaMagnitudeUs);
        // The step is applied to the schedule, so the filter must see it
        // too or it would keep asking for the same correction.
        if (emaUs > 0) {
            correctionUs -= static_cast<int64_t>(residualStepUs);
            emaUs -= static_cast<int64_t>(residualStepUs);
        }
        else {
            correctionUs += static_cast<int64_t>(residualStepUs);
            emaUs += static_cast<int64_t>(residualStepUs);
        }
    }
    m_Pending.phaseDebtUs = debtUs;
    m_Pending.phaseResidualEmaUs = emaUs;
    m_Pending.basisMappingUs = mappingUs;
    remainingDebtUs = debtUs;
    const uint64_t correctionMagnitudeQ16 =
        static_cast<uint64_t>(correctionUs < 0 ? -correctionUs : correctionUs) *
        kQ16One;
    tickQ16 = correctionUs < 0 ?
        (tickQ16 > correctionMagnitudeQ16 ? tickQ16 - correctionMagnitudeQ16 : 0) :
        saturatingAdd(tickQ16, correctionMagnitudeQ16);
    m_Pending.phaseDebtUs = debtUs;
    m_Pending.phaseResidualEmaUs = emaUs;

    // Never earlier than the mapped source clock itself.
    const uint64_t sourceBasisUs = rawBasisUs > playoutDelayUs ?
        rawBasisUs - playoutDelayUs : 0;
    const uint64_t sourceBasisQ16 = sourceBasisUs > maximum / kQ16One ?
        maximum : sourceBasisUs * kQ16One;
    tickQ16 = std::max(tickQ16, sourceBasisQ16);

    // A frame that arrives after its tick presents as soon as it is ready:
    // on a VRR panel one interval stretched by the shortfall is less visible
    // than a full repeated frame, and the schedule then continues from the
    // slot actually used and walks the lag back at the bounded step. The
    // caller applies that slip; the lag returned here is against the tick
    // the frame should have made, so the calibrator still sees the full
    // lateness and can grow the cushion to cover it. Whole periods of
    // shortfall are reported so the worker can shed a backlog by dropping a
    // frame that a fresher successor has already overtaken.
    const uint64_t earliestQ16 = earliestBasisUs > maximum / kQ16One ?
        maximum : earliestBasisUs * kQ16One;
    if (tickQ16 < earliestQ16) {
        missedTicks = (earliestQ16 - tickQ16) / periodQ16;
    }

    m_Pending.hasSmoothedBasis = true;
    m_Pending.smoothedBasisUsQ16 = tickQ16;
    m_Pending.smoothedBasisOrdinal = m_SourceFrameOrdinal;
    return signedDifference(roundedQ16(tickQ16), rawBasisUs);
}

int64_t VrrTimingController::cadenceSmoothingAdjustUs(
    const CadenceObservation& cadence, bool rebased, uint64_t rawBasisUs,
    uint64_t playoutDelayUs)
{
    if (m_Parameters.playoutResponsiveBuffer) {
        // Judge adjacent intervals together: alternating short/long frames are
        // exactly what the smoother is meant to handle, not a sustained change
        // of source rate. Keep the 200 ms recovery gate for actual transitions.
        const bool compensatingPair = m_Parameters.playoutResponsiveBuffer >= 2 &&
            m_PreviousSmoothingIntervalUs &&
            !withinPercent(m_PreviousSmoothingIntervalUs, m_SourcePeriodUs, 25) &&
            ((cadence.intervalUs < m_SourcePeriodUs && m_PreviousSmoothingIntervalUs > m_SourcePeriodUs) ||
             (cadence.intervalUs > m_SourcePeriodUs && m_PreviousSmoothingIntervalUs < m_SourcePeriodUs));
        const auto confidenceIntervalUs = compensatingPair ?
            (cadence.intervalUs + m_PreviousSmoothingIntervalUs) / 2 : cadence.intervalUs;
        const bool stable = !rebased && !cadence.sourceRateChanged &&
            !cadence.phaseDiscontinuity && cadence.eligible && cadence.frameDelta == 1 &&
            withinPercent(confidenceIntervalUs, m_SourcePeriodUs, 25);
        m_PreviousSmoothingIntervalUs = cadence.eligible && cadence.frameDelta == 1 &&
            !rebased && !cadence.phaseDiscontinuity ? cadence.intervalUs : 0;
        if (!stable) m_CadenceStableSinceUs = 0;
        else if (!m_CadenceStableSinceUs) m_CadenceStableSinceUs = m_SourceTimeUs;
        if (!m_CadenceStableSinceUs || m_SourceTimeUs < m_CadenceStableSinceUs ||
            m_SourceTimeUs - m_CadenceStableSinceUs < 200000) {
            resetCadenceSmoothing();
            return 0;
        }
    }
    const uint64_t gainPerMille = m_Parameters.playoutSmoothingGainPerMille;
    if (gainPerMille == 0 || gainPerMille >= 1000) {
        resetCadenceSmoothing();
        return 0;
    }
    if (rebased || cadence.sourceRateChanged || cadence.phaseDiscontinuity ||
            !cadence.eligible || cadence.intervalUs == 0) {
        // A new epoch, a material rate change, or too little cadence history
        // to trust: present on the raw slot and rebuild from here.
        resetCadenceSmoothing();
        return 0;
    }
    const uint64_t intervalUs = cadence.intervalUs;
    // The cumulative rate fit is the authority on the source period. The
    // tracked period only follows short-term drift around it; if the two
    // disagree by more than a quarter (a rate change the fit has absorbed,
    // or a bad seed from a startup burst) re-seed from the fit rather than
    // rejecting every interval as a stall from then on.
    const uint64_t fittedPeriodUs = std::max<uint64_t>(1, m_SourcePeriodUs);
    if (m_SmoothedPeriodUs == 0 ||
            m_SmoothedPeriodUs > fittedPeriodUs + fittedPeriodUs / 4 ||
            m_SmoothedPeriodUs + fittedPeriodUs / 4 < fittedPeriodUs) {
        m_SmoothedPeriodUs = fittedPeriodUs;
    }
    if (intervalUs > fittedPeriodUs * 5 / 2 ||
            intervalUs * 2 < fittedPeriodUs) {
        // A host stall or burst is not cadence. Keep the period estimate,
        // restart the schedule on this frame's raw slot.
        resetCadenceSmoothing();
        return 0;
    }
    else {
        const uint64_t alpha = std::min<uint64_t>(
            1000, m_Parameters.playoutSmoothingPeriodAlphaPerMille);
        const int64_t deltaUs = static_cast<int64_t>(intervalUs) -
            static_cast<int64_t>(m_SmoothedPeriodUs);
        m_SmoothedPeriodUs = static_cast<uint64_t>(
            static_cast<int64_t>(m_SmoothedPeriodUs) +
            deltaUs * static_cast<int64_t>(alpha) / 1000);
        if (m_SmoothedPeriodUs == 0) {
            m_SmoothedPeriodUs = 1;
        }
    }
    if (!m_HaveSmoothedBasis) {
        return 0;
    }
    const int64_t predictedUs = static_cast<int64_t>(
        saturatingAdd(m_LastSmoothedBasisUs, m_SmoothedPeriodUs));
    const int64_t errorUs = predictedUs - static_cast<int64_t>(rawBasisUs);
    const int64_t snapUs = static_cast<int64_t>(
        m_SmoothedPeriodUs * m_Parameters.playoutSmoothingSnapPerMille / 1000);
    if (errorUs > snapUs || errorUs < -snapUs) {
        resetCadenceSmoothing();
        return 0;
    }
    int64_t adjustUs = errorUs * static_cast<int64_t>(1000 - gainPerMille) / 1000;
    adjustUs = std::min(adjustUs, static_cast<int64_t>(
        m_Parameters.playoutSmoothingMaxLagUs));
    adjustUs = std::max(adjustUs, -static_cast<int64_t>(playoutDelayUs));
    return adjustUs;
}

VrrTimingController::CadenceObservation
VrrTimingController::observeCadence(const PacedFrame& frame)
{
    CadenceObservation observation;

    uint64_t frameDelta = 1;
    if (m_HaveLastFrameNumber && frame.frameNumber() >= 0) {
        if (frame.frameNumber() <= m_LastFrameNumber) {
            observation.needsRebase = true;
            return observation;
        }
        frameDelta = static_cast<uint64_t>(frame.frameNumber() -
                                           m_LastFrameNumber);
    }
    observation.frameDelta = frameDelta;

    if (frame.timestampValid() && m_LastTimestampValid) {
        const uint32_t wrappedDelta =
            frame.rtpTimestamp() - m_LastRtpTimestamp;
        if (wrappedDelta == 0 || wrappedDelta > 0x7fffffffU) {
            observation.needsRebase = true;
            return observation;
        }

        const uint64_t carriedRemainder = m_LastCadenceUsedRtp ?
            m_RtpConversionRemainder : 0;
        const uint64_t intervalNumerator =
            static_cast<uint64_t>(wrappedDelta) * kMicrosecondsPerSecond +
            carriedRemainder;
        const uint64_t intervalUs = intervalNumerator / kRtpClockRate;
        if (intervalUs > m_Parameters.maximumForwardMovementUs) {
            observation.needsRebase = true;
            return observation;
        }

        m_RtpConversionRemainder = intervalNumerator % kRtpClockRate;
        m_FrameConversionRemainder = 0;
        m_LastCadenceUsedRtp = true;

        observation.intervalUs = intervalUs;
        observation.usedRtpTimestamp = true;
        observeRtpCadence(wrappedDelta, observation);
        return observation;
    }

    if (m_Config.streamRateHz <= 0 ||
        frameDelta > static_cast<uint64_t>(m_Config.streamRateHz)) {
        observation.needsRebase = true;
        return observation;
    }

    const uint64_t carriedRemainder = !m_LastCadenceUsedRtp ?
        m_FrameConversionRemainder : 0;
    const uint64_t intervalNumerator =
        frameDelta * kMicrosecondsPerSecond + carriedRemainder;
    observation.intervalUs = intervalNumerator /
        static_cast<uint64_t>(m_Config.streamRateHz);
    m_FrameConversionRemainder = intervalNumerator %
        static_cast<uint64_t>(m_Config.streamRateHz);
    m_RtpConversionRemainder = 0;
    m_LastCadenceUsedRtp = false;
    m_CadenceSamples.clear();
    m_RateCandidateSamples.clear();
    observation.eligible = frameDelta == 1;
    return observation;
}

void VrrTimingController::observeRtpCadence(
    uint32_t rtpDelta, CadenceObservation& observation)
{
    const CadenceSample previous {
        m_SourceFrameOrdinal,
        m_UnwrappedRtpTicks,
    };
    m_SourceFrameOrdinal = saturatingAdd(m_SourceFrameOrdinal,
                                          observation.frameDelta);
    m_UnwrappedRtpTicks = saturatingAdd(m_UnwrappedRtpTicks,
                                         static_cast<uint64_t>(rtpDelta));
    const CadenceSample current {
        m_SourceFrameOrdinal,
        m_UnwrappedRtpTicks,
    };

    const bool majorDeparture = isMajorCadenceDeparture(
        observation.intervalUs, observation.frameDelta);

    if (!m_RateCandidateSamples.empty()) {
        // A normal interval immediately after a major departure identifies an
        // isolated source/capture gap. Preserve the stable rate, discard the
        // provisional segment, and begin a fresh cumulative phase at the
        // current sample.
        const uint64_t observedPeriodUs = std::max<uint64_t>(
            1, observation.intervalUs / observation.frameDelta);
        const bool returnedToStableCadence =
            observedPeriodUs *
                    m_Parameters.candidateCadenceRatioDenominator <=
                m_SourcePeriodUs *
                    m_Parameters.candidateCadenceRatioNumerator &&
            m_SourcePeriodUs *
                    m_Parameters.candidateCadenceRatioDenominator <=
                observedPeriodUs *
                    m_Parameters.candidateCadenceRatioNumerator;
        if (returnedToStableCadence) {
            m_RateCandidateSamples.clear();
            m_CadenceSamples.clear();
            m_CadenceSamples.push_back(current);
            observation.phaseDiscontinuity = true;
            return;
        }

        appendCadenceSample(m_RateCandidateSamples, current);
        observation.phaseDiscontinuity = true;
        const uint64_t candidateSpanUs =
            (m_RateCandidateSamples.back().rtpTicks -
             m_RateCandidateSamples.front().rtpTicks) *
            kMicrosecondsPerSecond / kRtpClockRate;
        if (m_RateCandidateSamples.size() >=
                m_Parameters.rateCandidateSamples &&
            candidateSpanUs >= m_Parameters.rateCandidateMinimumUs) {
            const uint64_t candidatePeriodQ16 = fittedSourcePeriodQ16(
                m_RateCandidateSamples);
            if (candidatePeriodQ16 != 0) {
                observation.sourceRateChanged = acceptSourcePeriodQ16(
                    candidatePeriodQ16);
                m_CadenceSamples = m_RateCandidateSamples;
                m_RateCandidateSamples.clear();
                observation.eligible = true;
            }
        }
        return;
    }

    if (majorDeparture) {
        m_RateCandidateSamples.push_back(previous);
        m_RateCandidateSamples.push_back(current);
        observation.phaseDiscontinuity = true;
        return;
    }

    appendCadenceSample(m_CadenceSamples, current);
    observation.eligible = true;
    if (m_CadenceSamples.size() >= m_Parameters.minimumCadenceSamples) {
        const uint64_t fittedPeriodQ16 = fittedSourcePeriodQ16(
            m_CadenceSamples);
        if (fittedPeriodQ16 != 0 &&
            acceptSourcePeriodQ16(fittedPeriodQ16)) {
            observation.sourceRateChanged = true;
            observation.phaseDiscontinuity = true;
        }
    }
}

void VrrTimingController::appendCadenceSample(
    std::deque<CadenceSample>& samples, const CadenceSample& sample)
{
    samples.push_back(sample);
    while (samples.size() > m_Parameters.maximumCadenceSamples) {
        samples.pop_front();
    }

    while (samples.size() > m_Parameters.minimumCadenceSamples) {
        const uint64_t spanTicks = samples.back().rtpTicks -
            samples.front().rtpTicks;
        const uint64_t spanUs = spanTicks * kMicrosecondsPerSecond /
            kRtpClockRate;
        if (spanUs <= cadenceWindowUs()) {
            break;
        }
        samples.pop_front();
    }
}

uint64_t VrrTimingController::fittedSourcePeriodQ16(
    const std::deque<CadenceSample>& samples) const
{
    if (samples.size() < 2) {
        return 0;
    }

    const CadenceSample& first = samples.front();
    const CadenceSample& last = samples.back();
    if (last.frameOrdinal <= first.frameOrdinal ||
        last.rtpTicks <= first.rtpTicks) {
        return 0;
    }

    // RTP timestamps from a host-refresh-quantized source form a staircase.
    // OLS weighs the placement of each staircase step, so its slope breathes
    // as a long atom crosses the window.  The endpoint span measures only
    // the net source movement and still accounts for skipped local frames.
    const uint64_t spanFrames = last.frameOrdinal - first.frameOrdinal;
    const uint64_t spanTicks = last.rtpTicks - first.rtpTicks;
    constexpr uint64_t kPeriodQ16Scale =
        kMicrosecondsPerSecond * kQ16One;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();

    // The cadence window is at most one previous window plus one valid
    // one-second RTP interval, so these products are comfortably 64-bit in
    // normal operation. Keep explicit guards for malformed frame numbers.
    if (spanTicks > maximum / kPeriodQ16Scale ||
        spanFrames > maximum / kRtpClockRate) {
        return 0;
    }

    const uint64_t numerator = spanTicks * kPeriodQ16Scale;
    const uint64_t denominator = spanFrames * kRtpClockRate;
    const uint64_t quotient = numerator / denominator;
    const uint64_t remainder = numerator % denominator;
    const uint64_t halfway = denominator / 2 + denominator % 2;
    if (remainder >= halfway && quotient < maximum) {
        return quotient + 1;
    }
    return quotient;
}

uint64_t VrrTimingController::cadenceWindowUs() const
{
    const uint64_t displayFloorUs = saturatingAdd(m_DisplayPeriodUs,
                                                   m_GuardUs);
    const uint64_t headroomUs = m_SourcePeriodUs > displayFloorUs ?
        m_SourcePeriodUs - displayFloorUs : 0;
    const uint64_t looseHeadroomUs =
        m_Parameters.looseHeadroomDisplayPeriods >
                std::numeric_limits<uint64_t>::max() / m_DisplayPeriodUs ?
            std::numeric_limits<uint64_t>::max() :
            m_DisplayPeriodUs *
                m_Parameters.looseHeadroomDisplayPeriods;
    if (headroomUs >= looseHeadroomUs) {
        return m_Parameters.looseCadenceWindowUs;
    }
    if (headroomUs <= m_DisplayPeriodUs) {
        return m_Parameters.tightCadenceWindowUs;
    }

    const uint64_t tightnessNumerator = looseHeadroomUs - headroomUs;
    const uint64_t windowRangeUs = m_Parameters.tightCadenceWindowUs -
        m_Parameters.looseCadenceWindowUs;
    return m_Parameters.looseCadenceWindowUs +
        windowRangeUs * tightnessNumerator /
            std::max<uint64_t>(1, looseHeadroomUs - m_DisplayPeriodUs);
}

bool VrrTimingController::isMajorCadenceDeparture(
    uint64_t intervalUs, uint64_t frameDelta) const
{
    if (intervalUs == 0 || frameDelta == 0 || m_SourcePeriodUs == 0) {
        return false;
    }
    const uint64_t observedPeriodUs = std::max<uint64_t>(
        1, intervalUs / frameDelta);
    return observedPeriodUs * m_Parameters.majorCadenceRatioDenominator >
               m_SourcePeriodUs *
                   m_Parameters.majorCadenceRatioNumerator ||
        m_SourcePeriodUs * m_Parameters.majorCadenceRatioDenominator >
               observedPeriodUs *
                   m_Parameters.majorCadenceRatioNumerator;
}

bool VrrTimingController::acceptSourcePeriodQ16(uint64_t periodUsQ16)
{
    if (periodUsQ16 == 0) {
        return false;
    }

    // Filter the raw fit for the metronome before the floor below is
    // applied; a material change re-seeds it so the tick follows a real
    // rate change within a frame rather than a filter window.
    const uint64_t windowFrames = std::max<uint64_t>(
        1, m_Parameters.playoutMetronomePeriodWindowFrames);
    if (m_MetronomePeriodUsQ16 == 0) {
        m_MetronomePeriodUsQ16 = periodUsQ16;
    }
    else if (periodUsQ16 >= m_MetronomePeriodUsQ16) {
        m_MetronomePeriodUsQ16 +=
            (periodUsQ16 - m_MetronomePeriodUsQ16) / windowFrames;
    }
    else {
        m_MetronomePeriodUsQ16 -=
            (m_MetronomePeriodUsQ16 - periodUsQ16) / windowFrames;
    }

    // The negotiated stream rate is an upper bound on source FPS. Preserve
    // it at Q16 precision so fractional rates do not acquire an artificial
    // drift from the rounded microsecond period.
    periodUsQ16 = std::max(periodUsQ16, m_ConfiguredStreamPeriodQ16);
    const uint64_t previousPeriodUs = m_SourcePeriodUs;
    m_SourcePeriodUsQ16 = periodUsQ16;
    m_SourcePeriodUs = std::max<uint64_t>(1, roundedQ16(periodUsQ16));
    m_RenderBaselineUs = std::min(m_RenderBaselineUs,
                                  m_SourcePeriodUs);
    m_RenderLeadUs = clampUnsigned(m_RenderLeadUs,
                                   renderLeadFloorUs(),
                                   renderLeadCeilingUs());
    m_GpuReadinessLeadUs = std::min(m_GpuReadinessLeadUs,
                                    gpuReadinessCeilingUs());
    m_GuardUs = clampUnsigned(m_GuardUs,
                              m_BaseGuardUs,
                              guardCeilingUs());
    clampReadinessReserveToPolicy();
    const bool materialChange = !withinPercent(
        m_SourcePeriodUs, previousPeriodUs,
        m_Parameters.materialRateChangePercent);
    if (materialChange) {
        m_MetronomePeriodUsQ16 = m_SourcePeriodUsQ16;
    }
    return materialChange;
}

void VrrTimingController::anchorSourceTime(uint64_t sourceTimeUs)
{
    m_SourceTimeUs = sourceTimeUs;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    m_SourceTimeUsQ16 = sourceTimeUs > maximum / kQ16One ?
        maximum : sourceTimeUs * kQ16One;
}

void VrrTimingController::notePreparationDuration(
    uint64_t preparationDurationUs, uint64_t acquisitionWaitUs,
    uint64_t preparationCompleteUs, uint64_t gpuReadyWaitUs)
{
    // Swapchain availability is not work that a larger jitter buffer fixes.
    // The worker already excludes its intentional waits from this duration.
    if (m_Parameters.playoutHistoryEnabled != 0) {
        preparationDurationUs -= std::min(preparationDurationUs, acquisitionWaitUs);
    }
    // D3D preparation includes the renderer's wait for the source-readiness
    // fence. Once that wait has its own bounded head start, do not charge it
    // a second time as generic render work. Legacy/replay policies leave this
    // disabled, preserving their exact learned render lead.
    if (m_Parameters.playoutGpuReadinessAdaptation != 0) {
        preparationDurationUs -= std::min(preparationDurationUs,
                                           gpuReadyWaitUs);
    }
    if (!m_Pending.valid) {
        return;
    }
    m_Pending.hasPreparationDuration = true;
    m_Pending.preparationDurationUs = preparationDurationUs;
    m_Pending.preparationCompleteUs = preparationCompleteUs;
}

void VrrTimingController::noteGpuReadyWait(uint64_t waitUs, bool completed,
                                           uint64_t completionUs)
{
    if (m_Parameters.playoutGpuReadinessAdaptation == 0 || !completed) {
        return;
    }

    const uint64_t at = completionUs != 0 ? completionUs :
        m_LastDecodeCompleteUs;
    if (at == 0) {
        return;
    }

    constexpr size_t kMaximumSamples = 512;
    const uint64_t windowUs = std::max<uint64_t>(
        1, m_Parameters.playoutGpuReadinessWindowUs);
    while (m_GpuReadinessSamples.size() > 1 &&
           (m_GpuReadinessSamples.size() > kMaximumSamples ||
            (at > windowUs &&
             m_GpuReadinessSamples.front().completionUs < at - windowUs))) {
        m_GpuReadinessSamples.pop_front();
    }
    m_GpuReadinessSamples.push_back({at, waitUs});
    while (m_GpuReadinessSamples.size() > kMaximumSamples) {
        m_GpuReadinessSamples.pop_front();
    }

    std::deque<uint64_t> waits;
    waits.resize(0);
    for (const GpuReadinessSample& sample : m_GpuReadinessSamples) {
        waits.push_back(sample.waitUs);
    }
    const uint64_t desired = clampUnsigned(
        saturatingAdd(percentile(waits, m_Parameters.playoutGpuReadinessPercentile),
                      m_Parameters.playoutGpuReadinessMarginUs),
        0, gpuReadinessCeilingUs());
    if (desired > m_GpuReadinessLeadUs) {
        m_GpuReadinessLeadUs = std::min(
            desired,
            saturatingAdd(m_GpuReadinessLeadUs,
                          std::max<uint64_t>(1,
                              m_Parameters.playoutGpuReadinessAttackUs)));
    }
    else if (desired < m_GpuReadinessLeadUs) {
        const uint64_t elapsed = m_LastGpuReadinessUpdateUs != 0 &&
                at >= m_LastGpuReadinessUpdateUs ?
            std::min<uint64_t>(at - m_LastGpuReadinessUpdateUs, 33333) :
            std::max<uint64_t>(1, m_SourcePeriodUs);
        const uint64_t rate = m_Parameters.playoutGpuReadinessReleaseUsPerSecond;
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        const uint64_t releaseNumerator = rate != 0 &&
                elapsed > maximum / rate ? maximum : rate * elapsed;
        const uint64_t releaseUs = rate == 0 ? 0 :
            std::max<uint64_t>(1, releaseNumerator / kMicrosecondsPerSecond);
        m_GpuReadinessLeadUs -= std::min(
            m_GpuReadinessLeadUs - desired, releaseUs);
    }
    m_GpuReadinessLeadUs = std::min(m_GpuReadinessLeadUs,
                                    gpuReadinessCeilingUs());
    m_LastGpuReadinessUpdateUs = at;
}

void VrrTimingController::noteSchedulerDelays(uint64_t renderDelayUs,
                                              uint64_t targetDelayUs,
                                              bool targetDelayValid)
{
    m_Pending.renderSchedulerUs = renderDelayUs;
    appendBounded(m_RenderSchedulerDelays, renderDelayUs,
                  m_Parameters.schedulerLearningSamples);
    if (targetDelayValid) {
        appendBounded(m_TargetSchedulerDelays, targetDelayUs,
                      m_Parameters.schedulerLearningSamples);
    }
    updateLearnedBudgets();
}

void VrrTimingController::noteSpacingDeficit(uint64_t deficitUs)
{
    if (deficitUs != 0) {
        m_CleanSpacingFrames = 0;
        const uint64_t increaseUs = std::max(m_Parameters.guardStepUs,
                                             deficitUs);
        m_GuardUs = std::min(guardCeilingUs(),
                             saturatingAdd(m_GuardUs, increaseUs));
        return;
    }

    if (m_GuardUs > m_BaseGuardUs &&
        ++m_CleanSpacingFrames >= m_Parameters.guardDecayFrames) {
        m_GuardUs -= std::min(m_Parameters.guardStepUs,
                              m_GuardUs - m_BaseGuardUs);
        m_CleanSpacingFrames = 0;
    }
}

void VrrTimingController::noteSubmission(bool submitted, bool cancelled,
                                         uint64_t submissionUs)
{
    if (!m_Pending.valid) {
        return;
    }

    if (m_Parameters.playoutResponsiveBuffer >= 5) {
        const auto& p = m_Pending.prediction;
        const auto work = saturatingAdd(m_Pending.preparationDurationUs, m_Pending.renderSchedulerUs);
        const auto ready = m_Pending.preparationCompleteUs ? m_Pending.preparationCompleteUs : saturatingAdd(p.decoded, work);
        const auto deadline = m_Pending.smoothness.intended;
        if (m_Parameters.playoutResponsiveBuffer >= 6) {
            m_IntervalBuffer.observe({m_Pending.smoothness.frame, m_Pending.intervalIntendedUs,
                submissionUs, deadline, ready, p.applied,
                submitted && !cancelled && m_Pending.intervalValid && m_Pending.hasPreparationDuration,
                p.eligible && work <= p.period && p.decoderQueue <= p.period},
                playoutDelayMinimumUs(), playoutDelayMaximumUs(),
                m_Parameters.playoutMeanMissHoldUs, m_Parameters.playoutMeanMissReleaseUsPerSecond,
                m_Parameters.playoutResponsiveBuffer >= 7, m_Parameters.playoutOnTimeTargetPerMillion,
                intervalQualityToleranceUs(m_Parameters),
                intervalQualityWindowUs(m_Parameters));
        }
        else m_MeanMissBuffer.observe(submissionUs, ready > deadline ? ready - deadline : 0,
            p.applied, submitted && !cancelled && m_Pending.hasPreparationDuration &&
                m_Pending.smoothness.eligible && p.eligible && work <= p.period && p.decoderQueue <= p.period,
            playoutDelayMinimumUs(), playoutDelayMaximumUs(),
            m_Parameters.playoutMeanMissHoldUs, m_Parameters.playoutMeanMissReleaseUsPerSecond);
    }

    if (m_Parameters.playoutReadinessHitchThresholdUs) {
        const auto& p = m_Pending.prediction;
        const auto work = saturatingAdd(m_Pending.preparationDurationUs,
                                       m_Pending.renderSchedulerUs);
        // Reconstruct readiness without our intentional render/target waits,
        // swapchain acquisition or post-submit blocking. Overload/backpressure
        // and discontinuities cannot be cured by storing more frames.
        const auto result = m_ReadinessFeedback.observe({
            m_Pending.smoothness.frame, m_Pending.smoothness.intended,
            submissionUs, saturatingAdd(p.decoded, work), p.applied,
            submitted && !cancelled && m_Pending.hasPreparationDuration &&
                m_Pending.smoothness.eligible && p.eligible &&
                work <= p.period && p.decoderQueue <= p.period},
            m_Parameters.playoutReadinessHitchThresholdUs);
        if (result.interval) {
            const auto ns = [](uint64_t us) {
                return int64_t(std::min<uint64_t>(us, INT64_MAX / 1000)) * 1000;
            };
            m_PlayoutHistory.observe(ns(result.demand), ns(p.applied), ns(submissionUs));
            if (result.demand) {
                m_UnclampedRequestedPlayoutDelayUs = std::max(
                    m_UnclampedRequestedPlayoutDelayUs, result.demand);
                m_RequestedPlayoutDelayUs = std::max(m_RequestedPlayoutDelayUs,
                    std::min(result.demand, playoutDelayMaximumUs()));
            }
        }
    }

    if (m_Parameters.playoutSmoothnessFeedbackEnabled) {
        if (submitted && !cancelled) {
            auto sample = m_Pending.smoothness;
            sample.at = submissionUs;
            const auto before = m_SubmissionSmoothness.observedIntervals();
            const uint64_t demand = m_SubmissionSmoothness.observe(sample, submissionUs,
                m_Parameters.playoutSubmissionEstimateFallback != 0);
            if (m_Parameters.playoutSubmissionEstimateFallback &&
                    m_SubmissionSmoothness.observedIntervals() > before) {
                ++m_EstimatedCadenceIntervals;
                m_EstimatedCadenceHitches += demand != 0;
                // A submission timestamp is a lower-confidence presentation
                // proxy. Never mix it into the verified display counters or
                // teach native service latency from it. It may guide padding
                // only while verified native feedback is unavailable.
                if (demand && !m_Parameters.playoutPredictionOnly &&
                        !hasRecentNativeFeedback(submissionUs)) {
                    m_UnclampedRequestedPlayoutDelayUs = std::max(
                        m_UnclampedRequestedPlayoutDelayUs, demand);
                    m_RequestedPlayoutDelayUs = std::max(m_RequestedPlayoutDelayUs, demand);
                }
            }
        }
        else m_SubmissionSmoothness.breakSequence();
    }
    if (submitted) {
        // Cancellation is a reason, not proof that nothing reached the native
        // presentation queue (Vulkan must submit some abandoned images).
        m_HaveLastSubmission = true;
        m_LastSubmissionUs = submissionUs;

        if (!cancelled) {
            if (m_Pending.hasSmoothedBasis) {
                m_LastSmoothedBasisUsQ16 = m_Pending.smoothedBasisUsQ16;
                m_LastSmoothedBasisUs = roundedQ16(m_Pending.smoothedBasisUsQ16);
                m_LastSmoothedBasisOrdinal = m_Pending.smoothedBasisOrdinal;
                m_PhaseDebtUs = m_Pending.phaseDebtUs;
                m_PhaseResidualEmaUs = m_Pending.phaseResidualEmaUs;
                m_LastBasisMappingUs = m_Pending.basisMappingUs;
                m_HaveSmoothedBasis = true;
            }
            if (m_Pending.cadenceEligible) {
                appendBounded(m_ReadyOffsets, m_Pending.readyOffsetUs,
                              readinessLearningSampleLimit());
            }
            if (m_Pending.hasPreparationDuration) {
                if (m_Parameters.playoutPredictionEnabled &&
                    !m_Parameters.playoutReadinessHitchThresholdUs) {
                    m_ReadinessPrediction.observe(m_PlayoutHistory, m_Pending.prediction,
                        m_Pending.preparationDurationUs, m_Pending.renderSchedulerUs,
                        m_Parameters.playoutResponsiveBuffer && m_Parameters.playoutResponsiveBuffer < 5 ? &m_RecentReadiness : nullptr);
                }
                appendBounded(m_PreparationDurations,
                              m_Pending.preparationDurationUs,
                              m_Parameters.preparationLearningSamples);
            }
            updateReadinessModel();
            updateLearnedBudgets();
        }
    }

    m_Pending = PendingFrame {};
}

Vrr13::SmoothnessFeedback::Sample VrrTimingController::smoothnessSample(const VrrTimingDecision& d) const
{
    Vrr13::SmoothnessFeedback::Sample sample;
    sample.frame = d.frameNumber;
    // Judge cadence against the intended source schedule. A newly learned
    // compositor lead changes our prediction, not the spacing we want to
    // display. Including its frame-to-frame changes manufactures buffer
    // demand even when both frames were prepared and presented on time.
    // Keep the old reference available for exact replay of existing captures.
    sample.intended = m_Parameters.playoutStableSmoothnessReference ?
        d.originalTargetUs : d.originalScanoutUs;
    if (m_Parameters.playoutNativeHitchAdaptation || m_Parameters.playoutPredictionOnly) {
        // Score client-added spacing against the game's source cadence. Changes
        // in our padding or render estimate must not redefine a smooth result.
        sample.intended = d.sourceTimeUs;
    }
    sample.buffer = d.playoutDelayUs;
    sample.headroom = m_Parameters.playoutNativeHitchAdaptation ? 0 : d.recoveryHeadroomUs;
    sample.eligible = d.cadenceEligible && !d.rebased && !d.phaseDiscontinuity &&
        !d.sourceRateChanged && d.sourceIntervalUs <= std::max<uint64_t>(25000, d.sourcePeriodUs * 3 / 2);
    return sample;
}

bool VrrTimingController::hasRecentNativeFeedback(uint64_t now) const
{
    return m_NativeSmoothness.samples() &&
        std::abs(signedDifference(now, m_NativeSmoothness.lastObservedUs())) <= 100000;
}

const Vrr13::SmoothnessFeedback& VrrTimingController::activeSmoothnessFeedback(uint64_t now) const
{
    return m_Parameters.playoutSubmissionEstimateFallback && !hasRecentNativeFeedback(now) ?
        m_SubmissionSmoothness : m_NativeSmoothness;
}

void VrrTimingController::notePresentation(const Vrr13::PresentationObservation& observation)
{
    // Prediction-only production retains native cadence as diagnostic evidence.
    // schedule() ignores this model's lead/floor and updatePlayoutHistory()
    // uses readiness alone; missing or delayed feedback cannot change timing.
    if (!m_Parameters.playoutPredictionEnabled) return;
    if (!m_Parameters.playoutSmoothnessFeedbackEnabled) {
        m_PresentationPrediction.observe(observation,
            m_Parameters.playoutRequireDisplayEvents != 0);
        return;
    }
    if (observation.submitted) {
        if (m_FeedbackModeValid && m_FeedbackLatched != observation.latched)
            m_NativeSmoothness.breakSequence();
        m_FeedbackModeValid = true;
        m_FeedbackLatched = observation.latched;
    }
    m_PresentationPrediction.observe(observation, [this, &observation](const Vrr13::SmoothnessFeedback::Sample& sample, uint64_t observed) {
        const auto intervalsBefore = m_NativeSmoothness.observedIntervals();
        const uint64_t demand = m_NativeSmoothness.observe(sample, observed,
            observation.timeKind == Vrr13::PresentationTimeKind::DisplayEvent ||
            m_Parameters.playoutNativeHitchAdaptation != 0);
        if (observation.timeKind == Vrr13::PresentationTimeKind::DisplayEvent &&
                m_NativeSmoothness.observedIntervals() > intervalsBefore) {
            ++m_NativeCadenceIntervals;
            m_NativeCadenceHitches += demand != 0;
        }
        if (m_Parameters.playoutNativeHitchAdaptation && demand != 0) {
            // Only a new, matched native interval miss authorizes growth.
            // Demand belongs to the delayed frame's original padding, so a
            // catch-up sample cannot repeatedly charge today's larger buffer.
            m_UnclampedRequestedPlayoutDelayUs = std::max(
                m_UnclampedRequestedPlayoutDelayUs, demand);
            m_RequestedPlayoutDelayUs = std::max(m_RequestedPlayoutDelayUs, demand);
        }
    }, m_Parameters.playoutRequireDisplayEvents != 0);
}

void VrrTimingController::updateLearnedBudgets()
{
    if (!m_PreparationDurations.empty()) {
        m_RenderBaselineUs = std::min(
            percentile(m_PreparationDurations,
                       m_Parameters.renderBaselinePercentile),
            m_SourcePeriodUs);
        m_RenderLeadUs = clampUnsigned(
            saturatingAdd(percentile(
                              m_PreparationDurations,
                              m_Parameters.preparationPercentile),
                          m_Parameters.renderLeadSlackUs),
            renderLeadFloorUs(), renderLeadCeilingUs());
        // The p99 tail and readiness reserve share one hard policy budget.
        // Enforce a newly larger render tail immediately without advancing
        // the gradual readiness acquisition a second time.
        clampReadinessReserveToPolicy();
    }

    if (!m_RenderSchedulerDelays.empty()) {
        m_RenderWakeLeadUs = std::min(
            m_Parameters.maximumRenderWakeLeadUs,
            percentile(m_RenderSchedulerDelays,
                       m_Parameters.schedulerPercentile));
    }

    if (!m_TargetSchedulerDelays.empty()) {
        m_TargetWakeLeadUs = std::min(
            m_Parameters.maximumTargetWakeLeadUs,
            percentile(m_TargetSchedulerDelays,
                       m_Parameters.schedulerPercentile));
    }
}

void VrrTimingController::updateReadinessModel()
{
    if (m_ReadyOffsets.size() < m_Parameters.minimumReadinessSamples) {
        return;
    }

    // Learn exogenous decode-arrival variation, not absolute source phase or
    // queue age created by this controller. The selected low percentile is
    // the local phase baseline. Smoothness mode uses a VRR8-style p10/p95
    // spread so the reserve remains visible to the latency policy instead of
    // hiding a high-percentile phase shift outside the budget.
    // Near the display ceiling, preserve the p90 arrival tail because there is
    // too little cadence slack to absorb a late frame. Slower sources can use
    // p80 and avoid making the slowest fifth standing latency for every frame.
    const int64_t lowUs = percentile(
        m_ReadyOffsets, m_Parameters.readinessLowPercentile);
    const unsigned int highPercentile = headroomUs() > m_DisplayPeriodUs ?
        m_Parameters.readinessLoosePercentile :
        m_Parameters.readinessTightPercentile;
    const int64_t highUs = percentile(m_ReadyOffsets, highPercentile);
    const uint64_t spreadUs = highUs > lowUs ?
        static_cast<uint64_t>(highUs - lowUs) : 0;
    const uint64_t candidateDemandUs = clampUnsigned(
        saturatingAdd(spreadUs, m_Parameters.arrivalSpreadGuardUs),
        m_Parameters.minimumReadinessReserveUs, readinessCeilingUs());

    if (!m_ReadinessModelValid) {
        m_ReadinessDemandUs = candidateDemandUs;
        m_ReadinessModelValid = true;
    }
    else if (candidateDemandUs > m_ReadinessDemandUs) {
        // Attack faster than release, but never let one observation window
        // impose its entire tail on subsequent frames.
        const uint64_t difference = candidateDemandUs - m_ReadinessDemandUs;
        m_ReadinessDemandUs += std::max<uint64_t>(
            1, difference * m_Parameters.readinessAttackNumerator /
                m_Parameters.readinessAttackDenominator);
    }
    else if (candidateDemandUs < m_ReadinessDemandUs) {
        const uint64_t difference = m_ReadinessDemandUs - candidateDemandUs;
        m_ReadinessDemandUs -= std::max<uint64_t>(
            1, difference * m_Parameters.readinessReleaseNumerator /
                m_Parameters.readinessReleaseDenominator);
    }

    m_ReadinessPhaseUs = lowUs;
    applyReadinessBudget(true);
}

void VrrTimingController::applyReadinessBudget(bool acquireReserve,
                                               bool immediateAcquisition)
{
    // Cadence slack can absorb most arrival variation without committing a
    // decoded frame early. Preserve one quarter as service margin, matching
    // the field-tested VRR8 reserve rule, and retain a small floor near the
    // panel ceiling where Mailbox still needs a standing cadence cushion.
    // In this projected-source-clock design, small cadence slack cannot
    // substitute for readiness reserve: doing so lets quantized late arrivals
    // clamp the target to "now" and turns their 8/16 ms atoms into visible
    // presentation bursts. Credit slack only when at least a full additional
    // display period is available; tight and near-ceiling cadences retain the
    // complete learned cushion.
    const uint64_t cadenceHeadroomUs = headroomUs();
    const uint64_t usableHeadroomUs =
        m_SourcePeriodUs >= m_DisplayPeriodUs *
                m_Parameters.looseHeadroomDisplayPeriods ?
            cadenceHeadroomUs * m_Parameters.usableHeadroomNumerator /
                m_Parameters.usableHeadroomDenominator : 0;
    const uint64_t learnedDemandUs = m_ReadinessModelValid ?
        m_ReadinessDemandUs : m_Parameters.coldStartReadinessDemandUs;
    const uint64_t effectiveDemandUs = std::max(
        learnedDemandUs, readinessPeriodFloorUs());
    m_AppliedReadinessReserveUs = std::min(
        readinessReserveCeilingUs(),
        std::max(minimumReadinessReserveUs(),
                 effectiveDemandUs > usableHeadroomUs ?
                     effectiveDemandUs - usableHeadroomUs : 0));

    const int64_t ceilingUs = static_cast<int64_t>(readinessCeilingUs());
    const int64_t reserveUs = static_cast<int64_t>(
        std::min<uint64_t>(m_AppliedReadinessReserveUs,
                           static_cast<uint64_t>(ceilingUs)));
    const int64_t desiredUs = m_ReadinessPhaseUs >
            std::numeric_limits<int64_t>::max() - reserveUs ?
        std::numeric_limits<int64_t>::max() :
        m_ReadinessPhaseUs + reserveUs;
    const int64_t clampedDesiredUs = std::max(
        -ceilingUs, std::min(desiredUs, ceilingUs));
    if (!acquireReserve) {
        m_ReadinessBudgetUs = std::max(
            -ceilingUs, std::min(m_ReadinessPhaseUs, ceilingUs));
    }
    else if (immediateAcquisition) {
        // A phase reset changes the source-clock origin, not the amount of
        // arrival variation already learned for this device. Reapply the
        // bounded reserve in the new epoch so cadence transitions cannot
        // collapse smoothness back to zero for another learning window.
        m_ReadinessBudgetUs = clampedDesiredUs;
    }
    else if (clampedDesiredUs > m_ReadinessBudgetUs) {
        m_ReadinessBudgetUs += std::min<int64_t>(
            clampedDesiredUs - m_ReadinessBudgetUs,
            static_cast<int64_t>(m_Parameters.readinessAcquireStepUs));
    }
    else if (clampedDesiredUs < m_ReadinessBudgetUs) {
        m_ReadinessBudgetUs -= std::min<int64_t>(
            m_ReadinessBudgetUs - clampedDesiredUs,
            static_cast<int64_t>(m_Parameters.readinessAcquireStepUs));
    }

    clampReadinessReserveToPolicy();
}

void VrrTimingController::clampReadinessReserveToPolicy()
{
    // Demand release remains gradual, but a larger render tail must not leave
    // the combined pacing reserve above the half-scanout policy ceiling.
    const int64_t ceilingUs = static_cast<int64_t>(readinessCeilingUs());
    m_AppliedReadinessReserveUs = std::min(
        m_AppliedReadinessReserveUs,
        std::min(readinessReserveCeilingUs(),
                 static_cast<uint64_t>(ceilingUs)));
    const int64_t reserveUs = static_cast<int64_t>(
        m_AppliedReadinessReserveUs);
    const int64_t policyCeilingUs = m_ReadinessPhaseUs >
            std::numeric_limits<int64_t>::max() - reserveUs ?
        std::numeric_limits<int64_t>::max() :
        m_ReadinessPhaseUs + reserveUs;
    m_ReadinessBudgetUs = std::min(
        m_ReadinessBudgetUs,
        std::max(-ceilingUs, std::min(policyCeilingUs, ceilingUs)));
}

uint64_t VrrTimingController::timingBudgetUs() const
{
    return saturatingAdd(
        m_TimestampPlayoutActive ? effectivePlayoutDelayUs() :
                                   m_Parameters.sourcePlayoutDelayUs,
        saturatingAdd(
            m_AppliedReadinessReserveUs,
            saturatingAdd(pacingLatencyPolicyEnabled() ?
                              renderInsuranceUs() : m_RenderLeadUs,
                          m_Parameters.presentationSafetyUs)));
}

int64_t VrrTimingController::readinessBudgetUs() const
{
    return m_ReadinessBudgetUs;
}

uint64_t VrrTimingController::headroomUs() const
{
    const uint64_t floorUs = saturatingAdd(m_DisplayPeriodUs, m_GuardUs);
    return m_SourcePeriodUs > floorUs ? m_SourcePeriodUs - floorUs : 0;
}

uint64_t VrrTimingController::typicalRenderUs() const
{
    return m_PreparationDurations.empty() ? m_RenderLeadUs :
        std::clamp<uint64_t>(percentile(m_PreparationDurations, 50), 100, 100000);
}

uint64_t VrrTimingController::recoveryHeadroomUs() const
{
    const uint64_t occupied = std::max(m_DisplayPeriodUs, typicalRenderUs());
    return m_SourcePeriodUs > occupied ? m_SourcePeriodUs - occupied : 0;
}

uint64_t VrrTimingController::scaledDisplayPeriodUs(
    uint64_t numerator, uint64_t denominator) const
{
    if (numerator == 0 || denominator == 0) {
        return 0;
    }
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    if (m_DisplayPeriodUs > maximum / numerator) {
        return maximum;
    }
    return m_DisplayPeriodUs * numerator / denominator;
}

bool VrrTimingController::rateProtectedPresentation() const
{
    const int cutoffHz = VrrRatePolicy::protectedRateForRefresh(m_Config.displayRefreshHz);
    // Compare at the controller's microsecond resolution so RTP quantization
    // around an exact integer cutoff does not toggle presentation modes.
    // The configured rate seeds this period until the source fit is available.
    return m_CanLatchPresentation && cutoffHz > 0 &&
        m_SourcePeriodUs <= periodForRate(cutoffHz, 0);
}

uint64_t VrrTimingController::latchedPresentationHeadroomUs() const
{
    return std::max(
        m_Parameters.latchedPresentationHeadroomUs,
        scaledDisplayPeriodUs(
            m_Parameters.latchedPresentationHeadroomPeriodNumerator,
            m_Parameters.latchedPresentationHeadroomPeriodDenominator));
}

uint64_t VrrTimingController::latchedPresentationExitHeadroomUs() const
{
    return std::max(
        m_Parameters.latchedPresentationExitHeadroomUs,
        scaledDisplayPeriodUs(
            m_Parameters.latchedPresentationExitHeadroomPeriodNumerator,
            m_Parameters.latchedPresentationExitHeadroomPeriodDenominator));
}

uint64_t VrrTimingController::sourcePeriodUs() const
{
    return m_SourcePeriodUs;
}

uint64_t VrrTimingController::displayPeriodUs() const
{
    return m_DisplayPeriodUs;
}

uint64_t VrrTimingController::guardUs() const
{
    return m_GuardUs;
}

uint64_t VrrTimingController::renderLeadUs() const
{
    return m_RenderLeadUs;
}

uint64_t VrrTimingController::gpuReadinessLeadUs() const
{
    return std::min(m_GpuReadinessLeadUs, gpuReadinessCeilingUs());
}

uint64_t VrrTimingController::targetWakeLeadUs() const
{
    return m_TargetWakeLeadUs;
}

uint64_t VrrTimingController::earliestSubmissionUs() const
{
    if (!m_HaveLastSubmission) {
        return 0;
    }
    if (m_Parameters.latchedFloorDisabled != 0 && m_LatchedPresentation) {
        // Latched presents omit the tearing flag, so the flip queue already
        // orders and spaces them. A software floor of one display period
        // plus a guard cannot sustain a source at the refresh rate and only
        // manufactured a backlog there.
        return 0;
    }
    // Persistent Vulkan Immediate cannot switch to a protected native present.
    // Enforce the same entry margin by waiting instead. Capable presenters
    // already selected protection for slots inside this margin; their adaptive
    // slots naturally satisfy it. Historical captures retain the old floor.
    const uint64_t safetyHeadroomUs = m_Parameters.playoutPerFrameLatch >= 2 ?
        latchedPresentationHeadroomUs() : 0;
    return saturatingAdd(
        m_LastSubmissionUs,
        saturatingAdd(saturatingAdd(m_DisplayPeriodUs, m_GuardUs),
                      safetyHeadroomUs));
}

uint64_t VrrTimingController::lastSubmissionUs() const
{
    return m_LastSubmissionUs;
}

bool VrrTimingController::hasLastSubmission() const
{
    return m_HaveLastSubmission;
}

int64_t VrrTimingController::playoutOffsetUs() const
{
    return m_AppliedPlayoutOffsetUs;
}

bool VrrTimingController::timestampPlayoutActive() const
{
    return m_TimestampPlayoutActive;
}

bool VrrTimingController::timestampPlayoutEnabled() const
{
    return m_Parameters.timestampPlayoutEnabled != 0;
}

void VrrTimingController::resetPlayoutOffsets()
{
    m_PlayoutOffsets.clear();
    m_PlayoutOffsetValid = false;
    m_AppliedPlayoutOffsetUs = 0;
    m_PlayoutSamplesSeen = 0;
    m_PlayoutOffsetClockValid = false;
    m_LastPlayoutOffsetObservationUs = 0;
    m_PlayoutOffsetSlewRemainder = 0;
    m_TimestampPlayoutActive = false;
}

int64_t VrrTimingController::observePlayoutOffset(uint64_t observationUs,
                                                int64_t offsetUs,
                                                bool cadenceEligible,
                                                bool phaseDiscontinuity)
{
    const bool timeBased = m_Parameters.playoutOffsetSlewUsPerSecond != 0;
    const bool clockReversed = timeBased && m_PlayoutOffsetClockValid &&
        observationUs < m_LastPlayoutOffsetObservationUs;
    const bool breakWindow = m_Parameters.playoutOffsetCadenceGate != 0 &&
        phaseDiscontinuity;
    if (m_PlayoutOffsetValid && (breakWindow || clockReversed)) {
        // A minimum from the previous source phase is not evidence that the
        // new phase can be presented earlier. Discard observations, not the
        // applied mapping or the interval buffer. End any remaining warmup:
        // adopting a new minimum immediately would introduce a phase jump.
        m_PlayoutOffsets.clear();
        m_PlayoutSamplesSeen = std::max<uint64_t>(
            m_PlayoutSamplesSeen, m_Parameters.playoutOffsetWarmupSamples);
        m_PlayoutOffsetSlewRemainder = 0;
    }

    uint64_t allowedSlewUs = m_Parameters.playoutOffsetSlewUs;
    if (timeBased) {
        allowedSlewUs = 0;
        if (m_PlayoutOffsetClockValid &&
            observationUs > m_LastPlayoutOffsetObservationUs) {
            // Clamp both factors before multiplying, even for direct callers
            // that bypass replay parameter validation. Keep fractional credit
            // so high FPS cannot round a small correction rate down to zero.
            const uint64_t elapsedUs = std::min<uint64_t>(
                observationUs - m_LastPlayoutOffsetObservationUs, 1000000);
            const uint64_t rate = std::min<uint64_t>(
                m_Parameters.playoutOffsetSlewUsPerSecond, 1000000);
            const uint64_t credit = elapsedUs * rate +
                m_PlayoutOffsetSlewRemainder;
            const uint64_t maximumStepUs = std::min<uint64_t>(
                m_Parameters.playoutOffsetMaximumStepUs, 1000000);
            allowedSlewUs = std::min(credit / 1000000, maximumStepUs);
            // A stall buys at most one bounded step, never future catch-up
            // debt. Unused whole microseconds are deliberately not banked.
            m_PlayoutOffsetSlewRemainder = allowedSlewUs < maximumStepUs ?
                credit % 1000000 : 0;
        }
        else {
            m_PlayoutOffsetSlewRemainder = 0;
        }
        m_LastPlayoutOffsetObservationUs = observationUs;
        m_PlayoutOffsetClockValid = true;
    }

    if (m_Parameters.playoutOffsetCadenceGate != 0 &&
        m_PlayoutOffsetValid && (!cadenceEligible || phaseDiscontinuity)) {
        // Seed a genuine epoch once, but never learn its floor from an
        // ineligible transition frame. Rejected samples cannot earn slew
        // credit while the source is stalled or the cadence is provisional.
        m_PlayoutOffsetSlewRemainder = 0;
        return m_AppliedPlayoutOffsetUs;
    }
    // Bound the window by both age and count so a pathological configuration
    // cannot grow the deque without limit.
    constexpr size_t kMaximumPlayoutOffsetSamples = 4096;
    m_PlayoutOffsets.push_back(PlayoutOffsetSample { observationUs, offsetUs });
    const uint64_t windowUs = m_Parameters.playoutOffsetWindowUs;
    while (m_PlayoutOffsets.size() > 1 &&
           (m_PlayoutOffsets.size() > kMaximumPlayoutOffsetSamples ||
            (observationUs > windowUs &&
             m_PlayoutOffsets.front().observationUs <
                 observationUs - windowUs))) {
        m_PlayoutOffsets.pop_front();
    }

    int64_t windowMinimumUs = std::numeric_limits<int64_t>::max();
    for (const PlayoutOffsetSample& sample : m_PlayoutOffsets) {
        windowMinimumUs = std::min(windowMinimumUs, sample.offsetUs);
    }

    ++m_PlayoutSamplesSeen;
    if (!m_PlayoutOffsetValid) {
        m_AppliedPlayoutOffsetUs = offsetUs;
        m_PlayoutOffsetValid = true;
    }
    else if (m_PlayoutSamplesSeen <= m_Parameters.playoutOffsetWarmupSamples) {
        // The first frame after an epoch is an arbitrary arrival. While the
        // window is still filling, adopt an earlier arrival immediately so
        // startup latency converges within a few frames instead of paying
        // the slew rate for the first several seconds.
        if (windowMinimumUs < m_AppliedPlayoutOffsetUs) {
            m_AppliedPlayoutOffsetUs = windowMinimumUs;
        }
    }
    else {
        // Steady state: track the earliest arrival in the window at a rate
        // far above clock drift but far below anything visible per frame.
        const int64_t slewUs = static_cast<int64_t>(std::min<uint64_t>(
            allowedSlewUs,
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
        if (windowMinimumUs > m_AppliedPlayoutOffsetUs) {
            m_AppliedPlayoutOffsetUs += std::min(
                slewUs, windowMinimumUs - m_AppliedPlayoutOffsetUs);
        }
        else if (windowMinimumUs < m_AppliedPlayoutOffsetUs) {
            m_AppliedPlayoutOffsetUs -= std::min(
                slewUs, m_AppliedPlayoutOffsetUs - windowMinimumUs);
        }
    }
    if (timeBased && (m_PlayoutSamplesSeen <= m_Parameters.playoutOffsetWarmupSamples ||
                      m_AppliedPlayoutOffsetUs == windowMinimumUs)) {
        m_PlayoutOffsetSlewRemainder = 0;
    }
    return m_AppliedPlayoutOffsetUs;
}

uint64_t VrrTimingController::playoutDelayUs() const
{
    const uint64_t delayUs = m_TimestampPlayoutActive ? effectivePlayoutDelayUs() :
                                                      m_Parameters.sourcePlayoutDelayUs;
    return std::min(delayUs, playoutDelayCapUs());
}

unsigned int VrrTimingController::playoutBandIndex() const
{
    return m_PlayoutBandValid ? m_PlayoutBandIndex : 0;
}

uint64_t VrrTimingController::playoutBandSamples() const
{
    if (m_Parameters.playoutHistoryEnabled != 0) return m_PlayoutHistory.evidence();
    if (!m_PlayoutBandValid) {
        return 0;
    }
    const auto band = m_PlayoutBands.find(m_PlayoutBandIndex);
    return band != m_PlayoutBands.end() ? band->second.samplesSeen : 0;
}

uint64_t VrrTimingController::effectivePlayoutDelayUs() const
{
    if (m_Parameters.playoutDelayAdaptive != 0) {
        // Before the first band opens, the delay the band will open with.
        const uint64_t delayUs = m_PlayoutBandValid ? m_AppliedPlayoutDelayUs :
                                                    playoutDelayStartUs();
        return std::min(delayUs, playoutDelayCapUs());
    }
    return m_Parameters.sourcePlayoutDelayUs;
}

uint64_t VrrTimingController::playoutDelayMinimumUs() const
{
    uint64_t minimumUs = std::min(m_Parameters.playoutDelayMinimumUs,
                                  playoutDelayCapUs());
    return m_Parameters.playoutHistoryEnabled != 0 ?
        std::min(minimumUs, playoutQueueLimitUs()) : minimumUs;
}

uint64_t VrrTimingController::scaledPerMille(uint64_t value,
                                             uint64_t perMille)
{
    const uint64_t whole = value / 1000;
    const uint64_t remainder = value % 1000;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    if (perMille != 0 && whole > maximum / perMille) {
        return maximum;
    }
    return whole * perMille + remainder * perMille / 1000;
}

uint64_t VrrTimingController::playoutDelayMaximumUs() const
{
    uint64_t maximumUs = std::max(m_Parameters.playoutDelayMaximumUs,
                                  m_Parameters.playoutDelayMinimumUs);
    if (m_Parameters.playoutDelayMaximumPeriodPerMille != 0) {
        maximumUs = std::max(
            maximumUs,
            scaledPerMille(m_SourcePeriodUs,
                           m_Parameters.playoutDelayMaximumPeriodPerMille));
    }
    maximumUs = std::min(maximumUs, playoutDelayCapUs());
    return m_Parameters.playoutHistoryEnabled != 0 ?
        std::min(maximumUs, playoutQueueLimitUs()) : maximumUs;
}

uint64_t VrrTimingController::latencyFixDelayLimitUs() const
{
    return scaledPerMille(m_DisplayPeriodUs, m_Parameters.latencyFixDelayPeriodPerMille);
}


uint64_t VrrTimingController::playoutDelayCapUs() const
{
    if (m_Parameters.playoutDelayCapSourcePeriodPerMille != 0) {
        const uint64_t capPeriodUs =
            m_Parameters.playoutDelayCapUsesObservedPeriod != 0 ?
                m_SourcePeriodUs :
                (m_Parameters.playoutResponsiveBuffer ?
                     m_ConfiguredStreamPeriodUs : m_SourcePeriodUs);
        return scaledPerMille(
            capPeriodUs,
            m_Parameters.playoutDelayCapSourcePeriodPerMille);
    }
    // Parameterized captures predating the source-frame preset cap retain the
    // old display-relative limiter for exact replay.
    return m_LatencyFixActive ? latencyFixDelayLimitUs() :
        std::numeric_limits<uint64_t>::max();
}

uint64_t VrrTimingController::gpuReadinessCeilingUs() const
{
    if (m_Parameters.playoutGpuReadinessAdaptation == 0) {
        return 0;
    }
    return std::min(
        std::max<uint64_t>(1, m_Parameters.playoutGpuReadinessMaximumUs),
        std::max<uint64_t>(1, m_SourcePeriodUs));
}

void VrrTimingController::updateLatencyFixState()
{
    const int enterHz = VrrRatePolicy::protectedRateForRefresh(m_Config.displayRefreshHz);
    // Reuse the near-ceiling boundary (116 at 120 Hz). Leave only once half
    // that headroom again is available (114 Hz), so small fitted-rate changes
    // cannot repeatedly compress and refill the buffer.
    const int exitHz = std::max(1, enterHz -
        std::max(1, (m_Config.displayRefreshHz - enterHz) / 2));
    const bool active = m_Parameters.latencyFixEnabled != 0 &&
        m_Config.displayRefreshHz > 0 && m_SourcePeriodUs > 0 &&
        (m_Parameters.latencyFixAllRates != 0 ||
         (enterHz > 0 && m_SourcePeriodUs <= periodForRate(m_LatencyFixActive ? exitHz : enterHz, 0)));
    if (m_LatencyFixActive && !active) {
        // Do not resurrect a near-ceiling hitch's rejected padding demand
        // when the source leaves this mode. Fresh lower-rate misses can still
        // acquire protection through the ordinary feedback path.
        m_RequestedPlayoutDelayUs = std::min(m_RequestedPlayoutDelayUs, latencyFixDelayLimitUs());
        m_UnclampedRequestedPlayoutDelayUs = m_RequestedPlayoutDelayUs;
    }
    m_LatencyFixActive = active;
}

uint64_t VrrTimingController::playoutQueueLimitUs() const
{
    // One frame is active, three can wait, and the next arrival needs a slot.
    // Use the faster of fitted and negotiated cadence during rate transitions.
    const uint64_t period = std::min(m_SourcePeriodUs, m_ConfiguredStreamPeriodUs);
    const uint64_t capacity = period * VrrMaximumQueuedFrames;
    const uint64_t work = saturatingAdd(m_RenderLeadUs, m_Parameters.presentationSafetyUs);
    const uint64_t smoothing = m_Parameters.playoutSmoothingGainPerMille != 0 ?
        m_Parameters.playoutSmoothingMaxLagUs : 0;
    const uint64_t occupied = saturatingAdd(work, smoothing);
    return capacity > occupied ? capacity - occupied : 0;
}

uint64_t VrrTimingController::playoutDelayStartUs() const
{
    uint64_t startUs = m_Parameters.playoutDelayStartUs != 0 ?
        m_Parameters.playoutDelayStartUs : m_Parameters.sourcePlayoutDelayUs;
    if (m_Parameters.playoutDelayStartPeriodPerMille != 0) {
        startUs = std::max(
            startUs,
            scaledPerMille(m_SourcePeriodUs,
                           m_Parameters.playoutDelayStartPeriodPerMille));
    }
    if (m_Parameters.playoutHistoryEnabled != 0) {
        // Only cold-start padding scales with display/work cost. Do not subtract
        // inter-frame headroom here: the learned target credits it exactly once.
        startUs = std::min(startUs, std::max(m_DisplayPeriodUs, m_RenderLeadUs));
    }
    return clampUnsigned(startUs, playoutDelayMinimumUs(),
                         playoutDelayMaximumUs());
}

void VrrTimingController::updatePlayoutDelay(
    const PacedFrame& frame, const CadenceObservation& cadence,
    bool rebased, int64_t readyOffsetUs, uint64_t nowUs)
{
    if (m_Parameters.playoutDelayAdaptive == 0) {
        m_PlayoutBandValid = false;
        return;
    }

    if (m_Parameters.playoutHistoryEnabled != 0) {
        updatePlayoutHistory(frame, cadence, rebased, readyOffsetUs);
        return;
    }

    // Band selection from the fitted source rate, with hysteresis so a rate
    // hovering at a band edge does not bounce between two reservoirs.
    const uint64_t rateHz = m_SourcePeriodUs != 0 ?
        (kMicrosecondsPerSecond + m_SourcePeriodUs / 2) / m_SourcePeriodUs : 0;
    const uint64_t widthHz = std::max<uint64_t>(
        1, m_Parameters.playoutBandWidthHz);
    const unsigned int candidate = static_cast<unsigned int>(
        std::min<uint64_t>(rateHz / widthHz,
                           std::numeric_limits<unsigned int>::max()));
    bool switched = false;
    if (!m_PlayoutBandValid) {
        m_PlayoutBandIndex = candidate;
        m_PlayoutBandValid = true;
        switched = true;
    }
    else if (candidate != m_PlayoutBandIndex) {
        const uint64_t lowEdgeHz = static_cast<uint64_t>(candidate) * widthHz;
        const uint64_t highEdgeHz = lowEdgeHz + widthHz - 1;
        const uint64_t hysteresisHz = std::max<uint64_t>(1, widthHz / 6);
        const bool confirmed = candidate > m_PlayoutBandIndex ?
            rateHz >= lowEdgeHz + hysteresisHz :
            rateHz + hysteresisHz <= highEdgeHz;
        if (confirmed) {
            m_PlayoutBandIndex = candidate;
            switched = true;
        }
    }

    PlayoutBand& band = m_PlayoutBands[m_PlayoutBandIndex];
    if (switched) {
        // A band not visited for a long time re-converges from the start
        // value instead of trusting a stale distribution.
        if (band.applied && m_Parameters.playoutBandStaleUs != 0 &&
                nowUs > band.lastUsedUs &&
                nowUs - band.lastUsedUs > m_Parameters.playoutBandStaleUs) {
            band = PlayoutBand {};
        }
        if (!band.applied) {
            band.appliedDelayUs = playoutDelayStartUs();
            band.applied = true;
        }
    }
    band.lastUsedUs = nowUs;

    // Admit this frame's lateness against the mapped sender clock. The
    // statistic is exogenous: the delay we choose never changes it, so there
    // is no feedback loop. Pairs spanning a host stall are excluded.
    const bool steadyArrival = m_HaveLastDecodeComplete &&
        frame.decodeCompleteUs() >= m_LastDecodeCompleteUs &&
        frame.decodeCompleteUs() - m_LastDecodeCompleteUs <=
            m_Parameters.playoutStallExclusionUs;
    // A sender interval well below the fitted period is a host burst: frames
    // captured back-to-back after a capture stall. They arrive spaced by
    // encode time, so their lateness against the mapping is an artifact of
    // the burst, not of the network or decoder, and it would only inflate the
    // delay for every normal frame.
    const uint64_t burstFloorUs = m_SourcePeriodUs / 1000 *
        std::min<uint64_t>(1000, m_Parameters.playoutBurstExclusionPerMille) +
        (m_SourcePeriodUs % 1000) *
        std::min<uint64_t>(1000, m_Parameters.playoutBurstExclusionPerMille) /
        1000;
    const bool steadySender = cadence.intervalUs != 0 &&
        cadence.intervalUs <= m_Parameters.playoutStallExclusionUs &&
        cadence.intervalUs >= burstFloorUs;
    // The frames that arrive bunched behind an arrival stall carry the
    // stall's backlog as lateness, not the link's jitter. Excluding only the
    // first of them let one hiccup pin the delay at its cap for the life of
    // the reservoir.
    bool burstExcluded = false;
    if (!steadyArrival) {
        if (m_Parameters.playoutStallBurstExclusion != 0 &&
                m_HaveLastDecodeComplete &&
                frame.decodeCompleteUs() >= m_LastDecodeCompleteUs) {
            const uint64_t gapUs = frame.decodeCompleteUs() -
                m_LastDecodeCompleteUs;
            m_BurstExclusionFrames = gapUs /
                std::max<uint64_t>(1, m_SourcePeriodUs);
        }
    }
    else if (m_BurstExclusionFrames != 0) {
        --m_BurstExclusionFrames;
        burstExcluded = true;
    }
    if (!rebased && !switched && cadence.eligible &&
            !cadence.phaseDiscontinuity && steadyArrival && steadySender &&
            !burstExcluded) {
        const uint64_t latenessUs = readyOffsetUs > 0 ?
            static_cast<uint64_t>(readyOffsetUs) : 0;
        const size_t capacity = std::max<size_t>(
            1, m_Parameters.playoutDelayReservoirSamples);
        if (band.latenessUs.size() < capacity) {
            band.latenessUs.push_back(latenessUs);
        }
        else {
            band.latenessUs[band.nextIndex % capacity] = latenessUs;
        }
        band.nextIndex = (band.nextIndex + 1) % capacity;
        ++band.samplesSeen;
    }

    // Desired delay: the configured lateness percentile plus a margin, once
    // the band has enough samples to estimate it.
    uint64_t desiredUs = band.appliedDelayUs;
    const size_t minimumSamples = std::max<size_t>(
        1, m_Parameters.playoutDelayMinimumSamples);
    if (band.latenessUs.size() >= minimumSamples) {
        std::vector<uint64_t> ordered(band.latenessUs.begin(),
                                      band.latenessUs.end());
        const uint64_t perMille = std::min<uint64_t>(
            1000, m_Parameters.playoutDelayPercentilePerMille);
        const size_t rank = std::max<size_t>(
            1, static_cast<size_t>(
                   (static_cast<uint64_t>(ordered.size()) * perMille + 999) /
                   1000));
        std::nth_element(ordered.begin(), ordered.begin() + (rank - 1),
                         ordered.end());
        // A frame later than the delay by less than the tolerance is a
        // sub-threshold stretch, not a hitch, so the delay only needs to
        // cover lateness beyond the tolerance.
        const uint64_t coveredUs = saturatingAdd(
            ordered[rank - 1], m_Parameters.playoutDelayMarginUs);
        desiredUs = clampUnsigned(
            coveredUs > m_Parameters.playoutDelayToleranceUs ?
                coveredUs - m_Parameters.playoutDelayToleranceUs : 0,
            playoutDelayMinimumUs(), playoutDelayMaximumUs());
    }

    // Attack quickly, release slowly, and never release below the start
    // value until the band has seen enough samples to trust its tail.
    if (desiredUs > band.appliedDelayUs) {
        band.appliedDelayUs += std::min(
            desiredUs - band.appliedDelayUs,
            m_Parameters.playoutDelayAttackUs);
    }
    else if (desiredUs < band.appliedDelayUs &&
             band.samplesSeen >= m_Parameters.playoutDelayReleaseSamples) {
        band.appliedDelayUs -= std::min(
            band.appliedDelayUs - desiredUs,
            m_Parameters.playoutDelayReleaseUs);
    }
    // A cap that shrank with the fitted period is approached at the release
    // rate rather than in one step, so the presented slot never jumps.
    const uint64_t maximumUs = playoutDelayMaximumUs();
    if (band.appliedDelayUs > maximumUs) {
        band.appliedDelayUs -= std::min(
            band.appliedDelayUs - maximumUs,
            std::max<uint64_t>(1, m_Parameters.playoutDelayReleaseUs));
    }
    band.appliedDelayUs = std::max(band.appliedDelayUs,
                                   playoutDelayMinimumUs());
    if (m_Parameters.playoutDelaySlewAcrossBands == 0 ||
            !m_AppliedPlayoutDelayValid) {
        m_AppliedPlayoutDelayUs = band.appliedDelayUs;
    }
    else if (band.appliedDelayUs > m_AppliedPlayoutDelayUs) {
        // The band's level is the goal; the delay in force walks to it at
        // the same slew it moves within a band, so a band change never
        // steps a frame off its slot.
        m_AppliedPlayoutDelayUs += std::min(
            band.appliedDelayUs - m_AppliedPlayoutDelayUs,
            std::max<uint64_t>(1, m_Parameters.playoutDelayAttackUs));
    }
    else if (band.appliedDelayUs < m_AppliedPlayoutDelayUs) {
        m_AppliedPlayoutDelayUs -= std::min(
            m_AppliedPlayoutDelayUs - band.appliedDelayUs,
            std::max<uint64_t>(1, m_Parameters.playoutDelayReleaseUs));
    }
    m_AppliedPlayoutDelayValid = true;
}

void VrrTimingController::updatePlayoutHistory(
    const PacedFrame& frame, const CadenceObservation& cadence,
    bool rebased, int64_t requiredUs)
{
    const uint64_t at = frame.decodeCompleteUs();
    const uint64_t elapsed = m_LastHistoryArrivalUs && at >= m_LastHistoryArrivalUs ?
        std::min<uint64_t>(at - m_LastHistoryArrivalUs, 33333) : 0;
    m_LastHistoryArrivalUs = at;
    if (!m_AppliedPlayoutDelayValid) {
        m_AppliedPlayoutDelayUs = playoutDelayStartUs();
        m_AppliedPlayoutDelayValid = true;
    }
    m_PlayoutBandValid = true;
    m_PlayoutBandIndex = 0; // One distribution, shared across source rates.

    if (m_Parameters.playoutPredictionOnly) {
        if (m_Parameters.playoutResponsiveBuffer >= 5) {
            if (rebased || cadence.sourceRateChanged || cadence.phaseDiscontinuity) {
                m_MeanMissBuffer.breakSequence();
                if (rebased || cadence.phaseDiscontinuity) m_IntervalBuffer.breakSequence();
            }
            const uint64_t requestedDemandUs =
                m_Parameters.playoutResponsiveBuffer >= 6 ?
                    m_IntervalBuffer.demand(m_AppliedPlayoutDelayUs) :
                    m_MeanMissBuffer.demand(m_AppliedPlayoutDelayUs);
            m_UnclampedRequestedPlayoutDelayUs = requestedDemandUs;
            m_RequestedPlayoutDelayUs = clampUnsigned(
                requestedDemandUs, playoutDelayMinimumUs(),
                playoutDelayMaximumUs());
            // Increases spread over several frames; release is time-based in
            // the observer. No prediction or cached percentile can authorize growth.
            if (m_RequestedPlayoutDelayUs > m_AppliedPlayoutDelayUs)
                m_AppliedPlayoutDelayUs += std::min<uint64_t>(125,
                    m_RequestedPlayoutDelayUs - m_AppliedPlayoutDelayUs);
            else
                m_AppliedPlayoutDelayUs = m_RequestedPlayoutDelayUs;
            m_AppliedPlayoutDelayUs = std::min(m_AppliedPlayoutDelayUs, playoutDelayMaximumUs());
            return;
        }
        if (m_Parameters.playoutResponsiveBuffer) {
            if (m_Parameters.playoutResponsiveBuffer >= 3 && cadence.sourceRateChanged) {
                // A confirmed material rate change retires incompatible history.
                // Preserve the applied delay and slew it toward fresh evidence.
                m_RecentReadiness = Vrr13::RecentReadiness(
                    m_Parameters.playoutReadinessWindowUs,
                    m_Parameters.playoutOnTimeTargetPerMillion,
                    m_Parameters.playoutResponsiveBuffer >= 4);
                m_ReadinessPrediction.reset();
            }
            // Calibration is diagnostic only. The selected live percentile
            // and expiring miss boost own demand, subject to the latency cap.
            // Revision 4 admits growth only for a hard miss or prevalent
            // 1-2 ms pressure; the raw target remains available for release.
            uint64_t requestedPlayoutDelayUs = saturatingAdd(
                m_RecentReadiness.demand(at), m_Parameters.playoutDelayMarginUs);
            if (m_Parameters.playoutResponsiveBuffer >= 4 &&
                    requestedPlayoutDelayUs > m_AppliedPlayoutDelayUs &&
                    !m_RecentReadiness.allowsGrowth(at, m_AppliedPlayoutDelayUs)) {
                requestedPlayoutDelayUs = m_AppliedPlayoutDelayUs;
            }
            m_UnclampedRequestedPlayoutDelayUs = requestedPlayoutDelayUs;
            m_RequestedPlayoutDelayUs = requestedPlayoutDelayUs;
            const auto desired = clampUnsigned(m_RequestedPlayoutDelayUs,
                playoutDelayMinimumUs(), playoutDelayMaximumUs());
            if (desired > m_AppliedPlayoutDelayUs) {
                m_AppliedPlayoutDelayUs += std::min(
                    desired - m_AppliedPlayoutDelayUs,
                    m_Parameters.playoutDelayAttackUs);
            }
            else if (m_RecentReadiness.canRelease(at)) {
                // Drain faster when both display and processing have spare
                // capacity; never spend post-deadline headroom as readiness.
                const auto headroom = std::min(recoveryHeadroomUs(),
                    m_SourcePeriodUs > m_RenderLeadUs ? m_SourcePeriodUs - m_RenderLeadUs : 0);
                const auto speed = 1000 + headroom * 1000 /
                    std::max<uint64_t>(1, m_SourcePeriodUs);
                const auto release = std::min<uint64_t>(250, scaledPerMille(
                    scaledPerMille(m_Parameters.playoutDelayReleaseUs, elapsed * 120 / 1000), speed));
                m_AppliedPlayoutDelayUs -= std::min(
                    m_AppliedPlayoutDelayUs - desired, release);
            }
            m_AppliedPlayoutDelayUs = std::min(m_AppliedPlayoutDelayUs, playoutDelayMaximumUs());
            return;
        }
        // Predict the required protection from delivery, FIFO work and
        // scheduler observations. Count existing padding once: neither our
        // intentional waits nor post-submission display delay is more work.
        const uint64_t protection = uint64_t(
            (m_PlayoutHistory.common() + m_PlayoutHistory.boost()) / 1000);
        if (m_Parameters.playoutReadinessHitchThresholdUs) {
            // History may retain/release a level, but only a fresh attributable
            // output hitch can raise it. In particular, cache loading cannot
            // authorize growth. Tolerance was already applied at the event;
            // do not add another fixed readiness margin.
            m_RequestedPlayoutDelayUs = std::min(m_RequestedPlayoutDelayUs, protection);
            m_UnclampedRequestedPlayoutDelayUs = m_RequestedPlayoutDelayUs;
        }
        else {
            m_RequestedPlayoutDelayUs = saturatingAdd(protection,
                                                    m_Parameters.playoutDelayMarginUs);
            m_UnclampedRequestedPlayoutDelayUs = m_RequestedPlayoutDelayUs;
        }
        const uint64_t desired = clampUnsigned(m_RequestedPlayoutDelayUs,
            playoutDelayMinimumUs(), playoutDelayMaximumUs());
        if (desired > m_AppliedPlayoutDelayUs) {
            m_AppliedPlayoutDelayUs += std::min(desired - m_AppliedPlayoutDelayUs,
                                              m_Parameters.playoutDelayAttackUs);
        }
        else if (m_PlayoutHistory.canRelease()) {
            const uint64_t release = scaledPerMille(m_Parameters.playoutDelayReleaseUs,
                                                    elapsed * 120 / 1000);
            m_AppliedPlayoutDelayUs -= std::min(m_AppliedPlayoutDelayUs - desired, release);
        }
        // The cold-start seed is already applied above, not extra protection
        // to add to the margin. Capacity remains a hard bound through rebases.
        m_AppliedPlayoutDelayUs = std::min(m_AppliedPlayoutDelayUs, playoutDelayMaximumUs());
        return;
    }

    if (m_Parameters.playoutNativeHitchAdaptation) {
        // Readiness may veto release. Growth requires a new native hitch or
        // an enabled submission estimate while native timing is unavailable.
        // Leave 3 ms above measured readiness when releasing padding.
        const uint64_t releaseFloor = saturatingAdd(
            uint64_t(m_PlayoutHistory.common() / 1000), 3000);
        // A demand beyond storage capacity must not pin release forever or
        // resurrect growth later when capacity becomes available again.
        m_UnclampedRequestedPlayoutDelayUs = m_RequestedPlayoutDelayUs;
        m_RequestedPlayoutDelayUs = std::min(m_RequestedPlayoutDelayUs,
                                            playoutDelayMaximumUs());
        if (m_RequestedPlayoutDelayUs <= m_AppliedPlayoutDelayUs) {
            m_RequestedPlayoutDelayUs = std::min(m_AppliedPlayoutDelayUs,
                                               releaseFloor);
        }
        const uint64_t desired = clampUnsigned(m_RequestedPlayoutDelayUs,
            playoutDelayMinimumUs(), playoutDelayMaximumUs());
        if (desired > m_AppliedPlayoutDelayUs) {
            m_AppliedPlayoutDelayUs += std::min(desired - m_AppliedPlayoutDelayUs,
                                              m_Parameters.playoutDelayAttackUs);
        }
        else if (activeSmoothnessFeedback(at).samples() &&
                 std::abs(signedDifference(at, activeSmoothnessFeedback(at).lastObservedUs())) <= 100000 &&
                 activeSmoothnessFeedback(at).canRelease() &&
                 m_PlayoutHistory.canRelease()) {
            const uint64_t release = scaledPerMille(m_Parameters.playoutDelayReleaseUs,
                                                    elapsed * 120 / 1000);
            m_AppliedPlayoutDelayUs -= std::min(m_AppliedPlayoutDelayUs - desired, release);
        }
        // Storage safety still applies when the source rate or work changes.
        m_AppliedPlayoutDelayUs = std::min(m_AppliedPlayoutDelayUs, playoutDelayMaximumUs());
        return;
    }

    // Classify stalls on the sender's clock. A long receive/decode gap with
    // steady RTP is receiver jitter and must not be discarded as a host stall.
    // The period term also admits ordinary 30 FPS content (33.3 ms).
    const uint64_t stall = std::max(m_Parameters.playoutStallExclusionUs,
                                    scaledPerMille(m_SourcePeriodUs, 1500));
    const uint64_t burst = scaledPerMille(m_SourcePeriodUs,
                                         m_Parameters.playoutBurstExclusionPerMille);
    if (!m_Parameters.playoutPredictionEnabled && !rebased && cadence.eligible && !cadence.phaseDiscontinuity &&
        cadence.frameDelta == 1 && cadence.intervalUs >= burst &&
        cadence.intervalUs <= stall) {
        const uint64_t raw = saturatingAdd(uint64_t(std::max<int64_t>(0, requiredUs)),
                                           m_Parameters.playoutDelayMarginUs);
        const uint64_t covered = raw > m_Parameters.playoutDelayToleranceUs ?
            raw - m_Parameters.playoutDelayToleranceUs : 0;
        // Reserve uses nanoseconds; clamp malformed replay clocks before conversion.
        const auto ns = [](uint64_t us) {
            return int64_t(std::min<uint64_t>(us, INT64_MAX / 1000)) * 1000;
        };
        const uint64_t decoderQueueUs = frame.reassembledUs() &&
            frame.decodeSubmitUs() >= frame.reassembledUs() ?
            frame.decodeSubmitUs() - frame.reassembledUs() : 0;
        m_WorkloadEpisode.observe(m_PlayoutHistory, ns(covered),
            ns(m_AppliedPlayoutDelayUs), ns(at), decoderQueueUs, m_SourcePeriodUs);
    }
    const bool smoothnessControlsDelay = m_Parameters.playoutSmoothnessFeedbackEnabled &&
        !m_Parameters.playoutReadinessDrivenAdaptation;
    uint64_t protection = uint64_t(m_PlayoutHistory.target(
        100000000, int64_t(playoutDelayStartUs()) * 1000,
        m_Parameters.playoutPredictionEnabled && !smoothnessControlsDelay &&
            !m_Parameters.playoutReadinessDrivenAdaptation ? int64_t(recoveryHeadroomUs()) * 1000 : 0) / 1000);
    if (smoothnessControlsDelay) {
        protection = std::max(protection, std::max(m_SubmissionSmoothness.protectionUs(), m_NativeSmoothness.protectionUs()));
        protection -= std::min(protection, recoveryHeadroomUs());
    }
    // Native/submission interval errors remain measured outcomes. A delay
    // after readiness can move with the playout target, so charging the
    // existing buffer plus that error produces a ratchet with no benefit.
    // Inter-frame recovery headroom cannot pay this frame's readiness
    // shortfall: time available after its deadline is not padding before it.
    // FIFO service already accounts for backlog recovery when it estimates
    // the readiness demand, so do not subtract that headroom again here.
    m_RequestedPlayoutDelayUs = saturatingAdd(protection,
        m_Parameters.playoutPredictionEnabled ? m_Parameters.playoutDelayMarginUs : 0);
    m_UnclampedRequestedPlayoutDelayUs = m_RequestedPlayoutDelayUs;
    const uint64_t desired = clampUnsigned(m_RequestedPlayoutDelayUs,
        playoutDelayMinimumUs(), playoutDelayMaximumUs());
    // Bound attack below the smoothness threshold; production responds faster
    // than legacy captures while avoiding a single large timeline step.
    // Release uses elapsed wall time at the former 120 FPS rate, with a cap
    // on recovery gaps. Five-minute memory does not imply five-minute startup.
    if (desired > m_AppliedPlayoutDelayUs) {
        m_AppliedPlayoutDelayUs += std::min(desired - m_AppliedPlayoutDelayUs,
                                          m_Parameters.playoutDelayAttackUs);
    }
    else if (m_PlayoutHistory.canRelease() && (!smoothnessControlsDelay ||
             (m_SubmissionSmoothness.canRelease() && m_NativeSmoothness.canRelease()))) {
        const uint64_t release = scaledPerMille(m_Parameters.playoutDelayReleaseUs,
                                                elapsed * 120 / 1000);
        m_AppliedPlayoutDelayUs -= std::min(m_AppliedPlayoutDelayUs - desired, release);
    }
    // Capacity is a hard safety bound, including after a source-rate increase.
    m_AppliedPlayoutDelayUs = std::min(m_AppliedPlayoutDelayUs, playoutQueueLimitUs());
}

uint64_t VrrTimingController::rtpTicksToUs(uint64_t ticks)
{
    constexpr uint64_t kTicksPerMillisecond = kRtpClockRate / 1000;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    if (ticks > maximum / 1000) {
        return ticks / kTicksPerMillisecond * 1000;
    }
    return ticks * 1000 / kTicksPerMillisecond;
}

const VrrTimingParameters& VrrTimingController::parameters() const
{
    return m_Parameters;
}

VrrTimingDiagnostics VrrTimingController::diagnostics() const
{
    VrrTimingDiagnostics value;
    value.readinessPhaseUs = m_ReadinessPhaseUs;
    value.readinessDemandUs = m_ReadinessDemandUs;
    value.appliedReadinessReserveUs = m_AppliedReadinessReserveUs;
    value.renderBaselineUs = m_RenderBaselineUs;
    value.renderInsuranceUs = renderInsuranceUs();
    value.gpuReadinessLeadUs = gpuReadinessLeadUs();
    value.pacingLatencyBudgetUs = pacingLatencyBudgetUs();
    value.cadenceSamples = m_CadenceSamples.size();
    value.rateCandidateSamples = m_RateCandidateSamples.size();
    value.readinessSamples = m_ReadyOffsets.size();
    value.preparationSamples = m_PreparationDurations.size();
    value.renderSchedulerSamples = m_RenderSchedulerDelays.size();
    value.targetSchedulerSamples = m_TargetSchedulerDelays.size();
    value.cleanSpacingFrames = m_CleanSpacingFrames;
    value.phaseErrorFrames = m_PhaseErrorFrames;
    value.readinessModelValid = m_ReadinessModelValid;
    return value;
}

uint64_t VrrTimingController::renderLeadFloorUs() const
{
    // Once measurements show a lower baseline, the historical 1 ms floor is
    // discretionary too and cannot violate the pacing policy at very high Hz.
    return std::min(
        std::min(m_Parameters.renderLeadFloorUs, m_SourcePeriodUs),
        saturatingAdd(m_RenderBaselineUs, renderInsuranceCeilingUs()));
}

uint64_t VrrTimingController::renderLeadCeilingUs() const
{
    uint64_t ceilingUs = std::min(
        saturatingAdd(m_RenderBaselineUs, renderInsuranceCeilingUs()),
        m_SourcePeriodUs);
    // Zero disables the legacy absolute render ceiling. Nonzero values remain
    // available to replay old captures and run explicit policy experiments.
    if (m_Parameters.renderLeadCeilingUs != 0) {
        ceilingUs = std::min(ceilingUs,
                             m_Parameters.renderLeadCeilingUs);
    }
    return std::max(renderLeadFloorUs(), ceilingUs);
}

uint64_t VrrTimingController::pacingLatencyBudgetUs() const
{
    if (!pacingLatencyPolicyEnabled()) {
        return 0;
    }
    // Immediate VRR presentation saves roughly half a scanout on average.
    // Spend no more than that on readiness plus render-tail insurance. The
    // render baseline is unavoidable work in fixed and adaptive modes alike.
    // Smoothness mode explicitly permits a cadence-scaled additional source
    // interval, matching the worker's extra stale-frame tolerance. A zero
    // explicit ratio retains the pre-schema behavior for old captures.
    const uint64_t lowLatencyBudgetUs = m_DisplayPeriodUs /
        std::max<uint64_t>(1, m_Parameters.pacingLatencyBudgetDivisor);
    uint64_t extraBudgetUs = 0;
    if (m_Parameters.pacingLatencyExtraPeriodNumerator != 0) {
        const uint64_t numerator =
            m_Parameters.pacingLatencyExtraPeriodNumerator;
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        const uint64_t scaledUs = m_SourcePeriodUs > maximum / numerator ?
            maximum : m_SourcePeriodUs * numerator;
        extraBudgetUs = scaledUs /
            m_Parameters.pacingLatencyExtraPeriodDenominator;
    }
    else if (m_Config.allowAdditionalQueuedFrame &&
             m_Parameters.pacingLatencyQueueModeExtra != 0) {
        // Pre-schema captures made under the retired smoothness option.
        extraBudgetUs = m_SourcePeriodUs;
    }
    return saturatingAdd(lowLatencyBudgetUs, extraBudgetUs);
}

bool VrrTimingController::pacingLatencyPolicyEnabled() const
{
    // Zero is reserved for replaying schema-5 captures made before the
    // baseline/tail policy existed. New production configurations use 2.
    return m_Parameters.pacingLatencyBudgetDivisor != 0;
}

uint64_t VrrTimingController::minimumReadinessReserveUs() const
{
    if (!pacingLatencyPolicyEnabled()) {
        return m_Parameters.minimumReadinessReserveUs;
    }
    return std::min(m_Parameters.minimumReadinessReserveUs,
                    pacingLatencyBudgetUs());
}

uint64_t VrrTimingController::renderInsuranceCeilingUs() const
{
    if (!pacingLatencyPolicyEnabled()) {
        return std::numeric_limits<uint64_t>::max();
    }
    const uint64_t budgetUs = pacingLatencyBudgetUs();
    const uint64_t minimumReserveUs = minimumReadinessReserveUs();
    return budgetUs > minimumReserveUs ?
        budgetUs - minimumReserveUs : 0;
}

uint64_t VrrTimingController::renderInsuranceUs() const
{
    return m_RenderLeadUs > m_RenderBaselineUs ?
        m_RenderLeadUs - m_RenderBaselineUs : 0;
}

uint64_t VrrTimingController::readinessReserveCeilingUs() const
{
    if (!pacingLatencyPolicyEnabled()) {
        return readinessCeilingUs();
    }
    const uint64_t budgetUs = pacingLatencyBudgetUs();
    const uint64_t insuranceUs = renderInsuranceUs();
    return budgetUs > insuranceUs ? budgetUs - insuranceUs : 0;
}

uint64_t VrrTimingController::readinessPeriodFloorUs() const
{
    if (m_Parameters.readinessPeriodFloorNumerator == 0 ||
            headroomUs() > m_DisplayPeriodUs) {
        return 0;
    }

    const uint64_t numerator =
        m_Parameters.readinessPeriodFloorNumerator;
    const uint64_t denominator =
        m_Parameters.readinessPeriodFloorDenominator;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    const uint64_t scaledUs = m_SourcePeriodUs > maximum / numerator ?
        maximum : m_SourcePeriodUs * numerator;
    return scaledUs / denominator;
}

size_t VrrTimingController::readinessLearningSampleLimit() const
{
    if (m_Parameters.readinessLearningWindowUs == 0) {
        return m_Parameters.readinessLearningSamples;
    }

    const uint64_t roundedSamples = std::max<uint64_t>(
        1, (m_Parameters.readinessLearningWindowUs +
            m_SourcePeriodUs / 2) / m_SourcePeriodUs);
    return static_cast<size_t>(std::max<uint64_t>(
        m_Parameters.minimumReadinessSamples,
        std::min<uint64_t>(m_Parameters.readinessLearningSamples,
                           roundedSamples)));
}

uint64_t VrrTimingController::readinessCeilingUs() const
{
    return std::min(m_Parameters.readinessCeilingUs, m_SourcePeriodUs);
}

uint64_t VrrTimingController::guardCeilingUs() const
{
    const uint64_t sourceSlackUs = m_SourcePeriodUs > m_DisplayPeriodUs ?
        m_SourcePeriodUs - m_DisplayPeriodUs : 0;
    return std::max(
        m_BaseGuardUs,
        std::min(m_Parameters.maximumAdaptiveGuardUs, sourceSlackUs));
}

uint64_t VrrTimingController::periodForRate(int rateHz, uint64_t fallbackUs)
{
    if (rateHz <= 0) {
        return fallbackUs;
    }
    const uint64_t rate = static_cast<uint64_t>(rateHz);
    return std::max<uint64_t>(1,
        (kMicrosecondsPerSecond + rate / 2) / rate);
}

uint64_t VrrTimingController::periodForRateQ16(int rateHz,
                                               uint64_t fallbackQ16)
{
    if (rateHz <= 0) {
        return fallbackQ16;
    }
    const uint64_t rate = static_cast<uint64_t>(rateHz);
    constexpr uint64_t kPeriodQ16Scale =
        kMicrosecondsPerSecond * kQ16One;
    return std::max<uint64_t>(1,
        (kPeriodQ16Scale + rate / 2) / rate);
}

uint64_t VrrTimingController::saturatingAdd(uint64_t left, uint64_t right)
{
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    return left > maximum - right ? maximum : left + right;
}

uint64_t VrrTimingController::addSigned(uint64_t value, int64_t adjustment)
{
    if (adjustment >= 0) {
        return saturatingAdd(value, static_cast<uint64_t>(adjustment));
    }
    const uint64_t magnitude =
        static_cast<uint64_t>(-(adjustment + 1)) + 1;
    return value > magnitude ? value - magnitude : 0;
}

int64_t VrrTimingController::signedDifference(uint64_t left, uint64_t right)
{
    if (left >= right) {
        const uint64_t difference = left - right;
        return difference > static_cast<uint64_t>(
                   std::numeric_limits<int64_t>::max()) ?
            std::numeric_limits<int64_t>::max() :
            static_cast<int64_t>(difference);
    }

    const uint64_t difference = right - left;
    if (difference > static_cast<uint64_t>(
                         std::numeric_limits<int64_t>::max())) {
        return std::numeric_limits<int64_t>::min();
    }
    return -static_cast<int64_t>(difference);
}

uint64_t VrrTimingController::roundedQ16(uint64_t valueQ16)
{
    return saturatingAdd(valueQ16, kQ16Half) / kQ16One;
}

bool VrrTimingController::withinPercent(uint64_t value, uint64_t reference,
                                        unsigned int percent)
{
    if (reference == 0) {
        return value == 0;
    }
    const uint64_t difference = value > reference ? value - reference :
                                                     reference - value;
    return static_cast<long double>(difference) * 100.0L <=
        static_cast<long double>(reference) * percent;
}
