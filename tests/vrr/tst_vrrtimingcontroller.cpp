// Standalone deterministic coverage for the platform-neutral VRR core.
// This file is intentionally not wired into the application build; tests/vrr
// owns the optional qmake harness.

#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrtargetwaiter.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrtimingcontroller.h"
#include "../../app/streaming/vrrratepolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

VrrSessionConfig config(int streamRateHz = 60, int displayRefreshHz = 120)
{
    VrrSessionConfig value;
    value.streamRateHz = streamRateHz;
    value.displayRefreshHz = displayRefreshHz;
    return value;
}

// Keep existing VRR13/metronome regression fixtures on their recorded policy.
VrrTimingParameters legacyPlayoutParameters(const VrrSessionConfig& session)
{
    auto policy = vrrTimingParametersForSession(session);
    policy.playoutOffsetCadenceGate = 0;
    policy.playoutOffsetSlewUsPerSecond = 0;
    policy.playoutResponsiveBuffer = 0;
    policy.playoutDelayCapSourcePeriodPerMille = 0;
    policy.playoutPredictionOnly = 0;
    policy.playoutSubmissionEstimateFallback = 0;
    policy.playoutDelayMarginUs = 300;
    policy.playoutAdaptiveOnly = 0;
    policy.playoutPerFrameLatch = 1;
    policy.playoutRateProtectionEnabled = 0;
    policy.playoutReadinessDrivenAdaptation = 0;
    policy.playoutNativeHitchAdaptation = 0;
    policy.playoutSubmissionEstimateFallback = 0;
    policy.playoutStableSmoothnessReference = 0;
    policy.renderStartPreserveLearnedLead = 0;
    policy.playoutPredictionEnabled = 0;
    policy.playoutSmoothnessFeedbackEnabled = 0;
    policy.playoutSmoothingGainPerMille = session.smoothFrameTiming ? 200 : 0;
    policy.playoutSmoothingMaxLagUs = 6000;
    policy.playoutDelayMaximumUs = 8000;
    policy.playoutDelayMaximumPeriodPerMille = 950;
    policy.playoutDelayAttackUs = 50;
    return policy;
}

// Historical display-feedback policies remain selectable for exact replay.
VrrTimingParameters legacyFeedbackParameters(const VrrSessionConfig& session)
{
    auto policy = vrrTimingParametersForSession(session);
    policy.playoutOffsetCadenceGate = 0;
    policy.playoutOffsetSlewUsPerSecond = 0;
    policy.playoutResponsiveBuffer = 0;
    policy.playoutDelayCapSourcePeriodPerMille = 0;
    policy.playoutDelayMaximumPeriodPerMille = 0;
    policy.playoutPredictionOnly = 0;
    policy.playoutSubmissionEstimateFallback = 0;
    policy.playoutNativeHitchAdaptation = 1;
    policy.playoutDelayMarginUs = 300;
    policy.playoutSmoothingGainPerMille = 0;
    policy.playoutSmoothingMaxLagUs = 6000;
    policy.playoutDelayMaximumUs = 16000;
    return policy;
}

// The retired metronome, for the tests that keep it replayable.
VrrTimingParameters metronomePolicy(const VrrSessionConfig& session)
{
    VrrTimingParameters policy = legacyPlayoutParameters(session);
    policy.playoutHistoryEnabled = 0;
    policy.playoutPerFrameLatch = 0;
    policy.playoutSmoothingGainPerMille = 150;
    policy.playoutMetronomeEnabled = 1;
    policy.playoutMotionDeadbandEnabled = 0;
    policy.playoutBandWidthHz = 20;
    policy.playoutDelaySlewAcrossBands = 0;
    return policy;
}

PacedFrame frame(int number, uint32_t timestamp, bool timestampValid,
                  uint64_t decodedUs)
{
    return PacedFrame(nullptr, number, timestamp, timestampValid, decodedUs);
}

uint32_t quantizedRtpTimestamp(int frameNumber, int sourceRateHz,
                               int captureRateHz = 120)
{
    const uint64_t captureFrame =
        (static_cast<uint64_t>(frameNumber) * captureRateHz +
         static_cast<uint64_t>(sourceRateHz) / 2) /
        static_cast<uint64_t>(sourceRateHz);
    return static_cast<uint32_t>(
        (captureFrame * 90000ULL +
         static_cast<uint64_t>(captureRateHz) / 2) /
        static_cast<uint64_t>(captureRateHz));
}

uint64_t decodedTimeForRtp(uint64_t epochUs, uint32_t timestamp)
{
    return epochUs + static_cast<uint64_t>(timestamp) * 1000000ULL / 90000ULL;
}

uint64_t idealDecodedTime(uint64_t epochUs, int frameNumber, int rateHz)
{
    return epochUs + (static_cast<uint64_t>(frameNumber) * 1000000ULL +
                      static_cast<uint64_t>(rateHz) / 2) /
        static_cast<uint64_t>(rateHz);
}

void testRtpWrapResetAndFallback()
{
    VrrTimingController controller(config());
    const uint32_t wrappedStart = 0xfffffe00U;
    controller.schedule(frame(1, wrappedStart, true, 100000), 100000);
    VrrTimingDecision wrapped = controller.schedule(
        frame(2, wrappedStart + 1500U, true, 116666), 116666);
    expect(!wrapped.rebased && wrapped.usedRtpTimestamp,
           "RTP wrap must be a normal valid interval");
    expect(wrapped.sourceIntervalUs == 16666,
           "wrapped RTP delta must convert at 90 kHz");

    VrrTimingDecision reset = controller.schedule(
        frame(3, 100U, true, 130000), 130000);
    expect(reset.rebased,
           "backward RTP movement must rebase rather than unwrap forward");

    VrrTimingController largeForwardController(config());
    largeForwardController.schedule(frame(1, 0, true, 100000), 100000);
    VrrTimingDecision largeForward = largeForwardController.schedule(
        frame(2, 90001, true, 1100011), 1100011);
    expect(largeForward.rebased,
           "valid RTP movement over one second must rebase");

    VrrTimingController fallbackController(config());
    fallbackController.schedule(frame(10, 0, false, 500000), 500000);
    VrrTimingDecision fallback = fallbackController.schedule(
        frame(12, 0, false, 533334), 533334);
    expect(!fallback.usedRtpTimestamp && fallback.sourceIntervalUs == 33333,
           "invalid timestamps must use rational frame-number cadence");

    VrrTimingDecision forwardReset = fallbackController.schedule(
        frame(1000, 0, false, 2000000), 2000000);
    expect(forwardReset.rebased,
           "fallback movement beyond one second must rebase");
}

void testTimingFormulaeAndReserveCap()
{
    VrrTimingController controller(config(60, 120));
    VrrTimingDecision first = controller.schedule(
        frame(1, 0, true, 100000), 100000);
    const VrrTimingParameters& parameters = controller.parameters();
    const uint64_t expectedGuardUs = std::clamp(
        controller.displayPeriodUs() / parameters.baseGuardDivisor,
        parameters.minimumGuardUs, parameters.maximumBaseGuardUs);
    expect(first.guardUs == expectedGuardUs,
           "display guard must honor the configured divisor and bounds");
    expect(first.headroomUs ==
               controller.sourcePeriodUs() - controller.displayPeriodUs() -
                   expectedGuardUs,
           "headroom must subtract one display period and the guard");
    expect(first.targetUs ==
               100000 + first.renderLeadUs +
                   parameters.presentationSafetyUs +
                   parameters.sourcePlayoutDelayUs &&
               first.renderStartUs ==
                   first.targetUs - first.renderLeadUs - first.renderWakeLeadUs,
           "target must include render lead and presentation safety");

    controller.noteSubmission(true, false, first.targetUs);
    VrrTimingDecision second = controller.schedule(
        frame(2, 1500, true, 116666), 116666);
    expect(second.targetUs >=
               first.targetUs + controller.displayPeriodUs() + expectedGuardUs,
           "target must honor the prior presentation floor and guard");

    VrrTimingController capped(config(360, 120));
    VrrTimingDecision cappedDecision = capped.schedule(
        frame(1, 0, true, 100000), 100000);
    capped.notePreparationDuration(10000);
    capped.noteSubmission(true, false, cappedDecision.targetUs);
    expect(capped.renderLeadUs() <= capped.sourcePeriodUs(),
           "render lead must never exceed the source period");
}

void testSourcePlayoutDelayOffsetsProjectedTargets()
{
    VrrTimingParameters parameters;
    parameters.sourcePlayoutDelayUs = 12000;
    VrrTimingController buffered(config(60, 120), true, parameters);
    VrrTimingController direct(config(60, 120), true,
                               VrrTimingParameters {});

    const VrrTimingDecision bufferedFirst = buffered.schedule(
        frame(1, 0, true, 100000), 100000);
    const VrrTimingDecision directFirst = direct.schedule(
        frame(1, 0, true, 100000), 100000);
    expect(bufferedFirst.targetUs ==
               directFirst.targetUs + parameters.sourcePlayoutDelayUs,
           "source playout delay must offset the projected target instead of the arrival-time clamp");
    expect(bufferedFirst.timingBudgetUs ==
               directFirst.timingBudgetUs + parameters.sourcePlayoutDelayUs,
           "source playout delay must be reported in the timing budget");

    const VrrSessionConfig lowLatency = config(60, 120);
    VrrSessionConfig smoothness = lowLatency;
    smoothness.allowAdditionalQueuedFrame = true;
    const VrrTimingParameters lowLatencyPolicy =
        legacyPlayoutParameters(lowLatency);
    const VrrTimingParameters smoothnessPolicy =
        legacyPlayoutParameters(smoothness);
    expect(lowLatencyPolicy.timestampPlayoutEnabled == 1 &&
               lowLatencyPolicy.playoutDelayAdaptive == 1 &&
               lowLatencyPolicy.sourcePlayoutDelayUs == 3000 &&
               lowLatencyPolicy.playoutDelayStartUs == 6000 &&
               lowLatencyPolicy.playoutDelayMinimumUs == 1000 &&
               lowLatencyPolicy.playoutDelayMaximumUs == 8000 &&
               lowLatencyPolicy.playoutDelayPercentilePerMille == 1000 &&
               lowLatencyPolicy.playoutPrepareOnArrival == 0 &&
               lowLatencyPolicy.renderStartAfterSubmissionUs == 6000 &&
               lowLatencyPolicy.renderStartMinimumLeadUs == 2500 &&
               lowLatencyPolicy.renderLeadFloorUs == 3000 &&
               lowLatencyPolicy.playoutSmoothingGainPerMille == 200 &&
               lowLatencyPolicy.playoutSmoothingPeriodAlphaPerMille == 100 &&
               lowLatencyPolicy.playoutSmoothingMaxLagUs == 6000 &&
               lowLatencyPolicy.playoutMetronomeEnabled == 0 &&
               lowLatencyPolicy.playoutDelayStartPeriodPerMille == 950 &&
               lowLatencyPolicy.playoutDelayMaximumPeriodPerMille == 950 &&
               lowLatencyPolicy.playoutSmoothingSnapPerMille == 3000 &&
               lowLatencyPolicy.playoutOffsetReseedFrames == 3 &&
               lowLatencyPolicy.playoutBandWidthHz == 20 &&
               lowLatencyPolicy.playoutMotionDeadbandEnabled == 0 &&
               lowLatencyPolicy.playoutDelaySlewAcrossBands == 1 &&
               lowLatencyPolicy.rateCandidateMinimumUs == 200000 &&
               lowLatencyPolicy.playoutStallBurstExclusion == 1 &&
               lowLatencyPolicy.latchedFloorDisabled == 1 &&
               lowLatencyPolicy.pacingLatencyExtraPeriodNumerator == 0 &&
               lowLatencyPolicy.pacingLatencyQueueModeExtra == 0 &&
               lowLatencyPolicy.readinessLowPercentile == 0 &&
               lowLatencyPolicy.readinessLoosePercentile == 80 &&
               lowLatencyPolicy.retainReadinessOnPhaseReset == 0,
           "sessions must resolve the calibrated adaptive timestamp playout policy");
    expect(smoothnessPolicy.playoutDelayAdaptive ==
                   lowLatencyPolicy.playoutDelayAdaptive &&
               smoothnessPolicy.playoutDelayStartUs ==
                   lowLatencyPolicy.playoutDelayStartUs &&
               smoothnessPolicy.playoutDelayMaximumUs ==
                   lowLatencyPolicy.playoutDelayMaximumUs &&
               smoothnessPolicy.playoutDelayPercentilePerMille ==
                   lowLatencyPolicy.playoutDelayPercentilePerMille &&
               smoothnessPolicy.pacingLatencyQueueModeExtra == 0,
           "the retired smoothness flag must not change the session policy");
    VrrSessionConfig legacySmoothness = config(100, 120);
    legacySmoothness.allowAdditionalQueuedFrame = true;
    VrrTimingParameters legacyParameters;
    legacyParameters.timestampPlayoutEnabled = 0;
    VrrTimingController legacy(legacySmoothness, true, legacyParameters);
    VrrTimingController current(legacySmoothness, true, smoothnessPolicy);
    expect(legacy.diagnostics().pacingLatencyBudgetUs ==
               current.diagnostics().pacingLatencyBudgetUs +
                   legacy.sourcePeriodUs(),
           "only captures made under the retired option keep its extra render budget");
}

void testTimestampModePreservesUnevenHostIntervals()
{
    // A 60 FPS average with alternating 13.33/20 ms source intervals.
    // Both fit a 120 Hz display, so a faithful scheduler must preserve
    // this real source variation while absorbing separate delivery jitter.
    VrrSessionConfig session = config(60, 120);
    session.smoothFrameTiming = false;
    VrrTimingParameters policy = legacyPlayoutParameters(session);
    expect(policy.timestampPlayoutEnabled == 1 &&
               policy.playoutDelayAdaptive == 1 &&
               policy.playoutMetronomeEnabled == 0 &&
               policy.playoutSmoothingGainPerMille == 0,
           "disabling frame timing smoothing must keep buffering and disable both smoothers");
    // Hold the buffering delay constant to isolate interval fidelity;
    // the adaptive calibrator has separate coverage.
    policy.playoutDelayAdaptive = 0;
    VrrTimingController controller(session, true, policy);
    const uint64_t epochUs = 1000000;
    const uint64_t delayUs = policy.sourcePlayoutDelayUs;

    const auto rtpFor = [](int i) {
        return static_cast<uint32_t>((i / 2) * 3000 + (i % 2) * 1200);
    };
    // Deterministic jitter in [0, 3000] us, within the 3 ms delay. Every
    // fiftieth frame arrives with zero jitter so the three-second offset
    // window always contains the true floor and the mapping stays fixed.
    const auto jitterFor = [](int i) {
        if (i % 50 == 0) {
            return static_cast<uint64_t>(0);
        }
        return static_cast<uint64_t>((static_cast<uint64_t>(i) * 7919ULL) % 3001ULL);
    };

    VrrTimingDecision decision;
    uint64_t previousTargetUs = 0;
    unsigned int spacingErrors = 0;
    unsigned int reserveReports = 0;
    for (int i = 0; i < 400; ++i) {
        const uint32_t timestamp = rtpFor(i);
        const uint64_t idealUs = decodedTimeForRtp(epochUs, timestamp);
        const uint64_t decodedUs = idealUs + jitterFor(i);
        decision = controller.schedule(
            frame(i + 1, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        expect(i == 0 || (!decision.rebased && !decision.phaseDiscontinuity),
               "sub-delay jitter must never re-anchor the timestamp clock");
        if (decision.readinessBudgetUs != 0 ||
                controller.diagnostics().appliedReadinessReserveUs != 0) {
            ++reserveReports;
        }
        if (i > 0) {
            const uint64_t expectedSpacingUs =
                idealUs - decodedTimeForRtp(epochUs, rtpFor(i - 1));
            const uint64_t spacingUs = decision.targetUs - previousTargetUs;
            if (spacingUs != expectedSpacingUs) {
                if (spacingErrors < 5) {
                    std::fprintf(stderr,
                                 "playout spacing i=%d target=%llu ideal=%llu decoded=%llu spacing=%llu expected=%llu offset=%lld ready=%lld source=%llu lead=%llu flags=%d%d%d\n",
                                 i,
                                 static_cast<unsigned long long>(decision.targetUs),
                                 static_cast<unsigned long long>(idealUs),
                                 static_cast<unsigned long long>(decodedUs),
                                 static_cast<unsigned long long>(spacingUs),
                                 static_cast<unsigned long long>(expectedSpacingUs),
                                 static_cast<long long>(controller.playoutOffsetUs()),
                                 static_cast<long long>(decision.readyOffsetUs),
                                 static_cast<unsigned long long>(decision.sourceTimeUs),
                                 static_cast<unsigned long long>(decision.renderLeadUs),
                                 decision.rebased ? 1 : 0,
                                 decision.phaseDiscontinuity ? 1 : 0,
                                 decision.sourceRateChanged ? 1 : 0);
                }
                ++spacingErrors;
            }
        }
        previousTargetUs = decision.targetUs;
    }
    expect(spacingErrors == 0,
           "timestamp playout must reproduce sender spacing exactly under sub-delay jitter");
    expect(reserveReports == 0,
           "timestamp playout must not apply or report a learned readiness reserve");
    expect(controller.timestampPlayoutActive(),
           "sessions with smoothing disabled must still use timestamp playout");
    expect(controller.playoutOffsetUs() == static_cast<int64_t>(epochUs),
           "the applied offset must be the earliest arrival in the window");
    expect(decision.targetUs ==
               decodedTimeForRtp(epochUs, rtpFor(399)) + delayUs +
                   decision.renderLeadUs,
           "every target must sit exactly one fixed delay after mapped sender time");
    expect(controller.timingBudgetUs() == delayUs,
           "the timing budget must report only the fixed playout delay");

    // A frame later than the delay clamps to now and nothing else moves.
    const uint32_t lateTimestamp = rtpFor(400);
    const uint64_t lateIdealUs = decodedTimeForRtp(epochUs, lateTimestamp);
    const uint64_t lateDecodedUs = lateIdealUs + delayUs + 1500;
    const VrrTimingDecision late = controller.schedule(
        frame(401, lateTimestamp, true, lateDecodedUs), lateDecodedUs);
    controller.noteSubmission(true, false, late.targetUs);
    expect(late.targetUs == lateDecodedUs + late.renderLeadUs,
           "a frame later than the delay must present as soon as it is ready");
    expect(!late.phaseDiscontinuity && !late.rebased &&
               controller.playoutOffsetUs() == static_cast<int64_t>(epochUs),
           "one late frame must not move the sender clock mapping");

    const uint32_t nextTimestamp = rtpFor(401);
    const uint64_t nextIdealUs = decodedTimeForRtp(epochUs, nextTimestamp);
    const VrrTimingDecision next = controller.schedule(
        frame(402, nextTimestamp, true, nextIdealUs + 200), nextIdealUs + 200);
    controller.noteSubmission(true, false, next.targetUs);
    expect(next.targetUs == nextIdealUs + delayUs + next.renderLeadUs,
           "the frame after a late frame must return to its own slot");

    // Slow clock drift is followed at the slew rate, never as a jump.
    const int64_t offsetBeforeDrift = controller.playoutOffsetUs();
    uint64_t maximumSpacingErrorUs = 0;
    previousTargetUs = next.targetUs;
    for (int i = 402; i < 1400; ++i) {
        const uint32_t timestamp = rtpFor(i);
        const uint64_t idealUs = decodedTimeForRtp(epochUs, timestamp);
        // Frames arrive one microsecond later per frame relative to RTP.
        const uint64_t decodedUs = idealUs + static_cast<uint64_t>(i - 401);
        decision = controller.schedule(
            frame(i + 1, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        const uint64_t expectedSpacingUs =
            idealUs - decodedTimeForRtp(epochUs, rtpFor(i - 1));
        const uint64_t spacingUs = decision.targetUs - previousTargetUs;
        const uint64_t errorUs = spacingUs > expectedSpacingUs ?
            spacingUs - expectedSpacingUs : expectedSpacingUs - spacingUs;
        maximumSpacingErrorUs = std::max(maximumSpacingErrorUs, errorUs);
        previousTargetUs = decision.targetUs;
    }
    expect(controller.playoutOffsetUs() > offsetBeforeDrift + 500,
           "the mapping offset must follow sustained drift");
    expect(maximumSpacingErrorUs <= policy.playoutOffsetSlewUs,
           "drift tracking must never move a target by more than the slew per frame");
}

// Isolate the mapper from adaptive delay, render training and smoothing. The
// public schedule path still exercises RTP conversion and cadence eligibility.
VrrTimingParameters offsetTestPolicy()
{
    VrrTimingParameters policy;
    policy.timestampPlayoutEnabled = 1;
    policy.sourcePlayoutDelayUs = 6000;
    policy.playoutOffsetWarmupSamples = 0;
    policy.playoutOffsetCadenceGate = 1;
    policy.playoutOffsetSlewUsPerSecond = 2400;
    policy.playoutOffsetSourceClock = 1;
    policy.playoutOffsetMaximumStepUs = 100;
    return policy;
}

void testOffsetSlewUsesElapsedTime()
{
    for (int rateHz : {30, 60, 120, 240}) {
        for (int direction : {-1, 1}) {
            for (uint64_t slewRate : {uint64_t(7), uint64_t(2400)}) {
                auto policy = offsetTestPolicy();
                policy.playoutOffsetWindowUs = 1;
                policy.playoutOffsetSlewUsPerSecond = slewRate;
                VrrTimingController controller(config(rateHz, 240), true, policy);
                constexpr uint64_t epochUs = 1000000;
                controller.schedule(frame(1, 0, true, epochUs), epochUs);
                int64_t previousOffset = controller.playoutOffsetUs();
                uint64_t lastSourceClockUs = 0;
                // Keep a downward phase step shorter than one source interval
                // so even the 240 FPS fixture has a monotonic observation clock.
                const int64_t phaseUs = static_cast<int64_t>(
                    std::min<uint64_t>(6000, 750000 / uint64_t(rateHz)));
                for (int i = 1; i <= rateHz; ++i) {
                    const auto timestamp = static_cast<uint32_t>(
                        uint64_t(i) * 90000 / uint64_t(rateHz));
                    const uint64_t decodedUs = static_cast<uint64_t>(
                        int64_t(decodedTimeForRtp(epochUs, timestamp)) + direction * phaseUs);
                    const auto decision = controller.schedule(
                        frame(i + 1, timestamp, true, decodedUs), decodedUs);
                    const int64_t offset = controller.playoutOffsetUs();
                    expect(std::abs(offset - previousOffset) <= 100,
                           "elapsed-time offset slew must respect its per-observation cap");
                    expect(decision.playoutDelayUs == 6000,
                           "offset correction must not acquire extra playout buffering");
                    previousOffset = offset;
                    lastSourceClockUs =
                        uint64_t(timestamp) * 1000000 / 90000;
                }
                const int64_t expectedUs = static_cast<int64_t>(
                    lastSourceClockUs * slewRate / 1000000);
                expect(std::abs((controller.playoutOffsetUs() - int64_t(epochUs)) -
                                direction * expectedUs) <= 1,
                       "equal elapsed time must buy equal correction across FPS, including fractional rates");
            }
        }
    }
}

void testOffsetSlewIgnoresWorkerBacklogAndPreservesHistoricalClock()
{
    auto policy = offsetTestPolicy();
    policy.playoutOffsetWindowUs = 1;
    auto historicalPolicy = policy;
    historicalPolicy.playoutOffsetSourceClock = 0;
    VrrTimingController controller(config(60, 120), true, policy);
    VrrTimingController delayed(config(60, 120), true, policy);
    VrrTimingController historical(config(60, 120), true, historicalPolicy);
    constexpr uint64_t epochUs = 1000000;
    controller.schedule(frame(1, 0, true, epochUs), epochUs);
    delayed.schedule(frame(1, 0, true, epochUs), epochUs);
    historical.schedule(frame(1, 0, true, epochUs), epochUs);
    // A two-second worker/GPU stall must not age the source-clock mapping.
    // Historical captures explicitly retain the worker-clock behavior.
    uint64_t nowUs = epochUs + 2000000;
    uint32_t timestamp = 1500;
    uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + 6000;
    controller.schedule(frame(2, timestamp, true, decodedUs), decodedUs);
    delayed.schedule(frame(2, timestamp, true, decodedUs), nowUs);
    historical.schedule(frame(2, timestamp, true, decodedUs), nowUs);
    expect(delayed.playoutOffsetUs() == controller.playoutOffsetUs(),
           "local GPU/worker backlog must not buy source-offset correction");
    expect(historical.playoutOffsetUs() == int64_t(epochUs + 100),
           "captured worker-clock policy must retain its bounded gap step");
    for (int i = 3; i <= 8; ++i) {
        timestamp += 1500;
        decodedUs = decodedTimeForRtp(epochUs, timestamp) + 6000;
        nowUs += 10000;
        controller.schedule(frame(i, timestamp, true, decodedUs), decodedUs);
        delayed.schedule(frame(i, timestamp, true, decodedUs), nowUs);
        expect(delayed.playoutOffsetUs() == controller.playoutOffsetUs(),
               "continued render contention must not perturb mapping age or slew");
    }
    controller.rebase();
    controller.schedule(frame(1, 0, true, epochUs), epochUs);
    expect(controller.playoutOffsetUs() == int64_t(epochUs),
           "a genuine epoch rebase must discard old offset state and credit");
}

void testOffsetRecoveryRejectsTransitionMinimum()
{
    // A source discontinuity can contain an unusually early readiness
    // observation. It must not pull every later target earlier for three
    // seconds, particularly when the following source phase is later.
    for (bool canLatch : {false, true}) {
        auto policy = offsetTestPolicy();
        auto historical = policy;
        historical.playoutOffsetCadenceGate = 0;
        historical.playoutOffsetSlewUsPerSecond = 0;
        VrrTimingController recovered(config(60, 120), canLatch, policy);
        VrrTimingController legacy(config(60, 120), canLatch, historical);
        constexpr uint64_t epochUs = 1000000;
        uint32_t timestamp = 0;
        int number = 0;
        for (int i = 0; i < 180; ++i) {
            timestamp = static_cast<uint32_t>(i * 1500);
            const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
            recovered.schedule(frame(++number, timestamp, true, decodedUs), decodedUs);
            legacy.schedule(frame(number, timestamp, true, decodedUs), decodedUs);
        }
        timestamp += 6000; // 66.7 ms source gap, below the full-rebase bound.
        const uint64_t earlyUs = decodedTimeForRtp(epochUs, timestamp) - 6000;
        const auto transition = recovered.schedule(
            frame(++number, timestamp, true, earlyUs), earlyUs);
        legacy.schedule(frame(number, timestamp, true, earlyUs), earlyUs);
        expect(transition.phaseDiscontinuity && !transition.cadenceEligible && !transition.rebased,
               "the regression fixture must exercise an ineligible phase change, not a full rebase");
        expect(recovered.playoutOffsetUs() == int64_t(epochUs),
               "an ineligible transition minimum must not move the applied offset");
        for (int i = 0; i < 60; ++i) {
            timestamp += 1500;
            const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + 4000;
            const int64_t before = recovered.playoutOffsetUs();
            const auto decision = recovered.schedule(
                frame(++number, timestamp, true, decodedUs), decodedUs);
            legacy.schedule(frame(number, timestamp, true, decodedUs), decodedUs);
            const int64_t after = recovered.playoutOffsetUs();
            expect(after >= before && after - before <= 100,
                   "post-transition offset recovery must be gradual and use the new phase");
            expect(decision.playoutDelayUs == 6000,
                   "transition recovery must not reseed the retained delay");
        }
        expect(recovered.playoutOffsetUs() >= int64_t(epochUs + 2000),
               "fresh stable cadence must recover without waiting for an old minimum to expire");
        expect(legacy.playoutOffsetUs() < int64_t(epochUs - 1000),
               "the historical policy must still reproduce the stale-minimum regression");
    }
}

void testOffsetRecoveryKeepsStartupPaddingAndNativePolicy()
{
    // canLatch=true covers DXGI and persistent Vulkan Mailbox. false covers
    // persistent Vulkan Immediate/FIFO: retain their software safety floor.
    for (bool canLatch : {false, true}) {
        for (int mode : {0, 1, 2}) {
            for (int rateHz : {30, 60, 116, 120}) {
                auto session = config(rateHz, 120);
                session.latencyMode = mode;
                auto policy = vrrTimingParametersForSession(session);
                auto historical = policy;
                historical.playoutOffsetCadenceGate = 0;
                historical.playoutOffsetSlewUsPerSecond = 0;
                VrrTimingController current(session, canLatch, policy);
                VrrTimingController previous(session, canLatch, historical);
                expect(policy.playoutOffsetCadenceGate == 1 &&
                           policy.playoutOffsetSlewUsPerSecond == 2400 &&
                           policy.playoutOffsetMaximumStepUs == 100,
                       "all live presets must select the same bounded mapping policy");
                for (int i = 0; i < 240; ++i) {
                    const auto timestamp = static_cast<uint32_t>(
                        uint64_t(i) * 90000 / uint64_t(rateHz));
                    const uint64_t decodedUs = decodedTimeForRtp(1000000, timestamp);
                    const auto a = current.schedule(frame(i + 1, timestamp, true, decodedUs), decodedUs);
                    const auto b = previous.schedule(frame(i + 1, timestamp, true, decodedUs), decodedUs);
                    expect(a.playoutDelayUs == b.playoutDelayUs && a.targetUs == b.targetUs &&
                               a.latchedPresentation == b.latchedPresentation,
                           "steady startup padding and native protection must not change at any source rate");
                    current.noteSubmission(true, false, a.targetUs);
                    previous.noteSubmission(true, false, b.targetUs);
                    expect(current.earliestSubmissionUs() == previous.earliestSubmissionUs(),
                           "Linux's unprotected-present safety floor must remain intact");
                }
            }
        }
    }
}

void testTimestampModeStillBoundsCatchUpBursts()
{
    VrrSessionConfig session = config(116, 120);
    session.smoothFrameTiming = false;
    VrrTimingParameters policy = legacyPlayoutParameters(session);
    policy.playoutDelayAdaptive = 0;
    VrrTimingController controller(session, false, policy);
    const uint64_t epochUs = 1000000;
    const auto present = [&](int number, uint32_t stamp, uint64_t readyUs) {
        const VrrTimingDecision decision = controller.schedule(
            frame(number, stamp, true, readyUs), readyUs);
        controller.noteSubmission(true, false, decision.targetUs);
        return decision;
    };

    present(1, 0, epochUs);
    // A 10 ms source interval, with the second frame arriving 8 ms late.
    const VrrTimingDecision late = present(2, 900, epochUs + 18000);
    const uint64_t floorUs = controller.earliestSubmissionUs();
    // The next frame arrives on time and must not catch up in only 5 ms.
    const VrrTimingDecision next = present(3, 1800, epochUs + 20000);
    expect(next.targetUs >= floorUs &&
               next.targetUs >= late.targetUs + controller.displayPeriodUs() +
                                   controller.guardUs(),
           "timestamp mode must retain the adaptive presentation spacing guard");
    expect(next.targetUs > epochUs + 20000 + policy.sourcePlayoutDelayUs,
           "display limits must override an infeasibly close host timestamp target");
    expect(next.targetUs == floorUs,
           "timestamp mode must not add latency beyond the required spacing floor");
    expect(next.cadenceSmoothingUs == 0 && controller.timestampPlayoutActive(),
           "protecting a catch-up burst must not re-enable cadence smoothing");
}

void testCadenceSmoothingEvensJitteredSource()
{
    // A steady 60 FPS game whose host stamps jitter +-3 ms per frame. With
    // smoothing off the presented intervals copy that jitter; with the
    // session policy they must be far more even, at a bounded lag behind
    // the raw slot, without ever re-anchoring.
    VrrSessionConfig session = config(60, 120);
    const uint64_t epochUs = 1000000;
    const auto stampFor = [](int i) {
        // Deterministic jitter in [-3000, 3000) us on the sender stamp.
        const int64_t jitterUs =
            static_cast<int64_t>((static_cast<uint64_t>(i) * 7919ULL) % 6000ULL) -
            3000;
        // Start 10 ms in so the jitter never makes the first stamp wrap.
        const int64_t idealUs = 10000 + static_cast<int64_t>(i) * 1000000LL / 60LL;
        return static_cast<uint32_t>((idealUs + jitterUs) * 90LL / 1000LL);
    };
    const auto run = [&](uint64_t gainPerMille, double& spreadUs,
                         int64_t& maximumLagUs, unsigned int& resets) {
        VrrTimingParameters policy = legacyPlayoutParameters(session);
        policy.playoutDelayAdaptive = 0;
        policy.sourcePlayoutDelayUs = 8000;
        policy.playoutSmoothingGainPerMille = gainPerMille;
        // The legacy gain smoother is what older captures replay under.
        policy.playoutMetronomeEnabled = 0;
        VrrTimingController controller(session, true, policy);
        std::vector<int64_t> intervals;
        uint64_t previousTargetUs = 0;
        maximumLagUs = 0;
        resets = 0;
        for (int i = 0; i < 600; ++i) {
            const uint32_t timestamp = stampFor(i);
            // The frame decodes a fixed transport delay after its stamp.
            const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + 500;
            const VrrTimingDecision decision = controller.schedule(
                frame(i + 1, timestamp, true, decodedUs), decodedUs);
            controller.noteSubmission(true, false, decision.targetUs);
            if (i > 0 && (decision.rebased || decision.phaseDiscontinuity)) {
                ++resets;
            }
            if (i >= 100) {
                intervals.push_back(static_cast<int64_t>(decision.targetUs) -
                                    static_cast<int64_t>(previousTargetUs));
                maximumLagUs = std::max(maximumLagUs,
                                        decision.cadenceSmoothingUs);
            }
            previousTargetUs = decision.targetUs;
        }
        double mean = 0;
        for (int64_t value : intervals) mean += static_cast<double>(value);
        mean /= static_cast<double>(intervals.size());
        double variance = 0;
        for (int64_t value : intervals) {
            variance += (static_cast<double>(value) - mean) *
                (static_cast<double>(value) - mean);
        }
        spreadUs = std::sqrt(variance / static_cast<double>(intervals.size()));
    };
    double rawSpreadUs = 0;
    double smoothSpreadUs = 0;
    int64_t rawLagUs = 0;
    int64_t smoothLagUs = 0;
    unsigned int rawResets = 0;
    unsigned int smoothResets = 0;
    run(0, rawSpreadUs, rawLagUs, rawResets);
    run(150, smoothSpreadUs, smoothLagUs, smoothResets);
    std::fprintf(stderr,
                 "cadence smoothing: raw spread=%.0f us lag=%lld resets=%u; smoothed spread=%.0f us lag=%lld resets=%u\n",
                 rawSpreadUs, static_cast<long long>(rawLagUs), rawResets,
                 smoothSpreadUs, static_cast<long long>(smoothLagUs),
                 smoothResets);
    expect(rawSpreadUs > 1500.0,
           "with smoothing off the presented intervals must copy the sender jitter");
    expect(smoothSpreadUs < rawSpreadUs / 3.0,
           "the session smoother must cut the presented interval spread by at least 3x");
    expect(smoothLagUs <= 8000 && smoothLagUs > 0,
           "the smoothed schedule must lag the raw slot by at most the configured maximum");
    expect(rawResets == 0 && smoothResets == 0,
           "sender stamp jitter must never re-anchor the timestamp clock");
}

void testAdaptivePlayoutDelaySlewsAcrossBands()
{
    VrrSessionConfig session = config(116, 120);
    VrrTimingParameters policy = legacyPlayoutParameters(session);
    policy.playoutHistoryEnabled = 0;
    policy.playoutPerFrameLatch = 0;
    VrrTimingController controller(session, true, policy);
    const uint64_t epochUs = 1000000;
    const auto rtpFor = [](int i, int rateHz) {
        return static_cast<uint32_t>(
            static_cast<uint64_t>(i) * 90000ULL /
            static_cast<uint64_t>(rateHz));
    };
    // Uniform-ish jitter in [0, 3000) us with a zero every 50th frame so the
    // mapping floor stays fixed: p98 is about 2940 us.
    const auto jitterFor = [](int i) {
        if (i % 50 == 0) {
            return static_cast<uint64_t>(0);
        }
        return static_cast<uint64_t>((static_cast<uint64_t>(i) * 7919ULL) % 3001ULL);
    };

    VrrTimingDecision decision;
    bool startHeld = true;
    uint64_t maximumStepUs = 0;
    uint64_t previousDelayUs = 0;
    constexpr int kReleaseFrames = 2600;
    for (int i = 0; i < kReleaseFrames; ++i) {
        const uint32_t timestamp = rtpFor(i, 116);
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + jitterFor(i);
        decision = controller.schedule(frame(i + 1, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        // The start value is two fitted source periods; the reservoir opens
        // on the negotiated 120 FPS period and the fitted 116 FPS one
        // follows, so both scaled starts are acceptable.
        const uint64_t scaledStartUs =
            decision.sourcePeriodUs * policy.playoutDelayStartPeriodPerMille / 1000;
        if (i < 400 && decision.playoutDelayUs < scaledStartUs - 500) {
            startHeld = false;
        }
        if (i > 0) {
            const uint64_t stepUs = decision.playoutDelayUs > previousDelayUs ?
                decision.playoutDelayUs - previousDelayUs :
                previousDelayUs - decision.playoutDelayUs;
            maximumStepUs = std::max(maximumStepUs, stepUs);
        }
        previousDelayUs = decision.playoutDelayUs;
    }
    expect(startHeld,
           "the delay must hold the start value until the reservoir has enough samples to release");
    expect(controller.playoutBandIndex() == 116 / 20,
           "the band must follow the fitted source rate");
    const uint64_t expectedUs = 2940 + policy.playoutDelayMarginUs;
    expect(decision.playoutDelayUs >= expectedUs - 200 &&
               decision.playoutDelayUs <= expectedUs + 200,
           "the delay must converge to the target lateness percentile plus margin");
    expect(maximumStepUs <= std::max(policy.playoutDelayAttackUs,
                                     policy.playoutDelayReleaseUs),
           "the delay must never move more than one slew step per frame within a band");
    // The target is built with the delay in force when its slot was mapped;
    // the budget reports the calibrator's latest value, one slew step on.
    const uint64_t budgetGapUs = controller.timingBudgetUs() > decision.playoutDelayUs ?
        controller.timingBudgetUs() - decision.playoutDelayUs :
        decision.playoutDelayUs - controller.timingBudgetUs();
    expect(budgetGapUs <= std::max(policy.playoutDelayAttackUs,
                                   policy.playoutDelayReleaseUs),
           "the timing budget must report the applied adaptive delay");

    // A worse regime: one frame in ten arrives 6 ms late. The delay must
    // attack upward at the attack rate, never in one jump.
    const uint64_t delayBeforeUs = decision.playoutDelayUs;
    uint64_t maximumRiseUs = 0;
    for (int i = kReleaseFrames; i < kReleaseFrames + 1000; ++i) {
        const uint32_t timestamp = rtpFor(i, 116);
        const uint64_t lateUs = (i % 10 == 0) ? 6000 : 0;
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + jitterFor(i) + lateUs;
        decision = controller.schedule(frame(i + 1, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        if (decision.playoutDelayUs > previousDelayUs) {
            maximumRiseUs = std::max(maximumRiseUs, decision.playoutDelayUs - previousDelayUs);
        }
        previousDelayUs = decision.playoutDelayUs;
    }
    expect(decision.playoutDelayUs > delayBeforeUs + 2000,
           "a sustained late regime must raise the delay");
    expect(maximumRiseUs <= policy.playoutDelayAttackUs,
           "the delay must rise at most one attack step per frame");

    // A cadence change to 60 FPS opens a new band at its start value, and
    // the delay in force walks there one slew step per frame.
    const int lastFrame = kReleaseFrames + 1000;
    const uint64_t baseUs = decodedTimeForRtp(epochUs, rtpFor(lastFrame, 116));
    const uint64_t delayAtChangeUs = decision.playoutDelayUs;
    uint64_t maximumStepAcrossRatesUs = 0;
    bool rateChanged = false;
    for (int i = 0; i < 200; ++i) {
        const uint32_t timestamp = rtpFor(lastFrame, 116) + rtpFor(i, 60);
        const uint64_t decodedUs = baseUs + static_cast<uint64_t>(i) * 1000000ULL / 60ULL;
        decision = controller.schedule(frame(lastFrame + 1 + i, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        rateChanged = rateChanged || decision.sourceRateChanged;
        const uint64_t stepUs = decision.playoutDelayUs > previousDelayUs ?
            decision.playoutDelayUs - previousDelayUs :
            previousDelayUs - decision.playoutDelayUs;
        maximumStepAcrossRatesUs = std::max(maximumStepAcrossRatesUs, stepUs);
        previousDelayUs = decision.playoutDelayUs;
    }
    std::fprintf(stderr, "band slew: rateChanged=%d band=%u period=%llu delay at change=%llu after=%llu max step=%llu\n",
                 rateChanged ? 1 : 0, controller.playoutBandIndex(),
                 static_cast<unsigned long long>(controller.sourcePeriodUs()),
                 static_cast<unsigned long long>(delayAtChangeUs),
                 static_cast<unsigned long long>(decision.playoutDelayUs),
                 static_cast<unsigned long long>(maximumStepAcrossRatesUs));
    // A gradual glide of the endpoint fit never crosses the material
    // threshold in one step, so the flag stays clear; the period itself
    // must arrive at the new rate.
    (void) rateChanged;
    expect(std::abs(static_cast<int64_t>(controller.sourcePeriodUs()) - 16667) < 100 &&
               controller.playoutBandIndex() == 60 / 20,
           "a material rate change must move to the new band");
    expect(maximumStepAcrossRatesUs <= std::max(policy.playoutDelayAttackUs,
                                                policy.playoutDelayReleaseUs),
           "a band change must never move the delay by more than one slew step");
    expect(decision.playoutDelayUs > delayAtChangeUs + 4000,
           "the delay must walk toward the new band's start value");
}

void testPrepareOnArrivalSpendsTheCushion()
{
    // With preparation on arrival a frame that arrives on time starts its
    // preparation at once: the render start sits a whole playout delay
    // before the usual lead, at the frame's mapped slot.
    VrrSessionConfig session = config(116, 120);
    VrrTimingParameters policy = legacyPlayoutParameters(session);
    policy.playoutPrepareOnArrival = 1;
    policy.renderStartAfterSubmissionUs = 0;
    VrrTimingController controller(session, true, policy);
    const uint64_t epochUs = 1000000;
    VrrTimingDecision decision;
    for (int i = 0; i < 300; ++i) {
        const uint32_t timestamp = static_cast<uint32_t>(static_cast<uint64_t>(i) * 90000ULL / 116ULL);
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + 700;
        decision = controller.schedule(frame(i + 1, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
    }
    expect(decision.renderStartUs + decision.renderLeadUs +
               decision.renderWakeLeadUs + decision.playoutDelayUs ==
               decision.targetUs,
           "the render start must precede the target by the lead plus the playout delay");
    expect(decision.renderStartUs <= decision.sourceTimeUs + 1,
           "an on-time frame must be ready to prepare at its mapped slot");
    expect(decision.playoutDelayUs >= 6000,
           "the whole-tail cushion must not release below the start before it has evidence");
}

void testRenderStartKeepsClearOfPreviousPresent()
{
    // Preparation must not begin within the acquire-blocking window after
    // the previous present, but it must keep the minimum lead before the
    // target when the interval is too short for both.
    VrrSessionConfig session = config(116, 120);
    const VrrTimingParameters policy = legacyPlayoutParameters(session);
    VrrTimingController controller(session, true, policy);
    const uint64_t epochUs = 1000000;
    VrrTimingDecision decision;
    uint64_t lastSubmissionUs = 0;
    bool clear = true;
    bool leadKept = true;
    for (int i = 0; i < 300; ++i) {
        const uint32_t timestamp = static_cast<uint32_t>(static_cast<uint64_t>(i) * 90000ULL / 116ULL);
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + 700;
        decision = controller.schedule(frame(i + 1, timestamp, true, decodedUs), decodedUs);
        if (i > 50) {
            const uint64_t gapUs = decision.renderStartUs - lastSubmissionUs;
            const uint64_t leadUs = decision.targetUs - decision.renderStartUs;
            // Inside the blocking window only when the minimum lead forced it.
            if (gapUs < policy.renderStartAfterSubmissionUs &&
                    leadUs > policy.renderStartMinimumLeadUs) {
                clear = false;
            }
            // The guard never leaves less than the minimum lead; a lead
            // already shorter than that was the learned lead, not the guard.
            if (gapUs >= policy.renderStartAfterSubmissionUs &&
                    leadUs < policy.renderStartMinimumLeadUs &&
                    decision.renderLeadUs + decision.renderWakeLeadUs >=
                        policy.renderStartMinimumLeadUs) {
                leadKept = false;
            }
        }
        controller.noteSubmission(true, false, decision.targetUs);
        lastSubmissionUs = decision.targetUs;
    }
    expect(clear,
           "preparation must start no sooner than the acquire-safe gap after the previous present");
    expect(leadKept,
           "preparation must keep the minimum lead before the target");
}

void testShortHitchDoesNotRefitSourceRate()
{
    // Four host frames stamped 33 ms apart are a hitch, not a 30 Hz source.
    // The fitted rate, the metronome period and the delay must hold; a real
    // cutscene that keeps the slow cadence for longer is still accepted.
    VrrSessionConfig session = config(116, 120);
    const VrrTimingParameters policy = legacyPlayoutParameters(session);
    VrrTimingController controller(session, true, policy);
    const uint64_t epochUs = 1000000;
    int frameNumber = 0;
    uint32_t timestamp = 0;
    VrrTimingDecision decision;
    for (int i = 0; i < 700; ++i) {
        timestamp = static_cast<uint32_t>(static_cast<uint64_t>(i) * 90000ULL / 116ULL);
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + 800;
        decision = controller.schedule(frame(++frameNumber, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
    }
    const uint64_t stablePeriodUs = controller.sourcePeriodUs();
    const uint64_t stableDelayUs = decision.playoutDelayUs;

    bool refitted = false;
    uint64_t maximumDelayStepUs = 0;
    uint64_t previousDelayUs = stableDelayUs;
    for (int i = 0; i < 4; ++i) {
        timestamp += 3000;
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + 800;
        decision = controller.schedule(frame(++frameNumber, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        refitted = refitted || decision.sourceRateChanged;
        maximumDelayStepUs = std::max(maximumDelayStepUs,
            uint64_t(std::abs(int64_t(decision.playoutDelayUs) - int64_t(previousDelayUs))));
        previousDelayUs = decision.playoutDelayUs;
    }
    for (int i = 0; i < 100; ++i) {
        timestamp += 776;
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + 800;
        decision = controller.schedule(frame(++frameNumber, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        refitted = refitted || decision.sourceRateChanged;
        const uint64_t stepUs = decision.playoutDelayUs > previousDelayUs ?
            decision.playoutDelayUs - previousDelayUs :
            previousDelayUs - decision.playoutDelayUs;
        maximumDelayStepUs = std::max(maximumDelayStepUs, stepUs);
        previousDelayUs = decision.playoutDelayUs;
    }
    expect(!refitted,
           "a four-frame hitch must not be accepted as a new source rate");
    expect(std::abs(static_cast<int64_t>(controller.sourcePeriodUs()) -
                    static_cast<int64_t>(stablePeriodUs)) < 100,
           "a four-frame hitch must leave the fitted source period alone");
    expect(maximumDelayStepUs <= std::max(policy.playoutDelayAttackUs,
                                          policy.playoutDelayReleaseUs),
           "a hitch must never move the playout delay by more than one slew step");

    VrrTimingDecision cutscene;
    bool accepted = false;
    for (int i = 0; i < 15; ++i) {
        timestamp += 3000;
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp) + 800;
        cutscene = controller.schedule(frame(++frameNumber, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, cutscene.targetUs);
        accepted = accepted || cutscene.sourceRateChanged;
    }
    expect(accepted &&
               std::abs(1000000.0 /
                   static_cast<double>(controller.sourcePeriodUs()) - 30.0) < 0.5,
           "a 30 FPS cutscene must be accepted once it has lasted 200 ms");
}

void testMotionDeadbandHonorsStampSteps()
{
    // Small stamp wobble is capture noise the grid absorbs. A stamp that
    // departs from the grid by more than the learned bound is the host
    // reporting a change in frame timing: the frame presents on its stamp
    // and the grid restarts there instead of paying the difference back
    // over dozens of frames.
    // A 60 FPS stream on the 120 Hz panel, so the display floor never
    // stands between a stamp and its slot. Replay-only policy.
    VrrSessionConfig session = config(60, 120);
    VrrTimingParameters policy = metronomePolicy(session);
    policy.playoutMotionDeadbandEnabled = 1;
    // One reservoir, so the slowdown below is not also a band change.
    policy.playoutBandWidthHz = 1000;
    VrrTimingController controller(session, true, policy);
    const uint64_t epochUs = 1000000;
    const int64_t periodUs = 1000000 / 60;
    const auto wobbleUs = [](int i) {
        return static_cast<int64_t>((static_cast<uint64_t>(i) * 7919ULL) % 600ULL) - 300;
    };
    int64_t stampUs = 20000;
    VrrTimingDecision decision;
    uint64_t previousTargetUs = 0;
    int64_t worstSteadyDeviationUs = 0;
    int frameNumber = 0;
    const auto step = [&](int64_t stampIntervalUs, int64_t jitterUs) {
        stampUs += stampIntervalUs;
        const uint32_t timestamp = static_cast<uint32_t>((stampUs + jitterUs) * 90LL / 1000LL);
        const uint64_t decodedUs = static_cast<uint64_t>(
            static_cast<int64_t>(epochUs) + stampUs + jitterUs + 900);
        decision = controller.schedule(frame(++frameNumber, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
    };
    for (int i = 0; i < 600; ++i) {
        step(periodUs, wobbleUs(i));
        if (i >= 300) {
            const int64_t deviationUs = static_cast<int64_t>(decision.targetUs) -
                static_cast<int64_t>(previousTargetUs) - periodUs;
            worstSteadyDeviationUs = std::max(
                worstSteadyDeviationUs, deviationUs < 0 ? -deviationUs : deviationUs);
        }
        previousTargetUs = decision.targetUs;
    }
    expect(worstSteadyDeviationUs < 300,
           "stamp wobble inside the deadband must not reach the presented cadence");

    // The game's phase steps 3 ms earlier than the grid would predict.
    // The grid must re-anchor within a frame or two rather than walk the
    // offset back at the bounded step.
    step(periodUs - 3000, 0);
    previousTargetUs = decision.targetUs;
    int64_t worstResidualUs = 0;
    std::fprintf(stderr, "deadband phase step residuals:");
    for (int i = 0; i < 40; ++i) {
        step(periodUs, wobbleUs(i));
        std::fprintf(stderr, " %lld", static_cast<long long>(decision.cadenceSmoothingUs));
        if (i >= 3) {
            const int64_t residualUs = decision.cadenceSmoothingUs;
            worstResidualUs = std::max(worstResidualUs,
                                       residualUs < 0 ? -residualUs : residualUs);
        }
        previousTargetUs = decision.targetUs;
    }
    std::fprintf(stderr, "\n");
    expect(worstResidualUs < 800,
           "after a phase step the grid must sit on the stamps again within a few frames");

    // The game slows to 24 ms frames. Every slowed frame must present on
    // its own stamp: the interval follows the stamp interval at once.
    int64_t worstSlowIntervalErrorUs = 0;
    std::fprintf(stderr, "deadband slow intervals:");
    for (int i = 0; i < 20; ++i) {
        step(24000, wobbleUs(i));
        const int64_t intervalUs = static_cast<int64_t>(decision.targetUs) -
            static_cast<int64_t>(previousTargetUs);
        const int64_t errorUs = intervalUs - 24000;
        std::fprintf(stderr, " %lld/%lld", static_cast<long long>(intervalUs),
                     static_cast<long long>(decision.cadenceSmoothingUs));
        if (i >= 1) {
            worstSlowIntervalErrorUs = std::max(
                worstSlowIntervalErrorUs, errorUs < 0 ? -errorUs : errorUs);
        }
        previousTargetUs = decision.targetUs;
    }
    std::fprintf(stderr, "\n");
    expect(worstSlowIntervalErrorUs < 700,
           "a real change in frame timing must present on the stamp, not on the old grid");
}

void testSmoothnessLearningWindowTracksCadence()
{
    // The quarter-second learning window remains a replay-selectable policy
    // even though the production smoothness session no longer uses it.
    const auto learnedSampleCount = [](int sourceRateHz) {
        VrrSessionConfig session = config(sourceRateHz, 240);
        session.allowAdditionalQueuedFrame = true;
        VrrTimingParameters parameters;
        parameters.readinessLearningWindowUs = 250000;
        parameters.readinessLearningSamples = 120;
        VrrTimingController controller(session, true, parameters);
        const uint64_t epochUs = 100000;
        for (int i = 0; i < 140; ++i) {
            const uint32_t timestamp = static_cast<uint32_t>(
                static_cast<uint64_t>(i) * 90000ULL /
                static_cast<uint64_t>(sourceRateHz));
            const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
            const VrrTimingDecision decision = controller.schedule(
                frame(i + 1, timestamp, true, decodedUs), decodedUs);
            controller.noteSubmission(true, false, decision.targetUs);
        }
        return controller.diagnostics().readinessSamples;
    };

    const size_t samples60 = learnedSampleCount(60);
    const size_t samples120 = learnedSampleCount(120);
    expect(samples60 == 16,
           "the quarter-second readiness window must retain its robust 16-sample floor at 60 FPS");
    expect(samples120 >= 29 && samples120 <= 31,
           "the quarter-second readiness window must contain about 30 samples at 120 FPS");
}

void testLongRunNearRefreshRtpCadence()
{
    constexpr int streamRateHz = 116;
    constexpr uint64_t rtpClockHz = 90000;
    constexpr uint64_t microsecondsPerSecond = 1000000;
    constexpr uint64_t initialUs = 1000000;
    VrrTimingController controller(config(streamRateHz, 120));
    controller.schedule(frame(0, 0, true, initialUs), initialUs);

    uint64_t maximumSourceErrorUs = 0;
    bool sawRebase = false;
    for (int i = 1; i <= 2000; ++i) {
        const uint32_t timestamp = static_cast<uint32_t>(
            (static_cast<uint64_t>(i) * rtpClockHz) / streamRateHz);
        const uint64_t expectedSourceUs = initialUs +
            (static_cast<uint64_t>(i) * microsecondsPerSecond) / streamRateHz;
        VrrTimingDecision decision = controller.schedule(
            frame(i, timestamp, true, expectedSourceUs), expectedSourceUs);
        sawRebase = sawRebase || decision.rebased;
        const uint64_t sourceErrorUs = decision.sourceTimeUs > expectedSourceUs ?
            decision.sourceTimeUs - expectedSourceUs :
            expectedSourceUs - decision.sourceTimeUs;
        maximumSourceErrorUs = std::max(maximumSourceErrorUs, sourceErrorUs);
    }

    expect(!sawRebase,
           "steady near-refresh RTP cadence must not rebase");
    expect(maximumSourceErrorUs <= 12,
           "90 kHz timestamp quantization must not accumulate source-clock drift");
}

void testQuantizedCadenceDoesNotOscillate()
{
    constexpr uint64_t epochUs = 1000000;
    struct CadenceCase {
        int streamRateHz;
        int captureRateHz;
        int displayRefreshHz;
    };
    const CadenceCase cases[] = {
        {59, 60, 60},
        {116, 120, 120},
        {138, 144, 144},
        {480, 480, 960},
    };

    for (const CadenceCase& cadence : cases) {
        VrrTimingController controller(config(cadence.streamRateHz,
                                              cadence.displayRefreshHz));
        VrrTimingDecision decision = controller.schedule(
            frame(0, 0, true, epochUs), epochUs);
        controller.noteSubmission(true, false, decision.targetUs);

        const uint64_t expectedPeriodUs =
            (1000000ULL + static_cast<uint64_t>(cadence.streamRateHz) / 2) /
            static_cast<uint64_t>(cadence.streamRateHz);
        uint64_t minimumPeriodUs = std::numeric_limits<uint64_t>::max();
        uint64_t maximumPeriodUs = 0;
        int64_t minimumReadyOffsetUs = std::numeric_limits<int64_t>::max();
        int64_t maximumReadyOffsetUs = std::numeric_limits<int64_t>::min();
        uint64_t maximumTimingBudgetUs = 0;
        bool sawRebase = false;
        const int warmupFrames = cadence.streamRateHz * 4;
        const int frameCount = cadence.streamRateHz * 12;

        for (int i = 1; i <= frameCount; ++i) {
            const uint32_t timestamp = quantizedRtpTimestamp(
                i, cadence.streamRateHz, cadence.captureRateHz);
            const uint64_t decodedUs = idealDecodedTime(
                epochUs, i, cadence.streamRateHz);
            decision = controller.schedule(
                frame(i, timestamp, true, decodedUs), decodedUs);
            controller.noteSubmission(true, false, decision.targetUs);
            sawRebase = sawRebase || decision.rebased;

            if (i > warmupFrames) {
                minimumPeriodUs = std::min(minimumPeriodUs,
                                            decision.sourcePeriodUs);
                maximumPeriodUs = std::max(maximumPeriodUs,
                                            decision.sourcePeriodUs);
                minimumReadyOffsetUs = std::min(minimumReadyOffsetUs,
                                                decision.readyOffsetUs);
                maximumReadyOffsetUs = std::max(maximumReadyOffsetUs,
                                                decision.readyOffsetUs);
                maximumTimingBudgetUs = std::max(
                    maximumTimingBudgetUs, controller.timingBudgetUs());
            }
        }

        const uint64_t readyOffsetSpanUs =
            maximumReadyOffsetUs > minimumReadyOffsetUs ?
                static_cast<uint64_t>(maximumReadyOffsetUs -
                                      minimumReadyOffsetUs) : 0;
        if (minimumPeriodUs != expectedPeriodUs ||
                maximumPeriodUs != expectedPeriodUs ||
                readyOffsetSpanUs > 2 ||
                maximumTimingBudgetUs > 2000) {
            std::fprintf(stderr,
                         "quantized cadence %d/%d: period=%llu..%llu "
                         "expected=%llu phase-span=%llu budget=%llu\n",
                         cadence.streamRateHz, cadence.captureRateHz,
                         static_cast<unsigned long long>(minimumPeriodUs),
                         static_cast<unsigned long long>(maximumPeriodUs),
                         static_cast<unsigned long long>(expectedPeriodUs),
                         static_cast<unsigned long long>(readyOffsetSpanUs),
                         static_cast<unsigned long long>(maximumTimingBudgetUs));
        }
        expect(!sawRebase,
               "steady host-quantized cadence must not rebase");
        expect(minimumPeriodUs == expectedPeriodUs &&
                   maximumPeriodUs == expectedPeriodUs,
               "host-quantized cadence must retain one exact learned period");
        expect(readyOffsetSpanUs <= 2,
               "host-quantized cadence must not create millisecond source phase motion");
        expect(maximumTimingBudgetUs <= 2000,
               "host-quantized cadence must not create a multi-millisecond readiness reserve");
    }
}

void testHighRefreshCalibrationBandsHaveNoCadenceCliff()
{
    constexpr uint64_t epochUs = 1000000;
    const int64_t stampJitterUs[] = {
        0, 700, -500, 900, -800, 400, -200, 600, -600, 200,
    };
    struct Sweep {
        int displayRefreshHz;
        int minimumSourceRateHz;
        int maximumSourceRateHz;
    };
    // 115..139 covers the reported 120..127 trouble range on a 144 Hz
    // display and crosses the 120 Hz reservoir edge.  A 240 Hz display lets
    // us cross every subsequent 20 Hz edge through 220 without asking the
    // physical 120 Hz development panel to present an impossible cadence.
    const Sweep sweeps[] = {
        {144, 115, 139},
        {240, 115, 227},
    };

    uint64_t worstP90JerkUs = 0;
    uint64_t worstDelayStepUs = 0;
    int worstRateHz = 0;
    int worstDisplayHz = 0;
    bool sawUnexpectedReset = false;
    bool sawWrongBand = false;
    bool sawBandOscillation = false;
    bool sawUntrainedBand = false;

    for (const Sweep& sweep : sweeps) {
        for (int rateHz = sweep.minimumSourceRateHz;
             rateHz <= sweep.maximumSourceRateHz; ++rateHz) {
            const VrrSessionConfig session = config(
                rateHz, sweep.displayRefreshHz);
            VrrTimingParameters policy =
                legacyPlayoutParameters(session);
            policy.playoutHistoryEnabled = 0;
            policy.playoutPerFrameLatch = 0;
            VrrTimingController controller(session, true, policy);
            std::vector<uint64_t> jerksUs;
            uint64_t previousTargetUs = 0;
            uint64_t previousIntervalUs = 0;
            uint64_t previousDelayUs = 0;
            unsigned int steadyBand = 0;
            bool haveSteadyBand = false;
            const int warmupFrames = rateHz * 4;
            const int frameCount = rateHz * 8;

            for (int i = 0; i <= frameCount; ++i) {
                const int64_t idealStampUs = 10000 +
                    static_cast<int64_t>(i) * 1000000LL / rateHz;
                const int64_t jitterUs =
                    stampJitterUs[i % (sizeof(stampJitterUs) /
                                        sizeof(stampJitterUs[0]))];
                const uint32_t timestamp = static_cast<uint32_t>(
                    (idealStampUs + jitterUs) * 90LL / 1000LL);
                const uint64_t arrivalJitterUs = i % 97 == 0 ? 0 :
                    (static_cast<uint64_t>(i) * 1291ULL) % 3001ULL;
                const uint64_t decodedUs =
                    decodedTimeForRtp(epochUs, timestamp) + arrivalJitterUs;
                const VrrTimingDecision decision = controller.schedule(
                    frame(i, timestamp, true, decodedUs), decodedUs);
                controller.noteSubmission(true, false, decision.targetUs);

                if (i > warmupFrames &&
                        (decision.rebased || decision.phaseDiscontinuity ||
                         decision.sourceRateChanged)) {
                    sawUnexpectedReset = true;
                }
                if (i > warmupFrames) {
                    if (!haveSteadyBand) {
                        steadyBand = controller.playoutBandIndex();
                        haveSteadyBand = true;
                    }
                    else if (controller.playoutBandIndex() != steadyBand) {
                        sawBandOscillation = true;
                    }
                }
                if (i > 0) {
                    const uint64_t delayStepUs =
                        decision.playoutDelayUs > previousDelayUs ?
                            decision.playoutDelayUs - previousDelayUs :
                            previousDelayUs - decision.playoutDelayUs;
                    worstDelayStepUs = std::max(worstDelayStepUs,
                                                delayStepUs);
                }
                if (i > warmupFrames && previousTargetUs != 0) {
                    const uint64_t intervalUs =
                        decision.targetUs - previousTargetUs;
                    if (previousIntervalUs != 0) {
                        jerksUs.push_back(intervalUs > previousIntervalUs ?
                            intervalUs - previousIntervalUs :
                            previousIntervalUs - intervalUs);
                    }
                    previousIntervalUs = intervalUs;
                }
                previousTargetUs = decision.targetUs;
                previousDelayUs = decision.playoutDelayUs;
            }

            std::sort(jerksUs.begin(), jerksUs.end());
            const size_t p90Index = jerksUs.empty() ? 0 :
                (jerksUs.size() * 90 + 99) / 100 - 1;
            const uint64_t p90JerkUs = jerksUs.empty() ? 0 :
                jerksUs[p90Index];
            if (p90JerkUs > worstP90JerkUs) {
                worstP90JerkUs = p90JerkUs;
                worstRateHz = rateHz;
                worstDisplayHz = sweep.displayRefreshHz;
            }
            const unsigned int expectedBand =
                static_cast<unsigned int>(rateHz) /
                    std::max<uint64_t>(1, policy.playoutBandWidthHz);
            const unsigned int actualBand = controller.playoutBandIndex();
            const uint64_t hysteresisHz = std::max<uint64_t>(
                1, policy.playoutBandWidthHz / 6);
            // An exact lower edge may deliberately remain in the lower band
            // until it clears the hysteresis shoulder. That is the stable
            // result, not a misclassification or a calibration cliff.
            const bool heldBelowUpperEdge = expectedBand != 0 &&
                actualBand + 1 == expectedBand &&
                static_cast<uint64_t>(rateHz) <
                    static_cast<uint64_t>(expectedBand) *
                        policy.playoutBandWidthHz + hysteresisHz;
            if (actualBand != expectedBand && !heldBelowUpperEdge) {
                std::fprintf(stderr,
                             "high-refresh band mismatch: %d/%d Hz settled in %u, nominal %u, learned period=%llu us\n",
                             rateHz, sweep.displayRefreshHz,
                             actualBand, expectedBand,
                             static_cast<unsigned long long>(
                                 controller.sourcePeriodUs()));
                sawWrongBand = true;
            }
            sawUntrainedBand = sawUntrainedBand ||
                controller.playoutBandSamples() <
                    policy.playoutDelayReleaseSamples;
        }
    }

    std::fprintf(stderr,
                 "high-refresh band sweep: worst p90 jerk=%llu us at %d/%d Hz, max delay step=%llu us\n",
                 static_cast<unsigned long long>(worstP90JerkUs),
                 worstRateHz, worstDisplayHz,
                 static_cast<unsigned long long>(worstDelayStepUs));
    expect(!sawUnexpectedReset,
           "steady 115..227 FPS sources must not reset or refit after warmup");
    expect(!sawWrongBand,
           "every high-refresh source rate must settle in its expected calibration band");
    expect(!sawBandOscillation,
           "a steady source near a calibration edge must not oscillate between bands");
    expect(!sawUntrainedBand,
           "every high-refresh calibration band must collect enough samples to release its start delay");
    expect(worstDelayStepUs <= 50,
           "adaptive delay must not introduce a cadence cliff at any high-refresh band");
    expect(worstP90JerkUs <= 1000,
           "production smoothing must keep p90 presented jerk below 1 ms across high-refresh bands");
}

void testReported120HzBandBoundarySlewsCalibration()
{
    struct Result {
        uint64_t maximumDelayStepUs = 0;
        bool sawLowerBand = false;
        bool returnedToUpperBand = false;
    };
    const auto run = [](VrrTimingParameters policy) {
        constexpr uint64_t epochUs = 1000000;
        VrrTimingController controller(config(127, 144), true, policy);
        Result result;
        long double rtpTicks = 9000.0L;
        uint64_t previousDelayUs = 0;
        int frameNumber = 0;

        const auto runRate = [&](int rateHz, int frameCount,
                                 uint64_t arrivalJitterLimitUs) {
            for (int i = 0; i < frameCount; ++i) {
                rtpTicks += 90000.0L / static_cast<long double>(rateHz);
                const uint32_t timestamp = static_cast<uint32_t>(
                    std::llround(rtpTicks));
                const uint64_t arrivalJitterUs =
                    (static_cast<uint64_t>(frameNumber) * 1291ULL) %
                    (arrivalJitterLimitUs + 1);
                const uint64_t decodedUs =
                    decodedTimeForRtp(epochUs, timestamp) + arrivalJitterUs;
                const VrrTimingDecision decision = controller.schedule(
                    frame(frameNumber, timestamp, true, decodedUs), decodedUs);
                controller.noteSubmission(true, false, decision.targetUs);
                if (frameNumber != 0) {
                    const uint64_t stepUs =
                        decision.playoutDelayUs > previousDelayUs ?
                            decision.playoutDelayUs - previousDelayUs :
                            previousDelayUs - decision.playoutDelayUs;
                    result.maximumDelayStepUs = std::max(
                        result.maximumDelayStepUs, stepUs);
                }
                previousDelayUs = decision.playoutDelayUs;
                ++frameNumber;

                if (controller.playoutBandIndex() == 5) {
                    result.sawLowerBand = true;
                }
                else if (result.sawLowerBand &&
                         controller.playoutBandIndex() == 6) {
                    result.returnedToUpperBand = true;
                }
            }
        };

        // Settle below the 120 Hz edge with a clean arrival tail, then move
        // into the reported 120..127 FPS range with a materially different
        // tail. The old direct band assignment exposed that difference as a
        // single-frame timing jump.
        runRate(116, 1800, 900);
        runRate(127, 1800, 5000);
        return result;
    };

    VrrTimingParameters production =
        legacyPlayoutParameters(config(127, 144));
    production.playoutHistoryEnabled = 0;
    production.playoutPerFrameLatch = 0;
    const Result current = run(production);

    VrrTimingParameters vrr12Like = production;
    vrr12Like.playoutDelaySlewAcrossBands = 0;
    vrr12Like.rateCandidateMinimumUs = 0;
    vrr12Like.playoutDelayStartPeriodPerMille = 0;
    vrr12Like.playoutDelayMaximumPeriodPerMille = 0;
    vrr12Like.playoutSmoothingGainPerMille = 150;
    vrr12Like.playoutSmoothingPeriodAlphaPerMille = 50;
    vrr12Like.playoutSmoothingMaxLagUs = 8000;
    const Result oldBandApplication = run(vrr12Like);

    std::fprintf(stderr,
                 "120 Hz calibration edge: current max delay step=%llu us, vrr12-style=%llu us\n",
                 static_cast<unsigned long long>(current.maximumDelayStepUs),
                 static_cast<unsigned long long>(
                     oldBandApplication.maximumDelayStepUs));
    expect(current.sawLowerBand && current.returnedToUpperBand,
           "the regression must cross from the lower calibration band into 120..127 FPS");
    expect(current.maximumDelayStepUs <=
               std::max(production.playoutDelayAttackUs,
                        production.playoutDelayReleaseUs),
           "crossing into 120..127 FPS must slew rather than step the active delay");
    expect(oldBandApplication.maximumDelayStepUs > 1000,
           "the vrr12-style direct band assignment must reproduce the old timing cliff");
}

void testNegotiatedRateCeiling()
{
    constexpr uint64_t epochUs = 1000000;

    VrrTimingController candidateController(config(60, 120));
    candidateController.schedule(frame(0, 0, true, epochUs), epochUs);
    candidateController.schedule(
        frame(1, 1500, true, idealDecodedTime(epochUs, 1, 60)),
        idealDecodedTime(epochUs, 1, 60));
    VrrTimingDecision provisional = candidateController.schedule(
        frame(2, 1875, true, idealDecodedTime(epochUs, 2, 240)),
        idealDecodedTime(epochUs, 2, 240));
    VrrTimingDecision boundedCandidate = candidateController.schedule(
        frame(3, 2250, true, idealDecodedTime(epochUs, 3, 240)),
        idealDecodedTime(epochUs, 3, 240));
    const uint64_t sixtyFpsPeriodUs = (1000000ULL + 30) / 60;
    expect(provisional.phaseDiscontinuity &&
               !boundedCandidate.sourceRateChanged &&
               candidateController.sourcePeriodUs() == sixtyFpsPeriodUs,
           "a faster provisional candidate must remain at the negotiated rate");

    VrrTimingController fittedController(config(116, 120));
    fittedController.schedule(frame(0, 0, true, epochUs), epochUs);
    uint64_t minimumPeriodUs = std::numeric_limits<uint64_t>::max();
    for (int i = 1; i <= 128; ++i) {
        const uint32_t timestamp = static_cast<uint32_t>(i * 750);
        const uint64_t decodedUs = idealDecodedTime(epochUs, i, 120);
        const VrrTimingDecision decision = fittedController.schedule(
            frame(i, timestamp, true, decodedUs), decodedUs);
        if (i >= 16) {
            minimumPeriodUs = std::min(minimumPeriodUs,
                                        decision.sourcePeriodUs);
        }
    }
    const uint64_t negotiatedPeriodUs = (1000000ULL + 58) / 116;
    expect(minimumPeriodUs == negotiatedPeriodUs,
           "a fitted cadence must never imply a rate above the negotiated stream FPS");
}

void testSpacingGuardFeedback()
{
    VrrTimingController controller(config(60, 120));
    VrrTimingDecision first = controller.schedule(
        frame(1, 0, true, 100000), 100000);
    controller.noteSubmission(true, false, first.targetUs);
    const VrrTimingParameters& parameters = controller.parameters();
    const uint64_t raisedGuardUs = std::min(
        parameters.maximumAdaptiveGuardUs,
        first.guardUs + std::max<uint64_t>(parameters.guardStepUs, 300));
    controller.noteSpacingDeficit(300);
    expect(controller.guardUs() == raisedGuardUs,
           "a spacing deficit must raise the bounded guard directly");

    VrrTimingDecision second = controller.schedule(
        frame(2, 1500, true, 116666), 116666);
    expect(second.targetUs >=
               first.targetUs + controller.displayPeriodUs() + raisedGuardUs,
           "the raised guard must affect the next display-spacing floor");

    for (size_t i = 0; i < parameters.guardDecayFrames; ++i) {
        controller.noteSpacingDeficit(0);
    }
    expect(controller.guardUs() ==
               raisedGuardUs - std::min(parameters.guardStepUs,
                                         raisedGuardUs - first.guardUs),
           "a clean run must decay the guard by one small step");
}

void testNearRefreshRequestsLatchedPresentation()
{
    VrrTimingParameters headroomOnlyParameters;
    headroomOnlyParameters.cadenceStabilityLatchFrames = 0;
    for (int streamRateHz = 30; streamRateHz <= 115; ++streamRateHz) {
        VrrTimingController withHeadroom(
            config(streamRateHz, 120), true, headroomOnlyParameters);
        const VrrTimingDecision decision = withHeadroom.schedule(
            frame(1, 0, true, 100000), 100000);
        expect(!decision.latchedPresentation,
               "useful in-range cadences at 120 Hz must retain adaptive presentation");
    }

    VrrTimingController nearRefresh(
        config(116, 120), true, headroomOnlyParameters);
    VrrTimingDecision decision = nearRefresh.schedule(
        frame(1, 0, true, 100000), 100000);
    expect(decision.headroomUs == 188 && decision.latchedPresentation,
           "116 FPS must retain its 188 us near-refresh latched path");

    VrrTimingController boundary(
        config(115, 120), true, headroomOnlyParameters);
    decision = boundary.schedule(frame(1, 0, true, 100000), 100000);
    expect(decision.headroomUs == 263 && !decision.latchedPresentation,
           "115 FPS must remain adaptive with 263 us of headroom");

    VrrTimingController immutableMailbox(config(116, 120), false);
    decision = immutableMailbox.schedule(
        frame(1, 0, true, 100000), 100000);
    expect(!decision.latchedPresentation,
           "an immutable cadence-following backend must not be classified as fixed-vsync latched");
}

void testLatchedPresentationUsesFullHysteresis()
{
    const auto decayGuardToBase = [](VrrTimingController& controller,
                                     uint64_t baseGuardUs) {
        const VrrTimingParameters& parameters = controller.parameters();
        const size_t decayCycles = static_cast<size_t>(
            (controller.guardUs() - baseGuardUs +
             parameters.guardStepUs - 1) /
            parameters.guardStepUs);
        for (size_t i = 0;
             i < decayCycles * parameters.guardDecayFrames; ++i) {
            controller.noteSpacingDeficit(0);
        }
    };

    // 115 FPS begins between the entry and exit thresholds. A small guard
    // excursion should latch it, and returning to the base guard must not
    // immediately undo that decision while it remains inside the band.
    VrrTimingParameters hysteresisParameters;
    hysteresisParameters.cadenceStabilityLatchFrames = 0;
    VrrTimingController borderline(
        config(115, 120), true, hysteresisParameters);
    VrrTimingDecision decision = borderline.schedule(
        frame(1, 0, true, 100000), 100000);
    expect(!decision.latchedPresentation,
           "115 FPS must begin adaptive above the latch-entry threshold");

    const uint64_t baseGuardUs = decision.guardUs;
    const VrrTimingParameters& parameters = borderline.parameters();
    expect(decision.headroomUs >
               parameters.latchedPresentationHeadroomUs &&
           decision.headroomUs <
               parameters.latchedPresentationExitHeadroomUs,
           "115 FPS must begin inside the configured hysteresis band");
    const uint64_t latchDeficitUs =
        decision.headroomUs - parameters.latchedPresentationHeadroomUs + 1;
    borderline.noteSpacingDeficit(latchDeficitUs);
    decision = borderline.schedule(
        frame(2, 783, true, 108696), 108696);
    expect(decision.latchedPresentation,
           "a transient guard increase must select the safe latched path");

    decayGuardToBase(borderline, baseGuardUs);
    decision = borderline.schedule(
        frame(3, 1565, true, 117392), 117392);
    expect(decision.latchedPresentation,
           "base-guard recovery inside the hysteresis band must stay latched");

    // 113 FPS has enough base headroom to cross the exit threshold after the
    // same temporary guard protection decays.
    VrrTimingController recoverable(
        config(113, 120), true, hysteresisParameters);
    decision = recoverable.schedule(
        frame(1, 0, true, 100000), 100000);
    const uint64_t recoverableBaseGuardUs = decision.guardUs;
    expect(!decision.latchedPresentation &&
               decision.headroomUs >=
                   parameters.latchedPresentationExitHeadroomUs,
           "113 FPS must have enough base headroom to exit latching");
    recoverable.noteSpacingDeficit(
        decision.headroomUs -
            parameters.latchedPresentationHeadroomUs + 1);
    decision = recoverable.schedule(
        frame(2, 796, true, 108850), 108850);
    expect(decision.latchedPresentation,
           "a large guard excursion must latch 113 FPS temporarily");
    decayGuardToBase(recoverable, recoverableBaseGuardUs);
    decision = recoverable.schedule(
        frame(3, 1593, true, 117700), 117700);
    expect(!decision.latchedPresentation,
           "crossing the full exit threshold must restore adaptive presentation");

    // Old traces did exit as soon as the guard reached its base value. Keep
    // that behavior selectable so exact baseline replay remains possible.
    VrrTimingParameters legacyParameters;
    legacyParameters.latchedPresentationBaseGuardExit = 1;
    legacyParameters.cadenceStabilityLatchFrames = 0;
    VrrTimingController legacy(config(115, 120), true, legacyParameters);
    decision = legacy.schedule(frame(1, 0, true, 100000), 100000);
    const uint64_t legacyBaseGuardUs = decision.guardUs;
    legacy.noteSpacingDeficit(
        decision.headroomUs -
            legacyParameters.latchedPresentationHeadroomUs + 1);
    decision = legacy.schedule(
        frame(2, 783, true, 108696), 108696);
    expect(decision.latchedPresentation,
           "legacy replay policy must still enter latching");
    decayGuardToBase(legacy, legacyBaseGuardUs);
    decision = legacy.schedule(
        frame(3, 1565, true, 117392), 117392);
    expect(!decision.latchedPresentation,
           "legacy replay policy must retain base-guard exit semantics");
}

void testOptionalDisplayScaledLatchedPresentationBoundary()
{
    VrrTimingParameters parameters;
    parameters.cadenceStabilityLatchFrames = 0;
    parameters.latchedPresentationHeadroomPeriodNumerator = 3;
    parameters.latchedPresentationHeadroomPeriodDenominator = 1;
    parameters.latchedPresentationExitHeadroomPeriodNumerator = 13;
    parameters.latchedPresentationExitHeadroomPeriodDenominator = 4;

    const struct {
        int displayHz;
        int protectedRateHz;
        int adaptiveRateHz;
    } cases[] = {
        {60, 15, 14},
        {120, 30, 29},
        {144, 36, 35},
        {165, 42, 41},
    };
    for (const auto& value : cases) {
        VrrTimingController protectedController(
            config(value.protectedRateHz, value.displayHz), true,
            parameters);
        const VrrTimingDecision protectedDecision =
            protectedController.schedule(
                frame(1, 0, true, 100000), 100000);
        expect(protectedDecision.latchedPresentation,
               "three-period latch protection must scale with display refresh");

        VrrTimingController adaptiveController(
            config(value.adaptiveRateHz, value.displayHz), true,
            parameters);
        const VrrTimingDecision adaptiveDecision =
            adaptiveController.schedule(
                frame(1, 0, true, 100000), 100000);
        expect(!adaptiveDecision.latchedPresentation,
               "cadence beyond the three-period window must stay adaptive at every display rate");
    }
}

void testCadenceInstabilityUsesLatchedRecovery()
{
    constexpr int sourceRateHz = 90;
    constexpr uint64_t epochUs = 100000;
    VrrTimingController controller(config(sourceRateHz, 120));
    const VrrTimingParameters& parameters = controller.parameters();

    VrrTimingDecision decision = controller.schedule(
        frame(0, 0, true, epochUs), epochUs);
    expect(decision.latchedPresentation,
           "an uninitialized source phase must begin on the safe latched path");

    for (size_t i = 1;
         i <= parameters.cadenceStabilityLatchFrames; ++i) {
        const uint32_t timestamp = static_cast<uint32_t>(
            i * 90000ULL / sourceRateHz);
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
        decision = controller.schedule(
            frame(static_cast<int>(i), timestamp, true, decodedUs),
            decodedUs);
        expect(decision.latchedPresentation,
               "cadence recovery must remain latched for the configured clean window");
    }

    const size_t adaptiveFrame = parameters.cadenceStabilityLatchFrames + 1;
    uint32_t timestamp = static_cast<uint32_t>(
        adaptiveFrame * 90000ULL / sourceRateHz);
    uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
    decision = controller.schedule(
        frame(static_cast<int>(adaptiveFrame), timestamp, true, decodedUs),
        decodedUs);
    expect(!decision.latchedPresentation,
           "a stable source with ample display headroom must recover adaptive VRR");

    const size_t hitchFrame = adaptiveFrame + 1;
    timestamp += 9000;
    decodedUs += 100000;
    decision = controller.schedule(
        frame(static_cast<int>(hitchFrame), timestamp, true, decodedUs),
        decodedUs);
    expect(decision.phaseDiscontinuity && decision.latchedPresentation,
           "a source hitch must immediately restore latched protection");
}

void testHeadroomAwareReadinessReserve()
{
    constexpr uint64_t epochUs = 1000000;
    VrrTimingController wideHeadroom(config(60, 120), false);
    VrrTimingController nearCeiling(config(116, 120), false);

    const auto train = [](VrrTimingController& controller, int rateHz) {
        constexpr uint64_t startUs = epochUs;
        VrrTimingDecision decision = controller.schedule(
            frame(0, 0, true, startUs), startUs);
        controller.noteSubmission(true, false, decision.targetUs);
        for (int i = 1; i <= 96; ++i) {
            const uint32_t timestamp = static_cast<uint32_t>(
                static_cast<uint64_t>(i) * 90000ULL /
                static_cast<uint64_t>(rateHz));
            const uint64_t sourceUs = decodedTimeForRtp(startUs, timestamp);
            const uint64_t tailUs = i % 4 == 0 ? 3000 : 0;
            decision = controller.schedule(
                frame(i, timestamp, true, sourceUs + tailUs),
                sourceUs + tailUs);
            controller.noteSubmission(true, false, decision.targetUs);
        }
    };

    train(wideHeadroom, 60);
    train(nearCeiling, 116);

    if (wideHeadroom.timingBudgetUs() + 500 >=
            nearCeiling.timingBudgetUs()) {
        std::fprintf(stderr,
                     "headroom budgets: wide=%llu us near=%llu us\n",
                     static_cast<unsigned long long>(wideHeadroom.timingBudgetUs()),
                     static_cast<unsigned long long>(nearCeiling.timingBudgetUs()));
    }
    expect(wideHeadroom.timingBudgetUs() + 500 <
               nearCeiling.timingBudgetUs(),
           "cadence headroom must absorb arrival spread without carrying the near-ceiling reserve at lower rates");
    expect(nearCeiling.timingBudgetUs() >= 3000,
           "near-ceiling cadence-following presentation must retain a real burst cushion");
}

void testCadenceGapAndRateChange()
{
    VrrTimingController controller(config(120, 120));
    uint32_t timestamp = 0;
    uint64_t decodedUs = 100000;
    controller.schedule(frame(0, timestamp, true, decodedUs), decodedUs);
    for (int i = 1; i <= 8; ++i) {
        timestamp += 1500;
        decodedUs += 16666;
        controller.schedule(frame(i, timestamp, true, decodedUs), decodedUs);
    }
    expect(controller.sourcePeriodUs() == 16667,
           "stable raw cadence must retain rational RTP conversion carry");

    timestamp += 6000;
    decodedUs += 66666;
    VrrTimingDecision gap = controller.schedule(
        frame(9, timestamp, true, decodedUs), decodedUs);
    expect(!gap.cadenceEligible,
           "one large interval must be isolated from cadence adaptation");
    expect(controller.sourcePeriodUs() == 16667,
           "an isolated gap must not retune the source period");

    timestamp += 1500;
    decodedUs += 16666;
    controller.schedule(frame(10, timestamp, true, decodedUs), decodedUs);

    VrrTimingDecision accepted;
    for (int i = 0; i < 16; ++i) {
        timestamp += 1000;
        decodedUs += 11111;
        accepted = controller.schedule(
            frame(11 + i, timestamp, true, decodedUs), decodedUs);
    }
    const double learnedRateHz = 1000000.0 /
        static_cast<double>(controller.sourcePeriodUs());
    expect(std::abs(learnedRateHz - 90.0) < 1.0,
           "a cumulative segment must converge on a non-atomic new rate");
}

void testFutureSourceProjectionReseedsPhase()
{
    VrrTimingController controller(config(116, 120));
    constexpr uint64_t initialUs = 1000000;
    controller.schedule(frame(0, 0, true, initialUs), initialUs);

    constexpr uint64_t nowUs = initialUs + 8621;
    VrrTimingDecision recovered = controller.schedule(
        frame(50, 38024, true, nowUs), nowUs);

    expect(!recovered.rebased && recovered.sourceIntervalUs != 0 &&
               recovered.targetUs < nowUs + 2 * 8621,
           "a source projection ahead of decoded local time must reseed phase without discarding cadence");
}

void testDecodeTailAdaptation()
{
    VrrTimingController controller(config());
    uint32_t timestamp = 0;
    uint64_t sourceUs = 1000000;
    controller.schedule(frame(0, timestamp, true, sourceUs), sourceUs);

    for (int i = 1; i <= 16; ++i) {
        timestamp += 1500;
        sourceUs += 16666;
        const uint64_t tailUs = i % 4 == 0 ? 5000 : 0;
        VrrTimingDecision decision = controller.schedule(
            frame(i, timestamp, true, sourceUs + tailUs), sourceUs + tailUs);
        controller.notePreparationDuration(1000);
        controller.noteSubmission(true, false, decision.targetUs);
    }

    expect(controller.renderLeadUs() >=
               1000 + controller.parameters().renderLeadSlackUs,
           "preparation duration must include render slack");
    expect(controller.diagnostics().readinessDemandUs >
               controller.parameters().minimumReadinessReserveUs,
           "positive readiness tail must grow learned readiness demand");
}

void testRateChangeReseedsReadinessBudget()
{
    VrrTimingController controller(config(116, 120));
    controller.schedule(frame(0, 0, true, 100000), 100000);
    VrrTimingDecision provisional = controller.schedule(
        frame(1, 3000, true, 133333), 133333);
    VrrTimingDecision accepted = controller.schedule(
        frame(2, 6000, true, 166666), 166666);

    expect(provisional.phaseDiscontinuity &&
               accepted.sourceRateChanged &&
               accepted.readinessBudgetUs == accepted.readyOffsetUs &&
               std::abs(1000000.0 /
                   static_cast<double>(accepted.sourcePeriodUs) - 30.0) < 0.1,
            "a confirmed major slowdown must reseed phase and readiness after two intervals");
}

void testFractionalQuantizedCadenceLearning()
{
    constexpr uint64_t epochUs = 1000000;

    for (int rateHz = 30; rateHz <= 116; ++rateHz) {
        VrrTimingController controller(config(116, 120));
        controller.schedule(frame(0, 0, true, epochUs), epochUs);

        const int sampleCount = std::max(160, rateHz * 2);
        bool rebased = false;
        for (int i = 1; i <= sampleCount; ++i) {
            const uint32_t timestamp = quantizedRtpTimestamp(i, rateHz);
            const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
            const VrrTimingDecision decision = controller.schedule(
                frame(i, timestamp, true, decodedUs), decodedUs);
            rebased = rebased || decision.rebased;
        }

        const double learnedRateHz = 1000000.0 /
            static_cast<double>(controller.sourcePeriodUs());
        expect(!rebased,
               "fractional capture-clock cadence must not rebase");
        if (std::abs(learnedRateHz - rateHz) >= 0.75) {
            std::fprintf(stderr,
                         "cadence mismatch: requested=%d learned=%.3f\n",
                         rateHz, learnedRateHz);
        }
        expect(std::abs(learnedRateHz - rateHz) < 0.75,
               "cumulative cadence learning must represent arbitrary rates continuously");
    }
}

void testCutsceneRecoveryAndHitchIsolation()
{
    constexpr uint64_t epochUs = 1000000;
    VrrTimingController controller(config(116, 120));
    controller.schedule(frame(0, 0, true, epochUs), epochUs);

    int frameNumber = 0;
    uint32_t timestamp = 0;
    for (int i = 1; i <= 140; ++i) {
        frameNumber = i;
        timestamp = quantizedRtpTimestamp(i, 116);
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
        controller.schedule(frame(frameNumber, timestamp, true, decodedUs),
                            decodedUs);
    }
    const uint64_t stablePeriodUs = controller.sourcePeriodUs();

    timestamp += 3750;
    ++frameNumber;
    uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
    VrrTimingDecision hitch = controller.schedule(
        frame(frameNumber, timestamp, true, decodedUs), decodedUs);
    expect(hitch.phaseDiscontinuity && !hitch.sourceRateChanged,
           "one large hitch must start only a provisional cadence segment");

    timestamp += 750;
    ++frameNumber;
    decodedUs = decodedTimeForRtp(epochUs, timestamp);
    VrrTimingDecision recovered = controller.schedule(
        frame(frameNumber, timestamp, true, decodedUs), decodedUs);
    expect(recovered.phaseDiscontinuity && !recovered.sourceRateChanged &&
               std::abs(static_cast<int64_t>(controller.sourcePeriodUs()) -
                        static_cast<int64_t>(stablePeriodUs)) < 100,
           "a normal successor must abandon a hitch without poisoning the stable rate");

    VrrTimingDecision cutscene;
    for (int i = 0; i < 2; ++i) {
        timestamp += 3000;
        ++frameNumber;
        decodedUs = decodedTimeForRtp(epochUs, timestamp);
        cutscene = controller.schedule(
            frame(frameNumber, timestamp, true, decodedUs), decodedUs);
    }
    expect(cutscene.sourceRateChanged &&
               std::abs(1000000.0 /
                   static_cast<double>(controller.sourcePeriodUs()) - 30.0) < 0.1,
           "a 30 FPS cutscene must be accepted after two confirming intervals");

    VrrTimingDecision acceleration;
    for (int i = 0; i < 2; ++i) {
        timestamp += 750;
        ++frameNumber;
        decodedUs = decodedTimeForRtp(epochUs, timestamp);
        acceleration = controller.schedule(
            frame(frameNumber, timestamp, true, decodedUs), decodedUs);
    }
    expect(acceleration.sourceRateChanged && acceleration.latchedPresentation,
           "returning to the tight high-rate range must recover provisionally in latched mode");
}

void testModerateSlowdownSelfHealsPhase()
{
    constexpr uint64_t epochUs = 1000000;
    VrrTimingController controller(config(60, 120));
    controller.schedule(frame(0, 0, true, epochUs), epochUs);

    int frameNumber = 0;
    uint32_t timestamp = 0;
    for (int i = 1; i <= 80; ++i) {
        frameNumber = i;
        timestamp += 1500;
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
        controller.schedule(frame(frameNumber, timestamp, true, decodedUs),
                            decodedUs);
    }

    bool healedPhase = false;
    for (int i = 0; i < 40; ++i) {
        ++frameNumber;
        timestamp += 3000;
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
        const VrrTimingDecision decision = controller.schedule(
            frame(frameNumber, timestamp, true, decodedUs), decodedUs);
        healedPhase = healedPhase || decision.phaseDiscontinuity;
        if (i == 3) {
            expect(healedPhase,
                   "a moderate slowdown must heal phase within four frames even without a major-rate candidate");
        }
    }

    const double learnedRateHz = 1000000.0 /
        static_cast<double>(controller.sourcePeriodUs());
    expect(std::abs(learnedRateHz - 30.0) < 0.75,
           "a moderate slowdown must converge to its cumulative cadence after phase recovery");

    ++frameNumber;
    timestamp += 1500;
    const uint64_t acceleratedDecodeUs = decodedTimeForRtp(epochUs,
                                                            timestamp);
    const VrrTimingDecision accelerated = controller.schedule(
        frame(frameNumber, timestamp, true, acceleratedDecodeUs),
        acceleratedDecodeUs);
    expect(accelerated.phaseDiscontinuity &&
               accelerated.targetUs < acceleratedDecodeUs + 5000,
           "a frame arriving ahead of a slower cutscene clock must bypass stale latency immediately");
}

void testContinuousCadenceSweep()
{
    constexpr uint64_t epochUs = 1000000;
    VrrTimingController controller(config(116, 120));
    controller.schedule(frame(0, 0, true, epochUs), epochUs);

    int frameNumber = 0;
    long double idealRtpTicks = 0.0L;
    uint32_t timestamp = 0;
    double maximumRateErrorHz = 0.0;
    bool rebased = false;

    const auto runRate = [&](int rateHz) {
        const uint32_t startTimestamp = timestamp;
        const int startFrame = frameNumber;
        for (int i = 0; i < rateHz; ++i) {
            idealRtpTicks += 90000.0L /
                static_cast<long double>(rateHz);
            const uint64_t captureTick = static_cast<uint64_t>(
                std::llround(idealRtpTicks / 750.0L));
            timestamp = static_cast<uint32_t>(captureTick * 750ULL);
            ++frameNumber;
            const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
            const VrrTimingDecision decision = controller.schedule(
                frame(frameNumber, timestamp, true, decodedUs), decodedUs);
            rebased = rebased || decision.rebased;
        }

        const uint32_t elapsedTicks = timestamp - startTimestamp;
        const int elapsedFrames = frameNumber - startFrame;
        const double measuredRateHz = elapsedTicks == 0 ? 0.0 :
            static_cast<double>(elapsedFrames) * 90000.0 /
                static_cast<double>(elapsedTicks);
        const double learnedRateHz = 1000000.0 /
            static_cast<double>(controller.sourcePeriodUs());
        maximumRateErrorHz = std::max(
            maximumRateErrorHz,
            std::abs(learnedRateHz - measuredRateHz));
    };

    for (int rateHz = 116; rateHz >= 30; --rateHz) {
        runRate(rateHz);
    }
    for (int rateHz = 31; rateHz <= 116; ++rateHz) {
        runRate(rateHz);
    }

    if (maximumRateErrorHz >= 2.0) {
        std::fprintf(stderr, "sweep maximum rate error: %.3f Hz\n",
                     maximumRateErrorHz);
    }
    expect(!rebased,
           "a continuous cadence sweep must not reset the source epoch");
    expect(maximumRateErrorHz < 2.0,
           "a one FPS-per-second sweep must remain within two FPS of measured cadence");
}

void testQuantizedCadenceProjectsSmoothTargets()
{
    constexpr uint64_t epochUs = 1000000;
    constexpr uint64_t expectedPeriodUs = 10000;
    VrrSessionConfig smoothnessConfig = config(116, 120);
    smoothnessConfig.allowAdditionalQueuedFrame = true;
    VrrTimingController controller(smoothnessConfig);
    VrrTimingDecision decision = controller.schedule(
        frame(0, 0, true, epochUs), epochUs);
    controller.noteSubmission(true, false, decision.targetUs);

    uint64_t previousTargetUs = decision.targetUs;
    unsigned int measuredSpacings = 0;
    unsigned int largeErrors = 0;
    for (int i = 1; i <= 300; ++i) {
        const uint32_t timestamp = quantizedRtpTimestamp(i, 100);
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
        decision = controller.schedule(
            frame(i, timestamp, true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);

        if (i > 180) {
            const uint64_t spacingUs = decision.targetUs - previousTargetUs;
            const uint64_t errorUs = spacingUs > expectedPeriodUs ?
                spacingUs - expectedPeriodUs : expectedPeriodUs - spacingUs;
            ++measuredSpacings;
            if (errorUs > 500) {
                ++largeErrors;
            }
        }
        previousTargetUs = decision.targetUs;
    }

    if (largeErrors * 20 > measuredSpacings) {
        std::fprintf(stderr,
                     "quantized target errors: %u/%u, readiness=%lld us, budget=%llu us\n",
                     largeErrors, measuredSpacings,
                     static_cast<long long>(controller.readinessBudgetUs()),
                     static_cast<unsigned long long>(controller.timingBudgetUs()));
    }
    expect(largeErrors * 20 <= measuredSpacings,
           "a learned quantized cadence must project at least 95 percent of targets within 500 us");
}

void testSkippedLocalFramePreservesCadence()
{
    constexpr uint64_t epochUs = 1000000;
    VrrTimingController controller(config(100, 120));
    VrrTimingDecision decision = controller.schedule(
        frame(0, 0, true, epochUs), epochUs);

    for (int i = 1; i <= 180; ++i) {
        const uint32_t timestamp = quantizedRtpTimestamp(i, 100);
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
        decision = controller.schedule(
            frame(i, timestamp, true, decodedUs), decodedUs);
    }
    const uint64_t stablePeriodUs = controller.sourcePeriodUs();

    const int successor = 183;
    const uint32_t timestamp = quantizedRtpTimestamp(successor, 100);
    const uint64_t decodedUs = decodedTimeForRtp(epochUs, timestamp);
    decision = controller.schedule(
        frame(successor, timestamp, true, decodedUs), decodedUs);

    expect(!decision.rebased && decision.cadenceEligible &&
               std::abs(static_cast<int64_t>(controller.sourcePeriodUs()) -
                        static_cast<int64_t>(stablePeriodUs)) < 100,
           "a latest-frame queue replacement must advance by frame delta without resetting cadence");
}

void testSchedulerDelayFeedback()
{
    VrrTimingController controller(config());
    VrrTimingDecision first = controller.schedule(
        frame(1, 0, true, 100000), 100000);
    controller.noteSchedulerDelays(600, 300, true);
    controller.noteSubmission(true, false, first.targetUs);

    VrrTimingDecision second = controller.schedule(
        frame(2, 1500, true, 116666), 116666);
    expect(second.renderWakeLeadUs == 600 &&
               second.targetWakeLeadUs == 300 &&
               second.renderStartUs + second.renderLeadUs +
                   second.renderWakeLeadUs == second.targetUs,
           "render and final-target wake delays must learn independently");

    for (int i = 0; i < 40; ++i) {
        controller.noteSchedulerDelays(0, 0, false);
    }
    expect(controller.targetWakeLeadUs() == 300,
           "frames without a coarse target sleep must retain learned delay");
}

void trainRenderDurations(VrrTimingController& controller,
                          uint64_t baselineUs, uint64_t tailUs)
{
    constexpr uint64_t epochUs = 1000000;
    for (int i = 0; i < 96; ++i) {
        const uint64_t decodedUs = epochUs +
            static_cast<uint64_t>(i) * 10000ULL;
        VrrTimingDecision decision = controller.schedule(
            frame(i, static_cast<uint32_t>(i * 900), true, decodedUs),
            decodedUs);
        controller.notePreparationDuration(i == 95 ? tailUs : baselineUs);
        controller.noteSubmission(true, false, decision.targetUs);
    }
}

void testRenderBaselineDoesNotConsumePacingBudget()
{
    VrrTimingController controller(config(100, 120));
    trainRenderDurations(controller, 9000, 9000);

    const VrrTimingDiagnostics diagnostics = controller.diagnostics();
    expect(diagnostics.renderBaselineUs == 9000 &&
               controller.renderLeadUs() == 9000,
           "stable render work above the old ceiling must be learned in full");
    expect(diagnostics.renderInsuranceUs == 0,
           "stable render work must not be counted as pacing insurance");
    expect(controller.timingBudgetUs() <=
               diagnostics.pacingLatencyBudgetUs,
           "unavoidable render baseline must not consume the half-scanout budget");
}

void testRenderTailSharesHalfScanoutBudget()
{
    VrrTimingController controller(config(100, 120));
    trainRenderDurations(controller, 4000, 10000);

    const VrrTimingDiagnostics diagnostics = controller.diagnostics();
    expect(diagnostics.renderBaselineUs == 4000,
           "render baseline must use the configured median percentile");
    expect(controller.renderLeadUs() >= 7600 &&
               controller.renderLeadUs() <= 7700,
           "render p99 must be capped to baseline plus available tail insurance");
    expect(diagnostics.renderInsuranceUs >= 3600 &&
               diagnostics.renderInsuranceUs <= 3700,
           "only the p99-minus-median render tail must consume pacing budget");
    expect(diagnostics.appliedReadinessReserveUs == 500,
           "render tail must leave the minimum readiness reserve intact");
    expect(controller.timingBudgetUs() <=
               diagnostics.pacingLatencyBudgetUs,
           "readiness and render-tail insurance must not exceed half a scanout");
}

void testPacingBudgetAtExtremeRefreshRates()
{
    for (int displayRefreshHz = 60; displayRefreshHz <= 2000;
            displayRefreshHz += 37) {
        VrrTimingController controller(config(100, displayRefreshHz));
        trainRenderDurations(controller, 400, 2400);
        const VrrTimingDiagnostics diagnostics = controller.diagnostics();
        if (controller.timingBudgetUs() >
                diagnostics.pacingLatencyBudgetUs) {
            std::fprintf(stderr,
                         "refresh %d: pacing=%llu budget=%llu baseline=%llu insurance=%llu readiness=%llu\n",
                         displayRefreshHz,
                         static_cast<unsigned long long>(
                             controller.timingBudgetUs()),
                         static_cast<unsigned long long>(
                             diagnostics.pacingLatencyBudgetUs),
                         static_cast<unsigned long long>(
                             diagnostics.renderBaselineUs),
                         static_cast<unsigned long long>(
                             diagnostics.renderInsuranceUs),
                         static_cast<unsigned long long>(
                             diagnostics.appliedReadinessReserveUs));
        }
        expect(controller.timingBudgetUs() <=
                   diagnostics.pacingLatencyBudgetUs,
               "pacing reserve must remain bounded from 60 through 2000 Hz");
    }
}

void testSmoothnessAddsOneSourcePeriodOfReserve()
{
    VrrSessionConfig lowLatencyConfig = config(100, 120);
    VrrSessionConfig smoothnessConfig = lowLatencyConfig;
    smoothnessConfig.allowAdditionalQueuedFrame = true;
    VrrTimingController lowLatency(lowLatencyConfig);
    VrrTimingController smoothness(smoothnessConfig);

    const VrrTimingDiagnostics lowLatencyDiagnostics =
        lowLatency.diagnostics();
    const VrrTimingDiagnostics smoothnessDiagnostics =
        smoothness.diagnostics();
    expect(smoothnessDiagnostics.pacingLatencyBudgetUs ==
               lowLatencyDiagnostics.pacingLatencyBudgetUs +
                   smoothness.sourcePeriodUs(),
           "smoothness mode must grant exactly one source period of reserve");
}

void testLegacyReplayPolicyRetainsAbsoluteRenderCeiling()
{
    VrrTimingParameters parameters;
    parameters.renderLeadCeilingUs = 6500;
    parameters.pacingLatencyBudgetDivisor = 0;
    VrrTimingController controller(config(100, 120), true, parameters);
    trainRenderDurations(controller, 9000, 9000);

    const VrrTimingDiagnostics diagnostics = controller.diagnostics();
    expect(controller.renderLeadUs() == 6500,
           "legacy replay mode must retain the captured absolute render ceiling");
    expect(diagnostics.pacingLatencyBudgetUs == 0,
           "legacy replay mode must mark the pacing latency policy disabled");
    expect(controller.timingBudgetUs() ==
               diagnostics.appliedReadinessReserveUs +
                   controller.renderLeadUs(),
           "legacy replay timing budget must include the full render lead");
}

void testTargetWaiterBoundaries()
{
    uint64_t nowUs = 0;
    uint64_t requestedCoarseSleepUs = 0;
    VrrTargetWaiterHooks hooks;
    hooks.nowUs = [&nowUs]() { return nowUs; };
    hooks.sleepForUs = [&nowUs, &requestedCoarseSleepUs](uint64_t durationUs) {
        requestedCoarseSleepUs += durationUs;
        nowUs += durationUs;
    };
    hooks.yield = [&nowUs]() { nowUs += 25; };
    VrrTargetWaiter waiter(hooks);

    VrrTargetWaitResult result = waiter.waitUntil(1000);
    expect(requestedCoarseSleepUs == 500 && result.finalNowUs >= 1000,
           "waiter must sleep to the active-wait boundary");
    expect(result.initialNowUs == 0 && result.activeWaitUs == 500 &&
               result.coarseSleepCount == 1 &&
               result.coarseSleepRequestedUs == 500 &&
               result.coarseSleepRequestedWakeUs == 500 &&
               result.coarseSleepReturnUs == 500 &&
               result.activeWaitEntered &&
               result.activeWaitStartUs == 500 &&
               result.activeWaitLimitUs == 1000 &&
               result.activeWaitYieldCount == 20,
           "waiter must expose the exact coarse and active lifecycle");

    nowUs = 0;
    requestedCoarseSleepUs = 0;
    result = waiter.waitUntil(1000, 400);
    expect(requestedCoarseSleepUs == 100 && result.finalNowUs >= 1000,
           "learned scheduler delay must wake the final wait earlier");
    expect(result.activeWaitUs == 900 &&
               result.coarseSleepRequestedWakeUs == 100 &&
               result.coarseSleepReturnUs == 100,
           "waiter lifecycle must include the bounded learned wake lead");

    uint64_t delayedNowUs = 0;
    VrrTargetWaiterHooks delayedHooks;
    delayedHooks.nowUs = [&delayedNowUs]() { return delayedNowUs; };
    delayedHooks.sleepForUs = [&delayedNowUs](uint64_t durationUs) {
        delayedNowUs += durationUs + 900;
    };
    delayedHooks.yield = [&delayedNowUs]() { delayedNowUs += 25; };
    VrrTargetWaiter delayedWaiter(delayedHooks);
    result = delayedWaiter.waitUntil(5000);
    expect(result.schedulerDelayValid && result.schedulerDelayUs == 400 &&
               result.finalNowUs == 5400 &&
               result.coarseSleepRequestedWakeUs == 4500 &&
               result.coarseSleepReturnUs == 5400 &&
               !result.activeWaitEntered,
           "coarse wake feedback must measure overshoot beyond the active margin");

    delayedNowUs = 0;
    result = delayedWaiter.waitUntil(5000, 400);
    expect(result.schedulerDelayValid && result.schedulerDelayUs == 400 &&
               result.finalNowUs == 5000 &&
               result.coarseSleepRequestedWakeUs == 4100 &&
               result.coarseSleepReturnUs == 5000 &&
               !result.activeWaitEntered,
           "learned wake delay must correct final-target overshoot");

    nowUs = 100;
    result = waiter.waitUntil(100);
    expect(result.deadlineAlreadyElapsed && result.finalNowUs == 100,
           "an elapsed deadline must return without waiting");
    expect(result.initialNowUs == 100 && result.activeWaitUs == 500 &&
               result.coarseSleepCount == 0 &&
               !result.activeWaitEntered,
           "elapsed waiter lifecycle must remain explicit and empty");

    unsigned int stalledSleepCalls = 0;
    unsigned int stalledYieldCalls = 0;
    VrrTargetWaiterHooks stalledHooks;
    stalledHooks.nowUs = []() { return 0ULL; };
    stalledHooks.sleepForUs = [&stalledSleepCalls](uint64_t) {
        ++stalledSleepCalls;
    };
    stalledHooks.yield = [&stalledYieldCalls]() { ++stalledYieldCalls; };
    VrrTargetWaiter stalled(stalledHooks);
    result = stalled.waitUntil(1000);
    expect(result.finalNowUs == 0 && stalledSleepCalls == 2 &&
               stalledYieldCalls == 64 &&
               result.coarseSleepClockStalled &&
               result.activeWaitClockStalled &&
               result.coarseSleepCount == 2 &&
               result.activeWaitYieldCount == 64,
           "a non-advancing clock must not create unbounded active spinning");

    uint64_t slowNowUs = 0;
    uint64_t slowYieldCalls = 0;
    VrrTargetWaiterHooks slowHooks;
    slowHooks.nowUs = [&slowNowUs]() { return slowNowUs; };
    slowHooks.sleepForUs = [&slowNowUs](uint64_t durationUs) {
        slowNowUs += durationUs;
    };
    slowHooks.yield = [&slowNowUs, &slowYieldCalls]() {
        ++slowYieldCalls;
        if (slowYieldCalls % 16 == 0) {
            ++slowNowUs;
        }
    };
    VrrTargetWaiter slowWaiter(slowHooks);
    result = slowWaiter.waitUntil(1000);
    expect(result.finalNowUs == 1000 &&
               result.activeWaitYieldCount == 8000 &&
               !result.activeWaitClockStalled &&
               !result.activeWaitYieldLimitReached,
           "an advancing clock must reach the deadline even when a fast CPU yields more than 4096 times");
}

void testMetronomeHoldsCadenceThroughJitterAndLateFrames()
{
    // A steady 116 FPS game. Host stamps wobble +-3 ms per frame and
    // delivery adds [0, 2 ms) of arrival jitter that is independent of the
    // stamp. The metronome must present at the fitted period with at most
    // a bounded step per frame: neither kind of jitter reaches the panel.
    VrrSessionConfig session = config(116, 120);
    const VrrTimingParameters policy = metronomePolicy(session);
    VrrTimingController controller(session, true, policy);
    const uint64_t epochUs = 1000000;
    const int64_t periodUs = 1000000 / 116;
    const auto idealUs = [&](int i) {
        return static_cast<int64_t>(20000) + static_cast<int64_t>(i) * 1000000LL / 116LL;
    };
    const auto stampJitterUs = [](int i) {
        return static_cast<int64_t>((static_cast<uint64_t>(i) * 7919ULL) % 6000ULL) - 3000;
    };
    const auto arrivalJitterUs = [](int i) {
        return static_cast<int64_t>((static_cast<uint64_t>(i) * 104729ULL) % 2000ULL);
    };
    const auto rtpFor = [&](int i) {
        return static_cast<uint32_t>((idealUs(i) + stampJitterUs(i)) * 90LL / 1000LL);
    };
    const auto decodedFor = [&](int i, int64_t extraUs) {
        return static_cast<uint64_t>(static_cast<int64_t>(epochUs) + idealUs(i) +
                                     500 + arrivalJitterUs(i) + extraUs);
    };

    const int64_t stepCeilingUs = std::max<int64_t>(
        static_cast<int64_t>(policy.playoutPhaseStepMinimumUs),
        periodUs * static_cast<int64_t>(policy.playoutPhaseStepPeriodPerMille) / 1000);
    // In steady state the tick moves by at most one minimum residual step
    // plus the calibrator's own slew, which it follows exactly.
    const int64_t steadyToleranceUs =
        static_cast<int64_t>(policy.playoutPhaseStepMinimumUs) +
        static_cast<int64_t>(std::max(policy.playoutDelayAttackUs,
                                      policy.playoutDelayReleaseUs)) + 2;
    const int64_t toleranceUs = stepCeilingUs +
        static_cast<int64_t>(policy.playoutPhaseStepMinimumUs) + 2;

    VrrTimingDecision decision;
    uint64_t previousTargetUs = 0;
    int64_t worstDeviationUs = 0;
    unsigned int resets = 0;
    unsigned int steppedFrames = 0;
    unsigned int buckets[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 1200; ++i) {
        const uint64_t decodedUs = decodedFor(i, 0);
        decision = controller.schedule(frame(i + 1, rtpFor(i), true, decodedUs),
                                       decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        if (i > 0 && (decision.rebased || decision.phaseDiscontinuity)) {
            ++resets;
        }
        if (i >= 200) {
            const int64_t intervalUs = static_cast<int64_t>(decision.targetUs) -
                static_cast<int64_t>(previousTargetUs);
            const int64_t deviationUs = intervalUs - periodUs;
            const int64_t magnitudeUs = deviationUs < 0 ? -deviationUs : deviationUs;
            worstDeviationUs = std::max(worstDeviationUs, magnitudeUs);
            if (magnitudeUs > steadyToleranceUs) {
                ++steppedFrames;
            }
            ++buckets[magnitudeUs <= 12 ? 0 : magnitudeUs <= 32 ? 1 :
                      magnitudeUs <= 62 ? 2 : magnitudeUs <= 120 ? 3 : 4];
        }
        previousTargetUs = decision.targetUs;
    }
    std::fprintf(stderr,
                 "metronome: worst steady interval deviation=%lld us (tolerance %lld), frames over minimum step=%u, resets=%u delay=%llu, |dev| buckets <=12:%u <=32:%u <=62:%u <=120:%u more:%u\n",
                 static_cast<long long>(worstDeviationUs),
                 static_cast<long long>(toleranceUs), steppedFrames, resets,
                 static_cast<unsigned long long>(decision.playoutDelayUs),
                 buckets[0], buckets[1], buckets[2], buckets[3], buckets[4]);
    expect(resets == 0,
           "stamp and arrival jitter must never re-anchor the metronome");
    expect(worstDeviationUs <= toleranceUs,
           "the metronome must never move the presented slot by more than one bounded step");
    expect(steppedFrames == 0,
           "in steady state no interval may move beyond one residual step plus the delay slew");
    expect(buckets[0] >= 700,
           "in steady state most intervals must sit within a dozen microseconds of the period");
    expect(decision.playoutDelayUs >= 4000,
           "with 2 ms arrival jitter over a 3 ms stamp floor the cushion must stay several ms");

    // A 30 ms arrival stall lands four frames at once. Emulating the
    // worker, a frame that missed its tick by a whole period yields to the
    // fresher frame already waiting; the last one presents as soon as it is
    // ready (one stretched interval). Nothing after it presents early, and
    // the lag it took on is paid back one bounded step per frame.
    int64_t lateIntervalUs = 0;
    int64_t worstRecoveryDeviationUs = 0;
    int64_t worstShortIntervalUs = 0;
    int64_t lagAfterUs = 0;
    unsigned int emulatedDrops = 0;
    bool sawStretch = false;
    const uint64_t stallEndUs = decodedFor(1200, 30000);
    std::string lagTrace;
    for (int i = 1200; i < 1700; ++i) {
        const bool inBurst = i < 1204;
        const uint64_t decodedUs = inBurst ?
            stallEndUs + static_cast<uint64_t>(i - 1200) * 100 : decodedFor(i, 0);
        decision = controller.schedule(frame(i + 1, rtpFor(i), true, decodedUs),
                                       decodedUs);
        const bool successorQueued = i < 1203;
        if (decision.missedTicks != 0 && successorQueued) {
            controller.noteSubmission(false, false, 0);
            ++emulatedDrops;
            continue;
        }
        controller.noteSubmission(true, false, decision.targetUs);
        const int64_t intervalUs = static_cast<int64_t>(decision.targetUs) -
            static_cast<int64_t>(previousTargetUs);
        if (!sawStretch) {
            // The first burst frame to present ends the stall.
            sawStretch = true;
            lateIntervalUs = intervalUs;
        }
        else {
            const int64_t deviationUs = intervalUs - periodUs;
            worstRecoveryDeviationUs = std::max(
                worstRecoveryDeviationUs, deviationUs < 0 ? -deviationUs : deviationUs);
            worstShortIntervalUs = std::min(worstShortIntervalUs, deviationUs);
        }
        if ((i >= 1200 && i < 1212) || i % 100 == 0) {
            char entry[64];
            std::snprintf(entry, sizeof(entry), " %d:%lld/%llu", i,
                          static_cast<long long>(decision.cadenceSmoothingUs),
                          static_cast<unsigned long long>(decision.playoutDelayUs));
            lagTrace += entry;
        }
        previousTargetUs = decision.targetUs;
        if (i >= 1600) {
            // The per-frame lag carries the raw slot's own stamp wobble;
            // average it out to see where the schedule actually sits.
            lagAfterUs += decision.cadenceSmoothingUs;
        }
    }
    lagAfterUs /= 100;
    std::fprintf(stderr,
                 "metronome: stall drops=%u late interval=%lld us, recovery worst deviation=%lld us, shortest=%lld us, lag after=%lld us\n"
                 "metronome lag/delay trace:%s\n",
                 emulatedDrops, static_cast<long long>(lateIntervalUs),
                 static_cast<long long>(worstRecoveryDeviationUs),
                 static_cast<long long>(worstShortIntervalUs),
                 static_cast<long long>(lagAfterUs), lagTrace.c_str());
    expect(emulatedDrops >= 2,
           "frames overtaken by whole periods during a stall must yield to the freshest one");
    expect(lateIntervalUs > periodUs,
           "the frame that ends a stall must present when ready, not be smeared into the cadence");
    expect(worstRecoveryDeviationUs <= toleranceUs,
           "after a stall every following interval must stay within one phase step of the period");
    expect(lagAfterUs > -600 && lagAfterUs < 600,
           "the lag from a stall must be paid back within a few hundred frames");
}

void testMetronomeIgnoresSingleEarlyOutlier()
{
    // A single frame whose stamp is far behind its arrival maps more than a
    // period into the future. The production policy must wait for it rather
    // than re-seed the clock on it, so the frames around it keep their
    // slots and cadence.
    VrrSessionConfig session = config(116, 120);
    const VrrTimingParameters policy = metronomePolicy(session);
    VrrTimingController controller(session, true, policy);
    const uint64_t epochUs = 1000000;
    const int64_t periodUs = 1000000 / 116;
    const auto rtpFor = [](int i) {
        return static_cast<uint32_t>(static_cast<uint64_t>(i) * 90000ULL / 116ULL);
    };
    VrrTimingDecision decision;
    uint64_t previousTargetUs = 0;
    int64_t worstDeviationUs = 0;
    unsigned int discontinuities = 0;
    for (int i = 0; i < 900; ++i) {
        uint64_t decodedUs = decodedTimeForRtp(epochUs, rtpFor(i)) + 800;
        if (i == 600) {
            // The stamp says this frame is two periods newer than it is.
            decodedUs -= static_cast<uint64_t>(2 * periodUs);
        }
        decision = controller.schedule(frame(i + 1, rtpFor(i), true, decodedUs),
                                       decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        if (i > 0 && (decision.rebased || decision.phaseDiscontinuity)) {
            ++discontinuities;
        }
        if (i >= 400) {
            const int64_t deviationUs = static_cast<int64_t>(decision.targetUs) -
                static_cast<int64_t>(previousTargetUs) - periodUs;
            worstDeviationUs = std::max(worstDeviationUs,
                                        deviationUs < 0 ? -deviationUs : deviationUs);
        }
        previousTargetUs = decision.targetUs;
    }
    std::fprintf(stderr, "early outlier: discontinuities=%u worst deviation=%lld us\n",
                 discontinuities, static_cast<long long>(worstDeviationUs));
    expect(discontinuities == 0,
           "one early outlier must not re-seed the sender clock mapping");
    expect(worstDeviationUs < 1000,
           "one early outlier must not move the presented cadence");
}

void testBurstExclusionKeepsDelayAfterStall()
{
    // A clean link learns a small cushion. A 60 ms arrival stall then lands
    // seven frames at once. Their lateness is the stall's backlog, not the
    // link's jitter, and must not pin the cushion at its cap.
    VrrSessionConfig session = config(116, 120);
    VrrTimingParameters policy = legacyPlayoutParameters(session);
    policy.playoutHistoryEnabled = 0;
    VrrTimingController controller(session, true, policy);
    const uint64_t epochUs = 1000000;
    const auto rtpFor = [](int i) {
        return static_cast<uint32_t>(static_cast<uint64_t>(i) * 90000ULL / 116ULL);
    };
    const auto jitterFor = [](int i) {
        return static_cast<uint64_t>((static_cast<uint64_t>(i) * 7919ULL) % 1500ULL);
    };
    VrrTimingDecision decision;
    for (int i = 0; i < 2500; ++i) {
        const uint64_t decodedUs = decodedTimeForRtp(epochUs, rtpFor(i)) + jitterFor(i);
        decision = controller.schedule(frame(i + 1, rtpFor(i), true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
    }
    const uint64_t delayBeforeUs = decision.playoutDelayUs;
    expect(delayBeforeUs < 4000,
           "a clean link must release the cushion well below the start value");

    const uint64_t stallEndUs = decodedTimeForRtp(epochUs, rtpFor(2500)) + 60000;
    uint64_t worstDelayUs = delayBeforeUs;
    for (int i = 2500; i < 3000; ++i) {
        uint64_t decodedUs = decodedTimeForRtp(epochUs, rtpFor(i)) + jitterFor(i);
        if (i >= 2500 && i < 2507) {
            decodedUs = stallEndUs + static_cast<uint64_t>(i - 2500) * 100;
        }
        decision = controller.schedule(frame(i + 1, rtpFor(i), true, decodedUs), decodedUs);
        controller.noteSubmission(true, false, decision.targetUs);
        worstDelayUs = std::max(worstDelayUs, decision.playoutDelayUs);
    }
    std::fprintf(stderr, "burst exclusion: delay before=%llu worst after stall=%llu\n",
                 static_cast<unsigned long long>(delayBeforeUs),
                 static_cast<unsigned long long>(worstDelayUs));
    expect(worstDelayUs <= delayBeforeUs + 500,
           "the backlog behind an arrival stall must not raise the cushion");
}

void testLatchedPresentationDropsSoftwareFloor()
{
    // At the refresh rate the source period equals the display period, so a
    // software floor of one display period plus a guard can never be met
    // and sheds a frame every second. Latched presents are ordered by the
    // flip queue instead, so the production policy reports no floor there.
    VrrSessionConfig session = config(120, 120);
    VrrTimingController production(session, true,
                                   legacyPlayoutParameters(session));
    VrrTimingController legacy(session, true, VrrTimingParameters {});
    const uint64_t startUs = 1000000;
    for (int i = 0; i < 10; ++i) {
        const uint32_t timestamp = static_cast<uint32_t>(i * 750);
        const uint64_t decodedUs = startUs + static_cast<uint64_t>(i) * 8333;
        const VrrTimingDecision produced =
            production.schedule(frame(i + 1, timestamp, true, decodedUs), decodedUs);
        production.noteSubmission(true, false, produced.targetUs);
        const VrrTimingDecision legacyDecision =
            legacy.schedule(frame(i + 1, timestamp, true, decodedUs), decodedUs);
        legacy.noteSubmission(true, false, legacyDecision.targetUs);
        if (i > 0) expect(produced.latchedPresentation && legacyDecision.latchedPresentation,
               "a source at the refresh rate must request latched presentation after its first frame");
    }
    expect(production.earliestSubmissionUs() == 0,
           "latched production presentation must not impose a software spacing floor");
    expect(legacy.earliestSubmissionUs() != 0,
           "the legacy policy must keep its spacing floor for replay fidelity");
}

void testRuntimeParametersChangePolicy()
{
    VrrTimingController defaults(config(60, 120));
    VrrTimingParameters parameters;
    parameters.guardStepUs = 500;
    VrrTimingController candidate(config(60, 120), true, parameters);
    defaults.schedule(frame(1, 0, true, 100000), 100000);
    candidate.schedule(frame(1, 0, true, 100000), 100000);
    defaults.noteSpacingDeficit(1);
    candidate.noteSpacingDeficit(1);
    expect(candidate.guardUs() > defaults.guardUs(),
           "runtime parameters must change controller policy without recompilation");
    expect(candidate.parameters().guardStepUs == 500,
           "controller must retain its resolved parameter set");
}

void testPerFrameLatchAtNativeMaximum()
{
    for (int rate : {109, 112, 116, 119, 120}) {
        auto session = config(rate, 120);
        auto policy = legacyPlayoutParameters(session);
        VrrTimingController controller(session, true, policy);
        unsigned latched = 0;
        for (int i = 0; i < 600; ++i) {
            const uint32_t rtp = uint32_t(uint64_t(i) * 90000 / rate);
            const uint64_t at = 1000000 + uint64_t(rtp) * 1000 / 90 + (i % 9) * 30;
            const auto prior = controller.lastSubmissionUs();
            const auto d = controller.schedule(frame(i, rtp, true, at), at);
            if (i > 100) latched += d.latchedPresentation;
            if (prior && !d.latchedPresentation)
                expect(d.targetUs >= prior + controller.displayPeriodUs(),
                       "every immediate submission must satisfy the display interval");
            controller.noteSubmission(true, false, d.targetUs);
        }
        if (rate <= 116) expect(latched == 0, "small jitter at 109..116 FPS must not trigger conservative latching");
        if (rate == 120) expect(latched > 400, "120 FPS must use native scanout protection without reducing the stream cap");
    }
    auto session = config(120, 120);
    VrrTimingController noLatch(session, false, legacyPlayoutParameters(session));
    for (int i = 0; i < 100; ++i) {
        auto at = 1000000 + uint64_t(i) * 8333;
        auto prior = noLatch.lastSubmissionUs();
        auto d = noLatch.schedule(frame(i, uint32_t(i * 750), true, at), at);
        expect(!d.latchedPresentation && (!prior || d.targetUs >= prior + noLatch.displayPeriodUs()),
               "backends without per-frame native latching must retain the software floor");
        noLatch.noteSubmission(true, false, d.targetUs);
    }
}

void testExplicitAdaptiveOnlyPolicy()
{
    for (int refresh : {60, 120, 144, 240}) {
        const auto session = config(refresh, refresh);
        auto policy = vrrTimingParametersForSession(session);
        policy.playoutAdaptiveOnly = 1;
        VrrTimingController controller(session, true, policy);
        uint64_t ticks = 0;
        uint64_t prior = 0;
        for (int i = 0; i < 900; ++i) {
            const int rate = i < 300 ? refresh : i < 600 ? refresh - 5 : refresh;
            ticks += 90000 / rate;
            const auto arrival = 1000000 + ticks * 1000 / 90;
            const auto d = controller.schedule(frame(i, uint32_t(ticks), true, arrival),
                                               std::max(arrival, prior));
            expect(!d.latchedPresentation, "rate changes and late work must not enable V-Sync");
            if (prior) expect(d.targetUs >= prior + controller.displayPeriodUs(),
                              "adaptive production must retain the software spacing floor");
            prior = d.targetUs + (i % 31 == 0 ? 4000 : 0);
            controller.noteSubmission(true, false, prior);
            if (i % 47 == 0) controller.noteSpacingDeficit(200);
        }
    }
}

void testProductionAdaptiveProtectionRecoversWithoutDrift()
{
    const auto session = config(120, 120);
    const auto policy = vrrTimingParametersForSession(session);
    expect(policy.playoutAdaptiveOnly == 0 && policy.playoutPerFrameLatch == 2 &&
           policy.playoutRateProtectionEnabled == 0,
           "production must choose protection for each slot, not force a rate band");
    VrrTimingController controller(session, true, policy);
    uint64_t ticks = 0, prior = 0;
    unsigned protectedAtCeiling = 0, adaptiveWithHeadroom = 0;
    for (int i = 0; i < 1800; ++i) {
        const int rate = i < 600 || i >= 1200 ? 120 : 100;
        ticks += 90000 / rate;
        const uint64_t arrival = 1000000 + ticks * 1000 / 90;
        const auto decision = controller.schedule(frame(i, uint32_t(ticks), true, arrival),
                                                  std::max(arrival, prior));
        if (decision.latchedPresentation) {
            expect(controller.earliestSubmissionUs() == 0,
                   "native protection must remove the unsustainable software floor");
            if (i > 1300) ++protectedAtCeiling;
        }
        else {
            if (prior) expect(decision.targetUs >= controller.earliestSubmissionUs(),
                              "tearing submissions must retain their spacing protection");
            if (i > 700 && i < 1100) ++adaptiveWithHeadroom;
        }
        expect(decision.targetUs < arrival + 25000,
               "120 FPS and temporary execution stalls must not accumulate pacing latency");
        // A late submission must recover on subsequent source slots rather
        // than pushing every later frame by a display period plus guard.
        prior = decision.targetUs + (i % 113 == 0 ? 4000 : 0);
        controller.noteSubmission(true, false, prior);
    }
    expect(protectedAtCeiling > 400, "native-rate slots must use protection");
    expect(adaptiveWithHeadroom > 300, "source slowdown must return to adaptive presentation");
}

void testPerFrameLatchIncludesSafetyHeadroom()
{
    for (int mode : {0, 1, 2}) {
        for (int refresh : {60, 120, 144, 165, 240, 360}) {
            auto session = config(refresh * 3 / 4, refresh);
            session.latencyMode = mode;
            session.smoothFrameTiming = false;
            const auto policy = vrrTimingParametersForSession(session);
            // A shadow schedule supplies the uncompressed source slots. No
            // preparation observations means both controllers retain the same
            // render budget and buffer; only submitted spacing differs.
            for (uint64_t slack : {224ULL, 225ULL, 399ULL, 400ULL}) {
                VrrTimingController reference(session, true, policy);
                VrrTimingController controller(session, true, policy);
                const auto period = 1000000ULL / session.streamRateHz;
                const auto a = frame(0, 0, true, 1000000);
                reference.schedule(a, 1000000);
                controller.schedule(a, 1000000);
                const auto b = frame(1, uint32_t(90000 / session.streamRateHz), true, 1000000 + period);
                const auto rawB = reference.schedule(b, b.decodeCompleteUs());
                const auto floorInterval = controller.displayPeriodUs() + controller.guardUs();
                controller.noteSubmission(true, false, rawB.targetUs - floorInterval - slack);
                auto d = controller.schedule(b, b.decodeCompleteUs());
                expect(d.latchedPresentation == (slack < policy.latchedPresentationHeadroomUs),
                       "entry must include the VRR12 safety headroom in every preset and refresh rate");

                // Force entry, then verify exit uses the larger hysteresis
                // threshold on the planned slot rather than fitted source FPS.
                const auto c = frame(2, uint32_t(180000 / session.streamRateHz), true, 1000000 + 2 * period);
                const auto rawC = reference.schedule(c, c.decodeCompleteUs());
                controller.noteSubmission(true, false, rawC.targetUs - floorInterval - 224);
                d = controller.schedule(c, c.decodeCompleteUs());
                expect(d.latchedPresentation, "a tight slot must enter native protection");
                const auto e = frame(3, uint32_t(270000 / session.streamRateHz), true, 1000000 + 3 * period);
                const auto rawE = reference.schedule(e, e.decodeCompleteUs());
                controller.noteSubmission(true, false, rawE.targetUs - floorInterval - slack);
                d = controller.schedule(e, e.decodeCompleteUs());
                expect(d.latchedPresentation == (slack < policy.latchedPresentationExitHeadroomUs),
                       "exit must retain the full VRR12 safety headroom hysteresis");
                expect(d.targetUs == rawE.targetUs,
                       "native protection must not add padding to the source deadline");
            }
        }
    }
}

void testProductionNearRefreshSafetyMargin()
{
    for (int mode : {0, 1, 2}) {
        auto session = config(116, 120);
        session.latencyMode = mode;
        session.smoothFrameTiming = false;
        const auto policy = vrrTimingParametersForSession(session);
        auto historical = policy;
        historical.playoutPerFrameLatch = 1;
        VrrTimingController controller(session, true, policy);
        VrrTimingController oldController(session, true, historical);
        unsigned protectedFrames = 0, oldProtectedFrames = 0;
        for (int i = 0; i < 600; ++i) {
            const auto ticks = uint32_t(uint64_t(i) * 90000 / 116);
            const auto at = 1000000 + uint64_t(ticks) * 1000 / 90;
            const auto f = frame(i, ticks, true, at);
            const auto d = controller.schedule(f, at);
            const auto old = oldController.schedule(f, at);
            if (i > 100) {
                protectedFrames += d.latchedPresentation;
                oldProtectedFrames += old.latchedPresentation;
            }
            expect(d.targetUs == old.targetUs && d.playoutDelayUs == old.playoutDelayUs,
                   "restoring protection must preserve VRR16's queue and planned deadlines");
            controller.noteSubmission(true, false, d.targetUs);
            oldController.noteSubmission(true, false, old.targetUs);
        }
        expect(protectedFrames == 499 && oldProtectedFrames == 0,
               "116/120 must regain protection while explicit VRR16 replay preserves its original decision");
    }
}

void testPersistentImmediateUsesSafetyFloor()
{
    for (int mode : {0, 1, 2}) {
        for (int refresh : {60, 120, 144, 165, 240, 360}) {
            auto session = config(refresh, refresh);
            session.latencyMode = mode;
            auto policy = vrrTimingParametersForSession(session);
            VrrTimingController controller(session, false, policy);
            uint64_t prior = 0;
            for (int i = 0; i < 200; ++i) {
                const auto ticks = uint32_t(uint64_t(i) * 90000 / refresh);
                const auto at = 1000000 + uint64_t(ticks) * 1000 / 90;
                const auto d = controller.schedule(frame(i, ticks, true, at), std::max(at, prior));
                expect(!d.latchedPresentation,
                       "persistent Immediate/FIFO must never request unsupported native protection");
                if (prior) {
                    const auto safe = prior + controller.displayPeriodUs() + d.guardUs +
                        policy.latchedPresentationHeadroomUs;
                    expect(controller.earliestSubmissionUs() == safe && d.targetUs >= safe,
                           "an immutable unprotected backend must enforce safety headroom through its software floor");
                }
                prior = d.targetUs + (i % 37 == 0 ? 3000 : 0);
                controller.noteSubmission(true, false, prior);
            }
        }
    }
}

void testSourceRateProtection()
{
    struct Display { int refreshHz; int cutoffHz; };
    for (const auto display : {Display{60, 59}, Display{120, 116},
                              Display{144, 138}, Display{165, 157},
                              Display{240, 224}, Display{360, 324}}) {
        for (double sourceRate : {display.cutoffHz - 0.25, double(display.cutoffHz),
                                  display.cutoffHz + 0.25, double(display.refreshHz)}) {
            for (bool canLatch : {false, true}) {
                // Negotiate native maximum but learn the actual game rate.
                const auto session = config(display.refreshHz, display.refreshHz);
                auto policy = vrrTimingParametersForSession(session);
                policy.playoutAdaptiveOnly = 0;
                policy.playoutRateProtectionEnabled = 1;
                expect(policy.playoutRateProtectionEnabled == 1,
                       "historical source-rate protection remains replayable");
                VrrTimingController controller(session, canLatch, policy);
                for (int i = 0; i < 600; ++i) {
                    const auto rtp = uint32_t(std::llround(i * 90000.0 / sourceRate));
                    const auto arrival = 1000000 + uint64_t(rtp) * 1000 / 90 + (i % 9) * 30;
                    const auto prior = controller.lastSubmissionUs();
                    const auto now = std::max(arrival, prior);
                    const auto d = controller.schedule(frame(i, rtp, true, arrival), now);
                    if (i == 0) {
                        expect(d.latchedPresentation == canLatch,
                               "native-rate startup must protect the first frame");
                    }
                    if (i > 200) {
                        expect(d.latchedPresentation == (canLatch && sourceRate >= display.cutoffHz),
                               "the fitted source rate must select protection inclusively at the old suggested cutoff");
                    }
                    if (prior && !d.latchedPresentation) {
                        expect(d.targetUs >= prior + controller.displayPeriodUs() + d.guardUs,
                               "adaptive presents must keep the software spacing floor");
                    }
                    // Late CPU/GPU completion and guard changes cannot change
                    // the selected mode while the source stays in this range.
                    controller.noteSubmission(true, false, d.targetUs + (i % 31 == 0 ? 2000 : 0));
                    if (i % 47 == 0) controller.noteSpacingDeficit(200);
                }
            }
        }
    }

    const auto session = config(120, 120);
    auto policy = vrrTimingParametersForSession(session);
    policy.playoutAdaptiveOnly = 0;
    policy.playoutRateProtectionEnabled = 1;
    VrrTimingController controller(session, true, policy);
    double ticks = 0;
    int number = 0;
    for (int rate : {110, 119, 110}) {
        for (int i = 0; i < 600; ++i, ++number) {
            ticks += 90000.0 / rate;
            const auto rtp = uint32_t(std::llround(ticks));
            const auto at = 1000000 + uint64_t(rtp) * 1000 / 90;
            const auto d = controller.schedule(frame(number, rtp, true, at), at);
            if (i > 300) {
                expect(d.latchedPresentation == (rate >= 116),
                       "source rate changes must enter protection and return to adaptive presentation");
            }
            controller.noteSubmission(true, false, d.targetUs);
        }
    }
}

void testProcessingEpisodeClassification()
{
    using R = Vrr13::Reserve;
    R history;
    Vrr13::WorkloadEpisode episode;
    episode.observe(history, 8000000, 10000000, R::Second, 30000, 16667);
    expect(history.evidence() == 0, "a work episode must remain provisional until recovery");
    episode.observe(history, 1000000, 10000000, R::Second + 16667000, 0, 16667);
    expect(history.evidence() == 2 && history.common() == 8000000,
           "a transient decoder backlog must teach its tail after draining");
    const auto before = history.evidence();
    for (int i = 0; i < 240; ++i)
        episode.observe(history, 90000000, 10000000,
                        2 * R::Second + int64_t(i) * 16667000, 100000, 16667);
    episode.observe(history, 1000000, 10000000, 7 * R::Second, 0, 16667);
    expect(history.evidence() == before + 1 && history.common() == 8000000,
           "sustained processing overload must not inflate the receiver jitter buffer");
}

void testRollingPlayoutHistory()
{
    using R = Vrr13::Reserve;
    R memory;
    for (int i = 0; i <= 600; ++i)
        memory.observe(i % 100 == 0 ? 8000000 : 1000000, 9000000,
                       R::Second + int64_t(i) * R::Second / 120);
    expect(memory.common() == 8000000, "five-minute history must retain recurring receiver tails");
    const auto checkpoint = memory.checkpoint();
    R restored;
    expect(restored.restore(checkpoint), "a checkpoint must restore the complete histogram and expiration clock");
    for (int i = 1; i <= 300; ++i) {
        const auto at = 6 * R::Second + int64_t(i) * R::Second / 30;
        memory.observe(1000000, 9000000, at);
        restored.observe(1000000, 9000000, at);
    }
    expect(memory.checkpoint() == restored.checkpoint(), "checkpoint continuation must be exact across an FPS change");
    expect(memory.common() == 8000000, "30 FPS must not discard the jitter learned at 120 FPS");
    memory.observe(1000000, 9000000, 307 * R::Second);
    expect(memory.common() == 1000000, "expired evidence must not pin latency forever");
    R prior;
    expect(prior.loadProfile(restored.profile()), "compatible jitter profiles must load");
    expect(prior.evidence() == 0 && !prior.canRelease(), "cached history is not fresh validation");
    prior.age(14);
    expect(prior.cachedEvidence() < restored.evidence(), "offline aging must reduce prior evidence");
    auto invalid = restored.profile(); invalid[0] = 12;
    expect(!prior.loadProfile(invalid), "VRR14 recovery-budget profiles must not be accepted by VRR13");
}

void testHistoryPreservesVrr13Scheduling()
{
    const auto session = config(60, 144);
    auto policy = legacyPlayoutParameters(session);
    expect(policy.playoutMetronomeEnabled == 0 && policy.playoutSmoothingGainPerMille == 200,
           "historical replay must retain VRR13's gain smoother");
    auto legacy = policy;
    legacy.playoutHistoryEnabled = 0;
    // Equal fixed buffers isolate the scheduling mechanism from its learner.
    policy.playoutDelayAdaptive = legacy.playoutDelayAdaptive = 0;
    VrrTimingController current(session, true, policy), before(session, true, legacy);
    for (int i = 0; i < 1000; ++i) {
        uint32_t rtp = uint32_t(i * 1500 + (i % 2 ? 90 : 0));
        uint64_t at = 1000000 + uint64_t(rtp) * 1000 / 90 + (i % 7) * 250;
        auto x = current.schedule(frame(i, rtp, true, at), at);
        auto y = before.schedule(frame(i, rtp, true, at), at);
        expect(x.targetUs == y.targetUs && x.cadenceSmoothingUs == y.cadenceSmoothingUs &&
               x.renderStartUs == y.renderStartUs, "buffer-only changes must leave the VRR13 scheduler identical");
        current.noteSubmission(true, false, x.targetUs);
        before.noteSubmission(true, false, y.targetUs);
    }
}

void testHistoryLearningAndQueueCapacity()
{
    for (int rate : {30, 60, 120, 240}) {
        auto session = config(rate, 360);
        auto policy = legacyPlayoutParameters(session);
        VrrTimingController controller(session, true, policy);
        for (int i = 0; i < rate * 12; ++i) {
            auto rtp = uint32_t(uint64_t(i) * 90000 / rate);
            auto at = 1000000 + uint64_t(rtp) * 1000 / 90;
            auto d = controller.schedule(frame(i, rtp, true, at), at);
            expect(d.playoutDelayUs <= controller.playoutQueueLimitUs(),
                   "buffer plus preparation and smoothing must leave a slot for the next arrival");
            controller.noteSubmission(true, false, d.targetUs);
        }
        expect(controller.playoutHistory().evidence() >= uint64_t(rate * 10),
               "low FPS is not a host stall and must warm the same histogram");
        expect(controller.playoutDelayUs() <= policy.playoutDelayMinimumUs + 500,
               "clean startup must release in wall time at every source FPS");
    }
    auto session = config(60, 144);
    auto policy = legacyPlayoutParameters(session);
    VrrTimingController controller(session, true, policy);
    for (int i = 0; i < 800; ++i) {
        auto rtp = uint32_t(i * 1500);
        // Steady sender, receiver stall. The original deadline must survive.
        auto at = 1000000 + uint64_t(rtp) * 1000 / 90 + (i == 799 ? 35000 : 0);
        auto d = controller.schedule(frame(i, rtp, true, at), at);
        if (i == 799) {
            expect(d.originalTargetUs < at && d.targetUs >= at,
                   "readiness clamps must never rewrite the original deadline");
            expect(controller.playoutHistory().common() >= 30000000,
                   "a steady sender's delivery stall must enter receiver jitter history");
        }
        controller.noteSubmission(true, false, d.targetUs);
    }
}

} // namespace

void testVrr14Prediction()
{
    using R = Vrr13::Reserve;
    R reserve(14);
    for (int i = 0; i <= 600; ++i)
        reserve.observe(9000000, 6000000, R::Second + int64_t(i) * R::Second / 120);
    expect(reserve.misses() == 0, "exactly 3 ms over available buffer is tolerated");
    expect(reserve.target(100000000, 0, 0) == 6000000, "native ceiling retains tail minus 3 ms tolerance");
    expect(reserve.target(100000000, 0, 4000000) == 2000000, "free recovery room must reduce queued protection");
    expect(reserve.target(100000000, 0, 10000000) == 0, "headroom credit cannot make protection negative");
    R old;
    expect(!old.loadProfile(reserve.profile()), "VRR13 must reject combined-readiness history");
    R restored(14);
    expect(restored.restore(reserve.checkpoint()), "VRR14 checkpoints must restore their tolerance and history");
    reserve.observe(9000001, 6000000, 7 * R::Second);
    restored.observe(9000001, 6000000, 7 * R::Second);
    expect(reserve.misses() == 1 && reserve.checkpoint() == restored.checkpoint(), "a miss beyond 3 ms and checkpoint continuation must agree");

    Vrr13::ReadinessPrediction workload;
    R ready(14);
    Vrr13::ReadinessPrediction::Probe probe{1001000, 1000000, 16667, 1000, 1000, 8334, 300, 0, true};
    workload.observe(ready, probe, 3000, 500);
    expect(ready.common() == 3500000, "combined readiness learns receiver jitter plus excess service and scheduling");
    expect(ready.misses() == 0, "miss accounting includes the same recovery credit as target selection");
    const auto evidence = ready.evidence();
    for (int i = 1; i < 180; ++i) {
        probe.decoded = 1001000 + uint64_t(i) * 16667;
        probe.expected = probe.decoded - 1000;
        probe.decoderQueue = 50000;
        workload.observe(ready, probe, 1000, 0);
    }
    probe.decoderQueue = 0; probe.decoded += 16667; probe.expected += 16667;
    workload.observe(ready, probe, 1000, 0);
    expect(ready.evidence() == evidence + 1, "sustained decoder overload must not become permanent buffer history");

    Vrr13::PresentationPrediction presentation;
    Vrr13::PresentationObservation o;
    o.timeKind = Vrr13::PresentationTimeKind::RefreshReference;
    o.submitted = o.idValid = o.sampleValid = o.dxgi = true;
    o.id = o.sampleId = 1; o.submission = 1000000; o.ready = 999000;
    o.deadline = 1004000; o.observed = 1005000; o.sampleTime = 1002000;
    o.presentRefresh = 11; o.syncRefresh = 10;
    presentation.observe(o, false);
    expect(presentation.measured() == 0, "unrelated DXGI sync timestamps must not teach presentation latency");
    o.submitted = false; o.syncRefresh = 11; o.sampleTime = 1004000;
    presentation.observe(o, false);
    expect(presentation.measured() == 1 && presentation.lead(1005000) == 4000,
           "historical replay retains refresh-derived compositor lead");
    expect(presentation.misses() == 0 && presentation.floor(1005000, 8333, 100) == 1012433,
           "original scanout deadline and physical display spacing are independent measurements");
    expect(presentation.lead(1200000) == 0 && presentation.floor(1200000, 8333, 100) == 0,
           "stale native feedback must stop controlling predictions");
    presentation.reset();
    o.submitted = true; o.submission = 1006000; o.sampleTime = 1004000;
    presentation.observe(o, false);
    expect(presentation.measured() == 0, "even equal refresh counters cannot timestamp a present that happened afterward");

    uint64_t delays[2]{};
    for (int k = 0; k < 2; ++k) {
        const int rate = k ? 60 : 120;
        const auto session = config(rate, 120);
        auto policy = legacyPlayoutParameters(session);
        policy.playoutPredictionEnabled = 1;
        VrrTimingController controller(session, true, policy);
        for (int i = 0; i < 2400; ++i) {
            const uint32_t rtp = uint32_t(uint64_t(i) * 90000 / rate);
            const auto decoded = decodedTimeForRtp(1000000, rtp) + (i % 10 == 9 ? 7000 : 0);
            const auto d = controller.schedule(frame(i, rtp, true, decoded), decoded);
            controller.notePreparationDuration(1000);
            controller.noteSchedulerDelays(0, 0, true);
            controller.noteSubmission(true, false, std::max(d.targetUs, decoded + 1000));
            delays[k] = d.playoutDelayUs;
            expect(d.predictedScanoutUs == d.targetUs + d.compositorLeadUs,
                   "predicted scanout must include learned compositor lead exactly once");
        }
    }
    expect(delays[1] + 2000 < delays[0], "the same receiver jitter must require less queued delay at 60 FPS than at 120 FPS");
    {
        const auto session = config(60, 120);
        auto policy = legacyPlayoutParameters(session);
        policy.playoutPredictionEnabled = 1;
        VrrTimingController clean(session, true, policy), late(session, true, policy);
        for (int i = 0; i < 180; ++i) {
            const uint32_t rtp = uint32_t(i * 1500);
            const auto at = decodedTimeForRtp(1000000, rtp);
            const auto lateAt = at + (i == 170 ? 15000 : 0);
            const auto a = clean.schedule(frame(i, rtp, true, at), at);
            const auto b = late.schedule(frame(i, rtp, true, lateAt), lateAt);
            if (i == 171) expect(std::abs(int64_t(a.originalTargetUs) - int64_t(b.originalTargetUs)) <= 200,
                "a late execution must not re-anchor the smoothed source clock and consume free recovery headroom");
            clean.notePreparationDuration(1000); late.notePreparationDuration(1000);
            clean.noteSubmission(true, false, a.targetUs); late.noteSubmission(true, false, b.targetUs);
        }
    }
    {
        auto session = config(60, 120);
        VrrTimingController controller(session, true, legacyFeedbackParameters(session));
        for (int i = 0; i < 20; ++i) {
            const auto at = uint64_t(1000000 + i * 16667);
            const auto d = controller.schedule(frame(i, uint32_t(i * 1500), true, at), at);
            if (i > 2) expect(d.compositorLeadUs == 2000,
                "historical feedback policies must reproduce subsequent scanout predictions");
            controller.notePreparationDuration(i == 15 ? 6000 : 1000);
            controller.noteSubmission(true, false, d.targetUs);
            Vrr13::PresentationObservation sample;
            sample.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
            sample.submitted = sample.idValid = sample.sampleValid = true;
            sample.id = sample.sampleId = uint64_t(i + 1);
            sample.submission = sample.ready = d.targetUs;
            sample.sampleTime = d.targetUs + 2000; sample.observed = sample.sampleTime;
            sample.deadline = d.originalScanoutUs; sample.latched = d.latchedPresentation;
            controller.notePresentation(sample);
        }
        expect(controller.typicalRenderUs() == 1000 && controller.renderLeadUs() > controller.typicalRenderUs(),
               "one render tail must increase preparation lead without becoming the normal playout offset");
    }
}

void testRefreshReferencesAreNotDisplayEvents()
{
    using namespace Vrr13;
    PresentationPrediction prediction;
    PresentationObservation first;
    first.submitted = first.idValid = first.sampleValid = first.dxgi = true;
    first.timeKind = PresentationTimeKind::RefreshReference;
    first.id = first.sampleId = 29;
    first.submission = first.ready = 895965;
    first.sampleTime = 902785;
    first.observed = 903325;
    first.presentRefresh = first.syncRefresh = 260933;
    first.deadline = 902785;
    prediction.observe(first);
    expect(prediction.measured() == 0 && prediction.lead(first.observed) == 0,
           "even a causal equal-refresh reference cannot certify a display event");

    auto second = first;
    second.id = second.sampleId = 30;
    second.submission = second.ready = 903178;
    second.observed = 910213;
    prediction.observe(second);
    expect(prediction.measured() == 0,
           "a newer present sharing the old refresh reference must remain unmeasured");

    // Delayed exact events remain usable for both DXGI and other backends.
    second.submitted = false;
    second.timeKind = PresentationTimeKind::DisplayEvent;
    second.sampleTime = 905178;
    prediction.observe(second);
    expect(prediction.measured() == 1 && prediction.lead(second.observed) == 2000,
           "an explicit display event matches the retained submission directly");
    prediction.observe(second);
    expect(prediction.measured() == 1, "duplicate display events cannot inflate coverage");

    const auto session = config(100, 120);
    const auto policy = vrrTimingParametersForSession(session);
    expect(policy.playoutRequireDisplayEvents == 1,
           "optional display cadence diagnostics must require real display events");
    VrrTimingController control(session, true, policy);
    VrrTimingController referenceOnly(session, true, policy);
    for (int i = 1; i <= 120; ++i) {
        const auto at = uint64_t(1000000 + i * 10000);
        const auto a = control.schedule(frame(i, i * 900, true, at), at);
        const auto b = referenceOnly.schedule(frame(i, i * 900, true, at), at);
        expect(a.targetUs == b.targetUs && a.playoutDelayUs == b.playoutDelayUs &&
                   b.compositorLeadUs == 0 && b.nativeSmoothnessSamples == 0 &&
                   referenceOnly.nativeCadenceIntervals() == 0 && referenceOnly.nativeCadenceHitches() == 0,
               "refresh references must not change latency learning or buffer adaptation");
        control.noteSubmission(true, false, a.targetUs);
        referenceOnly.noteSubmission(true, false, b.targetUs);
        PresentationObservation observation;
        observation.timeKind = PresentationTimeKind::RefreshReference;
        observation.dxgi = observation.submitted = observation.idValid = observation.sampleValid = true;
        observation.smoothness = referenceOnly.smoothnessSample(b);
        observation.id = observation.sampleId = uint64_t(i);
        observation.submission = observation.ready = b.targetUs;
        observation.sampleTime = b.targetUs + (i % 2 ? 1000 : 7000);
        observation.observed = observation.sampleTime;
        observation.presentRefresh = observation.syncRefresh = uint64_t(i);
        observation.deadline = b.originalScanoutUs;
        referenceOnly.notePresentation(observation);
    }
}

void testSmoothnessFeedback()
{
    using Feedback = Vrr13::SmoothnessFeedback;
    using R = Vrr13::Reserve;
    R raw(15);
    for (int i = 0; i < 400; ++i) raw.observe(8000000, 5000000, R::Second + int64_t(i) * 16667000);
    expect(raw.target(100000000, 0, 2000000) == 6000000,
           "8 ms required protection minus 2 ms headroom must give 6 ms buffer; tolerance is not a credit");
    expect(raw.misses() == 400, "version 15 must count exactly 3 ms as a violation");
    R restored(15);
    expect(restored.restore(raw.checkpoint()) && restored.checkpoint() == raw.checkpoint(),
           "new readiness semantics must preserve exact versioned checkpoints");
    for (uint64_t error : {2999ULL, 3000ULL, 3001ULL}) {
        Feedback f;
        f.observe({1, 1000000, 1000000, 6000, 2000, 0, true}, 1000000);
        f.observe({2, 1016667 + error, 1016667, 6000, 2000, 0, true}, 1025000);
        expect(f.samples() == 1 && f.misses() == (error >= 3000), "smoothness threshold must be inclusive at 3 ms");
        expect((f.protectionUs() > 8000) == (error >= 3000), "only a true interval violation must raise required protection");
    }
    Feedback stable;
    for (uint64_t i = 0; i < 200; ++i)
        stable.observe({i, 1010000 + i * 16667, 1000000 + i * 16667, 6000, 2000, 0, true}, 1010000 + i * 16667);
    expect(stable.samples() == 199 && stable.misses() == 0 && !stable.protectionUs(),
           "constant 10 ms lateness is smooth and must not cause buffer growth");
    Feedback missing;
    missing.observe({1, 1000000, 1000000, 6000, 2000, 0, true}, 1000000);
    missing.observe({3, 1040000, 1033334, 6000, 2000, 0, true}, 1040000);
    missing.observe({2, 1016667, 1016667, 6000, 2000, 0, true}, 1040000);
    expect(!missing.samples(), "missing and out-of-order native samples cannot manufacture success or failure");
    Feedback delayed;
    delayed.observe({1, 1000000, 1000000, 6000, 2000, 0, true}, 1100000);
    delayed.observe({2, 1022667, 1016667, 6000, 2000, 0, true}, 1100000);
    const auto demanded = delayed.protectionUs();
    delayed.observe({3, 1033334, 1033334, 30000, 2000, 0, true}, 1100000);
    expect(delayed.misses() == 2 && delayed.protectionUs() == demanded,
           "catch-up and delayed feedback must charge the late frame's original protection, not today's larger buffer");
    Feedback uncertain;
    uncertain.observe({1, 1000000, 1000000, 6000, 2000, 100, true}, 1000000);
    uncertain.observe({2, 1019667, 1016667, 6000, 2000, 100, true}, 1020000);
    expect(!uncertain.samples(), "an uncertain threshold crossing must remain unavailable evidence");
    Feedback capped;
    capped.observe({1, 1000000, 1000000, 100000, 0, 0, true}, 1000000);
    capped.observe({2, 1026667, 1016667, 100000, 0, 0, true}, 1026667);
    expect(capped.misses() == 1, "histogram saturation must not hide a smoothness failure");

    const auto session = config(60, 120);
    auto policy = legacyFeedbackParameters(session);
    // Preserve the historical feedback law for exact replay regression coverage.
    policy.playoutNativeHitchAdaptation = 0;
    policy.playoutReadinessDrivenAdaptation = 0;
    VrrTimingController controller(session, true, policy);
    uint64_t before = 0, after = 0, lateMisses = 0, samples = 0;
    for (int i = 0; i < 4000; ++i) {
        const auto at = decodedTimeForRtp(1000000, uint32_t(i * 1500));
        const auto d = controller.schedule(frame(i, uint32_t(i * 1500), true, at), at);
        if (i == 299) before = d.playoutDelayUs;
        // Delay occurs outside the readiness predictor: only actual timing can teach it.
        const auto submitted = std::max(d.targetUs, at + (i >= 300 && i % 10 == 9 ? 15000 : 1000));
        controller.notePreparationDuration(1000);
        controller.noteSubmission(true, false, submitted);
        if (i == 2000) { lateMisses = d.submissionSmoothnessMisses; samples = d.submissionSmoothnessSamples; }
        if (i == 3999) {
            after = d.playoutDelayUs;
            expect(d.submissionSmoothnessSamples > samples + 1900 && d.submissionSmoothnessMisses == lateMisses,
                   "closed-loop correction must eliminate repeated interval misses after learning");
            expect(d.smoothnessProtectionUs > 0 && !d.playoutCapacityLimited,
                   "feedback must request real protection within available queue capacity");
        }
    }
    expect(after > before + 3000, "actual misses must grow the live buffer even when readiness predicts success");

    VrrTimingController native(session, true, policy);
    uint64_t nativeProtection = 0;
    for (int i = 0; i < 60; ++i) {
        const auto at = decodedTimeForRtp(2000000, uint32_t(i * 1500));
        const auto d = native.schedule(frame(i, uint32_t(i * 1500), true, at), at);
        native.notePreparationDuration(1000);
        native.noteSubmission(true, false, d.targetUs);
        Vrr13::PresentationObservation o;
        o.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
        o.smoothness = native.smoothnessSample(d);
        o.submitted = o.idValid = o.sampleValid = true;
        o.id = o.sampleId = uint64_t(i + 1);
        o.submission = o.ready = d.targetUs;
        o.sampleTime = d.targetUs + (i == 40 ? 8000 : 2000);
        o.observed = o.sampleTime;
        o.deadline = d.originalScanoutUs; o.latched = d.latchedPresentation;
        native.notePresentation(o);
        if (i == 59) {
            nativeProtection = d.smoothnessProtectionUs;
            expect(d.nativeSmoothnessSamples > 40 && d.nativeSmoothnessMisses > 0,
                   "matched native presentation intervals must feed back into the live controller");
            expect(native.nativeCadenceIntervals() > 40 && native.nativeCadenceHitches() > 0,
                   "explicit display events must also publish cadence reporting counters");
        }
    }
    expect(nativeProtection > 0, "native-only misses must create protection demand");
}

void testStableNativeSmoothnessReference()
{
    const auto session = config(60, 120);
    auto policy = legacyFeedbackParameters(session);
    auto legacyPolicy = policy;
    legacyPolicy.playoutStableSmoothnessReference = 0;
    legacyPolicy.playoutNativeHitchAdaptation = 0;
    VrrTimingController current(session, true, policy);
    VrrTimingController legacy(session, true, legacyPolicy);
    Vrr13::SmoothnessFeedback stable, moving;
    // Recorded frames 7540/7541: the estimated compositor lead appeared
    // between frames, while observed presentation stayed close to cadence.
    VrrTimingDecision a, b;
    a.frameNumber = 7540; b.frameNumber = 7541;
    a.cadenceEligible = b.cadenceEligible = true;
    a.sourcePeriodUs = b.sourcePeriodUs = 9000;
    a.sourceIntervalUs = b.sourceIntervalUs = 9000;
    a.playoutDelayUs = b.playoutDelayUs = 16000;
    a.originalTargetUs = a.originalScanoutUs = 1000000;
    b.originalTargetUs = 1008698;
    a.sourceTimeUs = a.originalTargetUs;
    b.sourceTimeUs = b.originalTargetUs;
    b.originalScanoutUs = b.originalTargetUs + 4974;
    for (auto* controller : {&current, &legacy}) {
        auto first = controller->smoothnessSample(a);
        auto second = controller->smoothnessSample(b);
        first.at = 1005000; second.at = first.at + 8287;
        auto& feedback = controller == &current ? stable : moving;
        feedback.observe(first, first.at);
        feedback.observe(second, second.at);
    }
    expect(stable.samples() == 1 && stable.misses() == 0 && stable.protectionUs() == 0,
           "a changing compositor estimate must not manufacture native protection demand");
    expect(moving.misses() == 1 && moving.protectionUs() > 16000,
           "legacy replay must retain the captured moving-reference miss");

    // Also exercise the native predictor acquiring its first latency sample.
    for (int i = 0; i < 100; ++i) {
        const auto at = decodedTimeForRtp(2000000, uint32_t(i * 1500));
        const auto d = current.schedule(frame(i, uint32_t(i * 1500), true, at), at);
        current.notePreparationDuration(1000);
        current.noteSubmission(true, false, d.targetUs);
        Vrr13::PresentationObservation o;
        o.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
        o.smoothness = current.smoothnessSample(d);
        o.submitted = o.idValid = o.sampleValid = true;
        o.id = o.sampleId = uint64_t(i + 1);
        o.submission = o.ready = d.targetUs;
        o.sampleTime = d.targetUs + 5000;
        o.observed = o.sampleTime;
        o.deadline = d.originalScanoutUs; o.latched = d.latchedPresentation;
        current.notePresentation(o);
        if (i == 99) {
            expect(d.nativeSmoothnessSamples > 80 && d.nativeSmoothnessMisses == 0,
                   "learning a constant native latency must preserve smooth cadence");
        }
    }
}

void testPreparationKeepsLearnedLead()
{
    const auto session = config(120, 120);
    const auto policy = vrrTimingParametersForSession(session);
    VrrTimingController controller(session, true, policy);
    uint64_t previousSubmission = 0;
    bool sawLargerLead = false;
    for (int i = 0; i < 300; ++i) {
        const auto decoded = decodedTimeForRtp(1000000, uint32_t(i * 750));
        const auto now = std::max(decoded, previousSubmission + 100);
        const auto d = controller.schedule(frame(i, uint32_t(i * 750), true, decoded), now);
        if (i > 20) {
            expect(d.targetUs - d.renderStartUs >= d.renderLeadUs + d.renderWakeLeadUs,
                   "post-present spacing must not truncate learned preparation lead");
            if (i > 160 && d.renderLeadUs >= 5000) sawLargerLead = true;
        }
        const uint64_t work = i < 150 ? 3500 : 5000;
        const auto ready = std::max(now, d.renderStartUs) + work;
        controller.notePreparationDuration(work);
        previousSubmission = std::max(ready, d.targetUs);
        controller.noteSubmission(true, false, previousSubmission);
    }
    expect(sawLargerLead, "longer preparation must earn rendering time, not only a later target");
}

void testGpuReadinessLeadIsSeparateFromTarget()
{
    const auto session = config(60, 120);
    const auto production = vrrTimingParametersForSession(session);
    auto noGpuAdaptation = production;
    noGpuAdaptation.playoutGpuReadinessAdaptation = 0;
    VrrTimingController adapted(session, true, production);
    VrrTimingController baseline(session, true, noGpuAdaptation);

    // Feed the same completed GPU wait to the adapted controller and the
    // equivalent non-GPU preparation cost to the baseline. The learned term
    // must buy render-start time without moving the source presentation slot.
    for (int i = 0; i < 80; ++i) {
        const uint32_t rtp = static_cast<uint32_t>(i * 1500);
        const uint64_t decoded = decodedTimeForRtp(1000000, rtp);
        const auto a = adapted.schedule(frame(i, rtp, true, decoded), decoded);
        const auto b = baseline.schedule(frame(i, rtp, true, decoded), decoded);
        expect(a.targetUs == b.targetUs,
               "GPU readiness adaptation must not move the presentation target");
        adapted.notePreparationDuration(7000, 0, decoded + 7000, 6000);
        baseline.notePreparationDuration(1000, 0, decoded + 1000, 0);
        adapted.noteGpuReadyWait(6000, true, decoded + 7000);
        adapted.noteSubmission(true, false, a.targetUs);
        baseline.noteSubmission(true, false, b.targetUs);
    }

    const uint32_t rtp = 80U * 1500U;
    const uint64_t decoded = decodedTimeForRtp(1000000, rtp);
    const auto adaptedDecision = adapted.schedule(
        frame(80, rtp, true, decoded), decoded);
    const auto baselineDecision = baseline.schedule(
        frame(80, rtp, true, decoded), decoded);
    expect(adaptedDecision.gpuReadinessLeadUs > 0,
           "completed GPU waits must earn a readiness lead");
    expect(adaptedDecision.gpuReadinessLeadUs <=
               std::min<uint64_t>(production.playoutGpuReadinessMaximumUs,
                                  adaptedDecision.sourcePeriodUs),
           "GPU readiness lead must remain within its source-period ceiling");
    expect(adaptedDecision.targetUs == baselineDecision.targetUs,
           "learned GPU readiness must leave the source target unchanged");
    expect(adaptedDecision.renderStartUs < baselineDecision.renderStartUs,
           "learned GPU readiness must advance the render-start deadline");

    VrrTimingController failed(session, true, production);
    failed.schedule(frame(0, 0, true, 1000000), 1000000);
    failed.noteGpuReadyWait(12000, false, 1007000);
    expect(failed.gpuReadinessLeadUs() == 0,
           "failed GPU waits must not train readiness head start");
}

void testReadinessDrivenPadding()
{
    // Identical FIFO and three-frame capacity throughout. Only time padding
    // changes. Native/submission delay after readiness must remain visible,
    // without becoming a demand for still more padding on the next frame.
    for (int faultKind : {0, 1, 2, 3}) {
        for (uint64_t cap : {8000ULL, 16000ULL}) {
            const auto session = config(60, 120);
            auto policy = legacyFeedbackParameters(session);
            policy.playoutNativeHitchAdaptation = 0;
            policy.playoutDelayMaximumUs = cap;
            VrrTimingController controller(session, true, policy);
            uint64_t previousActual = 0, previousIntended = 0;
            uint64_t badPairs = 0, finalDelay = 0, misses = 0;
            for (int i = 0; i < 3000; ++i) {
                const uint64_t source = 1000000 + uint64_t(i) * 1000000 / 60;
                const bool fault = i >= 180 && i % 10 == 9;
                const uint64_t decoded = source + (faultKind == 1 && fault ? 12000 : 0);
                const auto d = controller.schedule(frame(i, uint32_t(i * 1500), true, decoded), decoded);
                const uint64_t work = faultKind == 3 && fault ? 13000 : 1000;
                const auto ready = decoded + work;
                const auto submitted = std::max(d.targetUs, ready) +
                    (faultKind == 2 && fault ? 5000 : 0);
                controller.notePreparationDuration(work);
                controller.noteSubmission(true, false, submitted);
                Vrr13::PresentationObservation o;
                o.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
                o.smoothness = controller.smoothnessSample(d);
                o.submitted = o.idValid = o.sampleValid = true;
                o.id = o.sampleId = uint64_t(i + 1);
                o.submission = submitted; o.ready = ready;
                o.sampleTime = submitted + 2000 + (faultKind == 0 && fault ? 5000 : 0);
                o.observed = o.sampleTime;
                o.deadline = d.originalScanoutUs; o.latched = d.latchedPresentation;
                controller.notePresentation(o);
                if (i >= 2000) {
                    const auto error = int64_t(o.sampleTime - previousActual) -
                                       int64_t(d.originalTargetUs - previousIntended);
                    badPairs += error >= 3000 || error <= -3000;
                }
                previousActual = o.sampleTime; previousIntended = d.originalTargetUs;
                finalDelay = d.playoutDelayUs;
                misses = d.nativeSmoothnessMisses;
                expect(d.playoutDelayUs <= cap, "applied padding must respect its time cap");
            }
            if (faultKind == 0 || faultKind == 2) {
                expect(finalDelay == policy.playoutDelayMinimumUs,
                       "post-ready jitter must not ratchet padding or block its release");
                expect(badPairs > 100 && misses > 100,
                       "post-ready jitter must remain visible in measured cadence");
            }
            else if (cap == 16000) {
                expect(finalDelay >= 12000 && finalDelay < cap && badPairs == 0,
                       "arrival or preparation jitter must learn enough padding without subtracting future recovery time");
            }
            else {
                expect(finalDelay == cap && badPairs > 100,
                       "insufficient padding must respect the cap and retain evidence of misses");
            }
        }
    }
}

void testDelayedDisplayEventsAgreeAcrossBackends()
{
    const auto session = config(60, 120);
    const auto policy = vrrTimingParametersForSession(session);
    VrrTimingController linuxController(session, true, policy);
    VrrTimingController windowsController(session, true, policy);
    std::array<Vrr13::PresentationObservation, 3> delayed{};
    uint64_t beforeHitch = 0, maximumAfterHitch = 0;
    for (uint64_t i = 1; i < 700; ++i) {
        const uint64_t source = decodedTimeForRtp(1000000, uint32_t(i * 1500));
        const auto linuxDecision = linuxController.schedule(frame(i, uint32_t(i * 1500), true, source), source);
        const auto windowsDecision = windowsController.schedule(frame(i, uint32_t(i * 1500), true, source), source);
        expect(linuxDecision.playoutDelayUs == windowsDecision.playoutDelayUs &&
               linuxDecision.nativeSmoothnessSamples == windowsDecision.nativeSmoothnessSamples,
               "verified delayed display events must drive the same padding across backends");
        linuxController.notePreparationDuration(1000);
        windowsController.notePreparationDuration(1000);
        linuxController.noteSubmission(true, false, linuxDecision.targetUs);
        windowsController.noteSubmission(true, false, windowsDecision.targetUs);
        Vrr13::PresentationObservation current;
        current.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
        current.smoothness = linuxController.smoothnessSample(linuxDecision);
        current.submitted = current.idValid = true;
        current.id = i;
        current.submission = linuxDecision.targetUs;
        current.ready = source + 1000;
        current.deadline = linuxDecision.originalScanoutUs;
        current.observed = linuxDecision.targetUs + 100;
        current.uncertainty = 10;
        // Real feedback arrives three submissions later, with IDs belonging to
        // that older image, not to the frame currently being submitted.
        const auto older = delayed[i % delayed.size()];
        if (older.id) {
            current.sampleValid = true;
            current.sampleId = older.id;
            current.sampleTime = older.submission + 2000 + (older.id == 300 ? 6000 : 0);
            current.presentRefresh = current.syncRefresh = older.id;
        }
        delayed[i % delayed.size()] = current;
        linuxController.notePresentation(current);
        current.dxgi = true;
        windowsController.notePresentation(current);
        if (i == 299) beforeHitch = linuxDecision.playoutDelayUs;
        if (i > 303) maximumAfterHitch = std::max(maximumAfterHitch, linuxDecision.playoutDelayUs);
    }
    expect(maximumAfterHitch <= beforeHitch && linuxController.nativeCadenceHitches() > 0,
           "delayed compositor hitches must remain diagnostic without growing predictive padding");
}

void testNativeHitchGatesPadding()
{
    for (uint64_t error : {2999ULL, 3000ULL, 3001ULL}) {
        Vrr13::SmoothnessFeedback feedback;
        feedback.observe({1, 1000000, 1000000, 6000, 0, 0, true}, 1000000, true);
        const auto demand = feedback.observe(
            {2, 1016667 + error, 1016667, 6000, 0, 0, true}, 1025000, true);
        expect((demand != 0) == (error > 3000),
               "native growth must require strictly more than 3 ms of client-added interval error");
    }
    Vrr13::SmoothnessFeedback gameCadence;
    uint64_t intended = 1000000;
    for (uint64_t i = 1; i < 100; ++i) {
        intended += i % 2 ? 8333 : 16667;
        expect(gameCadence.observe({i, intended + 10000, intended, 6000, 0, 0, true},
                                   intended + 10000, true) == 0,
               "game cadence changes and constant latency must not count as client hitches");
    }
    Vrr13::SmoothnessFeedback uncertain;
    uncertain.observe({1, 1000000, 1000000, 6000, 0, 100, true}, 1000000, true);
    expect(uncertain.observe({2, 1019867, 1016667, 6000, 0, 100, true}, 1020000, true) == 0 &&
           uncertain.samples() == 0,
           "a native error whose uncertainty reaches 3 ms cannot confirm a hitch");
    const auto session = config(60, 120);
    const auto policy = legacyFeedbackParameters(session);
    expect(policy.playoutNativeHitchAdaptation == 1,
           "historical native-hitch captures must retain their feedback policy");
    for (int scenario : {0, 1, 2, 3}) {
        VrrTimingController controller(session, true, policy);
        uint64_t initial = 0, beforeHitch = 0, maximumAfterHitch = 0, finalDelay = 0;
        for (int i = 0; i < 1500; ++i) {
            const uint64_t source = decodedTimeForRtp(1000000, uint32_t(i * 1500));
            const uint64_t decoded = source + (scenario == 1 && i % 10 == 9 ? 12000 :
                                               scenario == 3 && i % 10 == 9 ? 2000 : 0);
            const auto d = controller.schedule(frame(i, uint32_t(i * 1500), true, decoded), decoded);
            if (!i) initial = d.playoutDelayUs;
            if (i == 299) beforeHitch = d.playoutDelayUs;
            controller.notePreparationDuration(scenario == 1 && i % 10 == 9 ? 12000 : 1000);
            // CPU submission jitter alone is not proof of displayed jitter.
            const auto submitted = d.targetUs + (scenario == 0 && i % 10 == 9 ? 5000 : 0);
            controller.noteSubmission(true, false, submitted);
            if (scenario >= 2) {
                Vrr13::PresentationObservation o;
                o.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
                o.smoothness = controller.smoothnessSample(d);
                o.submitted = o.idValid = o.sampleValid = true;
                o.id = o.sampleId = uint64_t(i + 1);
                o.submission = submitted; o.ready = decoded + 1000;
                o.sampleTime = submitted + 2000 + (scenario == 2 && i == 300 ? 6000 : 0);
                o.observed = o.sampleTime; o.deadline = d.originalScanoutUs;
                o.latched = d.latchedPresentation;
                controller.notePresentation(o);
            }
            if (scenario < 2 || scenario == 3)
                expect(d.playoutDelayUs <= initial,
                       "readiness estimates and CPU jitter cannot authorize buffer growth");
            if (i > 300) maximumAfterHitch = std::max(maximumAfterHitch, d.playoutDelayUs);
            finalDelay = d.playoutDelayUs;
        }
        if (scenario < 2)
            expect(finalDelay == initial, "missing native evidence must not authorize growth or release");
        if (scenario == 2)
            expect(maximumAfterHitch > beforeHitch + 2000,
                   "a confirmed native interval hitch must authorize bounded buffer growth");
        if (scenario == 3)
            expect(finalDelay >= 5000 && finalDelay < initial,
                   "smooth native presentation may release padding but must retain 3 ms above readiness demand");
    }
    Vrr13::Reserve previous(16);
    for (int i = 0; i < 300; ++i)
        previous.observe(15000000, 16000000, Vrr13::Reserve::Second + int64_t(i) * 16667000);
    VrrTimingController controller(session, true, policy);
    expect(!controller.loadPlayoutHistory(previous.profile()),
           "a readiness-only calibration must not authorize startup buffer growth");
    const auto initialProfile = controller.playoutHistory().profile();
    expect(controller.loadPlayoutHistory(initialProfile),
           "current trace initialization must accept its own versioned profile for exact replay");
}

void testSubmissionEstimateFallback()
{
    const auto session = config(60, 120);
    expect(vrrTimingParametersForSession(session).playoutSubmissionEstimateFallback == 1 &&
               VrrTimingParameters{}.playoutSubmissionEstimateFallback == 0,
           "new sessions must enable estimates while old captures retain their recorded policy");
    for (bool nativeAvailable : {false, true}) {
        auto fallbackPolicy = legacyFeedbackParameters(session);
        fallbackPolicy.playoutSubmissionEstimateFallback = 1;
        auto exactPolicy = fallbackPolicy;
        exactPolicy.playoutSubmissionEstimateFallback = 0;
        VrrTimingController exact(session, true, exactPolicy);
        VrrTimingController fallback(session, true, fallbackPolicy);
        uint64_t beforeHitch = 0, maximumAfterHitch = 0;
        for (int i = 0; i < 1200; ++i) {
            const auto source = decodedTimeForRtp(1000000, uint32_t(i * 1500));
            const auto a = exact.schedule(frame(i, uint32_t(i * 1500), true, source), source);
            const auto b = fallback.schedule(frame(i, uint32_t(i * 1500), true, source), source);
            if (nativeAvailable)
                expect(a.targetUs == b.targetUs && a.playoutDelayUs == b.playoutDelayUs,
                       "fresh display events must take priority over submission estimates");
            if (i == 299) beforeHitch = b.playoutDelayUs;
            if (i > 300) maximumAfterHitch = std::max(maximumAfterHitch, b.playoutDelayUs);
            expect(b.playoutDelayUs <= 16000,
                   "estimated fallback must retain the production latency budget");
            for (auto* controller : {&exact, &fallback}) {
                const auto& d = controller == &exact ? a : b;
                const auto submitted = d.targetUs + (i >= 300 && i % 10 == 9 ? 5000 : 0);
                controller->notePreparationDuration(1000);
                controller->noteSubmission(true, false, submitted);
                if (nativeAvailable) {
                    Vrr13::PresentationObservation observation;
                    observation.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
                    observation.smoothness = controller->smoothnessSample(d);
                    observation.submitted = observation.idValid = observation.sampleValid = true;
                    observation.id = observation.sampleId = uint64_t(i + 1);
                    observation.submission = observation.ready = submitted;
                    // The display remains smooth despite variable submission
                    // lead. CPU jitter must not overrule this measured result.
                    observation.sampleTime = observation.smoothness.intended + 20000;
                    observation.observed = observation.sampleTime;
                    observation.deadline = d.originalScanoutUs;
                    observation.latched = d.latchedPresentation;
                    controller->notePresentation(observation);
                }
            }
        }
        expect(fallback.estimatedCadenceIntervals() > 1000 && fallback.estimatedCadenceHitches() > 0,
               "submission estimates must publish separate cadence and hitch counts");
        if (nativeAvailable)
            expect(fallback.nativeCadenceIntervals() > 1000 && fallback.nativeCadenceHitches() == 0,
                   "estimated hitches must not contaminate verified smooth display counters");
        else {
            expect(fallback.nativeCadenceIntervals() == 0 && fallback.nativeCadenceHitches() == 0,
                   "estimated submissions must never be labeled measured display events");
            expect(maximumAfterHitch > beforeHitch,
                   "submission hitches must restore bounded adaptation without native display timing");
        }
    }
}



void testReadinessHitchAttribution()
{
    using Feedback = Vrr13::ReadinessFeedback;
    Feedback feedback;
    feedback.observe({1, 10000, 10000, 9000, 1000, true}, 2000);
    auto r = feedback.observe({2, 23000, 23000, 22000, 1000, true}, 2000);
    expect(r.interval && !r.demand, "host interval changes alone must not grow padding");
    r = feedback.observe({3, 33000, 39000, 32000, 1000, true}, 2000);
    expect(r.interval && !r.demand, "native blocking with timely readiness must not grow padding");
    feedback.reset();
    feedback.observe({1, 10000, 15000, 15000, 1000, true}, 2000);
    r = feedback.observe({2, 20000, 25000, 25000, 1000, true}, 2000);
    expect(r.interval && !r.demand, "constant readiness offset with even output must not grow padding");
    feedback.reset();
    feedback.observe({1, 10000, 10000, 9000, 1000, true}, 2000);
    r = feedback.observe({2, 20000, 25000, 25000, 1000, true}, 2000);
    expect(r.interval && r.demand == 4000, "a readiness-caused miss must request only its excess over tolerance");
    r = feedback.observe({3, 30000, 30000, 29000, 4000, true}, 2000);
    expect(r.interval && r.demand == 4000, "catch-up must not charge a changed buffer twice");
    r = feedback.observe({5, 50000, 55000, 55000, 4000, true}, 2000);
    expect(!r.interval, "dropped frames must break attribution");
    feedback.observe({6, 60000, 60000, 60000, 4000, false}, 2000);
    r = feedback.observe({7, 70000, 77000, 77000, 4000, true}, 2000);
    expect(!r.interval, "ineligible work must break attribution");
}

void testReadinessHitchBufferAdaptation()
{
    auto session = config(60, 120);
    expect(vrrTimingParametersForSession(session).playoutReadinessHitchThresholdUs == 0,
           "Windows/default session must retain prediction-only growth");
    session.readinessHitchFeedback = true;
    const auto policy = vrrTimingParametersForSession(session);
    expect(policy.playoutReadinessHitchThresholdUs == 2000,
           "Linux session must require a readiness-attributed output miss");
    VrrTimingController controller(session, true, policy);
    Vrr13::Reserve oldHistory(18);
    expect(!controller.loadPlayoutHistory(oldHistory.profile()),
           "Linux event-demand history must reject old predictive calibration");
    Vrr13::Reserve cached(19);
    for (int i = 0; i < 300; ++i) cached.observe(16000000, 0, int64_t(i + 1) * 16667000);
    expect(controller.loadPlayoutHistory(cached.profile()), "matching history must remain replayable");
    uint64_t last = 0, initial = 0, clean = 0, peak = 0, finalDelay = 0;
    for (int i = 0; i < 20600; ++i) {
        const auto source = decodedTimeForRtp(1000000, uint32_t(i * 1500));
        const bool fault = i >= 600 && i < 1200 && i % 10 == 9;
        const auto decoded = source + (fault ? 9000 : 0);
        const auto now = std::max(decoded, last);
        const auto d = controller.schedule(frame(i, uint32_t(i * 1500), true, decoded), now);
        controller.notePreparationDuration(1000);
        last = std::max(d.targetUs, std::max(now, d.renderStartUs) + 1000);
        controller.noteSubmission(true, false, last);
        if (!i) initial = d.playoutDelayUs;
        if (i < 600) expect(d.playoutDelayUs <= initial, "cached demand alone must not grow the cold-start buffer");
        if (i == 599) clean = d.playoutDelayUs;
        if (i >= 600) peak = std::max(peak, d.playoutDelayUs);
        finalDelay = d.playoutDelayUs;
        expect(d.playoutDelayUs <= policy.playoutDelayMaximumUs,
               "attributed growth must retain the hard cap");
    }
    expect(peak > clean, "observed readiness-caused output misses must earn additional buffering");
    expect(finalDelay < peak, "expired event demands must release increased buffering");
}

void testPredictionOnlyBufferAdaptation()
{
    const auto session = config(60, 120);
    // Preserve the previous five-minute/3 ms law for historical replay.
    auto policy = vrrTimingParametersForSession(session);
    policy.playoutResponsiveBuffer = 0;
    policy.playoutDelayMarginUs = 3000;
    expect(policy.playoutPredictionOnly == 1 && policy.playoutNativeHitchAdaptation == 0 &&
               policy.playoutReadinessDrivenAdaptation == 1 && policy.playoutDelayMarginUs == 3000,
           "production must adapt from readiness prediction with 3 ms headroom");

    // One active worker honors render-start deadlines and GPU readiness.
    // Delivery, render work and render-wakeup faults each need more padding,
    // even when the backend never supplies any display observation.
    for (int faultKind : {1, 2, 3}) {
        VrrTimingController controller(session, true, policy);
        uint64_t lastSubmission = 0, initial = 0, clean = 0, peak = 0, finalDelay = 0;
        uint64_t maximumLatency = 0, previousDelay = 0;
        bool bounded = true, startupStable = true;
        for (int i = 0; i < 20600; ++i) {
            const auto source = decodedTimeForRtp(1000000, uint32_t(i * 1500));
            const bool fault = i >= 600 && i < 1200 && i % 10 == 9;
            const auto decoded = source + (faultKind == 1 && fault ? 9000 : 0);
            const auto now = std::max(decoded, lastSubmission);
            const auto d = controller.schedule(frame(i, uint32_t(i * 1500), true, decoded), now);
            const uint64_t work = faultKind == 2 && fault ? 10000 : 1000;
            const uint64_t scheduler = faultKind == 3 && fault ? 9000 : 0;
            const auto ready = std::max(now, d.renderStartUs) + scheduler + work;
            const auto submitted = std::max(d.targetUs, ready);
            controller.notePreparationDuration(work);
            controller.noteSchedulerDelays(scheduler, 0, true);
            controller.noteSubmission(true, false, submitted);
            if (i == 0) initial = d.playoutDelayUs;
            if (i < 120) startupStable &= d.playoutDelayUs <= initial;
            if (i == 599) clean = d.playoutDelayUs;
            if (i >= 600) peak = std::max(peak, d.playoutDelayUs);
            bounded &= d.playoutDelayUs <= policy.playoutDelayMaximumUs &&
                (!i || d.playoutDelayUs <= previousDelay + policy.playoutDelayAttackUs) &&
                (!lastSubmission || submitted >= controller.displayPeriodUs() + lastSubmission);
            maximumLatency = std::max(maximumLatency, submitted - decoded);
            lastSubmission = submitted;
            previousDelay = finalDelay = d.playoutDelayUs;
        }
        expect(startupStable, "cold-start padding must not grow just because its margin was counted twice");
        expect(clean == 3000, "clean readiness must release startup padding without display confirmation");
        expect(peak >= 12000, "delivery, render and scheduler faults must grow predictive protection");
        expect(finalDelay == 3000,
               "expired readiness tails must release the increased buffer while retaining 3 ms headroom");
        expect(bounded && maximumLatency <= 30000,
               "prediction adaptation must preserve bounded attack, the Smooth cap, spacing and latency");
        expect(controller.nativeCadenceIntervals() == 0,
               "bidirectional predictive adaptation must work with no display-event coverage");
    }

    // Low-rate desktop delivery must not expand protection past the selected
    // profile's absolute cap.
    for (int fps : {20, 30, 60}) {
        auto desktopSession = config(fps, 120);
        const auto desktopPolicy = vrrTimingParametersForSession(desktopSession);
        VrrTimingController desktop(desktopSession, true, desktopPolicy);
        auto expandingPolicy = desktopPolicy;
        expandingPolicy.playoutResponsiveBuffer = 4;
        expandingPolicy.playoutDelayMaximumPeriodPerMille = 2000;
        VrrTimingController expanding(desktopSession, true, expandingPolicy);
        uint64_t maximumDelay = 0, historicalMaximum = 0;
        for (int i = 0; i < 1800; ++i) {
            const uint32_t rtp = uint32_t(i * (90000 / fps));
            const uint64_t decoded = decodedTimeForRtp(1000000, rtp);
            const auto d = desktop.schedule(frame(i, rtp, true, decoded), decoded);
            maximumDelay = std::max(maximumDelay, d.playoutDelayUs);
            const uint64_t work = i % 10 == 9 ? 24000 : 1000;
            const auto old = expanding.schedule(frame(i, rtp, true, decoded), decoded);
            historicalMaximum = std::max(historicalMaximum, old.playoutDelayUs);
            expanding.notePreparationDuration(work);
            expanding.noteSubmission(true, false, std::max(old.targetUs, decoded + work));
            desktop.notePreparationDuration(work);
            desktop.noteSubmission(true, false, std::max(d.targetUs, decoded + work));
        }
        std::printf("slow source %d FPS: expanding peak %llu us, bounded peak %llu us\n", fps,
                    (unsigned long long)historicalMaximum, (unsigned long long)maximumDelay);
        expect(historicalMaximum > 16000,
               "regression workload must reproduce the previous expanding buffer");
        expect(maximumDelay <= desktopPolicy.playoutDelayMaximumUs,
               "slow source cadence must never expand the absolute buffer ceiling");
    }

    // Display-only errors can be logged but cannot steer any timing decision.
    VrrTimingController control(session, true, policy), feedback(session, true, policy);
    bool identical = true;
    uint64_t initial = 0, finalDelay = 0;
    for (int i = 0; i < 1000; ++i) {
        const auto at = decodedTimeForRtp(1000000, uint32_t(i * 1500));
        const auto a = control.schedule(frame(i, uint32_t(i * 1500), true, at), at);
        const auto b = feedback.schedule(frame(i, uint32_t(i * 1500), true, at), at);
        identical &= a.targetUs == b.targetUs && a.renderStartUs == b.renderStartUs &&
            a.playoutDelayUs == b.playoutDelayUs && b.compositorLeadUs == 0 &&
            a.smoothnessProtectionUs == b.smoothnessProtectionUs;
        if (!i) initial = a.playoutDelayUs;
        finalDelay = a.playoutDelayUs;
        for (auto* c : {&control, &feedback}) {
            c->notePreparationDuration(1000);
            c->noteSubmission(true, false, a.targetUs);
        }
        Vrr13::PresentationObservation o;
        o.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
        o.smoothness = feedback.smoothnessSample(b);
        o.submitted = o.idValid = o.sampleValid = true;
        o.id = o.sampleId = uint64_t(i + 1);
        o.submission = b.targetUs; o.ready = std::max(at, b.renderStartUs) + 1000;
        o.sampleTime = b.targetUs + (i % 10 == 9 ? 7000 : 1000);
        o.observed = o.sampleTime; o.deadline = b.originalScanoutUs;
        feedback.notePresentation(o);
    }
    expect(identical && finalDelay == 3000 && finalDelay < initial,
           "display-only hitches must neither inflate padding, block release nor move render deadlines");
    expect(feedback.nativeCadenceHitches() > 100,
           "ignored display timing must remain available as optional diagnostic evidence");
    expect(feedback.estimatedCadenceIntervals() > 900,
           "submission prediction must remain the production cadence report");

    Vrr13::Reserve oldHistory(17);
    oldHistory.observe(46000000, 16000000);
    VrrTimingController restored(session, true, policy);
    expect(restored.playoutHistory().version() == 18 && !restored.loadPlayoutHistory(oldHistory.profile()),
           "prediction-only buffers must not inherit native-hitch calibration");
    expect(restored.loadPlayoutHistory(control.playoutHistory().profile()),
           "prediction-only profiles must remain loadable for trace initialization");
}

void testProductionSmoothFrameTiming()
{
    // Approx. 77 FPS with alternating 11/15 ms source intervals. Exercise
    // the actual session resolver and adaptive buffer at every latency preset.
    for (int mode : {0, 1, 2}) {
        struct Result {
            uint64_t jerk = 0;
            uint64_t latency = 0;
            unsigned samples = 0;
        } results[2];
        for (int enabled : {0, 1}) {
            auto session = config(120, 120);
            session.latencyMode = mode;
            session.smoothFrameTiming = enabled != 0;
            const auto policy = vrrTimingParametersForSession(session);
            expect(policy.playoutSmoothingGainPerMille == (enabled ? 500 : 0) &&
                       policy.playoutSmoothingMaxLagUs == 2000 &&
                       policy.playoutMetronomeEnabled == 0,
                   "the preference must select moderate smoothing independently of the timing preset");
            VrrTimingController controller(session, true, policy);
            uint32_t ticks = 0;
            uint64_t lastSubmission = 0, previousInterval = 0;
            for (int i = 0; i < 1800; ++i) {
                // A host stall, then a sustained change to 60 FPS, must not
                // trap the smoother at the original rate or grow phase debt.
                ticks += i == 600 ? 4500 : i >= 900 ? 1500 :
                    (i % 2 ? 1350 : 990);
                const auto decoded = decodedTimeForRtp(1000000, ticks);
                const auto now = std::max(decoded, lastSubmission);
                const auto d = controller.schedule(frame(i, ticks, true, decoded), now);
                const uint64_t lateWake = i == 750 ? 4000 : 0;
                const auto ready = std::max(now, d.renderStartUs) + 1000 + lateWake;
                const auto submitted = std::max(d.targetUs, ready);
                controller.notePreparationDuration(1000);
                controller.noteSchedulerDelays(lateWake, 0, true);
                controller.noteSubmission(true, false, submitted);
                expect(d.cadenceSmoothingUs <= 2000 &&
                           (enabled || d.cadenceSmoothingUs == 0),
                       "smoothing must respect its positive retiming cap and the off switch");
                expect(submitted >= decoded && submitted - decoded <= 30000 &&
                           d.playoutDelayUs <= controller.playoutQueueLimitUs(),
                       "host jitter, stalls and rate changes must retain bounded latency and queue capacity");
                if (i == 600) {
                    expect(d.cadenceSmoothingUs == 0,
                           "a host stall must restart smoothing on the raw source slot");
                }
                if (i > 1200) {
                    expect(std::abs(int64_t(controller.sourcePeriodUs()) - 16667) <= 2 &&
                               std::abs(d.cadenceSmoothingUs) <= 100,
                           "the smoother must settle at a new source rate without persistent phase debt");
                }
                const uint64_t interval = submitted - lastSubmission;
                if (i >= 200 && i < 600) {
                    results[enabled].jerk += interval > previousInterval ?
                        interval - previousInterval : previousInterval - interval;
                    results[enabled].latency += submitted - decoded;
                    ++results[enabled].samples;
                }
                previousInterval = interval;
                lastSubmission = submitted;
            }
        }
        expect(results[1].jerk * 2 < results[0].jerk,
               "moderate smoothing must more than halve adjacent interval wobble near 77 FPS");
        std::fprintf(stderr, "production smoothing mode %d: mean jerk %llu -> %llu us, latency %llu -> %llu us\n",
                     mode, static_cast<unsigned long long>(results[0].jerk / results[0].samples),
                     static_cast<unsigned long long>(results[1].jerk / results[1].samples),
                     static_cast<unsigned long long>(results[0].latency / results[0].samples),
                     static_cast<unsigned long long>(results[1].latency / results[1].samples));
        // Unlike the captured stream, this clean-delivery fixture has little
        // spare buffering. Retiming can acquire extra readiness protection;
        // bound its average cost to 4 ms, well below one 13 ms source frame.
        expect(results[1].latency <= results[0].latency + results[1].samples * 4000ULL,
               "moderate smoothing must add at most 4 ms average latency under alternating host jitter");
    }
}

void testProductionPreservesRelativeGameSpacing()
{
    auto session = config(120, 120);
    session.smoothFrameTiming = false;
    auto policy = vrrTimingParametersForSession(session);
    expect(policy.playoutDelayMaximumUs == 24000 &&
               policy.playoutDelayMaximumPeriodPerMille == 0 &&
               policy.playoutDelayCapSourcePeriodPerMille == 3000,
           "production Smooth must allow three source frames up to 24 ms");
    // Hold padding constant to isolate the spacing contract from adaptation.
    policy.playoutDelayMaximumPeriodPerMille = 0;
    policy.playoutDelayCapSourcePeriodPerMille = 0;
    policy.playoutDelayMinimumUs = policy.playoutDelayMaximumUs;
    VrrTimingController zeroEpoch(session, true, policy);
    VrrTimingController shiftedEpoch(session, true, policy);
    uint32_t ticks = 0;
    uint64_t previousTarget = 0, previousSource = 0;
    const uint32_t intervals[] = {750, 1500, 1000, 1250};
    for (int i = 0; i < 500; ++i) {
        ticks += intervals[i % 4];
        const uint64_t source = uint64_t(ticks) * 1000 / 90;
        const uint64_t decoded = 1000000 + source;
        const auto a = zeroEpoch.schedule(frame(i, ticks, true, decoded), decoded);
        const auto b = shiftedEpoch.schedule(frame(i, ticks + 0xffff0000U, true, decoded), decoded);
        expect(a.targetUs == b.targetUs && a.originalTargetUs == b.originalTargetUs,
               "an arbitrary RTP epoch or wrap must not alter the client deadline");
        if (i > 32) {
            const auto error = int64_t(a.originalTargetUs - previousTarget) -
                               int64_t(source - previousSource);
            expect(std::abs(error) <= 1 && a.cadenceSmoothingUs == 0,
                   "variable game intervals must survive unchanged through fixed playout padding");
        }
        previousTarget = a.originalTargetUs; previousSource = source;
        for (auto* controller : {&zeroEpoch, &shiftedEpoch}) {
            controller->notePreparationDuration(1000);
            controller->noteSubmission(true, false, a.targetUs);
        }
    }
}

void testLatencyFixModeSelection()
{
    expect(!VrrSessionConfig{}.latencyFix &&
               VrrTimingParameters{}.latencyFixEnabled == 0,
           "latency fix must remain opt-in for sessions and historical replay parameters");
    for (int rate : {100, 120}) {
        const auto ordinarySession = config(rate, 120);
        auto selectedSession = ordinarySession;
        selectedSession.latencyFix = true;
        const auto ordinaryPolicy = vrrTimingParametersForSession(ordinarySession);
        const auto selectedPolicy = vrrTimingParametersForSession(selectedSession);
        expect(ordinaryPolicy.latencyFixEnabled == 0 &&
                   selectedPolicy.latencyFixEnabled == 1,
               "the snapshotted checkbox must resolve into the recorded controller parameters");
        VrrTimingController ordinary(ordinarySession, true, ordinaryPolicy);
        VrrTimingController selected(selectedSession, true, selectedPolicy);
        // A recorded parameter snapshot takes precedence over today's setting.
        VrrTimingController recorded(selectedSession, true, ordinaryPolicy);
        const uint64_t limit = selected.displayPeriodUs() / 2;
        expect(selected.latencyFixActive() == (rate == 120),
               "cold start must use the negotiated rate to select the near-refresh mode");
        expect(!recorded.latencyFixActive(),
               "an older unchecked parameter snapshot must not inherit the current checkbox");
        for (int i = 0; i < 360; ++i) {
            const auto rtp = uint32_t(std::llround(i * 90000.0 / rate));
            const auto decoded = decodedTimeForRtp(1000000, rtp) + (i % 7) * 50;
            const auto a = ordinary.schedule(frame(i, rtp, true, decoded), decoded);
            const auto b = selected.schedule(frame(i, rtp, true, decoded), decoded);
            const auto c = recorded.schedule(frame(i, rtp, true, decoded), decoded);
            if (i == 0 && rate == 120) {
                expect(b.playoutDelayUs <= limit && a.playoutDelayUs > limit,
                       "near-refresh startup must cap intentional padding at half a display period");
            }
            expect(a.targetUs == c.targetUs && a.renderStartUs == c.renderStartUs &&
                       a.playoutDelayUs == c.playoutDelayUs &&
                       a.latchedPresentation == c.latchedPresentation,
                   "recorded unchecked policy must reproduce ordinary scheduling exactly");
            if (rate == 100) {
                expect(!selected.latencyFixActive() && a.targetUs == b.targetUs &&
                           a.originalTargetUs == b.originalTargetUs &&
                           a.renderStartUs == b.renderStartUs &&
                           a.playoutDelayUs == b.playoutDelayUs &&
                           a.latchedPresentation == b.latchedPresentation,
                       "selecting latency fix must leave 100 FPS scheduling unchanged");
            }
            else {
                expect(b.playoutDelayUs <= limit,
                       "near-refresh padding must remain within the selected delay budget");
            }
            ordinary.notePreparationDuration(1000);
            selected.notePreparationDuration(1000);
            recorded.notePreparationDuration(1000);
            ordinary.noteSubmission(true, false, a.targetUs);
            selected.noteSubmission(true, false, b.targetUs);
            recorded.noteSubmission(true, false, c.targetUs);
        }
    }

    struct Boundary { int refresh; int enter; };
    for (const auto boundary : {Boundary{60, 59}, Boundary{120, 116},
                               Boundary{144, 138}, Boundary{240, 224},
                               Boundary{360, 324}}) {
        expect(VrrRatePolicy::protectedRateForRefresh(boundary.refresh) == boundary.enter,
               "latency fix must share the refresh-scaled near-ceiling cutoff");
        for (int rate : {boundary.enter - 1, boundary.enter, boundary.refresh}) {
            auto session = config(rate, boundary.refresh);
            session.latencyFix = true;
            VrrTimingController controller(session, true, vrrTimingParametersForSession(session));
            expect(controller.latencyFixActive() == (rate >= boundary.enter),
                   "near-ceiling entry must scale with refresh and include its cutoff");
            if (controller.latencyFixActive()) {
                const auto first = controller.schedule(frame(1, 0, true, 1000000), 1000000);
                expect(first.playoutDelayUs <= controller.displayPeriodUs() / 2,
                       "high-refresh padding must scale down with the display period");
            }
        }
    }
}

void testLatencyFixFittedRateHysteresis()
{
    auto session = config(120, 120);
    session.latencyFix = true;
    VrrTimingController controller(session, true, vrrTimingParametersForSession(session));
    struct Segment { int rate; bool active; };
    double ticks = 0;
    int number = 0;
    int transitions = 0;
    bool previousActive = controller.latencyFixActive();
    for (const auto segment : {Segment{110, false}, Segment{115, false},
                              Segment{117, true}, Segment{115, true},
                              Segment{113, false}, Segment{115, false},
                              Segment{117, true}}) {
        for (int i = 0; i < 600; ++i, ++number) {
            ticks += 90000.0 / segment.rate;
            const auto rtp = uint32_t(std::llround(ticks));
            const auto decoded = decodedTimeForRtp(1000000, rtp);
            const auto prior = controller.lastSubmissionUs();
            const auto decision = controller.schedule(
                frame(number, rtp, true, decoded), std::max(decoded, prior));
            if (controller.latencyFixActive() != previousActive) {
                ++transitions;
                expect(!decision.rebased && !decision.phaseDiscontinuity &&
                           controller.hasLastSubmission() && controller.lastSubmissionUs() == prior &&
                           controller.diagnostics().cadenceSamples > 0,
                       "a latency-mode transition must retain timeline, cadence and prior submission state");
            }
            if (i > 300) {
                expect(controller.latencyFixActive() == segment.active,
                       "fitted cadence must enter at 116, retain either state at 115, and exit below 114 FPS");
            }
            if (controller.latencyFixActive()) {
                expect(decision.playoutDelayUs <= controller.displayPeriodUs() / 2,
                       "rate-band entry must apply the low-delay budget immediately");
            }
            previousActive = controller.latencyFixActive();
            controller.notePreparationDuration(1000);
            controller.noteSubmission(true, false, decision.targetUs);
        }
    }
    expect(transitions == 4,
           "hysteresis must make one transition at each genuine band crossing without oscillation");
}

void testLatencyFixNativeHitchesStayBounded()
{
    const auto ordinarySession = config(120, 120);
    auto selectedSession = ordinarySession;
    selectedSession.latencyFix = true;
    VrrTimingController ordinary(ordinarySession, true, vrrTimingParametersForSession(ordinarySession));
    VrrTimingController selected(selectedSession, true, vrrTimingParametersForSession(selectedSession));
    const uint64_t limit = selected.displayPeriodUs() / 2;
    uint64_t ordinaryBeforeHitches = 0;
    uint64_t ordinaryMaximum = 0;
    uint64_t selectedHitches = 0;
    double ticks = 0;
    bool exited = false;
    for (int i = 0; i < 1200; ++i) {
        ticks += 90000.0 / (i < 600 ? 120 : 113);
        const auto rtp = uint32_t(std::llround(ticks));
        const auto decoded = decodedTimeForRtp(1000000, rtp);
        for (auto* controller : {&ordinary, &selected}) {
            const auto decision = controller->schedule(
                frame(i, rtp, true, decoded), std::max(decoded, controller->lastSubmissionUs()));
            if (controller == &ordinary) {
                if (i == 199) ordinaryBeforeHitches = decision.playoutDelayUs;
                if (i > 200 && i < 600)
                    ordinaryMaximum = std::max(ordinaryMaximum, decision.playoutDelayUs);
            }
            else {
                expect(decision.playoutDelayUs <= limit,
                       "confirmed near-ceiling hitches must not expand padding beyond the latency-fix budget");
                if (!controller->latencyFixActive()) {
                    exited = true;
                    expect(decision.requestedPlayoutDelayUs <= limit,
                           "leaving the near-ceiling band must not revive rejected hitch demand");
                }
            }
            controller->notePreparationDuration(1000);
            controller->noteSubmission(true, false, decision.targetUs);
            Vrr13::PresentationObservation observation;
            observation.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
            observation.smoothness = controller->smoothnessSample(decision);
            observation.submitted = observation.idValid = observation.sampleValid = true;
            observation.id = observation.sampleId = uint64_t(i + 1);
            observation.submission = decision.targetUs;
            observation.ready = decoded + 1000;
            // A stable service delay with a confirmed, occasional 6 ms hitch.
            // Keep the observation's native mode constant to isolate padding.
            const uint64_t hitch = i >= 200 && i < 600 && i % 10 == 9 ? 6000 : 0;
            observation.sampleTime = observation.smoothness.intended + 20000 + hitch;
            observation.observed = observation.sampleTime;
            observation.deadline = decision.originalScanoutUs;
            controller->notePresentation(observation);
            if (controller == &selected && i == 599)
                selectedHitches = controller->nativeCadenceHitches();
        }
    }
    expect(ordinaryMaximum <= ordinaryBeforeHitches,
           "display-only hitches must not grow prediction-only padding outside the latency-fix budget");
    expect(selectedHitches > 20,
           "latency fix must keep reporting confirmed native hitches it chooses not to buffer");
    expect(exited && !selected.latencyFixActive(),
           "the hitch fixture must actually leave the near-ceiling band");
}

void testLatencyFixBufferlessLateFrameSafety()
{
    auto session = config(120, 120);
    session.latencyFix = true;
    auto policy = vrrTimingParametersForSession(session);
    policy.playoutDelayCapSourcePeriodPerMille = 0;
    policy.latencyFixDelayPeriodPerMille = 0;
    for (bool canLatch : {false, true}) {
        VrrTimingController controller(session, canLatch, policy);
        for (int i = 0; i < 360; ++i) {
            const auto rtp = uint32_t(i * 750);
            const auto decoded = decodedTimeForRtp(1000000, rtp) + (i == 180 ? 18000 : 0);
            const auto now = std::max(decoded, controller.lastSubmissionUs());
            const auto decision = controller.schedule(frame(i, rtp, i != 181, decoded), now);
            expect(controller.latencyFixActive() && decision.playoutDelayUs == 0,
                   "zero delay must override startup, late-frame and invalid-RTP fallback padding");
            expect(controller.playoutDelayUs() == 0,
                   "delay telemetry must agree with the active fallback limit");
            expect(decision.targetUs >= now && decision.targetUs >= controller.earliestSubmissionUs(),
                   "a lone late frame must remain presentable without violating readiness or the active native spacing floor");
            if (i > 0 && !decision.latchedPresentation) {
                expect(decision.targetUs >= controller.lastSubmissionUs() +
                           controller.displayPeriodUs() + decision.guardUs,
                       "bufferless adaptive presentation must retain its scanout safety floor");
            }
            controller.notePreparationDuration(1000);
            controller.noteSubmission(true, false, decision.targetUs);
        }
    }
}

void testLatencyPresetsAcrossSourceAndDisplayRates()
{
    expect(VrrSessionConfig{}.latencyMode == 0 &&
               VrrTimingParameters{}.latencyFixAllRates == 0 &&
               VrrTimingParameters{}.playoutDelayCapSourcePeriodPerMille == 0,
           "historical session and replay defaults must retain Smooth and the legacy rate band");
    struct Rates { int source; int display; };
    for (const auto rates : {Rates{40, 120}, Rates{60, 120}, Rates{100, 120},
                             Rates{120, 120}, Rates{60, 60}, Rates{120, 240}}) {
        const auto ordinarySession = config(rates.source, rates.display);
        const auto ordinaryPolicy = vrrTimingParametersForSession(ordinarySession);
        expect(ordinaryPolicy.latencyFixEnabled == 0 &&
                   ordinaryPolicy.latencyFixAllRates == 0 &&
                   ordinaryPolicy.playoutDelayCapSourcePeriodPerMille == 3000 &&
                   ordinaryPolicy.playoutDelayMaximumUs == 24000,
               "Smooth must cap adaptive buffering at three source frames");
        for (int mode : {1, 2}) {
            auto session = ordinarySession;
            session.latencyMode = mode;
            const auto policy = vrrTimingParametersForSession(session);
            const uint64_t capPerMille = mode == 1 ? 1000 : 500;
            expect(policy.latencyFixEnabled == 1 && policy.latencyFixAllRates == 1 &&
                       policy.latencyFixDelayPeriodPerMille == (mode == 1 ? 500 : 0) &&
                       policy.playoutDelayCapSourcePeriodPerMille == capPerMille,
                   "Balanced Target and Low Latency must resolve to replayable source-frame buffer caps");
            for (bool canLatch : {false, true}) {
                VrrTimingController selected(session, canLatch, policy);
                VrrTimingController ordinary(ordinarySession, canLatch, ordinaryPolicy);
                // Replay parameters, including an old disabled snapshot, win
                // over the user's current preference.
                VrrTimingController recorded(session, canLatch, ordinaryPolicy);
                expect(selected.latencyFixActive() && !recorded.latencyFixActive(),
                       "all-rate presets must activate at cold start without altering old snapshots");
                for (int i = 0; i < 240; ++i) {
                    const auto rtp = uint32_t(std::llround(i * 90000.0 / rates.source));
                    const auto decoded = decodedTimeForRtp(1000000, rtp) +
                        (i == 120 ? 18000 : (i % 7) * 50);
                    const bool validRtp = i != 121 && i != 122;
                    const auto a = ordinary.schedule(frame(i, rtp, validRtp, decoded),
                        std::max(decoded, ordinary.lastSubmissionUs()));
                    const auto b = recorded.schedule(frame(i, rtp, validRtp, decoded),
                        std::max(decoded, recorded.lastSubmissionUs()));
                    const uint64_t ordinaryLimit =
                        ordinary.sourcePeriodUs() * 3000 / 1000;
                    expect(a.targetUs == b.targetUs && a.originalTargetUs == b.originalTargetUs &&
                               a.renderStartUs == b.renderStartUs &&
                               a.playoutDelayUs == b.playoutDelayUs &&
                               a.requestedPlayoutDelayUs == b.requestedPlayoutDelayUs &&
                               a.latchedPresentation == b.latchedPresentation,
                           "Smooth and its recorded policy must retain identical decisions through late and invalid-RTP frames");
                    expect(a.playoutDelayUs <= ordinaryLimit &&
                               ordinary.playoutDelayUs() <= ordinaryLimit,
                           "Smooth padding must stay within three fitted source frames");
                    const auto now = std::max(decoded, selected.lastSubmissionUs());
                    const auto decision = selected.schedule(frame(i, rtp, validRtp, decoded), now);
                    const uint64_t limit =
                        selected.sourcePeriodUs() * capPerMille / 1000;
                    expect(selected.latencyFixActive() && decision.playoutDelayUs <= limit &&
                               selected.playoutDelayUs() <= limit,
                           "startup, late-frame and invalid-RTP padding must obey the source-frame cap at every rate");
                    expect(decision.targetUs >= now &&
                               decision.targetUs >= selected.earliestSubmissionUs(),
                           "reduced padding must retain readiness and native presentation deadlines");
                    if (i > 0 && !decision.latchedPresentation) {
                        expect(decision.targetUs >= selected.lastSubmissionUs() +
                                   selected.displayPeriodUs() + decision.guardUs,
                               "all-rate presets must preserve the adaptive scanout safety floor");
                    }
                    ordinary.notePreparationDuration(1000);
                    recorded.notePreparationDuration(1000);
                    selected.notePreparationDuration(1000);
                    ordinary.noteSubmission(true, false, a.targetUs);
                    recorded.noteSubmission(true, false, b.targetUs);
                    selected.noteSubmission(true, false, decision.targetUs);
                }
            }
        }
    }
}

void testLatencyPresetsBoundHitchesThroughCadenceChanges()
{
    for (int mode : {1, 2}) {
        const auto ordinarySession = config(100, 120);
        auto selectedSession = ordinarySession;
        selectedSession.latencyMode = mode;
        VrrTimingController ordinary(ordinarySession, true, vrrTimingParametersForSession(ordinarySession));
        VrrTimingController selected(selectedSession, true, vrrTimingParametersForSession(selectedSession));
        const uint64_t capPerMille = mode == 1 ? 1000 : 500;
        uint64_t ordinaryBeforeHitches = 0;
        uint64_t ordinaryMaximum = 0;
        double ticks = 0;
        int number = 0;
        for (int rate : {100, 120, 60, 40, 117, 113}) {
            for (int i = 0; i < 600; ++i, ++number) {
                ticks += 90000.0 / rate;
                const auto rtp = uint32_t(std::llround(ticks));
                const auto decoded = decodedTimeForRtp(1000000, rtp);
                for (auto* controller : {&ordinary, &selected}) {
                    const auto priorSubmission = controller->lastSubmissionUs();
                    const auto decision = controller->schedule(frame(number, rtp, true, decoded),
                        std::max(decoded, priorSubmission));
                    if (controller == &ordinary && number < 600) {
                        if (number == 199) ordinaryBeforeHitches = decision.playoutDelayUs;
                        if (number > 200)
                            ordinaryMaximum = std::max(ordinaryMaximum, decision.playoutDelayUs);
                    }
                    if (controller == &selected) {
                        const uint64_t limit =
                            controller->sourcePeriodUs() * capPerMille / 1000;
                        expect(controller->latencyFixActive() && decision.playoutDelayUs <= limit,
                               "native hitches and cadence steps must not revive the old rate band or exceed the preset cushion");
                        expect(controller->lastSubmissionUs() == priorSubmission,
                               "a source-rate step must retain the prior submission boundary");
                        if (number > 0) {
                            expect(!decision.rebased && !decision.phaseDiscontinuity,
                                   "ordinary source-rate changes must retain the timing epoch in reduced-delay presets");
                        }
                    }
                    controller->notePreparationDuration(1000);
                    controller->noteSubmission(true, false, decision.targetUs);
                    Vrr13::PresentationObservation observation;
                    observation.timeKind = Vrr13::PresentationTimeKind::DisplayEvent;
                    observation.smoothness = controller->smoothnessSample(decision);
                    observation.submitted = observation.idValid = observation.sampleValid = true;
                    observation.id = observation.sampleId = uint64_t(number + 1);
                    observation.submission = decision.targetUs;
                    observation.ready = decoded + 1000;
                    const uint64_t hitch = number >= 200 && number % 10 == 9 ? 6000 : 0;
                    observation.sampleTime = observation.smoothness.intended + 20000 + hitch;
                    observation.observed = observation.sampleTime;
                    observation.deadline = decision.originalScanoutUs;
                    controller->notePresentation(observation);
                }
            }
        }
        expect(ordinaryMaximum <= ordinaryBeforeHitches,
               "display-only hitches must not grow Smooth's predictive buffer");
        expect(selected.nativeCadenceHitches() > 20,
               "reduced-delay presets must continue reporting the hitches they choose not to buffer");
    }
}

void testResponsiveSmoothingWithWideJitter()
{
    for (int mode : {0, 1, 2}) {
        uint64_t jerk[2] = {};
        for (int smooth : {0, 1}) {
            auto session = config(120, 120);
            session.latencyMode = mode;
            session.smoothFrameTiming = smooth;
            VrrTimingController controller(session, true, vrrTimingParametersForSession(session));
            uint32_t ticks = 0;
            uint64_t last = 0, previousInterval = 0;
            unsigned active = 0;
            for (int i = 0; i < 2400; ++i) {
                // 9/17 ms pairs have a steady 77 FPS mean. Neither interval
                // is a source stall; both exceed the old single-frame gate.
                ticks += i % 2 ? 1530 : 810;
                const auto decoded = decodedTimeForRtp(1000000, ticks);
                const auto now = std::max(last, decoded);
                const auto d = controller.schedule(frame(i, ticks, true, decoded), now);
                const auto submitted = std::max(d.targetUs,
                    std::max(now, d.renderStartUs) + 1000);
                controller.notePreparationDuration(1000);
                controller.noteSchedulerDelays(0, 0, true);
                controller.noteSubmission(true, false, submitted);
                const auto interval = submitted - last;
                if (i > 600) {
                    active += d.cadenceSmoothingUs != 0;
                    jerk[smooth] += interval > previousInterval ?
                        interval - previousInterval : previousInterval - interval;
                }
                expect(submitted - decoded <= 22000,
                       "wide source jitter must retain bounded client latency");
                last = submitted;
                previousInterval = interval;
            }
            expect(!smooth || active > 1700,
                   "Reduce judder must stay active on alternating source jitter above 25 percent");
        }
        std::printf("wide jitter mode=%d jerk=%llu -> %llu us\n", mode,
            (unsigned long long)(jerk[0] / 1799), (unsigned long long)(jerk[1] / 1799));
        expect(jerk[1] * 2 < jerk[0], "Reduce judder must halve wide alternating jitter");
    }
}

void testResponsiveReadinessKeepsEarlySlack()
{
    for (bool corrected : {false, true}) {
        Vrr13::ReadinessPrediction prediction;
        Vrr13::Reserve history(20);
        Vrr13::RecentReadiness recent;
        Vrr13::ReadinessPrediction::Probe p{1000000, 1005000, 16667, 1000,
            1000, 0, 500, 0, true, 3000, corrected};
        prediction.observe(history, p, 1000, 0, &recent);
        expect(recent.demand(1000000) == (corrected ? 0 : 3000),
               "early-ready slack must pay for smoothing before demanding more buffer; revision 1 must replay unchanged");
        p.decoded += 20000;
        p.expected = p.decoded + 1000;
        prediction.observe(history, p, 1000, 0, &recent);
        expect(recent.demand(p.decoded) == (corrected ? 2000 : 3000),
               "only the uncovered part of an advanced deadline must become reserve demand");
    }
}

void testResponsiveBufferRecoveryAndDesktopCadence()
{
    // Start from a deliberately expensive saved prior: it must not own the
    // live request. Test the real scheduler/work loop, not only its estimator.
    for (int mode : {0, 1, 2}) for (bool smooth : {false, true}) for (int faultKind : {0, 1, 2}) {
        auto session = config(120, 120);
        session.latencyMode = mode;
        session.smoothFrameTiming = smooth;
        auto policy = vrrTimingParametersForSession(session);
        // Preserve the revision-2 three-second recovery contract. Revision 3
        // has explicit, longer retention tested separately below.
        policy.playoutResponsiveBuffer = 2;
        policy.playoutDelayMaximumUs = 16000;
        expect(policy.playoutResponsiveBuffer == 2 && policy.playoutDelayMarginUs == 500,
               "live policy must select recent readiness rather than the five-minute tail");
        VrrTimingController controller(session, true, policy);
        Vrr13::Reserve cache(20);
        for (int i = 0; i < 300; ++i) cache.observe(90000000, 16000000);
        expect(controller.loadPlayoutHistory(cache.profile()), "matching diagnostic prior must load");
        uint64_t ticks = 0, last = 0, previousDelay = 0, clean = 0, peak = 0, finalDelay = 0;
        uint64_t transitionPeak = 0, recoveryAt = 0, maximumLatency = 0;
        unsigned number = 0;
        const uint64_t configuredCap = std::min<uint64_t>(16000,
            (1000000 / 120) * (mode == 0 ? 3000 : mode == 1 ? 1000 : 500) / 1000);
        // 12 s clean startup; repeated large desktop transitions, jitter during
        // one transition cycle, then 16 s to prove bounded recovery.
        for (int second = 0; second < 56; ++second) {
            const int fps = second < 12 || second >= 40 ? 120 :
                (second / 2) % 4 == 0 ? 19 : (second / 2) % 4 == 1 ? 120 :
                (second / 2) % 4 == 2 ? 30 : 19;
            for (int n = 0; n < fps; ++n, ++number) {
                ticks += uint64_t((n + 1) * 90000 / fps - n * 90000 / fps);
                const auto source = decodedTimeForRtp(1000000, uint32_t(ticks));
                const bool fault = second >= 28 && second < 36 && n % 5 == 0;
                const auto decoded = source + (fault && faultKind == 0 ? 6000 : 0);
                const auto now = std::max(decoded, last);
                const auto d = controller.schedule(frame(number, uint32_t(ticks), true, decoded), now);
                const uint64_t work = fault && faultKind == 1 ? 7000 : 1000;
                const uint64_t scheduler = fault && faultKind == 2 ? 6000 : 0;
                const auto ready = std::max(now, d.renderStartUs) + work + scheduler;
                last = std::max(ready, d.targetUs);
                controller.notePreparationDuration(work);
                controller.noteSchedulerDelays(scheduler, 0, true);
                controller.noteSubmission(true, false, last);
                const uint64_t cap = std::min<uint64_t>(
                    16000,
                    d.sourcePeriodUs *
                        (mode == 0 ? 3000 : mode == 1 ? 1000 : 500) /
                        1000);
                expect(d.playoutDelayUs <= cap,
                       "buffer must remain within the observed-cadence cap");
                expect(!number || d.playoutDelayUs <= previousDelay + 500,
                       "buffer attack must remain bounded through rate changes");
                maximumLatency = std::max(maximumLatency, last - decoded);
                if (second == 11) clean = d.playoutDelayUs;
                if (second >= 12 && second < 28) transitionPeak = std::max(transitionPeak, d.playoutDelayUs);
                if (second >= 28 && second < 40) peak = std::max(peak, d.playoutDelayUs);
                if (second >= 36 && !recoveryAt && d.playoutDelayUs <= 1250) recoveryAt = source;
                previousDelay = finalDelay = d.playoutDelayUs;
            }
        }
        std::printf("responsive mode=%d smoothing=%d fault=%d clean=%llu transition=%llu peak=%llu final=%llu recovery=%llu latency=%llu us\n",
            mode, smooth, faultKind, (unsigned long long)clean, (unsigned long long)transitionPeak,
            (unsigned long long)peak, (unsigned long long)finalDelay,
            (unsigned long long)recoveryAt, (unsigned long long)maximumLatency);
        expect(clean <= 1250 && transitionPeak <= 1500,
               "clean 120/19/30 FPS desktop changes and cached tails must not inflate buffering");
        expect(peak >= std::min<uint64_t>(configuredCap, 5000),
               "real delivery jitter during desktop transitions must still earn protection");
        expect(finalDelay <= 1250 && recoveryAt && recoveryAt < 53000000,
               "a recovered burst must drain within 16 seconds, not five minutes");
        expect(maximumLatency <= 22000, "desktop transitions must retain a bounded client residence");
    }

    // Expiring burst protection, successful observations, and silence behavior.
    Vrr13::RecentReadiness recent;
    for (uint64_t at = 1000000; at < 5000000; at += 10000)
        recent.observe(0, 0, 1000, at);
    recent.observe(9000, 0, 1000, 5000000);
    expect(recent.demand(5010000) >= 9000 && !recent.canRelease(5010000),
           "a meaningful isolated miss must acquire temporary protection");
    for (uint64_t at = 5010000; at <= 9000000; at += 10000)
        recent.observe(0, 0, 9000, at);
    expect(recent.demand(9000000) == 0 && recent.canRelease(9000000),
           "old bursts must expire while clean evidence permits downward probing");
    expect(!recent.canRelease(10000000), "silence is not clean readiness evidence");
}

void testPresetReadinessTargets()
{
    for (int mode : {0, 1, 2}) {
        auto session = config(120, 116);
        session.latencyMode = mode;
        const auto policy = vrrTimingParametersForSession(session);
        const uint64_t target = mode == 2 ? 990000 : mode == 1 ? 995000 : 999900;
        const uint64_t window = mode == 2 ? 60000000 : mode == 1 ? 120000000 : 300000000;
        expect(policy.playoutResponsiveBuffer == 7 &&
                   policy.playoutOnTimeTargetPerMillion == target &&
                   policy.playoutReadinessWindowUs == window,
               "presets must resolve their exact reliability target and bounded history");
        Vrr13::RecentReadiness recent(window, target);
        for (uint64_t n = 0; n < 20000; ++n) {
            const uint64_t required = n < 19800 ? 1000 : n < 19900 ? 3000 : n < 19990 ? 6000 : 9000;
            recent.observe(required, 0, 100000, 1000000 + n * 1000);
        }
        const uint64_t expected = mode == 2 ? 1000 : mode == 1 ? 3000 : 9000;
        expect(recent.demand(21000000) == expected,
           "nearest-rank percentiles must distinguish the preset targets without rounding to 100");
        expect(recent.demand(21000000 + window) == 0,
               "samples older than each preset's history must expire completely");
        expect(!recent.canRelease(21000000 + window),
               "empty history after silence must not authorize release");
    }
}

void testPresetIntervalTolerances()
{
    for (int mode : {0, 1, 2}) {
        auto session = config(120, 116);
        session.latencyMode = mode;
        VrrTimingController controller(
            session, true, vrrTimingParametersForSession(session));
        const auto decision = controller.schedule(
            frame(1, 0, true, 100000), 100000);
        controller.notePreparationDuration(1000, 0, 101000);
        controller.noteSubmission(true, false, decision.targetUs);

        const uint64_t expected = mode == 0 ? 200 : 500;
        expect(controller.intervalStats().toleranceUs == expected,
               "Smooth must use 0.2 ms interval tolerance while other presets use 0.5 ms");
    }
}

void testThresholdedReadinessGrowth()
{
    const uint64_t startUs = 1000000;
    Vrr13::RecentReadiness isolatedSoft(3000000, 1000000, true);
    for (uint64_t n = 0; n < 100; ++n) {
        isolatedSoft.observe(n == 99 ? 1500 : 0, 0, 0,
                            startUs + n * 1000);
    }
    expect(isolatedSoft.demand(startUs + 100000) == 1500 &&
               !isolatedSoft.allowsGrowth(startUs + 100000, 0),
           "an isolated 1-2 ms miss may be measured but must not grow the buffer");

    Vrr13::RecentReadiness prevalentSoft(3000000, 1000000, true);
    for (uint64_t n = 0; n < 100; ++n) {
        prevalentSoft.observe(n < 51 ? 1500 : 0, 0, 0,
                             startUs + n * 1000);
    }
    expect(prevalentSoft.allowsGrowth(startUs + 100000, 0),
           "1-2 ms misses must grow the buffer only above half of the live window");

    Vrr13::RecentReadiness hardMiss(3000000, 990000, true);
    for (uint64_t n = 0; n < 100; ++n) {
        hardMiss.observe(n == 99 ? 2250 : 0, 0, 0,
                         startUs + n * 1000);
    }
    expect(hardMiss.demand(startUs + 100000) >= 2250 &&
               hardMiss.allowsGrowth(startUs + 100000, 0),
           "one miss over 2 ms must acquire temporary buffer protection");

    Vrr13::RecentReadiness oneMillisecond(3000000, 1000000, true);
    for (uint64_t n = 0; n < 100; ++n) {
        oneMillisecond.observe(1000, 0, 0, startUs + n * 1000);
    }
    expect(!oneMillisecond.allowsGrowth(startUs + 100000, 0),
           "lateness through 1 ms must never grow the buffer");
}

void testMeanMissBuffer()
{
    Vrr13::MeanMissBuffer threshold;
    uint64_t at = 1000000;
    // An exactly one-millisecond mean is inside the deadband.
    for (unsigned i = 0; i < 120; ++i, at += 10000)
        threshold.observe(at, 1000, 1000, true, 1000, 16000, 4000000, 200);
    expect(threshold.demand(1000) == 1000, "a mean miss of exactly one millisecond must not grow buffer");
    Vrr13::MeanMissBuffer rare;
    at = 1000000;
    for (unsigned i = 0; i < 101; ++i, at += 10000)
        rare.observe(at, i == 100 ? 2000 : 0, 1000, true, 1000, 16000, 4000000, 200);
    expect(rare.demand(1000) == 1250, "successful frames must not dilute the missed-frame average");
    for (unsigned i = 0; i < 50; ++i, at += 10000)
        rare.observe(at, 0, 1250, true, 1000, 16000, 4000000, 200);
    expect(rare.demand(1000) == 1250, "old missed frames alone cannot repeatedly increase protection");
    Vrr13::MeanMissBuffer above;
    at = 1000000;
    for (unsigned i = 0; i < 100; ++i, at += 10000)
        above.observe(at, 1001, 1000, true, 1000, 16000, 4000000, 200);
    expect(above.demand(1000) == 1001, "one microsecond above the average threshold must request only that excess");
    Vrr13::MeanMissBuffer held;
    at = 1000000;
    for (unsigned i = 0; i < 100; ++i, at += 10000)
        held.observe(at, 2000, 4000, true, 1000, 16000, 4000000, 200);
    const auto peak = held.demand(1000);
    for (unsigned i = 0; i < 300; ++i, at += 10000)
        held.observe(at, 0, peak, true, 1000, 16000, 4000000, 200);
    expect(held.demand(1000) == peak, "hold must preserve protection before clean recovery");
    for (unsigned i = 0; i < 400; ++i, at += 10000)
        held.observe(at, 0, peak, true, 1000, 16000, 4000000, 200);
    expect(held.demand(1000) < peak && held.demand(1000) >= peak - 600,
        "release must remain slow and require clean observations");
    const auto beforeGap = held.demand(1000);
    held.observe(at + 10000000, 0, beforeGap, true, 1000, 16000, 4000000, 200);
    expect(held.demand(1000) == beforeGap, "silence must not be treated as clean release time");
    for (int mode : {0, 1, 2}) {
        auto session = config(116, 120);
        session.latencyMode = mode;
        const auto policy = vrrTimingParametersForSession(session);
        expect(policy.playoutResponsiveBuffer == 7 &&
            policy.playoutMeanMissHoldUs == (mode == 2 ? 6000000 : mode == 1 ? 8000000 : 10000000) &&
            policy.playoutMeanMissReleaseUsPerSecond == (mode == 0 ? 50 : mode == 2 ? 125 : 100),
            "every preset must select the production interval queue and record its release policy");
    }
}

void testIntervalQualityBuffer()
{
    // Constant lateness must cancel from spacing; alternating late readiness
    // must acquire bounded protection only when it explains the error.
    for (bool variable : {false, true}) {
        for (bool absorbable : {false, true}) {
            Vrr13::IntervalBuffer buffer;
            uint64_t applied = 1000;
            for (uint64_t i = 1; i <= 500; ++i) {
                const uint64_t intended = 1000000 + i * 10000;
                const uint64_t late = variable && i % 2 ? 3000 : 1000;
                const uint64_t previous = applied;
                buffer.observe({i, intended, intended + late, intended,
                    intended + late, applied, true, absorbable},
                    1000, 4000, 6000000, 125, true, 990000);
                applied = buffer.demand(applied);
                expect(applied >= 1000 && applied <= 4000 && applied <= previous + 250,
                       "interval buffer must respect its minimum, cap, and bounded attack");
            }
            expect(buffer.stats().averageValid, "continuous intervals must establish valid coverage");
            if (!variable) {
                expect(applied == 1000 && buffer.stats().qualityPercent() == 100.0,
                       "constant lateness must not reduce interval quality or grow buffering");
            } else {
                expect(buffer.stats().qualityPercent() < 99.0,
                       "sustained spacing variation must lower the severity-weighted score");
                expect(absorbable ? applied > 1000 : applied == 1000,
                       "only readiness-attributable spacing errors may grow protection");
            }
        }
    }
}

void testIntervalQualityUsesPresetHistory()
{
    Vrr13::IntervalBuffer oneMinute;
    Vrr13::IntervalBuffer fiveMinutes;
    for (uint64_t i = 0; i < 7000; ++i) {
        const uint64_t intended = 1000000 + i * 10000;
        const uint64_t offset = i == 1 ? 1000 : 0;
        const auto sample = Vrr13::IntervalBuffer::Sample{
            i, intended, intended + offset, intended,
            intended + offset, 1000, true, true};
        oneMinute.observe(sample, 1000, 24000, 10000000, 50,
                          true, 999900, 500, 60000000);
        fiveMinutes.observe(sample, 1000, 24000, 10000000, 50,
                            true, 999900, 500, 300000000);
    }
    expect(oneMinute.stats().averageValid && fiveMinutes.stats().averageValid &&
               fiveMinutes.stats().evaluatedUs > oneMinute.stats().evaluatedUs,
           "the active quality score must retain the selected preset history duration");
}

int main()
{
    testIntervalQualityBuffer();
    testIntervalQualityUsesPresetHistory();
    testPresetIntervalTolerances();
    testMeanMissBuffer();
    testThresholdedReadinessGrowth();
    testPresetReadinessTargets();
    testResponsiveSmoothingWithWideJitter();
    testResponsiveReadinessKeepsEarlySlack();
    testResponsiveBufferRecoveryAndDesktopCadence();
    testLatencyFixModeSelection();
    testLatencyFixFittedRateHysteresis();
    testLatencyFixNativeHitchesStayBounded();
    testLatencyFixBufferlessLateFrameSafety();
    testLatencyPresetsAcrossSourceAndDisplayRates();
    testLatencyPresetsBoundHitchesThroughCadenceChanges();
    testSubmissionEstimateFallback();
    testReadinessHitchAttribution();
    testReadinessHitchBufferAdaptation();
    testPredictionOnlyBufferAdaptation();
    testNativeHitchGatesPadding();
    testDelayedDisplayEventsAgreeAcrossBackends();
    testProductionPreservesRelativeGameSpacing();
    testProductionSmoothFrameTiming();
    testReadinessDrivenPadding();
    testStableNativeSmoothnessReference();
    testPreparationKeepsLearnedLead();
    testGpuReadinessLeadIsSeparateFromTarget();
    testSmoothnessFeedback();
    testRefreshReferencesAreNotDisplayEvents();
    testVrr14Prediction();
    testProcessingEpisodeClassification();
    testPerFrameLatchAtNativeMaximum();
    testExplicitAdaptiveOnlyPolicy();
    testProductionAdaptiveProtectionRecoversWithoutDrift();
    testPerFrameLatchIncludesSafetyHeadroom();
    testProductionNearRefreshSafetyMargin();
    testPersistentImmediateUsesSafetyFloor();
    testSourceRateProtection();
    testRollingPlayoutHistory();
    testHistoryPreservesVrr13Scheduling();
    testHistoryLearningAndQueueCapacity();
    testRtpWrapResetAndFallback();
    testTimingFormulaeAndReserveCap();
    testSourcePlayoutDelayOffsetsProjectedTargets();
    testTimestampModePreservesUnevenHostIntervals();
    testOffsetSlewUsesElapsedTime();
    testOffsetSlewIgnoresWorkerBacklogAndPreservesHistoricalClock();
    testOffsetRecoveryRejectsTransitionMinimum();
    testOffsetRecoveryKeepsStartupPaddingAndNativePolicy();
    testTimestampModeStillBoundsCatchUpBursts();
    testCadenceSmoothingEvensJitteredSource();
    testAdaptivePlayoutDelaySlewsAcrossBands();
    testPrepareOnArrivalSpendsTheCushion();
    testRenderStartKeepsClearOfPreviousPresent();
    testShortHitchDoesNotRefitSourceRate();
    testMotionDeadbandHonorsStampSteps();
    testSmoothnessLearningWindowTracksCadence();
    testLongRunNearRefreshRtpCadence();
    testQuantizedCadenceDoesNotOscillate();
    testHighRefreshCalibrationBandsHaveNoCadenceCliff();
    testReported120HzBandBoundarySlewsCalibration();
    testNegotiatedRateCeiling();
    testSpacingGuardFeedback();
    testNearRefreshRequestsLatchedPresentation();
    testLatchedPresentationUsesFullHysteresis();
    testOptionalDisplayScaledLatchedPresentationBoundary();
    testCadenceInstabilityUsesLatchedRecovery();
    testHeadroomAwareReadinessReserve();
    testCadenceGapAndRateChange();
    testFutureSourceProjectionReseedsPhase();
    testDecodeTailAdaptation();
    testRateChangeReseedsReadinessBudget();
    testFractionalQuantizedCadenceLearning();
    testCutsceneRecoveryAndHitchIsolation();
    testModerateSlowdownSelfHealsPhase();
    testContinuousCadenceSweep();
    testQuantizedCadenceProjectsSmoothTargets();
    testSkippedLocalFramePreservesCadence();
    testSchedulerDelayFeedback();
    testRenderBaselineDoesNotConsumePacingBudget();
    testRenderTailSharesHalfScanoutBudget();
    testPacingBudgetAtExtremeRefreshRates();
    testSmoothnessAddsOneSourcePeriodOfReserve();
    testLegacyReplayPolicyRetainsAbsoluteRenderCeiling();
    testTargetWaiterBoundaries();
    testMetronomeHoldsCadenceThroughJitterAndLateFrames();
    testMetronomeIgnoresSingleEarlyOutlier();
    testBurstExclusionKeepsDelayAfterStall();
    testLatchedPresentationDropsSoftwareFloor();
    testRuntimeParametersChangePolicy();
    return failures == 0 ? 0 : 1;
}
