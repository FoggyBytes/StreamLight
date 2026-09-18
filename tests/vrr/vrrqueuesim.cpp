#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrtimingcontroller.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrframedroppolicy.h"
#include "../../app/streaming/vrrratepolicy.h"
#include "vrrreplayconfig.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace {

struct InputFrame {
    uint64_t sequence = 0;
    int number = 0;
    uint32_t rtp = 0;
    bool rtpValid = false;
    uint64_t decodeUs = 0;
    uint64_t arrivalUs = 0;
    uint64_t sourcePeriodUs = 0;
};

struct Distribution {
    std::vector<uint64_t> values;

    uint64_t percentile(unsigned int perMille) const
    {
        if (values.empty()) return 0;
        std::vector<uint64_t> ordered = values;
        std::sort(ordered.begin(), ordered.end());
        const size_t rank = std::max<size_t>(
            1, (ordered.size() * std::min(1000U, perMille) + 999) / 1000);
        return ordered[rank - 1];
    }

    QJsonObject json() const
    {
        QJsonObject result;
        result["count"] = static_cast<qint64>(values.size());
        long double total = 0;
        for (uint64_t sample : values) total += sample;
        result["mean"] = values.empty() ? 0.0 :
            static_cast<double>(total / values.size());
        result["p50"] = static_cast<qint64>(percentile(500));
        result["p95"] = static_cast<qint64>(percentile(950));
        result["p99"] = static_cast<qint64>(percentile(990));
        result["max"] = static_cast<qint64>(
            values.empty() ? 0 : *std::max_element(values.begin(), values.end()));
        return result;
    }
};

struct Capture {
    VrrSessionConfig session;
    bool canLatchPresentation = false;
    std::vector<InputFrame> frames;
    std::vector<uint64_t> preparationUs;
    std::vector<uint64_t> acquisitionUs;
    std::vector<uint64_t> presentCallUs;
    uint64_t decisionCallUs = 0;
    uint64_t missingArrivalSequences = 0;
};

uint64_t value(const QList<QByteArray>& fields,
               const QMap<QByteArray, int>& columns,
               const QByteArray& name)
{
    const int column = columns.value(name, -1);
    return column >= 0 && column < fields.size() ?
        fields[column].toULongLong() : 0;
}

bool loadCapture(const QString& path, Capture& capture, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    const QList<QByteArray> header = file.readLine().trimmed().split(',');
    QMap<QByteArray, int> columns;
    for (int i = 0; i < header.size(); ++i) columns.insert(header[i], i);
    const QList<QByteArray> required {
        "arrival_sequence", "frame", "rtp_timestamp", "rtp_valid",
        "decode_complete_us", "pacer_arrival_us", "display_refresh_hz",
        "stream_rate_hz", "additional_queued_frame", "prepare_us",
        "present_call_us", "controller_call_us"
    };
    for (const QByteArray& name : required) {
        if (!columns.contains(name)) {
            error = "missing CSV column: " + QString::fromLatin1(name);
            return false;
        }
    }

    Distribution decisionCalls;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QList<QByteArray> fields = line.split(',');
        if (fields.size() != header.size()) {
            error = "malformed CSV row";
            return false;
        }
        InputFrame frame;
        frame.sequence = value(fields, columns, "arrival_sequence");
        frame.number = fields[columns["frame"]].toInt();
        frame.rtp = static_cast<uint32_t>(value(fields, columns, "rtp_timestamp"));
        frame.rtpValid = value(fields, columns, "rtp_valid") != 0;
        frame.decodeUs = value(fields, columns, "decode_complete_us");
        frame.arrivalUs = value(fields, columns, "pacer_arrival_us");
        frame.sourcePeriodUs = value(fields, columns, "source_period_us");
        capture.frames.push_back(frame);

        if (capture.session.displayRefreshHz == 0) {
            capture.canLatchPresentation = value(fields, columns, "can_latch_present") != 0;
            capture.session.displayRefreshHz = static_cast<int>(
                value(fields, columns, "display_refresh_hz"));
            capture.session.streamRateHz = static_cast<int>(
                value(fields, columns, "stream_rate_hz"));
            capture.session.allowAdditionalQueuedFrame =
                value(fields, columns, "additional_queued_frame") != 0;
            const bool allRates = value(fields, columns, "param_latency_fix_all_rates") != 0;
            capture.session.latencyFix = value(fields, columns, "param_latency_fix_enabled") != 0 && !allRates;
            capture.session.latencyMode = allRates ?
                (value(fields, columns, "param_latency_fix_delay_period_per_mille") == 0 ? 2 : 1) : 0;
            capture.session.readinessHitchFeedback = false; // Shared current policy.

        }
        const uint64_t preparation = value(fields, columns, "prepare_us");
        if (preparation != 0) {
            capture.preparationUs.push_back(preparation);
            capture.acquisitionUs.push_back(value(fields, columns, "prepare_acquire_us"));
        }
        const uint64_t presentCall = value(fields, columns, "present_call_us");
        if (presentCall != 0) capture.presentCallUs.push_back(presentCall);
        const uint64_t decisionCall = value(fields, columns, "controller_call_us");
        if (decisionCall != 0) decisionCalls.values.push_back(decisionCall);
    }
    std::sort(capture.frames.begin(), capture.frames.end(),
              [](const InputFrame& left, const InputFrame& right) {
                  return left.sequence < right.sequence;
              });
    capture.decisionCallUs = decisionCalls.percentile(500);
    if (capture.frames.empty() || capture.session.displayRefreshHz <= 0 ||
            capture.session.streamRateHz <= 0) {
        error = "capture has no frames or invalid session rates";
        return false;
    }
    uint64_t priorSequence = 0;
    uint64_t priorArrival = 0;
    uint64_t period = (1000000ULL + capture.session.streamRateHz / 2) /
        capture.session.streamRateHz;
    for (InputFrame& frame : capture.frames) {
        if (!frame.sequence || !frame.arrivalUs || !frame.decodeUs ||
                (priorSequence && frame.sequence <= priorSequence) ||
                frame.arrivalUs < priorArrival) {
            error = "arrival identities/timestamps are missing, duplicated, or out of order";
            return false;
        }
        if (priorSequence && frame.sequence > priorSequence + 1)
            capture.missingArrivalSequences += frame.sequence - priorSequence - 1;
        priorSequence = frame.sequence;
        priorArrival = frame.arrivalUs;
        if (frame.sourcePeriodUs) period = frame.sourcePeriodUs;
        else frame.sourcePeriodUs = period;
    }
    if (capture.preparationUs.empty()) {
        capture.preparationUs.push_back(0);
        capture.acquisitionUs.push_back(0);
    }
    if (capture.presentCallUs.empty()) capture.presentCallUs.push_back(0);
    return true;
}

uint64_t addSaturated(uint64_t left, uint64_t right)
{
    return left > std::numeric_limits<uint64_t>::max() - right ?
        std::numeric_limits<uint64_t>::max() : left + right;
}

uint64_t rtpDeltaUs(uint32_t older, uint32_t newer)
{
    const uint32_t ticks = newer - older;
    return (static_cast<uint64_t>(ticks) * 1000000ULL + 45000ULL) / 90000ULL;
}

QJsonObject simulate(const Capture& capture, VrrReplayScenario scenario,
                     size_t queueCapacity)
{
    VrrSessionConfig simulatedSession = capture.session;
    if (!scenario.controllerCustomized) {
        // Match the UI migration from the enabled legacy checkbox to Balanced Target
        // when resolving current session policy, leaving explicit snapshots alone.
        if (simulatedSession.latencyFix && simulatedSession.latencyMode == 0) {
            simulatedSession.latencyFix = false;
            simulatedSession.latencyMode = 1;
        }
        scenario.controller = vrrTimingParametersForSession(simulatedSession);
    }
    VrrTimingController controller(simulatedSession, capture.canLatchPresentation, scenario.controller);
    std::deque<size_t> queue;
    size_t nextArrival = 0;
    size_t serviceOrdinal = 0;
    uint64_t scheduled = 0;
    uint64_t activeDecisions = 0;
    uint64_t activeTransitions = 0;
    uint64_t injectedPeriodicDecisions = 0;
    bool priorActive = false;
    uint64_t nowUs = capture.frames.front().arrivalUs;
    uint64_t capacityDrops = 0;
    uint64_t staleDrops = 0;
    uint64_t presented = 0;
    uint64_t maximumQueueDepth = 0;
    Distribution queueDepth;
    Distribution decodeLatency;
    Distribution arrivalLatency;
    Distribution scheduledAge;
    Distribution appliedDelay;
    Distribution senderSpacingError;
    Distribution presentationIntervals;
    Distribution presentationJerk;
    Distribution senderResidualJerk;
    Distribution jerkWithoutSourceStalls;
    uint64_t jerkOver2Ms = 0;
    uint64_t sourceStallIntervals = 0;
    uint64_t adaptiveSpacingViolations = 0;
    uint64_t latchedPresents = 0;
    std::vector<bool> dropped(capture.frames.size(), false);
    std::vector<bool> framePresented(capture.frames.size(), false);
    std::vector<bool> frameActive(capture.frames.size(), false);
    std::vector<uint64_t> sourceStallPrefix(capture.frames.size(), 0);
    for (size_t i = 1; i < capture.frames.size(); ++i) {
        const InputFrame& previous = capture.frames[i - 1];
        const InputFrame& current = capture.frames[i];
        const bool sourceGap = previous.rtpValid && current.rtpValid &&
            current.number == previous.number + 1 &&
            rtpDeltaUs(previous.rtp, current.rtp) > std::max<uint64_t>(
                25000, current.sourcePeriodUs * 3 / 2);
        sourceStallPrefix[i] = sourceStallPrefix[i - 1] + (sourceGap ? 1 : 0);
    }
    bool havePresented = false;
    uint64_t priorSubmissionUs = 0;
    uint32_t priorPresentedRtp = 0;
    bool priorPresentedRtpValid = false;
    int64_t priorSpacingErrorUs = 0;
    bool havePriorSpacingError = false;
    uint64_t priorPresentedInterval = 0;
    bool priorIntervalHadSourceStall = true;
    bool havePriorPresentedInterval = false;
    size_t priorPresentedIndex = 0;

    const VrrReplayExecutionParameters& execution = scenario.execution;
    const auto withPeriodic = [](uint64_t regular, uint64_t periodic,
                                 bool selected) {
        return addSaturated(regular, selected ? periodic : 0);
    };

    auto admitThrough = [&](uint64_t boundaryUs) {
        while (nextArrival < capture.frames.size() &&
                capture.frames[nextArrival].arrivalUs <= boundaryUs) {
            if (queue.size() >= queueCapacity) {
                dropped[queue.front()] = true;
                frameActive[queue.front()] = controller.latencyFixActive();
                queue.pop_front();
                ++capacityDrops;
            }
            queue.push_back(nextArrival++);
            maximumQueueDepth = std::max<uint64_t>(maximumQueueDepth,
                                                    queue.size());
            queueDepth.values.push_back(queue.size());
        }
    };

    while (nextArrival < capture.frames.size() || !queue.empty()) {
        if (queue.empty()) {
            nowUs = std::max(nowUs, capture.frames[nextArrival].arrivalUs);
        }
        admitThrough(nowUs);
        if (queue.empty()) continue;

        const size_t inputIndex = queue.front();
        const InputFrame& input = capture.frames[inputIndex];
        queue.pop_front();
        ++scheduled;
        const uint64_t every = execution.periodicStallEveryFrames;
        const uint64_t phase = every ? scheduled % every : 0;
        const uint64_t distance = every ?
            (phase + every - execution.periodicStallPhaseFrames) % every : 0;
        const bool periodic = every && distance < execution.periodicStallBurstFrames;
        injectedPeriodicDecisions += periodic;
        const uint64_t decisionDelay = withPeriodic(execution.decisionDelayUs,
            execution.periodicStallUs, periodic);
        // Captured GPU readiness can follow arrival. It is a fixed external
        // observation here, not a prediction of changed decoder backpressure.
        const uint64_t decisionUs = addSaturated(
            std::max(nowUs, input.decodeUs), decisionDelay);
        PacedFrame frame(nullptr, input.number, input.rtp, input.rtpValid,
                         input.decodeUs);
        const VrrTimingDecision decision = controller.schedule(frame, decisionUs);
        const uint64_t decisionEndUs = addSaturated(decisionUs, capture.decisionCallUs);
        admitThrough(decisionEndUs);
        const bool metronome = scenario.controller.playoutMetronomeEnabled != 0;
        const bool active = controller.latencyFixActive();
        activeDecisions += active;
        if (scheduled > 1 && active != priorActive) ++activeTransitions;
        priorActive = active;
        frameActive[inputIndex] = active;
        const uint64_t ageOrigin = active ? input.arrivalUs : input.decodeUs;
        const uint64_t ageUs = decisionEndUs >= ageOrigin ?
            decisionEndUs - ageOrigin : 0;
        scheduledAge.values.push_back(ageUs);
        appliedDelay.values.push_back(decision.playoutDelayUs);
        if (!queue.empty() && VrrFrameDropPolicy::beforeRender(decision,
                controller.displayPeriodUs(), ageUs, metronome, active)) {
            ++staleDrops;
            dropped[inputIndex] = true;
            controller.noteSubmission(false, false, 0);
            nowUs = decisionEndUs;
            continue;
        }

        // A scheduler delay only applies if the corresponding wait actually
        // sleeps. An already elapsed deadline cannot create wake lateness.
        const uint64_t renderWakeDelay = decisionEndUs < decision.renderStartUs ?
            withPeriodic(execution.renderWakeDelayUs,
                execution.periodicRenderWakeDelayUs, periodic) : 0;
        uint64_t renderStartUs = addSaturated(
            std::max(decisionEndUs, decision.renderStartUs), renderWakeDelay);
        admitThrough(renderStartUs);
        if (!queue.empty() && VrrFrameDropPolicy::afterRenderWait(decision,
                ageOrigin, renderStartUs, metronome, active)) {
            ++staleDrops;
            dropped[inputIndex] = true;
            if (metronome || active) controller.noteSubmission(false, false, 0);
            else controller.rebase();
            nowUs = renderStartUs;
            continue;
        }

        const uint64_t preparationUs = addSaturated(capture.preparationUs[
            serviceOrdinal % capture.preparationUs.size()],
            withPeriodic(execution.preparationDelayUs,
                execution.periodicPreparationStallUs, periodic));
        const uint64_t presentCallUs = capture.presentCallUs[
            serviceOrdinal % capture.presentCallUs.size()];
        const uint64_t acquisitionUs = capture.acquisitionUs[
            serviceOrdinal % capture.acquisitionUs.size()];
        ++serviceOrdinal;
        const uint64_t preparationEndUs = addSaturated(renderStartUs,
                                                        preparationUs);
        admitThrough(preparationEndUs);
        controller.notePreparationDuration(preparationUs, acquisitionUs);
        controller.noteSpacingDeficit(0);
        const bool targetWaited = preparationEndUs < decision.targetUs;
        const uint64_t targetWakeDelay = targetWaited ?
            withPeriodic(execution.targetWakeDelayUs,
                execution.periodicTargetWakeDelayUs, periodic) : 0;
        uint64_t submissionUs = addSaturated(
            std::max(preparationEndUs, decision.targetUs), targetWakeDelay);
        submissionUs = std::max(submissionUs, controller.earliestSubmissionUs());
        submissionUs = addSaturated(submissionUs,
            withPeriodic(execution.submissionDelayUs,
                execution.periodicSubmissionStallUs, periodic));
        admitThrough(submissionUs);
        controller.noteSchedulerDelays(renderWakeDelay, targetWakeDelay, targetWaited);
        controller.noteSubmission(true, false, submissionUs);
        ++presented;
        framePresented[inputIndex] = true;
        latchedPresents += decision.latchedPresentation;
        decodeLatency.values.push_back(submissionUs >= input.decodeUs ?
            submissionUs - input.decodeUs : 0);
        arrivalLatency.values.push_back(submissionUs >= input.arrivalUs ?
            submissionUs - input.arrivalUs : 0);

        if (havePresented) {
            const uint64_t presentationInterval = submissionUs - priorSubmissionUs;
            presentationIntervals.values.push_back(presentationInterval);
            if (!decision.latchedPresentation &&
                    presentationInterval < controller.displayPeriodUs())
                ++adaptiveSpacingViolations;
            bool sourceStall = true;
            if (input.rtpValid && priorPresentedRtpValid) {
                const uint64_t sourceInterval = rtpDeltaUs(priorPresentedRtp,
                                                           input.rtp);
                const int64_t spacingError =
                    static_cast<int64_t>(presentationInterval) -
                    static_cast<int64_t>(sourceInterval);
                senderSpacingError.values.push_back(static_cast<uint64_t>(
                    std::llabs(spacingError)));
                // Local shedding lengthens the presented source interval.
                // Exclude only gaps that already existed between adjacent
                // captured source frames, never the gaps this policy created.
                sourceStall = sourceStallPrefix[inputIndex] !=
                    sourceStallPrefix[priorPresentedIndex];
                sourceStallIntervals += sourceStall;
                if (havePriorSpacingError) {
                    senderResidualJerk.values.push_back(static_cast<uint64_t>(
                        std::llabs(spacingError - priorSpacingErrorUs)));
                }
                priorSpacingErrorUs = spacingError;
                havePriorSpacingError = true;
            }
            else havePriorSpacingError = false;
            if (havePriorPresentedInterval) {
                const uint64_t jerk = presentationInterval >= priorPresentedInterval ?
                    presentationInterval - priorPresentedInterval :
                    priorPresentedInterval - presentationInterval;
                presentationJerk.values.push_back(jerk);
                jerkOver2Ms += jerk > 2000;
                if (!sourceStall && !priorIntervalHadSourceStall)
                    jerkWithoutSourceStalls.values.push_back(jerk);
            }
            priorIntervalHadSourceStall = sourceStall;
            priorPresentedInterval = presentationInterval;
            havePriorPresentedInterval = true;
        }
        havePresented = true;
        priorSubmissionUs = submissionUs;
        priorPresentedRtp = input.rtp;
        priorPresentedRtpValid = input.rtpValid;
        priorPresentedIndex = inputIndex;
        nowUs = addSaturated(submissionUs, presentCallUs);
        admitThrough(nowUs);
    }

    uint64_t consecutiveDrops = 0;
    Distribution dropClusters;
    uint64_t activeDrops = 0;
    QMap<QString, QJsonObject> bands;
    const int cutoff = VrrRatePolicy::protectedRateForRefresh(capture.session.displayRefreshHz);
    const uint64_t cutoffPeriod = cutoff > 0 ?
        (1000000ULL + static_cast<uint64_t>(cutoff) / 2) / cutoff : 0;
    const uint64_t displayPeriod = controller.displayPeriodUs();
    for (size_t i = 0; i < capture.frames.size(); ++i) {
        if (dropped[i]) {
            ++consecutiveDrops;
            activeDrops += frameActive[i];
        }
        else if (consecutiveDrops) {
            dropClusters.values.push_back(consecutiveDrops);
            consecutiveDrops = 0;
        }
        const uint64_t period = capture.frames[i].sourcePeriodUs;
        const QString band = period < displayPeriod ? "above_refresh" :
            cutoffPeriod && period <= cutoffPeriod ? "near_refresh" : "below_near_refresh";
        QJsonObject& counts = bands[band];
        counts["arrivals"] = counts.value("arrivals").toInteger() + 1;
        counts["presented"] = counts.value("presented").toInteger() + (framePresented[i] ? 1 : 0);
        counts["dropped"] = counts.value("dropped").toInteger() + (dropped[i] ? 1 : 0);
    }
    if (consecutiveDrops) dropClusters.values.push_back(consecutiveDrops);
    QJsonObject bandResults;
    for (auto it = bands.cbegin(); it != bands.cend(); ++it) bandResults[it.key()] = it.value();

    QJsonObject result;
    result["scenario"] = scenario.name;
    result["can_latch_present"] = capture.canLatchPresentation;
    result["queue_capacity"] = static_cast<qint64>(queueCapacity);
    result["arrivals"] = static_cast<qint64>(capture.frames.size());
    result["presented"] = static_cast<qint64>(presented);
    result["capacity_drops"] = static_cast<qint64>(capacityDrops);
    result["stale_drops"] = static_cast<qint64>(staleDrops);
    result["total_drops"] = static_cast<qint64>(capacityDrops + staleDrops);
    result["drop_percent"] = capture.frames.empty() ? 0.0 :
        100.0 * static_cast<double>(capacityDrops + staleDrops) /
            static_cast<double>(capture.frames.size());
    result["maximum_queue_depth"] = static_cast<qint64>(maximumQueueDepth);
    result["scheduled"] = static_cast<qint64>(scheduled);
    result["latency_fix_active_decisions"] = static_cast<qint64>(activeDecisions);
    result["latency_fix_transitions"] = static_cast<qint64>(activeTransitions);
    result["latency_fix_active_drops"] = static_cast<qint64>(activeDrops);
    result["periodic_injection_selected_decisions"] = static_cast<qint64>(injectedPeriodicDecisions);
    result["drop_cluster_frames"] = dropClusters.json();
    result["maximum_consecutive_drops"] = static_cast<qint64>(dropClusters.percentile(1000));
    result["recorded_source_bands"] = bandResults;
    result["near_refresh_band_minimum_fps"] = cutoff;
    result["queue_depth"] = queueDepth.json();
    result["decode_to_submission_us"] = decodeLatency.json();
    result["arrival_to_submission_us"] = arrivalLatency.json();
    result["schedule_age_us"] = scheduledAge.json();
    result["playout_delay_us"] = appliedDelay.json();
    result["sender_spacing_error_us"] = senderSpacingError.json();
    result["presentation_interval_us"] = presentationIntervals.json();
    result["presentation_jerk_us"] = presentationJerk.json();
    result["presentation_jerk_over_2ms_percent"] = presentationJerk.values.empty() ? 0.0 :
        100.0 * jerkOver2Ms / presentationJerk.values.size();
    result["presentation_jerk_excluding_source_stalls_us"] = jerkWithoutSourceStalls.json();
    result["sender_residual_jerk_us"] = senderResidualJerk.json();
    result["presented_source_gap_intervals"] = static_cast<qint64>(sourceStallIntervals);
    result["adaptive_spacing_violations"] = static_cast<qint64>(adaptiveSpacingViolations);
    result["latched_presents"] = static_cast<qint64>(latchedPresents);
    result["accounting_valid"] = presented + staleDrops + capacityDrops == capture.frames.size();
    result["resolved_execution"] = vrrExecutionParametersToJson(scenario.execution);
    result["controller_parameter_source"] = scenario.controllerCustomized ?
        "explicit scenario configuration" : "current session policy";
    result["resolved_controller"] = vrrTimingParametersToJson(scenario.controller);
    return result;
}

bool validateSupportedScenario(const VrrReplayScenario& scenario, QString& error)
{
    if (vrrDisplayParametersToJson(scenario.display) !=
            vrrDisplayParametersToJson(VrrReplayDisplayParameters {})) {
        error = "queue simulation does not model display parameters";
        return false;
    }
    const auto& e = scenario.execution;
    if (e.displayTransitionDelayUs || e.periodicDisplayTransitionDelayUs ||
            e.spacingGuardFeedbackUs || e.periodicSpacingGuardFeedbackUs ||
            e.submissionAdvanceUs || e.periodicSubmissionAdvanceUs ||
            e.removePrePresentRasterProbeOverhead) {
        error = "queue simulation does not support display-transition, guard-fault, "
                "early-submission, or raster-probe execution overrides";
        return false;
    }
    if (!e.periodicStallEveryFrames && (e.periodicStallUs ||
            e.periodicRenderWakeDelayUs || e.periodicTargetWakeDelayUs ||
            e.periodicPreparationStallUs || e.periodicSubmissionStallUs)) {
        error = "periodic execution delays require periodic_stall_every_frames";
        return false;
    }
    return true;
}

bool applyAssertions(QJsonObject& result, const QList<VrrReplayAssertion>& assertions)
{
    bool allPassed = true;
    QJsonArray outcomes;
    for (const VrrReplayAssertion& assertion : assertions) {
        QJsonValue value(result);
        for (const QString& component : assertion.metric.split('.'))
            value = value.toObject().value(component);
        bool passed = value.isDouble() || value.isBool();
        const double actual = value.isBool() ? (value.toBool() ? 1.0 : 0.0) : value.toDouble();
        if (passed && assertion.operation == "<") passed = actual < assertion.value;
        else if (passed && assertion.operation == "<=") passed = actual <= assertion.value;
        else if (passed && assertion.operation == "==") passed = actual == assertion.value;
        else if (passed && assertion.operation == ">=") passed = actual >= assertion.value;
        else if (passed && assertion.operation == ">") passed = actual > assertion.value;
        else passed = false;
        QJsonObject outcome;
        outcome["metric"] = assertion.metric;
        outcome["operator"] = assertion.operation;
        outcome["expected"] = assertion.value;
        outcome["actual"] = value;
        outcome["passed"] = passed;
        if (value.isUndefined()) outcome["error"] = "unknown queue simulation metric";
        outcomes.append(outcome);
        allPassed &= passed;
    }
    result["assertions"] = outcomes;
    result["assertions_passed"] = allPassed;
    return allPassed;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("vrrqueuesim");
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Replay every captured arrival through the real VRR controller and bounded worker queue");
    parser.addHelpOption();
    parser.addPositionalArgument("csv", "Expanded VRR trace CSV");
    QCommandLineOption configOption(
        "config", "Replay scenario configuration", "json");
    QCommandLineOption capacityOption(
        "queue-capacity", "Override queue capacity", "frames");
    QCommandLineOption outputOption(
        "output", "Write JSON output", "json");
    parser.addOption(configOption);
    parser.addOption(capacityOption);
    parser.addOption(outputOption);
    parser.process(application);
    if (parser.positionalArguments().size() != 1) parser.showHelp(1);

    Capture capture;
    QString error;
    if (!loadCapture(parser.positionalArguments().front(), capture, error)) {
        qCritical("Unable to load capture: %s", qPrintable(error));
        return 1;
    }

    VrrReplayConfiguration configuration;
    if (parser.isSet(configOption)) {
        QFile configFile(parser.value(configOption));
        if (!configFile.open(QIODevice::ReadOnly) ||
                !loadVrrReplayConfiguration(configFile.readAll(),
                                            configuration, error)) {
            qCritical("Unable to load configuration: %s", qPrintable(error));
            return 1;
        }
    }
    else {
        configuration.scenarios.append(VrrReplayScenario {});
    }

    bool capacityOk = false;
    const qulonglong capacityOverride = parser.value(capacityOption).
        toULongLong(&capacityOk);
    if (parser.isSet(capacityOption) && (!capacityOk || capacityOverride == 0 ||
            capacityOverride > std::numeric_limits<size_t>::max())) {
        qCritical("queue capacity must be a positive integer");
        return 1;
    }

    QJsonArray scenarios;
    bool allAssertionsPassed = true;
    for (const VrrReplayScenario& scenario : configuration.scenarios) {
        if (!validateSupportedScenario(scenario, error)) {
            qCritical("Unsupported scenario %s: %s", qPrintable(scenario.name), qPrintable(error));
            return 2;
        }
        QJsonObject result = simulate(
            capture, scenario,
            parser.isSet(capacityOption) ?
                static_cast<size_t>(capacityOverride) :
                scenario.worker.queueCapacity);
        allAssertionsPassed &= applyAssertions(result, scenario.assertions);
        scenarios.append(result);
    }
    QJsonObject root;
    root["model"] = "all-arrival-event-driven-worker-v2";
    root["model_limitations"] = QJsonArray {
        "Uses the production controller and shared stale policy with simulated queue admission; does not execute the native worker or renderer",
        "Preparation and native-call durations replay captured sample sequences by completed service ordinal; they are not predictions of changed native service costs",
        "Captured GPU-ready times remain fixed; changed decoder backpressure and dropped-frame service costs are unknown",
        "Assumes arrivals belong to one active session; does not reconstruct suspension or native presentation feedback",
        "Presentation metrics describe CPU submission times, not verified scanout or optical smoothness",
        "Raw presented jerk includes policy-created frame gaps; sender-residual jerk is reported separately",
        "Scenario assertions use this output's numeric dot paths, not vrrreplay summary paths"
    };
    root["baseline_exact"] = false;
    root["missing_arrival_sequences"] = static_cast<qint64>(capture.missingArrivalSequences);
    root["assertions_passed"] = allAssertionsPassed;
    root["trace"] = parser.positionalArguments().front();
    root["display_hz"] = capture.session.displayRefreshHz;
    root["stream_fps"] = capture.session.streamRateHz;
    root["decision_call_us"] = static_cast<qint64>(capture.decisionCallUs);
    root["preparation_samples"] = static_cast<qint64>(
        capture.preparationUs.size());
    root["present_call_samples"] = static_cast<qint64>(
        capture.presentCallUs.size());
    root["scenarios"] = scenarios;
    const QByteArray output = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (parser.isSet(outputOption)) {
        QFile file(parser.value(outputOption));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                file.write(output) != output.size()) {
            qCritical("Unable to write output: %s", qPrintable(file.errorString()));
            return 1;
        }
    }
    else {
        std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
    }
    return allAssertionsPassed ? 0 : 3;
}
