#include "vrrreplayconfig.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>

#include <cmath>
#include <limits>

namespace {

template<typename T>
bool readUnsigned(const QJsonValue& json, const QString& name, T& value,
                  QString& error)
{
    if (!json.isDouble() || !std::isfinite(json.toDouble()) ||
            json.toDouble() < 0 || std::floor(json.toDouble()) != json.toDouble() ||
            json.toDouble() > std::min(
                9007199254740991.0,
                static_cast<double>(std::numeric_limits<T>::max()))) {
        error = name + " must be a non-negative integer in range";
        return false;
    }
    value = static_cast<T>(json.toDouble());
    return true;
}

bool applyControllerObject(const QJsonObject& object,
                           VrrTimingParameters& value, QString& error)
{
    QSet<QString> known;
#define APPLY_FIELD(type, jsonName, memberName, defaultValue) \
    known.insert(QStringLiteral(#jsonName)); \
    if (object.contains(QStringLiteral(#jsonName)) && \
            !readUnsigned(object.value(QStringLiteral(#jsonName)), \
                          QStringLiteral("controller.") + \
                          QStringLiteral(#jsonName), \
                          value.memberName, error)) return false;
    VRR_TIMING_PARAMETER_FIELDS(APPLY_FIELD)
#undef APPLY_FIELD
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            error = "unknown controller parameter: " + it.key();
            return false;
        }
    }
    return validateVrrTimingParameters(value, error);
}

bool applyWorkerObject(const QJsonObject& object,
                       VrrReplayWorkerParameters& value, QString& error)
{
    const QSet<QString> known { "queue_capacity" };
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            error = "unknown worker parameter: " + it.key();
            return false;
        }
    }
    return (!object.contains("queue_capacity") ||
            readUnsigned(object.value("queue_capacity"),
                         "worker.queue_capacity", value.queueCapacity,
                         error)) && validateVrrWorkerParameters(value, error);
}

bool applyDisplayObject(const QJsonObject& object,
                        VrrReplayDisplayParameters& value, QString& error)
{
    const QSet<QString> known {
        "calibration_confirmed",
        "scanout_period_us",
        "scanout_period_ps",
        "active_scanout_us",
        "active_scanout_ps",
        "active_scanout_percent",
        "sync_to_active_scanout_us",
        "present_transport_us",
        "phase_uncertainty_us",
        "anchor_max_age_us",
    };
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            error = "unknown display parameter: " + it.key();
            return false;
        }
    }
#define READ_DISPLAY(name, member) \
    if (object.contains(name) && \
            !readUnsigned(object.value(name), "display." name, \
                          value.member, error)) return false
    READ_DISPLAY("calibration_confirmed", calibrationConfirmed);
    READ_DISPLAY("scanout_period_us", scanoutPeriodUs);
    READ_DISPLAY("scanout_period_ps", scanoutPeriodPs);
    READ_DISPLAY("active_scanout_us", activeScanoutUs);
    READ_DISPLAY("active_scanout_ps", activeScanoutPs);
    READ_DISPLAY("active_scanout_percent", activeScanoutPercent);
    READ_DISPLAY("sync_to_active_scanout_us", syncToActiveScanoutUs);
    READ_DISPLAY("present_transport_us", presentTransportUs);
    READ_DISPLAY("phase_uncertainty_us", phaseUncertaintyUs);
    READ_DISPLAY("anchor_max_age_us", anchorMaxAgeUs);
#undef READ_DISPLAY
    return validateVrrDisplayParameters(value, error);
}

bool applyExecutionObject(const QJsonObject& object,
                          VrrReplayExecutionParameters& value, QString& error)
{
    const QSet<QString> known {
        "decision_delay_us",
        "render_wake_delay_us",
        "target_wake_delay_us",
        "preparation_delay_us",
        "submission_delay_us",
        "display_transition_delay_us",
        "spacing_guard_feedback_us",
        "submission_advance_us",
        "remove_pre_present_raster_probe_overhead",
        "periodic_stall_every_frames",
        "periodic_stall_phase_frames",
        "periodic_stall_burst_frames",
        "periodic_stall_us",
        "periodic_render_wake_delay_us",
        "periodic_target_wake_delay_us",
        "periodic_preparation_stall_us",
        "periodic_submission_stall_us",
        "periodic_display_transition_delay_us",
        "periodic_spacing_guard_feedback_us",
        "periodic_submission_advance_us",
    };
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            error = "unknown execution parameter: " + it.key();
            return false;
        }
    }
#define READ_EXECUTION(name, member) \
    if (object.contains(name) && \
            !readUnsigned(object.value(name), "execution." name, \
                          value.member, error)) return false
    READ_EXECUTION("decision_delay_us", decisionDelayUs);
    READ_EXECUTION("render_wake_delay_us", renderWakeDelayUs);
    READ_EXECUTION("target_wake_delay_us", targetWakeDelayUs);
    READ_EXECUTION("preparation_delay_us", preparationDelayUs);
    READ_EXECUTION("submission_delay_us", submissionDelayUs);
    READ_EXECUTION("display_transition_delay_us",
                   displayTransitionDelayUs);
    READ_EXECUTION("spacing_guard_feedback_us", spacingGuardFeedbackUs);
    READ_EXECUTION("submission_advance_us", submissionAdvanceUs);
    READ_EXECUTION("remove_pre_present_raster_probe_overhead",
                   removePrePresentRasterProbeOverhead);
    READ_EXECUTION("periodic_stall_every_frames", periodicStallEveryFrames);
    READ_EXECUTION("periodic_stall_phase_frames",
                   periodicStallPhaseFrames);
    READ_EXECUTION("periodic_stall_burst_frames",
                   periodicStallBurstFrames);
    READ_EXECUTION("periodic_stall_us", periodicStallUs);
    READ_EXECUTION("periodic_render_wake_delay_us",
                   periodicRenderWakeDelayUs);
    READ_EXECUTION("periodic_target_wake_delay_us",
                   periodicTargetWakeDelayUs);
    READ_EXECUTION("periodic_preparation_stall_us",
                   periodicPreparationStallUs);
    READ_EXECUTION("periodic_submission_stall_us",
                   periodicSubmissionStallUs);
    READ_EXECUTION("periodic_display_transition_delay_us",
                   periodicDisplayTransitionDelayUs);
    READ_EXECUTION("periodic_spacing_guard_feedback_us",
                   periodicSpacingGuardFeedbackUs);
    READ_EXECUTION("periodic_submission_advance_us",
                   periodicSubmissionAdvanceUs);
#undef READ_EXECUTION
    return validateVrrExecutionParameters(value, error);
}

bool applyParametersObject(const QJsonObject& object,
                           VrrTimingParameters& controller,
                           VrrReplayWorkerParameters& worker,
                           VrrReplayDisplayParameters& display,
                           VrrReplayExecutionParameters& execution,
                           QString& error)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.key() != "controller" && it.key() != "worker" &&
                it.key() != "display" && it.key() != "execution") {
            error = "unknown parameter section: " + it.key();
            return false;
        }
        if (!it.value().isObject()) {
            error = it.key() + " parameter section must be an object";
            return false;
        }
    }
    return (!object.contains("controller") ||
            applyControllerObject(object.value("controller").toObject(),
                                  controller, error)) &&
        (!object.contains("worker") ||
         applyWorkerObject(object.value("worker").toObject(), worker, error)) &&
        (!object.contains("display") ||
         applyDisplayObject(object.value("display").toObject(),
                            display, error)) &&
        (!object.contains("execution") ||
         applyExecutionObject(object.value("execution").toObject(),
                              execution, error));
}

} // namespace

QJsonObject vrrTimingParametersToJson(const VrrTimingParameters& value)
{
    QJsonObject object;
#define WRITE_FIELD(type, jsonName, memberName, defaultValue) \
    object[QStringLiteral(#jsonName)] = static_cast<double>(value.memberName);
    VRR_TIMING_PARAMETER_FIELDS(WRITE_FIELD)
#undef WRITE_FIELD
    return object;
}

QJsonObject vrrWorkerParametersToJson(const VrrReplayWorkerParameters& value)
{
    QJsonObject object;
    object["queue_capacity"] = static_cast<double>(value.queueCapacity);
    return object;
}

QJsonObject vrrDisplayParametersToJson(
    const VrrReplayDisplayParameters& value)
{
    QJsonObject object;
    object["calibration_confirmed"] =
        static_cast<double>(value.calibrationConfirmed);
    object["scanout_period_us"] = static_cast<double>(value.scanoutPeriodUs);
    object["scanout_period_ps"] = static_cast<double>(value.scanoutPeriodPs);
    object["active_scanout_us"] = static_cast<double>(value.activeScanoutUs);
    object["active_scanout_ps"] = static_cast<double>(value.activeScanoutPs);
    object["active_scanout_percent"] =
        static_cast<double>(value.activeScanoutPercent);
    object["sync_to_active_scanout_us"] =
        static_cast<double>(value.syncToActiveScanoutUs);
    object["present_transport_us"] =
        static_cast<double>(value.presentTransportUs);
    object["phase_uncertainty_us"] =
        static_cast<double>(value.phaseUncertaintyUs);
    object["anchor_max_age_us"] =
        static_cast<double>(value.anchorMaxAgeUs);
    return object;
}

QJsonObject vrrExecutionParametersToJson(
    const VrrReplayExecutionParameters& value)
{
    QJsonObject object;
    object["decision_delay_us"] =
        static_cast<double>(value.decisionDelayUs);
    object["render_wake_delay_us"] =
        static_cast<double>(value.renderWakeDelayUs);
    object["target_wake_delay_us"] =
        static_cast<double>(value.targetWakeDelayUs);
    object["preparation_delay_us"] =
        static_cast<double>(value.preparationDelayUs);
    object["submission_delay_us"] =
        static_cast<double>(value.submissionDelayUs);
    object["display_transition_delay_us"] =
        static_cast<double>(value.displayTransitionDelayUs);
    object["spacing_guard_feedback_us"] =
        static_cast<double>(value.spacingGuardFeedbackUs);
    object["submission_advance_us"] =
        static_cast<double>(value.submissionAdvanceUs);
    object["remove_pre_present_raster_probe_overhead"] =
        static_cast<double>(
            value.removePrePresentRasterProbeOverhead);
    object["periodic_stall_every_frames"] =
        static_cast<double>(value.periodicStallEveryFrames);
    object["periodic_stall_phase_frames"] =
        static_cast<double>(value.periodicStallPhaseFrames);
    object["periodic_stall_burst_frames"] =
        static_cast<double>(value.periodicStallBurstFrames);
    object["periodic_stall_us"] =
        static_cast<double>(value.periodicStallUs);
    object["periodic_render_wake_delay_us"] =
        static_cast<double>(value.periodicRenderWakeDelayUs);
    object["periodic_target_wake_delay_us"] =
        static_cast<double>(value.periodicTargetWakeDelayUs);
    object["periodic_preparation_stall_us"] =
        static_cast<double>(value.periodicPreparationStallUs);
    object["periodic_submission_stall_us"] =
        static_cast<double>(value.periodicSubmissionStallUs);
    object["periodic_display_transition_delay_us"] =
        static_cast<double>(value.periodicDisplayTransitionDelayUs);
    object["periodic_spacing_guard_feedback_us"] =
        static_cast<double>(value.periodicSpacingGuardFeedbackUs);
    object["periodic_submission_advance_us"] =
        static_cast<double>(value.periodicSubmissionAdvanceUs);
    return object;
}

QJsonObject vrrDefaultReplayConfigurationJson()
{
    QJsonObject parameters;
    parameters["controller"] = vrrTimingParametersToJson({});
    parameters["worker"] = vrrWorkerParametersToJson({});
    parameters["display"] = vrrDisplayParametersToJson({});
    parameters["execution"] = vrrExecutionParametersToJson({});
    QJsonObject scenario;
    scenario["name"] = "candidate";
    scenario["mode"] = "fixed";
    QJsonObject root;
    root["config_schema"] = 1;
    root["parameters"] = parameters;
    root["scenarios"] = QJsonArray { scenario };
    return root;
}

QStringList vrrReplayParameterNames()
{
    QStringList names;
#define ADD_NAME(type, jsonName, memberName, defaultValue) \
    names.append("controller." #jsonName);
    VRR_TIMING_PARAMETER_FIELDS(ADD_NAME)
#undef ADD_NAME
    names << "worker.queue_capacity"
          << "display.calibration_confirmed"
          << "display.scanout_period_us"
          << "display.scanout_period_ps"
          << "display.active_scanout_us"
          << "display.active_scanout_ps"
          << "display.active_scanout_percent"
          << "display.sync_to_active_scanout_us"
          << "display.present_transport_us"
          << "display.phase_uncertainty_us"
          << "display.anchor_max_age_us"
          << "execution.decision_delay_us"
          << "execution.render_wake_delay_us"
          << "execution.target_wake_delay_us"
          << "execution.preparation_delay_us"
          << "execution.submission_delay_us"
          << "execution.display_transition_delay_us"
          << "execution.spacing_guard_feedback_us"
          << "execution.submission_advance_us"
          << "execution.remove_pre_present_raster_probe_overhead"
          << "execution.periodic_stall_every_frames"
          << "execution.periodic_stall_phase_frames"
          << "execution.periodic_stall_burst_frames"
          << "execution.periodic_stall_us"
          << "execution.periodic_render_wake_delay_us"
          << "execution.periodic_target_wake_delay_us"
          << "execution.periodic_preparation_stall_us"
          << "execution.periodic_submission_stall_us"
          << "execution.periodic_display_transition_delay_us"
          << "execution.periodic_spacing_guard_feedback_us"
          << "execution.periodic_submission_advance_us";
    return names;
}

bool validateVrrTimingParameters(const VrrTimingParameters& value,
                                 QString& error)
{
    const auto fail = [&error](const char* text) { error = text; return false; };
    if (value.playoutPredictionOnly > 1 || value.playoutSubmissionEstimateFallback > 1 ||
            value.playoutRequireDisplayEvents > 1 ||
            value.playoutNativeHitchAdaptation > 1 || value.playoutReadinessDrivenAdaptation > 1 ||
            value.playoutStableSmoothnessReference > 1 ||
            value.renderStartPreserveLearnedLead > 1 ||
            value.playoutDelayCapUsesObservedPeriod > 1 ||
            value.playoutCapacityTelemetry > 1 ||
            value.playoutGpuReadinessAdaptation > 1) {
        return fail("prediction-only, display event requirement, native hitch adaptation, readiness adaptation, stable smoothness reference, learned preparation lead, observed-period cap, capacity telemetry, and GPU readiness flags must be 0 or 1");
    }
    if (value.baseGuardDivisor == 0 ||
            value.pacingLatencyExtraPeriodDenominator == 0 ||
            value.majorCadenceRatioDenominator == 0 ||
            value.candidateCadenceRatioDenominator == 0 ||
            value.readinessAttackDenominator == 0 ||
            value.readinessReleaseDenominator == 0 ||
            value.readinessPeriodFloorDenominator == 0 ||
            value.usableHeadroomDenominator == 0 ||
            value.latchedPresentationHeadroomPeriodDenominator == 0 ||
            value.latchedPresentationExitHeadroomPeriodDenominator == 0) {
        return fail("parameter denominators must be non-zero");
    }
    if ((value.renderLeadCeilingUs != 0 &&
         value.renderLeadFloorUs > value.renderLeadCeilingUs) ||
            value.minimumGuardUs > value.maximumBaseGuardUs ||
            value.maximumBaseGuardUs > value.maximumAdaptiveGuardUs ||
            value.minimumReadinessReserveUs > value.readinessCeilingUs ||
            value.latchedPresentationHeadroomUs >
                value.latchedPresentationExitHeadroomUs ||
            static_cast<long double>(
                value.latchedPresentationHeadroomPeriodNumerator) /
                    static_cast<long double>(
                        value.latchedPresentationHeadroomPeriodDenominator) >
                static_cast<long double>(
                    value.latchedPresentationExitHeadroomPeriodNumerator) /
                    static_cast<long double>(
                        value.latchedPresentationExitHeadroomPeriodDenominator) ||
            value.looseCadenceWindowUs > value.tightCadenceWindowUs) {
        return fail("parameter floors, ceilings, or hysteresis are inverted");
    }
    if (value.guardDecayFrames == 0 || value.schedulerLearningSamples == 0 ||
            value.readinessLearningSamples == 0 ||
            value.preparationLearningSamples == 0 ||
            value.minimumReadinessSamples == 0 ||
            value.minimumCadenceSamples < 2 ||
            value.maximumCadenceSamples < value.minimumCadenceSamples ||
            value.rateCandidateSamples < 2 || value.phaseErrorFrames == 0) {
        return fail("sample counts and frame thresholds must be consistent and non-zero");
    }
    if (value.latchedPresentationBaseGuardExit > 1) {
        return fail("latch_base_guard_exit must be 0 or 1");
    }
    if (value.retainReadinessOnPhaseReset > 1) {
        return fail("retain_readiness_on_phase_reset must be 0 or 1");
    }
    if (value.timestampPlayoutEnabled > 1) {
        return fail("timestamp_playout_enabled must be 0 or 1");
    }
    if (value.playoutOffsetWindowUs == 0) {
        return fail("playout_offset_window_us must be non-zero");
    }
    if (value.playoutOffsetCadenceGate > 1) {
        return fail("playout_offset_cadence_gate must be 0 or 1");
    }
    if (value.playoutOffsetSourceClock > 1) {
        return fail("playout_offset_source_clock must be 0 or 1");
    }
    if (value.playoutOffsetSlewUsPerSecond > 1000000 ||
        value.playoutOffsetMaximumStepUs == 0 ||
        value.playoutOffsetMaximumStepUs > 1000000) {
        return fail("offset slew rate must be in 0..1000000 and maximum step in 1..1000000");
    }
    if (value.playoutDelayAdaptive > 1) {
        return fail("playout_delay_adaptive must be 0 or 1");
    }
    if (value.playoutAdaptiveOnly > 1) {
        return fail("playout_adaptive_only must be 0 or 1");
    }
    if (value.latencyFixEnabled > 1 || value.latencyFixAllRates > 1 || value.latencyFixDelayPeriodPerMille > 1000) {
        return fail("latency_fix_enabled and latency_fix_all_rates must be 0 or 1 and latency_fix_delay_period_per_mille must be in 0..1000");
    }
    if (value.playoutDelayCapSourcePeriodPerMille > 4000) {
        return fail("playout_delay_cap_source_period_per_mille must be in 0..4000");
    }
    if (value.playoutGpuReadinessWindowUs == 0 ||
            value.playoutGpuReadinessMaximumUs == 0 ||
            value.playoutGpuReadinessPercentile > 100) {
        return fail("GPU readiness window and ceiling must be non-zero and its percentile must be in 0..100");
    }
    if (value.latencyFixAllRates && !value.latencyFixEnabled) {
        return fail("latency_fix_all_rates requires latency_fix_enabled");
    }
    if (value.latencyFixEnabled && (!value.timestampPlayoutEnabled ||
                                    !value.playoutDelayAdaptive || !value.playoutHistoryEnabled)) {
        return fail("latency_fix_enabled requires adaptive timestamp history playout");
    }
    if (value.playoutRateProtectionEnabled > 1) {
        return fail("playout_rate_protection_enabled must be 0 or 1");
    }
    if (value.playoutPerFrameLatch > 2) {
        return fail("playout_per_frame_latch must be 0, 1 or 2");
    }
    if (value.playoutSmoothnessFeedbackEnabled > 1 || value.playoutHistoryEnabled > 1 || value.playoutPredictionEnabled > 1) {
        return fail("history, prediction and smoothness feedback flags must be 0 or 1");
    }
    if (value.playoutSmoothnessFeedbackEnabled && !value.playoutPredictionEnabled) {
        return fail("playout_smoothness_feedback_enabled requires playout_prediction_enabled");
    }
    if (value.playoutReadinessDrivenAdaptation && !value.playoutPredictionEnabled) {
        return fail("playout_readiness_driven_adaptation requires playout_prediction_enabled");
    }
    if (value.playoutPredictionOnly &&
            (!value.playoutReadinessDrivenAdaptation || value.playoutNativeHitchAdaptation)) {
        return fail("playout_prediction_only requires readiness adaptation without native hitch adaptation");
    }
    if (value.playoutMeanMissHoldUs < 1000000 || value.playoutMeanMissHoldUs > 60000000 ||
            value.playoutMeanMissReleaseUsPerSecond > 1000) {
        error = QStringLiteral("Mean-miss hold must be 1-60 seconds and release at most 1000 us per second");
        return false;
    }
    if (value.playoutResponsiveBuffer > 8 || (value.playoutResponsiveBuffer &&
            (!value.playoutPredictionOnly || value.playoutReadinessHitchThresholdUs))) {
        return fail("playout_responsive_buffer requires prediction-only playout without historical hitch feedback");
    }
    if (value.playoutOnTimeTargetPerMillion < 900000 || value.playoutOnTimeTargetPerMillion > 1000000)
        return fail("playout_on_time_target_per_million must be in 900000..1000000");
    if (value.playoutReadinessWindowUs < 3000000 || value.playoutReadinessWindowUs > 300000000 ||
            value.playoutReadinessWindowUs % 100000 != 0)
        return fail("playout_readiness_window_us must be a multiple of 100000 in 3000000..300000000");
    if (value.playoutReadinessHitchThresholdUs &&
            (!value.playoutPredictionOnly || value.playoutReadinessHitchThresholdUs > 10000)) {
        return fail("playout_readiness_hitch_threshold_us requires prediction-only playout and must be in 1..10000");
    }
    if (value.playoutNativeHitchAdaptation &&
            (!value.playoutSmoothnessFeedbackEnabled || !value.playoutReadinessDrivenAdaptation)) {
        return fail("playout_native_hitch_adaptation requires smoothness feedback and readiness prediction");
    }
    if (value.playoutPredictionEnabled && (!value.playoutHistoryEnabled || !value.timestampPlayoutEnabled || !value.playoutDelayAdaptive)) {
        return fail("playout_prediction_enabled requires adaptive timestamp history playout");
    }
    if (value.playoutDelayMinimumUs > value.playoutDelayMaximumUs) {
        return fail("playout_delay_minimum_us must not exceed playout_delay_maximum_us");
    }
    if (value.playoutDelayPercentilePerMille > 1000) {
        return fail("playout_delay_percentile_per_mille must be in 0..1000");
    }
    if (value.playoutSmoothingGainPerMille > 1000 ||
            value.playoutSmoothingPeriodAlphaPerMille > 1000) {
        return fail("playout_smoothing gain and period alpha must be in 0..1000");
    }
    if (value.playoutDelayMinimumSamples == 0 ||
            value.playoutDelayReservoirSamples == 0 ||
            value.playoutBandWidthHz == 0 ||
            value.playoutStallExclusionUs == 0) {
        return fail("playout delay sample counts, band width and stall exclusion must be non-zero");
    }
    if (value.playoutMotionDeadbandEnabled > 1 ||
            value.playoutDelaySlewAcrossBands > 1 ||
            value.playoutPrepareOnArrival > 1) {
        return fail("playout_motion_deadband_enabled, playout_delay_slew_across_bands and playout_prepare_on_arrival must be 0 or 1");
    }
    if (value.playoutMotionWindowFrames == 0 ||
            value.playoutMotionMinimumSamples == 0 ||
            value.playoutMotionCeilingPeriodPerMille > 1000) {
        return fail("playout motion window and minimum samples must be non-zero and the ceiling in 0..1000 per mille");
    }
    const unsigned int percents[] = {
        value.materialRateChangePercent, value.renderBaselinePercentile,
        value.preparationPercentile,
        value.schedulerPercentile, value.readinessLowPercentile,
        value.readinessTightPercentile, value.readinessLoosePercentile,
        value.playoutMotionPercentile,
    };
    for (unsigned int percent : percents) {
        if (percent > 100) return fail("percentiles and percentages must be in 0..100");
    }
    if (value.readinessLowPercentile > value.readinessLoosePercentile ||
            value.readinessLoosePercentile > value.readinessTightPercentile) {
        return fail("readiness percentiles must be low <= loose <= tight");
    }
    if (value.renderBaselinePercentile > value.preparationPercentile) {
        return fail("render percentiles must be baseline <= preparation");
    }
    return true;
}

bool validateVrrWorkerParameters(const VrrReplayWorkerParameters& value,
                                 QString& error)
{
    if (value.queueCapacity == 0) {
        error = "worker parameters must be non-zero";
        return false;
    }
    return true;
}

bool validateVrrDisplayParameters(const VrrReplayDisplayParameters& value,
                                  QString& error)
{
    if (value.calibrationConfirmed > 1) {
        error = "display.calibration_confirmed must be 0 or 1";
        return false;
    }
    if (value.calibrationConfirmed != 0 &&
            ((value.scanoutPeriodUs == 0 &&
              value.scanoutPeriodPs == 0) ||
             (value.activeScanoutUs == 0 &&
              value.activeScanoutPs == 0))) {
        error = "display calibration requires an explicit scanout period and active scanout duration in microseconds or picoseconds";
        return false;
    }
    if (value.activeScanoutPercent == 0 ||
            value.activeScanoutPercent > 100) {
        error = "display.active_scanout_percent must be in 1..100";
        return false;
    }
    if (value.scanoutPeriodUs != 0 && value.activeScanoutUs >
            value.scanoutPeriodUs) {
        error = "display.active_scanout_us must not exceed scanout_period_us";
        return false;
    }
    if (value.scanoutPeriodPs != 0 && value.scanoutPeriodUs != 0) {
        const uint64_t roundedPeriodUs =
            value.scanoutPeriodPs / 1000000ULL +
            (value.scanoutPeriodPs % 1000000ULL >= 500000ULL ?
                1ULL : 0ULL);
        const uint64_t differenceUs =
            roundedPeriodUs >= value.scanoutPeriodUs ?
                roundedPeriodUs - value.scanoutPeriodUs :
                value.scanoutPeriodUs - roundedPeriodUs;
        if (differenceUs > 1) {
            error = "display.scanout_period_ps must agree with scanout_period_us within 1 us";
            return false;
        }
    }
    if (value.scanoutPeriodPs != 0 &&
            (value.activeScanoutUs != 0 ||
             value.activeScanoutPs != 0)) {
        constexpr uint64_t picosecondsPerMicrosecond = 1000000ULL;
        const uint64_t maximumMicroseconds =
            std::numeric_limits<uint64_t>::max() /
                picosecondsPerMicrosecond;
        uint64_t resolvedActiveScanoutPs = value.activeScanoutPs;
        if (resolvedActiveScanoutPs == 0) {
            if (value.activeScanoutUs > maximumMicroseconds) {
                error = "display.active_scanout_us is too large for picosecond simulation";
                return false;
            }
            resolvedActiveScanoutPs =
                value.activeScanoutUs * picosecondsPerMicrosecond;
        }
        uint64_t syncToActiveScanoutPs = 0;
        if (value.activeScanoutUs > maximumMicroseconds ||
                value.syncToActiveScanoutUs > maximumMicroseconds ||
                resolvedActiveScanoutPs > value.scanoutPeriodPs) {
            error = "display active scanout plus sync_to_active_scanout_us must fit within scanout_period_ps";
            return false;
        }
        syncToActiveScanoutPs =
            value.syncToActiveScanoutUs * picosecondsPerMicrosecond;
    }
    if (value.activeScanoutPs != 0 && value.activeScanoutUs != 0) {
        const uint64_t roundedActiveScanoutUs =
            value.activeScanoutPs / 1000000ULL +
            (value.activeScanoutPs % 1000000ULL >= 500000ULL ?
                1ULL : 0ULL);
        const uint64_t differenceUs =
            roundedActiveScanoutUs >= value.activeScanoutUs ?
                roundedActiveScanoutUs - value.activeScanoutUs :
                value.activeScanoutUs - roundedActiveScanoutUs;
        if (differenceUs > 1) {
            error = "display.active_scanout_ps must agree with active_scanout_us within 1 us";
            return false;
        }
    }
    if (value.scanoutPeriodUs != 0 &&
            value.syncToActiveScanoutUs >= value.scanoutPeriodUs) {
        error = "display.sync_to_active_scanout_us must be less than scanout_period_us";
        return false;
    }
    if (value.scanoutPeriodUs != 0 &&
            value.phaseUncertaintyUs > value.scanoutPeriodUs / 2) {
        error = "display.phase_uncertainty_us must not exceed half scanout_period_us";
        return false;
    }
    return true;
}

bool validateVrrExecutionParameters(
    const VrrReplayExecutionParameters& value, QString& error)
{
    if (value.removePrePresentRasterProbeOverhead > 1) {
        error =
            "execution.remove_pre_present_raster_probe_overhead must be 0 or 1";
        return false;
    }
    if (value.periodicStallBurstFrames == 0) {
        error = "execution.periodic_stall_burst_frames must be non-zero";
        return false;
    }
    if (value.periodicStallEveryFrames != 0 &&
            (value.periodicStallPhaseFrames >=
                 value.periodicStallEveryFrames ||
             value.periodicStallBurstFrames >
                 value.periodicStallEveryFrames)) {
        error = "execution periodic phase must be below the period and burst must not exceed it";
        return false;
    }
    return true;
}

bool loadVrrReplayConfiguration(const QByteArray& json,
                                VrrReplayConfiguration& configuration,
                                QString& error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) {
        error = "invalid replay configuration: " + parseError.errorString();
        return false;
    }
    const QJsonObject root = document.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key() != "config_schema" && it.key() != "parameters" &&
                it.key() != "scenarios") {
            error = "unknown replay configuration key: " + it.key();
            return false;
        }
    }
    if (root.value("config_schema").toInt(-1) != 1) {
        error = "unsupported replay config_schema (expected 1)";
        return false;
    }
    configuration = VrrReplayConfiguration {};
    if (root.contains("parameters")) {
        const QJsonObject parameterObject =
            root.value("parameters").toObject();
        if (!root.value("parameters").isObject() ||
                !applyParametersObject(parameterObject,
                                       configuration.commonController,
                                       configuration.commonWorker,
                                       configuration.commonDisplay,
                                       configuration.commonExecution,
                                       error)) {
            return false;
        }
        configuration.commonControllerCustomized =
            parameterObject.value("controller").isObject() &&
            !parameterObject.value("controller").toObject().isEmpty();
    }
    const QJsonValue scenariosValue = root.value("scenarios");
    if (!scenariosValue.isArray() || scenariosValue.toArray().isEmpty()) {
        error = "scenarios must be a non-empty array";
        return false;
    }
    QSet<QString> names;
    for (const QJsonValue& item : scenariosValue.toArray()) {
        if (!item.isObject()) { error = "each scenario must be an object"; return false; }
        const QJsonObject object = item.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key() != "name" && it.key() != "mode" &&
                    it.key() != "parameters" && it.key() != "assertions") {
                error = "unknown scenario key: " + it.key(); return false;
            }
        }
        VrrReplayScenario scenario;
        scenario.controller = configuration.commonController;
        scenario.controllerCustomized =
            configuration.commonControllerCustomized;
        scenario.worker = configuration.commonWorker;
        scenario.display = configuration.commonDisplay;
        scenario.execution = configuration.commonExecution;
        scenario.name = object.value("name").toString();
        scenario.mode = object.value("mode").toString("fixed");
        if (scenario.name.isEmpty() || names.contains(scenario.name)) {
            error = "scenario names must be non-empty and unique"; return false;
        }
        names.insert(scenario.name);
        if (scenario.mode != "fixed" && scenario.mode != "worker") {
            error = "scenario mode must be fixed or worker"; return false;
        }
        if (object.contains("parameters")) {
            const QJsonObject parameterObject =
                object.value("parameters").toObject();
            if (!object.value("parameters").isObject() ||
                 !applyParametersObject(parameterObject,
                                        scenario.controller, scenario.worker,
                                        scenario.display, scenario.execution,
                                        error)) return false;
            scenario.controllerCustomized =
                scenario.controllerCustomized ||
                (parameterObject.value("controller").isObject() &&
                 !parameterObject.value("controller").toObject().isEmpty());
        }
        if (object.contains("assertions")) {
            if (!object.value("assertions").isArray()) {
                error = "scenario assertions must be an array"; return false;
            }
            for (const QJsonValue& assertionValue :
                 object.value("assertions").toArray()) {
                if (!assertionValue.isObject()) {
                    error = "each scenario assertion must be an object";
                    return false;
                }
                const QJsonObject assertionObject = assertionValue.toObject();
                const QSet<QString> assertionKeys {
                    "metric", "operator", "value",
                };
                for (auto it = assertionObject.constBegin();
                        it != assertionObject.constEnd(); ++it) {
                    if (!assertionKeys.contains(it.key())) {
                        error = "unknown scenario assertion key: " + it.key();
                        return false;
                    }
                }
                VrrReplayAssertion assertion;
                assertion.metric = assertionObject.value("metric").toString();
                assertion.operation = assertionObject.value("operator").toString();
                assertion.value = assertionObject.value("value").toDouble(
                    std::numeric_limits<double>::quiet_NaN());
                if (assertion.metric.isEmpty() ||
                        !QStringList { "<", "<=", "==", ">=", ">" }
                            .contains(assertion.operation) ||
                        !std::isfinite(assertion.value)) {
                    error = "invalid scenario assertion"; return false;
                }
                scenario.assertions.append(assertion);
            }
        }
        configuration.scenarios.append(scenario);
    }
    return true;
}

bool applyVrrReplayOverride(const QString& expression,
                            VrrReplayScenario& scenario, QString& error)
{
    const int equals = expression.indexOf('=');
    if (equals <= 0 || equals == expression.size() - 1) {
        error = "override must be section.name=value: " + expression;
        return false;
    }
    const QString path = expression.left(equals);
    bool ok = false;
    const qulonglong number = expression.mid(equals + 1).toULongLong(&ok);
    if (!ok || number > 9007199254740991ULL) {
        error = "override value must be an exact unsigned JSON integer: " + expression;
        return false;
    }
    QJsonObject section;
    section[path.section('.', 1, 1)] = static_cast<double>(number);
    if (path.startsWith("controller.")) {
        if (!applyControllerObject(section, scenario.controller, error)) {
            return false;
        }
        scenario.controllerCustomized = true;
        return true;
    }
    if (path.startsWith("worker.")) {
        return applyWorkerObject(section, scenario.worker, error);
    }
    if (path.startsWith("display.")) {
        return applyDisplayObject(section, scenario.display, error);
    }
    if (path.startsWith("execution.")) {
        return applyExecutionObject(section, scenario.execution, error);
    }
    error = "override must start with controller., worker., display., or execution.: " +
        expression;
    return false;
}

bool applyVrrReplayControllerSnapshot(const QJsonObject& object,
                                      VrrTimingParameters& parameters,
                                      QString& error)
{
    // Captured parameters are one coherent controller snapshot. Applying
    // interdependent fields one at a time can temporarily invert a valid
    // floor/ceiling or hysteresis pair against the current defaults.
    VrrTimingParameters candidate = parameters;
    if (!applyControllerObject(object, candidate, error)) {
        return false;
    }
    parameters = candidate;
    return true;
}
