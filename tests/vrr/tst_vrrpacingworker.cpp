#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/profile.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/profilecodec.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrframedroppolicy.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrrpacingworker.h"
#include "vrrtestfakes.h"

#include <SDL.h>

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

namespace {

std::chrono::steady_clock::time_point g_TestClockOrigin =
    std::chrono::steady_clock::now();
std::atomic_uint64_t g_FrozenTestClockUs { 0 };
std::atomic_int64_t g_TestClockOffsetUs { 0 };
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void resetFakeClock()
{
    g_TestClockOrigin = std::chrono::steady_clock::now();
    g_FrozenTestClockUs.store(0);
    g_TestClockOffsetUs.store(0);
}

// The worker has real threads and waits. Freeze only while presenter gates
// establish the queue under test, so stale-age boundaries do not depend on
// whether the test machine was descheduled for another few milliseconds.
class FrozenTestClock {
public:
    FrozenTestClock()
    {
        g_FrozenTestClockUs.store(std::max<uint64_t>(LiGetMicroseconds(), 1));
    }

    ~FrozenTestClock() { resume(); }

    void advance(uint64_t durationUs)
    {
        g_FrozenTestClockUs.fetch_add(durationUs);
    }

    void resume()
    {
        const uint64_t frozenUs = g_FrozenTestClockUs.load();
        if (frozenUs == 0) return;
        const int64_t elapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - g_TestClockOrigin).count();
        g_TestClockOffsetUs.store(static_cast<int64_t>(frozenUs) - elapsedUs);
        g_FrozenTestClockUs.store(0);
    }
};

bool waitFor(const std::function<bool()>& predicate,
             std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

QByteArray readExpandedTrace(const QString& tracePath)
{
    QFile traceFile(tracePath);
    if (!traceFile.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray encoded = traceFile.readAll();
    constexpr int magicLength = 7;
    if (!encoded.startsWith("MLVRR1\n")) {
        return encoded;
    }

    QByteArray expanded;
    int offset = magicLength;
    while (offset < encoded.size()) {
        if (encoded.size() - offset < 4) {
            return {};
        }
        const unsigned char* length =
            reinterpret_cast<const unsigned char*>(encoded.constData() + offset);
        const uint32_t compressedBytes =
            static_cast<uint32_t>(length[0]) |
            (static_cast<uint32_t>(length[1]) << 8) |
            (static_cast<uint32_t>(length[2]) << 16) |
            (static_cast<uint32_t>(length[3]) << 24);
        offset += 4;
        if (compressedBytes > static_cast<uint32_t>(encoded.size() - offset)) {
            return {};
        }
        const QByteArray chunk = qUncompress(
            reinterpret_cast<const uchar*>(encoded.constData() + offset),
            static_cast<int>(compressedBytes));
        if (chunk.isEmpty()) {
            return {};
        }
        expanded.append(chunk);
        offset += static_cast<int>(compressedBytes);
    }
    return expanded;
}

VrrSessionConfig enabledConfig()
{
    VrrSessionConfig config;
    config.displayRefreshHz = 120;
    config.streamRateHz = 60;
    return config;
}

PacerTelemetrySnapshot telemetryStats(const PacerTelemetry& telemetry)
{
    return telemetry.snapshot();
}

PacedFrame frame(int number, TrackedFrameLifetime& lifetime)
{
    return makeTrackedPacedFrame(number,
                                 static_cast<uint32_t>((number - 1) * 1500),
                                 LiGetMicroseconds(),
                                 lifetime);
}

void testCapabilityRejection()
{
    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;

    backend.setSupport(
        VrrFallbackReason::AdaptivePresentationUnavailable);
    VrrPacingWorker unsupportedWorker(&backend, enabledConfig(), &telemetry);
    expect(!unsupportedWorker.start(),
           "worker must reject an unsupported VRR presentation backend");
}

void testPresentationRequestSelectedBeforePreparation()
{
    for (int mode : {0, 1, 2}) {
        for (int rate : {60, 115, 116, 120}) {
            resetFakeClock();
            FakeVrrFramePresenter backend;
            backend.setCanLatch(true);
            backend.blockPreparation();
            PacerTelemetry telemetry;
            TrackedFrameLifetime lifetime;
            auto config = enabledConfig();
            config.streamRateHz = rate;
            config.latencyMode = mode;
            {
                VrrPacingWorker worker(&backend, config, &telemetry);
                expect(worker.start(), "worker must start for swapchain mode selection");
                worker.submit(frame(1, lifetime));
                expect(backend.waitForPrepareCount(1), "preparation must receive the mode before acquiring an image");
                const auto prepared = backend.prepareRequests();
                expect(prepared.size() == 1 && !prepared[0].latchedPresentation,
                       "the first frame must remain adaptive at every rate before preparation");
                expect(backend.presentRequests().empty(), "selection must precede native presentation");
                // A slow recreation must not change the request at Present.
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                backend.releasePreparation();
                expect(backend.waitForPresentCount(1), "a late recreation must still present the prepared frame");
                const auto presented = backend.presentRequests();
                expect(presented.size() == 1 && !prepared.empty() &&
                       presented[0].latchedPresentation == prepared[0].latchedPresentation &&
                       presented[0].collectDiagnostics == prepared[0].collectDiagnostics,
                       "preparation and presentation must receive the same mode after a delay");
                expect(waitFor([&telemetry] { return telemetryStats(telemetry).vrrPrepareLateFrames != 0; }),
                       "swapchain preparation delays must remain accounted for in timing telemetry");
                expect(telemetryStats(telemetry).vrrCadenceIntervals == 0 &&
                           telemetryStats(telemetry).vrrCadenceHitches == 0,
                       "preparation lateness cannot manufacture measured cadence or hitches");
            }
            expect(lifetime.releases.load() == 1, "mode preparation must release its decoder frame exactly once");
        }
    }
}

void testEmptyQueueDoesNotRepeatFrames()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime second;
    VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
    expect(worker.start(), "worker must start for idle queue coverage");
    worker.submit(frame(1, first));
    expect(backend.waitForPresentCount(1), "first frame must present");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    expect(backend.presentCount() == 1,
           "an empty queue must not generate repeated presentations");
    worker.submit(frame(2, second));
    expect(backend.waitForPresentCount(2),
           "a new frame must wake the idle worker");
}

void testQueueCapacityAndDrops()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.blockPreparation();
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime second;
    TrackedFrameLifetime third;
    TrackedFrameLifetime fourth;
    TrackedFrameLifetime freshest;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start for a capable backend");
        worker.submit(frame(1, first));
        expect(backend.waitForPrepareCount(1),
               "first worker frame must enter the preparation gate");

        // The active frame and three successors absorb a short decoder burst.
        // A fourth successor evicts only the oldest queued frame.
        worker.submit(frame(2, second));
        worker.submit(frame(3, third));
        worker.submit(frame(4, fourth));
        worker.submit(frame(50, freshest));
        const PacerTelemetrySnapshot stats = telemetryStats(telemetry);
        expect(stats.vrrPacingDroppedFrames == 1 &&
                   stats.pacerDroppedFrames == 1,
               "a short successor burst must be buffered with only capacity overflow coalesced");
        expect(stats.vrrReadiness.samples == 1 && stats.vrrReadiness.misses == 1 &&
                   stats.vrrReadiness.dropped == 1,
               "capacity eviction must count as one readiness miss even without tracing");

        const uint64_t releaseUs = LiGetMicroseconds();
        backend.releasePreparation();
        expect(backend.waitForPresentCount(1),
               "releasing preparation must present the active frame");
        expect(backend.waitForPresentCount(4),
               "overflow recovery must drain every retained successor");

        const std::vector<int> presentedFrames = backend.presentedFrames();
        expect(presentedFrames.size() >= 4 && presentedFrames[0] == 1 &&
                   presentedFrames[1] == 3 && presentedFrames[2] == 4 &&
                   presentedFrames[3] == 50,
               "queue overflow must retain the three freshest successors in cadence order");
        const std::vector<uint64_t> calls = backend.presentCallTimesUs();
        expect(calls.size() >= 2 && calls[1] >= releaseUs &&
                   calls[1] - releaseUs < 100000,
               "overflow recovery must promptly rebase to the freshest frame");
    }

    expect(backend.cancelCount() == 1,
           "worker shutdown must release the presenter exactly once");
    expect(telemetryStats(telemetry).vrrReadiness.samples == 5 &&
               telemetryStats(telemetry).vrrReadiness.dropped == 1,
           "completion accounting must count each presented or evicted frame once");
}

void testLatePreparedFramePresentsImmediately()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.blockPreparation();
    PacerTelemetry telemetry;
    TrackedFrameLifetime lifetime;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start for late-presentation recovery");
        worker.submit(frame(1, lifetime));
        expect(backend.waitForPrepareCount(1),
               "frame must enter preparation before simulating a stall");

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        backend.releasePreparation();
        expect(backend.waitForPresentCount(1),
               "a late prepared frame must present instead of being cancelled");
        expect(waitFor([&telemetry] {
                   return telemetryStats(telemetry).vrrEligibleFrames >= 1;
               }),
               "late preparation telemetry must publish with the presentation");
        const PacerTelemetrySnapshot stats = telemetryStats(telemetry);
        expect(stats.vrrPacingDroppedFrames == 0,
               "a late observation must not manufacture a pacing drop");
        expect(stats.vrrPrepareLateFrames >= 1,
               "late preparation must remain visible in timing telemetry");
    }
}

void testQueuedStaleFrameYieldsToFreshSuccessor()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.blockPreparation();
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime stale;
    TrackedFrameLifetime fresh;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start for queued-stale recovery");
        worker.submit(frame(1, first));
        expect(backend.waitForPrepareCount(1),
               "active frame must enter preparation before queueing successors");

        worker.submit(frame(2, stale));
        worker.submit(frame(3, fresh));
        // Frame 2's metronome tick sits one cushion (a little over a 60 FPS
        // period) after its stamp. Holding the pipeline 70 ms leaves that
        // tick more than a whole period in the past while frame 3 is
        // available. It is stale content, not a pacing deadline to honor.
        std::this_thread::sleep_for(std::chrono::milliseconds(70));
        backend.releasePreparation();

        expect(backend.waitForPresentCount(2),
               "the active frame and freshest successor must present");
        expect(waitFor([&telemetry] {
                   return telemetryStats(telemetry).vrrPacingDroppedFrames >= 1;
               }),
               "an obsolete queued frame must count as a pacing drop");

        const std::vector<int> presentedFrames = backend.presentedFrames();
        expect(presentedFrames.size() >= 2 && presentedFrames[0] == 1 &&
                   presentedFrames[1] == 3,
               "a stale queued frame must yield to its fresher successor");
    }
}

void testSinglePeriodQueueDelayPreservesFluidity()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.blockPreparation();
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime retained;
    TrackedFrameLifetime fresh;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(),
               "worker must start for single-period queue recovery");
        worker.submit(frame(1, first));
        expect(backend.waitForPrepareCount(1),
               "active frame must enter preparation before queueing successors");

        worker.submit(frame(2, retained));
        worker.submit(frame(3, fresh));
        // This crosses the 60 FPS source period but frame 2's tick, one
        // cushion after its stamp, is still ahead. The captured production
        // bug dropped content at this boundary despite a valid future target.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        backend.releasePreparation();

        expect(backend.waitForPresentCount(3),
               "a one-period queue delay must retain every frame");
        const std::vector<int> presentedFrames = backend.presentedFrames();
        expect(presentedFrames.size() >= 3 &&
                   presentedFrames[0] == 1 &&
                   presentedFrames[1] == 2 &&
                   presentedFrames[2] == 3,
               "ordinary pipeline occupancy must preserve frame order");
        expect(telemetryStats(telemetry).vrrPacingDroppedFrames == 0,
               "ordinary pipeline occupancy must not manufacture a pacing drop");
    }
}

void testLatencyFixDropBoundaries()
{
    VrrTimingDecision decision;
    decision.sourcePeriodUs = 8333;
    decision.targetUs = 50000;
    expect(!VrrFrameDropPolicy::beforeRender(decision, 8333, 8334, false, true),
           "latency fix must retain ordinary one-period worker occupancy");
    expect(!VrrFrameDropPolicy::beforeRender(decision, 8333, 16666, false, true),
           "latency fix must retain the exact two-period boundary");
    expect(VrrFrameDropPolicy::beforeRender(decision, 8333, 16667, false, true),
           "latency fix may replace work older than two periods when a successor exists");
    expect(!VrrFrameDropPolicy::beforeRender(decision, 8333, 12500, false, false),
           "ordinary VRR must retain its two-period age tolerance");
    expect(VrrFrameDropPolicy::afterRenderWait(decision, 10000, 26667, false, true),
           "latency fix must replace work beyond two periods even when the target is ahead");
    expect(!VrrFrameDropPolicy::afterRenderWait(decision, 10000, 26666, false, true),
           "post-wait replacement must preserve the exact two-period boundary");
    expect(!VrrFrameDropPolicy::afterRenderWait(decision, 10000, 18334, false, false),
           "ordinary post-wait policy must retain its target-relative horizon");

    decision.sourcePeriodUs = 9000;
    decision.presentationFloorPushUs = 4500;
    expect(!VrrFrameDropPolicy::beforeRender(decision, 8333, 0, false, true),
           "half-period floor debt is not sufficient for replacement");
    ++decision.presentationFloorPushUs;
    expect(VrrFrameDropPolicy::beforeRender(decision, 8333, 0, false, true),
           "active latency fix may shed material floor debt below exact refresh");
    expect(!VrrFrameDropPolicy::beforeRender(decision, 8333, 0, false, false),
           "ordinary below-refresh playback must keep its natural debt recovery");

    decision.sourcePeriodUs = std::numeric_limits<uint64_t>::max();
    expect(VrrFrameDropPolicy::maximumAgeUs(decision, false, false) ==
               std::numeric_limits<uint64_t>::max(),
           "age tolerance multiplication must saturate instead of wrapping");
}

void runLatencyFixQueuedRecovery(int streamRateHz, bool enabled,
                                bool fresherSuccessor,
                                unsigned int agePerMille = 1500,
                                bool advanceClockBeforeArrival = false,
                                int latencyMode = 0)
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.setCanLatch(true);
    backend.blockPreparation();
    backend.setPreparationLimit(1);
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime queued;
    TrackedFrameLifetime fresh;
    VrrSessionConfig config = enabledConfig();
    config.streamRateHz = streamRateHz;
    config.latencyFix = enabled;
    config.latencyMode = latencyMode;
    const bool reducedDelay = latencyMode == 1 || latencyMode == 2 ||
        (enabled && streamRateHz >= 116);
    const bool replaceQueued = reducedDelay &&
        fresherSuccessor && agePerMille > 2000;
    auto makeFrame = [streamRateHz](int number, TrackedFrameLifetime& lifetime) {
        return makeTrackedPacedFrame(number,
            static_cast<uint32_t>((number - 1) * 90000 / streamRateHz),
            LiGetMicroseconds(), lifetime);
    };

    {
        VrrPacingWorker worker(&backend, config, &telemetry);
        expect(worker.start(), "latency-fix queue worker must start");
        worker.submit(makeFrame(1, first));
        const bool entered = backend.waitForPrepareCount(1);
        expect(entered, "first frame must enter the controlled preparation gate");
        if (!entered) {
            backend.setPreparationLimit(std::numeric_limits<size_t>::max());
            backend.releasePreparation();
            return;
        }

        FrozenTestClock clock;
        if (advanceClockBeforeArrival) clock.advance(100000);
        worker.submit(makeFrame(2, queued));
        if (fresherSuccessor) worker.submit(makeFrame(3, fresh));
        // Queue age, rather than time since process startup, controls the
        // replacement. Wall time spent in the gates cannot change it.
        clock.advance((1000000ULL / streamRateHz) * agePerMille / 1000);
        backend.releasePreparation();
        expect(backend.waitForPrepareCount(2),
               "a retained successor must reach preparation after the first present");
        const std::vector<int> prepared = backend.preparedFrames();
        expect(prepared.size() == 2 && prepared[0] == 1 &&
                   prepared[1] == (replaceQueued ? 3 : 2),
               "the selected latency policy must control stale replacement before preparation");
        expect(telemetryStats(telemetry).vrrPacingDroppedFrames ==
                   (replaceQueued ? 1U : 0U),
               "queue recovery must count exactly the expected optional replacement");
        if (replaceQueued) {
            expect(queued.releases.load() == 1,
                   "the skipped decoded image must be released before preparing its successor");
        }
        clock.resume();
        backend.setPreparationLimit(std::numeric_limits<size_t>::max());
        const size_t expectedPresents = fresherSuccessor && !replaceQueued ? 3 : 2;
        expect(backend.waitForPresentCount(expectedPresents),
               "every retained image must drain after the preparation gate opens");
        const std::vector<int> expected = replaceQueued ? std::vector<int>{1, 3} :
            fresherSuccessor ? std::vector<int>{1, 2, 3} : std::vector<int>{1, 2};
        expect(backend.presentedFrames() == expected,
               "optional latest-frame replacement must preserve retained frame order");
    }
    expect(first.releases.load() == 1 && queued.releases.load() == 1 &&
               fresh.releases.load() == (fresherSuccessor ? 1U : 0U),
           "presented and skipped decoded images must each be released exactly once");
}

void testLatencyFixQueuedRecovery()
{
    runLatencyFixQueuedRecovery(120, true, true);
    runLatencyFixQueuedRecovery(116, true, true);
    runLatencyFixQueuedRecovery(120, true, true, 2500);
    runLatencyFixQueuedRecovery(116, true, true, 2500);
    runLatencyFixQueuedRecovery(120, false, true);
    runLatencyFixQueuedRecovery(110, true, true);
    runLatencyFixQueuedRecovery(120, true, false);

    const QByteArray priorTracePath = qgetenv("MOONLIGHT_VRR_TRACE");
    SDL_setenv("MOONLIGHT_VRR_TRACE", "", 1);
    // Admission time must exist even without diagnostics. Advancing the
    // application clock first exposes an accidental zero age origin while
    // the actual queued frame is only half a source interval old.
    runLatencyFixQueuedRecovery(120, true, true, 500, true);
    SDL_setenv("MOONLIGHT_VRR_TRACE", priorTracePath.constData(), 1);
}

void testLatencyPresetsQueuedRecovery()
{
    for (int rate : {60, 100, 110}) {
        for (int mode : {0, 1, 2}) {
            // A single interval is normal worker occupancy for every preset.
            runLatencyFixQueuedRecovery(rate, false, true, 1500, false, mode);
            // Even Low Latency must keep the only available image.
            runLatencyFixQueuedRecovery(rate, false, false, 1500, false, mode);
        }
    }
    for (int mode : {1, 2}) {
        runLatencyFixQueuedRecovery(100, false, true, 500, true, mode);
    }
}

void testLatencyFixQueueAgeIncludesDecodeWait()
{
    resetFakeClock();
    QTemporaryDir traceDirectory;
    expect(traceDirectory.isValid(),
           "decode-wait timing test must create a temporary directory");
    const QString tracePath = traceDirectory.filePath("decode-wait.vrrtrace");
    const QByteArray tracePathBytes = QFile::encodeName(tracePath);
    SDL_setenv("MOONLIGHT_VRR_TRACE", tracePathBytes.constData(), 1);
    FakeVrrFramePresenter backend;
    backend.setCanLatch(true);
    backend.blockPreparation();
    backend.blockDecodeFrame(2);
    backend.setPreparationLimit(1);
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime delayed;
    TrackedFrameLifetime fresh;
    VrrSessionConfig config = enabledConfig();
    config.streamRateHz = 120;
    config.latencyFix = true;
    auto makeFrame = [](int number, TrackedFrameLifetime& lifetime) {
        return makeTrackedPacedFrame(number, (number - 1) * 750,
                                      LiGetMicroseconds(), lifetime);
    };
    {
        VrrPacingWorker worker(&backend, config, &telemetry);
        expect(worker.start(), "decode-wait latency-fix worker must start");
        worker.submit(makeFrame(1, first));
        expect(backend.waitForPrepareCount(1),
               "first image must hold the worker before the decode gate");
        FrozenTestClock clock;
        worker.submit(makeFrame(2, delayed));
        clock.advance(5000);
        backend.releasePreparation();
        expect(backend.waitForPresentCount(1),
               "first image must present before the queued decode gate");
        expect(backend.waitForDecodeWaitCount(1),
               "second image must enter the controlled GPU-readiness wait");
        clock.advance(21000);
        worker.submit(makeFrame(3, fresh));
        backend.releaseDecode();
        expect(backend.waitForPrepareCount(2),
               "fresh image must reach preparation after delayed decode readiness");
        expect(backend.preparedFrames() == std::vector<int>({1, 3}),
               "updating GPU readiness must not erase stale transport-queue age");
        expect(telemetryStats(telemetry).vrrPacingDroppedFrames == 1 &&
                   delayed.releases.load() == 1,
               "a decode-wait replacement must release and count only the stale image");
        clock.resume();
        backend.setPreparationLimit(std::numeric_limits<size_t>::max());
        expect(backend.waitForPresentCount(2), "fresh image must present after decode-wait recovery");
        expect(backend.presentedFrames() == std::vector<int>({1, 3}),
               "decode-wait recovery must retain presentation order");
    }
    expect(first.releases.load() == 1 && delayed.releases.load() == 1 &&
               fresh.releases.load() == 1,
           "decode-wait recovery must release every source surface exactly once");

    const QByteArray expandedTrace = readExpandedTrace(tracePath);
    const QList<QByteArray> lines = expandedTrace.split('\n');
    const QList<QByteArray> columns = lines.value(0).split(',');
    const int frameColumn = columns.indexOf("frame");
    const int decoderOutputColumn = columns.indexOf("decoder_output_us");
    const int decodeCompleteColumn = columns.indexOf("decode_complete_us");
    const int decodeWaitColumn = columns.indexOf("decode_sync_wait_us");
    bool verifiedDecodeBoundary = false;
    for (int i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> fields = lines[i].split(',');
        if (fields.value(frameColumn) != "2") continue;
        const uint64_t decoderOutputUs = fields.value(decoderOutputColumn).toULongLong();
        const uint64_t decodeCompleteUs = fields.value(decodeCompleteColumn).toULongLong();
        const uint64_t decodeWaitUs = fields.value(decodeWaitColumn).toULongLong();
        verifiedDecodeBoundary = decoderOutputUs != 0 && decodeWaitUs == 21000 &&
            decodeCompleteUs - decoderOutputUs == decodeWaitUs;
        break;
    }
    expect(verifiedDecodeBoundary,
           "GPU readiness must add only the blocking fence wait and exclude pacing-queue residence");
    SDL_setenv("MOONLIGHT_VRR_TRACE", "", 1);
}

void testTelemetrySnapshotsRemainCumulative()
{
    PacedFrame readinessProbe(nullptr, 1, 0, false, 100);
    readinessProbe.noteGpuReadyUs(250);
    expect(readinessProbe.decoderOutputUs() == 100 &&
               readinessProbe.decodeCompleteUs() == 250,
           "GPU readiness must not rewrite the decoder-output timestamp");

    // Motion must reveal host-timed judder even when source fidelity is perfect.
    PacerTelemetry motion;
    VrrTelemetrySample motionSample;
    motionSample.presented = true;
    motionSample.submissionUs = 1000000;
    motion.recordVrrFrame(motionSample);
    for (int i = 0; i < 6; ++i) {
        motionSample.submissionUs += i % 2 ? 16000 : 10000;
        motion.recordVrrFrame(motionSample);
    }
    expect(motion.snapshot().vrrMotionPairs == 5 &&
               motion.snapshot().vrrMotionHitches == 5,
           "alternating host-spaced output must lower motion cadence");
    motionSample.motionDiscontinuity = true;
    motionSample.submissionUs += 1000000;
    motion.recordVrrFrame(motionSample);
    motionSample.motionDiscontinuity = false;
    for (int i = 0; i < 3; ++i) {
        motionSample.submissionUs += 16667;
        motion.recordVrrFrame(motionSample);
    }
    expect(motion.snapshot().vrrMotionPairs == 7 &&
               motion.snapshot().vrrMotionHitches == 5,
           "an epoch reset must exclude downtime and stable lower FPS must score cleanly");
    motion.recordVrrDrop();
    motionSample.submissionUs += 33334;
    motion.recordVrrFrame(motionSample);
    expect(motion.snapshot().vrrMotionHitches == 6,
           "a local drop must not hide the resulting motion hitch");

    PacerTelemetry legacyTelemetry;
    // Decode synchronization must not inflate the visible queue statistic.
    PacerTelemetry decodeTelemetry;
    VrrTelemetrySample decodeSample;
    decodeSample.presented = true;
    decodeSample.clientProcessingTimeUs = 10000;
    decodeSample.renderingTimeUs = 1000;
    decodeSample.decodeWaitUs = 4000;
    decodeTelemetry.recordVrrFrame(decodeSample);
    const auto decodeSnapshot = decodeTelemetry.snapshot();
    expect(decodeSnapshot.totalQueuePacingTimeUs == 5000 &&
           decodeSnapshot.totalClientProcessingTimeUs == 10000 &&
           decodeSnapshot.totalRenderingTimeUs == 1000 &&
           decodeSnapshot.vrrDecodeWaitUs == 4000,
           "decode synchronization is diagnostic time, not visible queue delay");
    legacyTelemetry.recordLegacyFrame(100, 40);
    const PacerTelemetrySnapshot legacySnapshot =
        telemetryStats(legacyTelemetry);
    expect(legacySnapshot.renderedFrames == 1 &&
               legacySnapshot.totalClientProcessingTimeUs == 100 &&
               legacySnapshot.totalQueuePacingTimeUs == 60 &&
               legacySnapshot.totalRenderingTimeUs == 40,
           "legacy timing must use the same exact client-processing partition");

    PacerTelemetry telemetry;
    telemetry.beginVrrSession();

    constexpr uint64_t frameCount = 512;
    std::atomic_bool producerDone { false };
    std::thread producer([&telemetry, &producerDone, frameCount] {
        for (uint64_t i = 1; i <= frameCount; ++i) {
            VrrTelemetrySample sample;
            sample.decisionTimeUs = i;
            sample.clientProcessingTimeUs = i * 3;
            sample.renderingTimeUs = i * 2;
            sample.prepareLate = (i % 2) == 0;
            sample.preparationLatenessUs = i;
            sample.cadenceIntervals = i / 2;
            sample.cadenceHitches = i / 128;
            sample.submitErrorUs = static_cast<int64_t>(i) - 450;
            sample.spacingCorrected = (i % 8) == 0;
            sample.presented = true;
            sample.readinessBudgetUs = static_cast<int64_t>(i);
            sample.timingBudgetUs = i * 3;
            sample.renderLeadUs = i * 4;
            sample.renderWakeLeadUs = i * 5;
            sample.targetWakeLeadUs = i * 6;
            sample.guardUs = i * 7;
            sample.sourcePeriodUs = i * 8;
            telemetry.recordVrrFrame(sample);
        }
        producerDone.store(true);
    });

    uint64_t previousSequence = 0;
    uint64_t previousRenderedFrames = 0;
    bool monotonic = true;
    while (!producerDone.load()) {
        const PacerTelemetrySnapshot snapshot = telemetryStats(telemetry);
        monotonic = monotonic && snapshot.sequence >= previousSequence &&
            snapshot.renderedFrames >= previousRenderedFrames;
        previousSequence = snapshot.sequence;
        previousRenderedFrames = snapshot.renderedFrames;
    }
    producer.join();

    VrrTelemetrySample delayedSubmission;
    delayedSubmission.decisionTimeUs = frameCount + 1;
    delayedSubmission.cadenceIntervals = frameCount / 2;
    delayedSubmission.cadenceHitches = frameCount / 128;
    delayedSubmission.targetWaitEntryLate = true;
    delayedSubmission.clientProcessingTimeUs = 1000000;
    delayedSubmission.renderingTimeUs = 900000;
    telemetry.recordVrrFrame(delayedSubmission);

    const PacerTelemetrySnapshot finalSnapshot = telemetryStats(telemetry);
    constexpr uint64_t sequenceSum = frameCount * (frameCount + 1) / 2;
    expect(finalSnapshot.vrrCadenceIntervals == frameCount / 2 &&
               finalSnapshot.vrrCadenceHitches == frameCount / 128,
           "publishing cumulative cadence snapshots must not count them repeatedly");
    expect(monotonic,
           "telemetry snapshots must not regress while another thread publishes");
    expect(finalSnapshot.vrrActive &&
               finalSnapshot.renderedFrames == frameCount &&
               finalSnapshot.vrrEligibleFrames == frameCount + 1,
           "cumulative telemetry must retain every published frame");
    expect(finalSnapshot.totalClientProcessingTimeUs == sequenceSum * 3 &&
               finalSnapshot.totalQueuePacingTimeUs == sequenceSum &&
               finalSnapshot.totalRenderingTimeUs == sequenceSum * 2 &&
               finalSnapshot.totalClientProcessingTimeUs ==
                   finalSnapshot.totalQueuePacingTimeUs +
                       finalSnapshot.totalRenderingTimeUs,
           "presented-frame timing must be paired and partition client processing exactly");
    expect(finalSnapshot.vrrPrepareLateFrames == frameCount / 2 &&
               finalSnapshot.vrrPrepareLatenessP50Us == 384 &&
               finalSnapshot.vrrPrepareLatenessP95Us == 500 &&
               finalSnapshot.vrrPrepareLatenessP99Us == 510 &&
               finalSnapshot.vrrTargetWaitEntryLateFrames == 1 &&
               finalSnapshot.vrrSubmitErrorP50Us == -2 &&
               finalSnapshot.vrrSubmitErrorP95Us == 56 &&
               finalSnapshot.vrrSubmitErrorP99Us == 61 &&
               finalSnapshot.vrrSubmitErrorMaxUs == 62 &&
               finalSnapshot.vrrPresentFailedFrames == 1 &&
               finalSnapshot.vrrStateSequence == finalSnapshot.sequence,
           "telemetry must keep bounded timing distributions and output outcomes separate");
}

void testSuspendDiscardAndFreshFrame()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.blockPreparation();
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime queuedOne;
    TrackedFrameLifetime queuedTwo;
    TrackedFrameLifetime fresh;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start before exercising suspension");
        worker.submit(frame(1, first));
        expect(backend.waitForPrepareCount(1),
               "active frame must be preparing before suspension");
        worker.submit(frame(2, queuedOne));
        worker.submit(frame(3, queuedTwo));

        WINDOW_STATE_CHANGE_INFO minimized {};
        minimized.stateChangeFlags = WINDOW_STATE_CHANGE_MINIMIZED;
        worker.notifyWindowChanged(&minimized);
        expect(telemetryStats(telemetry).vrrPacingDroppedFrames >= 2,
               "minimize must synchronously discard queued VRR frames");

        backend.releasePreparation();
        expect(backend.waitForCancelCount(1),
               "suspension while preparing must cancel the acquired image");
        expect(waitFor([&backend] { return backend.suspendedCount() == 1; }),
               "worker must suspend the presenter on its own thread");
        expect(backend.presentCount() == 0,
               "a suspended active frame must not be presented");

        WINDOW_STATE_CHANGE_INFO restored {};
        restored.stateChangeFlags = WINDOW_STATE_CHANGE_RESTORED;
        worker.notifyWindowChanged(&restored);
        worker.submit(frame(4, fresh));
        expect(backend.waitForPresentCount(1),
               "a fresh frame after restore must use a rebased timeline");
        expect(waitFor([&backend] { return backend.resumedCount() == 1; }),
               "resume must reach the presenter before fresh presentation");
        expect(backend.presentedFrames().front() == 4,
               "pre-suspend frames must not survive restoration");
    }
}

void testDeferredSurfaceLifetime()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime second;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start for deferred lifetime testing");
        worker.submit(frame(1, first));
        expect(backend.waitForPresentCount(1), "first frame must present");
        expect(first.releases.load() == 0,
               "a presented decoder surface must remain deferred");

        worker.submit(frame(2, second));
        expect(backend.waitForPresentCount(2), "second frame must present");
        expect(waitFor([&first] { return first.releases.load() == 1; }),
               "the next result must release the prior deferred surface");
        expect(second.releases.load() == 0,
               "the current surface must remain deferred");
    }

    expect(second.releases.load() == 1,
           "worker destruction must release the final deferred surface");
}

void testReusableSurfaceReleasedWithoutSuccessor()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.setSourceFrameReusable(true);
    PacerTelemetry telemetry;
    TrackedFrameLifetime reusable;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start for reusable lifetime testing");
        worker.submit(frame(1, reusable));
        expect(backend.waitForPresentCount(1), "reusable frame must present");
        expect(waitFor([&reusable] { return reusable.releases.load() == 1; }),
               "a GPU-complete decoder surface must release without a successor");
        const std::vector<int> presented = backend.presentedFrames();
        expect(presented.size() == 1 && presented.front() == 1,
               "presentation must not depend on the released source frame");
    }

    expect(reusable.releases.load() == 1,
           "worker destruction must not release a reusable surface twice");
}

void testDecodeBoundaryCapturedBeforeQueueAndPreparedExactly()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.setDecodeBoundary(73);
    PacerTelemetry telemetry;
    TrackedFrameLifetime lifetime;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start for decode-boundary testing");
        worker.submit(frame(1, lifetime));
        expect(backend.waitForPresentCount(1),
               "decode-boundary frame must present");
        const std::vector<uint64_t> boundaries =
            backend.preparedDecodeBoundaries();
        expect(backend.decodeBoundaryCaptureCount() == 1,
               "each submitted frame must capture one decoder boundary");
        expect(boundaries.size() == 1 && boundaries.front() == 73,
               "preparation must receive the submitted frame's exact boundary");
        expect(backend.waitedDecodeBoundaries() == boundaries,
               "the pre-schedule decode wait must receive the same frame-specific boundary as preparation");
    }
}

void testCancelledPresentationCountsAsDroppedOutput()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.setPresentCancelled(true);
    PacerTelemetry telemetry;
    TrackedFrameLifetime cancelled;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start for cancellation classification");
        worker.submit(frame(1, cancelled));
        expect(backend.waitForPresentCount(1),
               "cancelled presentation fixture must still submit its frame");
        expect(waitFor([&telemetry] {
                   const PacerTelemetrySnapshot stats = telemetryStats(telemetry);
                   return stats.pacerDroppedFrames == 1 &&
                       stats.vrrPacingDroppedFrames == 1 &&
                       stats.vrrPresentCancelledFrames == 1;
               }),
               "a cancelled presentation must increment both pacing drop counters");
        const PacerTelemetrySnapshot stats = telemetryStats(telemetry);
        expect(stats.renderedFrames == 0 && stats.vrrEligibleFrames == 1 &&
                   stats.vrrPresentFailedFrames == 0,
               "a cancelled presentation must not count as rendered output");
    }
}

void testPresentCallSpacingSetsDisplayFloor()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime second;
    VrrSessionConfig atRefresh = enabledConfig();
    atRefresh.streamRateHz = 120;

    {
        VrrPacingWorker worker(&backend, atRefresh, &telemetry);
        expect(worker.start(), "worker must start for presentation spacing");
        worker.submit(makeTrackedPacedFrame(1, 0, LiGetMicroseconds(), first));
        expect(backend.waitForPresentCount(1), "first spacing frame must present");
        worker.submit(makeTrackedPacedFrame(2, 750, LiGetMicroseconds(), second));
        expect(backend.waitForPresentCount(2), "second spacing frame must present");

        const std::vector<uint64_t> calls = backend.presentCallTimesUs();
        expect(calls.size() >= 2 && calls[1] >= calls[0] + 8333,
               "present calls must remain at least one display period apart");
    }
}

void testBlockingPresentUsesWorkerCallBoundary()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.setPresentDelayUs(20000);
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime second;
    VrrSessionConfig config = enabledConfig();
    config.displayRefreshHz = 60;

    {
        VrrPacingWorker worker(&backend, config, &telemetry);
        expect(worker.start(), "worker must start for blocking presentation testing");
        worker.submit(frame(1, first));
        expect(backend.waitForPresentCount(1), "blocking first present must return");

        backend.setPresentDelayUs(0);
        worker.submit(frame(2, second));
        expect(backend.waitForPresentCount(2), "second frame must present");

        const std::vector<uint64_t> calls = backend.presentCallTimesUs();
        const std::vector<uint64_t> returns = backend.presentReturnTimesUs();
        constexpr uint64_t displayPeriodUs = 16666;
        expect(calls.size() >= 2 &&
                   calls[1] >= calls[0] + displayPeriodUs,
               "the worker-owned call boundary must enforce display spacing");
        expect(calls.size() >= 2 && returns.size() >= 1 &&
                   calls[1] < returns[0] + displayPeriodUs,
               "a blocking presenter must not add a second display period");
    }
}

void testSubmissionErrorCapturesPresentOverhead()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    backend.setPreSubmissionDelayUs(1000);
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;

    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start for submission-error testing");
        worker.submit(frame(1, first));
        expect(backend.waitForPresentCount(1),
               "submission-error fixture must present its frame");
        expect(waitFor([&telemetry] {
                   return telemetryStats(telemetry).vrrEligibleFrames >= 1;
               }),
               "submission-error telemetry must publish with the presentation");

        const PacerTelemetrySnapshot stats = telemetryStats(telemetry);
        expect(stats.vrrSubmitErrorP50Us > 0 &&
                   stats.vrrSubmitErrorMaxUs > 0 &&
                   stats.vrrPresentFailedFrames == 0,
               "post-target present overhead must be reported as submission error, not output failure");
    }
}

void testFailedPreparationCancellationHonorsDisplayFloor()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime failed;
    VrrSessionConfig config = enabledConfig();
    config.displayRefreshHz = 20;

    {
        VrrPacingWorker worker(&backend, config, &telemetry);
        expect(worker.start(), "worker must start for cancellation timing");

        backend.blockPreparation();
        worker.submit(frame(1, first));
        expect(backend.waitForPrepareCount(1), "priming frame must prepare");
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        backend.releasePreparation();
        expect(backend.waitForPresentCount(1),
               "priming frame must establish the display floor");

        backend.setPreparationSucceeds(false);
        backend.setCancellationMaySubmit(true);
        backend.setCancelSubmits(true);
        worker.submit(frame(2, failed));
        expect(backend.waitForPresentCount(2),
               "failed preparation must release its image by cancellation");

        const std::vector<uint64_t> calls = backend.presentCallTimesUs();
        expect(calls.size() >= 2 && calls[1] >= calls[0] + 50000,
               "a cancellation that may submit must honor the display floor");
        expect(backend.cancelCount() >= 1,
               "failed preparation must invoke the cancellation boundary");
    }
}

void testSuspendedPreparedCancellationHonorsDisplayFloor()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime suspended;
    VrrSessionConfig config = enabledConfig();
    config.displayRefreshHz = 20;

    {
        VrrPacingWorker worker(&backend, config, &telemetry);
        expect(worker.start(), "worker must start for suspended cancellation timing");

        backend.blockPreparation();
        worker.submit(frame(1, first));
        expect(backend.waitForPrepareCount(1), "priming frame must prepare");
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        backend.releasePreparation();
        expect(backend.waitForPresentCount(1),
               "priming frame must establish the display floor");

        backend.setCancellationMaySubmit(true);
        backend.setCancelSubmits(true);
        backend.blockPreparation();
        worker.submit(frame(2, suspended));
        expect(backend.waitForPrepareCount(2),
               "second frame must acquire its image before suspension");

        WINDOW_STATE_CHANGE_INFO minimized {};
        minimized.stateChangeFlags = WINDOW_STATE_CHANGE_MINIMIZED;
        worker.notifyWindowChanged(&minimized);
        backend.releasePreparation();
        expect(backend.waitForPresentCount(2),
               "suspension must cancel an acquired image even if release submits");

        const std::vector<uint64_t> calls = backend.presentCallTimesUs();
        expect(calls.size() >= 2 && calls[1] >= calls[0] + 50000,
               "suspending cancellation must honor the display floor");
        expect(backend.cancelCount() >= 1,
               "suspension must invoke the cancellation boundary");
    }
}

void testImmutablePresentationContract()
{
    resetFakeClock();
    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;
    TrackedFrameLifetime second;
    VrrSessionConfig immutableConfig = enabledConfig();
    immutableConfig.streamRateHz = 116;

    {
        VrrPacingWorker worker(&backend, immutableConfig, &telemetry);
        expect(worker.start(), "worker must start for presentation contract testing");
        worker.submit(frame(1, first));
        expect(backend.waitForPresentCount(1), "first contract frame must present");
        worker.submit(frame(2, second));
        expect(backend.waitForPresentCount(2), "second contract frame must present");
    }

    const std::vector<int> presentedFrames = backend.presentedFrames();
    expect(presentedFrames.size() == 2 && presentedFrames[0] == 1 &&
               presentedFrames[1] == 2,
           "the minimal presenter contract must preserve frame order");
    const std::vector<VrrPresentRequest> requests = backend.presentRequests();
    expect(requests.size() == 2 && !requests[0].latchedPresentation &&
               !requests[1].latchedPresentation,
           "an immutable presenter must never receive a per-present latch request");
}

void testTraceCapturesEveryDeliveredFrame()
{
    resetFakeClock();
    QTemporaryDir traceDirectory;
    expect(traceDirectory.isValid(),
           "replay trace test must create a temporary directory");
    const QString tracePath = traceDirectory.filePath("vrr-replay.csv");
    const QByteArray tracePathBytes = QFile::encodeName(tracePath);
    SDL_setenv("MOONLIGHT_VRR_TRACE", tracePathBytes.constData(), 1);
    SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "0", 1);

    FakeVrrFramePresenter backend;
    backend.blockPreparation();
    PacerTelemetry telemetry;
    TrackedFrameLifetime lifetimes[10];
    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "worker must start for replay tracing");
        worker.submit(frame(1, lifetimes[0]));
        expect(backend.waitForPrepareCount(1),
               "replay trace must hold one active frame");
        for (int frameNumber = 2; frameNumber <= 5; ++frameNumber) {
            worker.submit(frame(frameNumber, lifetimes[frameNumber - 1]));
        }
        backend.releasePreparation();
        expect(backend.waitForPresentCount(4),
               "replay trace test must drain retained frames");

        WINDOW_STATE_CHANGE_INFO minimized {};
        minimized.stateChangeFlags = WINDOW_STATE_CHANGE_MINIMIZED;
        worker.notifyWindowChanged(&minimized);
        // The fake publishes its present count before the worker writes the
        // terminal trace. This fixture tests accounting, not intentional trace
        // loss under producer/writer contention; let the preceding row drain.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        worker.submit(frame(6, lifetimes[5]));

        WINDOW_STATE_CHANGE_INFO restored {};
        restored.stateChangeFlags = WINDOW_STATE_CHANGE_RESTORED;
        worker.notifyWindowChanged(&restored);
        worker.submit(frame(7, lifetimes[6]));
        expect(backend.waitForPresentCount(5),
               "restored trace frame must present after an explicit rebase");

        WINDOW_STATE_CHANGE_INFO resized {};
        resized.stateChangeFlags = WINDOW_STATE_CHANGE_SIZE;
        resized.width = 1920;
        resized.height = 1080;
        worker.notifyWindowChanged(&resized);
        worker.submit(frame(8, lifetimes[7]));
        expect(backend.waitForPresentCount(6),
               "first frame after a geometry refresh must use a new trace epoch");

        backend.blockPreparation();
        worker.submit(frame(9, lifetimes[8]));
        expect(backend.waitForPrepareCount(7),
               "display-epoch race test must hold an in-flight frame");
        WINDOW_STATE_CHANGE_INFO displayChanged {};
        displayChanged.stateChangeFlags = WINDOW_STATE_CHANGE_DISPLAY;
        displayChanged.displayIndex = 0;
        worker.notifyWindowChanged(&displayChanged);
        backend.releasePreparation();
        expect(backend.waitForCancelCount(1),
               "an in-flight frame must be cancelled after display state changes");
        expect(backend.presentCount() == 6,
               "a decision from the prior display epoch must not be presented");

        worker.submit(frame(10, lifetimes[9]));
        expect(backend.waitForPresentCount(7),
               "the post-interrupt frame must start the refreshed display epoch");
    }

    const QByteArray expandedTrace = readExpandedTrace(tracePath);
    const QList<QByteArray> lines = expandedTrace.split('\n');
    const QList<QByteArray> columns = lines.value(0).split(',');
    const int frameColumn = columns.indexOf("frame");
    const int rtpColumn = columns.indexOf("rtp_timestamp");
    const int arrivalColumn = columns.indexOf("pacer_arrival_us");
    const int decisionValidColumn = columns.indexOf("decision_valid");
    const int dispositionColumn = columns.indexOf("disposition");
    const int additionalQueuedFrameColumn =
        columns.indexOf("additional_queued_frame");
    const int externalRebaseColumn =
        columns.indexOf("external_rebase_applied");
    const int externalRebaseFlagsColumn =
        columns.indexOf("external_rebase_flags");
    const int midframeWindowStateFlagsColumn =
        columns.indexOf("midframe_window_state_flags");
    const QList<QByteArray> controllerDiagnosticNames {
        "readiness_phase_us",
        "readiness_demand_us",
        "applied_readiness_reserve_us",
        "render_baseline_us",
        "render_insurance_us",
        "pacing_latency_budget_us",
        "cadence_sample_count",
        "rate_candidate_sample_count",
        "readiness_sample_count",
        "preparation_sample_count",
        "render_scheduler_sample_count",
        "target_scheduler_sample_count",
        "clean_spacing_frames",
        "phase_error_frames",
        "readiness_model_valid",
        "render_wait_initial_us",
        "render_wait_active_budget_us",
        "render_wait_coarse_sleep_count",
        "render_wait_coarse_requested_total_us",
        "render_wait_coarse_requested_wake_us",
        "render_wait_coarse_return_us",
        "render_wait_coarse_clock_stalled",
        "render_wait_active_entered",
        "render_wait_active_start_us",
        "render_wait_active_limit_us",
        "render_wait_active_yield_count",
        "render_wait_active_clock_stalled",
        "render_wait_active_yield_limit_reached",
        "target_wait_initial_us",
        "target_wait_active_budget_us",
        "target_wait_coarse_sleep_count",
        "target_wait_coarse_requested_total_us",
        "target_wait_coarse_requested_wake_us",
        "target_wait_coarse_return_us",
        "target_wait_coarse_clock_stalled",
        "target_wait_active_entered",
        "target_wait_active_start_us",
        "target_wait_active_limit_us",
        "target_wait_active_yield_count",
        "target_wait_active_clock_stalled",
        "target_wait_active_yield_limit_reached",
    };
    QList<int> controllerDiagnosticColumns;
    for (const QByteArray& name : controllerDiagnosticNames) {
        controllerDiagnosticColumns.append(columns.indexOf(name));
    }
    const QList<QByteArray> nativeEvidenceNames {
        "native_backend_valid",
        "native_backend",
        "native_present_result_valid",
        "native_present_result",
        "native_present_parameters_valid",
        "native_present_sync_interval",
        "native_present_flags",
        "native_vrr_state_valid",
        "native_tearing_supported",
        "native_borderless_flip_model",
        "native_same_gpu_output",
        "native_render_adapter_luid_valid",
        "native_render_adapter_luid",
        "native_swap_chain_allows_tearing",
        "native_tearing_feature_query_result_valid",
        "native_tearing_feature_query_result",
        "native_tearing_feature_allows_tearing",
        "native_swap_chain_desc_query_result_valid",
        "native_swap_chain_desc_query_result",
        "native_swap_chain_flags",
        "native_swap_chain_swap_effect",
        "native_fullscreen_state_query_result_valid",
        "native_fullscreen_state_query_result",
        "native_fullscreen_exclusive",
        "native_window_flags",
        "native_present_ready_available",
        "native_foreground_window",
        "native_vrr_fallback_reason",
        "native_desktop_monitor_count",
        "native_vblank_virtualization_probe_complete",
        "native_vblank_virtualization_call_available",
        "native_vblank_virtualization_result_valid",
        "native_vblank_virtualization_result",
        "native_vblank_virtualization_disabled",
        "native_display_config_query_result_valid",
        "native_display_config_query_result",
        "native_display_path_valid",
        "native_display_path_flags",
        "native_display_target_available",
        "native_display_source_adapter_luid",
        "native_display_source_id",
        "native_display_target_adapter_luid",
        "native_display_target_id",
        "native_display_output_technology",
        "native_display_rotation",
        "native_display_scaling",
        "native_display_path_refresh_numerator",
        "native_display_path_refresh_denominator",
        "native_display_signal_valid",
        "native_display_signal_pixel_rate_hz",
        "native_display_signal_hsync_numerator",
        "native_display_signal_hsync_denominator",
        "native_display_signal_vsync_numerator",
        "native_display_signal_vsync_denominator",
        "native_display_signal_active_width",
        "native_display_signal_active_height",
        "native_display_signal_total_width",
        "native_display_signal_total_height",
        "native_display_signal_additional_info_raw",
        "native_display_signal_scanline_ordering",
        "gpu_ready_attempted",
        "gpu_ready_signal_result_valid",
        "gpu_ready_signal_result",
        "gpu_ready_set_event_result_valid",
        "gpu_ready_set_event_result",
        "gpu_ready_wait_result_valid",
        "gpu_ready_wait_result",
        "gpu_ready_signal_start_us",
        "gpu_ready_signal_end_us",
        "gpu_ready_flush_start_us",
        "gpu_ready_flush_end_us",
        "gpu_ready_set_event_start_us",
        "gpu_ready_set_event_end_us",
        "native_raster_sampling_requested",
        "native_raster_open_result_valid",
        "native_raster_open_result",
        "native_raster_source_valid",
        "native_raster_vidpn_source_id",
        "native_raster_before_query_result_valid",
        "native_raster_before_query_result",
        "native_raster_before_query_start_us",
        "native_raster_before_query_end_us",
        "native_raster_before_in_vertical_blank",
        "native_raster_before_scanline",
        "native_raster_after_query_result_valid",
        "native_raster_after_query_result",
        "native_raster_after_query_start_us",
        "native_raster_after_query_end_us",
        "native_raster_after_in_vertical_blank",
        "native_raster_after_scanline",
        "submission_id_query_result_valid",
        "submission_id_query_result",
        "submission_id_query_start_us",
        "submission_id_query_end_us",
        "frame_stats_query_result_valid",
        "frame_stats_query_result",
        "frame_stats_query_start_us",
        "frame_stats_query_end_us",
        "latch_raw_sync_qpc_valid",
        "latch_raw_sync_qpc_ticks",
        "latch_raw_sync_qpc_frequency_hz",
        "latch_qpc_correlation_valid",
        "latch_qpc_correlation_reference_ticks",
        "latch_qpc_correlation_reference_time_us",
        "latch_qpc_correlation_span_ticks",
    };
    QList<int> nativeEvidenceColumns;
    for (const QByteArray& name : nativeEvidenceNames) {
        nativeEvidenceColumns.append(columns.indexOf(name));
    }
    expect(frameColumn >= 0 && rtpColumn >= 0 && arrivalColumn >= 0 &&
               decisionValidColumn >= 0 && dispositionColumn >= 0 &&
               additionalQueuedFrameColumn >= 0 &&
               externalRebaseColumn >= 0 &&
               externalRebaseFlagsColumn >= 0 &&
               midframeWindowStateFlagsColumn >= 0 &&
               std::all_of(
                   controllerDiagnosticColumns.cbegin(),
                   controllerDiagnosticColumns.cend(),
                   [](int column) { return column >= 0; }) &&
               std::all_of(
                   nativeEvidenceColumns.cbegin(),
                   nativeEvidenceColumns.cend(),
                   [](int column) { return column >= 0; }),
           "replay schema must expose raw arrivals and terminal disposition");

    bool observedFrames[11] = {};
    bool observedCapacityDrop = false;
    bool observedRejectedArrival = false;
    bool observedRestoreRebase = false;
    bool observedGeometryRebase = false;
    bool observedMidframeDisplayInterrupt = false;
    bool observedPostInterruptDisplayRebase = false;
    bool observedCleanFooter = false;
    QByteArray observedFooterHash;
    int rowCount = 0;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].isEmpty()) {
            continue;
        }
        if (lines[i].startsWith("#vrr_trace_footer,")) {
            const QByteArray hashMarker("decoded_sha256=");
            const int hashOffset = lines[i].indexOf(hashMarker);
            const QByteArray footerHash = hashOffset >= 0 ?
                lines[i].mid(hashOffset + hashMarker.size()) :
                QByteArray();
            observedFooterHash = footerHash;
            observedCleanFooter =
                lines[i].contains("format_version=2") &&
                lines[i].contains("clean_shutdown=1") &&
                lines[i].contains("arrival_sequence_allocated=10") &&
                lines[i].contains("rows_enqueued=10") &&
                lines[i].contains("rows_dropped=0") &&
                lines[i].contains("size_capped=0") &&
                lines[i].contains("write_failed=0") &&
                footerHash.size() == 64 &&
                std::all_of(
                    footerHash.cbegin(), footerHash.cend(),
                    [](char value) {
                        return (value >= '0' && value <= '9') ||
                            (value >= 'a' && value <= 'f');
                    });
            continue;
        }
        const QList<QByteArray> fields = lines[i].split(',');
        expect(fields.size() == columns.size(),
               "every replay row must match the declared schema");
        if (fields.size() != columns.size()) {
            continue;
        }
        ++rowCount;
        const int frameNumber = fields[frameColumn].toInt();
        if (frameNumber >= 1 && frameNumber <= 10) {
            observedFrames[frameNumber] = true;
        }
        expect(fields[rtpColumn].toULongLong() ==
                   static_cast<uint64_t>((frameNumber - 1) * 1500),
               "replay trace must preserve each raw RTP timestamp");
        expect(fields[arrivalColumn].toULongLong() != 0,
               "replay trace must capture the pacer arrival instant");
        expect(fields[additionalQueuedFrameColumn] == "0",
               "default trace rows must record low-latency queue policy");
        if (fields[decisionValidColumn] == "0") {
            for (int diagnosticColumn : controllerDiagnosticColumns) {
                expect(fields[diagnosticColumn] == "0",
                       "producer-side terminal rows must not inspect worker-owned controller state");
            }
            for (int evidenceColumn : nativeEvidenceColumns) {
                expect(fields[evidenceColumn] == "0",
                       "nondecision rows must not invent native outcome or QPC evidence");
            }
        }
        if (frameNumber == 2 &&
            fields[dispositionColumn] == "queue_capacity") {
            observedCapacityDrop = true;
            expect(fields[decisionValidColumn] == "0",
                   "pre-schedule eviction must not invent a timing decision");
        }
        if (frameNumber == 6 &&
            fields[dispositionColumn] == "arrival_rejected") {
            observedRejectedArrival = true;
            expect(fields[decisionValidColumn] == "0",
                    "suspended arrival must not invent a timing decision");
        }
        if (frameNumber == 7 &&
                fields[dispositionColumn] == "presented") {
            observedRestoreRebase =
                fields[decisionValidColumn] == "1" &&
                fields[externalRebaseColumn] == "1" &&
                fields[externalRebaseFlagsColumn] ==
                    QByteArray::number(
                        WINDOW_STATE_CHANGE_MINIMIZED |
                        WINDOW_STATE_CHANGE_RESTORED) &&
                fields[midframeWindowStateFlagsColumn] == "0";
        }
        if (frameNumber == 8 &&
                fields[dispositionColumn] == "presented") {
            observedGeometryRebase =
                fields[decisionValidColumn] == "1" &&
                fields[externalRebaseColumn] == "1" &&
                fields[externalRebaseFlagsColumn] ==
                    QByteArray::number(
                        WINDOW_STATE_CHANGE_SIZE) &&
                fields[midframeWindowStateFlagsColumn] == "0";
        }
        if (frameNumber == 9 &&
                fields[dispositionColumn] == "interrupted") {
            observedMidframeDisplayInterrupt =
                fields[decisionValidColumn] == "1" &&
                fields[externalRebaseColumn] == "0" &&
                fields[externalRebaseFlagsColumn] == "0" &&
                fields[midframeWindowStateFlagsColumn] ==
                    QByteArray::number(
                        WINDOW_STATE_CHANGE_DISPLAY);
        }
        if (frameNumber == 10 &&
                fields[dispositionColumn] == "presented") {
            observedPostInterruptDisplayRebase =
                fields[decisionValidColumn] == "1" &&
                fields[externalRebaseColumn] == "1" &&
                fields[externalRebaseFlagsColumn] ==
                    QByteArray::number(
                        WINDOW_STATE_CHANGE_DISPLAY) &&
                fields[midframeWindowStateFlagsColumn] == "0";
        }
    }
    expect(rowCount == 10,
           "trace must contain exactly one terminal row per delivered frame");
    expect(observedFrames[1] && observedFrames[2] && observedFrames[3] &&
               observedFrames[4] && observedFrames[5] && observedFrames[6] &&
               observedFrames[7] && observedFrames[8] &&
               observedFrames[9] && observedFrames[10],
           "trace must not omit evicted or presented deliveries");
    expect(observedCapacityDrop,
           "trace must identify the frame evicted by queue capacity");
    expect(observedRejectedArrival,
           "trace must retain frames rejected before queue admission");
    expect(observedRestoreRebase,
           "trace must distinguish an explicit window-state rebase from an internal controller reset");
    expect(observedGeometryRebase,
           "trace must begin a new epoch after renderer geometry/display state refresh");
    expect(observedMidframeDisplayInterrupt,
           "trace must identify a display change that interrupts an in-flight frame");
    expect(observedPostInterruptDisplayRebase,
           "the first frame after a mid-frame display change must carry its exact rebase cause");
    expect(observedCleanFooter,
           "trace must end with exact clean-close row accounting");
    const int footerOffset = expandedTrace.indexOf("#vrr_trace_footer,");
    const QByteArray expectedFooterHash = footerOffset >= 0 ?
        QCryptographicHash::hash(
            expandedTrace.left(footerOffset),
            QCryptographicHash::Sha256).toHex() :
        QByteArray();
    expect(!expectedFooterHash.isEmpty() &&
               observedFooterHash == expectedFooterHash,
           "trace footer must authenticate the decoded header and rows");

    SDL_setenv("MOONLIGHT_VRR_TRACE", "", 1);
}



void testSmoothnessTraceCapturesReadinessPolicy()
{
    resetFakeClock();
    QTemporaryDir traceDirectory;
    expect(traceDirectory.isValid(),
           "smoothness policy trace test must create a temporary directory");
    const QString tracePath = traceDirectory.filePath("vrr-smoothness-policy.csv");
    const QByteArray tracePathBytes = QFile::encodeName(tracePath);
    SDL_setenv("MOONLIGHT_VRR_TRACE", tracePathBytes.constData(), 1);
    SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "0", 1);

    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;
    TrackedFrameLifetime lifetime;
    VrrSessionConfig smoothnessConfig = enabledConfig();
    {
        VrrPacingWorker worker(&backend, smoothnessConfig, &telemetry);
        expect(worker.start(),
               "worker must start for playout policy tracing");
        worker.submit(frame(1, lifetime));
        expect(backend.waitForPresentCount(1),
               "smoothness policy trace must present one frame");
    }

    const QList<QByteArray> lines = readExpandedTrace(tracePath).split('\n');
    const QList<QByteArray> columns = lines.value(0).split(',');
    const int dispositionColumn = columns.indexOf("disposition");
    const int additionalQueueColumn =
        columns.indexOf("additional_queued_frame");
    const int lowPercentileColumn =
        columns.indexOf("param_readiness_low_percentile");
    const int loosePercentileColumn =
        columns.indexOf("param_readiness_loose_percentile");
    const int sourceDelayColumn =
        columns.indexOf("param_source_playout_delay_us");
    const int timestampPlayoutColumn =
        columns.indexOf("param_timestamp_playout_enabled");
    const int retainReserveColumn =
        columns.indexOf("param_retain_readiness_on_phase_reset");
    const int adaptiveColumn =
        columns.indexOf("param_playout_delay_adaptive");
    const int offsetGateColumn = columns.indexOf("param_playout_offset_cadence_gate");
    const int offsetRateColumn = columns.indexOf("param_playout_offset_slew_us_per_second");
    const int offsetClockColumn = columns.indexOf("param_playout_offset_source_clock");
    const int offsetStepColumn = columns.indexOf("param_playout_offset_maximum_step_us");
    const int playoutDelayColumn = columns.indexOf("playout_delay_us");
    expect(dispositionColumn >= 0 && additionalQueueColumn >= 0 &&
               lowPercentileColumn >= 0 && loosePercentileColumn >= 0 &&
               sourceDelayColumn >= 0 && timestampPlayoutColumn >= 0 &&
               retainReserveColumn >= 0 && adaptiveColumn >= 0 &&
               playoutDelayColumn >= 0 && offsetGateColumn >= 0 &&
               offsetRateColumn >= 0 && offsetClockColumn >= 0 &&
               offsetStepColumn >= 0,
           "smoothness trace must expose its resolved playout policy and the applied delay");

    bool foundPresentedRow = false;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].isEmpty() || lines[i].startsWith("#vrr_trace_footer,")) {
            continue;
        }
        const QList<QByteArray> fields = lines[i].split(',');
        if (fields.value(dispositionColumn) != "presented") {
            continue;
        }
        foundPresentedRow = true;
        expect(fields.value(additionalQueueColumn) == "0" &&
                   fields.value(lowPercentileColumn) == "0" &&
                   fields.value(loosePercentileColumn) == "80" &&
                   fields.value(sourceDelayColumn) == "3000" &&
                   fields.value(timestampPlayoutColumn) == "1" &&
                   fields.value(adaptiveColumn) == "1" &&
                   fields.value(retainReserveColumn) == "0" &&
                   fields.value(offsetGateColumn) == "1" &&
                   fields.value(offsetRateColumn) == "2400" &&
                   fields.value(offsetClockColumn) == "1" &&
                   fields.value(offsetStepColumn) == "100" &&
                   fields.value(playoutDelayColumn).toULongLong() >= 1000,
                "the session must record the single adaptive timestamp playout policy");
    }
    expect(foundPresentedRow,
           "smoothness policy trace must contain a presented row");

    SDL_setenv("MOONLIGHT_VRR_TRACE", "", 1);
    SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "0", 1);
}

void testFailedCancellationNativeEvidenceIsTraced()
{
    resetFakeClock();
    QTemporaryDir traceDirectory;
    expect(traceDirectory.isValid(),
           "failed cancellation trace test must create a temporary directory");
    const QString tracePath =
        traceDirectory.filePath("vrr-cancellation-failure.csv");
    const QByteArray tracePathBytes = QFile::encodeName(tracePath);
    SDL_setenv("MOONLIGHT_VRR_TRACE", tracePathBytes.constData(), 1);
    SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "0", 1);

    FakeVrrFramePresenter backend;
    backend.setPreparationSucceeds(false);
    backend.setCancellationMaySubmit(true);
    backend.setCancelSubmits(false);
    PacerTelemetry telemetry;
    TrackedFrameLifetime failed;
    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(),
               "worker must start for failed cancellation tracing");
        worker.submit(frame(1, failed));
        expect(backend.waitForCancelCount(1),
               "failed preparation must attempt native cancellation");
    }

    const QByteArray expandedTrace = readExpandedTrace(tracePath);
    const QList<QByteArray> lines = expandedTrace.split('\n');
    const QList<QByteArray> columns = lines.value(0).split(',');
    const QList<QByteArray> fields = lines.value(1).split(',');
    const int dispositionColumn = columns.indexOf("disposition");
    const int backendValidColumn = columns.indexOf("native_backend_valid");
    const int backendColumn = columns.indexOf("native_backend");
    const int resultValidColumn =
        columns.indexOf("native_present_result_valid");
    const int resultColumn = columns.indexOf("native_present_result");
    expect(fields.size() == columns.size() &&
               dispositionColumn >= 0 &&
               backendValidColumn >= 0 &&
               backendColumn >= 0 &&
               resultValidColumn >= 0 &&
               resultColumn >= 0,
           "failed cancellation trace must retain a complete row");
    if (fields.size() == columns.size() &&
            dispositionColumn >= 0 &&
            backendValidColumn >= 0 &&
            backendColumn >= 0 &&
            resultValidColumn >= 0 &&
            resultColumn >= 0) {
        expect(fields[dispositionColumn] == "preparation_failed" &&
                   fields[backendValidColumn] == "1" &&
                   fields[backendColumn] == "2" &&
                   fields[resultValidColumn] == "1" &&
                   fields[resultColumn] == "-1",
               "failed native cancellation must not be collapsed into generic cancellation");
    }

    SDL_setenv("MOONLIGHT_VRR_TRACE", "", 1);
}

void testReconnectPreservesCompletedTraces()
{
    resetFakeClock();
    QTemporaryDir directory;
    expect(directory.isValid(), "reconnect trace directory must exist");
    const QString tracePath = directory.filePath("capture.vrrtrace");
    SDL_setenv("MOONLIGHT_VRR_TRACE", QFile::encodeName(tracePath).constData(), 1);
    QByteArray previous;
    for (int connection = 1; connection <= 3; ++connection) {
        FakeVrrFramePresenter backend;
        TrackedFrameLifetime lifetime;
        {
            VrrPacingWorker worker(&backend, enabledConfig(), nullptr);
            expect(worker.start(), "reconnected worker must start");
            worker.submit(frame(connection, lifetime));
            expect(backend.waitForPresentCount(1), "reconnected stream must present");
        }
        if (connection > 1) {
            expect(readExpandedTrace(directory.filePath(
                       QStringLiteral("capture-connection-%1.vrrtrace").arg(connection - 1))) == previous,
                   "reconnect must preserve the previous complete trace unchanged");
        }
        const QByteArray current = readExpandedTrace(tracePath);
        expect(current.contains("clean_shutdown=1,arrival_sequence_allocated=1"),
               "canonical trace path must contain the latest complete connection");
        expect(current != previous, "new connection must write its own frame data");
        previous = current;
    }
    expect(!readExpandedTrace(directory.filePath("capture-connection-1.vrrtrace")).isEmpty(),
           "third connection must retain the first archived capture");
    SDL_setenv("MOONLIGHT_VRR_TRACE", "", 1);
}

void testDeepTraceRequestsNativeObservationsWithoutChangingMode()
{
    resetFakeClock();
    QTemporaryDir traceDirectory;
    expect(traceDirectory.isValid(),
           "deep diagnostics test must create a temporary directory");
    const QString tracePath = traceDirectory.filePath("vrr-deep-trace.vrrtrace");
    const QByteArray tracePathBytes = QFile::encodeName(tracePath);
    SDL_setenv("MOONLIGHT_VRR_TRACE", tracePathBytes.constData(), 1);
    SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "1", 1);
    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;
    TrackedFrameLifetime first;

    auto cachedConfig = enabledConfig();
    cachedConfig.calibrationPath = traceDirectory.filePath("profile.json").toStdString();
    cachedConfig.calibrationKey = "replay-test";
    Vrr13::Reserve cachedHistory(20);
    for (int i = 0; i < 256; ++i)
        cachedHistory.observe(4000000, 8000000, Vrr13::Reserve::Second + int64_t(i) * 16667000);
    expect(Vrr13::saveProfile(QString::fromStdString(cachedConfig.calibrationPath),
                             "replay-test", cachedHistory), "test calibration must save");
    {
        VrrPacingWorker worker(&backend, cachedConfig, &telemetry);
        expect(worker.start(), "worker must start for deep diagnostics testing");
        auto input = frame(1, first);
        worker.submit(std::move(input));
        expect(backend.waitForPresentCount(1),
               "deep diagnostics must not suppress presentation");
    }

    const std::vector<VrrPresentRequest> requests = backend.presentRequests();
    expect(requests.size() == 1 && requests[0].collectDiagnostics,
           "deep trace must request adjacent native observations");
    expect(requests.size() == 1 && !requests[0].latchedPresentation,
           "deep trace must not change the controller presentation mode");

    const QByteArray expandedTrace = readExpandedTrace(tracePath);
    const QList<QByteArray> lines = expandedTrace.split('\n');
    const QByteArray header = lines.value(0);
    const QByteArray row = lines.value(1);
    const QList<QByteArray> columns = header.split(',');
    const QList<QByteArray> fields = row.split(',');
    expect(columns.size() == fields.size(), "diagnostic columns must align with every value");
    expect(columns.contains("decoder_output_us") &&
               fields.value(columns.indexOf("decoder_output_us")).toULongLong() > 0 &&
               fields.value(columns.indexOf("decoder_output_us")).toULongLong() ==
                   fields.value(columns.indexOf("decode_complete_us")).toULongLong(),
           "a frame without a blocking fence wait must keep decoder output as its readiness boundary");
    expect(fields.value(columns.indexOf("session_latency_mode")) ==
               QByteArray::number(cachedConfig.latencyMode) &&
               fields.value(columns.indexOf("calibration_loaded")) == "1" &&
               fields.value(columns.indexOf("initial_cached_samples")).toULongLong() >= 240 &&
               fields.value(columns.indexOf("history_version")) == "20" &&
               fields.value(columns.indexOf("history_state_valid")) == "1",
           "capture must identify the active preset and loaded calibration independently of native present results");
    expect(columns.contains("presentation_uncertainty_us") &&
           fields.value(columns.indexOf("presentation_uncertainty_us")) == "0",
           "trace must preserve non-DXGI clock uncertainty, defaulting to zero for legacy presenters");
    std::vector<int64_t> profile;
    expect(columns.contains("original_target_us") &&
           decodeVrrPlayoutProfile(fields.value(columns.indexOf("playout_initial_profile")), profile),
           "capture must carry its original deadline and complete starting calibration");
    Vrr13::Reserve restored(20);
    expect(restored.loadProfile(profile) && restored.common() == 4000000 && restored.evidence() == 0,
           "captured calibration must restore prior history without inventing fresh successes");
    expect(fields.value(columns.indexOf("param_playout_prediction_only")) == "1" &&
               fields.value(columns.indexOf("param_playout_responsive_buffer")) == "7" &&
               fields.value(columns.indexOf("param_playout_native_hitch_adaptation")) == "0",
           "capture must identify production interval-quality adaptation for exact replay");
    expect(header.contains("frame_receive_us") &&
               header.contains("frame_reassembled_us") &&
               header.contains("decode_submit_us") &&
               header.contains("native_present_call_us") &&
               header.contains("presenter_submission_time_valid") &&
               header.contains("presenter_submission_time_us") &&
               header.contains("presenter_submission_time_used") &&
               header.contains("gpu_ready_attempted") &&
               header.contains("gpu_ready_signal_result_valid") &&
               header.contains("gpu_ready_signal_result") &&
               header.contains("gpu_ready_set_event_result_valid") &&
               header.contains("gpu_ready_set_event_result") &&
               header.contains("gpu_ready_wait_result_valid") &&
               header.contains("gpu_ready_wait_result") &&
               header.contains("gpu_ready_signal_start_us") &&
               header.contains("gpu_ready_signal_end_us") &&
               header.contains("gpu_ready_flush_start_us") &&
               header.contains("gpu_ready_flush_end_us") &&
               header.contains("gpu_ready_set_event_start_us") &&
               header.contains("gpu_ready_set_event_end_us") &&
               header.contains("gpu_ready_poll_start_us") &&
               header.contains("gpu_ready_poll_end_us") &&
               header.contains("gpu_ready_fence_value") &&
               header.contains("gpu_ready_poll_completed_value") &&
               header.contains("gpu_ready_completed_before_wait") &&
               header.contains("gpu_ready_completion_lower_bound_us") &&
               header.contains("gpu_ready_completion_upper_bound_us") &&
               header.contains("gpu_ready_completion_uncertainty_us") &&
               header.contains("gpu_ready_wait_us") &&
               header.contains(
                   "native_display_signal_additional_info_raw") &&
               header.contains("tear_classification") &&
               header.contains("spacing_deficit_us") &&
               header.contains("spacing_guard_feedback_us") &&
               header.contains("spacing_corrected") &&
               header.contains("spacing_recheck_us") &&
               header.contains("spacing_corrected_floor_us") &&
               header.contains("submission_id_query_start_us") &&
               header.contains("submission_id_query_end_us") &&
               header.contains("frame_stats_query_start_us") &&
               header.contains("frame_stats_query_end_us") &&
               header.contains("completion_queue_depth") &&
               header.contains("latch_present_refresh_seq"),
           "deep trace must identify presenter, native, tear, spacing, queue, and renderer-readiness timing");
    expect(row.startsWith("5,"),
           "new captures must use parameterized trace schema 5");
    expect(!row.isEmpty() && header.count(',') == row.count(','),
           "deep trace rows must match the CSV schema");
    const int submissionBoundaryColumn =
        columns.indexOf("submission_boundary_us");
    const int presenterSubmissionValidColumn =
        columns.indexOf("presenter_submission_time_valid");
    const int presenterSubmissionTimeColumn =
        columns.indexOf("presenter_submission_time_us");
    const int presenterSubmissionUsedColumn =
        columns.indexOf("presenter_submission_time_used");
    expect(fields.size() == columns.size() &&
               submissionBoundaryColumn >= 0 &&
               presenterSubmissionValidColumn >= 0 &&
               presenterSubmissionTimeColumn >= 0 &&
               presenterSubmissionUsedColumn >= 0,
           "deep trace must retain a complete presenter submission-timing row");
    if (fields.size() == columns.size() &&
            submissionBoundaryColumn >= 0 &&
            presenterSubmissionValidColumn >= 0 &&
            presenterSubmissionTimeColumn >= 0 &&
            presenterSubmissionUsedColumn >= 0) {
        expect(fields[presenterSubmissionValidColumn] == "1" &&
                   fields[presenterSubmissionUsedColumn] == "1" &&
                   fields[presenterSubmissionTimeColumn] ==
                       fields[submissionBoundaryColumn],
               "worker submission boundary must preserve the presenter's exact native-call timestamp");
    }
    expect(expandedTrace.contains(
               "#vrr_trace_footer,format_version=2,clean_shutdown=1,"
               "arrival_sequence_allocated=1,rows_enqueued=1,"
               "rows_dropped=0,size_capped=0,write_failed=0,"
               "decoded_sha256="),
           "compressed traces must retain the clean-close accounting footer");
    QFile traceFile(tracePath);
    expect(traceFile.size() > 7 && traceFile.size() < expandedTrace.size(),
           "recommended traces must be chunk-compressed on disk");
    expect(traceFile.size() < 16384,
           "one deep trace row must remain compact");

    const char* exportPath = SDL_getenv("MOONLIGHT_VRR_TEST_EXPORT_TRACE");
    if (exportPath != nullptr && exportPath[0] != '\0') {
        QFile::remove(QString::fromLocal8Bit(exportPath));
        expect(QFile::copy(tracePath, QString::fromLocal8Bit(exportPath)),
               "deep trace test must export its replay fixture when requested");
    }

    SDL_setenv("MOONLIGHT_VRR_TRACE", "", 1);
    SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "0", 1);
}

void testTraceCapturesAllowTearingWithoutChangingController()
{
    for (bool allowTearing : {true, false}) {
        resetFakeClock();
        QTemporaryDir directory;
        expect(directory.isValid(), "permission trace fixture needs a temporary directory");
        const QString tracePath = directory.filePath("vrr-permission.vrrtrace");
        const QByteArray tracePathBytes = QFile::encodeName(tracePath);
        SDL_setenv("MOONLIGHT_VRR_TRACE", tracePathBytes.constData(), 1);
        SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "1", 1);
        FakeVrrFramePresenter backend;
        PacerTelemetry telemetry;
        TrackedFrameLifetime lifetime;
        auto config = enabledConfig();
        config.allowTearing = allowTearing;
        const auto parameters = vrrTimingParametersForSession(config);
        auto controlConfig = config;
        controlConfig.allowTearing = !allowTearing;
        const auto controlParameters = vrrTimingParametersForSession(controlConfig);
#define VRR_EXPECT_PERMISSION_INDEPENDENT_PARAMETER(type, jsonName, memberName, defaultValue) \
        expect(parameters.memberName == controlParameters.memberName, \
               "Allow tearing must leave controller parameter " #jsonName " unchanged");
        VRR_TIMING_PARAMETER_FIELDS(VRR_EXPECT_PERMISSION_INDEPENDENT_PARAMETER)
#undef VRR_EXPECT_PERMISSION_INDEPENDENT_PARAMETER
        {
            VrrPacingWorker worker(&backend, config, &telemetry);
            expect(worker.start(), "permission fixture worker must start");
            worker.submit(frame(1, lifetime));
            expect(backend.waitForPresentCount(1), "both permission arms must present");
        }
        const auto requests = backend.presentRequests();
        expect(requests.size() == 1 && !requests[0].latchedPresentation,
               "native permission must not manufacture a controller latch request");
        const QByteArray expanded = readExpandedTrace(tracePath);
        const auto lines = expanded.split('\n');
        const auto columns = lines.value(0).split(',');
        const auto fields = lines.value(1).split(',');
        expect(columns.size() == fields.size() && columns.last() == "session_allow_tearing",
               "permission must be an appended schema-5 field aligned with the row");
        expect(fields.value(columns.indexOf("session_allow_tearing")) ==
                   (allowTearing ? "1" : "0"),
               "trace must identify the snapshotted native permission arm");
        expect(fields.value(columns.indexOf("trace_schema")) == "5" &&
                   fields.value(columns.indexOf("latched_present")) == "0",
               "the off arm keeps the existing schema and recorded controller decision");
        if (!allowTearing) {
            const char* exportPath = SDL_getenv("MOONLIGHT_VRR_TEST_EXPORT_NO_TEAR_TRACE");
            if (exportPath && exportPath[0]) {
                QFile::remove(QString::fromLocal8Bit(exportPath));
                expect(QFile::copy(tracePath, QString::fromLocal8Bit(exportPath)),
                       "off-arm trace must export its exact replay fixture when requested");
            }
        }
        SDL_setenv("MOONLIGHT_VRR_TRACE", "", 1);
        SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "0", 1);
    }
}

void testTraceQueueConcurrency()
{
    Vrr13::TraceQueue<uint64_t, 4> bounded;
    for (uint64_t i = 0; i < 4; ++i) expect(bounded.push(uint64_t(i)), "trace queue must accept its capacity");
    expect(!bounded.push(99), "a full trace queue must reject immediately");
    for (uint64_t i = 0, value = 0; i < 4; ++i)
        expect(bounded.pop(value) && value == i, "trace queue must preserve order across wrap");
    uint64_t value = 0;
    expect(!bounded.pop(value), "an empty trace queue must reject immediately");
    struct Row { uint64_t producer = 0, sequence = 0, checksum = 0; };
    Vrr13::TraceQueue<Row, 1024> queue;
    constexpr unsigned producers = 3, rows = 20000;
    std::array<std::thread, producers> threads;
    for (unsigned p = 0; p < producers; ++p) {
        threads[p] = std::thread([&, p] {
            for (unsigned i = 0; i < rows; ++i) {
                Row row{p, i, (uint64_t(p) << 32) ^ i ^ 0xabcddcba};
                while (!queue.push(std::move(row))) std::this_thread::yield();
            }
        });
    }
    std::array<uint64_t, producers> next{};
    bool valid = true;
    for (unsigned i = 0; i < producers * rows;) {
        Row row;
        if (!queue.pop(row)) { std::this_thread::yield(); continue; }
        valid &= row.producer < producers;
        if (row.producer < producers) valid &= row.sequence == next[row.producer]++;
        valid &= row.checksum == ((row.producer << 32) ^ row.sequence ^ 0xabcddcba);
        ++i;
    }
    for (auto& thread : threads) thread.join();
    expect(valid, "concurrent trace producers must publish complete rows exactly once and in producer order");
}

void exportWarmHistoryReplayFixture()
{
    const char* exportPath = SDL_getenv("MOONLIGHT_VRR_TEST_EXPORT_WARM_TRACE");
    if (!exportPath || !exportPath[0]) return;
    resetFakeClock();
    SDL_setenv("MOONLIGHT_VRR_TRACE", exportPath, 1);
    SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "1", 1);
    FakeVrrFramePresenter backend;
    PacerTelemetry telemetry;
    TrackedFrameLifetime lifetime[180];
    {
        VrrPacingWorker worker(&backend, enabledConfig(), &telemetry);
        expect(worker.start(), "warm-history replay worker must start");
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 180; ++i) {
            std::this_thread::sleep_until(start + std::chrono::microseconds(int64_t(i) * 16667));
            if (i == 120) {
                expect(backend.waitForPresentCount(120), "feedback fixture must drain before its controlled stall");
                backend.blockPreparation();
            }
            worker.submit(frame(i + 1, lifetime[i]));
            if (i == 120) {
                expect(backend.waitForPrepareCount(121), "feedback fixture must enter preparation before its controlled stall");
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                backend.releasePreparation();
            }
        }
        expect(backend.waitForPresentCount(180), "warm-history replay must drain every submitted frame");
    }
    SDL_setenv("MOONLIGHT_VRR_TRACE", "", 1);
    SDL_setenv("MOONLIGHT_VRR_DEEP_TRACE", "0", 1);
}

} // namespace

// VrrPacingWorker uses the common monotonic clock. The isolated test owns an
// equivalent steady-clock epoch so it needs no network or streaming runtime.
extern "C" uint64_t LiGetMicroseconds(void)
{
    const uint64_t frozenUs = g_FrozenTestClockUs.load();
    if (frozenUs != 0) return frozenUs;
    const int64_t elapsedUs =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - g_TestClockOrigin).count();
    return static_cast<uint64_t>(elapsedUs + g_TestClockOffsetUs.load());
}

void testReadinessWindow()
{
    Vrr13::ReadinessWindow window;
    for (uint64_t late : {uint64_t(0), uint64_t(1000), uint64_t(2000), uint64_t(2001)})
        window.record(1000000, late, false);
    window.record(1000000, 0, true);
    auto result = window.snapshot();
    expect(result.samples == 5 && result.misses == 4 && result.over1ms == 2 &&
               result.over2ms == 1 && result.dropped == 1,
           "readiness must retain strict deadlines, separate severity, and count drops as misses");
    result = window.snapshot(0, true);
    expect(result.samples == 5 && result.misses == 2 && result.over1ms == 2 &&
               result.over2ms == 1 && result.dropped == 1,
           "thresholded readiness must tolerate through 1 ms and sparse 1-2 ms misses");
    Vrr13::ReadinessWindow prevalentSoft;
    prevalentSoft.record(1000000, 1500, false);
    prevalentSoft.record(1000000, 1500, false);
    prevalentSoft.record(1000000, 0, false);
    result = prevalentSoft.snapshot(0, true);
    expect(result.samples == 3 && result.misses == 2,
           "1-2 ms readiness misses must count only when they exceed half the window");
    expect(window.snapshot(31000000).samples == 0,
           "the thirty-second outcome window must expire without another frame");
    window.record(32000000, 0, false);
    window.record(31999999, 0, false);
    result = window.snapshot();
    expect(result.samples == 2 && result.misses == 0 && result.atUs == 32000000,
           "old failures must expire and reversed producer lock order must preserve both outcomes");
}

int main()
{
    Vrr13::ReadinessWindow averageWindow;
    averageWindow.record(1000000, 500, false);
    averageWindow.record(1001000, 1500, false);
    averageWindow.record(1002000, 0, false);
    expect(Vrr13::ReadinessWindow::meanMissUs(averageWindow.snapshot()) == 1000.0 &&
        Vrr13::ReadinessWindow::meanMissScore(averageWindow.snapshot()) == 100.0,
        "the mean-miss score must remain exactly 100 through one millisecond");
    averageWindow.record(1003000, 2000, false);
    expect(Vrr13::ReadinessWindow::meanMissScore(averageWindow.snapshot()) < 100.0,
        "the mean-miss score must fall only when the average exceeds one millisecond");
    expect(Vrr13::ReadinessWindow::meanMissScore(averageWindow.snapshot(32000000)) == 100.0,
        "expired misses must not depress the current mean-miss score");
    testTraceQueueConcurrency();
    testReadinessWindow();
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    testCapabilityRejection();
    testPresentationRequestSelectedBeforePreparation();
    testEmptyQueueDoesNotRepeatFrames();
    testQueueCapacityAndDrops();
    testLatePreparedFramePresentsImmediately();
    testQueuedStaleFrameYieldsToFreshSuccessor();
    testSinglePeriodQueueDelayPreservesFluidity();
    testLatencyFixDropBoundaries();
    testLatencyFixQueuedRecovery();
    testLatencyPresetsQueuedRecovery();
    testLatencyFixQueueAgeIncludesDecodeWait();
    testTelemetrySnapshotsRemainCumulative();
    testSuspendDiscardAndFreshFrame();
    testDeferredSurfaceLifetime();
    testReusableSurfaceReleasedWithoutSuccessor();
    testDecodeBoundaryCapturedBeforeQueueAndPreparedExactly();
    testCancelledPresentationCountsAsDroppedOutput();
    testPresentCallSpacingSetsDisplayFloor();
    testBlockingPresentUsesWorkerCallBoundary();
    testSubmissionErrorCapturesPresentOverhead();
    testFailedPreparationCancellationHonorsDisplayFloor();
    testSuspendedPreparedCancellationHonorsDisplayFloor();
    testImmutablePresentationContract();
    testTraceCapturesEveryDeliveredFrame();
    testSmoothnessTraceCapturesReadinessPolicy();
    testFailedCancellationNativeEvidenceIsTraced();
    testReconnectPreservesCompletedTraces();
    testDeepTraceRequestsNativeObservationsWithoutChangingMode();
    testTraceCapturesAllowTearingWithoutChangingController();

    exportWarmHistoryReplayFixture();
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}
