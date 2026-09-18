// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include "vrrtypes.h"
#include "reserve.h"
#include "workload.h"
#include "prediction.h"
#include "readinessfeedback.h"
#include "meanmissbuffer.h"
#include "intervalbuffer.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

// This list is also the replay/trace parameter schema. Keeping the JSON name,
// C++ member, type, and production default together prevents those copies from
// drifting while avoiding hand-written serialization for every field. The
// display-period latch terms default to zero so production uses the absolute
// headroom thresholds; non-zero ratios remain available to replay captures
// made with display-scaled protection.
#define VRR_TIMING_PARAMETER_FIELDS(X) \
    X(uint64_t, latency_fix_enabled, latencyFixEnabled, 0) \
    X(uint64_t, latency_fix_all_rates, latencyFixAllRates, 0) \
    X(uint64_t, latency_fix_delay_period_per_mille, latencyFixDelayPeriodPerMille, 500) \
    X(uint64_t, playout_delay_cap_source_period_per_mille, playoutDelayCapSourcePeriodPerMille, 0) \
    X(uint64_t, playout_delay_cap_uses_observed_period, playoutDelayCapUsesObservedPeriod, 0) \
    X(uint64_t, playout_capacity_telemetry, playoutCapacityTelemetry, 0) \
    X(uint64_t, playout_gpu_readiness_adaptation, playoutGpuReadinessAdaptation, 0) \
    X(uint64_t, playout_gpu_readiness_window_us, playoutGpuReadinessWindowUs, 10000000) \
    X(unsigned int, playout_gpu_readiness_percentile, playoutGpuReadinessPercentile, 99) \
    X(uint64_t, playout_gpu_readiness_margin_us, playoutGpuReadinessMarginUs, 500) \
    X(uint64_t, playout_gpu_readiness_attack_us, playoutGpuReadinessAttackUs, 1000) \
    X(uint64_t, playout_gpu_readiness_release_us_per_second, playoutGpuReadinessReleaseUsPerSecond, 250) \
    X(uint64_t, playout_gpu_readiness_maximum_us, playoutGpuReadinessMaximumUs, 12000) \
    X(uint64_t, playout_prediction_only, playoutPredictionOnly, 0) \
    X(uint64_t, playout_responsive_buffer, playoutResponsiveBuffer, 0) \
    X(uint64_t, playout_mean_miss_hold_us, playoutMeanMissHoldUs, 4000000) \
    X(uint64_t, playout_mean_miss_release_us_per_second, playoutMeanMissReleaseUsPerSecond, 200) \
    X(uint64_t, playout_on_time_target_per_million, playoutOnTimeTargetPerMillion, 990000) \
    X(uint64_t, playout_readiness_window_us, playoutReadinessWindowUs, 3000000) \
    X(uint64_t, playout_readiness_hitch_threshold_us, playoutReadinessHitchThresholdUs, 0) \
    X(uint64_t, playout_require_display_events, playoutRequireDisplayEvents, 0) \
    X(uint64_t, playout_submission_estimate_fallback, playoutSubmissionEstimateFallback, 0) \
    X(uint64_t, playout_native_hitch_adaptation, playoutNativeHitchAdaptation, 0) \
    X(uint64_t, playout_readiness_driven_adaptation, playoutReadinessDrivenAdaptation, 0) \
    X(uint64_t, playout_stable_smoothness_reference, playoutStableSmoothnessReference, 0) \
    X(uint64_t, render_start_preserve_learned_lead, renderStartPreserveLearnedLead, 0) \
    X(uint64_t, playout_smoothness_feedback_enabled, playoutSmoothnessFeedbackEnabled, 0) \
    X(uint64_t, playout_prediction_enabled, playoutPredictionEnabled, 0) \
    /* 0: historical cadence latch; 1: slot only; 2: slot plus safety headroom. */ \
    X(uint64_t, playout_per_frame_latch, playoutPerFrameLatch, 0) \
    X(uint64_t, playout_adaptive_only, playoutAdaptiveOnly, 0) \
    X(uint64_t, playout_rate_protection_enabled, playoutRateProtectionEnabled, 0) \
    X(uint64_t, playout_history_enabled, playoutHistoryEnabled, 0) \
    X(uint64_t, maximum_forward_movement_us, maximumForwardMovementUs, 1000000) \
    X(uint64_t, render_lead_floor_us, renderLeadFloorUs, 1000) \
    X(uint64_t, render_lead_ceiling_us, renderLeadCeilingUs, 0) \
    X(uint64_t, render_lead_slack_us, renderLeadSlackUs, 0) \
    X(unsigned int, render_baseline_percentile, renderBaselinePercentile, 50) \
    X(uint64_t, pacing_latency_budget_divisor, pacingLatencyBudgetDivisor, 2) \
    X(uint64_t, pacing_latency_extra_period_numerator, pacingLatencyExtraPeriodNumerator, 0) \
    X(uint64_t, pacing_latency_extra_period_denominator, pacingLatencyExtraPeriodDenominator, 1) \
    X(uint64_t, pacing_latency_queue_mode_extra, pacingLatencyQueueModeExtra, 1) \
    X(uint64_t, presentation_safety_us, presentationSafetyUs, 0) \
    X(uint64_t, source_playout_delay_us, sourcePlayoutDelayUs, 0) \
    X(uint64_t, timestamp_playout_enabled, timestampPlayoutEnabled, 0) \
    X(uint64_t, playout_offset_window_us, playoutOffsetWindowUs, 3000000) \
    X(uint64_t, playout_offset_slew_us, playoutOffsetSlewUs, 20) \
    /* Zero defaults retain the per-frame, unfiltered historical mapping. */ \
    X(uint64_t, playout_offset_cadence_gate, playoutOffsetCadenceGate, 0) \
    X(uint64_t, playout_offset_slew_us_per_second, playoutOffsetSlewUsPerSecond, 0) \
    X(uint64_t, playout_offset_source_clock, playoutOffsetSourceClock, 0) \
    X(uint64_t, playout_offset_maximum_step_us, playoutOffsetMaximumStepUs, 100) \
    X(size_t, playout_offset_warmup_samples, playoutOffsetWarmupSamples, 64) \
    X(uint64_t, playout_delay_adaptive, playoutDelayAdaptive, 0) \
    X(uint64_t, playout_delay_start_us, playoutDelayStartUs, 0) \
    X(uint64_t, playout_delay_minimum_us, playoutDelayMinimumUs, 1000) \
    X(uint64_t, playout_delay_maximum_us, playoutDelayMaximumUs, 12000) \
    X(uint64_t, playout_delay_percentile_per_mille, playoutDelayPercentilePerMille, 980) \
    X(uint64_t, playout_delay_margin_us, playoutDelayMarginUs, 300) \
    X(uint64_t, playout_delay_tolerance_us, playoutDelayToleranceUs, 0) \
    X(uint64_t, playout_delay_attack_us, playoutDelayAttackUs, 50) \
    X(uint64_t, playout_delay_release_us, playoutDelayReleaseUs, 10) \
    X(size_t, playout_delay_minimum_samples, playoutDelayMinimumSamples, 250) \
    X(size_t, playout_delay_release_samples, playoutDelayReleaseSamples, 500) \
    X(size_t, playout_delay_reservoir_samples, playoutDelayReservoirSamples, 1024) \
    X(uint64_t, playout_band_width_hz, playoutBandWidthHz, 20) \
    X(uint64_t, playout_band_stale_us, playoutBandStaleUs, 120000000) \
    X(uint64_t, playout_stall_exclusion_us, playoutStallExclusionUs, 25000) \
    X(uint64_t, playout_burst_exclusion_per_mille, playoutBurstExclusionPerMille, 0) \
    X(uint64_t, playout_smoothing_gain_per_mille, playoutSmoothingGainPerMille, 0) \
    X(uint64_t, playout_smoothing_period_alpha_per_mille, playoutSmoothingPeriodAlphaPerMille, 50) \
    X(uint64_t, playout_smoothing_max_lag_us, playoutSmoothingMaxLagUs, 8000) \
    X(uint64_t, playout_smoothing_snap_per_mille, playoutSmoothingSnapPerMille, 1000) \
    X(uint64_t, playout_metronome_enabled, playoutMetronomeEnabled, 0) \
    X(uint64_t, playout_delay_start_period_per_mille, playoutDelayStartPeriodPerMille, 0) \
    X(uint64_t, playout_delay_maximum_period_per_mille, playoutDelayMaximumPeriodPerMille, 0) \
    X(uint64_t, playout_phase_step_minimum_us, playoutPhaseStepMinimumUs, 30) \
    X(uint64_t, playout_phase_step_divisor, playoutPhaseStepDivisor, 64) \
    X(uint64_t, playout_phase_step_period_per_mille, playoutPhaseStepPeriodPerMille, 20) \
    X(uint64_t, playout_phase_residual_window_frames, playoutPhaseResidualWindowFrames, 256) \
    X(uint64_t, playout_phase_deadband_us, playoutPhaseDeadbandUs, 100) \
    X(uint64_t, playout_metronome_period_window_frames, playoutMetronomePeriodWindowFrames, 128) \
    X(uint64_t, playout_offset_reseed_frames, playoutOffsetReseedFrames, 1) \
    X(uint64_t, playout_motion_deadband_enabled, playoutMotionDeadbandEnabled, 0) \
    X(uint64_t, playout_motion_floor_us, playoutMotionFloorUs, 1000) \
    X(uint64_t, playout_motion_ceiling_period_per_mille, playoutMotionCeilingPeriodPerMille, 750) \
    X(uint64_t, playout_motion_gain_per_mille, playoutMotionGainPerMille, 1500) \
    X(unsigned int, playout_motion_percentile, playoutMotionPercentile, 90) \
    X(size_t, playout_motion_window_frames, playoutMotionWindowFrames, 128) \
    X(size_t, playout_motion_minimum_samples, playoutMotionMinimumSamples, 16) \
    X(uint64_t, playout_delay_slew_across_bands, playoutDelaySlewAcrossBands, 0) \
    X(uint64_t, playout_prepare_on_arrival, playoutPrepareOnArrival, 0) \
    X(uint64_t, render_start_after_submission_us, renderStartAfterSubmissionUs, 0) \
    X(uint64_t, render_start_minimum_lead_us, renderStartMinimumLeadUs, 1500) \
    X(uint64_t, playout_stall_burst_exclusion, playoutStallBurstExclusion, 0) \
    X(uint64_t, latched_floor_disabled, latchedFloorDisabled, 0) \
    X(uint64_t, readiness_ceiling_us, readinessCeilingUs, 10000) \
    X(uint64_t, minimum_readiness_reserve_us, minimumReadinessReserveUs, 500) \
    X(uint64_t, cold_start_readiness_demand_us, coldStartReadinessDemandUs, 1500) \
    X(uint64_t, arrival_spread_guard_us, arrivalSpreadGuardUs, 900) \
    X(uint64_t, readiness_acquire_step_us, readinessAcquireStepUs, 1000) \
    X(uint64_t, readiness_learning_window_us, readinessLearningWindowUs, 0) \
    X(uint64_t, readiness_floor_period_numerator, readinessPeriodFloorNumerator, 0) \
    X(uint64_t, readiness_floor_period_denominator, readinessPeriodFloorDenominator, 1) \
    X(uint64_t, retain_readiness_on_phase_reset, retainReadinessOnPhaseReset, 0) \
    X(uint64_t, maximum_render_wake_lead_us, maximumRenderWakeLeadUs, 2000) \
    X(uint64_t, maximum_target_wake_lead_us, maximumTargetWakeLeadUs, 500) \
    X(uint64_t, minimum_guard_us, minimumGuardUs, 100) \
    X(uint64_t, latch_enter_headroom_us, latchedPresentationHeadroomUs, 225) \
    X(uint64_t, latch_exit_headroom_us, latchedPresentationExitHeadroomUs, 400) \
    X(uint64_t, latch_base_guard_exit, latchedPresentationBaseGuardExit, 0) \
    X(size_t, cadence_stability_latch_frames, cadenceStabilityLatchFrames, 64) \
    X(uint64_t, latch_enter_headroom_period_numerator, latchedPresentationHeadroomPeriodNumerator, 0) \
    X(uint64_t, latch_enter_headroom_period_denominator, latchedPresentationHeadroomPeriodDenominator, 1) \
    X(uint64_t, latch_exit_headroom_period_numerator, latchedPresentationExitHeadroomPeriodNumerator, 0) \
    X(uint64_t, latch_exit_headroom_period_denominator, latchedPresentationExitHeadroomPeriodDenominator, 1) \
    X(uint64_t, maximum_base_guard_us, maximumBaseGuardUs, 250) \
    X(uint64_t, maximum_adaptive_guard_us, maximumAdaptiveGuardUs, 1000) \
    X(uint64_t, guard_step_us, guardStepUs, 50) \
    X(size_t, guard_decay_frames, guardDecayFrames, 120) \
    X(size_t, scheduler_learning_samples, schedulerLearningSamples, 19) \
    X(size_t, readiness_learning_samples, readinessLearningSamples, 16) \
    X(size_t, preparation_learning_samples, preparationLearningSamples, 96) \
    X(size_t, minimum_readiness_samples, minimumReadinessSamples, 16) \
    X(size_t, minimum_cadence_samples, minimumCadenceSamples, 6) \
    X(size_t, maximum_cadence_samples, maximumCadenceSamples, 512) \
    X(size_t, rate_candidate_samples, rateCandidateSamples, 3) \
    X(uint64_t, rate_candidate_minimum_us, rateCandidateMinimumUs, 0) \
    X(uint64_t, loose_cadence_window_us, looseCadenceWindowUs, 350000) \
    X(uint64_t, tight_cadence_window_us, tightCadenceWindowUs, 1000000) \
    X(uint64_t, major_cadence_ratio_numerator, majorCadenceRatioNumerator, 7) \
    X(uint64_t, major_cadence_ratio_denominator, majorCadenceRatioDenominator, 2) \
    X(uint64_t, candidate_cadence_ratio_numerator, candidateCadenceRatioNumerator, 2) \
    X(uint64_t, candidate_cadence_ratio_denominator, candidateCadenceRatioDenominator, 1) \
    X(unsigned int, material_rate_change_percent, materialRateChangePercent, 12) \
    X(size_t, phase_error_frames, phaseErrorFrames, 3) \
    X(unsigned int, preparation_percentile, preparationPercentile, 99) \
    X(unsigned int, scheduler_percentile, schedulerPercentile, 95) \
    X(unsigned int, readiness_low_percentile, readinessLowPercentile, 0) \
    X(unsigned int, readiness_tight_percentile, readinessTightPercentile, 100) \
    X(unsigned int, readiness_loose_percentile, readinessLoosePercentile, 80) \
    X(uint64_t, readiness_attack_numerator, readinessAttackNumerator, 1) \
    X(uint64_t, readiness_attack_denominator, readinessAttackDenominator, 1) \
    X(uint64_t, readiness_release_numerator, readinessReleaseNumerator, 1) \
    X(uint64_t, readiness_release_denominator, readinessReleaseDenominator, 32) \
    X(uint64_t, usable_headroom_numerator, usableHeadroomNumerator, 3) \
    X(uint64_t, usable_headroom_denominator, usableHeadroomDenominator, 4) \
    X(uint64_t, loose_headroom_display_periods, looseHeadroomDisplayPeriods, 2) \
    X(uint64_t, base_guard_divisor, baseGuardDivisor, 96)

// Every value that changes VRR policy remains replaceable by replay without
// rebuilding the controller. Production callers use these defaults.
struct VrrTimingParameters {
#define VRR_DECLARE_TIMING_PARAMETER(type, jsonName, memberName, defaultValue) \
    type memberName = defaultValue;
    VRR_TIMING_PARAMETER_FIELDS(VRR_DECLARE_TIMING_PARAMETER)
#undef VRR_DECLARE_TIMING_PARAMETER
};

// Resolve mode-dependent production policy once for both the live worker and
// the replay baseline. Candidate replay configs may still override any field.
VrrTimingParameters vrrTimingParametersForSession(
    const VrrSessionConfig& config);

struct VrrTimingDiagnostics {
    int64_t readinessPhaseUs = 0;
    uint64_t readinessDemandUs = 0;
    uint64_t appliedReadinessReserveUs = 0;
    uint64_t renderBaselineUs = 0;
    uint64_t renderInsuranceUs = 0;
    uint64_t gpuReadinessLeadUs = 0;
    uint64_t pacingLatencyBudgetUs = 0;
    size_t cadenceSamples = 0;
    size_t rateCandidateSamples = 0;
    size_t readinessSamples = 0;
    size_t preparationSamples = 0;
    size_t renderSchedulerSamples = 0;
    size_t targetSchedulerSamples = 0;
    size_t cleanSpacingFrames = 0;
    size_t phaseErrorFrames = 0;
    bool readinessModelValid = false;
};

// Platform-neutral, feed-forward VRR timing. The controller projects the
// sender clock into the local monotonic epoch and learns bounded readiness,
// render, scheduler, and spacing budgets. It contains no renderer/native API
// types; the worker translates platform observations into neutral timing
// feedback.
struct VrrTimingDecision {
    uint64_t frameNumber = 0;
    uint64_t smoothnessProtectionUs = 0;
    uint64_t requestedPlayoutDelayUs = 0;
    uint64_t submissionSmoothnessSamples = 0, submissionSmoothnessMisses = 0;
    uint64_t nativeSmoothnessSamples = 0, nativeSmoothnessMisses = 0;
    bool playoutCapacityLimited = false;
    uint64_t originalScanoutUs = 0;
    uint64_t predictedScanoutUs = 0;
    uint64_t compositorLeadUs = 0;
    uint64_t recoveryHeadroomUs = 0;
    // The original smoothed slot, before readiness and display-floor clamps.
    // Observation only: a late frame must never rewrite its own deadline.
    uint64_t originalTargetUs = 0;
    uint64_t sourceTimeUs = 0;
    uint64_t sourceIntervalUs = 0;
    uint64_t sourcePeriodUs = 0;

    int64_t readyOffsetUs = 0;
    // How far the display floor pushed the target past the frame's own slot.
    uint64_t presentationFloorPushUs = 0;
    int64_t readinessBudgetUs = 0;
    // The source playout delay this target was built with: the adaptive
    // per-band delay under timestamp playout, else the fixed parameter.
    uint64_t playoutDelayUs = 0;
    // Cadence smoothing: how far this target was moved from its raw mapped
    // slot (positive = later) to keep presented intervals even. Under the
    // metronome this is the schedule's lag behind the mapped sender clock;
    // a lag of a full source period or more means this frame missed its tick
    // and is occupying a later one.
    int64_t cadenceSmoothingUs = 0;
    // Metronome: the frame arrived this many whole source periods after its
    // tick. Non-zero means a fresher successor, if one is already queued,
    // should be shown instead.
    uint64_t missedTicks = 0;

    uint64_t renderStartUs = 0;
    uint64_t targetUs = 0;
    uint64_t guardUs = 0;
    uint64_t headroomUs = 0;
    uint64_t timingBudgetUs = 0;
    uint64_t renderLeadUs = 0;
    uint64_t renderWakeLeadUs = 0;
    // Preparation begins this much earlier than the non-GPU render budget
    // requires. It is a learned, bounded head start and never moves targetUs.
    uint64_t gpuReadinessLeadUs = 0;
    uint64_t targetWakeLeadUs = 0;

    bool latchedPresentation = false;
    bool usedRtpTimestamp = false;
    bool cadenceEligible = false;
    bool sourceRateChanged = false;
    bool phaseDiscontinuity = false;
    bool rebased = false;
};

class VrrTimingController {
public:
    explicit VrrTimingController(const VrrSessionConfig& config,
                                 bool canLatchPresentation = true);
    VrrTimingController(const VrrSessionConfig& config,
                        bool canLatchPresentation,
                        const VrrTimingParameters& parameters);

    void reset();

    // Starts a fresh source epoch while retaining learned render/wake budgets
    // and the last submission instant used by the display-spacing floor.
    void rebase();

    VrrTimingDecision schedule(const PacedFrame& frame, uint64_t nowUs);

    // Samples affect subsequent frames only. The current presentation target
    // never moves after rendering has begun.
    void notePreparationDuration(uint64_t preparationDurationUs,
                                 uint64_t acquisitionWaitUs = 0,
                                 uint64_t preparationCompleteUs = 0,
                                 uint64_t gpuReadyWaitUs = 0);
    // Successful renderer fence waits are feedback for future preparation
    // starts. Failed/unknown waits are deliberately not learned.
    void noteGpuReadyWait(uint64_t waitUs, bool completed,
                          uint64_t completionUs = 0);
    void noteSchedulerDelays(uint64_t renderDelayUs,
                             uint64_t targetDelayUs,
                             bool targetDelayValid);

    // A positive deficit means the worker reached the presentation boundary
    // before the display-spacing floor. The worker corrects the current frame;
    // this feedback adjusts one bounded guard for future frames.
    void noteSpacingDeficit(uint64_t deficitUs);

    // The worker records its own call boundary and supplies only the neutral
    // lifecycle result. The timing controller has no renderer/native types.
    void noteSubmission(bool submitted, bool cancelled,
                        uint64_t submissionUs);
    void notePresentation(const Vrr13::PresentationObservation& observation);
    uint64_t nativeCadenceIntervals() const { return m_NativeCadenceIntervals; }
    uint64_t estimatedCadenceIntervals() const { return m_EstimatedCadenceIntervals; }
    uint64_t estimatedCadenceHitches() const { return m_EstimatedCadenceHitches; }
    uint64_t nativeCadenceHitches() const { return m_NativeCadenceHitches; }
    Vrr13::SmoothnessFeedback::Sample smoothnessSample(const VrrTimingDecision& decision) const;
    Vrr13::IntervalBuffer::Stats intervalStats() const { return m_IntervalBuffer.stats(); }
    uint64_t typicalRenderUs() const;
    uint64_t recoveryHeadroomUs() const;

    uint64_t timingBudgetUs() const;
    int64_t readinessBudgetUs() const;
    uint64_t headroomUs() const;
    uint64_t sourcePeriodUs() const;
    uint64_t displayPeriodUs() const;
    uint64_t guardUs() const;
    uint64_t renderLeadUs() const;
    uint64_t gpuReadinessLeadUs() const;
    uint64_t targetWakeLeadUs() const;
    uint64_t earliestSubmissionUs() const;
    uint64_t lastSubmissionUs() const;
    bool hasLastSubmission() const;
    // Timestamp playout: the applied sender-to-local clock offset and whether
    // the last scheduled frame used the fixed-delay timestamp path.
    int64_t playoutOffsetUs() const;
    bool timestampPlayoutActive() const;
    // Adaptive playout delay: the delay currently applied, the rate band it
    // belongs to (fitted source rate divided by the band width), and how many
    // lateness samples that band has admitted.
    uint64_t playoutDelayUs() const;
    unsigned int playoutBandIndex() const;
    uint64_t playoutBandSamples() const;
    const VrrTimingParameters& parameters() const;
    VrrTimingDiagnostics diagnostics() const;
    const Vrr13::Reserve& playoutHistory() const { return m_PlayoutHistory; }
    bool loadPlayoutHistory(const std::vector<int64_t>& profile) {
        return !m_HaveTimeline && m_PlayoutHistory.loadProfile(profile);
    }
    uint64_t playoutQueueLimitUs() const;
    bool latencyFixActive() const { return m_LatencyFixActive; }

private:
    void updateLatencyFixState();
    uint64_t latencyFixDelayLimitUs() const;
    uint64_t playoutDelayCapUs() const;
    uint64_t gpuReadinessCeilingUs() const;
    bool m_LatencyFixActive = false;
    Vrr13::RecentReadiness m_RecentReadiness;
    uint64_t m_CadenceStableSinceUs = 0;
    uint64_t m_PreviousSmoothingIntervalUs = 0;
    struct PendingFrame {
        Vrr13::SmoothnessFeedback::Sample smoothness;
        Vrr13::ReadinessPrediction::Probe prediction;
        uint64_t renderSchedulerUs = 0;
        bool valid = false;
        bool cadenceEligible = false;
        bool hasPreparationDuration = false;
        int64_t readyOffsetUs = 0;
        uint64_t preparationDurationUs = 0;
        uint64_t preparationCompleteUs = 0;
        uint64_t intervalIntendedUs = 0;
        bool intervalValid = false;
        // Metronome: the slot this frame occupies, committed as the schedule
        // basis only if the frame is actually presented so a dropped frame
        // frees its tick for the successor.
        bool hasSmoothedBasis = false;
        uint64_t smoothedBasisUsQ16 = 0;
        uint64_t smoothedBasisOrdinal = 0;
        int64_t phaseDebtUs = 0;
        int64_t phaseResidualEmaUs = 0;
        int64_t basisMappingUs = 0;
    };

    struct CadenceObservation {
        uint64_t intervalUs = 0;
        uint64_t frameDelta = 1;
        bool usedRtpTimestamp = false;
        bool eligible = false;
        bool sourceRateChanged = false;
        bool phaseDiscontinuity = false;
        bool needsRebase = false;
    };

    struct CadenceSample {
        uint64_t frameOrdinal = 0;
        uint64_t rtpTicks = 0;
    };

    struct PlayoutOffsetSample {
        uint64_t observationUs = 0;
        int64_t offsetUs = 0;
    };

    // Per-rate-band lateness reservoir and the delay learned from it.
    struct PlayoutBand {
        std::vector<uint64_t> latenessUs;
        size_t nextIndex = 0;
        uint64_t samplesSeen = 0;
        uint64_t lastUsedUs = 0;
        uint64_t appliedDelayUs = 0;
        bool applied = false;
    };

    bool timestampPlayoutEnabled() const;
    void resetPlayoutOffsets();
    int64_t observePlayoutOffset(uint64_t observationUs, int64_t offsetUs,
                                 bool cadenceEligible = true,
                                 bool phaseDiscontinuity = false);
    static uint64_t rtpTicksToUs(uint64_t ticks);
    void updatePlayoutDelay(const PacedFrame& frame,
                            const CadenceObservation& cadence,
                            bool rebased, int64_t readyOffsetUs,
                            uint64_t nowUs);
    uint64_t effectivePlayoutDelayUs() const;
    // Cadence smoothing: returns the signed adjustment to add to the raw
    // mapped slot for this frame, tracking the source period and pulling
    // toward the raw slot by the configured gain. Resets on discontinuities.
    int64_t cadenceSmoothingAdjustUs(const CadenceObservation& cadence,
                                     bool rebased, uint64_t rawBasisUs,
                                     uint64_t playoutDelayUs);
    // Metronome: the schedule advances from the last presented slot by the
    // fitted source period and corrects phase toward the raw slot by a
    // bounded step per frame. Returns the lag of that tick behind the raw
    // slot; a frame that arrived after its tick reports the whole periods
    // of shortfall in missedTicks and the caller slips it to "now".
    int64_t metronomeAdjustUs(const CadenceObservation& cadence,
                              bool rebased, uint64_t rawBasisUs,
                              uint64_t playoutDelayUs,
                              uint64_t earliestBasisUs,
                              uint64_t& missedTicks,
                              int64_t& remainingDebtUs);
    bool metronomeEnabled() const;
    uint64_t motionThresholdUs(uint64_t periodUs) const;
    void resetCadenceSmoothing();
    uint64_t playoutDelayStartUs() const;
    uint64_t playoutDelayMinimumUs() const;
    uint64_t playoutDelayMaximumUs() const;
    void updatePlayoutHistory(const PacedFrame& frame,
                              const CadenceObservation& cadence,
                              bool rebased, int64_t requiredUs);
    static uint64_t scaledPerMille(uint64_t value, uint64_t perMille);

    void clearTimeline(bool retainLearnedBudgets);
    void initializeTimeline(const PacedFrame& frame);
    CadenceObservation observeCadence(const PacedFrame& frame);
    void observeRtpCadence(uint32_t rtpDelta,
                           CadenceObservation& observation);
    void appendCadenceSample(std::deque<CadenceSample>& samples,
                             const CadenceSample& sample);
    uint64_t fittedSourcePeriodQ16(
        const std::deque<CadenceSample>& samples) const;
    uint64_t cadenceWindowUs() const;
    bool isMajorCadenceDeparture(uint64_t intervalUs,
                                 uint64_t frameDelta) const;
    bool acceptSourcePeriodQ16(uint64_t periodUsQ16);
    void anchorSourceTime(uint64_t sourceTimeUs);
    void updateLearnedBudgets();
    void updateReadinessModel();
    void applyReadinessBudget(bool acquireReserve,
                              bool immediateAcquisition = false);
    void clampReadinessReserveToPolicy();

    uint64_t pacingLatencyBudgetUs() const;
    bool pacingLatencyPolicyEnabled() const;
    uint64_t minimumReadinessReserveUs() const;
    uint64_t renderInsuranceCeilingUs() const;
    uint64_t renderInsuranceUs() const;
    uint64_t readinessReserveCeilingUs() const;
    uint64_t readinessPeriodFloorUs() const;
    size_t readinessLearningSampleLimit() const;
    uint64_t renderLeadFloorUs() const;
    uint64_t renderLeadCeilingUs() const;
    uint64_t readinessCeilingUs() const;
    uint64_t guardCeilingUs() const;
    uint64_t latchedPresentationHeadroomUs() const;
    bool rateProtectedPresentation() const;
    uint64_t latchedPresentationExitHeadroomUs() const;
    uint64_t scaledDisplayPeriodUs(uint64_t numerator,
                                   uint64_t denominator) const;
    static uint64_t periodForRate(int rateHz, uint64_t fallbackUs);
    static uint64_t periodForRateQ16(int rateHz, uint64_t fallbackQ16);
    static uint64_t saturatingAdd(uint64_t left, uint64_t right);
    static uint64_t addSigned(uint64_t value, int64_t adjustment);
    static int64_t signedDifference(uint64_t left, uint64_t right);
    static uint64_t roundedQ16(uint64_t valueQ16);
    static bool withinPercent(uint64_t value, uint64_t reference,
                              unsigned int percent);

    VrrSessionConfig m_Config;
    VrrTimingParameters m_Parameters;
    uint64_t m_ConfiguredStreamPeriodUs = 0;
    uint64_t m_ConfiguredStreamPeriodQ16 = 0;
    uint64_t m_DisplayPeriodUs = 0;
    uint64_t m_BaseGuardUs = 0;
    uint64_t m_GuardUs = 0;

    uint64_t m_SourcePeriodUs = 0;
    uint64_t m_SourcePeriodUsQ16 = 0;
    int64_t m_ReadinessBudgetUs = 0;
    int64_t m_ReadinessPhaseUs = 0;
    uint64_t m_ReadinessDemandUs = 0;
    uint64_t m_AppliedReadinessReserveUs = 0;
    bool m_ReadinessModelValid = false;
    uint64_t m_RenderBaselineUs = 0;
    uint64_t m_RenderLeadUs = 0;
    uint64_t m_RenderWakeLeadUs = 0;
    uint64_t m_TargetWakeLeadUs = 0;
    uint64_t m_GpuReadinessLeadUs = 0;
    struct GpuReadinessSample {
        uint64_t completionUs = 0;
        uint64_t waitUs = 0;
    };
    std::deque<GpuReadinessSample> m_GpuReadinessSamples;
    uint64_t m_LastGpuReadinessUpdateUs = 0;
    bool m_CanLatchPresentation = true;
    bool m_LatchedPresentation = false;
    size_t m_CadenceStabilityLatchFramesRemaining = 0;

    bool m_HaveTimeline = false;
    uint64_t m_SourceTimeUs = 0;
    uint64_t m_SourceTimeUsQ16 = 0;
    uint64_t m_SourceFrameOrdinal = 0;
    uint64_t m_UnwrappedRtpTicks = 0;
    int m_LastFrameNumber = -1;
    bool m_HaveLastFrameNumber = false;
    uint32_t m_LastRtpTimestamp = 0;
    bool m_LastTimestampValid = false;
    uint64_t m_RtpConversionRemainder = 0;
    uint64_t m_FrameConversionRemainder = 0;
    bool m_LastCadenceUsedRtp = false;

    bool m_HaveLastSubmission = false;
    uint64_t m_LastSubmissionUs = 0;
    unsigned int m_CleanSpacingFrames = 0;
    unsigned int m_PhaseErrorFrames = 0;

    std::deque<PlayoutOffsetSample> m_PlayoutOffsets;
    bool m_PlayoutOffsetValid = false;
    int64_t m_AppliedPlayoutOffsetUs = 0;
    uint64_t m_PlayoutSamplesSeen = 0;
    bool m_PlayoutOffsetClockValid = false;
    uint64_t m_LastPlayoutOffsetObservationUs = 0;
    uint64_t m_PlayoutOffsetSlewRemainder = 0;
    bool m_TimestampPlayoutActive = false;
    std::map<unsigned int, PlayoutBand> m_PlayoutBands;
    Vrr13::Reserve m_PlayoutHistory;
    Vrr13::WorkloadEpisode m_WorkloadEpisode;
    Vrr13::ReadinessPrediction m_ReadinessPrediction;
    Vrr13::ReadinessFeedback m_ReadinessFeedback;
    Vrr13::MeanMissBuffer m_MeanMissBuffer;
    Vrr13::IntervalBuffer m_IntervalBuffer;
    Vrr13::PresentationPrediction m_PresentationPrediction;
    Vrr13::SmoothnessFeedback m_SubmissionSmoothness, m_NativeSmoothness;
    // Lifetime counters for decoder-owned reporting windows. These do not
    // expire with the controller's rolling adaptation histogram.
    uint64_t m_NativeCadenceIntervals = 0, m_NativeCadenceHitches = 0;
    uint64_t m_EstimatedCadenceIntervals = 0, m_EstimatedCadenceHitches = 0;
    bool hasRecentNativeFeedback(uint64_t now) const;
    const Vrr13::SmoothnessFeedback& activeSmoothnessFeedback(uint64_t now) const;
    uint64_t m_RequestedPlayoutDelayUs = 0;
    uint64_t m_UnclampedRequestedPlayoutDelayUs = 0;
    bool m_FeedbackModeValid = false, m_FeedbackLatched = false;
    uint64_t m_LastHistoryArrivalUs = 0;
    unsigned int m_PlayoutBandIndex = 0;
    bool m_PlayoutBandValid = false;
    uint64_t m_AppliedPlayoutDelayUs = 0;
    bool m_AppliedPlayoutDelayValid = false;
    uint64_t m_LastDecodeCompleteUs = 0;
    bool m_HaveLastDecodeComplete = false;
    // Cadence smoothing state: the presented slot the schedule continues
    // from (target minus lead and safety) and the tracked source period.
    bool m_HaveSmoothedBasis = false;
    uint64_t m_LastSmoothedBasisUs = 0;
    uint64_t m_LastSmoothedBasisUsQ16 = 0;
    uint64_t m_LastSmoothedBasisOrdinal = 0;
    // Metronome phase state. The debt is how far the grid sits from where
    // it should be: positive after a late frame presented when ready or a
    // floor wait, negative when the clock mapping moved later underneath it.
    // It is paid back at the bounded step. The residual is the slow filtered
    // difference that remains once the debt is excluded, which tracks clock
    // drift and fit error without chasing per-frame stamp wobble.
    int64_t m_PhaseDebtUs = 0;
    int64_t m_PhaseResidualEmaUs = 0;
    // The clock mapping (applied offset plus playout delay) the committed
    // basis was placed against. Known movement of that mapping since then is
    // owed to the schedule exactly, so neither delay slew nor offset
    // tracking has to be rediscovered through the residual filter.
    int64_t m_LastBasisMappingUs = 0;
    // The metronome's tick period: the cumulative endpoint fit filtered over
    // many frames before the negotiated-rate floor is applied. Clamping each
    // noisy fit first biases the mean above the true period, and a tick that
    // runs a dozen microseconds slow per frame drifts visibly.
    uint64_t m_MetronomePeriodUsQ16 = 0;
    uint64_t m_SmoothedPeriodUs = 0;
    // Recent magnitudes of the stamp's deviation from the metronome grid
    // once known debt is excluded. Their upper percentile is the capture
    // jitter the grid absorbs; a deviation beyond it is motion timing the
    // stamp is reporting, and the frame presents on its stamp instead.
    std::deque<uint64_t> m_MotionResiduals;
    // Consecutive frames whose mapped source time sat more than a period in
    // the future; the offset is re-seeded only once enough agree.
    unsigned int m_FutureProjectionFrames = 0;
    // Frames still to exclude from the lateness reservoir after an arrival
    // stall: the backlog that the stall held up, not the link's jitter.
    uint64_t m_BurstExclusionFrames = 0;

    std::deque<CadenceSample> m_CadenceSamples;
    std::deque<CadenceSample> m_RateCandidateSamples;
    std::deque<int64_t> m_ReadyOffsets;
    std::deque<uint64_t> m_PreparationDurations;
    std::deque<uint64_t> m_RenderSchedulerDelays;
    std::deque<uint64_t> m_TargetSchedulerDelays;

    PendingFrame m_Pending;
};
