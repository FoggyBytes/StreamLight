#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/profilecodec.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrtimingcontroller.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrtargetwaiter.h"
#include "vrrreplayconfig.h"
#include "vrrreplaymodel.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/readinesswindow.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>

namespace {

constexpr char kTraceMagic[] = "MLVRR1\n";
constexpr char kReplayModel[] =
    "recorded-world-controller-exact-spacing-recorded-coarse-wake-return-active-margin-absorbed-explicit-wait-path-coverage-scheduler-injection-measured-raster-probe-normalization-display-transition-faults-gpu-stage-timed-fence-bounded-post-present-query-timed-actual-swapchain-capability-explicit-display-epoch-causes-picosecond-circular-sync-phase-raster-envelope-displayconfig-complete-signal-d3dkmt-dynamic-scanline-scale-phase-validated-freshest-causal-anchor-free-running-refresh-2026-07";
constexpr uint64_t kMinimumExactRasterValidationSamples = 100;
constexpr uint64_t kSyncAnchorTranslationJitterToleranceUs = 50;
constexpr uint64_t kRawQpcTranslationToleranceUs = 2;
constexpr uint64_t kSyncAnchorMinimumIntervalToleranceUs = 500;
constexpr uint64_t kDisplaySignalConsistencyTolerancePpm = 100;
constexpr uint64_t kCapturedWorkerQueueCapacity = 3;
constexpr uint64_t kNativeBackendDxgi = 1;
constexpr uint64_t kNativeBackendVulkan = 2;
constexpr uint64_t kNativeBackendComposition = 3;
constexpr uint64_t kSdlWindowFullscreenDesktop = 0x00001001ULL;
constexpr uint64_t kDisplayConfigPathActive = 0x00000001ULL;
constexpr uint64_t kDisplayConfigPathBoostRefreshRate = 0x00000010ULL;
// DISPLAYCONFIG_PATH_VALID_FLAGS from the serialized Windows ABI, including
// the currently unimplemented PREFERRED_UNSCALED bit.
constexpr uint64_t kDisplayConfigPathKnownFlags = 0x0000001dULL;
constexpr uint64_t kDisplayConfigProgressiveScan = 1;
constexpr uint64_t kDisplayConfigRotationIdentity = 1;
constexpr uint64_t kDisplayConfigScalingIdentity = 1;
constexpr uint64_t kWindowStateChangeSize = 0x01ULL;
constexpr uint64_t kWindowStateChangeDisplay = 0x02ULL;
constexpr uint64_t kWindowStateChangeMinimized = 0x04ULL;
constexpr uint64_t kWindowStateChangeRestored = 0x08ULL;
constexpr uint64_t kWindowStateChangeSuspended = 0x10ULL;
constexpr uint64_t kWindowStateChangeKnownMask =
    kWindowStateChangeSize |
    kWindowStateChangeDisplay |
    kWindowStateChangeMinimized |
    kWindowStateChangeRestored |
    kWindowStateChangeSuspended;
constexpr uint64_t kWindowStateChangeDisplayEpochMask =
    kWindowStateChangeSize |
    kWindowStateChangeDisplay;
constexpr uint64_t kWaiterMaximumActiveWaitUs =
    VrrTargetWaiter::kMaximumActiveWaitUs;
constexpr uint64_t kWaiterMaximumAdditionalWakeLeadUs =
    VrrTargetWaiter::kMaximumAdditionalWakeLeadUs;

class TraceReader {
public:
    explicit TraceReader(const QString& path) : m_File(path) {}

    bool open(QString& error)
    {
        if (!m_File.open(QIODevice::ReadOnly)) {
            error = m_File.errorString();
            return false;
        }
        const QByteArray magic = m_File.read(sizeof(kTraceMagic) - 1);
        m_Compressed = magic == kTraceMagic;
        if (!m_Compressed && !m_File.seek(0)) {
            error = m_File.errorString();
            return false;
        }
        return true;
    }

    bool readLine(QByteArray& line, QString& error)
    {
        if (!m_Compressed) {
            line = m_File.readLine();
            if (!line.isEmpty()) {
                return true;
            }
            if (m_File.error() != QFileDevice::NoError) {
                error = m_File.errorString();
            }
            return false;
        }

        while (m_ChunkOffset >= m_Chunk.size()) {
            char lengthBytes[4];
            const qint64 lengthRead = m_File.read(lengthBytes, 4);
            if (lengthRead == 0) {
                return false;
            }
            if (lengthRead != 4) {
                error = "partial compressed chunk length";
                return false;
            }
            const auto* bytes = reinterpret_cast<const unsigned char*>(
                lengthBytes);
            const uint32_t compressedBytes =
                static_cast<uint32_t>(bytes[0]) |
                (static_cast<uint32_t>(bytes[1]) << 8) |
                (static_cast<uint32_t>(bytes[2]) << 16) |
                (static_cast<uint32_t>(bytes[3]) << 24);
            const QByteArray encoded = m_File.read(compressedBytes);
            if (encoded.size() != static_cast<int>(compressedBytes)) {
                error = "partial compressed chunk payload";
                return false;
            }
            m_Chunk = qUncompress(
                reinterpret_cast<const uchar*>(encoded.constData()),
                encoded.size());
            m_ChunkOffset = 0;
            if (m_Chunk.isEmpty()) {
                error = "invalid compressed chunk";
                return false;
            }
        }

        const int newline = m_Chunk.indexOf('\n', m_ChunkOffset);
        if (newline < 0) {
            error = "compressed chunk ends in a partial CSV row";
            return false;
        }
        line = m_Chunk.mid(m_ChunkOffset, newline - m_ChunkOffset + 1);
        m_ChunkOffset = newline + 1;
        return true;
    }

private:
    QFile m_File;
    bool m_Compressed = false;
    QByteArray m_Chunk;
    int m_ChunkOffset = 0;
};

struct TraceFooter {
    bool present = false;
    uint64_t formatVersion = 0;
    bool cleanShutdown = false;
    uint64_t arrivalSequenceAllocated = 0;
    uint64_t rowsEnqueued = 0;
    uint64_t rowsDropped = 0;
    bool sizeCapped = false;
    bool writeFailed = false;
    QByteArray decodedSha256;
};

bool parseTraceFooter(const QByteArray& line, TraceFooter& footer,
                      QString& error)
{
    if (footer.present) {
        error = "trace contains more than one clean-close footer";
        return false;
    }
    const QList<QByteArray> fields = line.split(',');
    if (fields.isEmpty() || fields.front() != "#vrr_trace_footer") {
        error = "invalid trace footer marker";
        return false;
    }
    QMap<QByteArray, QByteArray> values;
    for (int i = 1; i < fields.size(); ++i) {
        const int equals = fields[i].indexOf('=');
        if (equals <= 0 || equals == fields[i].size() - 1) {
            error = "invalid trace footer field";
            return false;
        }
        const QByteArray key = fields[i].left(equals);
        if (values.contains(key)) {
            error = "duplicate trace footer field: " +
                QString::fromLatin1(key);
            return false;
        }
        values.insert(key, fields[i].mid(equals + 1));
    }
    const QList<QByteArray> required {
        "format_version",
        "clean_shutdown",
        "arrival_sequence_allocated",
        "rows_enqueued",
        "rows_dropped",
        "size_capped",
        "write_failed",
    };
    std::array<uint64_t, 7> parsed {};
    for (int i = 0; i < required.size(); ++i) {
        if (!values.contains(required[i])) {
            error = "trace footer is missing " +
                QString::fromLatin1(required[i]);
            return false;
        }
        bool ok = false;
        parsed[i] = values.value(required[i]).toULongLong(&ok);
        if (!ok || QByteArray::number(parsed[i]) !=
                values.value(required[i])) {
            error = "trace footer field is not a canonical unsigned integer: " +
                QString::fromLatin1(required[i]);
            return false;
        }
    }
    if ((parsed[0] != 1 && parsed[0] != 2) ||
            parsed[1] > 1 || parsed[5] > 1 ||
            parsed[6] > 1) {
        error = "trace footer has an unsupported version or invalid boolean";
        return false;
    }
    QSet<QByteArray> allowed;
    for (const QByteArray& name : required) {
        allowed.insert(name);
    }
    if (parsed[0] == 2) {
        allowed.insert("decoded_sha256");
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!allowed.contains(it.key())) {
            error = "trace footer contains an unknown field: " +
                QString::fromLatin1(it.key());
            return false;
        }
    }
    if (values.size() != allowed.size()) {
        error = "trace footer has unknown or missing fields";
        return false;
    }
    const QByteArray decodedSha256 = values.value("decoded_sha256");
    if (parsed[0] == 2 &&
            (decodedSha256.size() != 64 ||
             !std::all_of(
                 decodedSha256.cbegin(), decodedSha256.cend(),
                 [](char value) {
                     return (value >= '0' && value <= '9') ||
                         (value >= 'a' && value <= 'f');
                 }))) {
        error = "trace footer decoded_sha256 is not canonical lowercase SHA-256";
        return false;
    }
    footer.present = true;
    footer.formatVersion = parsed[0];
    footer.cleanShutdown = parsed[1] != 0;
    footer.arrivalSequenceAllocated = parsed[2];
    footer.rowsEnqueued = parsed[3];
    footer.rowsDropped = parsed[4];
    footer.sizeCapped = parsed[5] != 0;
    footer.writeFailed = parsed[6] != 0;
    footer.decodedSha256 = decodedSha256;
    return true;
}

bool validateTraceRowSyntax(const QList<QByteArray>& header,
                            const QList<QByteArray>& fields,
                            QString& error)
{
    static const QSet<QByteArray> signedColumns {
        "frame",
        "display_refresh_hz",
        "stream_rate_hz",
        "ready_offset_us",
        "readiness_budget_us",
        "submit_error_us",
        "spacing_margin_us",
        "cadence_smoothing_us",
        "readiness_phase_us",
        "native_present_result",
        "native_tearing_feature_query_result",
        "native_swap_chain_desc_query_result",
        "native_fullscreen_state_query_result",
        "native_vblank_virtualization_result",
        "native_raster_open_result",
        "native_raster_before_query_result",
        "native_raster_after_query_result",
        "submission_id_query_result",
        "frame_stats_query_result",
        "gpu_ready_signal_result",
        "gpu_ready_set_event_result",
    };
    static const QSet<QByteArray> booleanColumns {
        "rtp_valid",
        "queue_accepted",
        "queue_discontinuity",
        "decision_valid",
        "can_latch_present",
        "additional_queued_frame",
        "session_allow_tearing",
        "render_scheduler_delay_valid",
        "render_deadline_already_elapsed",
        "render_wait_coarse_clock_stalled",
        "render_wait_active_entered",
        "render_wait_active_clock_stalled",
        "render_wait_active_yield_limit_reached",
        "target_scheduler_delay_valid",
        "target_deadline_already_elapsed",
        "target_wait_coarse_clock_stalled",
        "target_wait_active_entered",
        "target_wait_active_clock_stalled",
        "target_wait_active_yield_limit_reached",
        "presenter_submission_time_valid",
        "presenter_submission_time_used",
        "spacing_corrected",
        "had_prior_submission",
        "tear_risk",
        "dropped",
        "presented",
        "cancelled",
        "submission_id_valid",
        "latch_valid",
        "latched_present",
        "used_rtp_timestamp",
        "cadence_eligible",
        "source_rate_changed",
        "phase_discontinuity",
        "rebased",
        "external_rebase_applied",
        "deep_trace",
        "native_present_timing_valid",
        "present_count_before_valid",
        "frame_stats_before_valid",
        "gpu_ready_attempted",
        "gpu_ready_signal_result_valid",
        "gpu_ready_set_event_result_valid",
        "gpu_ready_wait_result_valid",
        "gpu_ready_timing_valid",
        "gpu_ready_completed_before_wait",
        "native_backend_valid",
        "native_present_result_valid",
        "native_present_parameters_valid",
        "native_vrr_state_valid",
        "native_tearing_supported",
        "native_borderless_flip_model",
        "native_same_gpu_output",
        "native_render_adapter_luid_valid",
        "native_swap_chain_allows_tearing",
        "native_tearing_feature_query_result_valid",
        "native_tearing_feature_allows_tearing",
        "native_swap_chain_desc_query_result_valid",
        "native_fullscreen_state_query_result_valid",
        "native_fullscreen_exclusive",
        "native_present_ready_available",
        "native_foreground_window",
        "native_vblank_virtualization_probe_complete",
        "native_vblank_virtualization_call_available",
        "native_vblank_virtualization_result_valid",
        "native_vblank_virtualization_disabled",
        "native_display_config_query_result_valid",
        "native_display_path_valid",
        "native_display_target_available",
        "native_display_signal_valid",
        "native_raster_sampling_requested",
        "native_raster_open_result_valid",
        "native_raster_source_valid",
        "native_raster_before_query_result_valid",
        "native_raster_before_in_vertical_blank",
        "native_raster_after_query_result_valid",
        "native_raster_after_in_vertical_blank",
        "submission_id_query_result_valid",
        "frame_stats_query_result_valid",
        "latch_raw_sync_qpc_valid",
        "latch_qpc_correlation_valid",
        "readiness_model_valid",
    };
    static const QSet<QByteArray> dispositions {
        "presented",
        "output_dropped",
        "queue_capacity",
        "arrival_rejected",
        "suspension_discard",
        "shutdown_discard",
        "interrupted",
        "stale",
        "preparation_failed",
    };
    static const QSet<QByteArray> tearClassifications {
        "not_presented",
        "confirmed_safe_latched",
        "first_submission_unknown",
        "adaptive_interval_violation",
        "adaptive_interval_safe",
    };

    for (int i = 0; i < header.size(); ++i) {
        const QByteArray& name = header[i];
        const QByteArray& value = fields[i];
        bool valid = false;
        if (name == "playout_initial_profile") {
            valid = value.size() <= 16384 && !value.isEmpty() &&
                std::all_of(value.cbegin(), value.cend(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
                });
        }
        else if (name == "disposition") {
            valid = dispositions.contains(value);
        }
        else if (name == "tear_classification") {
            valid = tearClassifications.contains(value);
        }
        else if (name == "source_rate_hz") {
            const int decimal = value.indexOf('.');
            valid = decimal > 0 && value.size() - decimal == 4 &&
                std::all_of(
                    value.cbegin(), value.cbegin() + decimal,
                    [](char digit) { return digit >= '0' && digit <= '9'; }) &&
                std::all_of(
                    value.cbegin() + decimal + 1, value.cend(),
                    [](char digit) { return digit >= '0' && digit <= '9'; });
        }
        else if (booleanColumns.contains(name)) {
            valid = value == "0" || value == "1";
        }
        else if (signedColumns.contains(name)) {
            bool ok = false;
            const qlonglong parsed = value.toLongLong(&ok);
            valid = ok && QByteArray::number(parsed) == value;
        }
        else {
            bool ok = false;
            const qulonglong parsed = value.toULongLong(&ok);
            valid = ok && QByteArray::number(parsed) == value;
        }
        if (!valid) {
            error = "invalid trace value for " +
                QString::fromLatin1(name) + ": " +
                QString::fromLatin1(value);
            return false;
        }
    }
    return true;
}

struct Columns {
    int traceSchema = -1;
    int arrivalSequence = -1;
    int frame = -1;
    int rtpTimestamp = -1;
    int rtpValid = -1;
    int decodeCompleteUs = -1;
    int decoderOutputUs = -1;
    int decodeSyncWaitUs = -1;
    int pacerArrivalUs = -1;
    int queueDepthBefore = -1;
    int queueDepthAfter = -1;
    int queueAccepted = -1;
    int dequeueUs = -1;
    int queueDiscontinuity = -1;
    int decisionValid = -1;
    int decisionUs = -1;
    int displayRefreshHz = -1;
    int streamRateHz = -1;
    int additionalQueuedFrame = -1;
    int sessionAllowTearing = -1;
    int displayPeriodUs = -1;
    int canLatch = -1;
    int sourceIntervalUs = -1;
    int sourceRateHz = -1;
    int sourceTimeUs = -1;
    int sourcePeriodUs = -1;
    int readyOffsetUs = -1;
    int readinessBudgetUs = -1;
    int timingBudgetUs = -1;
    int renderLeadUs = -1;
    int gpuReadinessLeadUs = -1;
    int renderWakeLeadUs = -1;
    int targetWakeLeadUs = -1;
    int guardUs = -1;
    int headroomUs = -1;
    int renderStartUs = -1;
    int preparationStartUs = -1;
    int preparationEndUs = -1;
    int preparationUs = -1;
    int renderWaitOvershootUs = -1;
    int renderSchedulerDelayUs = -1;
    int renderSchedulerDelayValid = -1;
    int renderDeadlineAlreadyElapsed = -1;
    int renderWaitInitialUs = -1;
    int renderWaitActiveBudgetUs = -1;
    int renderWaitCoarseSleepCount = -1;
    int renderWaitCoarseRequestedTotalUs = -1;
    int renderWaitCoarseRequestedWakeUs = -1;
    int renderWaitCoarseReturnUs = -1;
    int renderWaitCoarseClockStalled = -1;
    int renderWaitActiveEntered = -1;
    int renderWaitActiveStartUs = -1;
    int renderWaitActiveLimitUs = -1;
    int renderWaitActiveYieldCount = -1;
    int renderWaitActiveClockStalled = -1;
    int renderWaitActiveYieldLimitReached = -1;
    int targetSchedulerDelayUs = -1;
    int targetSchedulerDelayValid = -1;
    int targetWaitOvershootUs = -1;
    int targetDeadlineAlreadyElapsed = -1;
    int targetWaitInitialUs = -1;
    int targetWaitActiveBudgetUs = -1;
    int targetWaitCoarseSleepCount = -1;
    int targetWaitCoarseRequestedTotalUs = -1;
    int targetWaitCoarseRequestedWakeUs = -1;
    int targetWaitCoarseReturnUs = -1;
    int targetWaitCoarseClockStalled = -1;
    int targetWaitActiveEntered = -1;
    int targetWaitActiveStartUs = -1;
    int targetWaitActiveLimitUs = -1;
    int targetWaitActiveYieldCount = -1;
    int targetWaitActiveClockStalled = -1;
    int targetWaitActiveYieldLimitReached = -1;
    int recordedTargetUs = -1;
    int presentStartUs = -1;
    int submissionBoundaryUs = -1;
    int presenterSubmissionTimeValid = -1;
    int presenterSubmissionTimeUs = -1;
    int presenterSubmissionTimeUsed = -1;
    int presentEndUs = -1;
    int presentCallUs = -1;
    int submitErrorUs = -1;
    int submissionSpacingUs = -1;
    int spacingMarginUs = -1;
    int spacingDeficitUs = -1;
    int spacingGuardFeedbackUs = -1;
    int spacingCorrected = -1;
    int hadPriorSubmission = -1;
    int completionQueueDepth = -1;
    int presented = -1;
    int cancelled = -1;
    int disposition = -1;
    int dropped = -1;
    int tearClassification = -1;
    int tearRisk = -1;
    int submissionIdValid = -1;
    int submissionId = -1;
    int latchValid = -1;
    int latchSubmissionId = -1;
    int latchTimeUs = -1;
    int latchPresentRefreshSequence = -1;
    int latchSyncRefreshSequence = -1;
    int latchedPresent = -1;
    int usedRtpTimestamp = -1;
    int cadenceEligible = -1;
    int sourceRateChanged = -1;
    int phaseDiscontinuity = -1;
    int rebased = -1;
    int externalRebaseApplied = -1;
    int externalRebaseFlags = -1;
    int midframeWindowStateFlags = -1;
    int deepTrace = -1;
    int nativePresentTimingValid = -1;
    int nativePresentStartUs = -1;
    int nativePresentEndUs = -1;
    int nativePresentCallUs = -1;
    int presentCountBeforeValid = -1;
    int presentCountBefore = -1;
    int frameStatsBeforeValid = -1;
    int frameStatsBeforePresentCount = -1;
    int frameStatsBeforeTimeUs = -1;
    int frameStatsBeforePresentRefreshSequence = -1;
    int frameStatsBeforeSyncRefreshSequence = -1;
    int gpuReadyAttempted = -1;
    int gpuReadySignalResultValid = -1;
    int gpuReadySignalResult = -1;
    int gpuReadySetEventResultValid = -1;
    int gpuReadySetEventResult = -1;
    int gpuReadyWaitResultValid = -1;
    int gpuReadyWaitResult = -1;
    int gpuReadyTimingValid = -1;
    int gpuReadySignalStartUs = -1;
    int gpuReadySignalEndUs = -1;
    int gpuReadyFlushStartUs = -1;
    int gpuReadyFlushEndUs = -1;
    int gpuReadySetEventStartUs = -1;
    int gpuReadySetEventEndUs = -1;
    int gpuReadyPollStartUs = -1;
    int gpuReadyPollEndUs = -1;
    int gpuReadyFenceValue = -1;
    int gpuReadyPollCompletedValue = -1;
    int gpuReadyCompletedBeforeWait = -1;
    int gpuReadyCompletionLowerBoundUs = -1;
    int gpuReadyCompletionUpperBoundUs = -1;
    int gpuReadyCompletionUncertaintyUs = -1;
    int gpuReadyWaitStartUs = -1;
    int gpuReadyTimeUs = -1;
    int gpuReadyWaitUs = -1;
    int decisionEndUs = -1;
    int controllerCallUs = -1;
    int staleCheckUs = -1;
    int staleAgeUs = -1;
    int renderWaitEntryUs = -1;
    int renderWaitFinalUs = -1;
    int targetWaitEntryUs = -1;
    int targetWaitFinalUs = -1;
    int spacingCheckUs = -1;
    int presentationFloorUs = -1;
    int spacingRecheckUs = -1;
    int spacingCorrectedFloorUs = -1;
    int correctionWaitStartUs = -1;
    int correctionWaitEndUs = -1;
    int terminalTimeUs = -1;
    int nativeBackendValid = -1;
    int nativeBackend = -1;
    int nativePresentResultValid = -1;
    int nativePresentResult = -1;
    int nativePresentParametersValid = -1;
    int nativePresentSyncInterval = -1;
    int nativePresentFlags = -1;
    int nativeVrrStateValid = -1;
    int nativeTearingSupported = -1;
    int nativeBorderlessFlipModel = -1;
    int nativeSameGpuOutput = -1;
    int nativeRenderAdapterLuidValid = -1;
    int nativeRenderAdapterLuid = -1;
    int nativeSwapChainAllowsTearing = -1;
    int nativeTearingFeatureQueryResultValid = -1;
    int nativeTearingFeatureQueryResult = -1;
    int nativeTearingFeatureAllowsTearing = -1;
    int nativeSwapChainDescQueryResultValid = -1;
    int nativeSwapChainDescQueryResult = -1;
    int nativeSwapChainFlags = -1;
    int nativeSwapChainSwapEffect = -1;
    int nativeFullscreenStateQueryResultValid = -1;
    int nativeFullscreenStateQueryResult = -1;
    int nativeFullscreenExclusive = -1;
    int nativeWindowFlags = -1;
    int nativePresentReadyAvailable = -1;
    int nativeForegroundWindow = -1;
    int nativeVrrFallbackReason = -1;
    int nativeDesktopMonitorCount = -1;
    int nativeVblankVirtualizationProbeComplete = -1;
    int nativeVblankVirtualizationCallAvailable = -1;
    int nativeVblankVirtualizationResultValid = -1;
    int nativeVblankVirtualizationResult = -1;
    int nativeVblankVirtualizationDisabled = -1;
    int nativeDisplayConfigQueryResultValid = -1;
    int nativeDisplayConfigQueryResult = -1;
    int nativeDisplayPathValid = -1;
    int nativeDisplayPathFlags = -1;
    int nativeDisplayTargetAvailable = -1;
    int nativeDisplaySourceAdapterLuid = -1;
    int nativeDisplaySourceId = -1;
    int nativeDisplayTargetAdapterLuid = -1;
    int nativeDisplayTargetId = -1;
    int nativeDisplayOutputTechnology = -1;
    int nativeDisplayRotation = -1;
    int nativeDisplayScaling = -1;
    int nativeDisplayPathRefreshNumerator = -1;
    int nativeDisplayPathRefreshDenominator = -1;
    int nativeDisplaySignalValid = -1;
    int nativeDisplaySignalPixelRateHz = -1;
    int nativeDisplaySignalHSyncNumerator = -1;
    int nativeDisplaySignalHSyncDenominator = -1;
    int nativeDisplaySignalVSyncNumerator = -1;
    int nativeDisplaySignalVSyncDenominator = -1;
    int nativeDisplaySignalActiveWidth = -1;
    int nativeDisplaySignalActiveHeight = -1;
    int nativeDisplaySignalTotalWidth = -1;
    int nativeDisplaySignalTotalHeight = -1;
    int nativeDisplaySignalAdditionalInfoRaw = -1;
    int nativeDisplaySignalScanLineOrdering = -1;
    int nativeRasterSamplingRequested = -1;
    int nativeRasterOpenResultValid = -1;
    int nativeRasterOpenResult = -1;
    int nativeRasterSourceValid = -1;
    int nativeRasterVidPnSourceId = -1;
    int nativeRasterBeforeQueryResultValid = -1;
    int nativeRasterBeforeQueryResult = -1;
    int nativeRasterBeforeQueryStartUs = -1;
    int nativeRasterBeforeQueryEndUs = -1;
    int nativeRasterBeforeInVerticalBlank = -1;
    int nativeRasterBeforeScanLine = -1;
    int nativeRasterAfterQueryResultValid = -1;
    int nativeRasterAfterQueryResult = -1;
    int nativeRasterAfterQueryStartUs = -1;
    int nativeRasterAfterQueryEndUs = -1;
    int nativeRasterAfterInVerticalBlank = -1;
    int nativeRasterAfterScanLine = -1;
    int submissionIdQueryResultValid = -1;
    int submissionIdQueryResult = -1;
    int submissionIdQueryStartUs = -1;
    int submissionIdQueryEndUs = -1;
    int frameStatsQueryResultValid = -1;
    int frameStatsQueryResult = -1;
    int frameStatsQueryStartUs = -1;
    int frameStatsQueryEndUs = -1;
    int latchRawSyncQpcValid = -1;
    int latchRawSyncQpcTicks = -1;
    int latchRawSyncQpcFrequency = -1;
    int latchQpcCorrelationValid = -1;
    int latchQpcCorrelationReferenceTicks = -1;
    int latchQpcCorrelationReferenceTimeUs = -1;
    int latchQpcCorrelationSpanTicks = -1;
    int readinessPhaseUs = -1;
    int readinessDemandUs = -1;
    int appliedReadinessReserveUs = -1;
    int renderBaselineUs = -1;
    int renderInsuranceUs = -1;
    int gpuReadinessAppliedUs = -1;
    int pacingLatencyBudgetUs = -1;
    int cadenceSampleCount = -1;
    int rateCandidateSampleCount = -1;
    int readinessSampleCount = -1;
    int preparationSampleCount = -1;
    int renderSchedulerSampleCount = -1;
    int targetSchedulerSampleCount = -1;
    int cleanSpacingFrames = -1;
    int phaseErrorFrames = -1;
    int readinessModelValid = -1;
    int playoutDelayUs = -1;
    QMap<QString, int> capturedParameterColumns;

    bool resolve(const QList<QByteArray>& header, QString& error)
    {
        QSet<QByteArray> uniqueColumns;
        for (const QByteArray& name : header) {
            if (name.isEmpty() || uniqueColumns.contains(name)) {
                error = name.isEmpty() ?
                    "trace header contains an empty column name" :
                    "trace header contains a duplicate column: " +
                        QString::fromLatin1(name);
                return false;
            }
            uniqueColumns.insert(name);
        }
        const auto find = [&header](const char* name) {
            return header.indexOf(name);
        };
        traceSchema = find("trace_schema");
        arrivalSequence = find("arrival_sequence");
        frame = find("frame");
        rtpTimestamp = find("rtp_timestamp");
        rtpValid = find("rtp_valid");
        decodeCompleteUs = find("decode_complete_us");
        decoderOutputUs = find("decoder_output_us");
        decodeSyncWaitUs = find("decode_sync_wait_us");
        pacerArrivalUs = find("pacer_arrival_us");
        queueDepthBefore = find("arrival_queue_depth_before");
        queueDepthAfter = find("arrival_queue_depth_after");
        queueAccepted = find("queue_accepted");
        dequeueUs = find("dequeue_us");
        queueDiscontinuity = find("queue_discontinuity");
        decisionValid = find("decision_valid");
        decisionUs = find("decision_us");
        displayRefreshHz = find("display_refresh_hz");
        streamRateHz = find("stream_rate_hz");
        additionalQueuedFrame = find("additional_queued_frame");
        sessionAllowTearing = find("session_allow_tearing");
        displayPeriodUs = find("display_period_us");
        canLatch = find("can_latch_present");
        sourceIntervalUs = find("sender_interval_us");
        sourceRateHz = find("source_rate_hz");
        sourceTimeUs = find("source_time_us");
        sourcePeriodUs = find("source_period_us");
        readyOffsetUs = find("ready_offset_us");
        readinessBudgetUs = find("readiness_budget_us");
        timingBudgetUs = find("timing_budget_us");
        renderLeadUs = find("render_lead_us");
        gpuReadinessLeadUs = find("gpu_readiness_lead_us");
        renderWakeLeadUs = find("render_wake_lead_us");
        targetWakeLeadUs = find("target_wake_lead_us");
        guardUs = find("guard_us");
        headroomUs = find("headroom_us");
        renderStartUs = find("render_start_us");
        preparationStartUs = find("prepare_start_us");
        preparationEndUs = find("prepare_end_us");
        preparationUs = find("prepare_us");
        renderWaitOvershootUs = find("render_wait_overshoot_us");
        renderSchedulerDelayUs = find("render_scheduler_delay_us");
        renderSchedulerDelayValid =
            find("render_scheduler_delay_valid");
        renderDeadlineAlreadyElapsed =
            find("render_deadline_already_elapsed");
        renderWaitInitialUs = find("render_wait_initial_us");
        renderWaitActiveBudgetUs =
            find("render_wait_active_budget_us");
        renderWaitCoarseSleepCount =
            find("render_wait_coarse_sleep_count");
        renderWaitCoarseRequestedTotalUs =
            find("render_wait_coarse_requested_total_us");
        renderWaitCoarseRequestedWakeUs =
            find("render_wait_coarse_requested_wake_us");
        renderWaitCoarseReturnUs =
            find("render_wait_coarse_return_us");
        renderWaitCoarseClockStalled =
            find("render_wait_coarse_clock_stalled");
        renderWaitActiveEntered =
            find("render_wait_active_entered");
        renderWaitActiveStartUs =
            find("render_wait_active_start_us");
        renderWaitActiveLimitUs =
            find("render_wait_active_limit_us");
        renderWaitActiveYieldCount =
            find("render_wait_active_yield_count");
        renderWaitActiveClockStalled =
            find("render_wait_active_clock_stalled");
        renderWaitActiveYieldLimitReached =
            find("render_wait_active_yield_limit_reached");
        targetWaitOvershootUs = find("target_wait_overshoot_us");
        targetSchedulerDelayUs = find("target_scheduler_delay_us");
        targetSchedulerDelayValid = find("target_scheduler_delay_valid");
        targetDeadlineAlreadyElapsed =
            find("target_deadline_already_elapsed");
        targetWaitInitialUs = find("target_wait_initial_us");
        targetWaitActiveBudgetUs =
            find("target_wait_active_budget_us");
        targetWaitCoarseSleepCount =
            find("target_wait_coarse_sleep_count");
        targetWaitCoarseRequestedTotalUs =
            find("target_wait_coarse_requested_total_us");
        targetWaitCoarseRequestedWakeUs =
            find("target_wait_coarse_requested_wake_us");
        targetWaitCoarseReturnUs =
            find("target_wait_coarse_return_us");
        targetWaitCoarseClockStalled =
            find("target_wait_coarse_clock_stalled");
        targetWaitActiveEntered =
            find("target_wait_active_entered");
        targetWaitActiveStartUs =
            find("target_wait_active_start_us");
        targetWaitActiveLimitUs =
            find("target_wait_active_limit_us");
        targetWaitActiveYieldCount =
            find("target_wait_active_yield_count");
        targetWaitActiveClockStalled =
            find("target_wait_active_clock_stalled");
        targetWaitActiveYieldLimitReached =
            find("target_wait_active_yield_limit_reached");
        recordedTargetUs = find("target_us");
        presentStartUs = find("present_start_us");
        submissionBoundaryUs = find("submission_boundary_us");
        presenterSubmissionTimeValid =
            find("presenter_submission_time_valid");
        presenterSubmissionTimeUs =
            find("presenter_submission_time_us");
        presenterSubmissionTimeUsed =
            find("presenter_submission_time_used");
        presentEndUs = find("present_end_us");
        presentCallUs = find("present_call_us");
        submitErrorUs = find("submit_error_us");
        submissionSpacingUs = find("submission_spacing_us");
        spacingMarginUs = find("spacing_margin_us");
        spacingDeficitUs = find("spacing_deficit_us");
        spacingGuardFeedbackUs = find("spacing_guard_feedback_us");
        spacingCorrected = find("spacing_corrected");
        hadPriorSubmission = find("had_prior_submission");
        completionQueueDepth = find("completion_queue_depth");
        presented = find("presented");
        cancelled = find("cancelled");
        disposition = find("disposition");
        dropped = find("dropped");
        tearClassification = find("tear_classification");
        tearRisk = find("tear_risk");
        submissionIdValid = find("submission_id_valid");
        submissionId = find("submission_id");
        latchValid = find("latch_valid");
        latchSubmissionId = find("latch_submission_id");
        latchTimeUs = find("latch_time_us");
        latchPresentRefreshSequence = find("latch_present_refresh_seq");
        latchSyncRefreshSequence = find("latch_sync_refresh_seq");
        latchedPresent = find("latched_present");
        usedRtpTimestamp = find("used_rtp_timestamp");
        cadenceEligible = find("cadence_eligible");
        sourceRateChanged = find("source_rate_changed");
        phaseDiscontinuity = find("phase_discontinuity");
        rebased = find("rebased");
        externalRebaseApplied = find("external_rebase_applied");
        externalRebaseFlags = find("external_rebase_flags");
        midframeWindowStateFlags =
            find("midframe_window_state_flags");
        deepTrace = find("deep_trace");
        nativePresentTimingValid = find("native_present_timing_valid");
        nativePresentStartUs = find("native_present_start_us");
        nativePresentEndUs = find("native_present_end_us");
        nativePresentCallUs = find("native_present_call_us");
        presentCountBeforeValid = find("present_count_before_valid");
        presentCountBefore = find("present_count_before");
        frameStatsBeforeValid = find("frame_stats_before_valid");
        frameStatsBeforePresentCount =
            find("frame_stats_before_present_count");
        frameStatsBeforeTimeUs = find("frame_stats_before_time_us");
        frameStatsBeforePresentRefreshSequence =
            find("frame_stats_before_present_refresh_seq");
        frameStatsBeforeSyncRefreshSequence =
            find("frame_stats_before_sync_refresh_seq");
        gpuReadyAttempted = find("gpu_ready_attempted");
        gpuReadySignalResultValid =
            find("gpu_ready_signal_result_valid");
        gpuReadySignalResult = find("gpu_ready_signal_result");
        gpuReadySetEventResultValid =
            find("gpu_ready_set_event_result_valid");
        gpuReadySetEventResult = find("gpu_ready_set_event_result");
        gpuReadyWaitResultValid =
            find("gpu_ready_wait_result_valid");
        gpuReadyWaitResult = find("gpu_ready_wait_result");
        gpuReadyTimingValid = find("gpu_ready_timing_valid");
        gpuReadySignalStartUs = find("gpu_ready_signal_start_us");
        gpuReadySignalEndUs = find("gpu_ready_signal_end_us");
        gpuReadyFlushStartUs = find("gpu_ready_flush_start_us");
        gpuReadyFlushEndUs = find("gpu_ready_flush_end_us");
        gpuReadySetEventStartUs =
            find("gpu_ready_set_event_start_us");
        gpuReadySetEventEndUs =
            find("gpu_ready_set_event_end_us");
        gpuReadyPollStartUs = find("gpu_ready_poll_start_us");
        gpuReadyPollEndUs = find("gpu_ready_poll_end_us");
        gpuReadyFenceValue = find("gpu_ready_fence_value");
        gpuReadyPollCompletedValue =
            find("gpu_ready_poll_completed_value");
        gpuReadyCompletedBeforeWait =
            find("gpu_ready_completed_before_wait");
        gpuReadyCompletionLowerBoundUs =
            find("gpu_ready_completion_lower_bound_us");
        gpuReadyCompletionUpperBoundUs =
            find("gpu_ready_completion_upper_bound_us");
        gpuReadyCompletionUncertaintyUs =
            find("gpu_ready_completion_uncertainty_us");
        gpuReadyWaitStartUs = find("gpu_ready_wait_start_us");
        gpuReadyTimeUs = find("gpu_ready_time_us");
        gpuReadyWaitUs = find("gpu_ready_wait_us");
        decisionEndUs = find("decision_end_us");
        controllerCallUs = find("controller_call_us");
        staleCheckUs = find("stale_check_us");
        staleAgeUs = find("stale_age_us");
        renderWaitEntryUs = find("render_wait_entry_us");
        renderWaitFinalUs = find("render_wait_final_us");
        targetWaitEntryUs = find("target_wait_entry_us");
        targetWaitFinalUs = find("target_wait_final_us");
        spacingCheckUs = find("spacing_check_us");
        presentationFloorUs = find("presentation_floor_us");
        spacingRecheckUs = find("spacing_recheck_us");
        spacingCorrectedFloorUs =
            find("spacing_corrected_floor_us");
        correctionWaitStartUs = find("correction_wait_start_us");
        correctionWaitEndUs = find("correction_wait_end_us");
        terminalTimeUs = find("terminal_time_us");
        nativeBackendValid = find("native_backend_valid");
        nativeBackend = find("native_backend");
        nativePresentResultValid =
            find("native_present_result_valid");
        nativePresentResult = find("native_present_result");
        nativePresentParametersValid =
            find("native_present_parameters_valid");
        nativePresentSyncInterval =
            find("native_present_sync_interval");
        nativePresentFlags = find("native_present_flags");
        nativeVrrStateValid = find("native_vrr_state_valid");
        nativeTearingSupported =
            find("native_tearing_supported");
        nativeBorderlessFlipModel =
            find("native_borderless_flip_model");
        nativeSameGpuOutput = find("native_same_gpu_output");
        nativeRenderAdapterLuidValid =
            find("native_render_adapter_luid_valid");
        nativeRenderAdapterLuid =
            find("native_render_adapter_luid");
        nativeSwapChainAllowsTearing =
            find("native_swap_chain_allows_tearing");
        nativeTearingFeatureQueryResultValid =
            find("native_tearing_feature_query_result_valid");
        nativeTearingFeatureQueryResult =
            find("native_tearing_feature_query_result");
        nativeTearingFeatureAllowsTearing =
            find("native_tearing_feature_allows_tearing");
        nativeSwapChainDescQueryResultValid =
            find("native_swap_chain_desc_query_result_valid");
        nativeSwapChainDescQueryResult =
            find("native_swap_chain_desc_query_result");
        nativeSwapChainFlags =
            find("native_swap_chain_flags");
        nativeSwapChainSwapEffect =
            find("native_swap_chain_swap_effect");
        nativeFullscreenStateQueryResultValid =
            find("native_fullscreen_state_query_result_valid");
        nativeFullscreenStateQueryResult =
            find("native_fullscreen_state_query_result");
        nativeFullscreenExclusive =
            find("native_fullscreen_exclusive");
        nativeWindowFlags = find("native_window_flags");
        nativePresentReadyAvailable =
            find("native_present_ready_available");
        nativeForegroundWindow = find("native_foreground_window");
        nativeVrrFallbackReason =
            find("native_vrr_fallback_reason");
        nativeDesktopMonitorCount =
            find("native_desktop_monitor_count");
        nativeVblankVirtualizationProbeComplete =
            find("native_vblank_virtualization_probe_complete");
        nativeVblankVirtualizationCallAvailable =
            find("native_vblank_virtualization_call_available");
        nativeVblankVirtualizationResultValid =
            find("native_vblank_virtualization_result_valid");
        nativeVblankVirtualizationResult =
            find("native_vblank_virtualization_result");
        nativeVblankVirtualizationDisabled =
            find("native_vblank_virtualization_disabled");
        nativeDisplayConfigQueryResultValid =
            find("native_display_config_query_result_valid");
        nativeDisplayConfigQueryResult =
            find("native_display_config_query_result");
        nativeDisplayPathValid =
            find("native_display_path_valid");
        nativeDisplayPathFlags =
            find("native_display_path_flags");
        nativeDisplayTargetAvailable =
            find("native_display_target_available");
        nativeDisplaySourceAdapterLuid =
            find("native_display_source_adapter_luid");
        nativeDisplaySourceId =
            find("native_display_source_id");
        nativeDisplayTargetAdapterLuid =
            find("native_display_target_adapter_luid");
        nativeDisplayTargetId =
            find("native_display_target_id");
        nativeDisplayOutputTechnology =
            find("native_display_output_technology");
        nativeDisplayRotation =
            find("native_display_rotation");
        nativeDisplayScaling =
            find("native_display_scaling");
        nativeDisplayPathRefreshNumerator =
            find("native_display_path_refresh_numerator");
        nativeDisplayPathRefreshDenominator =
            find("native_display_path_refresh_denominator");
        nativeDisplaySignalValid =
            find("native_display_signal_valid");
        nativeDisplaySignalPixelRateHz =
            find("native_display_signal_pixel_rate_hz");
        nativeDisplaySignalHSyncNumerator =
            find("native_display_signal_hsync_numerator");
        nativeDisplaySignalHSyncDenominator =
            find("native_display_signal_hsync_denominator");
        nativeDisplaySignalVSyncNumerator =
            find("native_display_signal_vsync_numerator");
        nativeDisplaySignalVSyncDenominator =
            find("native_display_signal_vsync_denominator");
        nativeDisplaySignalActiveWidth =
            find("native_display_signal_active_width");
        nativeDisplaySignalActiveHeight =
            find("native_display_signal_active_height");
        nativeDisplaySignalTotalWidth =
            find("native_display_signal_total_width");
        nativeDisplaySignalTotalHeight =
            find("native_display_signal_total_height");
        nativeDisplaySignalAdditionalInfoRaw =
            find("native_display_signal_additional_info_raw");
        nativeDisplaySignalScanLineOrdering =
            find("native_display_signal_scanline_ordering");
        nativeRasterSamplingRequested =
            find("native_raster_sampling_requested");
        nativeRasterOpenResultValid =
            find("native_raster_open_result_valid");
        nativeRasterOpenResult =
            find("native_raster_open_result");
        nativeRasterSourceValid =
            find("native_raster_source_valid");
        nativeRasterVidPnSourceId =
            find("native_raster_vidpn_source_id");
        nativeRasterBeforeQueryResultValid =
            find("native_raster_before_query_result_valid");
        nativeRasterBeforeQueryResult =
            find("native_raster_before_query_result");
        nativeRasterBeforeQueryStartUs =
            find("native_raster_before_query_start_us");
        nativeRasterBeforeQueryEndUs =
            find("native_raster_before_query_end_us");
        nativeRasterBeforeInVerticalBlank =
            find("native_raster_before_in_vertical_blank");
        nativeRasterBeforeScanLine =
            find("native_raster_before_scanline");
        nativeRasterAfterQueryResultValid =
            find("native_raster_after_query_result_valid");
        nativeRasterAfterQueryResult =
            find("native_raster_after_query_result");
        nativeRasterAfterQueryStartUs =
            find("native_raster_after_query_start_us");
        nativeRasterAfterQueryEndUs =
            find("native_raster_after_query_end_us");
        nativeRasterAfterInVerticalBlank =
            find("native_raster_after_in_vertical_blank");
        nativeRasterAfterScanLine =
            find("native_raster_after_scanline");
        submissionIdQueryResultValid =
            find("submission_id_query_result_valid");
        submissionIdQueryResult =
            find("submission_id_query_result");
        submissionIdQueryStartUs =
            find("submission_id_query_start_us");
        submissionIdQueryEndUs =
            find("submission_id_query_end_us");
        frameStatsQueryResultValid =
            find("frame_stats_query_result_valid");
        frameStatsQueryResult = find("frame_stats_query_result");
        frameStatsQueryStartUs =
            find("frame_stats_query_start_us");
        frameStatsQueryEndUs =
            find("frame_stats_query_end_us");
        latchRawSyncQpcValid =
            find("latch_raw_sync_qpc_valid");
        latchRawSyncQpcTicks = find("latch_raw_sync_qpc_ticks");
        latchRawSyncQpcFrequency =
            find("latch_raw_sync_qpc_frequency_hz");
        latchQpcCorrelationValid =
            find("latch_qpc_correlation_valid");
        latchQpcCorrelationReferenceTicks =
            find("latch_qpc_correlation_reference_ticks");
        latchQpcCorrelationReferenceTimeUs =
            find("latch_qpc_correlation_reference_time_us");
        latchQpcCorrelationSpanTicks =
            find("latch_qpc_correlation_span_ticks");
        readinessPhaseUs = find("readiness_phase_us");
        readinessDemandUs = find("readiness_demand_us");
        appliedReadinessReserveUs = find("applied_readiness_reserve_us");
        renderBaselineUs = find("render_baseline_us");
        renderInsuranceUs = find("render_insurance_us");
        gpuReadinessAppliedUs = find("gpu_readiness_applied_us");
        pacingLatencyBudgetUs = find("pacing_latency_budget_us");
        cadenceSampleCount = find("cadence_sample_count");
        rateCandidateSampleCount = find("rate_candidate_sample_count");
        readinessSampleCount = find("readiness_sample_count");
        preparationSampleCount = find("preparation_sample_count");
        renderSchedulerSampleCount = find("render_scheduler_sample_count");
        targetSchedulerSampleCount = find("target_scheduler_sample_count");
        cleanSpacingFrames = find("clean_spacing_frames");
        phaseErrorFrames = find("phase_error_frames");
        readinessModelValid = find("readiness_model_valid");
        playoutDelayUs = find("playout_delay_us");
        for (const QString& path : vrrReplayParameterNames()) {
            if (!path.startsWith("controller.")) continue;
            const QString key = path.mid(QString("controller.").size());
            const int column = header.indexOf(
                ("param_" + key).toLatin1());
            if (column >= 0) capturedParameterColumns.insert(path, column);
        }

        const int required[] = {
            traceSchema, arrivalSequence, frame, rtpTimestamp, rtpValid,
            decodeCompleteUs, pacerArrivalUs, queueDepthBefore,
            queueAccepted, dequeueUs, decisionValid,
            decisionUs, displayRefreshHz, streamRateHz, canLatch,
            displayPeriodUs,
            sourceIntervalUs, sourceRateHz, sourceTimeUs, sourcePeriodUs,
            readyOffsetUs,
            readinessBudgetUs, timingBudgetUs,
            renderLeadUs, renderWakeLeadUs, targetWakeLeadUs, guardUs,
            headroomUs, renderStartUs, preparationStartUs,
            preparationEndUs, preparationUs, renderWaitOvershootUs,
            targetSchedulerDelayUs, targetSchedulerDelayValid,
            recordedTargetUs, submissionBoundaryUs, presentCallUs,
            submitErrorUs, submissionSpacingUs, hadPriorSubmission,
            presented, cancelled,
            disposition, dropped, tearClassification, tearRisk,
            latchValid, latchSubmissionId,
            latchPresentRefreshSequence, latchedPresent, usedRtpTimestamp,
            cadenceEligible, sourceRateChanged, phaseDiscontinuity, rebased,
            nativePresentCallUs, gpuReadyWaitUs,
        };
        if (std::any_of(std::begin(required), std::end(required),
                        [](int column) { return column < 0; })) {
            error = "trace schema is missing exact-simulation fields (schema 3, 4, or 5 required)";
            return false;
        }
        m_Maximum = header.size() - 1;
        return true;
    }

    int maximum() const { return m_Maximum; }

private:
    int m_Maximum = -1;
};

// Schema default for a controller parameter path, as text, or empty when the
// path is not a controller parameter.
QString vrrControllerParameterDefaultText(const QString& path)
{
#define VRR_REPLAY_PARAMETER_DEFAULT(type, jsonName, memberName, defaultValue) \
    if (path == QStringLiteral("controller." #jsonName)) { \
        return QString::number(static_cast<qulonglong>(defaultValue)); \
    }
    VRR_TIMING_PARAMETER_FIELDS(VRR_REPLAY_PARAMETER_DEFAULT)
#undef VRR_REPLAY_PARAMETER_DEFAULT
    return QString();
}

struct Distribution {
    static constexpr uint64_t kExactLimitUs = 100000;
    static constexpr uint64_t kMediumLimitUs = 1000000;
    static constexpr uint64_t kLongLimitUs = 60000000;
    static constexpr uint64_t kMediumBucketUs = 100;
    static constexpr uint64_t kLongBucketUs = 1000;
    static constexpr size_t kExactBuckets = kExactLimitUs + 1;
    static constexpr size_t kMediumBuckets =
        (kMediumLimitUs - kExactLimitUs + kMediumBucketUs - 1) /
            kMediumBucketUs;
    static constexpr size_t kLongBuckets =
        (kLongLimitUs - kMediumLimitUs + kLongBucketUs - 1) /
            kLongBucketUs;
    static constexpr size_t kOverflowBucket =
        kExactBuckets + kMediumBuckets + kLongBuckets;
    static constexpr size_t kBucketCount = kOverflowBucket + 1;

    uint64_t count = 0;
    uint64_t minimum = 0;
    uint64_t maximum = 0;
    long double mean = 0;
    long double squaredDifferenceTotal = 0;
    std::vector<uint32_t> histogram;

    static size_t bucketFor(uint64_t value)
    {
        if (value <= kExactLimitUs) {
            return static_cast<size_t>(value);
        }
        if (value <= kMediumLimitUs) {
            return kExactBuckets + static_cast<size_t>(
                (value - kExactLimitUs - 1) / kMediumBucketUs);
        }
        if (value <= kLongLimitUs) {
            return kExactBuckets + kMediumBuckets + static_cast<size_t>(
                (value - kMediumLimitUs - 1) / kLongBucketUs);
        }
        return kOverflowBucket;
    }

    uint64_t bucketUpperBound(size_t bucket) const
    {
        if (bucket < kExactBuckets) {
            return static_cast<uint64_t>(bucket);
        }
        if (bucket < kExactBuckets + kMediumBuckets) {
            const uint64_t offset = static_cast<uint64_t>(
                bucket - kExactBuckets + 1);
            return std::min(kMediumLimitUs,
                            kExactLimitUs + offset * kMediumBucketUs);
        }
        if (bucket < kOverflowBucket) {
            const uint64_t offset = static_cast<uint64_t>(
                bucket - kExactBuckets - kMediumBuckets + 1);
            return std::min(kLongLimitUs,
                            kMediumLimitUs + offset * kLongBucketUs);
        }
        return maximum;
    }

    void add(uint64_t value)
    {
        if (count == 0) {
            minimum = value;
            maximum = value;
            histogram.resize(kBucketCount);
        }
        else {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        ++count;
        const long double delta = static_cast<long double>(value) - mean;
        mean += delta / static_cast<long double>(count);
        const long double adjustedDelta =
            static_cast<long double>(value) - mean;
        squaredDifferenceTotal += delta * adjustedDelta;
        ++histogram[bucketFor(value)];
    }

    void addElapsed(uint64_t endUs, uint64_t startUs)
    {
        if (startUs != 0 && endUs >= startUs) {
            add(endUs - startUs);
        }
    }

    uint64_t percentileRatio(uint64_t numerator, uint64_t denominator) const
    {
        if (count == 0 || denominator == 0) {
            return 0;
        }
        const uint64_t boundedNumerator = std::min(denominator, numerator);
        const uint64_t rank = std::max<uint64_t>(
            1,
            (count / denominator) * boundedNumerator +
                ((count % denominator) * boundedNumerator +
                 denominator - 1) /
                    denominator);
        uint64_t cumulative = 0;
        for (size_t bucket = 0; bucket < histogram.size(); ++bucket) {
            cumulative += histogram[bucket];
            if (cumulative >= rank) {
                return bucketUpperBound(bucket);
            }
        }
        return maximum;
    }

    uint64_t percentile(unsigned int percent) const
    {
        return percentileRatio(percent, 100);
    }
};

// Sender-spacing cadence. Compares each consecutive pair of presented
// submissions against the sender's own RTP spacing, which is the cadence the
// game produced. Pairs whose sender or arrival interval exceeds the stall
// threshold are excluded so host capture stalls do not dominate the tails. A
// hitch is a pair presented more than two milliseconds wider than the sender
// spacing; hitches are attributed to a frame that arrived later than the
// playout delay, a render-lead jump, the display-spacing floor, or other.
struct SenderCadenceTracker {
    static constexpr uint64_t kStallIntervalUs = 25000;
    static constexpr uint64_t kHitchUs = 2000;
    static constexpr uint64_t kRenderLeadJumpUs = 500;
    static constexpr uint64_t kFloorSlackUs = 300;

    bool havePrior = false;
    bool haveResidual = false;
    uint64_t priorSenderUs = 0;
    uint64_t priorDecodeUs = 0;
    uint64_t priorSubmissionUs = 0;
    int64_t priorResidualUs = 0;
    uint64_t pairs = 0;
    uint64_t hitches = 0;
    uint64_t spacingErrorsOverHitch = 0;
    uint64_t clientSpacingPairs = 0;
    uint64_t clientSpacingErrorsOver3ms = 0;
    uint64_t sourceStallPairs = 0;
    Distribution clientSpacingErrorUs;
    uint64_t hitchLateArrivals = 0;
    uint64_t hitchRenderLeadJumps = 0;
    uint64_t hitchDisplayFloor = 0;
    uint64_t hitchOther = 0;
    Distribution absoluteSpacingErrorUs;
    Distribution absoluteJerkUs;
    // Evenness of what was shown, independent of the sender: the change in
    // presented interval from one pair to the next. This is the quantity the
    // viewer perceives as stutter; matching a jittery sender scores well on
    // the fields above and badly here.
    bool havePresentedInterval = false;
    uint64_t priorPresentedIntervalUs = 0;
    uint64_t presentedJerkPairs = 0;
    uint64_t presentedJerkOverHitch = 0;
    Distribution presentedJerkUs;

    void observe(uint64_t senderUs, uint64_t decodeUs, uint64_t submissionUs,
                 bool lateArrival, bool renderLeadJump,
                 uint64_t displayPeriodUs)
    {
        if (havePrior && senderUs > priorSenderUs &&
                decodeUs >= priorDecodeUs &&
                submissionUs >= priorSubmissionUs) {
            const uint64_t senderIntervalUs = senderUs - priorSenderUs;
            const uint64_t arrivalIntervalUs = decodeUs - priorDecodeUs;
            if (senderIntervalUs <= kStallIntervalUs) {
                // The user's 3 ms client-error goal must include network and
                // decode stalls. Only a gap on the source clock is excluded;
                // a long local arrival gap cannot excuse a client hitch.
                const uint64_t actual = submissionUs - priorSubmissionUs;
                const uint64_t error = actual > senderIntervalUs ?
                    actual - senderIntervalUs : senderIntervalUs - actual;
                ++clientSpacingPairs;
                clientSpacingErrorsOver3ms += error > 3000;
                clientSpacingErrorUs.add(error);
            }
            else {
                ++sourceStallPairs;
            }
            if (senderIntervalUs <= kStallIntervalUs &&
                    arrivalIntervalUs <= kStallIntervalUs) {
                const uint64_t submissionIntervalUs =
                    submissionUs - priorSubmissionUs;
                if (havePresentedInterval) {
                    const uint64_t jerkUs =
                        submissionIntervalUs > priorPresentedIntervalUs ?
                            submissionIntervalUs - priorPresentedIntervalUs :
                            priorPresentedIntervalUs - submissionIntervalUs;
                    presentedJerkUs.add(jerkUs);
                    ++presentedJerkPairs;
                    if (jerkUs > kHitchUs) {
                        ++presentedJerkOverHitch;
                    }
                }
                havePresentedInterval = true;
                priorPresentedIntervalUs = submissionIntervalUs;
                const int64_t residualUs =
                    static_cast<int64_t>(submissionIntervalUs) -
                    static_cast<int64_t>(senderIntervalUs);
                ++pairs;
                absoluteSpacingErrorUs.add(static_cast<uint64_t>(
                    residualUs < 0 ? -residualUs : residualUs));
                spacingErrorsOverHitch += residualUs > static_cast<int64_t>(kHitchUs) ||
                    residualUs < -static_cast<int64_t>(kHitchUs);
                if (haveResidual) {
                    const int64_t jerkUs = residualUs - priorResidualUs;
                    absoluteJerkUs.add(static_cast<uint64_t>(
                        jerkUs < 0 ? -jerkUs : jerkUs));
                }
                haveResidual = true;
                priorResidualUs = residualUs;
                if (residualUs > static_cast<int64_t>(kHitchUs)) {
                    ++hitches;
                    if (lateArrival) {
                        ++hitchLateArrivals;
                    }
                    else if (renderLeadJump) {
                        ++hitchRenderLeadJumps;
                    }
                    else if (senderIntervalUs < displayPeriodUs &&
                             submissionIntervalUs <=
                                 displayPeriodUs + kFloorSlackUs) {
                        ++hitchDisplayFloor;
                    }
                    else {
                        ++hitchOther;
                    }
                }
            }
            else {
                haveResidual = false;
                havePresentedInterval = false;
            }
        }
        else {
            haveResidual = false;
            havePresentedInterval = false;
        }
        havePrior = true;
        priorSenderUs = senderUs;
        priorDecodeUs = decodeUs;
        priorSubmissionUs = submissionUs;
    }
};

// Rate-band and paired-delta diagnostics are supplemental. Keep their memory
// fixed even for multi-hour or unusually high-rate traces while retaining
// exact count/mean/stddev/min/max and a deterministic quantile sample.
constexpr size_t kMaximumDiagnosticSamples = 32768;
constexpr size_t kMaximumPendingSubmissionBands = 4096;
constexpr size_t kMaximumRasterSyncAnchors = 512;

struct BoundedDistribution {
    uint64_t count = 0;
    uint64_t minimum = 0;
    uint64_t maximum = 0;
    long double mean = 0;
    long double squaredDifferenceTotal = 0;
    std::vector<uint64_t> samples;

    static uint64_t sampleHash(uint64_t value)
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    void add(uint64_t value)
    {
        if (count == 0) {
            minimum = value;
            maximum = value;
            samples.reserve(kMaximumDiagnosticSamples);
        }
        else {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        ++count;
        const long double delta = static_cast<long double>(value) - mean;
        mean += delta / static_cast<long double>(count);
        const long double adjustedDelta =
            static_cast<long double>(value) - mean;
        squaredDifferenceTotal += delta * adjustedDelta;

        if (samples.size() < kMaximumDiagnosticSamples) {
            samples.push_back(value);
            return;
        }
        const uint64_t selected = sampleHash(count) % count;
        if (selected < kMaximumDiagnosticSamples) {
            samples[static_cast<size_t>(selected)] = value;
        }
    }

    void addElapsed(uint64_t endUs, uint64_t startUs)
    {
        if (startUs != 0 && endUs >= startUs) {
            add(endUs - startUs);
        }
    }
};

struct SignedAccumulator {
    uint64_t count = 0;
    int64_t minimum = 0;
    int64_t maximum = 0;
    long double mean = 0;
    long double squaredDifferenceTotal = 0;
    uint64_t negative = 0;
    uint64_t zero = 0;
    uint64_t positive = 0;

    void add(int64_t value)
    {
        if (count == 0) {
            minimum = value;
            maximum = value;
        }
        else {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        ++count;
        const long double delta = static_cast<long double>(value) - mean;
        mean += delta / static_cast<long double>(count);
        const long double adjustedDelta =
            static_cast<long double>(value) - mean;
        squaredDifferenceTotal += delta * adjustedDelta;
        negative += value < 0 ? 1 : 0;
        zero += value == 0 ? 1 : 0;
        positive += value > 0 ? 1 : 0;
    }
};

enum RateBandIndex : size_t {
    UnknownRate,
    Below40Fps,
    Fps40To49,
    Fps50To59,
    Fps60To69,
    Fps70To79,
    Fps80To89,
    Fps90To100,
    Fps101To109,
    Fps110To116,
    Above116Fps,
    Fps40To116,
    Fps60To100,
    RateBandCount,
};

constexpr std::array<const char*, RateBandCount> kRateBandNames {
    "unknown", "below_40", "40_49", "50_59", "60_69", "70_79",
    "80_89", "90_100", "101_109", "110_116", "above_116",
    "40_116", "60_100",
};

struct CadenceBandMetrics {
    uint64_t presentedFrames = 0;
    uint64_t cadenceTransitions = 0;
    uint64_t modelledIntervalViolations = 0;
    uint64_t scanoutAnomalies = 0;
    uint64_t repeatedRefreshes = 0;
    uint64_t rasterCertainActive = 0;
    uint64_t rasterPossibleActive = 0;
    uint64_t rasterInactive = 0;
    uint64_t rasterUnclassified = 0;
    uint64_t rasterLatched = 0;
    uint64_t exactRefreshBeforeActive = 0;
    uint64_t exactRefreshLatched = 0;
    uint64_t exactRefreshActive = 0;
    uint64_t exactRefreshBoundary = 0;
    uint64_t exactRefreshAfterActive = 0;
    uint64_t exactRefreshUnclassified = 0;
    BoundedDistribution decodeToSubmission;
    BoundedDistribution absoluteCadenceResidual;
    BoundedDistribution absoluteJerk;
    SignedAccumulator signedCadenceResidual;
};

struct NativeRasterScanLineBandMetrics {
    uint64_t comparableSamples = 0;
    uint64_t mismatches = 0;
    Distribution absoluteError;
    Distribution tolerance;
    SignedAccumulator residual;
};

constexpr uint64_t kJerkAnomalyThresholdUs = 4000;

struct AnomalyWindowMetrics {
    uint64_t anomalies = 0;
    uint64_t consecutive = 0;
    uint64_t longestConsecutive = 0;
    uint64_t worstOneSecond = 0;
    uint64_t worstTenSeconds = 0;
    uint64_t worstSixtySeconds = 0;
    std::deque<uint64_t> oneSecond;
    std::deque<uint64_t> tenSeconds;
    std::deque<uint64_t> sixtySeconds;

    static void appendWindow(std::deque<uint64_t>& values,
                             uint64_t nowUs, uint64_t durationUs,
                             uint64_t& maximum)
    {
        while (!values.empty() && nowUs >= values.front() &&
                nowUs - values.front() >= durationUs) {
            values.pop_front();
        }
        values.push_back(nowUs);
        maximum = std::max(maximum,
                           static_cast<uint64_t>(values.size()));
    }

    void observe(uint64_t timestampUs, bool anomalous)
    {
        if (!anomalous) {
            consecutive = 0;
            return;
        }
        ++anomalies;
        ++consecutive;
        longestConsecutive = std::max(longestConsecutive, consecutive);
        appendWindow(oneSecond, timestampUs, 1000000ULL, worstOneSecond);
        appendWindow(tenSeconds, timestampUs, 10000000ULL, worstTenSeconds);
        appendWindow(sixtySeconds, timestampUs, 60000000ULL,
                     worstSixtySeconds);
    }
};

struct RasterEnvelopeMetrics {
    uint64_t eligibleAdaptiveSubmissions = 0;
    uint64_t certainActive = 0;
    uint64_t possibleActive = 0;
    uint64_t inactiveInBothModels = 0;
    uint64_t unclassified = 0;
    uint64_t latchedSuppressed = 0;
    uint64_t activeScanoutClamped = 0;
    uint64_t scanoutPhaseWindowInvalid = 0;
    uint64_t vrrLockedActive = 0;
    uint64_t vrrLockedInactive = 0;
    uint64_t vrrLockedBoundary = 0;
    uint64_t freeRunningActive = 0;
    uint64_t freeRunningInactive = 0;
    uint64_t freeRunningBoundary = 0;
    BoundedDistribution anchorAge;
    BoundedDistribution freeRunningPhase;
    BoundedDistribution freeRunningPhasePs;
    BoundedDistribution resolvedScanoutPeriod;
    BoundedDistribution resolvedScanoutPeriodPs;
    BoundedDistribution resolvedActiveScanout;
    BoundedDistribution resolvedActiveScanoutPs;
    BoundedDistribution resolvedSyncToActiveScanout;
    BoundedDistribution resolvedAnchorMaxAge;
    BoundedDistribution vrrLockedProtectionWait;
    BoundedDistribution freeRunningProtectionWait;
    BoundedDistribution vrrLockedScanoutPositionPpm;
    BoundedDistribution freeRunningScanoutPositionPpm;
};

struct Metrics {
    Vrr13::ReadinessWindow observedReadiness;
    Vrr13::ReadinessWindow simulatedReadiness;
    uint64_t feedbackCapacityLimitedFrames = 0;
    VrrTimingDecision lastFeedbackDecision;

    uint64_t delivered = 0;
    uint64_t scheduled = 0;
    uint64_t presentedFrames = 0;
    uint64_t traceSchema = 0;
    uint64_t firstArrivalSequence = 0;
    uint64_t lastArrivalSequence = 0;
    uint64_t priorArrivalSequence = 0;
    // Worker-occupancy decision model state: the last recorded gap between a
    // submission and the next dequeue while the worker was busy, and the
    // smallest recorded arrival-to-decision latency while it was idle.
    uint64_t modeledBusyGapUs = 300;
    uint64_t modeledIdleLatencyUs = 0;
    bool modeledIdleLatencyValid = false;
    Distribution occupancyDecisionShiftUs;
    // Sender-spacing cadence for the recorded session, the candidate, and a
    // stock-style present-on-render emulation (decode + prepare + present).
    SenderCadenceTracker observedSenderCadence;
    SenderCadenceTracker simulatedSenderCadence;
    SenderCadenceTracker stockSenderCadence;
    bool senderClockValid = false;
    uint32_t senderPriorRtpTimestamp = 0;
    uint64_t senderUnwrappedTicks = 0;
    bool observedPriorRenderLeadValid = false;
    uint64_t observedPriorRenderLeadUs = 0;
    bool simulatedPriorRenderLeadValid = false;
    uint64_t simulatedPriorRenderLeadUs = 0;
    Distribution simulatedPlayoutDelayUs;
    Distribution observedPlayoutDelayUs;
    uint64_t arrivalSequenceGaps = 0;
    uint64_t arrivalSequenceDuplicates = 0;
    uint64_t arrivalSequenceOutOfOrderTransitions = 0;
    std::vector<uint64_t> arrivalSequences;
    TraceFooter traceFooter;
    uint64_t displayRefreshMismatchRows = 0;
    uint64_t streamRateMismatchRows = 0;
    uint64_t additionalQueuedFrameMismatchRows = 0;
    bool sessionAllowTearingTelemetryAvailable = false;
    uint64_t sessionAllowTearingMismatchRows = 0;
    uint64_t latchCapabilityMismatchRows = 0;
    uint64_t displayPeriodMismatchRows = 0;
    uint64_t controllerParameterMismatchRows = 0;
    uint64_t decodeToArrivalOrderViolations = 0;
    uint64_t decodeReadinessOrderViolations = 0;
    uint64_t arrivalToDequeueOrderViolations = 0;
    uint64_t dequeueToDecisionOrderViolations = 0;
    uint64_t controllerCallOrderViolations = 0;
    uint64_t controllerCallDurationMismatchRows = 0;
    uint64_t staleCheckOrderViolations = 0;
    uint64_t staleAgeMismatchRows = 0;
    uint64_t waitBoundaryOrderViolations = 0;
    uint64_t correctionWaitOrderViolations = 0;
    uint64_t terminalTimeOrderViolations = 0;
    uint64_t preparationOrderViolations = 0;
    uint64_t preparationDurationMismatchRows = 0;
    bool presentTimingIntegrityTelemetryAvailable = false;
    uint64_t presentOperationOrderViolations = 0;
    uint64_t presentOperationDurationMismatchRows = 0;
    uint64_t presentedPresentOperationIntegrityRows = 0;
    uint64_t nativePresentOrderViolations = 0;
    uint64_t nativePresentDurationMismatchRows = 0;
    bool presenterSubmissionTimingTelemetryAvailable = false;
    uint64_t presenterSubmissionTimingRelationshipMismatchRows = 0;
    uint64_t presenterSubmissionTimestampUsedRows = 0;
    bool gpuReadyNativeResultTelemetryAvailable = false;
    bool gpuReadyBoundsTelemetryAvailable = false;
    bool gpuReadyStageTimingTelemetryAvailable = false;
    uint64_t gpuReadyAttemptedRows = 0;
    uint64_t gpuReadyNativeResultRelationshipMismatchRows = 0;
    uint64_t gpuReadyStageTimingRelationshipMismatchRows = 0;
    uint64_t presentedGpuReadyNativeSuccessRows = 0;
    QMap<QByteArray, uint64_t> gpuReadySignalResults;
    QMap<QByteArray, uint64_t> gpuReadySetEventResults;
    QMap<QByteArray, uint64_t> gpuReadyWaitResults;
    uint64_t gpuReadyOrderViolations = 0;
    uint64_t gpuReadyDurationMismatchRows = 0;
    uint64_t gpuReadyFenceRelationshipMismatchRows = 0;
    uint64_t gpuReadyBoundsDerivationMismatchRows = 0;
    uint64_t submissionTimestampRegressions = 0;
    bool nativeOutcomeTelemetryAvailable = false;
    bool nativePresentContractTelemetryAvailable = false;
    bool nativeDxgiCapabilityTelemetryAvailable = false;
    bool nativeVblankVirtualizationTelemetryAvailable = false;
    bool nativeDisplayTimingTelemetryAvailable = false;
    bool nativeRasterTelemetryAvailable = false;
    bool qpcCorrelationTelemetryAvailable = false;
    uint64_t normalPresentAttemptRows = 0;
    uint64_t nativePresentAttemptRows = 0;
    uint64_t nativeDxgiPresentAttemptRows = 0;
    uint64_t nativeVulkanPresentAttemptRows = 0;
    uint64_t nativePresentResultValidRows = 0;
    uint64_t nativePresentParametersValidRows = 0;
    uint64_t nativeVrrStateValidRows = 0;
    uint64_t nativeTearingFeatureQueryResultValidRows = 0;
    uint64_t nativeTearingFeatureQuerySuccessRows = 0;
    uint64_t nativeTearingFeatureAllowsTearingRows = 0;
    uint64_t nativeSwapChainDescQueryResultValidRows = 0;
    uint64_t nativeSwapChainDescQuerySuccessRows = 0;
    uint64_t nativeSwapChainFlipModelRows = 0;
    uint64_t nativeSwapChainAllowsTearingRows = 0;
    uint64_t nativeFullscreenStateQueryResultValidRows = 0;
    uint64_t nativeFullscreenStateQuerySuccessRows = 0;
    uint64_t nativeWindowBorderlessRows = 0;
    uint64_t nativeWindowedSwapChainRows = 0;
    uint64_t nativeDxgiCapabilityExactEligibleRows = 0;
    uint64_t nativeDxgiCapabilityRelationshipMismatchRows = 0;
    uint64_t nativeDxgiCapabilitySnapshotMismatchRows = 0;
    uint64_t nativeWindowRawFlagDifferenceRows = 0;
    uint64_t nativeDxgiCapabilitySnapshotEpochs = 0;
    bool haveNativeDxgiCapabilityReference = false;
    int64_t nativeTearingFeatureQueryResult = 0;
    bool nativeTearingFeatureAllowsTearing = false;
    int64_t nativeSwapChainDescQueryResult = 0;
    uint64_t nativeSwapChainFlags = 0;
    uint64_t nativeSwapChainSwapEffect = 0;
    int64_t nativeFullscreenStateQueryResult = 0;
    bool nativeFullscreenExclusive = false;
    uint64_t nativeWindowFlags = 0;
    uint64_t nativeRenderAdapterLuidValidRows = 0;
    uint64_t nativeRenderAdapterLuidRelationshipMismatchRows = 0;
    uint64_t nativeRenderAdapterIdentityMismatchRows = 0;
    uint64_t nativeRenderAdapterLuidSnapshotMismatchRows = 0;
    bool haveNativeRenderAdapterLuidReference = false;
    uint64_t nativeRenderAdapterLuid = 0;
    uint64_t nativeForegroundWindowRows = 0;
    uint64_t nativeVblankVirtualizationProbeCompleteRows = 0;
    uint64_t nativeVblankVirtualizationCallAvailableRows = 0;
    uint64_t nativeVblankVirtualizationResultValidRows = 0;
    uint64_t nativeVblankVirtualizationSuccessRows = 0;
    uint64_t nativeVblankVirtualizationDisabledRows = 0;
    uint64_t nativeVblankVirtualizationRelationshipMismatchRows = 0;
    QMap<QByteArray, uint64_t> nativeVblankVirtualizationResults;
    uint64_t nativeDisplayConfigQueryResultValidRows = 0;
    uint64_t nativeDisplayConfigQuerySuccessRows = 0;
    uint64_t nativeDisplayPathValidRows = 0;
    uint64_t nativeDisplayTargetAvailableRows = 0;
    uint64_t nativeDisplaySignalValidRows = 0;
    uint64_t nativeDisplayDrrBoostRows = 0;
    uint64_t nativeDisplayTimingRelationshipMismatchRows = 0;
    uint64_t nativeDisplayTimingSnapshotMismatchRows = 0;
    uint64_t nativeDisplaySignalRateMismatchRows = 0;
    uint64_t nativeDisplayUnknownPathFlagRows = 0;
    uint64_t nativeDisplayPathSignalRateMismatchRows = 0;
    uint64_t nativeDisplayNonProgressiveRows = 0;
    uint64_t nativeDisplayNonIdentityRotationRows = 0;
    uint64_t nativeDisplayNonIdentityScalingRows = 0;
    uint64_t nativeDisplaySignalVsyncDividerRows = 0;
    uint64_t nativeDisplaySignalReservedInfoRows = 0;
    uint64_t configuredScanoutPeriodMismatchRows = 0;
    uint64_t configuredActiveScanoutMismatchRows = 0;
    bool haveNativeDisplayTimingReference = false;
    uint64_t nativeDisplayPathFlags = 0;
    uint64_t nativeDisplaySourceAdapterLuid = 0;
    uint64_t nativeDisplaySourceId = 0;
    uint64_t nativeDisplayTargetAdapterLuid = 0;
    uint64_t nativeDisplayTargetId = 0;
    uint64_t nativeDisplayOutputTechnology = 0;
    uint64_t nativeDisplayRotation = 0;
    uint64_t nativeDisplayScaling = 0;
    uint64_t nativeDisplayPathRefreshNumerator = 0;
    uint64_t nativeDisplayPathRefreshDenominator = 0;
    uint64_t nativeDisplaySignalPixelRateHz = 0;
    uint64_t nativeDisplaySignalHSyncNumerator = 0;
    uint64_t nativeDisplaySignalHSyncDenominator = 0;
    uint64_t nativeDisplaySignalVSyncNumerator = 0;
    uint64_t nativeDisplaySignalVSyncDenominator = 0;
    uint64_t nativeDisplaySignalActiveWidth = 0;
    uint64_t nativeDisplaySignalActiveHeight = 0;
    uint64_t nativeDisplaySignalTotalWidth = 0;
    uint64_t nativeDisplaySignalTotalHeight = 0;
    uint64_t nativeDisplaySignalAdditionalInfoRaw = 0;
    uint64_t nativeDisplaySignalScanLineOrdering = 0;
    uint64_t nativeDisplaySignalPeriodPs = 0;
    uint64_t nativeRasterSamplingRequestedRows = 0;
    uint64_t nativeRasterOpenResultValidRows = 0;
    uint64_t nativeRasterSourceValidRows = 0;
    uint64_t nativeRasterBeforeQueryResultValidRows = 0;
    uint64_t nativeRasterBeforeQuerySuccessRows = 0;
    uint64_t nativeRasterBeforeVerticalBlankRows = 0;
    uint64_t nativeRasterBeforeActiveScanoutRows = 0;
    uint64_t nativeRasterAfterQueryResultValidRows = 0;
    uint64_t nativeRasterAfterQuerySuccessRows = 0;
    uint64_t nativeRasterAfterVerticalBlankRows = 0;
    uint64_t nativeRasterAfterActiveScanoutRows = 0;
    uint64_t nativeRasterRelationshipMismatchRows = 0;
    uint64_t nativeRasterTimingOrderMismatchRows = 0;
    uint64_t nativeRasterSourceIdMismatchRows = 0;
    VrrRasterScanLineScaleInference nativeRasterScanLineScaleInference;
    uint64_t nativeRasterSignalRangeCheckedSamples = 0;
    uint64_t nativeRasterSignalRangeMismatchSamples = 0;
    uint64_t nativeRasterModelObservationSamples = 0;
    uint64_t nativeRasterModelAnchoredSamples = 0;
    uint64_t nativeRasterModelCurrentPostAnchorSamples = 0;
    uint64_t nativeRasterModelPriorAnchorSamples = 0;
    uint64_t nativeRasterVrrLockedComparableSamples = 0;
    uint64_t nativeRasterVrrLockedContradictions = 0;
    uint64_t nativeRasterFreeRunningComparableSamples = 0;
    uint64_t nativeRasterFreeRunningContradictions = 0;
    uint64_t nativeRasterEnvelopeComparableSamples = 0;
    uint64_t nativeRasterEnvelopeContradictions = 0;
    uint64_t nativeRasterVrrLockedScanLineComparableSamples = 0;
    uint64_t nativeRasterVrrLockedScanLineMismatches = 0;
    uint64_t nativeRasterFreeRunningScanLineComparableSamples = 0;
    uint64_t nativeRasterFreeRunningScanLineMismatches = 0;
    uint64_t nativeRasterScanLineEnvelopeComparableSamples = 0;
    uint64_t nativeRasterScanLineEnvelopeMatchedSamples = 0;
    uint64_t nativeRasterScanLineEnvelopeContradictions = 0;
    Distribution nativeRasterBeforeQueryDurationUs;
    Distribution nativeRasterAfterQueryDurationUs;
    Distribution nativeRasterBeforeToPresentUs;
    Distribution nativeRasterPresentToAfterUs;
    Distribution nativeRasterBeforeScanLine;
    Distribution nativeRasterAfterScanLine;
    Distribution nativeRasterBeforeNormalizedScanLine;
    Distribution nativeRasterAfterNormalizedScanLine;
    Distribution nativeRasterBeforeActiveScanoutPositionPpm;
    Distribution nativeRasterAfterActiveScanoutPositionPpm;
    Distribution nativeRasterModelAnchorAgeUs;
    Distribution nativeRasterVrrLockedPredictedScanLine;
    Distribution nativeRasterVrrLockedScanLineAbsoluteError;
    Distribution nativeRasterVrrLockedScanLineTolerance;
    SignedAccumulator nativeRasterVrrLockedScanLineResidual;
    std::array<NativeRasterScanLineBandMetrics, RateBandCount>
        nativeRasterVrrLockedScanLineBands;
    Distribution nativeRasterFreeRunningPredictedScanLine;
    Distribution nativeRasterFreeRunningScanLineAbsoluteError;
    Distribution nativeRasterFreeRunningScanLineTolerance;
    SignedAccumulator nativeRasterFreeRunningScanLineResidual;
    QMap<QByteArray, uint64_t> nativeRasterOpenResults;
    QMap<QByteArray, uint64_t> nativeRasterBeforeQueryResults;
    QMap<QByteArray, uint64_t> nativeRasterAfterQueryResults;
    QMap<QByteArray, uint64_t> nativeRasterVrrLockedObservationCrossTable;
    QMap<QByteArray, uint64_t> nativeRasterFreeRunningObservationCrossTable;
    uint64_t presentedNativePresentResultValidRows = 0;
    uint64_t presentedSubmissionIdQueryResultValidRows = 0;
    uint64_t presentedFrameStatsQueryResultValidRows = 0;
    bool postPresentQueryTimingTelemetryAvailable = false;
    uint64_t presentedPostPresentQueryTimingValidRows = 0;
    uint64_t postPresentQueryTimingRelationshipMismatchRows = 0;
    uint64_t rawSyncQpcValidRows = 0;
    uint64_t frameStatsSuccessWithoutRawSyncQpcRows = 0;
    uint64_t rawSyncQpcWithoutTranslatedRows = 0;
    uint64_t rawSyncQpcFrequencyMismatchRows = 0;
    uint64_t rawSyncQpcTranslationBaselines = 0;
    uint64_t rawSyncQpcTranslationComparisons = 0;
    uint64_t rawSyncQpcTranslationMismatchRows = 0;
    uint64_t qpcCorrelationValidRows = 0;
    uint64_t qpcCorrelationRelationshipMismatchRows = 0;
    uint64_t qpcCorrelationReferenceMismatchRows = 0;
    uint64_t qpcCorrelationTranslationMismatchRows = 0;
    uint64_t qpcCorrelationUncertaintyInvalidRows = 0;
    uint64_t qpcCorrelationHalfSpanUncertaintyUsMaximum = 0;
    bool haveQpcCorrelationReference = false;
    uint64_t qpcCorrelationReferenceTicks = 0;
    uint64_t qpcCorrelationReferenceTimeUs = 0;
    uint64_t qpcCorrelationFrequency = 0;
    uint64_t qpcCorrelationSpanTicks = 0;
    uint64_t nativePresentParameterMismatchRows = 0;
    uint64_t nativeVrrStateMismatchRows = 0;
    uint64_t decisionDispositionMismatches = 0;
    uint64_t presentationDispositionMismatches = 0;
    uint64_t dispositionDropFlagMismatches = 0;
    uint64_t queueStateMismatches = 0;
    uint64_t dequeueDecisionPresenceMismatches = 0;
    uint64_t validityPayloadMismatches = 0;
    uint64_t nonDecisionPayloadMismatchRows = 0;
    uint64_t sourceRateDisplayMismatches = 0;
    uint64_t renderWaitTelemetryMismatchRows = 0;
    uint64_t targetWaitTelemetryMismatchRows = 0;
    bool waitLifecycleTelemetryAvailable = false;
    uint64_t renderWaitExpectedRows = 0;
    uint64_t targetWaitExpectedRows = 0;
    uint64_t renderWaitLifecycleRows = 0;
    uint64_t targetWaitLifecycleRows = 0;
    uint64_t renderWaitLifecycleRelationshipMismatchRows = 0;
    uint64_t targetWaitLifecycleRelationshipMismatchRows = 0;
    uint64_t renderWaitCleanCompletionRows = 0;
    uint64_t targetWaitCleanCompletionRows = 0;
    uint64_t renderWaitCoarseSleepRows = 0;
    uint64_t targetWaitCoarseSleepRows = 0;
    uint64_t renderWaitClockStallRows = 0;
    uint64_t targetWaitClockStallRows = 0;
    uint64_t renderWaitYieldLimitRows = 0;
    uint64_t targetWaitYieldLimitRows = 0;
    uint64_t renderWaitEarlyReturnRows = 0;
    uint64_t targetWaitEarlyReturnRows = 0;
    uint64_t simulatedRenderWaitPathComparisons = 0;
    uint64_t simulatedRenderWaitPathMatches = 0;
    uint64_t simulatedRenderWaitRecordedFinalResidualRows = 0;
    uint64_t simulatedTargetWaitPathComparisons = 0;
    uint64_t simulatedTargetWaitPathMatches = 0;
    uint64_t simulatedTargetWaitRecordedFinalResidualRows = 0;
    uint64_t nativeOutcomeRelationshipMismatchRows = 0;
    uint64_t hadPriorSubmissionMismatches = 0;
    uint64_t submissionBoundaryMismatches = 0;
    uint64_t submitErrorMismatches = 0;
    uint64_t submissionSpacingMismatches = 0;
    uint64_t spacingMarginMismatches = 0;
    bool spacingCorrectionTelemetryAvailable = false;
    bool spacingLifecycleTimingTelemetryAvailable = false;
    uint64_t spacingCorrectionRelationshipMismatchRows = 0;
    uint64_t spacingLifecycleTimingRelationshipMismatchRows = 0;
    uint64_t spacingLifecycleTimingValidatedRows = 0;
    uint64_t spacingCorrectedRows = 0;
    bool completionQueueDepthTelemetryAvailable = false;
    uint64_t completionQueueDepthOutOfRangeRows = 0;
    uint64_t tearClassificationMismatches = 0;
    uint64_t tearRiskMismatches = 0;
    uint64_t firstArrivalUs = 0;
    uint64_t lastArrivalUs = 0;
    uint64_t originalDrops = 0;
    uint64_t originalTearRisks = 0;
    uint64_t simulatedTearRisks = 0;
    uint64_t simulatedLatchedFrames = 0;
    uint64_t scanoutAnomalies = 0;
    uint64_t repeatedRefreshes = 0;
    uint64_t exactReferenceTargets = 0;
    uint64_t exactSimulatedSubmissions = 0;
    uint64_t exactTearClassifications = 0;
    uint64_t exactRasterEnvelopeClassifications = 0;
    uint64_t invalidExecutionResiduals = 0;
    uint64_t invalidControllerLifecycleRows = 0;
    bool externalRebaseTelemetryAvailable = false;
    bool windowStateCauseTelemetryAvailable = false;
    uint64_t externalRebaseFlagRelationshipMismatchRows = 0;
    uint64_t externalRebaseFlagCarryForwardMismatchRows = 0;
    uint64_t unknownWindowStateFlagRows = 0;
    uint64_t midframeDisplayEpochInterruptRows = 0;
    uint64_t midframeDisplayEpochRelationshipMismatchRows = 0;
    QMap<QByteArray, uint64_t> externalRebaseFlagValues;
    QMap<QByteArray, uint64_t> midframeWindowStateFlagValues;
    uint64_t capturedRebaseEventsReplayed = 0;
    uint64_t referenceLatchedPresentationMismatches = 0;
    uint64_t referenceUsedRtpTimestampMismatches = 0;
    uint64_t referenceCadenceEligibleMismatches = 0;
    uint64_t referenceSourceRateChangedMismatches = 0;
    uint64_t referencePhaseDiscontinuityMismatches = 0;
    uint64_t referenceRebasedMismatches = 0;
    uint64_t referenceReadinessModelValidMismatches = 0;
    uint64_t workerArrivals = 0;
    uint64_t workerAccepted = 0;
    uint64_t workerCapacityEvictions = 0;
    uint64_t deepTraceRows = 0;
    uint64_t submissionIdValidRows = 0;
    uint64_t latchValidRows = 0;
    uint64_t uniqueLatchSamples = 0;
    uint64_t staleLatchSamples = 0;
    uint64_t freshLatchSamplesMatchedToSubmission = 0;
    uint64_t submissionSequenceResets = 0;
    uint64_t submissionSequenceDuplicates = 0;
    uint64_t latchSequenceResets = 0;
    uint64_t exactPresentRefreshTimestampSamples = 0;
    uint64_t exactPresentRefreshCorrelations = 0;
    uint64_t invalidExactPresentRefreshCorrelations = 0;
    uint64_t simulatedRecordedRefreshComparisons = 0;
    uint64_t simulatedAfterRecordedRefresh = 0;
    uint64_t exactPresentRefreshPhaseMatches = 0;
    uint64_t nativePresentTimingValidRows = 0;
    uint64_t presentedNativePresentTimingValidRows = 0;
    uint64_t presentedSubmissionIdValidRows = 0;
    uint64_t presentedRawPrePresentAnchorValidRows = 0;
    uint64_t adaptiveRawPrePresentAnchorValidRows = 0;
    uint64_t adaptivePrePresentAnchorValidRows = 0;
    uint64_t simulatedAdaptivePrePresentAnchorValidRows = 0;
    uint64_t nativePresentBoundaryComparisons = 0;
    uint64_t nativePresentBoundaryMismatches = 0;
    uint64_t prePresentAnchorSequenceRegressions = 0;
    uint64_t prePresentAnchorMissingRefreshSequence = 0;
    uint64_t prePresentAnchorTimeRegressions = 0;
    uint64_t prePresentAnchorCausalOrderViolations = 0;
    uint64_t prePresentAnchorTimestampJitterBeyondTolerance = 0;
    uint64_t prePresentAnchorNonadvancingTime = 0;
    uint64_t prePresentAnchorImplausiblyShortIntervals = 0;
    uint64_t prePresentAnchorsSuppressedByEpoch = 0;
    uint64_t postPresentAnchorMissingRefreshSequence = 0;
    uint64_t postPresentAnchorSequenceRegressions = 0;
    uint64_t postPresentAnchorTimeRegressions = 0;
    uint64_t postPresentAnchorCausalOrderViolations = 0;
    uint64_t postPresentAnchorTimestampJitterBeyondTolerance = 0;
    uint64_t postPresentAnchorNonadvancingTime = 0;
    uint64_t postPresentAnchorImplausiblyShortIntervals = 0;
    uint64_t unifiedAnchorSequenceRegressions = 0;
    uint64_t unifiedAnchorSameSequenceTimestampMismatches = 0;
    uint64_t unifiedAnchorNonadvancingTime = 0;
    uint64_t unifiedAnchorImplausiblyShortIntervals = 0;
    uint64_t observedRasterAnchorFallbacks = 0;
    uint64_t simulatedRasterAnchorFallbacks = 0;
    uint64_t observedRasterValidationContradictions = 0;
    uint64_t simulatedRasterValidationContradictions = 0;
    uint64_t presentCountBeforeValidRows = 0;
    uint64_t frameStatsBeforeValidRows = 0;
    uint64_t deepBeforeStateEligibleRows = 0;
    uint64_t deepBeforeStateComparisons = 0;
    uint64_t deepBeforeStateValidityMismatches = 0;
    uint64_t deepBeforePresentCountMismatches = 0;
    uint64_t deepBeforeFrameStatsMismatches = 0;
    uint64_t gpuReadyTimingValidRows = 0;
    uint64_t presentedGpuReadyTimingValidRows = 0;
    uint64_t presentedGpuReadyBoundsValidRows = 0;
    uint64_t injectedPeriodicStalls = 0;
    uint64_t injectedDecisionDelayUs = 0;
    uint64_t requestedRenderWakeDelayUs = 0;
    uint64_t injectedRenderWakeDelayUs = 0;
    uint64_t suppressedRenderWakeDelayUs = 0;
    uint64_t absorbedRenderWakeDelayUs = 0;
    uint64_t renderWakeExecutionDelayUs = 0;
    uint64_t renderWakeDelayRequestedFrames = 0;
    uint64_t renderWakeDelayEligibleFrames = 0;
    uint64_t requestedTargetWakeDelayUs = 0;
    uint64_t injectedTargetWakeDelayUs = 0;
    uint64_t suppressedTargetWakeDelayUs = 0;
    uint64_t absorbedTargetWakeDelayUs = 0;
    uint64_t targetWakeExecutionDelayUs = 0;
    uint64_t targetWakeDelayRequestedFrames = 0;
    uint64_t targetWakeDelayEligibleFrames = 0;
    uint64_t injectedPreparationDelayUs = 0;
    uint64_t injectedSubmissionDelayUs = 0;
    uint64_t injectedDisplayTransitionDelayUs = 0;
    uint64_t injectedDisplayTransitionDelayFrames = 0;
    uint64_t injectedSpacingGuardFeedbackUs = 0;
    uint64_t injectedSpacingGuardFeedbackFrames = 0;
    uint64_t rasterProbeOverheadRemovalRequestedFrames = 0;
    uint64_t rasterProbeOverheadRemovalAvailableFrames = 0;
    uint64_t rasterProbeOverheadRemovalMissingFrames = 0;
    uint64_t measuredRasterProbeDurationUs = 0;
    uint64_t removedRasterProbeOverheadUs = 0;
    uint64_t rasterProbeOverheadRemovalClampedFrames = 0;
    uint64_t injectedSubmissionAdvanceRequestedUs = 0;
    uint64_t injectedSubmissionAdvanceAppliedUs = 0;
    uint64_t injectedSubmissionAdvanceClampedFrames = 0;
    uint64_t injectedSubmissionAdvanceFrames = 0;
    uint64_t injectedAdvanceIntervalViolations = 0;
    uint64_t injectedAdvanceRasterCertainActive = 0;
    uint64_t injectedAdvanceRasterPossibleActive = 0;
    uint64_t injectedAdvanceRasterInactive = 0;
    uint64_t injectedAdvanceRasterLatchedSuppressed = 0;
    uint64_t injectedAdvanceRasterUnclassified = 0;
    uint64_t injectedAdvanceExactRefreshComparisons = 0;
    uint64_t injectedAdvanceExactRefreshActive = 0;
    uint64_t injectedAdvanceExactRefreshBoundary = 0;
    uint64_t injectedAdvanceExactRefreshBefore = 0;
    uint64_t injectedAdvanceExactRefreshAfterActive = 0;
    uint64_t injectedAdvanceExactRefreshLatched = 0;
    uint64_t injectedAdvanceExactRefreshUnclassified = 0;
    uint64_t injectedDisplayTransitionRasterCertainActive = 0;
    uint64_t injectedDisplayTransitionRasterPossibleActive = 0;
    uint64_t injectedDisplayTransitionRasterInactive = 0;
    uint64_t injectedDisplayTransitionRasterLatchedSuppressed = 0;
    uint64_t injectedDisplayTransitionRasterUnclassified = 0;
    uint64_t injectedDisplayTransitionExactRefreshComparisons = 0;
    uint64_t injectedDisplayTransitionExactRefreshActive = 0;
    uint64_t injectedDisplayTransitionExactRefreshBoundary = 0;
    uint64_t injectedDisplayTransitionExactRefreshBefore = 0;
    uint64_t injectedDisplayTransitionExactRefreshAfterActive = 0;
    uint64_t injectedDisplayTransitionExactRefreshLatched = 0;
    uint64_t injectedDisplayTransitionExactRefreshUnclassified = 0;
    uint64_t counterfactualFreeRunningAdaptiveRows = 0;
    uint64_t counterfactualFreeRunningBaselines = 0;
    uint64_t counterfactualFreeRunningComparisons = 0;
    uint64_t counterfactualFreeRunningUnseededRows = 0;
    uint64_t counterfactualFreeRunningLatchedResets = 0;
    uint64_t counterfactualFreeRunningTimeRegressions = 0;
    uint64_t counterfactualFreeRunningPeriodChanges = 0;
    uint64_t counterfactualFreeRunningConversionFailures = 0;
    uint64_t counterfactualFreeRunningScanoutAnomalyLower = 0;
    uint64_t counterfactualFreeRunningScanoutAnomalies = 0;
    uint64_t counterfactualFreeRunningScanoutAnomalyUpper = 0;
    uint64_t counterfactualFreeRunningRepeatedRefreshLower = 0;
    uint64_t counterfactualFreeRunningRepeatedRefreshes = 0;
    uint64_t counterfactualFreeRunningRepeatedRefreshUpper = 0;
    QMap<QByteArray, uint64_t> dispositions;
    QMap<QByteArray, uint64_t> tearClassifications;
    QMap<QByteArray, uint64_t> simulatedTearClassifications;
    QMap<QByteArray, uint64_t> observedExactRefreshClassifications;
    QMap<QByteArray, uint64_t> simulatedExactRefreshClassifications;
    QMap<QByteArray, uint64_t> observedRasterValidation;
    QMap<QByteArray, uint64_t> simulatedRasterValidation;
    QMap<QByteArray, uint64_t> nativeBackendCounts;
    QMap<QByteArray, uint64_t> nativePresentResults;
    QMap<QByteArray, uint64_t> nativePresentFlags;
    QMap<QByteArray, uint64_t> nativeTearingFeatureQueryResults;
    QMap<QByteArray, uint64_t> nativeSwapChainDescQueryResults;
    QMap<QByteArray, uint64_t> nativeSwapChainFlagsObserved;
    QMap<QByteArray, uint64_t> nativeSwapChainSwapEffects;
    QMap<QByteArray, uint64_t> nativeFullscreenStateQueryResults;
    QMap<QByteArray, uint64_t> nativeWindowFlagsObserved;
    QMap<QByteArray, uint64_t> nativeVrrFallbackReasons;
    QMap<QByteArray, uint64_t> nativeDesktopMonitorCounts;
    QMap<QByteArray, uint64_t> submissionIdQueryResults;
    QMap<QByteArray, uint64_t> frameStatsQueryResults;
    Distribution observedDecodeToArrival;
    Distribution observedArrivalToDequeue;
    Distribution observedDequeueToDecision;
    Distribution observedDecodeToSubmission;
    Distribution observedArrivalToSubmission;
    Distribution observedDecisionToSubmission;
    Distribution observedProjectedSourceToSubmission;
    Distribution observedSubmissionSpacing;
    Distribution observedSpacingDeficit;
    Distribution observedCompletionQueueDepth;
    Distribution observedAbsoluteSubmitError;
    Distribution observedPreparation;
    Distribution observedPresentCall;
    Distribution observedNativePresentCall;
    SignedAccumulator observedNativePresentBoundaryDelta;
    BoundedDistribution repeatedSyncAnchorTimestampJitter;
    BoundedDistribution syncAnchorRefreshDelta;
    BoundedDistribution syncAnchorElapsedUs;
    BoundedDistribution syncAnchorMeanIntervalUs;
    BoundedDistribution repeatedPostSyncAnchorTimestampJitter;
    BoundedDistribution postSyncAnchorRefreshDelta;
    BoundedDistribution postSyncAnchorElapsedUs;
    BoundedDistribution postSyncAnchorMeanIntervalUs;
    Distribution observedGpuReadyWait;
    Distribution observedGpuReadySignalCall;
    Distribution observedGpuReadyFlushCall;
    Distribution observedGpuReadySetEventCall;
    Distribution observedGpuReadyPollFenceLag;
    Distribution observedGpuReadyCompletionUncertainty;
    Distribution observedGpuReadySignalToCompletionUpperBound;
    Distribution observedSubmissionIdQueryCall;
    Distribution observedFrameStatsQueryCall;
    Distribution observedPostPresentObservationTail;
    Distribution observedSpacingCheckToRecheck;
    Distribution observedControllerCall;
    Distribution observedStaleAge;
    Distribution observedRenderWait;
    Distribution observedRenderWaitOvershoot;
    Distribution observedRenderSchedulerDelay;
    SignedAccumulator observedRenderCoarseWakeOffset;
    Distribution observedRenderActiveYieldCount;
    Distribution observedTargetWait;
    Distribution observedTargetWaitOvershoot;
    Distribution observedTargetSchedulerDelay;
    SignedAccumulator observedTargetCoarseWakeOffset;
    Distribution observedTargetActiveYieldCount;
    Distribution observedCorrectionWait;
    uint64_t renderDeadlineAlreadyElapsedRows = 0;
    uint64_t targetDeadlineAlreadyElapsedRows = 0;
    SignedAccumulator observedExactPresentRefreshPhase;
    SignedAccumulator observedExactPresentRefreshModeledPhase;
    SignedAccumulator observedExactPresentActiveScanoutPhase;
    SignedAccumulator simulatedRecordedRefreshPhase;
    SignedAccumulator simulatedRecordedRefreshModeledPhase;
    SignedAccumulator simulatedRecordedActiveScanoutPhase;
    Distribution simulatedDecodeToSubmission;
    Distribution simulatedArrivalToSubmission;
    Distribution simulatedDecisionToSubmission;
    Distribution simulatedProjectedSourceToSubmission;
    Distribution simulatedSubmissionSpacing;
    Distribution simulatedAbsoluteSubmitError;
    Distribution referenceTargetDrift;
    Distribution referenceSourceIntervalDrift;
    Distribution referenceSourceTimeDrift;
    Distribution referenceSourcePeriodDrift;
    Distribution referenceReadyOffsetDrift;
    Distribution referenceReadinessBudgetDrift;
    Distribution referenceTimingBudgetDrift;
    Distribution referenceRenderLeadDrift;
    Distribution referenceRenderWakeLeadDrift;
    Distribution referenceTargetWakeLeadDrift;
    Distribution referenceGuardDrift;
    Distribution referenceHeadroomDrift;
    Distribution referenceRenderStartDrift;
    Distribution referenceReadinessPhaseDrift;
    Distribution referenceReadinessDemandDrift;
    Distribution referenceAppliedReadinessReserveDrift;
    Distribution referenceRenderBaselineDrift;
    Distribution referenceRenderInsuranceDrift;
    Distribution referencePacingLatencyBudgetDrift;
    Distribution referenceCadenceSampleCountDrift;
    Distribution referenceRateCandidateSampleCountDrift;
    Distribution referenceReadinessSampleCountDrift;
    Distribution referencePreparationSampleCountDrift;
    Distribution referenceRenderSchedulerSampleCountDrift;
    Distribution referenceTargetSchedulerSampleCountDrift;
    Distribution referenceCleanSpacingFramesDrift;
    Distribution referencePhaseErrorFramesDrift;
    Distribution simulatedTargetDrift;
    Distribution simulatedSubmissionDrift;
    Distribution simulatedCadenceError;
    BoundedDistribution pairedAbsoluteSubmissionDelta;
    BoundedDistribution counterfactualFreeRunningPhaseReferenceDifferencePs;
    SignedAccumulator pairedSubmissionDelta;
    std::array<CadenceBandMetrics, RateBandCount> observedRateBands;
    std::array<CadenceBandMetrics, RateBandCount> simulatedRateBands;
    AnomalyWindowMetrics observedJerkAnomalies;
    AnomalyWindowMetrics simulatedJerkAnomalies;
    RasterEnvelopeMetrics observedRasterEnvelope;
    RasterEnvelopeMetrics simulatedRasterEnvelope;
};

uint64_t unsignedField(const QList<QByteArray>& fields, int column)
{
    return fields[column].toULongLong();
}

int64_t signedField(const QList<QByteArray>& fields, int column)
{
    return fields[column].toLongLong();
}

uint64_t optionalUnsignedField(const QList<QByteArray>& fields, int column)
{
    return column >= 0 && column < fields.size() ?
        fields[column].toULongLong() : 0;
}

int64_t optionalSignedField(const QList<QByteArray>& fields, int column)
{
    return column >= 0 && column < fields.size() ?
        fields[column].toLongLong() : 0;
}

bool inferNativeRasterScanLineScale(
    const QString& tracePath, const QList<QByteArray>& expectedHeader,
    const Columns& columns,
    VrrRasterScanLineScaleInference& inference,
    QString& error)
{
    const int required[] = {
        columns.nativeDisplaySignalValid,
        columns.nativeDisplaySignalActiveHeight,
        columns.nativeDisplaySignalTotalHeight,
        columns.nativeRasterBeforeQueryResultValid,
        columns.nativeRasterBeforeQueryResult,
        columns.nativeRasterBeforeInVerticalBlank,
        columns.nativeRasterBeforeScanLine,
        columns.nativeRasterAfterQueryResultValid,
        columns.nativeRasterAfterQueryResult,
        columns.nativeRasterAfterInVerticalBlank,
        columns.nativeRasterAfterScanLine,
    };
    if (std::any_of(
            std::begin(required), std::end(required),
            [](int column) { return column < 0; })) {
        return true;
    }

    TraceReader reader(tracePath);
    if (!reader.open(error)) {
        return false;
    }
    QByteArray headerLine;
    if (!reader.readLine(headerLine, error)) {
        return false;
    }
    if (headerLine.trimmed().split(',') != expectedHeader) {
        error = "trace header changed during scan-line scale inference";
        return false;
    }

    QByteArray line;
    while (reader.readLine(line, error)) {
        const QByteArray normalizedLine = line.trimmed();
        if (normalizedLine.startsWith("#vrr_trace_footer,")) {
            continue;
        }
        const QList<QByteArray> fields = normalizedLine.split(',');
        if (fields.size() != columns.maximum() + 1) {
            error = "malformed trace row during scan-line scale inference";
            return false;
        }
        if (optionalUnsignedField(
                fields, columns.nativeDisplaySignalValid) == 0) {
            continue;
        }
        const uint64_t activeHeight = optionalUnsignedField(
            fields, columns.nativeDisplaySignalActiveHeight);
        const uint64_t totalHeight = optionalUnsignedField(
            fields, columns.nativeDisplaySignalTotalHeight);
        const auto addSample =
            [&](int resultValidColumn, int resultColumn,
                int verticalBlankColumn, int scanLineColumn) {
                if (optionalUnsignedField(
                        fields, resultValidColumn) == 0 ||
                        optionalSignedField(fields, resultColumn) != 0) {
                    return;
                }
                addVrrRasterScanLineScaleSample(
                    inference,
                    optionalUnsignedField(
                        fields, verticalBlankColumn) != 0,
                    optionalUnsignedField(fields, scanLineColumn),
                    activeHeight, totalHeight);
            };
        addSample(
            columns.nativeRasterBeforeQueryResultValid,
            columns.nativeRasterBeforeQueryResult,
            columns.nativeRasterBeforeInVerticalBlank,
            columns.nativeRasterBeforeScanLine);
        addSample(
            columns.nativeRasterAfterQueryResultValid,
            columns.nativeRasterAfterQueryResult,
            columns.nativeRasterAfterInVerticalBlank,
            columns.nativeRasterAfterScanLine);
    }
    return error.isEmpty();
}

uint64_t absoluteValue(int64_t value)
{
    if (value >= 0) {
        return static_cast<uint64_t>(value);
    }
    return static_cast<uint64_t>(-(value + 1)) + 1;
}

uint64_t absoluteDifference(int64_t left, int64_t right)
{
    if ((left < 0) == (right < 0)) {
        return left >= right ?
            static_cast<uint64_t>(left - right) :
            static_cast<uint64_t>(right - left);
    }
    const uint64_t leftMagnitude = absoluteValue(left);
    const uint64_t rightMagnitude = absoluteValue(right);
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    return rightMagnitude > maximum - leftMagnitude ?
        maximum : leftMagnitude + rightMagnitude;
}

int64_t signedDifference(uint64_t left, uint64_t right)
{
    if (left >= right) {
        const uint64_t difference = left - right;
        return difference > static_cast<uint64_t>(
            std::numeric_limits<int64_t>::max()) ?
                std::numeric_limits<int64_t>::max() :
                static_cast<int64_t>(difference);
    }
    const uint64_t difference = right - left;
    return difference > static_cast<uint64_t>(
        std::numeric_limits<int64_t>::max()) ?
            std::numeric_limits<int64_t>::min() :
            -static_cast<int64_t>(difference);
}

void addReferenceControllerDiagnostics(
    Metrics& metrics, const VrrTimingDiagnostics& diagnostics,
    const QList<QByteArray>& fields, const Columns& columns)
{
    metrics.referenceReadinessPhaseDrift.add(absoluteDifference(
        diagnostics.readinessPhaseUs,
        optionalSignedField(fields, columns.readinessPhaseUs)));
    metrics.referenceReadinessDemandDrift.add(absoluteValue(
        signedDifference(
            diagnostics.readinessDemandUs,
            optionalUnsignedField(fields, columns.readinessDemandUs))));
    metrics.referenceAppliedReadinessReserveDrift.add(absoluteValue(
        signedDifference(
            diagnostics.appliedReadinessReserveUs,
            optionalUnsignedField(
                fields, columns.appliedReadinessReserveUs))));
    metrics.referenceRenderBaselineDrift.add(absoluteValue(
        signedDifference(
            diagnostics.renderBaselineUs,
            optionalUnsignedField(fields, columns.renderBaselineUs))));
    metrics.referenceRenderInsuranceDrift.add(absoluteValue(
        signedDifference(
            diagnostics.renderInsuranceUs,
            optionalUnsignedField(fields, columns.renderInsuranceUs))));
    metrics.referencePacingLatencyBudgetDrift.add(absoluteValue(
        signedDifference(
            diagnostics.pacingLatencyBudgetUs,
            optionalUnsignedField(
                fields, columns.pacingLatencyBudgetUs))));
    metrics.referenceCadenceSampleCountDrift.add(absoluteValue(
        signedDifference(
            static_cast<uint64_t>(diagnostics.cadenceSamples),
            optionalUnsignedField(fields, columns.cadenceSampleCount))));
    metrics.referenceRateCandidateSampleCountDrift.add(absoluteValue(
        signedDifference(
            static_cast<uint64_t>(diagnostics.rateCandidateSamples),
            optionalUnsignedField(
                fields, columns.rateCandidateSampleCount))));
    metrics.referenceReadinessSampleCountDrift.add(absoluteValue(
        signedDifference(
            static_cast<uint64_t>(diagnostics.readinessSamples),
            optionalUnsignedField(fields, columns.readinessSampleCount))));
    metrics.referencePreparationSampleCountDrift.add(absoluteValue(
        signedDifference(
            static_cast<uint64_t>(diagnostics.preparationSamples),
            optionalUnsignedField(
                fields, columns.preparationSampleCount))));
    metrics.referenceRenderSchedulerSampleCountDrift.add(absoluteValue(
        signedDifference(
            static_cast<uint64_t>(diagnostics.renderSchedulerSamples),
            optionalUnsignedField(
                fields, columns.renderSchedulerSampleCount))));
    metrics.referenceTargetSchedulerSampleCountDrift.add(absoluteValue(
        signedDifference(
            static_cast<uint64_t>(diagnostics.targetSchedulerSamples),
            optionalUnsignedField(
                fields, columns.targetSchedulerSampleCount))));
    metrics.referenceCleanSpacingFramesDrift.add(absoluteValue(
        signedDifference(
            static_cast<uint64_t>(diagnostics.cleanSpacingFrames),
            optionalUnsignedField(fields, columns.cleanSpacingFrames))));
    metrics.referencePhaseErrorFramesDrift.add(absoluteValue(
        signedDifference(
            static_cast<uint64_t>(diagnostics.phaseErrorFrames),
            optionalUnsignedField(fields, columns.phaseErrorFrames))));
    metrics.referenceReadinessModelValidMismatches +=
        diagnostics.readinessModelValid !=
            (optionalUnsignedField(
                fields, columns.readinessModelValid) != 0) ? 1 : 0;
}

struct CadenceSample {
    bool valid = false;
    bool jerkValid = false;
    uint64_t sourceElapsedUs = 0;
    uint64_t submissionElapsedUs = 0;
    int64_t residualUs = 0;
    int64_t jerkUs = 0;
    int64_t phaseUs = 0;
};

struct CadenceTracker {
    bool havePresentation = false;
    bool haveResidual = false;
    uint64_t priorSourceUs = 0;
    uint64_t priorSubmissionUs = 0;
    int64_t priorResidualUs = 0;

    CadenceSample observe(uint64_t sourceUs, uint64_t submissionUs,
                          bool discontinuity)
    {
        CadenceSample sample;
        sample.phaseUs = signedDifference(submissionUs, sourceUs);
        if (!discontinuity && havePresentation && sourceUs >= priorSourceUs &&
                submissionUs >= priorSubmissionUs) {
            sample.sourceElapsedUs = sourceUs - priorSourceUs;
            sample.submissionElapsedUs = submissionUs - priorSubmissionUs;
            if (sample.sourceElapsedUs != 0) {
                sample.valid = true;
                sample.residualUs = signedDifference(
                    sample.submissionElapsedUs, sample.sourceElapsedUs);
                if (haveResidual) {
                    sample.jerkValid = true;
                    sample.jerkUs = sample.residualUs - priorResidualUs;
                }
            }
        }

        havePresentation = true;
        priorSourceUs = sourceUs;
        priorSubmissionUs = submissionUs;
        if (sample.valid) {
            haveResidual = true;
            priorResidualUs = sample.residualUs;
        }
        else {
            haveResidual = false;
        }
        return sample;
    }
};

int roundedRateForPeriod(uint64_t periodUs)
{
    if (periodUs == 0) {
        return 0;
    }
    return static_cast<int>((1000000ULL + periodUs / 2) / periodUs);
}

RateBandIndex primaryRateBand(int rateHz)
{
    if (rateHz <= 0) return UnknownRate;
    if (rateHz < 40) return Below40Fps;
    if (rateHz < 50) return Fps40To49;
    if (rateHz < 60) return Fps50To59;
    if (rateHz < 70) return Fps60To69;
    if (rateHz < 80) return Fps70To79;
    if (rateHz < 90) return Fps80To89;
    if (rateHz <= 100) return Fps90To100;
    if (rateHz < 110) return Fps101To109;
    if (rateHz <= 116) return Fps110To116;
    return Above116Fps;
}

template<typename Function>
void forRateBands(int rateHz, Function function)
{
    function(primaryRateBand(rateHz));
    if (rateHz >= 40 && rateHz <= 116) {
        function(Fps40To116);
    }
    if (rateHz >= 60 && rateHz <= 100) {
        function(Fps60To100);
    }
}

void addCadenceFrame(std::array<CadenceBandMetrics, RateBandCount>& bands,
                     int rateHz, uint64_t decodeCompleteUs,
                     uint64_t submissionUs, const CadenceSample& sample,
                     bool intervalViolation)
{
    forRateBands(rateHz, [&](RateBandIndex index) {
        CadenceBandMetrics& band = bands[index];
        ++band.presentedFrames;
        band.decodeToSubmission.addElapsed(submissionUs, decodeCompleteUs);
        band.modelledIntervalViolations += intervalViolation ? 1 : 0;
        if (sample.valid) {
            ++band.cadenceTransitions;
            band.absoluteCadenceResidual.add(absoluteValue(
                sample.residualUs));
            band.signedCadenceResidual.add(sample.residualUs);
        }
        if (sample.jerkValid) {
            band.absoluteJerk.add(absoluteValue(sample.jerkUs));
        }
    });
}

void addScanoutOutcome(
    std::array<CadenceBandMetrics, RateBandCount>& bands, int rateHz,
    uint64_t anomalies, uint64_t repeatedRefreshes)
{
    forRateBands(rateHz, [&](RateBandIndex index) {
        bands[index].scanoutAnomalies += anomalies;
        bands[index].repeatedRefreshes += repeatedRefreshes;
    });
}

void addRasterPhaseState(VrrRasterPhaseState state, uint64_t& active,
                         uint64_t& inactive, uint64_t& boundary)
{
    switch (state) {
    case VrrRasterPhaseState::Active:
        ++active;
        break;
    case VrrRasterPhaseState::Inactive:
        ++inactive;
        break;
    case VrrRasterPhaseState::BoundaryUncertain:
        ++boundary;
        break;
    case VrrRasterPhaseState::Unclassified:
        break;
    }
}

bool rasterStateComparable(VrrRasterPhaseState state)
{
    return state == VrrRasterPhaseState::Active ||
        state == VrrRasterPhaseState::Inactive;
}

bool rasterStateMatchesObservation(VrrRasterPhaseState state,
                                   bool observedActive)
{
    return (state == VrrRasterPhaseState::Active) ==
        observedActive;
}

QByteArray rasterObservationCrossKey(bool observedActive,
                                     VrrRasterPhaseState predicted)
{
    QByteArray key = observedActive ?
        "observed_active|predicted_" :
        "observed_vblank|predicted_";
    key.append(vrrRasterPhaseStateName(predicted));
    return key;
}

void addRasterEnvelope(RasterEnvelopeMetrics& metrics,
                       const VrrRasterPhaseResult& result)
{
    if (result.resolvedScanoutPeriodUs != 0) {
        metrics.resolvedScanoutPeriod.add(
            result.resolvedScanoutPeriodUs);
        metrics.resolvedScanoutPeriodPs.add(
            result.resolvedScanoutPeriodPs);
        metrics.resolvedActiveScanout.add(
            result.resolvedActiveScanoutUs);
        metrics.resolvedActiveScanoutPs.add(
            result.resolvedActiveScanoutPs);
        metrics.resolvedSyncToActiveScanout.add(
            result.resolvedSyncToActiveScanoutUs);
        metrics.resolvedAnchorMaxAge.add(
            result.resolvedAnchorMaxAgeUs);
        metrics.activeScanoutClamped +=
            result.activeScanoutClamped ? 1 : 0;
        metrics.scanoutPhaseWindowInvalid +=
            result.scanoutPhaseWindowInvalid ? 1 : 0;
    }
    switch (result.envelope) {
    case VrrRasterEnvelopeClass::LatchedSuppressed:
        ++metrics.latchedSuppressed;
        return;
    case VrrRasterEnvelopeClass::CertainActive:
        ++metrics.eligibleAdaptiveSubmissions;
        ++metrics.certainActive;
        break;
    case VrrRasterEnvelopeClass::PossibleActive:
        ++metrics.eligibleAdaptiveSubmissions;
        ++metrics.possibleActive;
        break;
    case VrrRasterEnvelopeClass::InactiveInBothModels:
        ++metrics.eligibleAdaptiveSubmissions;
        ++metrics.inactiveInBothModels;
        break;
    case VrrRasterEnvelopeClass::Unclassified:
        ++metrics.eligibleAdaptiveSubmissions;
        ++metrics.unclassified;
        return;
    }

    metrics.anchorAge.add(result.anchorAgeUs);
    metrics.freeRunningPhase.add(result.freeRunningPhaseUs);
    metrics.freeRunningPhasePs.add(result.freeRunningPhasePs);
    if (result.vrrLockedWaitUs != 0) {
        metrics.vrrLockedProtectionWait.add(result.vrrLockedWaitUs);
    }
    if (result.freeRunningWaitUs != 0) {
        metrics.freeRunningProtectionWait.add(result.freeRunningWaitUs);
    }
    if (result.vrrLockedScanoutPositionValid) {
        metrics.vrrLockedScanoutPositionPpm.add(
            result.vrrLockedScanoutPositionPpm);
    }
    if (result.freeRunningScanoutPositionValid) {
        metrics.freeRunningScanoutPositionPpm.add(
            result.freeRunningScanoutPositionPpm);
    }
    addRasterPhaseState(result.vrrLocked, metrics.vrrLockedActive,
                        metrics.vrrLockedInactive,
                        metrics.vrrLockedBoundary);
    addRasterPhaseState(result.freeRunning, metrics.freeRunningActive,
                        metrics.freeRunningInactive,
                        metrics.freeRunningBoundary);
}

void addRasterEnvelopeToBands(
    std::array<CadenceBandMetrics, RateBandCount>& bands, int rateHz,
    VrrRasterEnvelopeClass value)
{
    forRateBands(rateHz, [&](RateBandIndex index) {
        CadenceBandMetrics& band = bands[index];
        switch (value) {
        case VrrRasterEnvelopeClass::CertainActive:
            ++band.rasterCertainActive;
            break;
        case VrrRasterEnvelopeClass::PossibleActive:
            ++band.rasterPossibleActive;
            break;
        case VrrRasterEnvelopeClass::InactiveInBothModels:
            ++band.rasterInactive;
            break;
        case VrrRasterEnvelopeClass::LatchedSuppressed:
            ++band.rasterLatched;
            break;
        case VrrRasterEnvelopeClass::Unclassified:
            ++band.rasterUnclassified;
            break;
        }
    });
}

void addExactRefreshPhaseToBands(
    std::array<CadenceBandMetrics, RateBandCount>& bands, int rateHz,
    VrrExactRefreshPhaseClass value)
{
    forRateBands(rateHz, [&](RateBandIndex index) {
        CadenceBandMetrics& band = bands[index];
        switch (value) {
        case VrrExactRefreshPhaseClass::LatchedSuppressed:
            ++band.exactRefreshLatched;
            break;
        case VrrExactRefreshPhaseClass::BeforeActiveScanout:
            ++band.exactRefreshBeforeActive;
            break;
        case VrrExactRefreshPhaseClass::Active:
            ++band.exactRefreshActive;
            break;
        case VrrExactRefreshPhaseClass::BoundaryUncertain:
            ++band.exactRefreshBoundary;
            break;
        case VrrExactRefreshPhaseClass::AfterActiveScanout:
            ++band.exactRefreshAfterActive;
            break;
        case VrrExactRefreshPhaseClass::Unclassified:
            ++band.exactRefreshUnclassified;
            break;
        }
    });
}

void addInjectedAdvanceExactRefreshOutcome(
    Metrics& metrics, VrrExactRefreshPhaseClass value)
{
    ++metrics.injectedAdvanceExactRefreshComparisons;
    switch (value) {
    case VrrExactRefreshPhaseClass::LatchedSuppressed:
        ++metrics.injectedAdvanceExactRefreshLatched;
        break;
    case VrrExactRefreshPhaseClass::BeforeActiveScanout:
        ++metrics.injectedAdvanceExactRefreshBefore;
        break;
    case VrrExactRefreshPhaseClass::Active:
        ++metrics.injectedAdvanceExactRefreshActive;
        break;
    case VrrExactRefreshPhaseClass::BoundaryUncertain:
        ++metrics.injectedAdvanceExactRefreshBoundary;
        break;
    case VrrExactRefreshPhaseClass::AfterActiveScanout:
        ++metrics.injectedAdvanceExactRefreshAfterActive;
        break;
    case VrrExactRefreshPhaseClass::Unclassified:
        ++metrics.injectedAdvanceExactRefreshUnclassified;
        break;
    }
}

void addInjectedDisplayTransitionExactRefreshOutcome(
    Metrics& metrics, VrrExactRefreshPhaseClass value)
{
    ++metrics.injectedDisplayTransitionExactRefreshComparisons;
    switch (value) {
    case VrrExactRefreshPhaseClass::LatchedSuppressed:
        ++metrics.injectedDisplayTransitionExactRefreshLatched;
        break;
    case VrrExactRefreshPhaseClass::BeforeActiveScanout:
        ++metrics.injectedDisplayTransitionExactRefreshBefore;
        break;
    case VrrExactRefreshPhaseClass::Active:
        ++metrics.injectedDisplayTransitionExactRefreshActive;
        break;
    case VrrExactRefreshPhaseClass::BoundaryUncertain:
        ++metrics.injectedDisplayTransitionExactRefreshBoundary;
        break;
    case VrrExactRefreshPhaseClass::AfterActiveScanout:
        ++metrics.injectedDisplayTransitionExactRefreshAfterActive;
        break;
    case VrrExactRefreshPhaseClass::Unclassified:
        ++metrics.injectedDisplayTransitionExactRefreshUnclassified;
        break;
    }
}

bool addRasterValidation(QMap<QByteArray, uint64_t>& counts,
                         VrrRasterEnvelopeClass envelope,
                         VrrExactRefreshPhaseClass exact)
{
    const QByteArray key =
        QByteArray(vrrRasterEnvelopeClassName(envelope)) + "_to_" +
        vrrExactRefreshPhaseClassName(exact);
    ++counts[key];
    return vrrRasterEnvelopeContradictsExactRefresh(envelope, exact);
}

uint64_t saturatingAdd(uint64_t left, uint64_t right)
{
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    return right > maximum - left ? maximum : left + right;
}

uint64_t positiveDifference(uint64_t left, uint64_t right)
{
    return left > right ? left - right : 0;
}

uint64_t saturatingMultiply(uint64_t value, uint64_t multiplier)
{
    if (value == 0 || multiplier == 0) {
        return 0;
    }
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    return value > maximum / multiplier ?
        maximum : value * multiplier;
}

QJsonObject distributionObject(const Distribution& distribution)
{
    QJsonObject object;
    object["count"] = static_cast<qint64>(distribution.count);
    if (distribution.count == 0) {
        object["min"] = 0;
        object["mean"] = 0;
        object["stddev"] = 0;
        object["p50"] = 0;
        object["p90"] = 0;
        object["p95"] = 0;
        object["p99"] = 0;
        object["p99_5"] = 0;
        object["p99_9"] = 0;
        object["p99_95"] = 0;
        object["max"] = 0;
        return object;
    }
    object["min"] = static_cast<qint64>(distribution.minimum);
    object["mean"] = static_cast<double>(distribution.mean);
    object["stddev"] = static_cast<double>(std::sqrt(
        distribution.squaredDifferenceTotal /
        static_cast<long double>(distribution.count)));
    object["p50"] = static_cast<qint64>(distribution.percentile(50));
    object["p90"] = static_cast<qint64>(distribution.percentile(90));
    object["p95"] = static_cast<qint64>(distribution.percentile(95));
    object["p99"] = static_cast<qint64>(distribution.percentile(99));
    object["p99_5"] = static_cast<qint64>(
        distribution.percentileRatio(995, 1000));
    object["p99_9"] = static_cast<qint64>(
        distribution.percentileRatio(999, 1000));
    object["p99_95"] = static_cast<qint64>(
        distribution.percentileRatio(9995, 10000));
    object["max"] = static_cast<qint64>(distribution.maximum);
    return object;
}

QJsonObject senderCadenceObject(const SenderCadenceTracker& tracker,
                                uint64_t durationUs)
{
    QJsonObject object;
    object["pairs"] = static_cast<qint64>(tracker.pairs);
    object["stall_interval_exclusion_us"] =
        static_cast<qint64>(SenderCadenceTracker::kStallIntervalUs);
    object["hitch_threshold_us"] =
        static_cast<qint64>(SenderCadenceTracker::kHitchUs);
    object["hitches"] = static_cast<qint64>(tracker.hitches);
    object["spacing_errors_over_2ms"] = static_cast<qint64>(tracker.spacingErrorsOverHitch);
    object["spacing_accuracy_percent"] = tracker.pairs ?
        100.0 * (1.0 - double(tracker.spacingErrorsOverHitch) / double(tracker.pairs)) : 0.0;
    object["client_spacing_pairs"] = static_cast<qint64>(tracker.clientSpacingPairs);
    object["client_spacing_errors_over_3ms"] = static_cast<qint64>(tracker.clientSpacingErrorsOver3ms);
    object["client_spacing_accuracy_percent"] = tracker.clientSpacingPairs ?
        100.0 * (1.0 - double(tracker.clientSpacingErrorsOver3ms) / double(tracker.clientSpacingPairs)) : 0.0;
    object["client_spacing_error_us"] = distributionObject(tracker.clientSpacingErrorUs);
    object["source_stall_pairs"] = static_cast<qint64>(tracker.sourceStallPairs);
    object["hitches_per_second"] = durationUs != 0 ?
        static_cast<double>(tracker.hitches) * 1000000.0 /
            static_cast<double>(durationUs) : 0.0;
    object["hitch_late_arrivals"] =
        static_cast<qint64>(tracker.hitchLateArrivals);
    object["hitch_render_lead_jumps"] =
        static_cast<qint64>(tracker.hitchRenderLeadJumps);
    object["hitch_display_floor"] =
        static_cast<qint64>(tracker.hitchDisplayFloor);
    object["hitch_other"] = static_cast<qint64>(tracker.hitchOther);
    object["absolute_spacing_error_us"] = distributionObject(
        tracker.absoluteSpacingErrorUs);
    object["absolute_jerk_us"] = distributionObject(tracker.absoluteJerkUs);
    object["presented_jerk_us"] = distributionObject(tracker.presentedJerkUs);
    object["presented_jerk_pairs"] =
        static_cast<qint64>(tracker.presentedJerkPairs);
    object["presented_jerk_over_2ms_per_mille"] =
        tracker.presentedJerkPairs != 0 ?
            static_cast<qint64>(tracker.presentedJerkOverHitch * 1000 /
                                tracker.presentedJerkPairs) : 0;
    return object;
}

QJsonObject boundedDistributionObject(const BoundedDistribution& distribution)
{
    QJsonObject object;
    object["count"] = static_cast<qint64>(distribution.count);
    object["sample_count"] = static_cast<qint64>(
        distribution.samples.size());
    object["sample_capacity"] = static_cast<qint64>(
        kMaximumDiagnosticSamples);
    object["quantiles_approximate"] =
        distribution.count > distribution.samples.size();
    if (distribution.count == 0) {
        object["min"] = 0;
        object["mean"] = 0;
        object["stddev"] = 0;
        object["p50"] = 0;
        object["p90"] = 0;
        object["p95"] = 0;
        object["p99"] = 0;
        object["p99_5"] = 0;
        object["p99_9"] = 0;
        object["p99_95"] = 0;
        object["max"] = 0;
        return object;
    }

    std::vector<uint64_t> sortedValues = distribution.samples;
    std::sort(sortedValues.begin(), sortedValues.end());
    const auto sampledPercentile = [&sortedValues](
                                       size_t numerator,
                                       size_t denominator) {
        if (denominator == 0) {
            return uint64_t { 0 };
        }
        const size_t boundedNumerator = std::min(denominator, numerator);
        const size_t rank = std::max<size_t>(
            1,
            (sortedValues.size() / denominator) * boundedNumerator +
                ((sortedValues.size() % denominator) * boundedNumerator +
                 denominator - 1) /
                    denominator);
        return sortedValues[rank - 1];
    };
    object["min"] = static_cast<qint64>(distribution.minimum);
    object["mean"] = static_cast<double>(distribution.mean);
    object["stddev"] = static_cast<double>(std::sqrt(
        distribution.squaredDifferenceTotal /
        static_cast<long double>(distribution.count)));
    object["p50"] = static_cast<qint64>(sampledPercentile(50, 100));
    object["p90"] = static_cast<qint64>(sampledPercentile(90, 100));
    object["p95"] = static_cast<qint64>(sampledPercentile(95, 100));
    object["p99"] = static_cast<qint64>(sampledPercentile(99, 100));
    object["p99_5"] = static_cast<qint64>(sampledPercentile(995, 1000));
    object["p99_9"] = static_cast<qint64>(sampledPercentile(999, 1000));
    object["p99_95"] = static_cast<qint64>(
        sampledPercentile(9995, 10000));
    object["max"] = static_cast<qint64>(distribution.maximum);
    return object;
}

QJsonObject signedAccumulatorObject(const SignedAccumulator& accumulator)
{
    QJsonObject object;
    object["count"] = static_cast<qint64>(accumulator.count);
    object["min"] = static_cast<qint64>(
        accumulator.count == 0 ? 0 : accumulator.minimum);
    object["mean"] = accumulator.count == 0 ? 0.0 :
        static_cast<double>(accumulator.mean);
    object["stddev"] = accumulator.count == 0 ? 0.0 :
        static_cast<double>(std::sqrt(
            accumulator.squaredDifferenceTotal /
            static_cast<long double>(accumulator.count)));
    object["max"] = static_cast<qint64>(
        accumulator.count == 0 ? 0 : accumulator.maximum);
    object["negative"] = static_cast<qint64>(accumulator.negative);
    object["zero"] = static_cast<qint64>(accumulator.zero);
    object["positive"] = static_cast<qint64>(accumulator.positive);
    return object;
}

QJsonObject cadenceBandObject(const CadenceBandMetrics& band)
{
    QJsonObject object;
    object["presented_frames"] = static_cast<qint64>(band.presentedFrames);
    object["cadence_transitions"] = static_cast<qint64>(
        band.cadenceTransitions);
    object["decode_to_submission_us"] = boundedDistributionObject(
        band.decodeToSubmission);
    object["absolute_cadence_residual_us"] = boundedDistributionObject(
        band.absoluteCadenceResidual);
    object["signed_cadence_residual_us"] = signedAccumulatorObject(
        band.signedCadenceResidual);
    object["absolute_jerk_us"] = boundedDistributionObject(
        band.absoluteJerk);
    object["modelled_interval_violations"] = static_cast<qint64>(
        band.modelledIntervalViolations);
    object["scanout_anomalies"] = static_cast<qint64>(
        band.scanoutAnomalies);
    object["repeated_refreshes"] = static_cast<qint64>(
        band.repeatedRefreshes);
    object["scanout_anomalies_per_10000_presented"] =
        band.presentedFrames == 0 ? 0.0 :
            static_cast<double>(band.scanoutAnomalies) * 10000.0 /
                static_cast<double>(band.presentedFrames);
    QJsonObject raster;
    raster["certain_active"] = static_cast<qint64>(
        band.rasterCertainActive);
    raster["possible_active"] = static_cast<qint64>(
        band.rasterPossibleActive);
    raster["inactive_in_both_models"] = static_cast<qint64>(
        band.rasterInactive);
    raster["unclassified"] = static_cast<qint64>(
        band.rasterUnclassified);
    raster["latched_suppressed"] = static_cast<qint64>(
        band.rasterLatched);
    raster["exposure_lower_bound"] = static_cast<qint64>(
        band.rasterCertainActive);
    raster["classified_exposure_upper_bound"] = static_cast<qint64>(
        band.rasterCertainActive + band.rasterPossibleActive);
    raster["exposure_upper_bound"] = static_cast<qint64>(
        band.rasterCertainActive + band.rasterPossibleActive +
        band.rasterUnclassified);
    object["raster_phase_envelope"] = raster;
    QJsonObject exactRefresh;
    exactRefresh["latched_suppressed"] = static_cast<qint64>(
        band.exactRefreshLatched);
    exactRefresh["before_active_scanout"] = static_cast<qint64>(
        band.exactRefreshBeforeActive);
    exactRefresh["active"] = static_cast<qint64>(
        band.exactRefreshActive);
    exactRefresh["boundary_uncertain"] = static_cast<qint64>(
        band.exactRefreshBoundary);
    exactRefresh["after_active_scanout"] = static_cast<qint64>(
        band.exactRefreshAfterActive);
    exactRefresh["unclassified"] = static_cast<qint64>(
        band.exactRefreshUnclassified);
    object["exact_refresh_phase"] = exactRefresh;
    return object;
}

QJsonObject cadenceBandsObject(
    const std::array<CadenceBandMetrics, RateBandCount>& bands)
{
    QJsonObject object;
    for (size_t i = 0; i < bands.size(); ++i) {
        object[kRateBandNames[i]] = cadenceBandObject(bands[i]);
    }
    return object;
}

QJsonObject anomalyWindowObject(const AnomalyWindowMetrics& metrics)
{
    QJsonObject object;
    object["threshold_us"] = static_cast<qint64>(kJerkAnomalyThresholdUs);
    object["events"] = static_cast<qint64>(metrics.anomalies);
    object["longest_consecutive_run"] = static_cast<qint64>(
        metrics.longestConsecutive);
    object["worst_1s_count"] = static_cast<qint64>(metrics.worstOneSecond);
    object["worst_10s_count"] = static_cast<qint64>(metrics.worstTenSeconds);
    object["worst_60s_count"] = static_cast<qint64>(
        metrics.worstSixtySeconds);
    return object;
}

QJsonObject rasterEnvelopeObject(const RasterEnvelopeMetrics& metrics)
{
    QJsonObject object;
    object["eligible_adaptive_submissions"] = static_cast<qint64>(
        metrics.eligibleAdaptiveSubmissions);
    object["certain_active"] = static_cast<qint64>(metrics.certainActive);
    object["possible_active"] = static_cast<qint64>(metrics.possibleActive);
    object["inactive_in_both_models"] = static_cast<qint64>(
        metrics.inactiveInBothModels);
    object["unclassified"] = static_cast<qint64>(metrics.unclassified);
    object["latched_suppressed"] = static_cast<qint64>(
        metrics.latchedSuppressed);
    object["active_scanout_clamped_rows"] = static_cast<qint64>(
        metrics.activeScanoutClamped);
    object["scanout_phase_window_invalid_rows"] =
        static_cast<qint64>(
            metrics.scanoutPhaseWindowInvalid);
    const uint64_t classified = metrics.certainActive +
        metrics.possibleActive + metrics.inactiveInBothModels;
    const uint64_t adaptiveOutcomes = classified + metrics.unclassified;
    object["evaluated_presentations"] = static_cast<qint64>(
        metrics.eligibleAdaptiveSubmissions + metrics.latchedSuppressed);
    object["adaptive_outcome_rows"] = static_cast<qint64>(
        adaptiveOutcomes);
    object["outcome_accounting_complete"] =
        adaptiveOutcomes == metrics.eligibleAdaptiveSubmissions;
    object["classified"] = static_cast<qint64>(classified);
    object["classification_coverage_percent"] =
        metrics.eligibleAdaptiveSubmissions == 0 ? 0.0 :
            static_cast<double>(classified) * 100.0 /
                static_cast<double>(metrics.eligibleAdaptiveSubmissions);
    object["exposure_lower_bound"] = static_cast<qint64>(
        metrics.certainActive);
    object["classified_exposure_upper_bound"] = static_cast<qint64>(
        metrics.certainActive + metrics.possibleActive);
    object["exposure_upper_bound"] = static_cast<qint64>(
        metrics.certainActive + metrics.possibleActive +
        metrics.unclassified);
    object["exposure_upper_bound_includes_unclassified"] = true;
    object["exposure_lower_bound_per_10000_eligible"] =
        metrics.eligibleAdaptiveSubmissions == 0 ? 0.0 :
            static_cast<double>(metrics.certainActive) * 10000.0 /
                static_cast<double>(metrics.eligibleAdaptiveSubmissions);
    object["exposure_upper_bound_per_10000_eligible"] =
        metrics.eligibleAdaptiveSubmissions == 0 ? 0.0 :
            static_cast<double>(
                metrics.certainActive + metrics.possibleActive +
                metrics.unclassified) * 10000.0 /
                static_cast<double>(metrics.eligibleAdaptiveSubmissions);

    QJsonObject locked;
    locked["active"] = static_cast<qint64>(metrics.vrrLockedActive);
    locked["inactive"] = static_cast<qint64>(metrics.vrrLockedInactive);
    locked["boundary_uncertain"] = static_cast<qint64>(
        metrics.vrrLockedBoundary);
    locked["protection_wait_us"] = boundedDistributionObject(
        metrics.vrrLockedProtectionWait);
    locked["active_scanout_position_ppm"] = boundedDistributionObject(
        metrics.vrrLockedScanoutPositionPpm);
    object["ideal_vrr_flip_following"] = locked;

    QJsonObject freeRunning;
    freeRunning["active"] = static_cast<qint64>(
        metrics.freeRunningActive);
    freeRunning["inactive"] = static_cast<qint64>(
        metrics.freeRunningInactive);
    freeRunning["boundary_uncertain"] = static_cast<qint64>(
        metrics.freeRunningBoundary);
    freeRunning["phase_us"] = boundedDistributionObject(
        metrics.freeRunningPhase);
    freeRunning["phase_ps"] = boundedDistributionObject(
        metrics.freeRunningPhasePs);
    freeRunning["protection_wait_us"] = boundedDistributionObject(
        metrics.freeRunningProtectionWait);
    freeRunning["active_scanout_position_ppm"] =
        boundedDistributionObject(
            metrics.freeRunningScanoutPositionPpm);
    object["free_running_fixed_refresh"] = freeRunning;
    object["sync_anchor_age_us"] = boundedDistributionObject(
        metrics.anchorAge);
    object["resolved_scanout_period_us"] = boundedDistributionObject(
        metrics.resolvedScanoutPeriod);
    object["resolved_scanout_period_ps"] = boundedDistributionObject(
        metrics.resolvedScanoutPeriodPs);
    object["resolved_active_scanout_us"] = boundedDistributionObject(
        metrics.resolvedActiveScanout);
    object["resolved_active_scanout_ps"] = boundedDistributionObject(
        metrics.resolvedActiveScanoutPs);
    object["resolved_sync_to_active_scanout_us"] =
        boundedDistributionObject(
            metrics.resolvedSyncToActiveScanout);
    object["resolved_anchor_max_age_us"] = boundedDistributionObject(
        metrics.resolvedAnchorMaxAge);
    object["scanout_position_scale"] =
        "0 is raster start and 1000000 is raster end; physical scan direction is not assumed";
    return object;
}

QJsonObject validityObject(uint64_t valid, uint64_t eligible)
{
    QJsonObject object;
    object["valid"] = static_cast<qint64>(valid);
    object["eligible"] = static_cast<qint64>(eligible);
    object["coverage_percent"] = eligible == 0 ? 0.0 :
        static_cast<double>(valid) * 100.0 / static_cast<double>(eligible);
    return object;
}

QJsonObject countObject(const QMap<QByteArray, uint64_t>& counts)
{
    QJsonObject object;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        object[QString::fromUtf8(it.key())] = static_cast<qint64>(it.value());
    }
    return object;
}

uint64_t countTotal(const QMap<QByteArray, uint64_t>& counts)
{
    uint64_t total = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        total = saturatingAdd(total, it.value());
    }
    return total;
}

uint64_t periodForRate(int rateHz)
{
    if (rateHz <= 0) {
        return 16667;
    }
    return (1000000ULL + static_cast<uint64_t>(rateHz) / 2) /
        static_cast<uint64_t>(rateHz);
}

QByteArray simulatedTearClassification(bool presented, bool latched,
                                       bool canLatch,
                                       bool hadPriorSubmission,
                                       uint64_t submissionUs,
                                       uint64_t priorSubmissionUs,
                                       uint64_t displayPeriodUs)
{
    if (!presented) {
        return "not_presented";
    }
    if (latched && canLatch) {
        return "confirmed_safe_latched";
    }
    if (!hadPriorSubmission) {
        return "first_submission_unknown";
    }
    const uint64_t spacingUs = submissionUs >= priorSubmissionUs ?
        submissionUs - priorSubmissionUs : 0;
    return spacingUs < displayPeriodUs ?
        QByteArray("adaptive_interval_violation") :
        QByteArray("adaptive_interval_safe");
}

QJsonObject summaryObject(const Metrics& metrics, qint64 elapsedMs,
                          const QString& tracePath, qint64 traceBytes,
                          const QString& decodedTraceSha256,
                          int capturedDisplayHz, int capturedStreamFps,
                          int simulatedDisplayHz, int simulatedStreamFps,
                          bool additionalQueuedFrame,
                          bool sessionAllowTearing,
                          bool simulatedCanLatch,
                          uint64_t capturedResponsiveBuffer,
                          const VrrReplayScenario& scenario)
{
    const uint64_t captureDurationUs =
        metrics.lastArrivalUs >= metrics.firstArrivalUs ?
            metrics.lastArrivalUs - metrics.firstArrivalUs : 0;
    const uint64_t uniqueArrivalRows =
        metrics.delivered >= metrics.arrivalSequenceDuplicates ?
            metrics.delivered - metrics.arrivalSequenceDuplicates : 0;
    const bool footerAccountingValid =
        metrics.traceFooter.present &&
        metrics.traceFooter.cleanShutdown &&
        metrics.traceFooter.rowsEnqueued == metrics.delivered &&
        saturatingAdd(
            metrics.traceFooter.rowsEnqueued,
            metrics.traceFooter.rowsDropped) ==
                metrics.traceFooter.arrivalSequenceAllocated;
    const bool footerContentHashValid =
        metrics.traceFooter.present &&
        metrics.traceFooter.formatVersion >= 2 &&
        metrics.traceFooter.decodedSha256 ==
            decodedTraceSha256.toLatin1();
    const bool arrivalSequenceComplete = metrics.delivered != 0 &&
        footerAccountingValid &&
        !metrics.traceFooter.sizeCapped &&
        !metrics.traceFooter.writeFailed &&
        metrics.traceFooter.rowsDropped == 0 &&
        metrics.firstArrivalSequence == 1 &&
        metrics.lastArrivalSequence ==
            metrics.traceFooter.arrivalSequenceAllocated &&
        uniqueArrivalRows ==
            metrics.traceFooter.arrivalSequenceAllocated &&
        metrics.arrivalSequenceGaps == 0 &&
        metrics.arrivalSequenceDuplicates == 0;
    const uint64_t expectedArrivalRows = metrics.traceFooter.present ?
        metrics.traceFooter.arrivalSequenceAllocated :
        (metrics.lastArrivalSequence >= metrics.firstArrivalSequence &&
                metrics.firstArrivalSequence != 0 ?
            metrics.lastArrivalSequence - metrics.firstArrivalSequence + 1 :
            0);
    const uint64_t missingTraceRows = expectedArrivalRows > uniqueArrivalRows ?
        expectedArrivalRows - uniqueArrivalRows : 0;

    QJsonObject capture;
    capture["trace"] = QFileInfo(tracePath).fileName();
    capture["trace_bytes"] = traceBytes;
    capture["normalized_decoded_csv_sha256"] = decodedTraceSha256;
    capture["schema"] = static_cast<qint64>(metrics.traceSchema);
    capture["duration_us"] = static_cast<qint64>(captureDurationUs);
    capture["duration_seconds"] = static_cast<double>(captureDurationUs) /
        1000000.0;
    capture["delivered_frames"] = static_cast<qint64>(metrics.delivered);
    capture["scheduled_frames"] = static_cast<qint64>(metrics.scheduled);
    capture["presented_frames"] = static_cast<qint64>(
        metrics.presentedFrames);
    capture["arrival_sequence_first"] = static_cast<qint64>(
        metrics.firstArrivalSequence);
    capture["arrival_sequence_last"] = static_cast<qint64>(
        metrics.lastArrivalSequence);
    capture["arrival_sequence_complete"] = arrivalSequenceComplete;
    capture["recorded_sequence_integrity_valid"] = arrivalSequenceComplete;
    QJsonObject footer;
    footer["present"] = metrics.traceFooter.present;
    footer["format_version"] = static_cast<qint64>(
        metrics.traceFooter.formatVersion);
    footer["clean_shutdown"] = metrics.traceFooter.cleanShutdown;
    footer["arrival_sequence_allocated"] = static_cast<qint64>(
        metrics.traceFooter.arrivalSequenceAllocated);
    footer["rows_enqueued"] = static_cast<qint64>(
        metrics.traceFooter.rowsEnqueued);
    footer["rows_dropped"] = static_cast<qint64>(
        metrics.traceFooter.rowsDropped);
    footer["size_capped"] = metrics.traceFooter.sizeCapped;
    footer["write_failed"] = metrics.traceFooter.writeFailed;
    footer["accounting_valid"] = footerAccountingValid;
    footer["expected_decoded_sha256"] =
        QString::fromLatin1(metrics.traceFooter.decodedSha256);
    footer["decoded_sha256_matches"] = footerContentHashValid;
    capture["clean_close_footer"] = footer;
    capture["missing_trace_rows"] = static_cast<qint64>(missingTraceRows);
    capture["arrival_sequence_gaps"] = static_cast<qint64>(
        metrics.arrivalSequenceGaps);
    capture["arrival_sequence_duplicates"] = static_cast<qint64>(
        metrics.arrivalSequenceDuplicates);
    capture["arrival_sequence_out_of_order_transitions"] =
        static_cast<qint64>(
            metrics.arrivalSequenceOutOfOrderTransitions);
    capture["arrival_sequence_regressions"] = static_cast<qint64>(
        metrics.arrivalSequenceOutOfOrderTransitions);
    capture["terminal_row_order_is_arrival_order"] =
        metrics.arrivalSequenceOutOfOrderTransitions == 0;
    capture["tail_completeness_verifiable"] =
        metrics.traceFooter.present;
    capture["latch_time_semantics"] =
        "DXGI stores SyncQPCTime paired with latch_sync_refresh_seq; it is not generally the timestamp of latch_present_refresh_seq";
    capture["display_hz"] = capturedDisplayHz;
    capture["stream_fps"] = capturedStreamFps;
    capture["additional_queued_frame"] = additionalQueuedFrame;
    capture["session_allow_tearing"] = sessionAllowTearing;
    capture["session_allow_tearing_field_available"] =
        metrics.sessionAllowTearingTelemetryAvailable;
    QJsonObject sessionConfigIntegrity;
    sessionConfigIntegrity["display_refresh_mismatch_rows"] =
        static_cast<qint64>(metrics.displayRefreshMismatchRows);
    sessionConfigIntegrity["stream_rate_mismatch_rows"] =
        static_cast<qint64>(metrics.streamRateMismatchRows);
    sessionConfigIntegrity["additional_queued_frame_mismatch_rows"] =
        static_cast<qint64>(
            metrics.additionalQueuedFrameMismatchRows);
    sessionConfigIntegrity["allow_tearing_mismatch_rows"] =
        static_cast<qint64>(metrics.sessionAllowTearingMismatchRows);
    sessionConfigIntegrity["latch_capability_mismatch_rows"] =
        static_cast<qint64>(metrics.latchCapabilityMismatchRows);
    sessionConfigIntegrity["display_period_mismatch_rows"] =
        static_cast<qint64>(metrics.displayPeriodMismatchRows);
    sessionConfigIntegrity["controller_parameter_mismatch_rows"] =
        static_cast<qint64>(metrics.controllerParameterMismatchRows);
    sessionConfigIntegrity["valid"] =
        metrics.displayRefreshMismatchRows == 0 &&
        metrics.streamRateMismatchRows == 0 &&
        metrics.additionalQueuedFrameMismatchRows == 0 &&
        metrics.sessionAllowTearingMismatchRows == 0 &&
        metrics.latchCapabilityMismatchRows == 0 &&
        metrics.displayPeriodMismatchRows == 0 &&
        metrics.controllerParameterMismatchRows == 0;
    capture["session_config_integrity"] = sessionConfigIntegrity;
    QJsonObject timestampIntegrity;
    timestampIntegrity["decode_to_arrival_order_violations"] =
        static_cast<qint64>(metrics.decodeToArrivalOrderViolations);
    timestampIntegrity["decode_readiness_order_violations"] =
        static_cast<qint64>(metrics.decodeReadinessOrderViolations);
    timestampIntegrity["arrival_to_dequeue_order_violations"] =
        static_cast<qint64>(metrics.arrivalToDequeueOrderViolations);
    timestampIntegrity["dequeue_to_decision_order_violations"] =
        static_cast<qint64>(metrics.dequeueToDecisionOrderViolations);
    timestampIntegrity["controller_call_order_violations"] =
        static_cast<qint64>(metrics.controllerCallOrderViolations);
    timestampIntegrity["controller_call_duration_mismatch_rows"] =
        static_cast<qint64>(
            metrics.controllerCallDurationMismatchRows);
    timestampIntegrity["stale_check_order_violations"] =
        static_cast<qint64>(metrics.staleCheckOrderViolations);
    timestampIntegrity["stale_age_mismatch_rows"] =
        static_cast<qint64>(metrics.staleAgeMismatchRows);
    timestampIntegrity["wait_boundary_order_violations"] =
        static_cast<qint64>(metrics.waitBoundaryOrderViolations);
    timestampIntegrity["correction_wait_order_violations"] =
        static_cast<qint64>(metrics.correctionWaitOrderViolations);
    timestampIntegrity["terminal_time_order_violations"] =
        static_cast<qint64>(metrics.terminalTimeOrderViolations);
    timestampIntegrity["preparation_order_violations"] =
        static_cast<qint64>(metrics.preparationOrderViolations);
    timestampIntegrity["preparation_duration_mismatch_rows"] =
        static_cast<qint64>(metrics.preparationDurationMismatchRows);
    timestampIntegrity["present_operation_order_violations"] =
        static_cast<qint64>(metrics.presentOperationOrderViolations);
    timestampIntegrity["present_operation_duration_mismatch_rows"] =
        static_cast<qint64>(
            metrics.presentOperationDurationMismatchRows);
    timestampIntegrity["native_present_order_violations"] =
        static_cast<qint64>(metrics.nativePresentOrderViolations);
    timestampIntegrity["native_present_duration_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativePresentDurationMismatchRows);
    timestampIntegrity["gpu_ready_order_violations"] =
        static_cast<qint64>(metrics.gpuReadyOrderViolations);
    timestampIntegrity["gpu_ready_duration_mismatch_rows"] =
        static_cast<qint64>(metrics.gpuReadyDurationMismatchRows);
    timestampIntegrity["gpu_ready_stage_timing_fields_available"] =
        metrics.gpuReadyStageTimingTelemetryAvailable;
    timestampIntegrity[
        "gpu_ready_stage_timing_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.gpuReadyStageTimingRelationshipMismatchRows);
    timestampIntegrity[
        "gpu_ready_completion_bounds_fields_available"] =
        metrics.gpuReadyBoundsTelemetryAvailable;
    timestampIntegrity[
        "gpu_ready_completion_bounds_derivation_mismatch_rows"] =
        static_cast<qint64>(
            metrics.gpuReadyBoundsDerivationMismatchRows);
    timestampIntegrity[
        "gpu_ready_fence_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.gpuReadyFenceRelationshipMismatchRows);
    timestampIntegrity["native_raster_timing_order_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterTimingOrderMismatchRows);
    timestampIntegrity[
        "post_present_query_timing_fields_available"] =
        metrics.postPresentQueryTimingTelemetryAvailable;
    timestampIntegrity[
        "post_present_query_timing_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.postPresentQueryTimingRelationshipMismatchRows);
    timestampIntegrity["submission_timestamp_regressions"] =
        static_cast<qint64>(metrics.submissionTimestampRegressions);
    timestampIntegrity["valid"] =
        metrics.decodeToArrivalOrderViolations == 0 &&
        metrics.decodeReadinessOrderViolations == 0 &&
        metrics.arrivalToDequeueOrderViolations == 0 &&
        metrics.dequeueToDecisionOrderViolations == 0 &&
        metrics.controllerCallOrderViolations == 0 &&
        metrics.controllerCallDurationMismatchRows == 0 &&
        metrics.staleCheckOrderViolations == 0 &&
        metrics.staleAgeMismatchRows == 0 &&
        metrics.waitBoundaryOrderViolations == 0 &&
        metrics.correctionWaitOrderViolations == 0 &&
        metrics.terminalTimeOrderViolations == 0 &&
        metrics.preparationOrderViolations == 0 &&
        metrics.preparationDurationMismatchRows == 0 &&
        metrics.presentOperationOrderViolations == 0 &&
        metrics.presentOperationDurationMismatchRows == 0 &&
        metrics.nativePresentOrderViolations == 0 &&
        metrics.nativePresentDurationMismatchRows == 0 &&
        metrics.gpuReadyOrderViolations == 0 &&
        metrics.gpuReadyDurationMismatchRows == 0 &&
        (!metrics.gpuReadyStageTimingTelemetryAvailable ||
         metrics.gpuReadyStageTimingRelationshipMismatchRows == 0) &&
        (!metrics.gpuReadyBoundsTelemetryAvailable ||
         (metrics.gpuReadyFenceRelationshipMismatchRows == 0 &&
          metrics.gpuReadyBoundsDerivationMismatchRows == 0)) &&
        metrics.nativeRasterTimingOrderMismatchRows == 0 &&
        (!metrics.postPresentQueryTimingTelemetryAvailable ||
         metrics.postPresentQueryTimingRelationshipMismatchRows == 0) &&
        metrics.submissionTimestampRegressions == 0;
    capture["timestamp_integrity"] = timestampIntegrity;
    const bool semanticIntegrityReady =
        metrics.decisionDispositionMismatches == 0 &&
        metrics.presentationDispositionMismatches == 0 &&
        metrics.dispositionDropFlagMismatches == 0 &&
        metrics.queueStateMismatches == 0 &&
        metrics.dequeueDecisionPresenceMismatches == 0 &&
        metrics.validityPayloadMismatches == 0 &&
        metrics.nonDecisionPayloadMismatchRows == 0 &&
        metrics.sourceRateDisplayMismatches == 0 &&
        metrics.renderWaitTelemetryMismatchRows == 0 &&
        metrics.targetWaitTelemetryMismatchRows == 0 &&
        metrics.renderWaitLifecycleRelationshipMismatchRows == 0 &&
        metrics.targetWaitLifecycleRelationshipMismatchRows == 0 &&
        metrics.externalRebaseFlagRelationshipMismatchRows == 0 &&
        metrics.externalRebaseFlagCarryForwardMismatchRows == 0 &&
        metrics.unknownWindowStateFlagRows == 0 &&
        metrics.midframeDisplayEpochRelationshipMismatchRows == 0 &&
        metrics.gpuReadyNativeResultRelationshipMismatchRows == 0 &&
        metrics.nativeOutcomeRelationshipMismatchRows == 0 &&
        metrics.nativePresentParameterMismatchRows == 0 &&
        metrics.nativeVrrStateMismatchRows == 0 &&
        metrics.nativeDxgiCapabilityRelationshipMismatchRows == 0 &&
        metrics.nativeDxgiCapabilitySnapshotMismatchRows == 0 &&
        metrics.nativeRenderAdapterLuidRelationshipMismatchRows == 0 &&
        metrics.nativeRenderAdapterIdentityMismatchRows == 0 &&
        metrics.nativeRenderAdapterLuidSnapshotMismatchRows == 0 &&
        metrics.nativeVblankVirtualizationRelationshipMismatchRows == 0 &&
        metrics.nativeDisplayTimingRelationshipMismatchRows == 0 &&
        metrics.nativeDisplaySignalRateMismatchRows == 0 &&
        metrics.nativeRasterRelationshipMismatchRows == 0 &&
        metrics.nativeRasterSourceIdMismatchRows == 0 &&
        metrics.presenterSubmissionTimingRelationshipMismatchRows == 0 &&
        metrics.hadPriorSubmissionMismatches == 0 &&
        metrics.submissionBoundaryMismatches == 0 &&
        metrics.submitErrorMismatches == 0 &&
        metrics.submissionSpacingMismatches == 0 &&
        metrics.spacingMarginMismatches == 0 &&
        metrics.spacingCorrectionRelationshipMismatchRows == 0 &&
        (!metrics.spacingLifecycleTimingTelemetryAvailable ||
         metrics.spacingLifecycleTimingRelationshipMismatchRows == 0) &&
        metrics.completionQueueDepthOutOfRangeRows == 0 &&
        metrics.tearClassificationMismatches == 0 &&
        metrics.tearRiskMismatches == 0;
    QJsonObject semanticIntegrity;
    semanticIntegrity["decision_disposition_mismatches"] =
        static_cast<qint64>(metrics.decisionDispositionMismatches);
    semanticIntegrity["presentation_disposition_mismatches"] =
        static_cast<qint64>(metrics.presentationDispositionMismatches);
    semanticIntegrity["disposition_drop_flag_mismatches"] =
        static_cast<qint64>(metrics.dispositionDropFlagMismatches);
    semanticIntegrity["queue_state_mismatches"] =
        static_cast<qint64>(metrics.queueStateMismatches);
    semanticIntegrity["dequeue_decision_presence_mismatches"] =
        static_cast<qint64>(
            metrics.dequeueDecisionPresenceMismatches);
    semanticIntegrity["validity_payload_mismatches"] =
        static_cast<qint64>(metrics.validityPayloadMismatches);
    semanticIntegrity["nondecision_payload_mismatch_rows"] =
        static_cast<qint64>(metrics.nonDecisionPayloadMismatchRows);
    semanticIntegrity["source_rate_display_mismatches"] =
        static_cast<qint64>(metrics.sourceRateDisplayMismatches);
    semanticIntegrity["render_wait_telemetry_mismatch_rows"] =
        static_cast<qint64>(
            metrics.renderWaitTelemetryMismatchRows);
    semanticIntegrity["target_wait_telemetry_mismatch_rows"] =
        static_cast<qint64>(
            metrics.targetWaitTelemetryMismatchRows);
    semanticIntegrity[
        "render_wait_lifecycle_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.renderWaitLifecycleRelationshipMismatchRows);
    semanticIntegrity[
        "target_wait_lifecycle_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.targetWaitLifecycleRelationshipMismatchRows);
    semanticIntegrity["window_state_cause_fields_available"] =
        metrics.windowStateCauseTelemetryAvailable;
    semanticIntegrity["window_state_known_flags_mask"] =
        QString::number(kWindowStateChangeKnownMask);
    semanticIntegrity["window_state_display_epoch_flags_mask"] =
        QString::number(kWindowStateChangeDisplayEpochMask);
    semanticIntegrity[
        "external_rebase_flag_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.externalRebaseFlagRelationshipMismatchRows);
    semanticIntegrity[
        "external_rebase_flag_carry_forward_mismatch_rows"] =
        static_cast<qint64>(
            metrics.externalRebaseFlagCarryForwardMismatchRows);
    semanticIntegrity["unknown_window_state_flag_rows"] =
        static_cast<qint64>(
            metrics.unknownWindowStateFlagRows);
    semanticIntegrity["midframe_display_epoch_interrupt_rows"] =
        static_cast<qint64>(
            metrics.midframeDisplayEpochInterruptRows);
    semanticIntegrity[
        "midframe_display_epoch_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.midframeDisplayEpochRelationshipMismatchRows);
    semanticIntegrity["external_rebase_flag_values"] =
        countObject(metrics.externalRebaseFlagValues);
    semanticIntegrity["midframe_window_state_flag_values"] =
        countObject(metrics.midframeWindowStateFlagValues);
    semanticIntegrity[
        "gpu_ready_native_result_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.gpuReadyNativeResultRelationshipMismatchRows);
    semanticIntegrity["native_outcome_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeOutcomeRelationshipMismatchRows);
    semanticIntegrity["native_present_parameter_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativePresentParameterMismatchRows);
    semanticIntegrity["native_vrr_state_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeVrrStateMismatchRows);
    semanticIntegrity[
        "native_dxgi_capability_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeDxgiCapabilityRelationshipMismatchRows);
    semanticIntegrity[
        "native_dxgi_capability_snapshot_change_without_rebase_rows"] =
        static_cast<qint64>(
            metrics.nativeDxgiCapabilitySnapshotMismatchRows);
    semanticIntegrity[
        "native_render_adapter_luid_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRenderAdapterLuidRelationshipMismatchRows);
    semanticIntegrity[
        "native_render_adapter_identity_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRenderAdapterIdentityMismatchRows);
    semanticIntegrity[
        "native_render_adapter_luid_snapshot_change_rows"] =
        static_cast<qint64>(
            metrics.nativeRenderAdapterLuidSnapshotMismatchRows);
    semanticIntegrity[
        "native_vblank_virtualization_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeVblankVirtualizationRelationshipMismatchRows);
    semanticIntegrity["native_display_timing_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplayTimingRelationshipMismatchRows);
    semanticIntegrity[
        "native_display_signal_rate_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplaySignalRateMismatchRows);
    semanticIntegrity["native_raster_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterRelationshipMismatchRows);
    semanticIntegrity["native_raster_source_id_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterSourceIdMismatchRows);
    semanticIntegrity[
        "presenter_submission_timing_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.presenterSubmissionTimingRelationshipMismatchRows);
    semanticIntegrity["had_prior_submission_mismatches"] =
        static_cast<qint64>(metrics.hadPriorSubmissionMismatches);
    semanticIntegrity["submission_boundary_mismatches"] =
        static_cast<qint64>(metrics.submissionBoundaryMismatches);
    semanticIntegrity["submit_error_mismatches"] =
        static_cast<qint64>(metrics.submitErrorMismatches);
    semanticIntegrity["submission_spacing_mismatches"] =
        static_cast<qint64>(metrics.submissionSpacingMismatches);
    semanticIntegrity["spacing_margin_mismatches"] =
        static_cast<qint64>(metrics.spacingMarginMismatches);
    semanticIntegrity[
        "spacing_correction_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.spacingCorrectionRelationshipMismatchRows);
    semanticIntegrity[
        "spacing_lifecycle_timing_fields_available"] =
        metrics.spacingLifecycleTimingTelemetryAvailable;
    semanticIntegrity[
        "spacing_lifecycle_timing_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.spacingLifecycleTimingRelationshipMismatchRows);
    semanticIntegrity[
        "spacing_lifecycle_timing_validated_rows"] =
        static_cast<qint64>(
            metrics.spacingLifecycleTimingValidatedRows);
    semanticIntegrity["completion_queue_depth_out_of_range_rows"] =
        static_cast<qint64>(
            metrics.completionQueueDepthOutOfRangeRows);
    semanticIntegrity["tear_classification_mismatches"] =
        static_cast<qint64>(metrics.tearClassificationMismatches);
    semanticIntegrity["tear_risk_flag_mismatches"] =
        static_cast<qint64>(metrics.tearRiskMismatches);
    semanticIntegrity["valid"] = semanticIntegrityReady;
    capture["row_semantic_integrity"] = semanticIntegrity;

    const uint64_t presentedFrames = metrics.presentedFrames;
    const auto exactScheduledDistribution =
        [scheduled = metrics.scheduled](const Distribution& distribution) {
            return scheduled != 0 &&
                distribution.count == scheduled &&
                distribution.maximum == 0;
        };
    const bool referenceDecisionStateExact =
        exactScheduledDistribution(metrics.referenceTargetDrift) &&
        exactScheduledDistribution(metrics.referenceSourceIntervalDrift) &&
        exactScheduledDistribution(metrics.referenceSourceTimeDrift) &&
        exactScheduledDistribution(metrics.referenceSourcePeriodDrift) &&
        exactScheduledDistribution(metrics.referenceReadyOffsetDrift) &&
        exactScheduledDistribution(metrics.referenceReadinessBudgetDrift) &&
        exactScheduledDistribution(metrics.referenceTimingBudgetDrift) &&
        exactScheduledDistribution(metrics.referenceRenderLeadDrift) &&
        exactScheduledDistribution(metrics.referenceRenderWakeLeadDrift) &&
        exactScheduledDistribution(metrics.referenceTargetWakeLeadDrift) &&
        exactScheduledDistribution(metrics.referenceGuardDrift) &&
        exactScheduledDistribution(metrics.referenceHeadroomDrift) &&
        exactScheduledDistribution(metrics.referenceRenderStartDrift) &&
        metrics.referenceLatchedPresentationMismatches == 0 &&
        metrics.referenceUsedRtpTimestampMismatches == 0 &&
        metrics.referenceCadenceEligibleMismatches == 0 &&
        metrics.referenceSourceRateChangedMismatches == 0 &&
        metrics.referencePhaseDiscontinuityMismatches == 0 &&
        metrics.referenceRebasedMismatches == 0;
    const bool referenceControllerDiagnosticsExact =
        metrics.traceSchema == 5 &&
        exactScheduledDistribution(metrics.referenceReadinessPhaseDrift) &&
        exactScheduledDistribution(metrics.referenceReadinessDemandDrift) &&
        exactScheduledDistribution(
            metrics.referenceAppliedReadinessReserveDrift) &&
        exactScheduledDistribution(metrics.referenceRenderBaselineDrift) &&
        exactScheduledDistribution(metrics.referenceRenderInsuranceDrift) &&
        exactScheduledDistribution(
            metrics.referencePacingLatencyBudgetDrift) &&
        exactScheduledDistribution(
            metrics.referenceCadenceSampleCountDrift) &&
        exactScheduledDistribution(
            metrics.referenceRateCandidateSampleCountDrift) &&
        exactScheduledDistribution(
            metrics.referenceReadinessSampleCountDrift) &&
        exactScheduledDistribution(
            metrics.referencePreparationSampleCountDrift) &&
        exactScheduledDistribution(
            metrics.referenceRenderSchedulerSampleCountDrift) &&
        exactScheduledDistribution(
            metrics.referenceTargetSchedulerSampleCountDrift) &&
        exactScheduledDistribution(
            metrics.referenceCleanSpacingFramesDrift) &&
        exactScheduledDistribution(metrics.referencePhaseErrorFramesDrift) &&
        metrics.referenceReadinessModelValidMismatches == 0;
    const bool nativeRenderAdapterIdentityReady =
        metrics.nativePresentContractTelemetryAvailable &&
        metrics.nativeDxgiPresentAttemptRows != 0 &&
        metrics.nativeRenderAdapterLuidValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeRenderAdapterLuidRelationshipMismatchRows == 0 &&
        metrics.nativeRenderAdapterIdentityMismatchRows == 0 &&
        metrics.nativeRenderAdapterLuidSnapshotMismatchRows == 0 &&
        metrics.haveNativeRenderAdapterLuidReference &&
        metrics.haveNativeDisplayTimingReference &&
        metrics.nativeRenderAdapterLuid ==
            metrics.nativeDisplaySourceAdapterLuid;
    const bool nativeDxgiCapabilityCoverageReady =
        metrics.nativeDxgiCapabilityTelemetryAvailable &&
        metrics.nativeDxgiPresentAttemptRows != 0 &&
        metrics.nativeTearingFeatureQueryResultValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeTearingFeatureQuerySuccessRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeTearingFeatureAllowsTearingRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeSwapChainDescQueryResultValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeSwapChainDescQuerySuccessRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeSwapChainFlipModelRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeSwapChainAllowsTearingRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeFullscreenStateQueryResultValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeFullscreenStateQuerySuccessRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeWindowBorderlessRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeWindowedSwapChainRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeDxgiCapabilityExactEligibleRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeDxgiCapabilityRelationshipMismatchRows == 0 &&
        metrics.nativeDxgiCapabilitySnapshotMismatchRows == 0 &&
        metrics.haveNativeDxgiCapabilityReference;
    const bool nativeOutcomeCoverageReady =
        metrics.nativeOutcomeTelemetryAvailable &&
        metrics.nativePresentContractTelemetryAvailable &&
        nativeDxgiCapabilityCoverageReady &&
        metrics.normalPresentAttemptRows != 0 &&
        metrics.nativePresentAttemptRows ==
            metrics.normalPresentAttemptRows &&
        metrics.nativeDxgiPresentAttemptRows ==
            metrics.normalPresentAttemptRows &&
        metrics.nativeVulkanPresentAttemptRows == 0 &&
        metrics.nativePresentResultValidRows ==
            metrics.nativePresentAttemptRows &&
        metrics.nativePresentParametersValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeVrrStateValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        nativeRenderAdapterIdentityReady &&
        metrics.nativeForegroundWindowRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.presentedNativePresentResultValidRows == presentedFrames &&
        metrics.presentedSubmissionIdQueryResultValidRows == presentedFrames &&
        metrics.presentedFrameStatsQueryResultValidRows == presentedFrames;
    const bool singleDesktopMonitorReady =
        metrics.nativeDesktopMonitorCounts.size() == 1 &&
        metrics.nativeDesktopMonitorCounts.value("1") ==
            metrics.nativeDxgiPresentAttemptRows;
    const VrrDisplaySignalConsistency nativeDisplaySignalConsistency =
        evaluateVrrDisplaySignalConsistency(
            metrics.nativeDisplaySignalPixelRateHz,
            metrics.nativeDisplaySignalHSyncNumerator,
            metrics.nativeDisplaySignalHSyncDenominator,
            metrics.nativeDisplaySignalVSyncNumerator,
            metrics.nativeDisplaySignalVSyncDenominator,
            metrics.nativeDisplaySignalTotalWidth,
            metrics.nativeDisplaySignalTotalHeight,
            kDisplaySignalConsistencyTolerancePpm);
    const uint64_t nativeDisplaySignalVideoStandard =
        metrics.nativeDisplaySignalAdditionalInfoRaw & 0xffffULL;
    const uint64_t nativeDisplaySignalVsyncDivider =
        (metrics.nativeDisplaySignalAdditionalInfoRaw >> 16) & 0x3fULL;
    const uint64_t nativeDisplaySignalReservedBits =
        (metrics.nativeDisplaySignalAdditionalInfoRaw >> 22) & 0x3ffULL;
    const bool nativeDisplaySignalHasNoVsyncDivider =
        nativeDisplaySignalVsyncDivider == 0 &&
        metrics.nativeDisplaySignalVsyncDividerRows == 0;
    const bool nativeDisplaySignalReservedBitsZero =
        nativeDisplaySignalReservedBits == 0 &&
        metrics.nativeDisplaySignalReservedInfoRows == 0;
    const uint64_t nativeDisplayUnknownPathFlags =
        metrics.nativeDisplayPathFlags & ~kDisplayConfigPathKnownFlags;
    const bool nativeDisplayPathFlagsKnown =
        nativeDisplayUnknownPathFlags == 0 &&
        metrics.nativeDisplayUnknownPathFlagRows == 0;
    const uint64_t nativeDisplaySignalRoundedHz =
        metrics.nativeDisplaySignalVSyncDenominator == 0 ? 0 :
            (metrics.nativeDisplaySignalVSyncNumerator +
             metrics.nativeDisplaySignalVSyncDenominator / 2) /
                metrics.nativeDisplaySignalVSyncDenominator;
    const bool nativeDisplaySignalRateMatchesCapture =
        capturedDisplayHz > 0 &&
        nativeDisplaySignalRoundedHz ==
            static_cast<uint64_t>(capturedDisplayHz) &&
        metrics.nativeDisplaySignalRateMismatchRows == 0;
    const bool nativeDisplayTimingCoverageReady =
        metrics.nativeDisplayTimingTelemetryAvailable &&
        metrics.nativeDxgiPresentAttemptRows != 0 &&
        metrics.nativeDisplayConfigQueryResultValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeDisplayConfigQuerySuccessRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeDisplayPathValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeDisplayTargetAvailableRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeDisplaySignalValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeDisplayTimingRelationshipMismatchRows == 0 &&
        metrics.nativeDisplayTimingSnapshotMismatchRows == 0 &&
        metrics.nativeDisplaySignalRateMismatchRows == 0 &&
        metrics.haveNativeDisplayTimingReference &&
        metrics.nativeDisplaySignalPeriodPs != 0 &&
        nativeDisplaySignalConsistency.inputsValid &&
        nativeDisplaySignalConsistency.withinTolerance &&
        nativeDisplaySignalRateMatchesCapture;
    const bool nativeDisplayTimingProgressive =
        metrics.haveNativeDisplayTimingReference &&
        metrics.nativeDisplaySignalScanLineOrdering ==
            kDisplayConfigProgressiveScan &&
        metrics.nativeDisplayNonProgressiveRows == 0;
    const bool nativeDisplayRotationSupported =
        metrics.haveNativeDisplayTimingReference &&
        metrics.nativeDisplayRotation ==
            kDisplayConfigRotationIdentity &&
        metrics.nativeDisplayNonIdentityRotationRows == 0;
    const bool nativeDisplayScalingSupported =
        metrics.haveNativeDisplayTimingReference &&
        metrics.nativeDisplayScaling ==
            kDisplayConfigScalingIdentity &&
        metrics.nativeDisplayNonIdentityScalingRows == 0;
    uint64_t nativeDisplaySignalActiveScanoutPs = 0;
    const bool nativeDisplaySignalActiveScanoutPsResolved =
        metrics.haveNativeDisplayTimingReference &&
        vrrActiveScanoutPicosecondsFromSignal(
            metrics.nativeDisplaySignalVSyncNumerator,
            metrics.nativeDisplaySignalVSyncDenominator,
            metrics.nativeDisplaySignalActiveWidth,
            metrics.nativeDisplaySignalActiveHeight,
            metrics.nativeDisplaySignalTotalWidth,
            metrics.nativeDisplaySignalTotalHeight,
            nativeDisplaySignalActiveScanoutPs);
    uint64_t nativeDisplaySignalVerticalActivePs = 0;
    const bool nativeDisplaySignalVerticalActivePsResolved =
        metrics.haveNativeDisplayTimingReference &&
        vrrActiveScanoutPicosecondsFromSignal(
            metrics.nativeDisplaySignalVSyncNumerator,
            metrics.nativeDisplaySignalVSyncDenominator,
            metrics.nativeDisplaySignalTotalWidth,
            metrics.nativeDisplaySignalActiveHeight,
            metrics.nativeDisplaySignalTotalWidth,
            metrics.nativeDisplaySignalTotalHeight,
            nativeDisplaySignalVerticalActivePs);
    const uint64_t nativeDisplayFinalActiveLineHorizontalBlankPs =
        nativeDisplaySignalVerticalActivePs >=
                nativeDisplaySignalActiveScanoutPs ?
            nativeDisplaySignalVerticalActivePs -
                nativeDisplaySignalActiveScanoutPs : 0;
    uint64_t nativeDisplaySignalLinePeriodPs = 0;
    const bool nativeDisplaySignalLinePeriodPsResolved =
        metrics.haveNativeDisplayTimingReference &&
        vrrActiveScanoutPicosecondsFromSignal(
            metrics.nativeDisplaySignalVSyncNumerator,
            metrics.nativeDisplaySignalVSyncDenominator,
            metrics.nativeDisplaySignalTotalWidth,
            1,
            metrics.nativeDisplaySignalTotalWidth,
            metrics.nativeDisplaySignalTotalHeight,
            nativeDisplaySignalLinePeriodPs);
    uint64_t nativeDisplaySignalActiveScanoutUs = 0;
    const bool nativeDisplaySignalActiveScanoutResolved =
        metrics.haveNativeDisplayTimingReference &&
        vrrActiveScanoutMicrosecondsFromSignal(
            metrics.nativeDisplaySignalVSyncNumerator,
            metrics.nativeDisplaySignalVSyncDenominator,
            metrics.nativeDisplaySignalActiveWidth,
            metrics.nativeDisplaySignalActiveHeight,
            metrics.nativeDisplaySignalTotalWidth,
            metrics.nativeDisplaySignalTotalHeight,
            nativeDisplaySignalActiveScanoutUs);
    const uint64_t resolvedConfiguredActiveScanoutUs =
        scenario.display.activeScanoutUs != 0 ?
            scenario.display.activeScanoutUs :
            nativeDisplaySignalActiveScanoutUs;
    const uint64_t configuredActiveScanoutDifferenceUs =
        resolvedConfiguredActiveScanoutUs >=
                nativeDisplaySignalActiveScanoutUs ?
            resolvedConfiguredActiveScanoutUs -
                nativeDisplaySignalActiveScanoutUs :
            nativeDisplaySignalActiveScanoutUs -
                resolvedConfiguredActiveScanoutUs;
    const bool configuredActiveScanoutMatchesSignal =
        nativeDisplaySignalActiveScanoutResolved &&
        configuredActiveScanoutDifferenceUs <= 1;
    const uint64_t resolvedConfiguredActiveScanoutPs =
        scenario.display.activeScanoutPs != 0 ?
            scenario.display.activeScanoutPs :
            nativeDisplaySignalActiveScanoutPs;
    const bool preciseActiveScanoutMatchesSignal =
        nativeDisplaySignalActiveScanoutPsResolved &&
        resolvedConfiguredActiveScanoutPs ==
            nativeDisplaySignalActiveScanoutPs;
    const bool nativeDisplayPathRefreshMatchesSignal =
        vrrRefreshRationalsEqual(
            metrics.nativeDisplayPathRefreshNumerator,
            metrics.nativeDisplayPathRefreshDenominator,
            metrics.nativeDisplaySignalVSyncNumerator,
            metrics.nativeDisplaySignalVSyncDenominator) &&
        metrics.nativeDisplayPathSignalRateMismatchRows == 0;
    const bool nativeVblankVirtualizationDisableReady =
        metrics.nativeVblankVirtualizationTelemetryAvailable &&
        metrics.nativeDxgiPresentAttemptRows != 0 &&
        metrics.nativeVblankVirtualizationProbeCompleteRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeVblankVirtualizationCallAvailableRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeVblankVirtualizationResultValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeVblankVirtualizationSuccessRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeVblankVirtualizationDisabledRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeVblankVirtualizationRelationshipMismatchRows == 0;
    const bool nativeDisplayTimingNotVirtualized =
        nativeDisplayTimingCoverageReady &&
        metrics.nativeDisplayDrrBoostRows == 0 &&
        nativeDisplayPathFlagsKnown &&
        nativeDisplayPathRefreshMatchesSignal &&
        nativeDisplaySignalHasNoVsyncDivider &&
        nativeDisplaySignalReservedBitsZero &&
        nativeVblankVirtualizationDisableReady;
    const bool nativeRasterSampleAccountingReady =
        metrics.nativeRasterBeforeQuerySuccessRows ==
            saturatingAdd(
                metrics.nativeRasterBeforeVerticalBlankRows,
                metrics.nativeRasterBeforeActiveScanoutRows) &&
        metrics.nativeRasterAfterQuerySuccessRows ==
            saturatingAdd(
                metrics.nativeRasterAfterVerticalBlankRows,
                metrics.nativeRasterAfterActiveScanoutRows);
    const bool nativeRasterSamplingReady =
        metrics.nativeRasterTelemetryAvailable &&
        metrics.nativeDxgiPresentAttemptRows != 0 &&
        metrics.nativeRasterSamplingRequestedRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeRasterOpenResultValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeRasterSourceValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeRasterBeforeQueryResultValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeRasterBeforeQuerySuccessRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeRasterAfterQueryResultValidRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeRasterAfterQuerySuccessRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeRasterRelationshipMismatchRows == 0 &&
        metrics.nativeRasterTimingOrderMismatchRows == 0 &&
        metrics.nativeRasterSourceIdMismatchRows == 0 &&
        nativeRasterSampleAccountingReady;
    const uint64_t nativeRasterSuccessfulSamples =
        saturatingAdd(
            metrics.nativeRasterBeforeQuerySuccessRows,
            metrics.nativeRasterAfterQuerySuccessRows);
    const bool nativeRasterScanLineScaleReady =
        metrics.nativeRasterScanLineScaleInference.valid &&
        metrics.nativeRasterScanLineScaleInference.scale != 0 &&
        metrics.nativeRasterScanLineScaleInference.samples ==
            nativeRasterSuccessfulSamples;
    const bool nativeRasterSignalRangeReady =
        nativeRasterSuccessfulSamples != 0 &&
        nativeRasterScanLineScaleReady &&
        metrics.nativeRasterSignalRangeCheckedSamples ==
            nativeRasterSuccessfulSamples &&
        metrics.nativeRasterSignalRangeMismatchSamples == 0;
    const bool nativeRasterModelAnchorCoverageReady =
        metrics.nativeRasterModelObservationSamples != 0 &&
        static_cast<long double>(
            metrics.nativeRasterModelAnchoredSamples) /
            static_cast<long double>(
                metrics.nativeRasterModelObservationSamples) >= 0.99L;
    const bool nativeRasterModelAnchorSourceAccountingReady =
        saturatingAdd(
            metrics.nativeRasterModelCurrentPostAnchorSamples,
            metrics.nativeRasterModelPriorAnchorSamples) ==
        metrics.nativeRasterModelAnchoredSamples;
    const bool unifiedRasterAnchorIntegrityReady =
        metrics.unifiedAnchorSequenceRegressions == 0 &&
        metrics.unifiedAnchorSameSequenceTimestampMismatches == 0 &&
        metrics.unifiedAnchorNonadvancingTime == 0 &&
        metrics.unifiedAnchorImplausiblyShortIntervals == 0;
    const uint64_t nativeRasterComparableHypothesisSamples =
        saturatingAdd(
            metrics.nativeRasterVrrLockedComparableSamples,
            metrics.nativeRasterFreeRunningComparableSamples);
    const bool nativeRasterScanLineModelValidationReady =
        nativeDisplaySignalVerticalActivePsResolved &&
        nativeDisplaySignalLinePeriodPsResolved &&
        metrics.nativeRasterScanLineEnvelopeComparableSamples >=
            kMinimumExactRasterValidationSamples &&
        metrics.nativeRasterScanLineEnvelopeMatchedSamples ==
            metrics.nativeRasterScanLineEnvelopeComparableSamples &&
        metrics.nativeRasterScanLineEnvelopeContradictions == 0;
    const bool nativeRasterModelValidationReady =
        metrics.nativeRasterModelObservationSamples ==
            nativeRasterSuccessfulSamples &&
        nativeRasterModelAnchorCoverageReady &&
        nativeRasterModelAnchorSourceAccountingReady &&
        unifiedRasterAnchorIntegrityReady &&
        nativeRasterComparableHypothesisSamples >=
            kMinimumExactRasterValidationSamples &&
        metrics.nativeRasterEnvelopeContradictions == 0 &&
        nativeRasterScanLineModelValidationReady;
    const uint64_t resolvedCounterfactualPeriodPs =
        scenario.display.scanoutPeriodPs != 0 ?
            scenario.display.scanoutPeriodPs :
            metrics.nativeDisplaySignalPeriodPs;
    const uint64_t counterfactualPeriodDifferencePs =
        resolvedCounterfactualPeriodPs >=
                metrics.nativeDisplaySignalPeriodPs ?
            resolvedCounterfactualPeriodPs -
                metrics.nativeDisplaySignalPeriodPs :
            metrics.nativeDisplaySignalPeriodPs -
                resolvedCounterfactualPeriodPs;
    const bool counterfactualPeriodMatchesSignal =
        resolvedCounterfactualPeriodPs != 0 &&
        metrics.nativeDisplaySignalPeriodPs != 0 &&
        counterfactualPeriodDifferencePs <= 1;
    const uint64_t nativeDisplaySignalRoundedPeriodUs =
        metrics.nativeDisplaySignalPeriodPs / 1000000ULL +
        (metrics.nativeDisplaySignalPeriodPs % 1000000ULL >=
                500000ULL ? 1ULL : 0ULL);
    const uint64_t resolvedConfiguredScanoutPeriodUs =
        scenario.display.scanoutPeriodUs != 0 ?
            scenario.display.scanoutPeriodUs :
            nativeDisplaySignalRoundedPeriodUs;
    const uint64_t configuredPeriodDifferenceUs =
        resolvedConfiguredScanoutPeriodUs >=
                nativeDisplaySignalRoundedPeriodUs ?
            resolvedConfiguredScanoutPeriodUs -
                nativeDisplaySignalRoundedPeriodUs :
            nativeDisplaySignalRoundedPeriodUs -
                resolvedConfiguredScanoutPeriodUs;
    const bool configuredScanoutPeriodMatchesSignal =
        metrics.nativeDisplaySignalPeriodPs != 0 &&
        configuredPeriodDifferenceUs <= 1;
    const bool rawSyncQpcTranslationAccountingReady =
        metrics.latchValidRows != 0 &&
        saturatingAdd(
            metrics.rawSyncQpcTranslationBaselines,
            metrics.rawSyncQpcTranslationComparisons) ==
                metrics.latchValidRows;
    const bool qpcCorrelationIntegrityReady =
        metrics.qpcCorrelationTelemetryAvailable &&
        metrics.haveQpcCorrelationReference &&
        metrics.qpcCorrelationValidRows ==
            metrics.rawSyncQpcValidRows &&
        metrics.qpcCorrelationRelationshipMismatchRows == 0 &&
        metrics.qpcCorrelationReferenceMismatchRows == 0 &&
        metrics.qpcCorrelationTranslationMismatchRows == 0 &&
        metrics.qpcCorrelationUncertaintyInvalidRows == 0;
    const bool nativeOutcomeAndQpcIntegrityReady =
        nativeOutcomeCoverageReady &&
        metrics.nativeOutcomeRelationshipMismatchRows == 0 &&
        metrics.nativePresentParameterMismatchRows == 0 &&
        metrics.nativeVrrStateMismatchRows == 0 &&
        metrics.nativeDxgiCapabilityRelationshipMismatchRows == 0 &&
        metrics.nativeDxgiCapabilitySnapshotMismatchRows == 0 &&
        metrics.nativeVblankVirtualizationTelemetryAvailable &&
        metrics.nativeVblankVirtualizationRelationshipMismatchRows == 0 &&
        singleDesktopMonitorReady &&
        nativeDisplayTimingCoverageReady &&
        metrics.rawSyncQpcValidRows >= metrics.latchValidRows &&
        metrics.frameStatsSuccessWithoutRawSyncQpcRows == 0 &&
        metrics.rawSyncQpcWithoutTranslatedRows == 0 &&
        metrics.rawSyncQpcFrequencyMismatchRows == 0 &&
        rawSyncQpcTranslationAccountingReady &&
        metrics.rawSyncQpcTranslationComparisons != 0 &&
        metrics.rawSyncQpcTranslationMismatchRows == 0 &&
        qpcCorrelationIntegrityReady;
    QJsonObject telemetryCoverage;
    telemetryCoverage["deep_trace_rows"] = validityObject(
        metrics.deepTraceRows, metrics.delivered);
    telemetryCoverage["submission_id"] = validityObject(
        metrics.submissionIdValidRows, presentedFrames);
    telemetryCoverage["presented_submission_id"] = validityObject(
        metrics.presentedSubmissionIdValidRows, presentedFrames);
    telemetryCoverage["latch_sample"] = validityObject(
        metrics.latchValidRows, presentedFrames);
    telemetryCoverage["native_present_timing"] = validityObject(
        metrics.nativePresentTimingValidRows, presentedFrames);
    telemetryCoverage["presented_native_present_timing"] = validityObject(
        metrics.presentedNativePresentTimingValidRows, presentedFrames);
    telemetryCoverage["presenter_submission_timing_fields_available"] =
        metrics.presenterSubmissionTimingTelemetryAvailable;
    telemetryCoverage["presenter_submission_timestamp_used_rows"] =
        static_cast<qint64>(
            metrics.presenterSubmissionTimestampUsedRows);
    telemetryCoverage["spacing_correction_fields_available"] =
        metrics.spacingCorrectionTelemetryAvailable;
    telemetryCoverage["spacing_lifecycle_timing_fields_available"] =
        metrics.spacingLifecycleTimingTelemetryAvailable;
    QJsonObject spacingLifecycleTiming;
    spacingLifecycleTiming["validated_normal_present_attempt_rows"] =
        static_cast<qint64>(
            metrics.spacingLifecycleTimingValidatedRows);
    spacingLifecycleTiming["relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.spacingLifecycleTimingRelationshipMismatchRows);
    spacingLifecycleTiming["spacing_check_to_recheck_us"] =
        distributionObject(metrics.observedSpacingCheckToRecheck);
    telemetryCoverage["spacing_lifecycle_timing"] =
        spacingLifecycleTiming;
    telemetryCoverage["completion_queue_depth_field_available"] =
        metrics.completionQueueDepthTelemetryAvailable;
    telemetryCoverage["present_timing_integrity_fields_available"] =
        metrics.presentTimingIntegrityTelemetryAvailable;
    telemetryCoverage["presented_present_operation_integrity"] =
        validityObject(
            metrics.presentedPresentOperationIntegrityRows, presentedFrames);
    QJsonObject nativeOutcomeIntegrity;
    nativeOutcomeIntegrity["fields_available"] =
        metrics.nativeOutcomeTelemetryAvailable;
    nativeOutcomeIntegrity["present_contract_fields_available"] =
        metrics.nativePresentContractTelemetryAvailable;
    nativeOutcomeIntegrity["normal_present_attempt_rows"] =
        static_cast<qint64>(metrics.normalPresentAttemptRows);
    nativeOutcomeIntegrity["native_present_attempt_rows"] =
        static_cast<qint64>(metrics.nativePresentAttemptRows);
    nativeOutcomeIntegrity["dxgi_present_attempt_rows"] =
        static_cast<qint64>(metrics.nativeDxgiPresentAttemptRows);
    nativeOutcomeIntegrity["vulkan_present_attempt_rows"] =
        static_cast<qint64>(metrics.nativeVulkanPresentAttemptRows);
    nativeOutcomeIntegrity["native_present_result"] = validityObject(
        metrics.nativePresentResultValidRows,
        metrics.nativePresentAttemptRows);
    nativeOutcomeIntegrity["native_present_parameters"] = validityObject(
        metrics.nativePresentParametersValidRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeOutcomeIntegrity["native_vrr_state"] = validityObject(
        metrics.nativeVrrStateValidRows,
        metrics.nativeDxgiPresentAttemptRows);
    QJsonObject nativeDxgiCapability;
    nativeDxgiCapability["fields_available"] =
        metrics.nativeDxgiCapabilityTelemetryAvailable;
    nativeDxgiCapability["feature_query_result"] = validityObject(
        metrics.nativeTearingFeatureQueryResultValidRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["feature_query_s_ok"] = validityObject(
        metrics.nativeTearingFeatureQuerySuccessRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["feature_allows_tearing"] = validityObject(
        metrics.nativeTearingFeatureAllowsTearingRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["actual_swap_chain_desc_result"] =
        validityObject(
            metrics.nativeSwapChainDescQueryResultValidRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["actual_swap_chain_desc_s_ok"] =
        validityObject(
            metrics.nativeSwapChainDescQuerySuccessRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["actual_swap_chain_flip_model"] =
        validityObject(
            metrics.nativeSwapChainFlipModelRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["actual_swap_chain_allows_tearing"] =
        validityObject(
            metrics.nativeSwapChainAllowsTearingRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["fullscreen_state_query_result"] =
        validityObject(
            metrics.nativeFullscreenStateQueryResultValidRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["fullscreen_state_query_s_ok"] =
        validityObject(
            metrics.nativeFullscreenStateQuerySuccessRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["swap_chain_is_windowed"] =
        validityObject(
            metrics.nativeWindowedSwapChainRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["sdl_window_is_fullscreen_desktop"] =
        validityObject(
            metrics.nativeWindowBorderlessRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["exact_eligible"] = validityObject(
        metrics.nativeDxgiCapabilityExactEligibleRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeDxgiCapability["snapshot_epochs"] =
        static_cast<qint64>(
            metrics.nativeDxgiCapabilitySnapshotEpochs);
    nativeDxgiCapability["relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeDxgiCapabilityRelationshipMismatchRows);
    nativeDxgiCapability[
        "snapshot_change_without_rebase_rows"] =
        static_cast<qint64>(
            metrics.nativeDxgiCapabilitySnapshotMismatchRows);
    nativeDxgiCapability[
        "raw_window_flag_difference_from_epoch_baseline_rows"] =
        static_cast<qint64>(
            metrics.nativeWindowRawFlagDifferenceRows);
    nativeDxgiCapability[
        "feature_query_result_counts_signed_decimal"] =
        countObject(metrics.nativeTearingFeatureQueryResults);
    nativeDxgiCapability[
        "swap_chain_desc_result_counts_signed_decimal"] =
        countObject(metrics.nativeSwapChainDescQueryResults);
    nativeDxgiCapability["swap_chain_flags_counts_decimal"] =
        countObject(metrics.nativeSwapChainFlagsObserved);
    nativeDxgiCapability["swap_chain_swap_effect_counts_decimal"] =
        countObject(metrics.nativeSwapChainSwapEffects);
    nativeDxgiCapability[
        "fullscreen_state_result_counts_signed_decimal"] =
        countObject(metrics.nativeFullscreenStateQueryResults);
    nativeDxgiCapability["sdl_window_flags_counts_decimal"] =
        countObject(metrics.nativeWindowFlagsObserved);
    QJsonObject nativeDxgiCapabilitySnapshot;
    nativeDxgiCapabilitySnapshot[
        "feature_query_result_signed_decimal"] =
        QString::number(
            metrics.nativeTearingFeatureQueryResult);
    nativeDxgiCapabilitySnapshot["feature_allows_tearing"] =
        metrics.nativeTearingFeatureAllowsTearing;
    nativeDxgiCapabilitySnapshot[
        "swap_chain_desc_result_signed_decimal"] =
        QString::number(
            metrics.nativeSwapChainDescQueryResult);
    nativeDxgiCapabilitySnapshot["swap_chain_flags_decimal"] =
        QString::number(metrics.nativeSwapChainFlags);
    nativeDxgiCapabilitySnapshot["swap_chain_swap_effect_decimal"] =
        QString::number(metrics.nativeSwapChainSwapEffect);
    nativeDxgiCapabilitySnapshot[
        "fullscreen_state_result_signed_decimal"] =
        QString::number(
            metrics.nativeFullscreenStateQueryResult);
    nativeDxgiCapabilitySnapshot["fullscreen_exclusive"] =
        metrics.nativeFullscreenExclusive;
    nativeDxgiCapabilitySnapshot["sdl_window_flags_decimal"] =
        QString::number(metrics.nativeWindowFlags);
    nativeDxgiCapability["latest_epoch_snapshot"] =
        nativeDxgiCapabilitySnapshot;
    nativeDxgiCapability["valid"] =
        nativeDxgiCapabilityCoverageReady;
    nativeDxgiCapability["semantics"] =
        "Replay independently audits the exact CheckFeatureSupport HRESULT and returned allow-tearing BOOL, the actual created swap-chain GetDesc1 HRESULT/flags/swap effect, GetFullscreenState HRESULT/exclusive state, and raw SDL window flags. Strict readiness requires S_OK, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING (2048), flip sequential/discard (3/4), windowed state, SDL fullscreen-desktop flags (4097), stable capability-relevant values inside each explicit display epoch, and agreement with every derived eligibility boolean. Volatile SDL focus/visibility flag changes are reported separately but do not falsely invalidate an otherwise identical swap-chain capability snapshot";
    nativeOutcomeIntegrity["dxgi_capability_evidence"] =
        nativeDxgiCapability;
    nativeOutcomeIntegrity["render_adapter_luid"] = validityObject(
        metrics.nativeRenderAdapterLuidValidRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeOutcomeIntegrity["render_adapter_luid_unsigned_decimal"] =
        QString::number(metrics.nativeRenderAdapterLuid);
    nativeOutcomeIntegrity["render_adapter_matches_display_source"] =
        nativeRenderAdapterIdentityReady;
    nativeOutcomeIntegrity["foreground_window"] = validityObject(
        metrics.nativeForegroundWindowRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeOutcomeIntegrity["presented_native_present_result"] =
        validityObject(
            metrics.presentedNativePresentResultValidRows, presentedFrames);
    nativeOutcomeIntegrity["presented_submission_id_query_result"] =
        validityObject(
            metrics.presentedSubmissionIdQueryResultValidRows,
            presentedFrames);
    nativeOutcomeIntegrity["presented_frame_statistics_query_result"] =
        validityObject(
            metrics.presentedFrameStatsQueryResultValidRows,
            presentedFrames);
    QJsonObject postPresentQueryTiming;
    postPresentQueryTiming["fields_available"] =
        metrics.postPresentQueryTimingTelemetryAvailable;
    postPresentQueryTiming["presented_valid_rows"] =
        static_cast<qint64>(
            metrics.presentedPostPresentQueryTimingValidRows);
    postPresentQueryTiming["relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.postPresentQueryTimingRelationshipMismatchRows);
    postPresentQueryTiming["submission_id_query_call_us"] =
        distributionObject(metrics.observedSubmissionIdQueryCall);
    postPresentQueryTiming["frame_statistics_query_call_us"] =
        distributionObject(metrics.observedFrameStatsQueryCall);
    postPresentQueryTiming["native_present_to_presenter_return_us"] =
        distributionObject(metrics.observedPostPresentObservationTail);
    nativeOutcomeIntegrity["post_present_query_timing"] =
        postPresentQueryTiming;
    nativeOutcomeIntegrity["native_present_result_counts_signed_decimal"] =
        countObject(metrics.nativePresentResults);
    nativeOutcomeIntegrity["native_backend_counts_decimal"] =
        countObject(metrics.nativeBackendCounts);
    nativeOutcomeIntegrity["native_present_flags_counts_decimal"] =
        countObject(metrics.nativePresentFlags);
    nativeOutcomeIntegrity["native_vrr_fallback_reason_counts_decimal"] =
        countObject(metrics.nativeVrrFallbackReasons);
    nativeOutcomeIntegrity["desktop_monitor_count_observations"] =
        countObject(metrics.nativeDesktopMonitorCounts);
    nativeOutcomeIntegrity[
        "submission_id_query_result_counts_signed_decimal"] =
        countObject(metrics.submissionIdQueryResults);
    nativeOutcomeIntegrity[
        "frame_statistics_query_result_counts_signed_decimal"] =
        countObject(metrics.frameStatsQueryResults);
    nativeOutcomeIntegrity["relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeOutcomeRelationshipMismatchRows);
    nativeOutcomeIntegrity["present_parameter_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativePresentParameterMismatchRows);
    nativeOutcomeIntegrity["vrr_state_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeVrrStateMismatchRows);
    nativeOutcomeIntegrity[
        "render_adapter_luid_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRenderAdapterLuidRelationshipMismatchRows);
    nativeOutcomeIntegrity[
        "render_adapter_display_source_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRenderAdapterIdentityMismatchRows);
    nativeOutcomeIntegrity["render_adapter_luid_snapshot_change_rows"] =
        static_cast<qint64>(
            metrics.nativeRenderAdapterLuidSnapshotMismatchRows);
    nativeOutcomeIntegrity["single_desktop_monitor"] =
        singleDesktopMonitorReady;
    nativeOutcomeIntegrity["result_semantics"] =
        "DXGI records signed HRESULT and only S_OK (0) is treated as a displayed submission; positive statuses such as occlusion and negative failures are retained as not presented. Vulkan maps libplacebo boolean submit to 0 success or -1 failure";
    nativeOutcomeIntegrity["backend_semantics"] =
        "1=DXGI, 2=Vulkan; strict raster diagnostics require every normal present attempt to be DXGI";
    nativeOutcomeIntegrity["present_contract"] =
        "Unlatched DXGI Presents require sync interval 0 and flags 512 when session_allow_tearing is true (including historical captures without the field), otherwise flags 0. Latched Presents require flags 0 and sync interval 0 or 1 for historical compatibility. Disabling session permission does not remove swapchain tearing capability. Renderer eligibility must remain valid with no fallback; adapter LUIDs must remain stable and match.";
    nativeOutcomeIntegrity["frame_statistics_topology_scope"] =
        "DXGI documents frame statistics as unreliable in many multiple-monitor scenarios and with other fullscreen apps; strict diagnostic readiness therefore requires one desktop monitor and Moonlight foreground on every DXGI Present, but cannot enumerate every background fullscreen process";
    QJsonObject nativeVblankVirtualization;
    nativeVblankVirtualization["fields_available"] =
        metrics.nativeVblankVirtualizationTelemetryAvailable;
    nativeVblankVirtualization["startup_probe_complete"] =
        validityObject(
            metrics.nativeVblankVirtualizationProbeCompleteRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeVblankVirtualization["call_available"] =
        validityObject(
            metrics.nativeVblankVirtualizationCallAvailableRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeVblankVirtualization["call_result"] =
        validityObject(
            metrics.nativeVblankVirtualizationResultValidRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeVblankVirtualization["call_succeeded"] =
        validityObject(
            metrics.nativeVblankVirtualizationSuccessRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeVblankVirtualization["disabled"] =
        validityObject(
            metrics.nativeVblankVirtualizationDisabledRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeVblankVirtualization[
        "result_counts_signed_decimal"] =
        countObject(
            metrics.nativeVblankVirtualizationResults);
    nativeVblankVirtualization["relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeVblankVirtualizationRelationshipMismatchRows);
    nativeVblankVirtualization["valid"] =
        nativeVblankVirtualizationDisableReady;
    nativeVblankVirtualization["scope"] =
        "Moonlight probes and calls DXGIDisableVBlankVirtualization once before any Qt swapchain. Raster readiness requires the API to exist and return exactly S_OK; replay does not infer success from matching refresh rates";
    nativeOutcomeIntegrity["vblank_virtualization"] =
        nativeVblankVirtualization;
    QJsonObject nativeDisplayTiming;
    nativeDisplayTiming["fields_available"] =
        metrics.nativeDisplayTimingTelemetryAvailable;
    nativeDisplayTiming["display_config_query_result"] =
        validityObject(
            metrics.nativeDisplayConfigQueryResultValidRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDisplayTiming["display_config_query_success"] =
        validityObject(
            metrics.nativeDisplayConfigQuerySuccessRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDisplayTiming["matched_active_path"] =
        validityObject(
            metrics.nativeDisplayPathValidRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDisplayTiming["target_available"] =
        validityObject(
            metrics.nativeDisplayTargetAvailableRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDisplayTiming["physical_signal_timing"] =
        validityObject(
            metrics.nativeDisplaySignalValidRows,
            metrics.nativeDxgiPresentAttemptRows);
    nativeDisplayTiming["relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplayTimingRelationshipMismatchRows);
    nativeDisplayTiming["snapshot_change_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplayTimingSnapshotMismatchRows);
    nativeDisplayTiming[
        "physical_signal_rate_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplaySignalRateMismatchRows);
    nativeDisplayTiming["unknown_path_flag_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplayUnknownPathFlagRows);
    nativeDisplayTiming[
        "path_physical_signal_rate_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplayPathSignalRateMismatchRows);
    nativeDisplayTiming["non_progressive_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplayNonProgressiveRows);
    nativeDisplayTiming["non_identity_rotation_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplayNonIdentityRotationRows);
    nativeDisplayTiming["non_identity_scaling_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplayNonIdentityScalingRows);
    nativeDisplayTiming["signal_vsync_divider_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplaySignalVsyncDividerRows);
    nativeDisplayTiming["signal_reserved_info_rows"] =
        static_cast<qint64>(
            metrics.nativeDisplaySignalReservedInfoRows);
    nativeDisplayTiming[
        "configured_scanout_period_mismatch_rows"] =
        static_cast<qint64>(
            metrics.configuredScanoutPeriodMismatchRows);
    nativeDisplayTiming[
        "configured_active_scanout_mismatch_rows"] =
        static_cast<qint64>(
            metrics.configuredActiveScanoutMismatchRows);
    nativeDisplayTiming["drr_boost_rows"] =
        static_cast<qint64>(metrics.nativeDisplayDrrBoostRows);
    nativeDisplayTiming["path_flags"] =
        QString::number(metrics.nativeDisplayPathFlags);
    nativeDisplayTiming["path_known_flags_mask"] =
        QString::number(kDisplayConfigPathKnownFlags);
    nativeDisplayTiming["path_unknown_flags"] =
        QString::number(nativeDisplayUnknownPathFlags);
    nativeDisplayTiming["path_unknown_flags_zero"] =
        nativeDisplayPathFlagsKnown;
    nativeDisplayTiming["source_adapter_luid"] =
        QString::number(
            metrics.nativeDisplaySourceAdapterLuid);
    nativeDisplayTiming["source_id"] =
        static_cast<qint64>(metrics.nativeDisplaySourceId);
    nativeDisplayTiming["target_adapter_luid"] =
        QString::number(
            metrics.nativeDisplayTargetAdapterLuid);
    nativeDisplayTiming["target_id"] =
        static_cast<qint64>(metrics.nativeDisplayTargetId);
    nativeDisplayTiming["output_technology"] =
        static_cast<qint64>(
            metrics.nativeDisplayOutputTechnology);
    nativeDisplayTiming["rotation"] =
        static_cast<qint64>(metrics.nativeDisplayRotation);
    nativeDisplayTiming["scaling"] =
        static_cast<qint64>(metrics.nativeDisplayScaling);
    nativeDisplayTiming["path_refresh_numerator"] =
        QString::number(
            metrics.nativeDisplayPathRefreshNumerator);
    nativeDisplayTiming["path_refresh_denominator"] =
        QString::number(
            metrics.nativeDisplayPathRefreshDenominator);
    nativeDisplayTiming["signal_pixel_rate_hz"] =
        QString::number(metrics.nativeDisplaySignalPixelRateHz);
    nativeDisplayTiming["signal_hsync_numerator"] =
        QString::number(
            metrics.nativeDisplaySignalHSyncNumerator);
    nativeDisplayTiming["signal_hsync_denominator"] =
        QString::number(
            metrics.nativeDisplaySignalHSyncDenominator);
    nativeDisplayTiming["signal_vsync_numerator"] =
        QString::number(
            metrics.nativeDisplaySignalVSyncNumerator);
    nativeDisplayTiming["signal_vsync_denominator"] =
        QString::number(
            metrics.nativeDisplaySignalVSyncDenominator);
    nativeDisplayTiming["signal_period_ps"] =
        QString::number(metrics.nativeDisplaySignalPeriodPs);
    nativeDisplayTiming["signal_consistency_tolerance_ppm"] =
        QString::number(kDisplaySignalConsistencyTolerancePpm);
    nativeDisplayTiming["signal_consistency_inputs_valid"] =
        nativeDisplaySignalConsistency.inputsValid;
    nativeDisplayTiming["signal_pixel_rate_to_hsync_error_ppm"] =
        QString::number(
            nativeDisplaySignalConsistency.pixelRateToHsyncErrorPpm);
    nativeDisplayTiming["signal_hsync_to_vsync_error_ppm"] =
        QString::number(
            nativeDisplaySignalConsistency.hsyncToVsyncErrorPpm);
    nativeDisplayTiming["signal_pixel_rate_to_vsync_error_ppm"] =
        QString::number(
            nativeDisplaySignalConsistency.pixelRateToVsyncErrorPpm);
    nativeDisplayTiming["signal_internally_consistent"] =
        nativeDisplaySignalConsistency.withinTolerance;
    nativeDisplayTiming["signal_active_width"] =
        static_cast<qint64>(
            metrics.nativeDisplaySignalActiveWidth);
    nativeDisplayTiming["signal_active_height"] =
        static_cast<qint64>(
            metrics.nativeDisplaySignalActiveHeight);
    nativeDisplayTiming["signal_total_width"] =
        static_cast<qint64>(
            metrics.nativeDisplaySignalTotalWidth);
    nativeDisplayTiming["signal_total_height"] =
        static_cast<qint64>(
            metrics.nativeDisplaySignalTotalHeight);
    nativeDisplayTiming["signal_additional_info_raw"] =
        QString::number(
            metrics.nativeDisplaySignalAdditionalInfoRaw);
    nativeDisplayTiming["signal_video_standard"] =
        QString::number(nativeDisplaySignalVideoStandard);
    nativeDisplayTiming["signal_vsync_frequency_divider"] =
        QString::number(nativeDisplaySignalVsyncDivider);
    nativeDisplayTiming["signal_reserved_bits"] =
        QString::number(nativeDisplaySignalReservedBits);
    nativeDisplayTiming["signal_vsync_frequency_divider_zero"] =
        nativeDisplaySignalHasNoVsyncDivider;
    nativeDisplayTiming["signal_reserved_bits_zero"] =
        nativeDisplaySignalReservedBitsZero;
    nativeDisplayTiming["signal_active_scanout_us"] =
        QString::number(nativeDisplaySignalActiveScanoutUs);
    nativeDisplayTiming["signal_active_scanout_ps"] =
        QString::number(nativeDisplaySignalActiveScanoutPs);
    nativeDisplayTiming["signal_active_scanout_resolved"] =
        nativeDisplaySignalActiveScanoutResolved;
    nativeDisplayTiming["signal_active_scanout_ps_resolved"] =
        nativeDisplaySignalActiveScanoutPsResolved;
    nativeDisplayTiming["signal_vertical_active_interval_ps"] =
        QString::number(nativeDisplaySignalVerticalActivePs);
    nativeDisplayTiming["signal_vertical_active_interval_ps_resolved"] =
        nativeDisplaySignalVerticalActivePsResolved;
    nativeDisplayTiming["signal_final_active_line_horizontal_blank_ps"] =
        QString::number(
            nativeDisplayFinalActiveLineHorizontalBlankPs);
    nativeDisplayTiming["signal_line_period_ps"] =
        QString::number(nativeDisplaySignalLinePeriodPs);
    nativeDisplayTiming["signal_line_period_ps_resolved"] =
        nativeDisplaySignalLinePeriodPsResolved;
    nativeDisplayTiming["configured_active_scanout_us"] =
        QString::number(scenario.display.activeScanoutUs);
    nativeDisplayTiming["resolved_configured_active_scanout_us"] =
        QString::number(resolvedConfiguredActiveScanoutUs);
    nativeDisplayTiming["configured_active_scanout_difference_us"] =
        QString::number(configuredActiveScanoutDifferenceUs);
    nativeDisplayTiming["configured_active_scanout_matches_signal"] =
        configuredActiveScanoutMatchesSignal;
    nativeDisplayTiming["configured_active_scanout_ps"] =
        QString::number(scenario.display.activeScanoutPs);
    nativeDisplayTiming["resolved_configured_active_scanout_ps"] =
        QString::number(resolvedConfiguredActiveScanoutPs);
    nativeDisplayTiming["precise_active_scanout_matches_signal"] =
        preciseActiveScanoutMatchesSignal;
    nativeDisplayTiming["signal_scanline_ordering"] =
        static_cast<qint64>(
            metrics.nativeDisplaySignalScanLineOrdering);
    nativeDisplayTiming["signal_progressive"] =
        nativeDisplayTimingProgressive;
    nativeDisplayTiming["rotation_identity"] =
        nativeDisplayRotationSupported;
    nativeDisplayTiming["scaling_identity"] =
        nativeDisplayScalingSupported;
    nativeDisplayTiming["signal_rate_matches_capture"] =
        nativeDisplaySignalRateMatchesCapture;
    nativeDisplayTiming["path_refresh_equals_physical_signal"] =
        nativeDisplayPathRefreshMatchesSignal;
    nativeDisplayTiming["not_drr_or_vblank_virtualized"] =
        nativeDisplayTimingNotVirtualized;
    nativeDisplayTiming["resolved_counterfactual_period_ps"] =
        QString::number(resolvedCounterfactualPeriodPs);
    nativeDisplayTiming["configured_scanout_period_us"] =
        QString::number(scenario.display.scanoutPeriodUs);
    nativeDisplayTiming["resolved_configured_scanout_period_us"] =
        QString::number(resolvedConfiguredScanoutPeriodUs);
    nativeDisplayTiming["counterfactual_period_matches_signal"] =
        counterfactualPeriodMatchesSignal;
    nativeDisplayTiming["configured_scanout_period_matches_signal"] =
        configuredScanoutPeriodMatchesSignal;
    nativeDisplayTiming["valid"] =
        nativeDisplayTimingCoverageReady;
    nativeDisplayTiming["scope"] =
        "QueryDisplayConfig path refresh is the desktop/virtual rate; target-mode signal vSync is the physical rate. Pixel rate, horizontal sync, vertical sync, and total geometry must agree within the reported ppm tolerance. The complete additional-signal-info word is preserved; a Miracast VSync divider or nonzero Windows-reserved bits block raster readiness. DRR boost, a rate mismatch, an internally inconsistent signal, or a failed DXGIDisableVBlankVirtualization startup call is not inferred away";
    nativeOutcomeIntegrity["display_timing"] =
        nativeDisplayTiming;
    QJsonObject nativeRasterSampling;
    nativeRasterSampling["fields_available"] =
        metrics.nativeRasterTelemetryAvailable;
    nativeRasterSampling["requested"] = validityObject(
        metrics.nativeRasterSamplingRequestedRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeRasterSampling["adapter_open_result"] = validityObject(
        metrics.nativeRasterOpenResultValidRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeRasterSampling["source"] = validityObject(
        metrics.nativeRasterSourceValidRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeRasterSampling["before_query_result"] = validityObject(
        metrics.nativeRasterBeforeQueryResultValidRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeRasterSampling["before_query_success"] = validityObject(
        metrics.nativeRasterBeforeQuerySuccessRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeRasterSampling["after_query_result"] = validityObject(
        metrics.nativeRasterAfterQueryResultValidRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeRasterSampling["after_query_success"] = validityObject(
        metrics.nativeRasterAfterQuerySuccessRows,
        metrics.nativeDxgiPresentAttemptRows);
    nativeRasterSampling["before_vertical_blank_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterBeforeVerticalBlankRows);
    nativeRasterSampling["before_active_scanout_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterBeforeActiveScanoutRows);
    nativeRasterSampling["after_vertical_blank_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterAfterVerticalBlankRows);
    nativeRasterSampling["after_active_scanout_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterAfterActiveScanoutRows);
    nativeRasterSampling["adapter_open_result_counts_signed_decimal"] =
        countObject(metrics.nativeRasterOpenResults);
    nativeRasterSampling["before_query_result_counts_signed_decimal"] =
        countObject(metrics.nativeRasterBeforeQueryResults);
    nativeRasterSampling["after_query_result_counts_signed_decimal"] =
        countObject(metrics.nativeRasterAfterQueryResults);
    nativeRasterSampling["before_query_duration_us"] =
        distributionObject(
            metrics.nativeRasterBeforeQueryDurationUs);
    nativeRasterSampling["before_query_to_present_us"] =
        distributionObject(
            metrics.nativeRasterBeforeToPresentUs);
    nativeRasterSampling["present_to_after_query_us"] =
        distributionObject(
            metrics.nativeRasterPresentToAfterUs);
    nativeRasterSampling["after_query_duration_us"] =
        distributionObject(
            metrics.nativeRasterAfterQueryDurationUs);
    nativeRasterSampling["before_scanline"] =
        distributionObject(
            metrics.nativeRasterBeforeScanLine);
    nativeRasterSampling["after_scanline"] =
        distributionObject(
            metrics.nativeRasterAfterScanLine);
    nativeRasterSampling["scanline_scale"] =
        QString::number(
            metrics.nativeRasterScanLineScaleInference.scale);
    nativeRasterSampling["scanline_scale_inference_valid"] =
        nativeRasterScanLineScaleReady;
    nativeRasterSampling["scanline_scale_inference_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterScanLineScaleInference.samples);
    nativeRasterSampling["maximum_raw_scanline"] =
        QString::number(
            metrics.nativeRasterScanLineScaleInference.
                maximumObservedScanLine);
    nativeRasterSampling["before_normalized_scanline"] =
        distributionObject(
            metrics.nativeRasterBeforeNormalizedScanLine);
    nativeRasterSampling["after_normalized_scanline"] =
        distributionObject(
            metrics.nativeRasterAfterNormalizedScanLine);
    nativeRasterSampling["before_active_scanout_position_ppm"] =
        distributionObject(
            metrics.nativeRasterBeforeActiveScanoutPositionPpm);
    nativeRasterSampling["after_active_scanout_position_ppm"] =
        distributionObject(
            metrics.nativeRasterAfterActiveScanoutPositionPpm);
    nativeRasterSampling["relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterRelationshipMismatchRows);
    nativeRasterSampling["timing_order_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterTimingOrderMismatchRows);
    nativeRasterSampling["display_source_id_mismatch_rows"] =
        static_cast<qint64>(
            metrics.nativeRasterSourceIdMismatchRows);
    nativeRasterSampling["sample_accounting_complete"] =
        nativeRasterSampleAccountingReady;
    nativeRasterSampling["signal_range_checked_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterSignalRangeCheckedSamples);
    nativeRasterSampling["signal_range_mismatch_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterSignalRangeMismatchSamples);
    nativeRasterSampling["signal_range_valid"] =
        nativeRasterSignalRangeReady;
    nativeRasterSampling["model_observation_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterModelObservationSamples);
    nativeRasterSampling["model_anchored_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterModelAnchoredSamples);
    nativeRasterSampling["model_current_row_post_anchor_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterModelCurrentPostAnchorSamples);
    nativeRasterSampling["model_prior_anchor_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterModelPriorAnchorSamples);
    nativeRasterSampling["model_anchor_age_us"] =
        distributionObject(metrics.nativeRasterModelAnchorAgeUs);
    nativeRasterSampling["model_anchor_coverage_valid"] =
        nativeRasterModelAnchorCoverageReady;
    nativeRasterSampling["model_anchor_source_accounting_complete"] =
        nativeRasterModelAnchorSourceAccountingReady;
    nativeRasterSampling["model_anchor_history_integrity_valid"] =
        unifiedRasterAnchorIntegrityReady;
    nativeRasterSampling["vrr_locked_comparable_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterVrrLockedComparableSamples);
    nativeRasterSampling["vrr_locked_contradictions"] =
        static_cast<qint64>(
            metrics.nativeRasterVrrLockedContradictions);
    nativeRasterSampling["free_running_comparable_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterFreeRunningComparableSamples);
    nativeRasterSampling["free_running_contradictions"] =
        static_cast<qint64>(
            metrics.nativeRasterFreeRunningContradictions);
    nativeRasterSampling["envelope_comparable_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterEnvelopeComparableSamples);
    nativeRasterSampling["comparable_hypothesis_samples"] =
        static_cast<qint64>(
            nativeRasterComparableHypothesisSamples);
    nativeRasterSampling["envelope_contradictions"] =
        static_cast<qint64>(
            metrics.nativeRasterEnvelopeContradictions);
    nativeRasterSampling["vrr_locked_scanline_comparable_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterVrrLockedScanLineComparableSamples);
    nativeRasterSampling["vrr_locked_scanline_mismatches"] =
        static_cast<qint64>(
            metrics.nativeRasterVrrLockedScanLineMismatches);
    nativeRasterSampling["vrr_locked_predicted_scanline"] =
        distributionObject(
            metrics.nativeRasterVrrLockedPredictedScanLine);
    nativeRasterSampling["vrr_locked_scanline_absolute_error_lines"] =
        distributionObject(
            metrics.nativeRasterVrrLockedScanLineAbsoluteError);
    nativeRasterSampling["vrr_locked_scanline_residual_lines"] =
        signedAccumulatorObject(
            metrics.nativeRasterVrrLockedScanLineResidual);
    nativeRasterSampling["vrr_locked_scanline_tolerance_lines"] =
        distributionObject(
            metrics.nativeRasterVrrLockedScanLineTolerance);
    QJsonObject nativeRasterVrrLockedScanLineBands;
    for (size_t index = 0; index < RateBandCount; ++index) {
        const NativeRasterScanLineBandMetrics& band =
            metrics.nativeRasterVrrLockedScanLineBands[index];
        QJsonObject bandObject;
        bandObject["comparable_samples"] =
            static_cast<qint64>(band.comparableSamples);
        bandObject["mismatches"] =
            static_cast<qint64>(band.mismatches);
        bandObject["absolute_error_lines"] =
            distributionObject(band.absoluteError);
        bandObject["residual_lines"] =
            signedAccumulatorObject(band.residual);
        bandObject["tolerance_lines"] =
            distributionObject(band.tolerance);
        nativeRasterVrrLockedScanLineBands[
            kRateBandNames[index]] = bandObject;
    }
    nativeRasterSampling[
        "vrr_locked_scanline_by_rounded_source_rate_fps"] =
            nativeRasterVrrLockedScanLineBands;
    nativeRasterSampling["free_running_scanline_comparable_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterFreeRunningScanLineComparableSamples);
    nativeRasterSampling["free_running_scanline_mismatches"] =
        static_cast<qint64>(
            metrics.nativeRasterFreeRunningScanLineMismatches);
    nativeRasterSampling["free_running_predicted_scanline"] =
        distributionObject(
            metrics.nativeRasterFreeRunningPredictedScanLine);
    nativeRasterSampling["free_running_scanline_absolute_error_lines"] =
        distributionObject(
            metrics.nativeRasterFreeRunningScanLineAbsoluteError);
    nativeRasterSampling["free_running_scanline_residual_lines"] =
        signedAccumulatorObject(
            metrics.nativeRasterFreeRunningScanLineResidual);
    nativeRasterSampling["free_running_scanline_tolerance_lines"] =
        distributionObject(
            metrics.nativeRasterFreeRunningScanLineTolerance);
    nativeRasterSampling["scanline_envelope_comparable_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterScanLineEnvelopeComparableSamples);
    nativeRasterSampling["scanline_envelope_matched_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterScanLineEnvelopeMatchedSamples);
    nativeRasterSampling["scanline_envelope_unmatched_samples"] =
        static_cast<qint64>(
            metrics.nativeRasterScanLineEnvelopeComparableSamples -
                std::min(
                    metrics.nativeRasterScanLineEnvelopeComparableSamples,
                    metrics.nativeRasterScanLineEnvelopeMatchedSamples));
    nativeRasterSampling["scanline_envelope_contradictions"] =
        static_cast<qint64>(
            metrics.nativeRasterScanLineEnvelopeContradictions);
    nativeRasterSampling[
        "scanline_position_model_validation_valid"] =
            nativeRasterScanLineModelValidationReady;
    nativeRasterSampling["scanline_residual_sign"] =
        "observed minus predicted; positive means the observed raster was farther down the active frame";
    nativeRasterSampling["vrr_locked_observation_cross_table"] =
        countObject(
            metrics.nativeRasterVrrLockedObservationCrossTable);
    nativeRasterSampling["free_running_observation_cross_table"] =
        countObject(
            metrics.nativeRasterFreeRunningObservationCrossTable);
    nativeRasterSampling["capture_valid"] =
        nativeRasterSamplingReady;
    nativeRasterSampling["model_validation_valid"] =
        nativeRasterModelValidationReady;
    nativeRasterSampling["valid"] =
        nativeRasterSamplingReady &&
        nativeRasterSignalRangeReady &&
        nativeRasterModelValidationReady;
    nativeRasterSampling["scope"] =
        "MOONLIGHT_VRR_ALIGN brackets the native Present with observation-only D3DKMTGetScanLine calls. A trace prepass infers the smallest capture-wide integer D3DKMT scan-line scale that places every successful active/vblank observation inside the captured physical signal; raw counters are divided by that scale before range and phase validation. The scale is display-signal-driven and independent of stream FPS. DXGI SyncQPCTime is treated as a periodic phase reference, so a calibrated sync-to-first-active offset may wrap its active interval through the nominal period boundary. Replay then compares active/vblank observations with both calibrated raster hypotheses and checks active observations against each hypothesis' predicted physical scan line with an explicit query-span and timestamp-quantization tolerance. D3DKMT validation uses the complete vertically-active line interval, while tear exposure ends at the final visible pixel before that line's horizontal blank. This validates the model envelope at the CPU call; it still does not prove that the queued flip took effect there or that an optical tear occurred";
    nativeOutcomeIntegrity["raster_sampling"] =
        nativeRasterSampling;
    QJsonObject rawSyncQpcIntegrity;
    rawSyncQpcIntegrity["valid_rows"] = static_cast<qint64>(
        metrics.rawSyncQpcValidRows);
    rawSyncQpcIntegrity["translated_latch_rows"] = static_cast<qint64>(
        metrics.latchValidRows);
    rawSyncQpcIntegrity["frame_statistics_success_without_raw_qpc_rows"] =
        static_cast<qint64>(
            metrics.frameStatsSuccessWithoutRawSyncQpcRows);
    rawSyncQpcIntegrity["raw_qpc_without_translated_latch_rows"] =
        static_cast<qint64>(
            metrics.rawSyncQpcWithoutTranslatedRows);
    rawSyncQpcIntegrity["frequency_mismatch_rows"] =
        static_cast<qint64>(
            metrics.rawSyncQpcFrequencyMismatchRows);
    rawSyncQpcIntegrity["translation_baseline_rows"] =
        static_cast<qint64>(
            metrics.rawSyncQpcTranslationBaselines);
    rawSyncQpcIntegrity["translation_comparisons"] =
        static_cast<qint64>(
            metrics.rawSyncQpcTranslationComparisons);
    rawSyncQpcIntegrity["translation_mismatch_rows"] =
        static_cast<qint64>(
            metrics.rawSyncQpcTranslationMismatchRows);
    rawSyncQpcIntegrity["translation_tolerance_us"] =
        static_cast<qint64>(kRawQpcTranslationToleranceUs);
    rawSyncQpcIntegrity["translated_latch_accounting_complete"] =
        rawSyncQpcTranslationAccountingReady;
    QJsonObject qpcCorrelationIntegrity;
    qpcCorrelationIntegrity["fields_available"] =
        metrics.qpcCorrelationTelemetryAvailable;
    qpcCorrelationIntegrity["valid_rows"] =
        static_cast<qint64>(metrics.qpcCorrelationValidRows);
    qpcCorrelationIntegrity["raw_qpc_rows"] =
        static_cast<qint64>(metrics.rawSyncQpcValidRows);
    qpcCorrelationIntegrity["raw_correlation_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.qpcCorrelationRelationshipMismatchRows);
    qpcCorrelationIntegrity["reference_mismatch_rows"] =
        static_cast<qint64>(
            metrics.qpcCorrelationReferenceMismatchRows);
    qpcCorrelationIntegrity["absolute_translation_mismatch_rows"] =
        static_cast<qint64>(
            metrics.qpcCorrelationTranslationMismatchRows);
    qpcCorrelationIntegrity["uncertainty_invalid_rows"] =
        static_cast<qint64>(
            metrics.qpcCorrelationUncertaintyInvalidRows);
    qpcCorrelationIntegrity["reference_qpc_ticks_decimal"] =
        QString::number(metrics.qpcCorrelationReferenceTicks);
    qpcCorrelationIntegrity["reference_clock_time_us_decimal"] =
        QString::number(metrics.qpcCorrelationReferenceTimeUs);
    qpcCorrelationIntegrity["qpc_frequency_hz_decimal"] =
        QString::number(metrics.qpcCorrelationFrequency);
    qpcCorrelationIntegrity["bracket_span_ticks_decimal"] =
        QString::number(metrics.qpcCorrelationSpanTicks);
    qpcCorrelationIntegrity["maximum_half_span_uncertainty_us"] =
        static_cast<qint64>(
            metrics.qpcCorrelationHalfSpanUncertaintyUsMaximum);
    qpcCorrelationIntegrity["scope"] =
        "the stable process-wide QPC/reference-clock pair reproduces every translated SyncQPCTime; half the QPC bracket span is a measured minimum alignment uncertainty and does not include panel transport or scanout calibration uncertainty";
    qpcCorrelationIntegrity["valid"] =
        qpcCorrelationIntegrityReady;
    rawSyncQpcIntegrity["absolute_clock_correlation"] =
        qpcCorrelationIntegrity;
    rawSyncQpcIntegrity["valid"] =
        metrics.rawSyncQpcValidRows >= metrics.latchValidRows &&
        metrics.frameStatsSuccessWithoutRawSyncQpcRows == 0 &&
        metrics.rawSyncQpcWithoutTranslatedRows == 0 &&
        metrics.rawSyncQpcFrequencyMismatchRows == 0 &&
        rawSyncQpcTranslationAccountingReady &&
        metrics.rawSyncQpcTranslationComparisons != 0 &&
        metrics.rawSyncQpcTranslationMismatchRows == 0 &&
        qpcCorrelationIntegrityReady;
    nativeOutcomeIntegrity["raw_sync_qpc_translation_integrity"] =
        rawSyncQpcIntegrity;
    nativeOutcomeIntegrity["valid"] =
        nativeOutcomeAndQpcIntegrityReady;
    telemetryCoverage["native_outcome_and_qpc_integrity"] =
        nativeOutcomeIntegrity;
    telemetryCoverage["present_count_before"] = validityObject(
        metrics.presentCountBeforeValidRows, presentedFrames);
    telemetryCoverage["frame_stats_before"] = validityObject(
        metrics.frameStatsBeforeValidRows, presentedFrames);
    QJsonObject deepBeforeCarryForward;
    deepBeforeCarryForward["coverage"] = validityObject(
        metrics.deepBeforeStateComparisons,
        metrics.deepBeforeStateEligibleRows);
    deepBeforeCarryForward["validity_mismatches"] = static_cast<qint64>(
        metrics.deepBeforeStateValidityMismatches);
    deepBeforeCarryForward["present_count_mismatches"] =
        static_cast<qint64>(
            metrics.deepBeforePresentCountMismatches);
    deepBeforeCarryForward["frame_statistics_mismatches"] =
        static_cast<qint64>(
            metrics.deepBeforeFrameStatsMismatches);
    deepBeforeCarryForward["epoch_scope"] =
        "first row and first row after an external rebase are excluded because the trace cannot prove whether the backend observed a suspend transition";
    telemetryCoverage["deep_before_state_carry_forward"] =
        deepBeforeCarryForward;
    telemetryCoverage["presented_raw_pre_present_sync_anchor"] =
        validityObject(
            metrics.presentedRawPrePresentAnchorValidRows, presentedFrames);
    telemetryCoverage["adaptive_raw_pre_present_sync_anchor"] = validityObject(
        metrics.adaptiveRawPrePresentAnchorValidRows,
        metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions);
    telemetryCoverage["adaptive_model_sync_anchor"] = validityObject(
        metrics.adaptivePrePresentAnchorValidRows,
        metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions);
    telemetryCoverage["adaptive_pre_present_sync_anchor"] =
        telemetryCoverage["adaptive_model_sync_anchor"];
    telemetryCoverage["gpu_ready_timing"] = validityObject(
        metrics.gpuReadyTimingValidRows, metrics.scheduled);
    telemetryCoverage["presented_gpu_ready_timing"] = validityObject(
        metrics.presentedGpuReadyTimingValidRows, presentedFrames);
    QJsonObject gpuReadyNativeOperations;
    gpuReadyNativeOperations["fields_available"] =
        metrics.gpuReadyNativeResultTelemetryAvailable;
    gpuReadyNativeOperations["attempted_rows"] =
        static_cast<qint64>(metrics.gpuReadyAttemptedRows);
    gpuReadyNativeOperations["signal_result"] = validityObject(
        countTotal(metrics.gpuReadySignalResults),
        metrics.gpuReadyAttemptedRows);
    gpuReadyNativeOperations["set_event_result_valid_rows"] =
        static_cast<qint64>(
            countTotal(metrics.gpuReadySetEventResults));
    gpuReadyNativeOperations["wait_result_valid_rows"] =
        static_cast<qint64>(
            countTotal(metrics.gpuReadyWaitResults));
    gpuReadyNativeOperations["signal_result_counts_signed_decimal"] =
        countObject(metrics.gpuReadySignalResults);
    gpuReadyNativeOperations[
        "set_event_result_counts_signed_decimal"] =
        countObject(metrics.gpuReadySetEventResults);
    gpuReadyNativeOperations["wait_result_counts_decimal"] =
        countObject(metrics.gpuReadyWaitResults);
    gpuReadyNativeOperations["relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.gpuReadyNativeResultRelationshipMismatchRows);
    gpuReadyNativeOperations["presented_exact_success"] =
        validityObject(
            metrics.presentedGpuReadyNativeSuccessRows,
            presentedFrames);
    gpuReadyNativeOperations["valid"] =
        metrics.gpuReadyNativeResultTelemetryAvailable &&
        metrics.gpuReadyNativeResultRelationshipMismatchRows == 0;
    gpuReadyNativeOperations["result_semantics"] =
        "D3D11 Signal and SetEventOnCompletion retain signed HRESULT and its wait retains the exact DWORD; Vulkan texture-poll rows use 0 for idle completion, 1 for the bounded timeout, and 2 for interruption or GPU failure. D3D11 stage validity follows HRESULT success (nonnegative), while successful timing requires WAIT_OBJECT_0 (0); presented rows require the backend's successful completion result for strict coverage";
    telemetryCoverage["gpu_ready_native_operations"] =
        gpuReadyNativeOperations;
    QJsonObject gpuReadyStageTiming;
    gpuReadyStageTiming["fields_available"] =
        metrics.gpuReadyStageTimingTelemetryAvailable;
    gpuReadyStageTiming["relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.gpuReadyStageTimingRelationshipMismatchRows);
    gpuReadyStageTiming["signal_call_us"] =
        distributionObject(metrics.observedGpuReadySignalCall);
    gpuReadyStageTiming["flush_call_us"] =
        distributionObject(metrics.observedGpuReadyFlushCall);
    gpuReadyStageTiming["set_event_call_us"] =
        distributionObject(metrics.observedGpuReadySetEventCall);
    telemetryCoverage["gpu_ready_stage_timing"] =
        gpuReadyStageTiming;
    telemetryCoverage["gpu_ready_completion_bounds_fields_available"] =
        metrics.gpuReadyBoundsTelemetryAvailable;
    telemetryCoverage["presented_gpu_ready_completion_bounds"] =
        validityObject(
            metrics.presentedGpuReadyBoundsValidRows, presentedFrames);
    telemetryCoverage["unique_latch_samples"] = static_cast<qint64>(
        metrics.uniqueLatchSamples);
    telemetryCoverage["stale_latch_samples"] = static_cast<qint64>(
        metrics.staleLatchSamples);
    telemetryCoverage["fresh_latch_sample_coverage"] = validityObject(
        metrics.uniqueLatchSamples, presentedFrames);
    telemetryCoverage["fresh_latch_submission_association"] =
        validityObject(
            metrics.freshLatchSamplesMatchedToSubmission,
            metrics.uniqueLatchSamples);
    telemetryCoverage["submission_sequence_resets"] = static_cast<qint64>(
        metrics.submissionSequenceResets);
    telemetryCoverage["submission_sequence_duplicates"] =
        static_cast<qint64>(metrics.submissionSequenceDuplicates);
    telemetryCoverage["latch_sequence_resets"] = static_cast<qint64>(
        metrics.latchSequenceResets);
    QJsonObject nativeBoundaryConsistency;
    nativeBoundaryConsistency["comparisons"] = static_cast<qint64>(
        metrics.nativePresentBoundaryComparisons);
    nativeBoundaryConsistency["mismatches"] = static_cast<qint64>(
        metrics.nativePresentBoundaryMismatches);
    nativeBoundaryConsistency["all_match"] =
        metrics.nativePresentBoundaryComparisons != 0 &&
        metrics.nativePresentBoundaryMismatches == 0;
    nativeBoundaryConsistency["native_present_start_minus_submission_boundary_us"] =
        signedAccumulatorObject(metrics.observedNativePresentBoundaryDelta);
    telemetryCoverage["native_present_boundary_consistency"] =
        nativeBoundaryConsistency;
    QJsonObject syncAnchorIntegrity;
    syncAnchorIntegrity["sequence_regressions"] = static_cast<qint64>(
        metrics.prePresentAnchorSequenceRegressions);
    syncAnchorIntegrity["missing_refresh_sequence"] =
        static_cast<qint64>(
            metrics.prePresentAnchorMissingRefreshSequence);
    syncAnchorIntegrity["timestamp_regressions_beyond_tolerance"] =
        static_cast<qint64>(
            metrics.prePresentAnchorTimeRegressions);
    syncAnchorIntegrity["sample_after_current_decision_beyond_tolerance"] =
        static_cast<qint64>(
            metrics.prePresentAnchorCausalOrderViolations);
    syncAnchorIntegrity["same_sequence_timestamp_jitter_beyond_tolerance"] =
        static_cast<qint64>(
            metrics.prePresentAnchorTimestampJitterBeyondTolerance);
    syncAnchorIntegrity["advanced_sequence_with_nonadvancing_timestamp"] =
        static_cast<qint64>(
            metrics.prePresentAnchorNonadvancingTime);
    syncAnchorIntegrity["translation_jitter_tolerance_us"] =
        static_cast<qint64>(kSyncAnchorTranslationJitterToleranceUs);
    syncAnchorIntegrity["minimum_interval_tolerance_us"] =
        static_cast<qint64>(kSyncAnchorMinimumIntervalToleranceUs);
    syncAnchorIntegrity["implausibly_short_refresh_intervals"] =
        static_cast<qint64>(
            metrics.prePresentAnchorImplausiblyShortIntervals);
    syncAnchorIntegrity["same_sequence_timestamp_jitter_us"] =
        boundedDistributionObject(
            metrics.repeatedSyncAnchorTimestampJitter);
    syncAnchorIntegrity["refresh_count_delta"] = boundedDistributionObject(
        metrics.syncAnchorRefreshDelta);
    syncAnchorIntegrity["elapsed_us"] = boundedDistributionObject(
        metrics.syncAnchorElapsedUs);
    syncAnchorIntegrity["mean_interval_per_refresh_us"] =
        boundedDistributionObject(
            metrics.syncAnchorMeanIntervalUs);
    syncAnchorIntegrity[
        "samples_suppressed_until_post_present_after_external_rebase"] =
        static_cast<qint64>(
            metrics.prePresentAnchorsSuppressedByEpoch);
    syncAnchorIntegrity["valid"] =
        metrics.prePresentAnchorSequenceRegressions == 0 &&
        metrics.prePresentAnchorMissingRefreshSequence == 0 &&
        metrics.prePresentAnchorTimeRegressions == 0 &&
        metrics.prePresentAnchorCausalOrderViolations == 0 &&
        metrics.prePresentAnchorTimestampJitterBeyondTolerance == 0 &&
        metrics.prePresentAnchorNonadvancingTime == 0 &&
        metrics.prePresentAnchorImplausiblyShortIntervals == 0 &&
        unifiedRasterAnchorIntegrityReady;
    telemetryCoverage["pre_present_sync_anchor_integrity"] =
        syncAnchorIntegrity;
    QJsonObject postSyncAnchorIntegrity;
    postSyncAnchorIntegrity["sequence_regressions"] = static_cast<qint64>(
        metrics.postPresentAnchorSequenceRegressions);
    postSyncAnchorIntegrity["missing_refresh_sequence_or_timestamp"] =
        static_cast<qint64>(
            metrics.postPresentAnchorMissingRefreshSequence);
    postSyncAnchorIntegrity["timestamp_regressions_beyond_tolerance"] =
        static_cast<qint64>(
            metrics.postPresentAnchorTimeRegressions);
    postSyncAnchorIntegrity["sample_after_present_return_beyond_tolerance"] =
        static_cast<qint64>(
            metrics.postPresentAnchorCausalOrderViolations);
    postSyncAnchorIntegrity[
        "same_sequence_timestamp_jitter_beyond_tolerance"] =
        static_cast<qint64>(
            metrics.postPresentAnchorTimestampJitterBeyondTolerance);
    postSyncAnchorIntegrity[
        "advanced_sequence_with_nonadvancing_timestamp"] =
        static_cast<qint64>(
            metrics.postPresentAnchorNonadvancingTime);
    postSyncAnchorIntegrity["implausibly_short_refresh_intervals"] =
        static_cast<qint64>(
            metrics.postPresentAnchorImplausiblyShortIntervals);
    postSyncAnchorIntegrity["same_sequence_timestamp_jitter_us"] =
        boundedDistributionObject(
            metrics.repeatedPostSyncAnchorTimestampJitter);
    postSyncAnchorIntegrity["refresh_count_delta"] =
        boundedDistributionObject(metrics.postSyncAnchorRefreshDelta);
    postSyncAnchorIntegrity["elapsed_us"] =
        boundedDistributionObject(metrics.postSyncAnchorElapsedUs);
    postSyncAnchorIntegrity["mean_interval_per_refresh_us"] =
        boundedDistributionObject(
            metrics.postSyncAnchorMeanIntervalUs);
    postSyncAnchorIntegrity["valid"] =
        metrics.postPresentAnchorSequenceRegressions == 0 &&
        metrics.postPresentAnchorMissingRefreshSequence == 0 &&
        metrics.postPresentAnchorTimeRegressions == 0 &&
        metrics.postPresentAnchorCausalOrderViolations == 0 &&
        metrics.postPresentAnchorTimestampJitterBeyondTolerance == 0 &&
        metrics.postPresentAnchorNonadvancingTime == 0 &&
        metrics.postPresentAnchorImplausiblyShortIntervals == 0 &&
        unifiedRasterAnchorIntegrityReady;
    telemetryCoverage["post_present_sync_anchor_integrity"] =
        postSyncAnchorIntegrity;
    QJsonObject unifiedSyncAnchorIntegrity;
    unifiedSyncAnchorIntegrity["cross_source_sequence_regressions"] =
        static_cast<qint64>(
            metrics.unifiedAnchorSequenceRegressions);
    unifiedSyncAnchorIntegrity[
        "same_sequence_timestamp_mismatches_beyond_tolerance"] =
        static_cast<qint64>(
            metrics.unifiedAnchorSameSequenceTimestampMismatches);
    unifiedSyncAnchorIntegrity[
        "advanced_sequence_with_nonadvancing_timestamp"] =
        static_cast<qint64>(
            metrics.unifiedAnchorNonadvancingTime);
    unifiedSyncAnchorIntegrity["implausibly_short_refresh_intervals"] =
        static_cast<qint64>(
            metrics.unifiedAnchorImplausiblyShortIntervals);
    unifiedSyncAnchorIntegrity["valid"] =
        unifiedRasterAnchorIntegrityReady;
    unifiedSyncAnchorIntegrity["scope"] =
        "validates the single causal anchor history after merging pre-Present cached statistics and current/prior post-Present observations";
    telemetryCoverage["unified_sync_anchor_integrity"] =
        unifiedSyncAnchorIntegrity;
    capture["telemetry_coverage"] = telemetryCoverage;

    QJsonObject observedLatency;
    observedLatency["decode_to_pacer_arrival"] = distributionObject(
        metrics.observedDecodeToArrival);
    observedLatency["pacer_arrival_to_dequeue"] = distributionObject(
        metrics.observedArrivalToDequeue);
    observedLatency["dequeue_to_decision"] = distributionObject(
        metrics.observedDequeueToDecision);
    observedLatency["decode_to_submission"] = distributionObject(
        metrics.observedDecodeToSubmission);
    observedLatency["pacer_arrival_to_submission"] = distributionObject(
        metrics.observedArrivalToSubmission);
    observedLatency["decision_to_submission"] = distributionObject(
        metrics.observedDecisionToSubmission);
    observedLatency["projected_source_to_submission"] = distributionObject(
        metrics.observedProjectedSourceToSubmission);
    observedLatency["submission_spacing"] = distributionObject(
        metrics.observedSubmissionSpacing);
    observedLatency["completion_queue_depth"] = distributionObject(
        metrics.observedCompletionQueueDepth);

    QJsonObject observedCosts;
    observedCosts["preparation"] = distributionObject(
        metrics.observedPreparation);
    observedCosts["present_call"] = distributionObject(
        metrics.observedPresentCall);
    observedCosts["native_present_call"] = distributionObject(
        metrics.observedNativePresentCall);
    observedCosts["native_present_start_minus_submission_boundary"] =
        signedAccumulatorObject(metrics.observedNativePresentBoundaryDelta);
    observedCosts["gpu_ready_wait"] = distributionObject(
        metrics.observedGpuReadyWait);
    observedCosts["gpu_ready_poll_fence_lag"] =
        distributionObject(metrics.observedGpuReadyPollFenceLag);
    observedCosts["gpu_ready_completion_uncertainty"] =
        distributionObject(
            metrics.observedGpuReadyCompletionUncertainty);
    observedCosts["gpu_ready_signal_to_completion_upper_bound"] =
        distributionObject(
            metrics.observedGpuReadySignalToCompletionUpperBound);
    observedCosts["controller_call"] = distributionObject(
        metrics.observedControllerCall);
    observedCosts["stale_age_at_check"] = distributionObject(
        metrics.observedStaleAge);
    observedCosts["render_wait"] = distributionObject(
        metrics.observedRenderWait);
    observedCosts["render_wait_overshoot"] = distributionObject(
        metrics.observedRenderWaitOvershoot);
    observedCosts["render_scheduler_delay"] = distributionObject(
        metrics.observedRenderSchedulerDelay);
    observedCosts["render_coarse_wake_return_minus_requested_us"] =
        signedAccumulatorObject(
            metrics.observedRenderCoarseWakeOffset);
    observedCosts["render_active_wait_yield_count"] =
        distributionObject(
            metrics.observedRenderActiveYieldCount);
    observedCosts["render_deadline_already_elapsed_rows"] =
        static_cast<qint64>(
            metrics.renderDeadlineAlreadyElapsedRows);
    observedCosts["target_wait"] = distributionObject(
        metrics.observedTargetWait);
    observedCosts["target_wait_overshoot"] = distributionObject(
        metrics.observedTargetWaitOvershoot);
    observedCosts["target_scheduler_delay"] = distributionObject(
        metrics.observedTargetSchedulerDelay);
    observedCosts["target_coarse_wake_return_minus_requested_us"] =
        signedAccumulatorObject(
            metrics.observedTargetCoarseWakeOffset);
    observedCosts["target_active_wait_yield_count"] =
        distributionObject(
            metrics.observedTargetActiveYieldCount);
    observedCosts["target_deadline_already_elapsed_rows"] =
        static_cast<qint64>(
            metrics.targetDeadlineAlreadyElapsedRows);
    observedCosts["spacing_correction_wait"] = distributionObject(
        metrics.observedCorrectionWait);
    observedCosts["spacing_deficit"] = distributionObject(
        metrics.observedSpacingDeficit);
    observedCosts["spacing_corrected_rows"] = static_cast<qint64>(
        metrics.spacingCorrectedRows);
    QJsonObject waiterLifecycle;
    waiterLifecycle["fields_available"] =
        metrics.waitLifecycleTelemetryAvailable;
    waiterLifecycle["render_expected_rows"] =
        static_cast<qint64>(metrics.renderWaitExpectedRows);
    waiterLifecycle["render_recorded_rows"] =
        static_cast<qint64>(metrics.renderWaitLifecycleRows);
    waiterLifecycle["render_clean_completion_rows"] =
        static_cast<qint64>(metrics.renderWaitCleanCompletionRows);
    waiterLifecycle["render_coarse_sleep_rows"] =
        static_cast<qint64>(metrics.renderWaitCoarseSleepRows);
    waiterLifecycle["render_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.renderWaitLifecycleRelationshipMismatchRows);
    waiterLifecycle["render_clock_stall_rows"] =
        static_cast<qint64>(metrics.renderWaitClockStallRows);
    waiterLifecycle["render_yield_limit_rows"] =
        static_cast<qint64>(metrics.renderWaitYieldLimitRows);
    waiterLifecycle["render_early_return_rows"] =
        static_cast<qint64>(metrics.renderWaitEarlyReturnRows);
    waiterLifecycle["target_expected_rows"] =
        static_cast<qint64>(metrics.targetWaitExpectedRows);
    waiterLifecycle["target_recorded_rows"] =
        static_cast<qint64>(metrics.targetWaitLifecycleRows);
    waiterLifecycle["target_clean_completion_rows"] =
        static_cast<qint64>(metrics.targetWaitCleanCompletionRows);
    waiterLifecycle["target_coarse_sleep_rows"] =
        static_cast<qint64>(metrics.targetWaitCoarseSleepRows);
    waiterLifecycle["target_relationship_mismatch_rows"] =
        static_cast<qint64>(
            metrics.targetWaitLifecycleRelationshipMismatchRows);
    waiterLifecycle["target_clock_stall_rows"] =
        static_cast<qint64>(metrics.targetWaitClockStallRows);
    waiterLifecycle["target_yield_limit_rows"] =
        static_cast<qint64>(metrics.targetWaitYieldLimitRows);
    waiterLifecycle["target_early_return_rows"] =
        static_cast<qint64>(metrics.targetWaitEarlyReturnRows);
    waiterLifecycle["candidate_render_wait_path_comparisons"] =
        static_cast<qint64>(
            metrics.simulatedRenderWaitPathComparisons);
    waiterLifecycle["candidate_render_wait_path_matches"] =
        static_cast<qint64>(
            metrics.simulatedRenderWaitPathMatches);
    waiterLifecycle[
        "candidate_render_wait_recorded_final_residual_rows"] =
        static_cast<qint64>(
            metrics.simulatedRenderWaitRecordedFinalResidualRows);
    waiterLifecycle["candidate_target_wait_path_comparisons"] =
        static_cast<qint64>(
            metrics.simulatedTargetWaitPathComparisons);
    waiterLifecycle["candidate_target_wait_path_matches"] =
        static_cast<qint64>(
            metrics.simulatedTargetWaitPathMatches);
    waiterLifecycle[
        "candidate_target_wait_recorded_final_residual_rows"] =
        static_cast<qint64>(
            metrics.simulatedTargetWaitRecordedFinalResidualRows);
    waiterLifecycle["semantics"] =
        "Each waiter records its internal first clock sample, exact active budget, coarse sleep count and requested duration, requested wake and actual return, active-yield start/limit/count, clock-stall exits, yield-cap exits, and final time. A candidate on the same elapsed/active/coarse path translates the captured final residual; a path-changing candidate is counted explicitly and uses an ideal deadline because that path has no captured OS residual. Wake injection shifts the recorded coarse return; only the portion beyond the recorded active-margin completion becomes execution delay";
    observedCosts["waiter_lifecycle"] = waiterLifecycle;

    QJsonObject observedTears;
    observedTears["classifications"] = countObject(
        metrics.tearClassifications);
    observedTears["modelled_interval_violations"] = static_cast<qint64>(
        metrics.originalTearRisks);
    observedTears["scanout_anomalies"] = static_cast<qint64>(
        metrics.scanoutAnomalies);
    observedTears["repeated_refreshes"] = static_cast<qint64>(
        metrics.repeatedRefreshes);
    QJsonObject observedRasterEnvelope = rasterEnvelopeObject(
        metrics.observedRasterEnvelope);
    observedRasterEnvelope["historical_anchor_fallbacks"] =
        static_cast<qint64>(metrics.observedRasterAnchorFallbacks);
    observedTears["raster_phase_envelope"] = observedRasterEnvelope;
    QJsonObject exactPresentRefresh;
    exactPresentRefresh["timestamp_samples"] = static_cast<qint64>(
        metrics.exactPresentRefreshTimestampSamples);
    exactPresentRefresh["correlated_submissions"] = static_cast<qint64>(
        metrics.exactPresentRefreshCorrelations);
    exactPresentRefresh["invalid_correlations"] = static_cast<qint64>(
        metrics.invalidExactPresentRefreshCorrelations);
    exactPresentRefresh["correlation_coverage"] = validityObject(
        metrics.exactPresentRefreshCorrelations,
        metrics.exactPresentRefreshTimestampSamples);
    exactPresentRefresh["submission_relative_to_refresh_us"] =
        signedAccumulatorObject(
            metrics.observedExactPresentRefreshPhase);
    exactPresentRefresh["native_present_start_relative_to_refresh_us"] =
        signedAccumulatorObject(
            metrics.observedExactPresentRefreshPhase);
    exactPresentRefresh["modeled_transition_relative_to_refresh_us"] =
        signedAccumulatorObject(
            metrics.observedExactPresentRefreshModeledPhase);
    exactPresentRefresh[
        "modeled_transition_relative_to_active_scanout_us"] =
        signedAccumulatorObject(
            metrics.observedExactPresentActiveScanoutPhase);
    exactPresentRefresh["classifications"] = countObject(
        metrics.observedExactRefreshClassifications);
    exactPresentRefresh["classification_rows"] = static_cast<qint64>(
        countTotal(metrics.observedExactRefreshClassifications));
    exactPresentRefresh["classification_accounting_complete"] =
        countTotal(metrics.observedExactRefreshClassifications) ==
            metrics.exactPresentRefreshCorrelations;
    exactPresentRefresh["pre_present_envelope_validation"] = countObject(
        metrics.observedRasterValidation);
    exactPresentRefresh["pre_present_envelope_validation_rows"] =
        static_cast<qint64>(countTotal(metrics.observedRasterValidation));
    exactPresentRefresh["pre_present_envelope_validation_contradictions"] =
        static_cast<qint64>(
            metrics.observedRasterValidationContradictions);
    exactPresentRefresh["minimum_validation_samples_for_readiness"] =
        static_cast<qint64>(kMinimumExactRasterValidationSamples);
    exactPresentRefresh["phase_sign"] =
        "negative is before the refresh anchor; positive is after it in the same refresh interval";
    exactPresentRefresh["phase_boundary"] =
        "native Present start is the measured boundary; classifications add display.present_transport_us and compare against refresh plus display.sync_to_active_scanout_us";
    exactPresentRefresh["qualification"] =
        "PresentRefreshCount equals SyncRefreshCount, so SyncQPCTime timestamps the same refresh";
    exactPresentRefresh["evidence_scope"] =
        "a positive active classification is the strongest schema-5 software evidence of an active-scanout transition, but it is not an optical tear sensor";
    exactPresentRefresh["validation_scope"] =
        "pre_present_envelope_to_equality_anchored_classification for the same Present ID";
    observedTears["exact_present_refresh_timing"] = exactPresentRefresh;

    const auto readinessObject = [](const Vrr13::ReadinessWindow& window,
                                    uint64_t revision) {
        const auto sample = window.snapshot(0, revision >= 4, revision >= 5);
        QJsonObject result;
        result["window_us"] = qint64(30000000);
        result["samples"] = qint64(sample.samples);
        result["misses"] = qint64(sample.misses);
        result["late_over_1ms"] = qint64(sample.over1ms);
        result["late_over_2ms"] = qint64(sample.over2ms);
        result["dropped"] = qint64(sample.dropped);
        result["valid"] = sample.samples != 0;
        result["on_time_percent"] = sample.samples ?
            QJsonValue(100.0 * (sample.samples - sample.misses) / sample.samples) : QJsonValue();
        if (revision >= 5) {
            result["average_miss_us"] = Vrr13::ReadinessWindow::meanMissUs(sample);
            result["mean_miss_smoothness_percent"] = Vrr13::ReadinessWindow::meanMissScore(sample);
        }
        result["scope"] = revision >= 5 ?
            "V2 Queue: average measured lateness among late frames; smoothness score remains 100 through a 1 ms mean; 30-second reporting and one-second buffer control; drops counted separately" : revision >= 4 ?
            "intended smoothed slot before recovery clamps; lateness through 1 ms is tolerated, 1-2 ms counts only above 50 percent prevalence, over 2 ms and client drops are misses; window/shutdown discards excluded" :
            "intended smoothed slot before recovery clamps; any lateness and client drops are misses; window/shutdown discards excluded";
        return result;
    };
    QJsonObject observed;
    observed["readiness"] = readinessObject(
        metrics.observedReadiness, capturedResponsiveBuffer);
    observed["dispositions"] = countObject(metrics.dispositions);
    observed["drops"] = static_cast<qint64>(metrics.originalDrops);
    observed["latency_us"] = observedLatency;
    observed["execution_cost_us"] = observedCosts;
    {
        const uint64_t durationUs =
            metrics.lastArrivalUs >= metrics.firstArrivalUs ?
                metrics.lastArrivalUs - metrics.firstArrivalUs : 0;
        observed["sender_cadence"] = senderCadenceObject(
            metrics.observedSenderCadence, durationUs);
        observed["playout_delay_us"] = distributionObject(
            metrics.observedPlayoutDelayUs);
        observed["stock_present_on_render_sender_cadence"] =
            senderCadenceObject(metrics.stockSenderCadence, durationUs);
    }
    observed["absolute_submit_error_us"] = distributionObject(
        metrics.observedAbsoluteSubmitError);
    observed["cadence_by_rounded_source_rate_fps"] = cadenceBandsObject(
        metrics.observedRateBands);
    observed["jerk_anomaly_windows"] = anomalyWindowObject(
        metrics.observedJerkAnomalies);
    observed["tear_and_scanout"] = observedTears;

    QJsonObject simulatedLatency;
    simulatedLatency["decode_to_submission"] = distributionObject(
        metrics.simulatedDecodeToSubmission);
    simulatedLatency["pacer_arrival_to_submission"] = distributionObject(
        metrics.simulatedArrivalToSubmission);
    simulatedLatency["decision_to_submission"] = distributionObject(
        metrics.simulatedDecisionToSubmission);
    simulatedLatency["projected_source_to_submission"] = distributionObject(
        metrics.simulatedProjectedSourceToSubmission);
    simulatedLatency["submission_spacing"] = distributionObject(
        metrics.simulatedSubmissionSpacing);

    QJsonObject simulatedTears;
    simulatedTears["classifications"] = countObject(
        metrics.simulatedTearClassifications);
    simulatedTears["modelled_interval_violations"] = static_cast<qint64>(
        metrics.simulatedTearRisks);
    simulatedTears["latched_frames"] = static_cast<qint64>(
        metrics.simulatedLatchedFrames);
    simulatedTears["ideal_vrr_locked_sequence_scope"] =
        "one display transition per accepted adaptive Present is assumed by the ideal VRR-locked hypothesis; this does not predict driver queue replacement, low-framerate compensation, or panel behavior";
    QJsonObject simulatedRasterEnvelope = rasterEnvelopeObject(
        metrics.simulatedRasterEnvelope);
    simulatedRasterEnvelope["historical_anchor_fallbacks"] =
        static_cast<qint64>(metrics.simulatedRasterAnchorFallbacks);
    simulatedTears["raster_phase_envelope"] = simulatedRasterEnvelope;
    QJsonObject recordedRefreshPhase;
    recordedRefreshPhase["comparisons"] = static_cast<qint64>(
        metrics.simulatedRecordedRefreshComparisons);
    recordedRefreshPhase["candidate_after_recorded_refresh"] =
        static_cast<qint64>(metrics.simulatedAfterRecordedRefresh);
    recordedRefreshPhase["comparable_display_hz"] =
        simulatedDisplayHz == capturedDisplayHz;
    recordedRefreshPhase["candidate_relative_to_refresh_us"] =
        signedAccumulatorObject(metrics.simulatedRecordedRefreshPhase);
    recordedRefreshPhase["candidate_native_present_start_relative_to_refresh_us"] =
        signedAccumulatorObject(metrics.simulatedRecordedRefreshPhase);
    recordedRefreshPhase["candidate_modeled_transition_relative_to_refresh_us"] =
        signedAccumulatorObject(
            metrics.simulatedRecordedRefreshModeledPhase);
    recordedRefreshPhase[
        "candidate_modeled_transition_relative_to_active_scanout_us"] =
        signedAccumulatorObject(
            metrics.simulatedRecordedActiveScanoutPhase);
    recordedRefreshPhase["classifications"] = countObject(
        metrics.simulatedExactRefreshClassifications);
    recordedRefreshPhase["classification_rows"] = static_cast<qint64>(
        countTotal(metrics.simulatedExactRefreshClassifications));
    recordedRefreshPhase["classification_accounting_complete"] =
        countTotal(metrics.simulatedExactRefreshClassifications) ==
            metrics.simulatedRecordedRefreshComparisons;
    recordedRefreshPhase["pre_present_envelope_validation"] = countObject(
        metrics.simulatedRasterValidation);
    recordedRefreshPhase["pre_present_envelope_validation_rows"] =
        static_cast<qint64>(countTotal(metrics.simulatedRasterValidation));
    recordedRefreshPhase[
        "pre_present_envelope_validation_contradictions"] =
        static_cast<qint64>(
            metrics.simulatedRasterValidationContradictions);
    recordedRefreshPhase["scope"] =
        "candidate submission versus the capture's equality-qualified refresh anchor; active-scanout phase additionally applies display.sync_to_active_scanout_us, while the separate free-running hypothesis propagates a counterfactual clock";
    simulatedTears["recorded_exact_refresh_phase"] =
        recordedRefreshPhase;
    QJsonObject freeRunningRefreshHypothesis;
    freeRunningRefreshHypothesis["adaptive_rows"] = static_cast<qint64>(
        metrics.counterfactualFreeRunningAdaptiveRows);
    freeRunningRefreshHypothesis["phase_baselines"] = static_cast<qint64>(
        metrics.counterfactualFreeRunningBaselines);
    freeRunningRefreshHypothesis["refresh_delta_comparisons"] =
        static_cast<qint64>(
            metrics.counterfactualFreeRunningComparisons);
    freeRunningRefreshHypothesis["unseeded_rows"] = static_cast<qint64>(
        metrics.counterfactualFreeRunningUnseededRows);
    freeRunningRefreshHypothesis["latched_present_resets"] =
        static_cast<qint64>(
            metrics.counterfactualFreeRunningLatchedResets);
    freeRunningRefreshHypothesis["classified_row_coverage"] =
        validityObject(
            saturatingAdd(
                metrics.counterfactualFreeRunningBaselines,
                metrics.counterfactualFreeRunningComparisons),
            metrics.counterfactualFreeRunningAdaptiveRows);
    freeRunningRefreshHypothesis["time_regressions"] =
        static_cast<qint64>(
            metrics.counterfactualFreeRunningTimeRegressions);
    freeRunningRefreshHypothesis["period_changes"] =
        static_cast<qint64>(
            metrics.counterfactualFreeRunningPeriodChanges);
    freeRunningRefreshHypothesis["picosecond_conversion_failures"] =
        static_cast<qint64>(
            metrics.counterfactualFreeRunningConversionFailures);
    QJsonObject freeRunningScanoutAnomalies;
    freeRunningScanoutAnomalies["lower"] = static_cast<qint64>(
        metrics.counterfactualFreeRunningScanoutAnomalyLower);
    freeRunningScanoutAnomalies["central"] = static_cast<qint64>(
        metrics.counterfactualFreeRunningScanoutAnomalies);
    freeRunningScanoutAnomalies["upper"] = static_cast<qint64>(
        metrics.counterfactualFreeRunningScanoutAnomalyUpper);
    freeRunningRefreshHypothesis["same_refresh_superseded_presents"] =
        freeRunningScanoutAnomalies;
    QJsonObject freeRunningRepeatedRefreshes;
    freeRunningRepeatedRefreshes["lower"] = static_cast<qint64>(
        metrics.counterfactualFreeRunningRepeatedRefreshLower);
    freeRunningRepeatedRefreshes["central"] = static_cast<qint64>(
        metrics.counterfactualFreeRunningRepeatedRefreshes);
    freeRunningRepeatedRefreshes["upper"] = static_cast<qint64>(
        metrics.counterfactualFreeRunningRepeatedRefreshUpper);
    freeRunningRefreshHypothesis["repeated_refreshes"] =
        freeRunningRepeatedRefreshes;
    freeRunningRefreshHypothesis[
        "propagated_phase_vs_captured_anchor_difference_ps"] =
        boundedDistributionObject(
            metrics.
                counterfactualFreeRunningPhaseReferenceDifferencePs);
    freeRunningRefreshHypothesis["hypothesis"] =
        "after one captured sync anchor seeds an adaptive segment, the display free-runs at explicit display.scanout_period_ps or the captured QueryDisplayConfig physical-signal rational (falling back to scanout_period_us only for non-ready legacy exploration); candidate Present transitions use display.present_transport_us plus per-frame execution.display_transition_delay_us injection and never re-anchor to later recorded VRR refreshes";
    freeRunningRefreshHypothesis["uncertainty_scope"] =
        "each lower bound uses the latest possible prior transition and earliest possible current transition, while each upper bound uses the opposite endpoints with display.phase_uncertainty_us applied to both; totals conservatively sum per-transition extrema, so endpoints need not be jointly attainable and do not bound unknown driver queuing, LFC, or panel behavior";
    freeRunningRefreshHypothesis["latched_scope"] =
        "a synchronized/latched Present has no measured counterfactual transition timestamp, so it terminates the propagated segment instead of fabricating one";
    simulatedTears["free_running_refresh_hypothesis"] =
        freeRunningRefreshHypothesis;

    QJsonObject simulation;
    simulation["model"] = kReplayModel;
    QJsonObject smoothnessFeedback;
    const auto& feedback = metrics.lastFeedbackDecision;
    smoothnessFeedback["protection_us"] = double(feedback.smoothnessProtectionUs);
    smoothnessFeedback["requested_buffer_us"] = double(feedback.requestedPlayoutDelayUs);
    smoothnessFeedback["applied_buffer_us"] = double(feedback.playoutDelayUs);
    smoothnessFeedback["capacity_limited_frames"] = double(metrics.feedbackCapacityLimitedFrames);
    smoothnessFeedback["submission_window_samples"] = double(feedback.submissionSmoothnessSamples);
    smoothnessFeedback["submission_window_misses"] = double(feedback.submissionSmoothnessMisses);
    smoothnessFeedback["native_window_samples"] = double(feedback.nativeSmoothnessSamples);
    smoothnessFeedback["native_window_misses"] = double(feedback.nativeSmoothnessMisses);
    smoothnessFeedback["native_evidence"] = "recorded service latency shifted with candidate submissions; missing intervals are not successes";
    simulation["smoothness_feedback"] = smoothnessFeedback;
    simulation["display_hz"] = simulatedDisplayHz;
    simulation["stream_fps"] = simulatedStreamFps;
    simulation["additional_queued_frame"] = additionalQueuedFrame;
    simulation["session_allow_tearing"] = sessionAllowTearing;
    simulation["native_presentation_permission_scope"] =
        "Retains the captured session's tearing permission. Controller spacing and raster classifications remain timing proxies; disabling permission does not establish optical tear absence or model changed native blocking.";
    simulation["can_latch_present"] = simulatedCanLatch;
    simulation["scenario"] = scenario.name;
    simulation["mode"] = scenario.mode;
    QJsonObject resolvedParameters;
    resolvedParameters["controller"] = vrrTimingParametersToJson(
        scenario.controller);
    resolvedParameters["worker"] = vrrWorkerParametersToJson(scenario.worker);
    resolvedParameters["display"] = vrrDisplayParametersToJson(
        scenario.display);
    resolvedParameters["execution"] = vrrExecutionParametersToJson(
        scenario.execution);
    simulation["resolved_parameters"] = resolvedParameters;
    simulation["parameter_fingerprint"] = QString::fromLatin1(
        QCryptographicHash::hash(
            QJsonDocument(resolvedParameters).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256).toHex());
    const bool rasterProbeOverheadRemovalAccountingComplete =
        saturatingAdd(
            metrics.rasterProbeOverheadRemovalAvailableFrames,
            metrics.rasterProbeOverheadRemovalMissingFrames) ==
                metrics.rasterProbeOverheadRemovalRequestedFrames;
    const bool rasterProbeOverheadRemovalEvidenceComplete =
        rasterProbeOverheadRemovalAccountingComplete &&
        metrics.rasterProbeOverheadRemovalMissingFrames == 0 &&
        (scenario.execution.removePrePresentRasterProbeOverhead == 0 ||
         metrics.rasterProbeOverheadRemovalRequestedFrames ==
             presentedFrames);
    QJsonObject observerOverheadNormalization;
    observerOverheadNormalization[
        "pre_present_raster_probe_removal_enabled"] =
        scenario.execution.removePrePresentRasterProbeOverhead != 0;
    observerOverheadNormalization["candidate_presented_frames"] =
        static_cast<qint64>(presentedFrames);
    observerOverheadNormalization["requested_frames"] =
        static_cast<qint64>(
            metrics.rasterProbeOverheadRemovalRequestedFrames);
    observerOverheadNormalization[
        "requested_evidence_available_frames"] =
        static_cast<qint64>(
            metrics.rasterProbeOverheadRemovalAvailableFrames);
    observerOverheadNormalization["missing_evidence_frames"] =
        static_cast<qint64>(
            metrics.rasterProbeOverheadRemovalMissingFrames);
    observerOverheadNormalization[
        "requested_measured_query_duration_total_us"] =
        static_cast<qint64>(metrics.measuredRasterProbeDurationUs);
    observerOverheadNormalization["removed_total_us"] =
        static_cast<qint64>(metrics.removedRasterProbeOverheadUs);
    observerOverheadNormalization[
        "retained_at_submission_floor_total_us"] =
        static_cast<qint64>(
            metrics.measuredRasterProbeDurationUs -
            std::min(
                metrics.measuredRasterProbeDurationUs,
                metrics.removedRasterProbeOverheadUs));
    observerOverheadNormalization["submission_floor_clamped_frames"] =
        static_cast<qint64>(
            metrics.rasterProbeOverheadRemovalClampedFrames);
    observerOverheadNormalization[
        "recorded_pre_present_query_duration_us"] =
        distributionObject(
            metrics.nativeRasterBeforeQueryDurationUs);
    observerOverheadNormalization[
        "recorded_pre_query_return_to_present_us"] =
        distributionObject(metrics.nativeRasterBeforeToPresentUs);
    observerOverheadNormalization[
        "recorded_present_return_to_post_query_us"] =
        distributionObject(metrics.nativeRasterPresentToAfterUs);
    observerOverheadNormalization[
        "recorded_post_present_query_duration_us"] =
        distributionObject(
            metrics.nativeRasterAfterQueryDurationUs);
    observerOverheadNormalization["evidence_accounting_complete"] =
        rasterProbeOverheadRemovalAccountingComplete;
    observerOverheadNormalization[
        "requested_transformation_complete"] =
        rasterProbeOverheadRemovalEvidenceComplete;
    observerOverheadNormalization["scope"] =
        "when enabled, subtracts only each successful timestamp-bracketed pre-Present D3DKMTGetScanLine call duration from the candidate's recorded execution residual; it preserves any unmeasured post-query scheduler residual and clamps at the reconstructed target/spacing/readiness floor before deliberate submission_advance_us faults are applied";
    observerOverheadNormalization["post_present_probe_removed"] =
        false;
    observerOverheadNormalization["lifecycle_scope"] =
        "fixed replay preserves recorded admission and frame lifecycle. This transform does not remove the after-Present D3DKMT sample or speculate how a shorter presenter return would have changed later queue admission, dequeue, or decision timing; the capture reports those after-query costs separately for future lifecycle simulation";
    simulation["observer_overhead_normalization"] =
        observerOverheadNormalization;
    simulation["fixed_recorded_admission_and_lifecycle"] =
        scenario.mode == "fixed";
    QJsonObject executionInjection;
    executionInjection["periodic_stalls"] = static_cast<qint64>(
        metrics.injectedPeriodicStalls);
    executionInjection["periodic_injection_frames"] =
        static_cast<qint64>(metrics.injectedPeriodicStalls);
    executionInjection["decision_delay_total_us"] = static_cast<qint64>(
        metrics.injectedDecisionDelayUs);
    executionInjection["render_wake_delay_requested_total_us"] =
        static_cast<qint64>(metrics.requestedRenderWakeDelayUs);
    executionInjection["render_wake_delay_applied_total_us"] =
        static_cast<qint64>(metrics.injectedRenderWakeDelayUs);
    executionInjection["render_wake_delay_total_us"] =
        static_cast<qint64>(metrics.injectedRenderWakeDelayUs);
    executionInjection["render_wake_delay_suppressed_total_us"] =
        static_cast<qint64>(metrics.suppressedRenderWakeDelayUs);
    executionInjection[
        "render_wake_delay_absorbed_by_active_margin_total_us"] =
        static_cast<qint64>(metrics.absorbedRenderWakeDelayUs);
    executionInjection["render_wake_execution_delay_total_us"] =
        static_cast<qint64>(metrics.renderWakeExecutionDelayUs);
    executionInjection["render_wake_delay_requested_frames"] =
        static_cast<qint64>(metrics.renderWakeDelayRequestedFrames);
    executionInjection["render_wake_delay_coarse_sleep_eligible_frames"] =
        static_cast<qint64>(metrics.renderWakeDelayEligibleFrames);
    executionInjection["render_wake_delay_accounting_complete"] =
        saturatingAdd(
            metrics.injectedRenderWakeDelayUs,
            metrics.suppressedRenderWakeDelayUs) ==
                metrics.requestedRenderWakeDelayUs;
    executionInjection["render_wake_effect_accounting_complete"] =
        saturatingAdd(
            metrics.absorbedRenderWakeDelayUs,
            metrics.renderWakeExecutionDelayUs) ==
                metrics.injectedRenderWakeDelayUs;
    executionInjection["target_wake_delay_requested_total_us"] =
        static_cast<qint64>(metrics.requestedTargetWakeDelayUs);
    executionInjection["target_wake_delay_applied_total_us"] =
        static_cast<qint64>(metrics.injectedTargetWakeDelayUs);
    executionInjection["target_wake_delay_total_us"] =
        static_cast<qint64>(metrics.injectedTargetWakeDelayUs);
    executionInjection["target_wake_delay_suppressed_total_us"] =
        static_cast<qint64>(metrics.suppressedTargetWakeDelayUs);
    executionInjection[
        "target_wake_delay_absorbed_by_active_margin_total_us"] =
        static_cast<qint64>(metrics.absorbedTargetWakeDelayUs);
    executionInjection["target_wake_execution_delay_total_us"] =
        static_cast<qint64>(metrics.targetWakeExecutionDelayUs);
    executionInjection["target_wake_delay_requested_frames"] =
        static_cast<qint64>(metrics.targetWakeDelayRequestedFrames);
    executionInjection["target_wake_delay_coarse_sleep_eligible_frames"] =
        static_cast<qint64>(metrics.targetWakeDelayEligibleFrames);
    executionInjection["target_wake_delay_accounting_complete"] =
        saturatingAdd(
            metrics.injectedTargetWakeDelayUs,
            metrics.suppressedTargetWakeDelayUs) ==
                metrics.requestedTargetWakeDelayUs;
    executionInjection["target_wake_effect_accounting_complete"] =
        saturatingAdd(
            metrics.absorbedTargetWakeDelayUs,
            metrics.targetWakeExecutionDelayUs) ==
                metrics.injectedTargetWakeDelayUs;
    executionInjection["preparation_delay_total_us"] = static_cast<qint64>(
        metrics.injectedPreparationDelayUs);
    executionInjection["submission_delay_total_us"] = static_cast<qint64>(
        metrics.injectedSubmissionDelayUs);
    executionInjection["display_transition_delay_total_us"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionDelayUs);
    executionInjection["display_transition_delay_frames"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionDelayFrames);
    executionInjection["spacing_guard_feedback_total_us"] =
        static_cast<qint64>(
            metrics.injectedSpacingGuardFeedbackUs);
    executionInjection["spacing_guard_feedback_frames"] =
        static_cast<qint64>(
            metrics.injectedSpacingGuardFeedbackFrames);
    executionInjection["scheduler_feedback_scope"] =
        "render/target wake injections apply only when the reconstructed candidate wait would enter VrrTargetWaiter's coarse-sleep path; ineligible requests are reported as suppressed and cannot move execution or contaminate scheduler learning. Eligible injections move candidate execution and feed the matching learned delay; preparation/submission injections add work without changing scheduler labels; spacing injection raises the candidate's reconstructed second-check feedback without modifying the captured reference";
    executionInjection["display_transition_delay_scope"] =
        "added after the candidate CPU Present boundary; changes modeled raster/free-running transition phase without changing CPU submission spacing, latency, or controller learning";
    executionInjection["submission_advance_requested_total_us"] =
        static_cast<qint64>(
            metrics.injectedSubmissionAdvanceRequestedUs);
    executionInjection["submission_advance_applied_total_us"] =
        static_cast<qint64>(
            metrics.injectedSubmissionAdvanceAppliedUs);
    executionInjection["submission_advance_clamped_frames"] =
        static_cast<qint64>(
            metrics.injectedSubmissionAdvanceClampedFrames);
    QJsonObject earlySubmissionOutcomes;
    earlySubmissionOutcomes["frames_with_applied_advance"] =
        static_cast<qint64>(metrics.injectedSubmissionAdvanceFrames);
    earlySubmissionOutcomes["modelled_interval_violations"] =
        static_cast<qint64>(
            metrics.injectedAdvanceIntervalViolations);
    earlySubmissionOutcomes["raster_certain_active"] =
        static_cast<qint64>(
            metrics.injectedAdvanceRasterCertainActive);
    earlySubmissionOutcomes["raster_possible_active"] =
        static_cast<qint64>(
            metrics.injectedAdvanceRasterPossibleActive);
    earlySubmissionOutcomes["raster_inactive_in_both_models"] =
        static_cast<qint64>(
            metrics.injectedAdvanceRasterInactive);
    earlySubmissionOutcomes["raster_latched_suppressed"] =
        static_cast<qint64>(
            metrics.injectedAdvanceRasterLatchedSuppressed);
    earlySubmissionOutcomes["raster_unclassified"] =
        static_cast<qint64>(
            metrics.injectedAdvanceRasterUnclassified);
    const uint64_t injectedAdvanceRasterOutcomes =
        metrics.injectedAdvanceRasterCertainActive +
        metrics.injectedAdvanceRasterPossibleActive +
        metrics.injectedAdvanceRasterInactive +
        metrics.injectedAdvanceRasterLatchedSuppressed +
        metrics.injectedAdvanceRasterUnclassified;
    earlySubmissionOutcomes["raster_outcome_rows"] =
        static_cast<qint64>(injectedAdvanceRasterOutcomes);
    earlySubmissionOutcomes["outcome_accounting_complete"] =
        injectedAdvanceRasterOutcomes ==
            metrics.injectedSubmissionAdvanceFrames;
    QJsonObject equalityAnchoredOutcomes;
    equalityAnchoredOutcomes["comparisons"] = static_cast<qint64>(
        metrics.injectedAdvanceExactRefreshComparisons);
    equalityAnchoredOutcomes["active"] = static_cast<qint64>(
        metrics.injectedAdvanceExactRefreshActive);
    equalityAnchoredOutcomes["boundary_uncertain"] = static_cast<qint64>(
        metrics.injectedAdvanceExactRefreshBoundary);
    equalityAnchoredOutcomes["before_active_scanout"] = static_cast<qint64>(
        metrics.injectedAdvanceExactRefreshBefore);
    equalityAnchoredOutcomes["after_active_scanout"] = static_cast<qint64>(
        metrics.injectedAdvanceExactRefreshAfterActive);
    equalityAnchoredOutcomes["latched_suppressed"] = static_cast<qint64>(
        metrics.injectedAdvanceExactRefreshLatched);
    equalityAnchoredOutcomes["unclassified"] = static_cast<qint64>(
        metrics.injectedAdvanceExactRefreshUnclassified);
    equalityAnchoredOutcomes["outcome_accounting_complete"] =
        metrics.injectedAdvanceExactRefreshComparisons ==
            metrics.injectedAdvanceExactRefreshActive +
            metrics.injectedAdvanceExactRefreshBoundary +
            metrics.injectedAdvanceExactRefreshBefore +
            metrics.injectedAdvanceExactRefreshAfterActive +
            metrics.injectedAdvanceExactRefreshLatched +
            metrics.injectedAdvanceExactRefreshUnclassified;
    earlySubmissionOutcomes["equality_anchored_refresh_outcomes"] =
        equalityAnchoredOutcomes;
    executionInjection["early_submission_outcomes"] =
        earlySubmissionOutcomes;
    QJsonObject displayTransitionOutcomes;
    displayTransitionOutcomes["frames_with_applied_delay"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionDelayFrames);
    displayTransitionOutcomes["raster_certain_active"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionRasterCertainActive);
    displayTransitionOutcomes["raster_possible_active"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionRasterPossibleActive);
    displayTransitionOutcomes["raster_inactive_in_both_models"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionRasterInactive);
    displayTransitionOutcomes["raster_latched_suppressed"] =
        static_cast<qint64>(
            metrics.
                injectedDisplayTransitionRasterLatchedSuppressed);
    displayTransitionOutcomes["raster_unclassified"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionRasterUnclassified);
    const uint64_t injectedDisplayTransitionRasterOutcomes =
        metrics.injectedDisplayTransitionRasterCertainActive +
        metrics.injectedDisplayTransitionRasterPossibleActive +
        metrics.injectedDisplayTransitionRasterInactive +
        metrics.injectedDisplayTransitionRasterLatchedSuppressed +
        metrics.injectedDisplayTransitionRasterUnclassified;
    displayTransitionOutcomes["raster_outcome_rows"] =
        static_cast<qint64>(
            injectedDisplayTransitionRasterOutcomes);
    displayTransitionOutcomes["outcome_accounting_complete"] =
        injectedDisplayTransitionRasterOutcomes ==
            metrics.injectedDisplayTransitionDelayFrames;
    QJsonObject displayTransitionExactOutcomes;
    displayTransitionExactOutcomes["comparisons"] =
        static_cast<qint64>(
            metrics.
                injectedDisplayTransitionExactRefreshComparisons);
    displayTransitionExactOutcomes["active"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionExactRefreshActive);
    displayTransitionExactOutcomes["boundary_uncertain"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionExactRefreshBoundary);
    displayTransitionExactOutcomes["before_active_scanout"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionExactRefreshBefore);
    displayTransitionExactOutcomes["after_active_scanout"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionExactRefreshAfterActive);
    displayTransitionExactOutcomes["latched_suppressed"] =
        static_cast<qint64>(
            metrics.injectedDisplayTransitionExactRefreshLatched);
    displayTransitionExactOutcomes["unclassified"] =
        static_cast<qint64>(
            metrics.
                injectedDisplayTransitionExactRefreshUnclassified);
    const uint64_t injectedDisplayTransitionExactOutcomes =
        metrics.injectedDisplayTransitionExactRefreshActive +
        metrics.injectedDisplayTransitionExactRefreshBoundary +
        metrics.injectedDisplayTransitionExactRefreshBefore +
        metrics.injectedDisplayTransitionExactRefreshAfterActive +
        metrics.injectedDisplayTransitionExactRefreshLatched +
        metrics.injectedDisplayTransitionExactRefreshUnclassified;
    displayTransitionExactOutcomes["outcome_accounting_complete"] =
        metrics.injectedDisplayTransitionExactRefreshComparisons ==
            injectedDisplayTransitionExactOutcomes;
    displayTransitionOutcomes["equality_anchored_refresh_outcomes"] =
        displayTransitionExactOutcomes;
    executionInjection["display_transition_delay_outcomes"] =
        displayTransitionOutcomes;
    executionInjection["deterministic"] = true;
    simulation["execution_injection"] = executionInjection;
    if (scenario.mode == "worker") {
        QJsonObject worker;
        worker["model"] = "recorded-arrival-capacity-audit-v1";
        worker["arrivals"] = static_cast<qint64>(metrics.workerArrivals);
        worker["accepted"] = static_cast<qint64>(metrics.workerAccepted);
        worker["unaccepted_arrivals"] = static_cast<qint64>(
            metrics.workerArrivals >= metrics.workerAccepted ?
                metrics.workerArrivals - metrics.workerAccepted : 0);
        worker["capacity_evictions"] = static_cast<qint64>(
            metrics.workerCapacityEvictions);
        worker["all_arrivals_accepted"] =
            metrics.workerAccepted == metrics.workerArrivals;
        worker["zero_capacity_evictions"] =
            metrics.workerCapacityEvictions == 0;
        worker["scanout_prediction"] = false;
        simulation["worker"] = worker;
    }
    simulation["drops"] = static_cast<qint64>(scenario.mode == "worker" ?
        metrics.workerCapacityEvictions : metrics.originalDrops);
    simulation["latency_us"] = simulatedLatency;
    // How far the worker-occupancy model moved each decision from the
    // recorded instant. A median shift beyond one source period means the
    // candidate keeps the single worker busier than the source cadence; the
    // live worker would shed frames through its stale check, which this
    // fixed-admission replay does not emulate, so latency runs away instead.
    simulation["occupancy_decision_shift_us"] = distributionObject(
        metrics.occupancyDecisionShiftUs);
    simulation["worker_saturated"] =
        metrics.occupancyDecisionShiftUs.count != 0 &&
        metrics.occupancyDecisionShiftUs.percentile(50) >
            periodForRate(simulatedStreamFps > 0 ?
                              simulatedStreamFps : 1);
    simulation["playout_delay_us"] = distributionObject(
        metrics.simulatedPlayoutDelayUs);
    simulation["readiness"] = readinessObject(
        metrics.simulatedReadiness,
        scenario.controller.playoutResponsiveBuffer);
    simulation["on_time_target_percent"] = scenario.controller.playoutOnTimeTargetPerMillion / 10000.0;
    simulation["readiness_history_us"] = qint64(scenario.controller.playoutReadinessWindowUs);
    simulation["sender_cadence"] = senderCadenceObject(
        metrics.simulatedSenderCadence,
        metrics.lastArrivalUs >= metrics.firstArrivalUs ?
            metrics.lastArrivalUs - metrics.firstArrivalUs : 0);
    simulation["absolute_submit_error_us"] = distributionObject(
        metrics.simulatedAbsoluteSubmitError);
    simulation["target_drift_us"] = distributionObject(
        metrics.simulatedTargetDrift);
    simulation["submission_drift_us"] = distributionObject(
        metrics.simulatedSubmissionDrift);
    simulation["cadence_error_us"] = distributionObject(
        metrics.simulatedCadenceError);
    simulation["gap_aware_cadence_by_rounded_source_rate_fps"] =
        cadenceBandsObject(metrics.simulatedRateBands);
    simulation["jerk_anomaly_windows"] = anomalyWindowObject(
        metrics.simulatedJerkAnomalies);
    QJsonObject pairedSubmissionDelta;
    pairedSubmissionDelta["signed_us"] = signedAccumulatorObject(
        metrics.pairedSubmissionDelta);
    pairedSubmissionDelta["absolute_us"] = boundedDistributionObject(
        metrics.pairedAbsoluteSubmissionDelta);
    pairedSubmissionDelta["scope"] =
        "candidate minus recorded submission for the same decoded frame";
    simulation["paired_submission_delta"] = pairedSubmissionDelta;
    simulation["tear"] = simulatedTears;

    QJsonObject fidelity;
    fidelity["reference_target_drift_us"] = distributionObject(
        metrics.referenceTargetDrift);
    fidelity["reference_source_interval_drift_us"] = distributionObject(
        metrics.referenceSourceIntervalDrift);
    fidelity["reference_source_time_drift_us"] = distributionObject(
        metrics.referenceSourceTimeDrift);
    fidelity["reference_source_period_drift_us"] = distributionObject(
        metrics.referenceSourcePeriodDrift);
    fidelity["reference_ready_offset_drift_us"] = distributionObject(
        metrics.referenceReadyOffsetDrift);
    fidelity["reference_readiness_budget_drift_us"] = distributionObject(
        metrics.referenceReadinessBudgetDrift);
    fidelity["reference_timing_budget_drift_us"] = distributionObject(
        metrics.referenceTimingBudgetDrift);
    fidelity["reference_render_lead_drift_us"] = distributionObject(
        metrics.referenceRenderLeadDrift);
    fidelity["reference_render_wake_lead_drift_us"] = distributionObject(
        metrics.referenceRenderWakeLeadDrift);
    fidelity["reference_target_wake_lead_drift_us"] = distributionObject(
        metrics.referenceTargetWakeLeadDrift);
    fidelity["reference_guard_drift_us"] = distributionObject(
        metrics.referenceGuardDrift);
    fidelity["reference_headroom_drift_us"] = distributionObject(
        metrics.referenceHeadroomDrift);
    fidelity["reference_render_start_drift_us"] = distributionObject(
        metrics.referenceRenderStartDrift);
    QJsonObject referenceDecisionBooleanMismatches;
    referenceDecisionBooleanMismatches["latched_presentation"] =
        static_cast<qint64>(
            metrics.referenceLatchedPresentationMismatches);
    referenceDecisionBooleanMismatches["used_rtp_timestamp"] =
        static_cast<qint64>(
            metrics.referenceUsedRtpTimestampMismatches);
    referenceDecisionBooleanMismatches["cadence_eligible"] =
        static_cast<qint64>(
            metrics.referenceCadenceEligibleMismatches);
    referenceDecisionBooleanMismatches["source_rate_changed"] =
        static_cast<qint64>(
            metrics.referenceSourceRateChangedMismatches);
    referenceDecisionBooleanMismatches["phase_discontinuity"] =
        static_cast<qint64>(
            metrics.referencePhaseDiscontinuityMismatches);
    referenceDecisionBooleanMismatches["rebased"] =
        static_cast<qint64>(metrics.referenceRebasedMismatches);
    fidelity["reference_decision_boolean_mismatches"] =
        referenceDecisionBooleanMismatches;
    QJsonObject referenceControllerDiagnostics;
    referenceControllerDiagnostics["readiness_phase_drift_us"] =
        distributionObject(metrics.referenceReadinessPhaseDrift);
    referenceControllerDiagnostics["readiness_demand_drift_us"] =
        distributionObject(metrics.referenceReadinessDemandDrift);
    referenceControllerDiagnostics["applied_readiness_reserve_drift_us"] =
        distributionObject(
            metrics.referenceAppliedReadinessReserveDrift);
    referenceControllerDiagnostics["render_baseline_drift_us"] =
        distributionObject(metrics.referenceRenderBaselineDrift);
    referenceControllerDiagnostics["render_insurance_drift_us"] =
        distributionObject(metrics.referenceRenderInsuranceDrift);
    referenceControllerDiagnostics["pacing_latency_budget_drift_us"] =
        distributionObject(metrics.referencePacingLatencyBudgetDrift);
    referenceControllerDiagnostics["cadence_sample_count_drift"] =
        distributionObject(metrics.referenceCadenceSampleCountDrift);
    referenceControllerDiagnostics["rate_candidate_sample_count_drift"] =
        distributionObject(
            metrics.referenceRateCandidateSampleCountDrift);
    referenceControllerDiagnostics["readiness_sample_count_drift"] =
        distributionObject(metrics.referenceReadinessSampleCountDrift);
    referenceControllerDiagnostics["preparation_sample_count_drift"] =
        distributionObject(metrics.referencePreparationSampleCountDrift);
    referenceControllerDiagnostics["render_scheduler_sample_count_drift"] =
        distributionObject(
            metrics.referenceRenderSchedulerSampleCountDrift);
    referenceControllerDiagnostics["target_scheduler_sample_count_drift"] =
        distributionObject(
            metrics.referenceTargetSchedulerSampleCountDrift);
    referenceControllerDiagnostics["clean_spacing_frames_drift"] =
        distributionObject(metrics.referenceCleanSpacingFramesDrift);
    referenceControllerDiagnostics["phase_error_frames_drift"] =
        distributionObject(metrics.referencePhaseErrorFramesDrift);
    referenceControllerDiagnostics["readiness_model_valid_mismatches"] =
        static_cast<qint64>(
            metrics.referenceReadinessModelValidMismatches);
    referenceControllerDiagnostics["exact"] =
        referenceControllerDiagnosticsExact;
    referenceControllerDiagnostics["scope"] =
        "decision_valid worker rows; producer-side terminal rows do not read worker-owned controller state";
    fidelity["reference_controller_diagnostics"] =
        referenceControllerDiagnostics;
    fidelity["external_rebase_telemetry_available"] =
        metrics.externalRebaseTelemetryAvailable;
    fidelity["window_state_cause_telemetry_available"] =
        metrics.windowStateCauseTelemetryAvailable;
    fidelity["captured_external_rebase_events_replayed"] =
        static_cast<qint64>(
        metrics.capturedRebaseEventsReplayed);
    fidelity["invalid_controller_lifecycle_rows"] = static_cast<qint64>(
        metrics.invalidControllerLifecycleRows);
    fidelity["exact_reference_targets"] = static_cast<qint64>(
        metrics.exactReferenceTargets);
    fidelity["reference_decision_state_exact"] =
        referenceDecisionStateExact;
    fidelity["reference_controller_diagnostics_exact"] =
        referenceControllerDiagnosticsExact;
    fidelity["exact_simulated_submissions"] = static_cast<qint64>(
        metrics.exactSimulatedSubmissions);
    fidelity["exact_tear_classifications"] = static_cast<qint64>(
        metrics.exactTearClassifications);
    fidelity["exact_raster_envelope_classifications"] =
        static_cast<qint64>(metrics.exactRasterEnvelopeClassifications);
    fidelity["exact_refresh_phase_comparisons"] = static_cast<qint64>(
        metrics.simulatedRecordedRefreshComparisons);
    fidelity["exact_present_refresh_phase_matches"] = static_cast<qint64>(
        metrics.exactPresentRefreshPhaseMatches);
    fidelity["recorded_refresh_phase_baseline_exact"] =
        metrics.simulatedRecordedRefreshComparisons ==
            metrics.exactPresentRefreshCorrelations &&
        countTotal(metrics.observedExactRefreshClassifications) ==
            metrics.exactPresentRefreshCorrelations &&
        countTotal(metrics.simulatedExactRefreshClassifications) ==
            metrics.simulatedRecordedRefreshComparisons &&
        metrics.exactPresentRefreshPhaseMatches ==
            metrics.exactPresentRefreshCorrelations;
    fidelity["invalid_execution_residuals"] = static_cast<qint64>(
        metrics.invalidExecutionResiduals);
    fidelity["baseline_exact"] = metrics.traceSchema == 5 &&
        arrivalSequenceComplete &&
        semanticIntegrityReady &&
        metrics.displayRefreshMismatchRows == 0 &&
        metrics.streamRateMismatchRows == 0 &&
        metrics.additionalQueuedFrameMismatchRows == 0 &&
        metrics.sessionAllowTearingMismatchRows == 0 &&
        metrics.latchCapabilityMismatchRows == 0 &&
        metrics.displayPeriodMismatchRows == 0 &&
        metrics.controllerParameterMismatchRows == 0 &&
        metrics.decodeToArrivalOrderViolations == 0 &&
        metrics.decodeReadinessOrderViolations == 0 &&
        metrics.arrivalToDequeueOrderViolations == 0 &&
        metrics.dequeueToDecisionOrderViolations == 0 &&
        metrics.controllerCallOrderViolations == 0 &&
        metrics.controllerCallDurationMismatchRows == 0 &&
        metrics.staleCheckOrderViolations == 0 &&
        metrics.staleAgeMismatchRows == 0 &&
        metrics.waitBoundaryOrderViolations == 0 &&
        metrics.correctionWaitOrderViolations == 0 &&
        metrics.terminalTimeOrderViolations == 0 &&
        metrics.preparationOrderViolations == 0 &&
        metrics.preparationDurationMismatchRows == 0 &&
        metrics.presentTimingIntegrityTelemetryAvailable &&
        metrics.presentOperationOrderViolations == 0 &&
        metrics.presentOperationDurationMismatchRows == 0 &&
        metrics.nativePresentOrderViolations == 0 &&
        metrics.nativePresentDurationMismatchRows == 0 &&
        metrics.gpuReadyOrderViolations == 0 &&
        metrics.gpuReadyDurationMismatchRows == 0 &&
        (!metrics.gpuReadyStageTimingTelemetryAvailable ||
         metrics.gpuReadyStageTimingRelationshipMismatchRows == 0) &&
        (!metrics.gpuReadyBoundsTelemetryAvailable ||
         (metrics.gpuReadyFenceRelationshipMismatchRows == 0 &&
          metrics.gpuReadyBoundsDerivationMismatchRows == 0)) &&
        (!metrics.postPresentQueryTimingTelemetryAvailable ||
         metrics.postPresentQueryTimingRelationshipMismatchRows == 0) &&
        metrics.submissionTimestampRegressions == 0 &&
        metrics.invalidControllerLifecycleRows == 0 &&
        metrics.externalRebaseTelemetryAvailable &&
        metrics.windowStateCauseTelemetryAvailable &&
        metrics.scheduled != 0 &&
        referenceDecisionStateExact &&
        referenceControllerDiagnosticsExact &&
        metrics.exactSimulatedSubmissions ==
            presentedFrames &&
        metrics.exactTearClassifications == metrics.delivered &&
        metrics.exactRasterEnvelopeClassifications == presentedFrames &&
        metrics.simulatedRecordedRefreshComparisons ==
            metrics.exactPresentRefreshCorrelations &&
        metrics.exactPresentRefreshPhaseMatches ==
            metrics.exactPresentRefreshCorrelations &&
        metrics.invalidExecutionResiduals == 0;
    fidelity["baseline_scope"] = metrics.traceFooter.present ?
        "all arrivals accounted for by the clean-close footer" :
        "legacy capture without a clean-close footer; exact full-session accounting is unavailable";

    const uint64_t classifiedRasterRows =
        metrics.observedRasterEnvelope.certainActive +
        metrics.observedRasterEnvelope.possibleActive +
        metrics.observedRasterEnvelope.inactiveInBothModels;
    const uint64_t simulatedClassifiedRasterRows =
        metrics.simulatedRasterEnvelope.certainActive +
        metrics.simulatedRasterEnvelope.possibleActive +
        metrics.simulatedRasterEnvelope.inactiveInBothModels;
    const bool rasterOutcomeAccountingReady =
        metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions +
            metrics.observedRasterEnvelope.latchedSuppressed ==
                presentedFrames &&
        classifiedRasterRows +
            metrics.observedRasterEnvelope.unclassified ==
                metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions;
    const bool simulatedRasterOutcomeAccountingReady =
        metrics.simulatedRasterEnvelope.eligibleAdaptiveSubmissions +
            metrics.simulatedRasterEnvelope.latchedSuppressed ==
                presentedFrames &&
        simulatedClassifiedRasterRows +
            metrics.simulatedRasterEnvelope.unclassified ==
                metrics.simulatedRasterEnvelope.eligibleAdaptiveSubmissions;
    const bool displayTransitionInjectionOutcomeAccountingReady =
        injectedDisplayTransitionRasterOutcomes ==
            metrics.injectedDisplayTransitionDelayFrames;
    const bool displayTransitionInjectionExactOutcomeAccountingReady =
        injectedDisplayTransitionExactOutcomes ==
            metrics.injectedDisplayTransitionExactRefreshComparisons;
    const bool renderWakeInjectionAccountingReady =
        saturatingAdd(
            metrics.injectedRenderWakeDelayUs,
            metrics.suppressedRenderWakeDelayUs) ==
                metrics.requestedRenderWakeDelayUs;
    const bool renderWakeInjectionEffectAccountingReady =
        saturatingAdd(
            metrics.absorbedRenderWakeDelayUs,
            metrics.renderWakeExecutionDelayUs) ==
                metrics.injectedRenderWakeDelayUs;
    const bool targetWakeInjectionAccountingReady =
        saturatingAdd(
            metrics.injectedTargetWakeDelayUs,
            metrics.suppressedTargetWakeDelayUs) ==
                metrics.requestedTargetWakeDelayUs;
    const bool targetWakeInjectionEffectAccountingReady =
        saturatingAdd(
            metrics.absorbedTargetWakeDelayUs,
            metrics.targetWakeExecutionDelayUs) ==
                metrics.injectedTargetWakeDelayUs;
    const bool waitLifecycleCoverageReady =
        metrics.waitLifecycleTelemetryAvailable &&
        metrics.renderWaitExpectedRows != 0 &&
        metrics.targetWaitExpectedRows != 0 &&
        metrics.renderWaitLifecycleRows ==
            metrics.renderWaitExpectedRows &&
        metrics.targetWaitLifecycleRows ==
            metrics.targetWaitExpectedRows &&
        metrics.renderWaitLifecycleRelationshipMismatchRows == 0 &&
        metrics.targetWaitLifecycleRelationshipMismatchRows == 0 &&
        metrics.renderWaitCleanCompletionRows ==
            metrics.renderWaitLifecycleRows &&
        metrics.targetWaitCleanCompletionRows ==
            metrics.targetWaitLifecycleRows &&
        metrics.renderWaitClockStallRows == 0 &&
        metrics.targetWaitClockStallRows == 0 &&
        metrics.renderWaitYieldLimitRows == 0 &&
        metrics.targetWaitYieldLimitRows == 0 &&
        metrics.renderWaitEarlyReturnRows == 0 &&
        metrics.targetWaitEarlyReturnRows == 0;
    const bool presentedRawAnchorCoverageReady =
        presentedFrames != 0 &&
        static_cast<long double>(
            metrics.presentedRawPrePresentAnchorValidRows) /
            static_cast<long double>(presentedFrames) >= 0.99L;
    const bool freshLatchCoverageReady =
        presentedFrames != 0 &&
        static_cast<long double>(metrics.uniqueLatchSamples) /
            static_cast<long double>(presentedFrames) >= 0.99L;
    const bool freshLatchAssociationReady =
        metrics.uniqueLatchSamples != 0 &&
        static_cast<long double>(
            metrics.freshLatchSamplesMatchedToSubmission) /
            static_cast<long double>(metrics.uniqueLatchSamples) >= 0.99L;
    const bool rawAdaptiveAnchorCoverageReady =
        metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions == 0 ||
        static_cast<long double>(
            metrics.adaptiveRawPrePresentAnchorValidRows) /
            static_cast<long double>(
                metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions) >=
            0.99L;
    const bool adaptiveAnchorCoverageReady =
        metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions == 0 ||
        static_cast<long double>(metrics.adaptivePrePresentAnchorValidRows) /
            static_cast<long double>(
                metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions) >=
            0.99L;
    const bool simulatedAdaptiveAnchorCoverageReady =
        metrics.simulatedRasterEnvelope.eligibleAdaptiveSubmissions == 0 ||
        static_cast<long double>(
            metrics.simulatedAdaptivePrePresentAnchorValidRows) /
            static_cast<long double>(
                metrics.simulatedRasterEnvelope.eligibleAdaptiveSubmissions) >=
            0.99L;
    const bool rasterClassificationCoverageReady =
        metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions == 0 ||
        static_cast<long double>(classifiedRasterRows) /
            static_cast<long double>(
                metrics.observedRasterEnvelope.eligibleAdaptiveSubmissions) >=
            0.99L;
    const bool simulatedRasterClassificationCoverageReady =
        metrics.simulatedRasterEnvelope.eligibleAdaptiveSubmissions == 0 ||
        static_cast<long double>(simulatedClassifiedRasterRows) /
            static_cast<long double>(
                metrics.simulatedRasterEnvelope.eligibleAdaptiveSubmissions) >=
            0.99L;
    const uint64_t counterfactualFreeRunningClassifiedRows =
        saturatingAdd(
            metrics.counterfactualFreeRunningBaselines,
            metrics.counterfactualFreeRunningComparisons);
    const bool counterfactualFreeRunningAccountingReady =
        saturatingAdd(
            counterfactualFreeRunningClassifiedRows,
            metrics.counterfactualFreeRunningUnseededRows) ==
                metrics.counterfactualFreeRunningAdaptiveRows;
    const bool counterfactualFreeRunningCoverageReady =
        metrics.counterfactualFreeRunningAdaptiveRows != 0 &&
        static_cast<long double>(
            counterfactualFreeRunningClassifiedRows) /
            static_cast<long double>(
            metrics.counterfactualFreeRunningAdaptiveRows) >= 0.99L;
    const uint64_t minimumCapturedPhaseUncertaintyUs =
        saturatingAdd(
            metrics.qpcCorrelationHalfSpanUncertaintyUsMaximum,
            1);
    const bool capturedClockUncertaintyCovered =
        metrics.qpcCorrelationValidRows != 0 &&
        scenario.display.phaseUncertaintyUs >=
            minimumCapturedPhaseUncertaintyUs;
    const bool equalityAnchorCorrelationCoverageReady =
        metrics.exactPresentRefreshTimestampSamples != 0 &&
        static_cast<long double>(
            metrics.exactPresentRefreshCorrelations) /
            static_cast<long double>(
                metrics.exactPresentRefreshTimestampSamples) >= 0.99L;
    const bool deepBeforeCarryForwardReady =
        metrics.deepBeforeStateEligibleRows != 0 &&
        static_cast<long double>(metrics.deepBeforeStateComparisons) /
            static_cast<long double>(
                metrics.deepBeforeStateEligibleRows) >= 0.99L &&
        metrics.deepBeforeStateValidityMismatches == 0 &&
        metrics.deepBeforePresentCountMismatches == 0 &&
        metrics.deepBeforeFrameStatsMismatches == 0;
    const uint64_t exactRasterValidationRows = countTotal(
        metrics.observedRasterValidation);
    const bool exactRasterValidationReady =
        exactRasterValidationRows >= kMinimumExactRasterValidationSamples &&
        exactRasterValidationRows ==
            metrics.exactPresentRefreshCorrelations &&
        metrics.observedRasterValidationContradictions == 0;
    const bool syncAnchorIntegrityReady =
        metrics.prePresentAnchorSequenceRegressions == 0 &&
        metrics.prePresentAnchorMissingRefreshSequence == 0 &&
        metrics.prePresentAnchorTimeRegressions == 0 &&
        metrics.prePresentAnchorCausalOrderViolations == 0 &&
        metrics.prePresentAnchorTimestampJitterBeyondTolerance == 0 &&
        metrics.prePresentAnchorNonadvancingTime == 0 &&
        metrics.prePresentAnchorImplausiblyShortIntervals == 0 &&
        unifiedRasterAnchorIntegrityReady;
    const bool postSyncAnchorIntegrityReady =
        metrics.postPresentAnchorSequenceRegressions == 0 &&
        metrics.postPresentAnchorMissingRefreshSequence == 0 &&
        metrics.postPresentAnchorTimeRegressions == 0 &&
        metrics.postPresentAnchorCausalOrderViolations == 0 &&
        metrics.postPresentAnchorTimestampJitterBeyondTolerance == 0 &&
        metrics.postPresentAnchorNonadvancingTime == 0 &&
        metrics.postPresentAnchorImplausiblyShortIntervals == 0 &&
        unifiedRasterAnchorIntegrityReady;
    const bool sessionConfigIntegrityReady =
        metrics.displayRefreshMismatchRows == 0 &&
        metrics.streamRateMismatchRows == 0 &&
        metrics.additionalQueuedFrameMismatchRows == 0 &&
        metrics.sessionAllowTearingMismatchRows == 0 &&
        metrics.latchCapabilityMismatchRows == 0 &&
        metrics.displayPeriodMismatchRows == 0 &&
        metrics.controllerParameterMismatchRows == 0;
    const bool timestampIntegrityReady =
        metrics.decodeToArrivalOrderViolations == 0 &&
        metrics.decodeReadinessOrderViolations == 0 &&
        metrics.arrivalToDequeueOrderViolations == 0 &&
        metrics.dequeueToDecisionOrderViolations == 0 &&
        metrics.controllerCallOrderViolations == 0 &&
        metrics.controllerCallDurationMismatchRows == 0 &&
        metrics.staleCheckOrderViolations == 0 &&
        metrics.staleAgeMismatchRows == 0 &&
        metrics.waitBoundaryOrderViolations == 0 &&
        metrics.correctionWaitOrderViolations == 0 &&
        metrics.terminalTimeOrderViolations == 0 &&
        metrics.preparationOrderViolations == 0 &&
        metrics.preparationDurationMismatchRows == 0 &&
        metrics.presentTimingIntegrityTelemetryAvailable &&
        metrics.presentOperationOrderViolations == 0 &&
        metrics.presentOperationDurationMismatchRows == 0 &&
        metrics.nativePresentOrderViolations == 0 &&
        metrics.nativePresentDurationMismatchRows == 0 &&
        metrics.gpuReadyOrderViolations == 0 &&
        metrics.gpuReadyDurationMismatchRows == 0 &&
        (!metrics.gpuReadyStageTimingTelemetryAvailable ||
         metrics.gpuReadyStageTimingRelationshipMismatchRows == 0) &&
        (!metrics.gpuReadyBoundsTelemetryAvailable ||
         (metrics.gpuReadyFenceRelationshipMismatchRows == 0 &&
          metrics.gpuReadyBoundsDerivationMismatchRows == 0)) &&
        (!metrics.postPresentQueryTimingTelemetryAvailable ||
         metrics.postPresentQueryTimingRelationshipMismatchRows == 0) &&
        metrics.submissionTimestampRegressions == 0;
    const bool controllerReplayReady =
        metrics.traceSchema == 5 &&
        arrivalSequenceComplete &&
        sessionConfigIntegrityReady &&
        timestampIntegrityReady &&
        semanticIntegrityReady &&
        referenceDecisionStateExact &&
        referenceControllerDiagnosticsExact &&
        metrics.externalRebaseTelemetryAvailable &&
        metrics.windowStateCauseTelemetryAvailable &&
        metrics.invalidExecutionResiduals == 0 &&
        metrics.invalidControllerLifecycleRows == 0;
    const bool diagnosticCaptureReady =
        controllerReplayReady &&
        footerContentHashValid &&
        metrics.deepTraceRows == metrics.delivered &&
        nativeOutcomeAndQpcIntegrityReady &&
        deepBeforeCarryForwardReady &&
        presentedFrames != 0 &&
        metrics.presentedPresentOperationIntegrityRows == presentedFrames &&
        metrics.presenterSubmissionTimingTelemetryAvailable &&
        metrics.presenterSubmissionTimestampUsedRows == presentedFrames &&
        metrics.presenterSubmissionTimingRelationshipMismatchRows == 0 &&
        metrics.spacingCorrectionTelemetryAvailable &&
        metrics.spacingLifecycleTimingTelemetryAvailable &&
        metrics.spacingLifecycleTimingValidatedRows ==
            metrics.normalPresentAttemptRows &&
        metrics.spacingLifecycleTimingRelationshipMismatchRows == 0 &&
        waitLifecycleCoverageReady &&
        metrics.completionQueueDepthTelemetryAvailable &&
        metrics.presentedSubmissionIdValidRows == presentedFrames &&
        metrics.presentedNativePresentTimingValidRows == presentedFrames &&
        metrics.gpuReadyNativeResultTelemetryAvailable &&
        metrics.presentedGpuReadyNativeSuccessRows == presentedFrames &&
        metrics.gpuReadyNativeResultRelationshipMismatchRows == 0 &&
        metrics.presentedGpuReadyTimingValidRows == presentedFrames &&
        metrics.gpuReadyStageTimingTelemetryAvailable &&
        metrics.gpuReadyStageTimingRelationshipMismatchRows == 0 &&
        metrics.gpuReadyBoundsTelemetryAvailable &&
        metrics.presentedGpuReadyBoundsValidRows == presentedFrames &&
        metrics.gpuReadyFenceRelationshipMismatchRows == 0 &&
        metrics.gpuReadyBoundsDerivationMismatchRows == 0 &&
        metrics.postPresentQueryTimingTelemetryAvailable &&
        metrics.presentedPostPresentQueryTimingValidRows ==
            presentedFrames &&
        metrics.postPresentQueryTimingRelationshipMismatchRows == 0 &&
        metrics.nativePresentBoundaryComparisons == presentedFrames &&
        metrics.nativePresentBoundaryMismatches == 0 &&
        metrics.submissionSequenceResets == 0 &&
        metrics.submissionSequenceDuplicates == 0 &&
        metrics.latchSequenceResets == 0 &&
        syncAnchorIntegrityReady &&
        postSyncAnchorIntegrityReady &&
        presentedRawAnchorCoverageReady &&
        freshLatchCoverageReady &&
        freshLatchAssociationReady &&
        equalityAnchorCorrelationCoverageReady &&
        metrics.exactPresentRefreshCorrelations >=
            kMinimumExactRasterValidationSamples;
    const bool rasterSimulationReady =
        diagnosticCaptureReady &&
        scenario.mode == "fixed" &&
        rasterProbeOverheadRemovalEvidenceComplete &&
        scenario.display.calibrationConfirmed != 0 &&
        simulatedDisplayHz == capturedDisplayHz &&
        nativeDisplayTimingProgressive &&
        nativeDisplayRotationSupported &&
        nativeDisplayScalingSupported &&
        configuredActiveScanoutMatchesSignal &&
        preciseActiveScanoutMatchesSignal &&
        metrics.configuredActiveScanoutMismatchRows == 0 &&
        nativeDisplayTimingNotVirtualized &&
        nativeRasterSamplingReady &&
        nativeRasterSignalRangeReady &&
        nativeRasterModelValidationReady &&
        configuredScanoutPeriodMatchesSignal &&
        counterfactualPeriodMatchesSignal &&
        metrics.configuredScanoutPeriodMismatchRows == 0 &&
        renderWakeInjectionAccountingReady &&
        renderWakeInjectionEffectAccountingReady &&
        targetWakeInjectionAccountingReady &&
        targetWakeInjectionEffectAccountingReady &&
        rasterOutcomeAccountingReady &&
        simulatedRasterOutcomeAccountingReady &&
        displayTransitionInjectionOutcomeAccountingReady &&
        displayTransitionInjectionExactOutcomeAccountingReady &&
        metrics.observedRasterEnvelope.activeScanoutClamped == 0 &&
        metrics.observedRasterEnvelope.scanoutPhaseWindowInvalid == 0 &&
        metrics.simulatedRasterEnvelope.activeScanoutClamped == 0 &&
        metrics.simulatedRasterEnvelope.scanoutPhaseWindowInvalid == 0 &&
        rawAdaptiveAnchorCoverageReady &&
        adaptiveAnchorCoverageReady &&
        simulatedAdaptiveAnchorCoverageReady &&
        capturedClockUncertaintyCovered &&
        rasterClassificationCoverageReady &&
        simulatedRasterClassificationCoverageReady &&
        countTotal(metrics.observedExactRefreshClassifications) ==
            metrics.exactPresentRefreshCorrelations &&
        exactRasterValidationReady;
    const bool counterfactualRefreshTimelineReady =
        rasterSimulationReady &&
        scenario.mode == "fixed" &&
        resolvedCounterfactualPeriodPs != 0 &&
        counterfactualPeriodMatchesSignal &&
        metrics.simulatedLatchedFrames == 0 &&
        metrics.counterfactualFreeRunningAdaptiveRows == presentedFrames &&
        metrics.counterfactualFreeRunningBaselines != 0 &&
        metrics.counterfactualFreeRunningComparisons != 0 &&
        counterfactualFreeRunningAccountingReady &&
        counterfactualFreeRunningCoverageReady &&
        metrics.counterfactualFreeRunningTimeRegressions == 0 &&
        metrics.counterfactualFreeRunningPeriodChanges == 0 &&
        metrics.counterfactualFreeRunningConversionFailures == 0;

    QJsonObject readinessGates;
    readinessGates["schema_5"] = metrics.traceSchema == 5;
    readinessGates["raster_simulation_mode_fixed"] =
        scenario.mode == "fixed";
    readinessGates["display_model_calibration_confirmed"] =
        scenario.display.calibrationConfirmed != 0;
    readinessGates["candidate_display_rate_matches_capture"] =
        simulatedDisplayHz == capturedDisplayHz;
    readinessGates["recorded_arrival_sequence_complete"] =
        arrivalSequenceComplete;
    readinessGates["clean_close_footer_and_exact_row_accounting"] =
        footerAccountingValid &&
        !metrics.traceFooter.sizeCapped &&
        !metrics.traceFooter.writeFailed &&
        metrics.traceFooter.rowsDropped == 0;
    readinessGates["decoded_trace_content_hash_matches_footer"] =
        footerContentHashValid;
    readinessGates["session_configuration_integrity"] =
        sessionConfigIntegrityReady;
    readinessGates["timestamp_integrity"] =
        timestampIntegrityReady;
    readinessGates["row_semantic_integrity"] =
        semanticIntegrityReady;
    readinessGates["present_timing_integrity_fields_available"] =
        metrics.presentTimingIntegrityTelemetryAvailable;
    readinessGates["native_outcome_and_raw_qpc_fields_available"] =
        metrics.nativeOutcomeTelemetryAvailable &&
        metrics.nativePresentContractTelemetryAvailable &&
        metrics.nativeDxgiCapabilityTelemetryAvailable &&
        metrics.nativeVblankVirtualizationTelemetryAvailable &&
        metrics.nativeDisplayTimingTelemetryAvailable &&
        metrics.qpcCorrelationTelemetryAvailable;
    readinessGates["all_normal_present_attempts_are_dxgi"] =
        metrics.normalPresentAttemptRows != 0 &&
        metrics.nativePresentAttemptRows ==
            metrics.normalPresentAttemptRows &&
        metrics.nativeDxgiPresentAttemptRows ==
            metrics.normalPresentAttemptRows &&
        metrics.nativeVulkanPresentAttemptRows == 0;
    readinessGates["native_present_result_all_native_attempts"] =
        metrics.nativePresentAttemptRows != 0 &&
        metrics.nativePresentResultValidRows ==
            metrics.nativePresentAttemptRows;
    readinessGates["native_query_result_all_presented_frames"] =
        presentedFrames != 0 &&
        metrics.presentedSubmissionIdQueryResultValidRows == presentedFrames &&
        metrics.presentedFrameStatsQueryResultValidRows == presentedFrames;
    readinessGates["native_present_parameters_all_dxgi_attempts"] =
        metrics.nativeDxgiPresentAttemptRows != 0 &&
        metrics.nativePresentParametersValidRows ==
            metrics.nativeDxgiPresentAttemptRows;
    readinessGates["native_vrr_state_all_dxgi_attempts"] =
        metrics.nativeDxgiPresentAttemptRows != 0 &&
        metrics.nativeVrrStateValidRows ==
            metrics.nativeDxgiPresentAttemptRows;
    readinessGates[
        "actual_dxgi_swap_chain_capability_all_attempts"] =
        nativeDxgiCapabilityCoverageReady;
    readinessGates["moonlight_foreground_all_dxgi_attempts"] =
        metrics.nativeDxgiPresentAttemptRows != 0 &&
        metrics.nativeForegroundWindowRows ==
            metrics.nativeDxgiPresentAttemptRows;
    readinessGates["native_present_contract_valid"] =
        metrics.nativePresentParameterMismatchRows == 0 &&
        metrics.nativeVrrStateMismatchRows == 0 &&
        metrics.nativeDxgiCapabilityRelationshipMismatchRows == 0 &&
        metrics.nativeDxgiCapabilitySnapshotMismatchRows == 0 &&
        metrics.nativeRenderAdapterLuidRelationshipMismatchRows == 0;
    readinessGates[
        "render_adapter_luid_matches_display_source"] =
        nativeRenderAdapterIdentityReady;
    readinessGates["single_desktop_monitor_for_dxgi_statistics"] =
        singleDesktopMonitorReady;
    readinessGates["native_display_timing_snapshot_complete_and_stable"] =
        nativeDisplayTimingCoverageReady;
    readinessGates[
        "native_vblank_virtualization_startup_probe_recorded"] =
        metrics.nativeVblankVirtualizationTelemetryAvailable &&
        metrics.nativeDxgiPresentAttemptRows != 0 &&
        metrics.nativeVblankVirtualizationProbeCompleteRows ==
            metrics.nativeDxgiPresentAttemptRows &&
        metrics.nativeVblankVirtualizationRelationshipMismatchRows == 0;
    readinessGates[
        "native_vblank_virtualization_disabled_before_swapchains"] =
        nativeVblankVirtualizationDisableReady;
    readinessGates["native_raster_sampling_fields_available"] =
        metrics.nativeRasterTelemetryAvailable;
    readinessGates["physical_display_signal_progressive"] =
        nativeDisplayTimingProgressive;
    readinessGates[
        "physical_display_signal_internally_consistent"] =
        nativeDisplaySignalConsistency.inputsValid &&
        nativeDisplaySignalConsistency.withinTolerance;
    readinessGates[
        "physical_display_signal_vsync_divider_zero"] =
        nativeDisplaySignalHasNoVsyncDivider;
    readinessGates[
        "physical_display_signal_reserved_bits_zero"] =
        nativeDisplaySignalReservedBitsZero;
    readinessGates["physical_display_rotation_identity"] =
        nativeDisplayRotationSupported;
    readinessGates["physical_display_scaling_identity"] =
        nativeDisplayScalingSupported;
    readinessGates[
        "physical_signal_active_scanout_duration_resolved"] =
        nativeDisplaySignalActiveScanoutResolved;
    readinessGates[
        "configured_active_scanout_matches_physical_signal"] =
        configuredActiveScanoutMatchesSignal;
    readinessGates[
        "configured_active_scanout_matches_all_display_epochs"] =
        metrics.configuredActiveScanoutMismatchRows == 0;
    readinessGates[
        "precise_active_scanout_matches_physical_signal"] =
        preciseActiveScanoutMatchesSignal;
    readinessGates[
        "display_path_not_drr_boosted_or_vblank_virtualized"] =
        nativeDisplayTimingNotVirtualized;
    readinessGates["display_path_has_no_unknown_status_flags"] =
        nativeDisplayPathFlagsKnown;
    readinessGates[
        "native_raster_samples_bracket_every_dxgi_present"] =
        nativeRasterSamplingReady;
    readinessGates[
        "native_raster_scanline_scale_inferred_for_all_samples"] =
        nativeRasterScanLineScaleReady;
    readinessGates[
        "native_raster_scanlines_within_physical_signal"] =
        nativeRasterSignalRangeReady;
    readinessGates[
        "native_raster_observations_validate_model_envelope"] =
        nativeRasterModelValidationReady;
    readinessGates[
        "native_raster_anchor_source_accounting_complete"] =
        nativeRasterModelAnchorSourceAccountingReady;
    readinessGates[
        "merged_pre_and_post_sync_anchor_history_integrity"] =
        unifiedRasterAnchorIntegrityReady;
    readinessGates[
        "native_active_scanlines_validate_model_phase"] =
        nativeRasterScanLineModelValidationReady;
    readinessGates[
        "configured_scanout_period_matches_physical_signal"] =
        configuredScanoutPeriodMatchesSignal;
    readinessGates[
        "configured_scanout_period_matches_all_display_epochs"] =
        metrics.configuredScanoutPeriodMismatchRows == 0;
    readinessGates[
        "precise_scanout_period_matches_physical_signal"] =
        counterfactualPeriodMatchesSignal;
    readinessGates["native_outcome_relationships_valid"] =
        metrics.nativeOutcomeRelationshipMismatchRows == 0 &&
        metrics.nativeRenderAdapterLuidRelationshipMismatchRows == 0 &&
        metrics.nativeRenderAdapterIdentityMismatchRows == 0 &&
        metrics.nativeRenderAdapterLuidSnapshotMismatchRows == 0 &&
        metrics.nativeVblankVirtualizationRelationshipMismatchRows == 0;
    readinessGates["raw_qpc_translation_reproducible"] =
        metrics.rawSyncQpcValidRows >= metrics.latchValidRows &&
        metrics.frameStatsSuccessWithoutRawSyncQpcRows == 0 &&
        metrics.rawSyncQpcWithoutTranslatedRows == 0 &&
        metrics.rawSyncQpcFrequencyMismatchRows == 0 &&
        rawSyncQpcTranslationAccountingReady &&
        metrics.rawSyncQpcTranslationComparisons != 0 &&
        metrics.rawSyncQpcTranslationMismatchRows == 0;
    readinessGates["absolute_qpc_clock_correlation_reproducible"] =
        qpcCorrelationIntegrityReady;
    readinessGates["native_outcome_and_raw_qpc_integrity"] =
        nativeOutcomeAndQpcIntegrityReady;
    readinessGates["reference_controller_decision_state_exact"] =
        referenceDecisionStateExact;
    readinessGates["reference_controller_diagnostics_exact"] =
        referenceControllerDiagnosticsExact;
    readinessGates["external_rebase_cause_telemetry_available"] =
        metrics.externalRebaseTelemetryAvailable &&
        metrics.windowStateCauseTelemetryAvailable;
    readinessGates["external_rebase_cause_relationships_valid"] =
        metrics.externalRebaseFlagRelationshipMismatchRows == 0 &&
        metrics.externalRebaseFlagCarryForwardMismatchRows == 0 &&
        metrics.unknownWindowStateFlagRows == 0 &&
        metrics.midframeDisplayEpochRelationshipMismatchRows == 0;
    readinessGates["execution_residuals_valid"] =
        metrics.invalidExecutionResiduals == 0;
    readinessGates[
        "requested_pre_present_raster_probe_overhead_removal_complete"] =
        rasterProbeOverheadRemovalEvidenceComplete;
    readinessGates["controller_feedback_lifecycle_reconstructable"] =
        metrics.invalidControllerLifecycleRows == 0;
    readinessGates["deep_trace_all_rows"] =
        metrics.delivered != 0 &&
        metrics.deepTraceRows == metrics.delivered;
    readinessGates[
        "deep_before_state_exact_carry_forward_at_least_99_percent"] =
        deepBeforeCarryForwardReady;
    readinessGates["submission_id_all_presented_frames"] =
        presentedFrames != 0 &&
        metrics.presentedSubmissionIdValidRows == presentedFrames;
    readinessGates["native_present_start_all_presented_frames"] =
        presentedFrames != 0 &&
        metrics.presentedNativePresentTimingValidRows == presentedFrames;
    readinessGates["present_operation_timing_all_presented_frames"] =
        presentedFrames != 0 &&
        metrics.presentedPresentOperationIntegrityRows == presentedFrames;
    readinessGates["presenter_submission_timing_fields_available"] =
        metrics.presenterSubmissionTimingTelemetryAvailable;
    readinessGates[
        "presenter_native_submission_timestamp_all_presented_frames"] =
        presentedFrames != 0 &&
        metrics.presenterSubmissionTimestampUsedRows == presentedFrames;
    readinessGates[
        "presenter_submission_boundary_derivation_exact"] =
        metrics.presenterSubmissionTimingTelemetryAvailable &&
        metrics.presenterSubmissionTimingRelationshipMismatchRows == 0;
    readinessGates["spacing_correction_fields_available"] =
        metrics.spacingCorrectionTelemetryAvailable;
    readinessGates["spacing_correction_relationships_valid"] =
        metrics.spacingCorrectionTelemetryAvailable &&
        metrics.spacingCorrectionRelationshipMismatchRows == 0;
    readinessGates[
        "spacing_lifecycle_timing_fields_available"] =
        metrics.spacingLifecycleTimingTelemetryAvailable;
    readinessGates[
        "spacing_lifecycle_timing_exact_for_all_present_attempts"] =
        metrics.spacingLifecycleTimingTelemetryAvailable &&
        metrics.spacingLifecycleTimingValidatedRows ==
            metrics.normalPresentAttemptRows &&
        metrics.spacingLifecycleTimingRelationshipMismatchRows == 0;
    readinessGates["waiter_lifecycle_fields_available"] =
        metrics.waitLifecycleTelemetryAvailable;
    readinessGates[
        "waiter_coarse_wake_and_active_margin_lifecycle_exact"] =
        waitLifecycleCoverageReady;
    readinessGates["completion_queue_depth_field_available"] =
        metrics.completionQueueDepthTelemetryAvailable;
    readinessGates["completion_queue_depth_in_range"] =
        metrics.completionQueueDepthTelemetryAvailable &&
        metrics.completionQueueDepthOutOfRangeRows == 0;
    readinessGates["present_and_native_call_timing_causal_and_exact"] =
        metrics.presentOperationOrderViolations == 0 &&
        metrics.presentOperationDurationMismatchRows == 0 &&
        metrics.nativePresentOrderViolations == 0 &&
        metrics.nativePresentDurationMismatchRows == 0 &&
        metrics.submissionTimestampRegressions == 0;
    readinessGates["gpu_ready_timing_all_presented_frames"] =
        presentedFrames != 0 &&
        metrics.presentedGpuReadyTimingValidRows == presentedFrames;
    readinessGates["gpu_ready_native_results_fields_available"] =
        metrics.gpuReadyNativeResultTelemetryAvailable;
    readinessGates["gpu_ready_native_results_all_presented_frames"] =
        presentedFrames != 0 &&
        metrics.presentedGpuReadyNativeSuccessRows == presentedFrames;
    readinessGates["gpu_ready_native_result_relationships_valid"] =
        metrics.gpuReadyNativeResultTelemetryAvailable &&
        metrics.gpuReadyNativeResultRelationshipMismatchRows == 0;
    readinessGates["gpu_ready_wait_timing_causal_and_exact"] =
        metrics.gpuReadyOrderViolations == 0 &&
        metrics.gpuReadyDurationMismatchRows == 0;
    readinessGates["gpu_ready_stage_timing_fields_available"] =
        metrics.gpuReadyStageTimingTelemetryAvailable;
    readinessGates[
        "gpu_ready_signal_flush_set_event_timing_causal"] =
        metrics.gpuReadyStageTimingTelemetryAvailable &&
        metrics.gpuReadyStageTimingRelationshipMismatchRows == 0;
    readinessGates[
        "gpu_ready_completion_bounds_fields_available"] =
        metrics.gpuReadyBoundsTelemetryAvailable;
    readinessGates[
        "gpu_ready_completion_bounds_all_presented_frames"] =
        presentedFrames != 0 &&
        metrics.presentedGpuReadyBoundsValidRows == presentedFrames;
    readinessGates[
        "gpu_ready_completion_bounds_causal_and_reproducible"] =
        metrics.gpuReadyBoundsTelemetryAvailable &&
        metrics.gpuReadyFenceRelationshipMismatchRows == 0 &&
        metrics.gpuReadyBoundsDerivationMismatchRows == 0;
    readinessGates[
        "gpu_ready_fence_target_and_completed_value_relationship"] =
        metrics.gpuReadyBoundsTelemetryAvailable &&
        metrics.gpuReadyFenceRelationshipMismatchRows == 0;
    readinessGates[
        "post_present_query_timing_fields_available"] =
        metrics.postPresentQueryTimingTelemetryAvailable;
    readinessGates[
        "post_present_query_timing_all_presented_frames"] =
        metrics.postPresentQueryTimingTelemetryAvailable &&
        presentedFrames != 0 &&
        metrics.presentedPostPresentQueryTimingValidRows ==
            presentedFrames &&
        metrics.postPresentQueryTimingRelationshipMismatchRows == 0;
    readinessGates["native_present_boundary_identity"] =
        presentedFrames != 0 &&
        metrics.nativePresentBoundaryComparisons == presentedFrames &&
        metrics.nativePresentBoundaryMismatches == 0;
    readinessGates["display_counter_sequences_continuous"] =
        metrics.submissionSequenceResets == 0 &&
        metrics.submissionSequenceDuplicates == 0 &&
        metrics.latchSequenceResets == 0;
    readinessGates["pre_present_sync_anchor_integrity"] =
        syncAnchorIntegrityReady;
    readinessGates["post_present_sync_anchor_integrity"] =
        postSyncAnchorIntegrityReady;
    readinessGates["raster_outcome_accounting"] =
        rasterOutcomeAccountingReady;
    readinessGates["candidate_raster_outcome_accounting"] =
        simulatedRasterOutcomeAccountingReady;
    readinessGates[
        "display_transition_injection_outcome_accounting"] =
        displayTransitionInjectionOutcomeAccountingReady;
    readinessGates[
        "display_transition_injection_exact_refresh_accounting"] =
        displayTransitionInjectionExactOutcomeAccountingReady;
    readinessGates["render_wake_injection_accounting"] =
        renderWakeInjectionAccountingReady;
    readinessGates["render_wake_effect_accounting"] =
        renderWakeInjectionEffectAccountingReady;
    readinessGates["target_wake_injection_accounting"] =
        targetWakeInjectionAccountingReady;
    readinessGates["target_wake_effect_accounting"] =
        targetWakeInjectionEffectAccountingReady;
    readinessGates["display_model_parameters_unclamped"] =
        metrics.observedRasterEnvelope.activeScanoutClamped == 0 &&
        metrics.observedRasterEnvelope.scanoutPhaseWindowInvalid == 0 &&
        metrics.simulatedRasterEnvelope.activeScanoutClamped == 0 &&
        metrics.simulatedRasterEnvelope.scanoutPhaseWindowInvalid == 0;
    readinessGates[
        "raw_pre_present_sync_anchor_all_presented_coverage_at_least_99_percent"] =
        presentedRawAnchorCoverageReady;
    readinessGates[
        "raw_pre_present_sync_anchor_coverage_at_least_99_percent"] =
        rawAdaptiveAnchorCoverageReady;
    readinessGates["model_sync_anchor_coverage_at_least_99_percent"] =
        adaptiveAnchorCoverageReady;
    readinessGates[
        "candidate_model_sync_anchor_coverage_at_least_99_percent"] =
        simulatedAdaptiveAnchorCoverageReady;
    readinessGates["raster_classification_coverage_at_least_99_percent"] =
        rasterClassificationCoverageReady;
    readinessGates[
        "candidate_raster_classification_coverage_at_least_99_percent"] =
        simulatedRasterClassificationCoverageReady;
    readinessGates[
        "counterfactual_free_running_row_accounting"] =
        counterfactualFreeRunningAccountingReady;
    readinessGates[
        "counterfactual_free_running_precise_period_calibrated"] =
        resolvedCounterfactualPeriodPs != 0 &&
        counterfactualPeriodMatchesSignal;
    readinessGates[
        "counterfactual_free_running_phase_coverage_at_least_99_percent"] =
        counterfactualFreeRunningCoverageReady;
    readinessGates[
        "display_phase_uncertainty_covers_clock_correlation_and_timestamp_quantization"] =
        capturedClockUncertaintyCovered;
    readinessGates[
        "counterfactual_free_running_all_presented_rows_adaptive"] =
        metrics.simulatedLatchedFrames == 0 &&
        metrics.counterfactualFreeRunningAdaptiveRows == presentedFrames;
    readinessGates[
        "counterfactual_free_running_clock_continuous"] =
        metrics.counterfactualFreeRunningTimeRegressions == 0 &&
        metrics.counterfactualFreeRunningPeriodChanges == 0;
    readinessGates[
        "counterfactual_free_running_picosecond_conversion_exact"] =
        metrics.counterfactualFreeRunningConversionFailures == 0;
    readinessGates["counterfactual_refresh_mode_fixed"] =
        scenario.mode == "fixed";
    readinessGates[
        "fresh_post_present_latch_coverage_at_least_99_percent"] =
        freshLatchCoverageReady;
    readinessGates[
        "fresh_latch_submission_association_at_least_99_percent"] =
        freshLatchAssociationReady;
    readinessGates[
        "equality_anchored_refresh_correlation_coverage_at_least_99_percent"] =
        equalityAnchorCorrelationCoverageReady;
    readinessGates["equality_anchored_capture_samples_at_least_100"] =
        metrics.exactPresentRefreshCorrelations >=
            kMinimumExactRasterValidationSamples;
    readinessGates["equality_anchored_classification_accounting"] =
        countTotal(metrics.observedExactRefreshClassifications) ==
            metrics.exactPresentRefreshCorrelations;
    readinessGates["equality_anchored_validation_samples_at_least_100"] =
        exactRasterValidationRows >= kMinimumExactRasterValidationSamples;
    readinessGates["equality_anchored_envelope_validation_accounting"] =
        exactRasterValidationRows ==
            metrics.exactPresentRefreshCorrelations;
    readinessGates["equality_anchored_envelope_contradictions_zero"] =
        metrics.observedRasterValidationContradictions == 0;

    QJsonObject readiness;
    readiness["controller_replay_ready"] = controllerReplayReady;
    readiness["diagnostic_capture_ready"] = diagnosticCaptureReady;
    readiness["raster_simulation_ready"] = rasterSimulationReady;
    readiness["optical_tear_confirmation_available"] = false;
    readiness["counterfactual_refresh_timeline_available"] =
        counterfactualRefreshTimelineReady;
    readiness["minimum_captured_phase_uncertainty_us"] =
        static_cast<qint64>(minimumCapturedPhaseUncertaintyUs);
    readiness["gates"] = readinessGates;
    readiness["scope"] = metrics.traceFooter.present ?
        "clean-close footer accounts for the full allocated arrival sequence" :
        "legacy capture has no clean-close footer, so full-session completeness is unverified";
    readiness["optical_scope"] =
        "software timing can bound active-scanout exposure but cannot observe the panel output or image-content discontinuity";
    readiness["display_calibration_scope"] =
        "calibration_confirmed is an operator assertion covering the SyncQPCTime-to-first-active-line offset, Present-to-display transport, and phase uncertainty; replay cannot measure panel transport or output. Optional microsecond period and active-duration overrides must agree with the captured physical refresh rational and active/total pixel geometry; zero means derive them from that signal. The tear-exposure interval ends at the final active pixel rather than including the last line's trailing horizontal blank. Raster classification and counterfactual propagation use an explicit scanout_period_ps override or the exact captured physical-signal rational, and reject disagreement beyond one picosecond. D3DKMT validation separately uses the complete vertically-active line interval, compares every definite active prediction with the recorded scan-line index, and reports its phase-derived error and tolerance for later calibration sweeps. Raster readiness requires at least 100 such active scan-line comparisons and requires phase_uncertainty_us to cover at least the captured QPC-correlation half-span plus one microsecond of timestamp quantization";
    readiness["gpu_ready_scope"] =
        "D3D11 records separate Signal, Flush, SetEventOnCompletion, completed-value poll, and event-wait brackets inside preparation. Exact native results and target/completed fence values validate stage progression, the producer's completed-before-wait bit, and reject device removal or impossible lag. The trace proves completion occurred within the derived interval and before Present; it does not fabricate an exact GPU completion timestamp";
    readiness["post_present_query_scope"] =
        "deep traces bracket GetLastPresentCount and GetFrameStatistics after native Present and any observation-only after-Present raster query. Replay requires their exact order before presenter return; ordinary traces retain result codes but deliberately leave the optional timing brackets zero";
    readiness["spacing_scope"] =
        "the immediate spacing recheck is distinct from the first target/guard-floor check. Replay derives both deficits from prior submission, display period, controller guard state, and the captured clock readings, then verifies any post-feedback guarded floor and correction wait";
    readiness["observer_overhead_scope"] =
        "alignment tracing adds a measured D3DKMTGetScanLine query immediately before Present. The opt-in execution.remove_pre_present_raster_probe_overhead transform removes only that successful query's exact bracketed duration, is clamped to the reconstructed submission floor, and fails raster readiness if any requested presented row lacks the required evidence";
    readiness["counter_epoch_scope"] =
        "external_rebase_applied starts a new display-counter and raster-anchor epoch; resets inside an epoch fail readiness";
    readiness["counterfactual_refresh_scope"] =
        "availability covers the calibrated picosecond-period fixed/free-running display hypothesis for an all-adaptive fixed-lifecycle candidate and requires observation-only D3DKMT scan-position samples around every DXGI Present, in-range scan lines, and at least 100 definite active/vblank hypothesis comparisons with zero cases where both hypotheses contradict the observation. DRR/vblank virtualization, non-progressive signals, rotated scanout, and non-identity output scaling fail readiness; latched Presents, driver queuing, LFC, and panel output remain outside the model";

    QJsonObject summary;
    summary["capture"] = capture;
    summary["observed"] = observed;
    summary["simulation"] = simulation;
    summary["fidelity"] = fidelity;
    summary["diagnostic_readiness"] = readiness;
    summary["runtime_ms"] = elapsedMs;
    summary["latency_scope"] =
        "client decode completion through native submission; host capture/encode/network timestamps are not present in this trace schema";
    summary["tear_semantics"] =
        "interval violations and raster exposure bounds are deterministic software models, not literal optical tear observations";
    summary["raster_model_scope"] =
        "schema-5 integrity-checked DXGI SyncQPCTime/SyncRefreshCount history, stable QueryDisplayConfig physical-signal timing, and observation-only D3DKMT scan-position brackets anchor and audit modeled transitions across ideal VRR flip-following versus free-running fixed-refresh raster states; PresentRefreshCount is never treated as its timestamp";

    // Sender-spacing cadence headline numbers for sweep tables.
    {
        const auto addSenderScalars = [&summary](
            const QString& prefix, const SenderCadenceTracker& tracker,
            uint64_t durationUs) {
            summary[prefix + "sender_pairs"] =
                static_cast<qint64>(tracker.pairs);
            summary[prefix + "sender_hitches"] =
                static_cast<qint64>(tracker.hitches);
            summary[prefix + "sender_hitches_per_second"] = durationUs != 0 ?
                static_cast<double>(tracker.hitches) * 1000000.0 /
                    static_cast<double>(durationUs) : 0.0;
            summary[prefix + "sender_hitch_late_arrivals"] =
                static_cast<qint64>(tracker.hitchLateArrivals);
            summary[prefix + "sender_spacing_error_p50_us"] =
                static_cast<qint64>(
                    tracker.absoluteSpacingErrorUs.percentile(50));
            summary[prefix + "sender_spacing_error_p90_us"] =
                static_cast<qint64>(
                    tracker.absoluteSpacingErrorUs.percentile(90));
            summary[prefix + "sender_spacing_error_p99_us"] =
                static_cast<qint64>(
                    tracker.absoluteSpacingErrorUs.percentile(99));
            summary[prefix + "sender_jerk_p99_us"] =
                static_cast<qint64>(tracker.absoluteJerkUs.percentile(99));
            summary[prefix + "presented_jerk_p50_us"] =
                static_cast<qint64>(tracker.presentedJerkUs.percentile(50));
            summary[prefix + "presented_jerk_p90_us"] =
                static_cast<qint64>(tracker.presentedJerkUs.percentile(90));
            summary[prefix + "presented_jerk_p99_us"] =
                static_cast<qint64>(tracker.presentedJerkUs.percentile(99));
            summary[prefix + "presented_jerk_over_2ms_per_mille"] =
                tracker.presentedJerkPairs != 0 ?
                    static_cast<qint64>(tracker.presentedJerkOverHitch * 1000 /
                                        tracker.presentedJerkPairs) : 0;
        };
        addSenderScalars("replay_", metrics.simulatedSenderCadence,
                         captureDurationUs);
        addSenderScalars("original_", metrics.observedSenderCadence,
                         captureDurationUs);
        addSenderScalars("stock_", metrics.stockSenderCadence,
                         captureDurationUs);
        summary["replay_decode_to_submission_p50_us"] =
            static_cast<qint64>(
                metrics.simulatedDecodeToSubmission.percentile(50));
        summary["replay_worker_saturated"] =
            metrics.occupancyDecisionShiftUs.count != 0 &&
            metrics.occupancyDecisionShiftUs.percentile(50) >
                periodForRate(simulatedStreamFps > 0 ? simulatedStreamFps : 1);
        summary["replay_playout_delay_p50_us"] = static_cast<qint64>(
            metrics.simulatedPlayoutDelayUs.percentile(50));
        summary["replay_playout_delay_p90_us"] = static_cast<qint64>(
            metrics.simulatedPlayoutDelayUs.percentile(90));
        summary["replay_playout_delay_max_us"] = static_cast<qint64>(
            metrics.simulatedPlayoutDelayUs.maximum);
        summary["original_decode_to_submission_p50_us"] =
            static_cast<qint64>(
                metrics.observedDecodeToSubmission.percentile(50));
    }

    // Stable top-level fields retain compatibility with existing launcher
    // summaries and comparison files.
    summary["display_hz"] = simulatedDisplayHz;
    summary["stream_fps"] = simulatedStreamFps;
    summary["delivered_frames"] = static_cast<qint64>(metrics.delivered);
    summary["replayed_frames"] = static_cast<qint64>(metrics.scheduled);
    summary["original_drops"] = static_cast<qint64>(metrics.originalDrops);
    summary["original_tear_risks"] = static_cast<qint64>(
        metrics.originalTearRisks);
    summary["original_scanout_anomalies"] = static_cast<qint64>(
        metrics.scanoutAnomalies);
    summary["original_raster_exposure_lower_bound"] =
        static_cast<qint64>(metrics.observedRasterEnvelope.certainActive);
    summary["original_raster_exposure_upper_bound"] =
        static_cast<qint64>(metrics.observedRasterEnvelope.certainActive +
            metrics.observedRasterEnvelope.possibleActive +
            metrics.observedRasterEnvelope.unclassified);
    summary["replay_raster_exposure_lower_bound"] =
        static_cast<qint64>(metrics.simulatedRasterEnvelope.certainActive);
    summary["replay_raster_exposure_upper_bound"] =
        static_cast<qint64>(metrics.simulatedRasterEnvelope.certainActive +
            metrics.simulatedRasterEnvelope.possibleActive +
            metrics.simulatedRasterEnvelope.unclassified);
    summary["original_raster_unclassified"] = static_cast<qint64>(
        metrics.observedRasterEnvelope.unclassified);
    summary["replay_raster_unclassified"] = static_cast<qint64>(
        metrics.simulatedRasterEnvelope.unclassified);
    summary["original_equality_anchored_active_exposures"] =
        static_cast<qint64>(
            metrics.observedExactRefreshClassifications.value("active"));
    summary["original_equality_anchored_possible_exposures"] =
        static_cast<qint64>(
            metrics.observedExactRefreshClassifications.value("active") +
            metrics.observedExactRefreshClassifications.value(
                "boundary_uncertain") +
            metrics.observedExactRefreshClassifications.value(
                "unclassified"));
    summary["original_equality_anchored_unclassified"] =
        static_cast<qint64>(
            metrics.observedExactRefreshClassifications.value(
                "unclassified"));
    if (simulatedDisplayHz == capturedDisplayHz) {
        summary["replay_after_recorded_exact_refresh"] =
            static_cast<qint64>(metrics.simulatedAfterRecordedRefresh);
        summary["replay_equality_anchored_active_exposures"] =
            static_cast<qint64>(
                metrics.simulatedExactRefreshClassifications.value("active"));
        summary["replay_equality_anchored_possible_exposures"] =
            static_cast<qint64>(
                metrics.simulatedExactRefreshClassifications.value("active") +
                metrics.simulatedExactRefreshClassifications.value(
                    "boundary_uncertain") +
                metrics.simulatedExactRefreshClassifications.value(
                    "unclassified"));
        summary["replay_equality_anchored_unclassified"] =
            static_cast<qint64>(
                metrics.simulatedExactRefreshClassifications.value(
                    "unclassified"));
    }
    else {
        summary["replay_after_recorded_exact_refresh"] = QJsonValue();
        summary["replay_equality_anchored_active_exposures"] =
            QJsonValue();
        summary["replay_equality_anchored_possible_exposures"] =
            QJsonValue();
        summary["replay_equality_anchored_unclassified"] =
            QJsonValue();
    }
    summary["original_repeated_refreshes"] = static_cast<qint64>(
        metrics.repeatedRefreshes);
    summary["replay_tear_risks"] = static_cast<qint64>(
        metrics.simulatedTearRisks);
    summary["replay_latched_frames"] = static_cast<qint64>(
        metrics.simulatedLatchedFrames);
    if (counterfactualRefreshTimelineReady) {
        summary["replay_free_running_scanout_anomaly_lower"] =
            static_cast<qint64>(
                metrics.counterfactualFreeRunningScanoutAnomalyLower);
        summary["replay_free_running_scanout_anomalies"] =
            static_cast<qint64>(
                metrics.counterfactualFreeRunningScanoutAnomalies);
        summary["replay_free_running_scanout_anomaly_upper"] =
            static_cast<qint64>(
                metrics.counterfactualFreeRunningScanoutAnomalyUpper);
        summary["replay_free_running_repeated_refresh_lower"] =
            static_cast<qint64>(
                metrics.counterfactualFreeRunningRepeatedRefreshLower);
        summary["replay_free_running_repeated_refreshes"] =
            static_cast<qint64>(
                metrics.counterfactualFreeRunningRepeatedRefreshes);
        summary["replay_free_running_repeated_refresh_upper"] =
            static_cast<qint64>(
                metrics.counterfactualFreeRunningRepeatedRefreshUpper);
    }
    else {
        summary["replay_free_running_scanout_anomaly_lower"] =
            QJsonValue();
        summary["replay_free_running_scanout_anomalies"] =
            QJsonValue();
        summary["replay_free_running_scanout_anomaly_upper"] =
            QJsonValue();
        summary["replay_free_running_repeated_refresh_lower"] =
            QJsonValue();
        summary["replay_free_running_repeated_refreshes"] =
            QJsonValue();
        summary["replay_free_running_repeated_refresh_upper"] =
            QJsonValue();
    }
    summary["original_abs_submit_error_p95_us"] = observed.value(
        "absolute_submit_error_us").toObject().value("p95");
    summary["replay_abs_submit_error_p95_us"] = simulation.value(
        "absolute_submit_error_us").toObject().value("p95");
    summary["replay_target_drift_p95_us"] = simulation.value(
        "target_drift_us").toObject().value("p95");
    summary["replay_target_drift_p99_us"] = simulation.value(
        "target_drift_us").toObject().value("p99");
    summary["replay_cadence_error_p95_us"] = simulation.value(
        "cadence_error_us").toObject().value("p95");
    summary["original_decode_to_submission_p95_us"] = observedLatency.value(
        "decode_to_submission").toObject().value("p95");
    summary["replay_decode_to_submission_p95_us"] = simulatedLatency.value(
        "decode_to_submission").toObject().value("p95");
    summary["replay_arrival_to_submission_p95_us"] = simulatedLatency.value(
        "pacer_arrival_to_submission").toObject().value("p95");
    summary["original_decode_to_submission_stddev_us"] =
        observedLatency.value("decode_to_submission").toObject().value(
            "stddev");
    summary["replay_decode_to_submission_stddev_us"] =
        simulatedLatency.value("decode_to_submission").toObject().value(
            "stddev");
    const QJsonObject cadenceBands = simulation.value(
        "gap_aware_cadence_by_rounded_source_rate_fps").toObject();
    const QJsonObject fullRangeBand = cadenceBands.value("40_116").toObject();
    summary["replay_40_116_cadence_residual_p95_us"] = fullRangeBand.value(
        "absolute_cadence_residual_us").toObject().value("p95");
    summary["replay_40_116_cadence_residual_p99_us"] = fullRangeBand.value(
        "absolute_cadence_residual_us").toObject().value("p99");
    summary["replay_40_116_jerk_p95_us"] = fullRangeBand.value(
        "absolute_jerk_us").toObject().value("p95");
    summary["replay_40_116_jerk_p99_us"] = fullRangeBand.value(
        "absolute_jerk_us").toObject().value("p99");
    const QJsonObject focusBand = cadenceBands.value("60_100").toObject();
    summary["replay_60_100_cadence_residual_p95_us"] = focusBand.value(
        "absolute_cadence_residual_us").toObject().value("p95");
    summary["replay_60_100_cadence_residual_p99_us"] = focusBand.value(
        "absolute_cadence_residual_us").toObject().value("p99");
    summary["replay_60_100_jerk_p95_us"] = focusBand.value(
        "absolute_jerk_us").toObject().value("p95");
    summary["replay_60_100_jerk_p99_us"] = focusBand.value(
        "absolute_jerk_us").toObject().value("p99");
    summary["paired_submission_delta_stddev_us"] = pairedSubmissionDelta.value(
        "signed_us").toObject().value("stddev");
    summary["paired_abs_submission_delta_p99_us"] = pairedSubmissionDelta.value(
        "absolute_us").toObject().value("p99");
    summary["controller_replay_ready"] = controllerReplayReady;
    summary["diagnostic_capture_ready"] = diagnosticCaptureReady;
    summary["raster_simulation_ready"] = rasterSimulationReady;
    summary["model"] = kReplayModel;
    summary["decision_time_model"] = "worker-occupancy-v1";
    return summary;
}

struct TimelineDetails {
    uint64_t decoderOutputUs = 0;
    int recordedSourceRateHz = 0;
    int simulatedSourceRateHz = 0;
    uint64_t recordedSourcePeriodUs = 0;
    uint64_t simulatedSourcePeriodUs = 0;
    int64_t simulatedReadyOffsetUs = 0;
    uint64_t simulatedRenderLeadUs = 0;
    int64_t simulatedReadinessBudgetUs = 0;
    uint64_t simulatedSourceTimeUs = 0;
    uint64_t simulatedPlayoutDelayUs = 0;
    int64_t simulatedCadenceSmoothingUs = 0;
    bool simulatedCadenceEligible = false;
    bool simulatedSourceRateChanged = false;
    bool simulatedPhaseDiscontinuity = false;
    CadenceSample recordedCadence;
    CadenceSample simulatedCadence;
    int64_t recordedSpacingMarginUs = 0;
    int64_t simulatedSpacingMarginUs = 0;
    uint64_t recordedSpacingDeficitUs = 0;
    uint64_t recordedSpacingGuardFeedbackUs = 0;
    bool recordedSpacingCorrected = false;
    uint64_t recordedSpacingCheckUs = 0;
    uint64_t recordedPresentationFloorUs = 0;
    uint64_t recordedSpacingRecheckUs = 0;
    uint64_t recordedSpacingCorrectedFloorUs = 0;
    uint64_t recordedCorrectionWaitStartUs = 0;
    uint64_t recordedCorrectionWaitEndUs = 0;
    uint64_t guardUs = 0;
    uint64_t headroomUs = 0;
    int64_t readinessBudgetUs = 0;
    uint64_t readinessReserveUs = 0;
    uint64_t queueDepthBefore = 0;
    uint64_t queueDepthAfter = 0;
    uint64_t completionQueueDepth = 0;
    bool queueDiscontinuity = false;
    bool recordedLatched = false;
    bool simulatedLatched = false;
    bool latchValid = false;
    uint64_t latchSubmissionId = 0;
    uint64_t latchTimeUs = 0;
    bool latchRawSyncQpcValid = false;
    uint64_t latchRawSyncQpcTicks = 0;
    uint64_t latchRawSyncQpcFrequency = 0;
    bool latchQpcCorrelationValid = false;
    uint64_t latchQpcCorrelationReferenceTicks = 0;
    uint64_t latchQpcCorrelationReferenceTimeUs = 0;
    uint64_t latchQpcCorrelationSpanTicks = 0;
    uint64_t latchPresentRefreshSequence = 0;
    uint64_t latchSyncRefreshSequence = 0;
    bool prePresentSyncSampleValid = false;
    bool prePresentSyncAnchorIntegrityValid = false;
    uint64_t prePresentSyncSampleUs = 0;
    uint64_t prePresentSyncRefreshSequence = 0;
    uint64_t recordedDecisionUs = 0;
    uint64_t simulatedDecisionUs = 0;
    bool recordedExternalRebaseApplied = false;
    uint64_t recordedExternalRebaseFlags = 0;
    uint64_t recordedMidframeWindowStateFlags = 0;
    uint64_t recordedRenderWaitOvershootUs = 0;
    uint64_t recordedRenderSchedulerDelayUs = 0;
    bool recordedRenderSchedulerDelayValid = false;
    bool recordedRenderDeadlineAlreadyElapsed = false;
    uint64_t recordedTargetWaitOvershootUs = 0;
    uint64_t recordedTargetSchedulerDelayUs = 0;
    bool recordedTargetSchedulerDelayValid = false;
    bool recordedTargetDeadlineAlreadyElapsed = false;
    bool recordedPresenterSubmissionTimeValid = false;
    uint64_t recordedPresenterSubmissionTimeUs = 0;
    bool recordedPresenterSubmissionTimeUsed = false;
    bool recordedNativePresentTimingValid = false;
    uint64_t recordedNativePresentStartUs = 0;
    int64_t recordedNativePresentBoundaryDeltaUs = 0;
    bool recordedGpuReadyAttempted = false;
    bool recordedGpuReadySignalResultValid = false;
    int64_t recordedGpuReadySignalResult = 0;
    bool recordedGpuReadySetEventResultValid = false;
    int64_t recordedGpuReadySetEventResult = 0;
    bool recordedGpuReadyWaitResultValid = false;
    uint64_t recordedGpuReadyWaitResult = 0;
    bool recordedGpuReadyTimingValid = false;
    uint64_t recordedGpuReadySignalStartUs = 0;
    uint64_t recordedGpuReadySignalEndUs = 0;
    uint64_t recordedGpuReadyFlushStartUs = 0;
    uint64_t recordedGpuReadyFlushEndUs = 0;
    uint64_t recordedGpuReadySetEventStartUs = 0;
    uint64_t recordedGpuReadySetEventEndUs = 0;
    uint64_t recordedGpuReadyPollStartUs = 0;
    uint64_t recordedGpuReadyPollEndUs = 0;
    uint64_t recordedGpuReadyFenceValue = 0;
    uint64_t recordedGpuReadyPollCompletedValue = 0;
    bool recordedGpuReadyCompletedBeforeWait = false;
    bool recordedGpuReadyCompletionBoundsValid = false;
    uint64_t recordedGpuReadyCompletionLowerBoundUs = 0;
    uint64_t recordedGpuReadyCompletionUpperBoundUs = 0;
    uint64_t recordedGpuReadyCompletionUncertaintyUs = 0;
    uint64_t recordedGpuReadyWaitStartUs = 0;
    uint64_t recordedGpuReadyWaitReturnUs = 0;
    uint64_t recordedGpuReadyWaitUs = 0;
    bool recordedNativeBackendValid = false;
    uint64_t recordedNativeBackend = 0;
    bool recordedNativePresentResultValid = false;
    int64_t recordedNativePresentResult = 0;
    bool recordedNativePresentParametersValid = false;
    uint64_t recordedNativePresentSyncInterval = 0;
    uint64_t recordedNativePresentFlags = 0;
    bool recordedNativeVrrStateValid = false;
    bool recordedNativeTearingSupported = false;
    bool recordedNativeBorderlessFlipModel = false;
    bool recordedNativeSameGpuOutput = false;
    bool recordedNativeRenderAdapterLuidValid = false;
    uint64_t recordedNativeRenderAdapterLuid = 0;
    bool recordedNativeSwapChainAllowsTearing = false;
    bool recordedNativePresentReadyAvailable = false;
    bool recordedNativeForegroundWindow = false;
    uint64_t recordedNativeVrrFallbackReason = 0;
    uint64_t recordedNativeDesktopMonitorCount = 0;
    bool recordedNativeVblankVirtualizationProbeComplete = false;
    bool recordedNativeVblankVirtualizationCallAvailable = false;
    bool recordedNativeVblankVirtualizationResultValid = false;
    int64_t recordedNativeVblankVirtualizationResult = 0;
    bool recordedNativeVblankVirtualizationDisabled = false;
    bool recordedSubmissionIdQueryResultValid = false;
    int64_t recordedSubmissionIdQueryResult = 0;
    uint64_t recordedSubmissionIdQueryStartUs = 0;
    uint64_t recordedSubmissionIdQueryEndUs = 0;
    bool recordedFrameStatsQueryResultValid = false;
    int64_t recordedFrameStatsQueryResult = 0;
    uint64_t recordedFrameStatsQueryStartUs = 0;
    uint64_t recordedFrameStatsQueryEndUs = 0;
    uint64_t injectedDecisionDelayUs = 0;
    uint64_t requestedRenderWakeDelayUs = 0;
    bool renderWakeDelayEligible = false;
    bool renderWaitPathCompared = false;
    bool renderWaitPathMatchesCaptured = false;
    bool renderWaitRecordedFinalResidualUsed = false;
    uint64_t injectedRenderWakeDelayUs = 0;
    uint64_t absorbedRenderWakeDelayUs = 0;
    uint64_t renderWakeExecutionDelayUs = 0;
    uint64_t requestedTargetWakeDelayUs = 0;
    bool targetWakeDelayEligible = false;
    bool targetWaitPathCompared = false;
    bool targetWaitPathMatchesCaptured = false;
    bool targetWaitRecordedFinalResidualUsed = false;
    uint64_t injectedTargetWakeDelayUs = 0;
    uint64_t absorbedTargetWakeDelayUs = 0;
    uint64_t targetWakeExecutionDelayUs = 0;
    uint64_t injectedPreparationDelayUs = 0;
    uint64_t injectedSubmissionDelayUs = 0;
    uint64_t injectedDisplayTransitionDelayUs = 0;
    uint64_t injectedSpacingGuardFeedbackUs = 0;
    bool rasterProbeOverheadRemovalRequested = false;
    bool rasterProbeOverheadAvailable = false;
    uint64_t measuredRasterProbeDurationUs = 0;
    uint64_t removedRasterProbeOverheadUs = 0;
    bool rasterProbeOverheadRemovalClamped = false;
    uint64_t injectedSubmissionAdvanceRequestedUs = 0;
    uint64_t injectedSubmissionAdvanceAppliedUs = 0;
    bool simulatedFreeRunningRefreshEligible = false;
    bool simulatedFreeRunningRefreshBaseline = false;
    bool simulatedFreeRunningRefreshComparison = false;
    uint64_t simulatedFreeRunningPropagatedPhasePs = 0;
    bool simulatedFreeRunningPhaseReferenceCompared = false;
    uint64_t simulatedFreeRunningPhaseReferenceDifferencePs = 0;
    uint64_t simulatedFreeRunningRefreshDeltaLower = 0;
    uint64_t simulatedFreeRunningRefreshDelta = 0;
    uint64_t simulatedFreeRunningRefreshDeltaUpper = 0;
    uint64_t simulatedFreeRunningScanoutAnomalyLower = 0;
    uint64_t simulatedFreeRunningScanoutAnomaly = 0;
    uint64_t simulatedFreeRunningScanoutAnomalyUpper = 0;
    uint64_t simulatedFreeRunningRepeatedRefreshLower = 0;
    uint64_t simulatedFreeRunningRepeatedRefresh = 0;
    uint64_t simulatedFreeRunningRepeatedRefreshUpper = 0;
    uint64_t observedPresentSequenceDelta = 0;
    uint64_t observedPresentRefreshDelta = 0;
    uint64_t observedScanoutAnomalyDelta = 0;
    uint64_t observedRepeatedRefreshDelta = 0;
    bool exactPresentRefreshTimestampValid = false;
    int64_t recordedExactRefreshPhaseUs = 0;
    int64_t simulatedExactRefreshPhaseUs = 0;
    int64_t recordedExactActiveScanoutPhaseUs = 0;
    int64_t simulatedExactActiveScanoutPhaseUs = 0;
    QByteArray recordedExactRefreshClass;
    QByteArray simulatedExactRefreshClass;
    VrrRasterPhaseResult recordedRaster;
    VrrRasterPhaseResult simulatedRaster;
};

bool writeTimelineHeader(QFile& file)
{
    static const QByteArray header =
        "arrival_sequence,frame,disposition,decode_complete_us,decoder_output_us,pacer_arrival_us,"
        "recorded_target_us,simulated_target_us,target_delta_us,"
        "recorded_submission_us,simulated_submission_us,submission_delta_us,"
        "recorded_presenter_submission_time_valid,"
        "recorded_presenter_submission_time_us,"
        "recorded_presenter_submission_time_used,"
        "recorded_native_present_timing_valid,"
        "recorded_native_present_start_us,"
        "recorded_native_present_start_minus_submission_boundary_us,"
        "recorded_gpu_ready_attempted,"
        "recorded_gpu_ready_signal_result_valid,"
        "recorded_gpu_ready_signal_result,"
        "recorded_gpu_ready_set_event_result_valid,"
        "recorded_gpu_ready_set_event_result,"
        "recorded_gpu_ready_wait_result_valid,"
        "recorded_gpu_ready_wait_result,"
        "recorded_gpu_ready_timing_valid,"
        "recorded_gpu_ready_signal_start_us,"
        "recorded_gpu_ready_signal_end_us,"
        "recorded_gpu_ready_flush_start_us,"
        "recorded_gpu_ready_flush_end_us,"
        "recorded_gpu_ready_set_event_start_us,"
        "recorded_gpu_ready_set_event_end_us,"
        "recorded_gpu_ready_poll_start_us,"
        "recorded_gpu_ready_poll_end_us,"
        "recorded_gpu_ready_fence_value,"
        "recorded_gpu_ready_poll_completed_value,"
        "recorded_gpu_ready_completed_before_wait,"
        "recorded_gpu_ready_completion_bounds_valid,"
        "recorded_gpu_ready_completion_lower_bound_us,"
        "recorded_gpu_ready_completion_upper_bound_us,"
        "recorded_gpu_ready_completion_uncertainty_us,"
        "recorded_gpu_ready_wait_start_us,"
        "recorded_gpu_ready_wait_return_us,"
        "recorded_gpu_ready_wait_us,"
        "recorded_native_backend_valid,"
        "recorded_native_backend,"
        "recorded_native_present_result_valid,"
        "recorded_native_present_result,"
        "recorded_native_present_parameters_valid,"
        "recorded_native_present_sync_interval,"
        "recorded_native_present_flags,"
        "recorded_native_vrr_state_valid,"
        "recorded_native_tearing_supported,"
        "recorded_native_borderless_flip_model,"
        "recorded_native_same_gpu_output,"
        "recorded_native_render_adapter_luid_valid,"
        "recorded_native_render_adapter_luid,"
        "recorded_native_swap_chain_allows_tearing,"
        "recorded_native_present_ready_available,"
        "recorded_native_foreground_window,"
        "recorded_native_vrr_fallback_reason,"
        "recorded_native_desktop_monitor_count,"
        "recorded_native_vblank_virtualization_probe_complete,"
        "recorded_native_vblank_virtualization_call_available,"
        "recorded_native_vblank_virtualization_result_valid,"
        "recorded_native_vblank_virtualization_result,"
        "recorded_native_vblank_virtualization_disabled,"
        "recorded_submission_id_query_result_valid,"
        "recorded_submission_id_query_result,"
        "recorded_submission_id_query_start_us,"
        "recorded_submission_id_query_end_us,"
        "recorded_frame_statistics_query_result_valid,"
        "recorded_frame_statistics_query_result,"
        "recorded_frame_statistics_query_start_us,"
        "recorded_frame_statistics_query_end_us,"
        "recorded_decode_to_submission_us,simulated_decode_to_submission_us,"
        "recorded_tear_classification,simulated_tear_classification,"
        "recorded_source_rate_hz_rounded,simulated_source_rate_hz_rounded,"
        "recorded_source_period_us,simulated_source_period_us,"
        "simulated_ready_offset_us,simulated_render_lead_us,"
        "simulated_readiness_budget_us,simulated_source_time_us,"
        "simulated_playout_delay_us,simulated_cadence_smoothing_us,"
        "simulated_cadence_eligible,simulated_source_rate_changed,simulated_phase_discontinuity,"
        "recorded_cadence_valid,simulated_cadence_valid,"
        "recorded_source_elapsed_us,simulated_source_elapsed_us,"
        "recorded_submission_elapsed_us,simulated_submission_elapsed_us,"
        "recorded_cadence_residual_us,simulated_cadence_residual_us,"
        "recorded_jerk_valid,simulated_jerk_valid,recorded_jerk_us,simulated_jerk_us,"
        "recorded_phase_us,simulated_phase_us,"
        "recorded_spacing_margin_us,simulated_spacing_margin_us,"
        "recorded_spacing_deficit_us,recorded_spacing_guard_feedback_us,"
        "recorded_spacing_corrected,recorded_spacing_check_us,"
        "recorded_presentation_floor_us,recorded_spacing_recheck_us,"
        "recorded_spacing_corrected_floor_us,"
        "recorded_correction_wait_start_us,"
        "recorded_correction_wait_end_us,"
        "guard_us,headroom_us,readiness_budget_us,readiness_reserve_us,"
        "queue_depth_before,queue_depth_after,completion_queue_depth,"
        "queue_discontinuity,"
        "recorded_latched,simulated_latched,latch_valid,latch_submission_id,"
        "sync_qpc_time_us,latch_raw_sync_qpc_valid,"
        "latch_raw_sync_qpc_ticks,latch_raw_sync_qpc_frequency_hz,"
        "latch_qpc_correlation_valid,"
        "latch_qpc_correlation_reference_ticks,"
        "latch_qpc_correlation_reference_time_us,"
        "latch_qpc_correlation_span_ticks,"
        "latch_present_refresh_seq,latch_sync_refresh_seq,"
        "pre_present_sync_sample_valid,"
        "pre_present_sync_anchor_integrity_valid,pre_present_sync_sample_us,"
        "pre_present_sync_refresh_seq,recorded_decision_us,"
        "simulated_decision_us,recorded_external_rebase_applied,"
        "recorded_external_rebase_flags,"
        "recorded_midframe_window_state_flags,"
        "recorded_render_wait_overshoot_us,"
        "recorded_render_scheduler_delay_us,"
        "recorded_render_scheduler_delay_valid,"
        "recorded_render_deadline_already_elapsed,"
        "recorded_target_wait_overshoot_us,"
        "recorded_target_scheduler_delay_us,"
        "recorded_target_scheduler_delay_valid,"
        "recorded_target_deadline_already_elapsed,"
        "injected_decision_delay_us,"
        "requested_render_wake_delay_us,"
        "render_wake_delay_eligible,"
        "render_wait_path_compared,"
        "render_wait_path_matches_captured,"
        "render_wait_recorded_final_residual_used,"
        "injected_render_wake_delay_us,"
        "absorbed_render_wake_delay_us,"
        "render_wake_execution_delay_us,"
        "requested_target_wake_delay_us,"
        "target_wake_delay_eligible,"
        "target_wait_path_compared,"
        "target_wait_path_matches_captured,"
        "target_wait_recorded_final_residual_used,"
        "injected_target_wake_delay_us,"
        "absorbed_target_wake_delay_us,"
        "target_wake_execution_delay_us,"
        "injected_preparation_delay_us,injected_submission_delay_us,"
        "injected_display_transition_delay_us,"
        "injected_spacing_guard_feedback_us,"
        "pre_present_raster_probe_overhead_removal_requested,"
        "pre_present_raster_probe_overhead_available,"
        "measured_pre_present_raster_probe_duration_us,"
        "removed_pre_present_raster_probe_overhead_us,"
        "pre_present_raster_probe_overhead_removal_clamped,"
        "injected_submission_advance_requested_us,"
        "injected_submission_advance_applied_us,"
        "simulated_free_running_refresh_eligible,"
        "simulated_free_running_refresh_baseline,"
        "simulated_free_running_refresh_comparison,"
        "simulated_free_running_propagated_phase_ps,"
        "simulated_free_running_phase_reference_compared,"
        "simulated_free_running_phase_reference_difference_ps,"
        "simulated_free_running_refresh_delta_lower,"
        "simulated_free_running_refresh_delta,"
        "simulated_free_running_refresh_delta_upper,"
        "simulated_free_running_scanout_anomaly_lower,"
        "simulated_free_running_scanout_anomaly,"
        "simulated_free_running_scanout_anomaly_upper,"
        "simulated_free_running_repeated_refresh_lower,"
        "simulated_free_running_repeated_refresh,"
        "simulated_free_running_repeated_refresh_upper,"
        "observed_present_sequence_delta,observed_present_refresh_delta,"
        "observed_scanout_anomaly_delta,observed_repeated_refresh_delta,"
        "exact_present_refresh_timestamp_valid,"
        "recorded_exact_refresh_phase_us,simulated_exact_refresh_phase_us,"
        "recorded_exact_active_scanout_phase_us,"
        "simulated_exact_active_scanout_phase_us,"
        "recorded_exact_refresh_class,simulated_exact_refresh_class,"
        "recorded_raster_envelope,simulated_raster_envelope,"
        "recorded_modeled_transition_us,simulated_modeled_transition_us,"
        "recorded_model_anchor_us,simulated_model_anchor_us,"
        "recorded_vrr_locked_state,simulated_vrr_locked_state,"
        "recorded_free_running_state,simulated_free_running_state,"
        "recorded_anchor_age_us,simulated_anchor_age_us,"
        "recorded_vrr_locked_phase_valid,"
        "simulated_vrr_locked_phase_valid,"
        "recorded_vrr_locked_phase_ps,simulated_vrr_locked_phase_ps,"
        "recorded_free_running_phase_us,simulated_free_running_phase_us,"
        "recorded_free_running_phase_ps,simulated_free_running_phase_ps,"
        "recorded_resolved_scanout_period_ps,"
        "simulated_resolved_scanout_period_ps,"
        "recorded_resolved_active_scanout_ps,"
        "simulated_resolved_active_scanout_ps,"
        "recorded_resolved_phase_uncertainty_ps,"
        "simulated_resolved_phase_uncertainty_ps,"
        "recorded_vrr_locked_scanout_position_valid,"
        "recorded_vrr_locked_scanout_position_ppm,"
        "simulated_vrr_locked_scanout_position_valid,"
        "simulated_vrr_locked_scanout_position_ppm,"
        "recorded_free_running_scanout_position_valid,"
        "recorded_free_running_scanout_position_ppm,"
        "simulated_free_running_scanout_position_valid,"
        "simulated_free_running_scanout_position_ppm\n";
    return file.write(header) == header.size();
}

bool writeTimelineRow(QFile& file, uint64_t arrivalSequence, int frame,
                      const QByteArray& disposition,
                      uint64_t decodeCompleteUs, uint64_t pacerArrivalUs,
                      uint64_t recordedTargetUs, uint64_t simulatedTargetUs,
                      uint64_t recordedSubmissionUs,
                      uint64_t simulatedSubmissionUs,
                      const QByteArray& recordedTear,
                      const QByteArray& simulatedTear,
                      const TimelineDetails& details)
{
    QByteArray line;
    line.reserve(768);
    const auto append = [&line](const QByteArray& value) {
        if (!line.isEmpty()) {
            line.append(',');
        }
        line.append(value);
    };
    append(QByteArray::number(arrivalSequence));
    append(QByteArray::number(frame));
    append(disposition);
    append(QByteArray::number(decodeCompleteUs));
    append(QByteArray::number(details.decoderOutputUs));
    append(QByteArray::number(pacerArrivalUs));
    append(QByteArray::number(recordedTargetUs));
    append(QByteArray::number(simulatedTargetUs));
    append(QByteArray::number(signedDifference(simulatedTargetUs,
                                               recordedTargetUs)));
    append(QByteArray::number(recordedSubmissionUs));
    append(QByteArray::number(simulatedSubmissionUs));
    append(QByteArray::number(signedDifference(simulatedSubmissionUs,
                                               recordedSubmissionUs)));
    append(QByteArray::number(
        details.recordedPresenterSubmissionTimeValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedPresenterSubmissionTimeUs));
    append(QByteArray::number(
        details.recordedPresenterSubmissionTimeUsed ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativePresentTimingValid ? 1 : 0));
    append(QByteArray::number(details.recordedNativePresentStartUs));
    append(QByteArray::number(
        details.recordedNativePresentBoundaryDeltaUs));
    append(QByteArray::number(
        details.recordedGpuReadyAttempted ? 1 : 0));
    append(QByteArray::number(
        details.recordedGpuReadySignalResultValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedGpuReadySignalResult));
    append(QByteArray::number(
        details.recordedGpuReadySetEventResultValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedGpuReadySetEventResult));
    append(QByteArray::number(
        details.recordedGpuReadyWaitResultValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedGpuReadyWaitResult));
    append(QByteArray::number(
        details.recordedGpuReadyTimingValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedGpuReadySignalStartUs));
    append(QByteArray::number(
        details.recordedGpuReadySignalEndUs));
    append(QByteArray::number(
        details.recordedGpuReadyFlushStartUs));
    append(QByteArray::number(
        details.recordedGpuReadyFlushEndUs));
    append(QByteArray::number(
        details.recordedGpuReadySetEventStartUs));
    append(QByteArray::number(
        details.recordedGpuReadySetEventEndUs));
    append(QByteArray::number(
        details.recordedGpuReadyPollStartUs));
    append(QByteArray::number(
        details.recordedGpuReadyPollEndUs));
    append(QByteArray::number(
        details.recordedGpuReadyFenceValue));
    append(QByteArray::number(
        details.recordedGpuReadyPollCompletedValue));
    append(QByteArray::number(
        details.recordedGpuReadyCompletedBeforeWait ? 1 : 0));
    append(QByteArray::number(
        details.recordedGpuReadyCompletionBoundsValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedGpuReadyCompletionLowerBoundUs));
    append(QByteArray::number(
        details.recordedGpuReadyCompletionUpperBoundUs));
    append(QByteArray::number(
        details.recordedGpuReadyCompletionUncertaintyUs));
    append(QByteArray::number(
        details.recordedGpuReadyWaitStartUs));
    append(QByteArray::number(
        details.recordedGpuReadyWaitReturnUs));
    append(QByteArray::number(
        details.recordedGpuReadyWaitUs));
    append(QByteArray::number(
        details.recordedNativeBackendValid ? 1 : 0));
    append(QByteArray::number(details.recordedNativeBackend));
    append(QByteArray::number(
        details.recordedNativePresentResultValid ? 1 : 0));
    append(QByteArray::number(details.recordedNativePresentResult));
    append(QByteArray::number(
        details.recordedNativePresentParametersValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativePresentSyncInterval));
    append(QByteArray::number(details.recordedNativePresentFlags));
    append(QByteArray::number(
        details.recordedNativeVrrStateValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeTearingSupported ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeBorderlessFlipModel ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeSameGpuOutput ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeRenderAdapterLuidValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeRenderAdapterLuid));
    append(QByteArray::number(
        details.recordedNativeSwapChainAllowsTearing ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativePresentReadyAvailable ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeForegroundWindow ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeVrrFallbackReason));
    append(QByteArray::number(
        details.recordedNativeDesktopMonitorCount));
    append(QByteArray::number(
        details.recordedNativeVblankVirtualizationProbeComplete ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeVblankVirtualizationCallAvailable ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeVblankVirtualizationResultValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedNativeVblankVirtualizationResult));
    append(QByteArray::number(
        details.recordedNativeVblankVirtualizationDisabled ? 1 : 0));
    append(QByteArray::number(
        details.recordedSubmissionIdQueryResultValid ? 1 : 0));
    append(QByteArray::number(details.recordedSubmissionIdQueryResult));
    append(QByteArray::number(
        details.recordedSubmissionIdQueryStartUs));
    append(QByteArray::number(
        details.recordedSubmissionIdQueryEndUs));
    append(QByteArray::number(
        details.recordedFrameStatsQueryResultValid ? 1 : 0));
    append(QByteArray::number(details.recordedFrameStatsQueryResult));
    append(QByteArray::number(
        details.recordedFrameStatsQueryStartUs));
    append(QByteArray::number(
        details.recordedFrameStatsQueryEndUs));
    append(QByteArray::number(recordedSubmissionUs >= details.decoderOutputUs ?
        recordedSubmissionUs - details.decoderOutputUs : 0));
    append(QByteArray::number(simulatedSubmissionUs >= details.decoderOutputUs ?
        simulatedSubmissionUs - details.decoderOutputUs : 0));
    append(recordedTear);
    append(simulatedTear);
    append(QByteArray::number(details.recordedSourceRateHz));
    append(QByteArray::number(details.simulatedSourceRateHz));
    append(QByteArray::number(details.recordedSourcePeriodUs));
    append(QByteArray::number(details.simulatedSourcePeriodUs));
    append(QByteArray::number(details.simulatedReadyOffsetUs));
    append(QByteArray::number(details.simulatedRenderLeadUs));
    append(QByteArray::number(details.simulatedReadinessBudgetUs));
    append(QByteArray::number(details.simulatedSourceTimeUs));
    append(QByteArray::number(details.simulatedPlayoutDelayUs));
    append(QByteArray::number(details.simulatedCadenceSmoothingUs));
    append(QByteArray::number(details.simulatedCadenceEligible ? 1 : 0));
    append(QByteArray::number(details.simulatedSourceRateChanged ? 1 : 0));
    append(QByteArray::number(details.simulatedPhaseDiscontinuity ? 1 : 0));
    append(QByteArray::number(details.recordedCadence.valid ? 1 : 0));
    append(QByteArray::number(details.simulatedCadence.valid ? 1 : 0));
    append(QByteArray::number(details.recordedCadence.sourceElapsedUs));
    append(QByteArray::number(details.simulatedCadence.sourceElapsedUs));
    append(QByteArray::number(details.recordedCadence.submissionElapsedUs));
    append(QByteArray::number(details.simulatedCadence.submissionElapsedUs));
    append(QByteArray::number(details.recordedCadence.residualUs));
    append(QByteArray::number(details.simulatedCadence.residualUs));
    append(QByteArray::number(details.recordedCadence.jerkValid ? 1 : 0));
    append(QByteArray::number(details.simulatedCadence.jerkValid ? 1 : 0));
    append(QByteArray::number(details.recordedCadence.jerkUs));
    append(QByteArray::number(details.simulatedCadence.jerkUs));
    append(QByteArray::number(details.recordedCadence.phaseUs));
    append(QByteArray::number(details.simulatedCadence.phaseUs));
    append(QByteArray::number(details.recordedSpacingMarginUs));
    append(QByteArray::number(details.simulatedSpacingMarginUs));
    append(QByteArray::number(details.recordedSpacingDeficitUs));
    append(QByteArray::number(
        details.recordedSpacingGuardFeedbackUs));
    append(QByteArray::number(
        details.recordedSpacingCorrected ? 1 : 0));
    append(QByteArray::number(details.recordedSpacingCheckUs));
    append(QByteArray::number(details.recordedPresentationFloorUs));
    append(QByteArray::number(details.recordedSpacingRecheckUs));
    append(QByteArray::number(
        details.recordedSpacingCorrectedFloorUs));
    append(QByteArray::number(
        details.recordedCorrectionWaitStartUs));
    append(QByteArray::number(
        details.recordedCorrectionWaitEndUs));
    append(QByteArray::number(details.guardUs));
    append(QByteArray::number(details.headroomUs));
    append(QByteArray::number(details.readinessBudgetUs));
    append(QByteArray::number(details.readinessReserveUs));
    append(QByteArray::number(details.queueDepthBefore));
    append(QByteArray::number(details.queueDepthAfter));
    append(QByteArray::number(details.completionQueueDepth));
    append(QByteArray::number(details.queueDiscontinuity ? 1 : 0));
    append(QByteArray::number(details.recordedLatched ? 1 : 0));
    append(QByteArray::number(details.simulatedLatched ? 1 : 0));
    append(QByteArray::number(details.latchValid ? 1 : 0));
    append(QByteArray::number(details.latchSubmissionId));
    append(QByteArray::number(details.latchTimeUs));
    append(QByteArray::number(details.latchRawSyncQpcValid ? 1 : 0));
    append(QByteArray::number(details.latchRawSyncQpcTicks));
    append(QByteArray::number(details.latchRawSyncQpcFrequency));
    append(QByteArray::number(
        details.latchQpcCorrelationValid ? 1 : 0));
    append(QByteArray::number(
        details.latchQpcCorrelationReferenceTicks));
    append(QByteArray::number(
        details.latchQpcCorrelationReferenceTimeUs));
    append(QByteArray::number(
        details.latchQpcCorrelationSpanTicks));
    append(QByteArray::number(details.latchPresentRefreshSequence));
    append(QByteArray::number(details.latchSyncRefreshSequence));
    append(QByteArray::number(details.prePresentSyncSampleValid ? 1 : 0));
    append(QByteArray::number(
        details.prePresentSyncAnchorIntegrityValid ? 1 : 0));
    append(QByteArray::number(details.prePresentSyncSampleUs));
    append(QByteArray::number(details.prePresentSyncRefreshSequence));
    append(QByteArray::number(details.recordedDecisionUs));
    append(QByteArray::number(details.simulatedDecisionUs));
    append(QByteArray::number(
        details.recordedExternalRebaseApplied ? 1 : 0));
    append(QByteArray::number(
        details.recordedExternalRebaseFlags));
    append(QByteArray::number(
        details.recordedMidframeWindowStateFlags));
    append(QByteArray::number(details.recordedRenderWaitOvershootUs));
    append(QByteArray::number(details.recordedRenderSchedulerDelayUs));
    append(QByteArray::number(
        details.recordedRenderSchedulerDelayValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedRenderDeadlineAlreadyElapsed ? 1 : 0));
    append(QByteArray::number(details.recordedTargetWaitOvershootUs));
    append(QByteArray::number(details.recordedTargetSchedulerDelayUs));
    append(QByteArray::number(
        details.recordedTargetSchedulerDelayValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedTargetDeadlineAlreadyElapsed ? 1 : 0));
    append(QByteArray::number(details.injectedDecisionDelayUs));
    append(QByteArray::number(details.requestedRenderWakeDelayUs));
    append(QByteArray::number(
        details.renderWakeDelayEligible ? 1 : 0));
    append(QByteArray::number(
        details.renderWaitPathCompared ? 1 : 0));
    append(QByteArray::number(
        details.renderWaitPathMatchesCaptured ? 1 : 0));
    append(QByteArray::number(
        details.renderWaitRecordedFinalResidualUsed ? 1 : 0));
    append(QByteArray::number(details.injectedRenderWakeDelayUs));
    append(QByteArray::number(details.absorbedRenderWakeDelayUs));
    append(QByteArray::number(details.renderWakeExecutionDelayUs));
    append(QByteArray::number(details.requestedTargetWakeDelayUs));
    append(QByteArray::number(
        details.targetWakeDelayEligible ? 1 : 0));
    append(QByteArray::number(
        details.targetWaitPathCompared ? 1 : 0));
    append(QByteArray::number(
        details.targetWaitPathMatchesCaptured ? 1 : 0));
    append(QByteArray::number(
        details.targetWaitRecordedFinalResidualUsed ? 1 : 0));
    append(QByteArray::number(details.injectedTargetWakeDelayUs));
    append(QByteArray::number(details.absorbedTargetWakeDelayUs));
    append(QByteArray::number(details.targetWakeExecutionDelayUs));
    append(QByteArray::number(details.injectedPreparationDelayUs));
    append(QByteArray::number(details.injectedSubmissionDelayUs));
    append(QByteArray::number(
        details.injectedDisplayTransitionDelayUs));
    append(QByteArray::number(
        details.injectedSpacingGuardFeedbackUs));
    append(QByteArray::number(
        details.rasterProbeOverheadRemovalRequested ? 1 : 0));
    append(QByteArray::number(
        details.rasterProbeOverheadAvailable ? 1 : 0));
    append(QByteArray::number(
        details.measuredRasterProbeDurationUs));
    append(QByteArray::number(
        details.removedRasterProbeOverheadUs));
    append(QByteArray::number(
        details.rasterProbeOverheadRemovalClamped ? 1 : 0));
    append(QByteArray::number(
        details.injectedSubmissionAdvanceRequestedUs));
    append(QByteArray::number(
        details.injectedSubmissionAdvanceAppliedUs));
    append(QByteArray::number(
        details.simulatedFreeRunningRefreshEligible ? 1 : 0));
    append(QByteArray::number(
        details.simulatedFreeRunningRefreshBaseline ? 1 : 0));
    append(QByteArray::number(
        details.simulatedFreeRunningRefreshComparison ? 1 : 0));
    append(QByteArray::number(
        details.simulatedFreeRunningPropagatedPhasePs));
    append(QByteArray::number(
        details.simulatedFreeRunningPhaseReferenceCompared ? 1 : 0));
    append(QByteArray::number(
        details.simulatedFreeRunningPhaseReferenceDifferencePs));
    append(QByteArray::number(
        details.simulatedFreeRunningRefreshDeltaLower));
    append(QByteArray::number(
        details.simulatedFreeRunningRefreshDelta));
    append(QByteArray::number(
        details.simulatedFreeRunningRefreshDeltaUpper));
    append(QByteArray::number(
        details.simulatedFreeRunningScanoutAnomalyLower));
    append(QByteArray::number(
        details.simulatedFreeRunningScanoutAnomaly));
    append(QByteArray::number(
        details.simulatedFreeRunningScanoutAnomalyUpper));
    append(QByteArray::number(
        details.simulatedFreeRunningRepeatedRefreshLower));
    append(QByteArray::number(
        details.simulatedFreeRunningRepeatedRefresh));
    append(QByteArray::number(
        details.simulatedFreeRunningRepeatedRefreshUpper));
    append(QByteArray::number(details.observedPresentSequenceDelta));
    append(QByteArray::number(details.observedPresentRefreshDelta));
    append(QByteArray::number(details.observedScanoutAnomalyDelta));
    append(QByteArray::number(details.observedRepeatedRefreshDelta));
    append(QByteArray::number(
        details.exactPresentRefreshTimestampValid ? 1 : 0));
    append(QByteArray::number(details.recordedExactRefreshPhaseUs));
    append(QByteArray::number(details.simulatedExactRefreshPhaseUs));
    append(QByteArray::number(
        details.recordedExactActiveScanoutPhaseUs));
    append(QByteArray::number(
        details.simulatedExactActiveScanoutPhaseUs));
    append(details.recordedExactRefreshClass);
    append(details.simulatedExactRefreshClass);
    append(QByteArray(vrrRasterEnvelopeClassName(
        details.recordedRaster.envelope)));
    append(QByteArray(vrrRasterEnvelopeClassName(
        details.simulatedRaster.envelope)));
    append(QByteArray::number(details.recordedRaster.modeledTransitionUs));
    append(QByteArray::number(details.simulatedRaster.modeledTransitionUs));
    append(QByteArray::number(details.recordedRaster.anchorTimeUs));
    append(QByteArray::number(details.simulatedRaster.anchorTimeUs));
    append(QByteArray(vrrRasterPhaseStateName(
        details.recordedRaster.vrrLocked)));
    append(QByteArray(vrrRasterPhaseStateName(
        details.simulatedRaster.vrrLocked)));
    append(QByteArray(vrrRasterPhaseStateName(
        details.recordedRaster.freeRunning)));
    append(QByteArray(vrrRasterPhaseStateName(
        details.simulatedRaster.freeRunning)));
    append(QByteArray::number(details.recordedRaster.anchorAgeUs));
    append(QByteArray::number(details.simulatedRaster.anchorAgeUs));
    append(QByteArray::number(
        details.recordedRaster.vrrLockedPhaseValid ? 1 : 0));
    append(QByteArray::number(
        details.simulatedRaster.vrrLockedPhaseValid ? 1 : 0));
    append(QByteArray::number(details.recordedRaster.vrrLockedPhasePs));
    append(QByteArray::number(details.simulatedRaster.vrrLockedPhasePs));
    append(QByteArray::number(details.recordedRaster.freeRunningPhaseUs));
    append(QByteArray::number(details.simulatedRaster.freeRunningPhaseUs));
    append(QByteArray::number(details.recordedRaster.freeRunningPhasePs));
    append(QByteArray::number(details.simulatedRaster.freeRunningPhasePs));
    append(QByteArray::number(
        details.recordedRaster.resolvedScanoutPeriodPs));
    append(QByteArray::number(
        details.simulatedRaster.resolvedScanoutPeriodPs));
    append(QByteArray::number(
        details.recordedRaster.resolvedActiveScanoutPs));
    append(QByteArray::number(
        details.simulatedRaster.resolvedActiveScanoutPs));
    append(QByteArray::number(
        details.recordedRaster.resolvedPhaseUncertaintyPs));
    append(QByteArray::number(
        details.simulatedRaster.resolvedPhaseUncertaintyPs));
    append(QByteArray::number(
        details.recordedRaster.vrrLockedScanoutPositionValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedRaster.vrrLockedScanoutPositionPpm));
    append(QByteArray::number(
        details.simulatedRaster.vrrLockedScanoutPositionValid ? 1 : 0));
    append(QByteArray::number(
        details.simulatedRaster.vrrLockedScanoutPositionPpm));
    append(QByteArray::number(
        details.recordedRaster.freeRunningScanoutPositionValid ? 1 : 0));
    append(QByteArray::number(
        details.recordedRaster.freeRunningScanoutPositionPpm));
    append(QByteArray::number(
        details.simulatedRaster.freeRunningScanoutPositionValid ? 1 : 0));
    append(QByteArray::number(
        details.simulatedRaster.freeRunningScanoutPositionPpm));
    line.append('\n');
    return file.write(line) == line.size();
}

QJsonValue jsonPathValue(const QJsonObject& root, const QString& path)
{
    QJsonValue value(root);
    for (const QString& component : path.split('.', Qt::SkipEmptyParts)) {
        if (!value.isObject()) return {};
        value = value.toObject().value(component);
    }
    return value;
}

bool evaluateAssertions(const VrrReplayScenario& scenario,
                        QJsonObject& summary)
{
    QJsonArray results;
    bool passed = true;
    for (const VrrReplayAssertion& assertion : scenario.assertions) {
        const QJsonValue metricValue = jsonPathValue(summary, assertion.metric);
        const double actual = metricValue.toDouble(
            std::numeric_limits<double>::quiet_NaN());
        bool result = std::isfinite(actual);
        if (result && assertion.operation == "<") result = actual < assertion.value;
        else if (result && assertion.operation == "<=") result = actual <= assertion.value;
        else if (result && assertion.operation == "==") result = actual == assertion.value;
        else if (result && assertion.operation == ">=") result = actual >= assertion.value;
        else if (result && assertion.operation == ">") result = actual > assertion.value;
        QJsonObject item;
        item["metric"] = assertion.metric;
        item["operator"] = assertion.operation;
        item["expected"] = assertion.value;
        item["actual"] = std::isfinite(actual) ? QJsonValue(actual) : QJsonValue();
        item["passed"] = result;
        results.append(item);
        passed = passed && result;
    }
    QJsonObject assertionSummary;
    assertionSummary["configured"] = !scenario.assertions.isEmpty();
    assertionSummary["passed"] = passed;
    assertionSummary["results"] = results;
    summary["assertions"] = assertionSummary;
    return passed;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("vrrreplay");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Replay a captured VRR workload and simulate the current timing controller");
    parser.addHelpOption();
    parser.addPositionalArgument("trace", "Input .vrrtrace or expanded CSV");
    QCommandLineOption displayOption(
        "display-hz", "Override display refresh for the candidate simulation", "hz");
    QCommandLineOption streamOption(
        "stream-fps", "Override negotiated stream rate for the candidate simulation", "fps");
    QCommandLineOption latchOption(
        "no-latch", "Disable per-present latched mode in the candidate simulation");
    QCommandLineOption compareOption(
        "compare", "Add deltas against a prior replay JSON summary", "json");
    QCommandLineOption outputOption(
        "output", "Write UTF-8 JSON to a file instead of stdout", "json");
    QCommandLineOption timelineOption(
        "timeline", "Write the recorded and simulated per-frame timeline as CSV", "csv");
    QCommandLineOption exactOption(
        "require-exact-baseline",
        "Fail unless the trace sequence is complete and an unmodified configuration exactly reproduces all controller decision fields, submissions, tear classes, and raster-envelope classes");
    QCommandLineOption controllerReadyOption(
        "require-controller-ready",
        "Fail unless the capture passes every controller-replay readiness gate");
    QCommandLineOption diagnosticCaptureReadyOption(
        "require-diagnostic-capture-ready",
        "Fail unless the capture contains the complete, calibration-independent telemetry needed for later raster simulation");
    QCommandLineOption rasterReadyOption(
        "require-raster-ready",
        "Fail unless the capture passes every raster-simulation readiness gate");
    QCommandLineOption counterfactualRefreshReadyOption(
        "require-counterfactual-refresh-ready",
        "Fail unless the candidate has a complete calibrated free-running counterfactual refresh timeline");
    QCommandLineOption configOption(
        "config", "Load versioned replay scenarios and parameters", "json");
    QCommandLineOption scenarioOption(
        "scenario", "Run only the named scenario (repeatable)", "name");
    QCommandLineOption jobsOption(
        "jobs",
        "Maximum parallel replay processes for a batch (0 selects an automatic limit)",
        "count", "0");
    QCommandLineOption setOption(
        "set", "Override a resolved parameter as section.name=value", "override");
    QCommandLineOption modeOption(
        "mode", "Override scenario mode: fixed or worker", "mode");
    QCommandLineOption listParametersOption(
        "list-parameters", "List every replay parameter and exit");
    QCommandLineOption dumpDefaultsOption(
        "dump-default-config", "Print a complete default JSON configuration and exit");
    parser.addOption(displayOption);
    parser.addOption(streamOption);
    parser.addOption(latchOption);
    parser.addOption(compareOption);
    parser.addOption(outputOption);
    parser.addOption(timelineOption);
    parser.addOption(exactOption);
    parser.addOption(controllerReadyOption);
    parser.addOption(diagnosticCaptureReadyOption);
    parser.addOption(rasterReadyOption);
    parser.addOption(counterfactualRefreshReadyOption);
    parser.addOption(configOption);
    parser.addOption(scenarioOption);
    parser.addOption(jobsOption);
    parser.addOption(setOption);
    parser.addOption(modeOption);
    parser.addOption(listParametersOption);
    parser.addOption(dumpDefaultsOption);
    parser.process(application);

    if (parser.isSet(listParametersOption)) {
        for (const QString& name : vrrReplayParameterNames()) {
            if (std::printf("%s\n", qPrintable(name)) < 0) {
                std::fprintf(stderr,
                             "Unable to write replay parameter list\n");
                return 1;
            }
        }
        if (std::fflush(stdout) != 0) {
            std::fprintf(stderr,
                         "Unable to finish replay parameter list\n");
            return 1;
        }
        return 0;
    }
    if (parser.isSet(dumpDefaultsOption)) {
        const QByteArray defaults = QJsonDocument(
            vrrDefaultReplayConfigurationJson()).toJson(QJsonDocument::Indented);
        if (std::fwrite(
                defaults.constData(), 1,
                static_cast<size_t>(defaults.size()), stdout) !=
                    static_cast<size_t>(defaults.size()) ||
                std::fflush(stdout) != 0) {
            std::fprintf(stderr,
                         "Unable to write default replay configuration\n");
            return 1;
        }
        return 0;
    }

    if (parser.positionalArguments().size() != 1) {
        parser.showHelp(2);
    }
    if (parser.isSet(exactOption) &&
            (parser.isSet(displayOption) || parser.isSet(streamOption) ||
             parser.isSet(latchOption) || parser.isSet(setOption) ||
             parser.isSet(modeOption) || parser.isSet(configOption))) {
        std::fprintf(stderr,
                     "--require-exact-baseline cannot be used with candidate overrides\n");
        return 2;
    }
    int displayOverrideHz = 0;
    int streamOverrideFps = 0;
    const auto parseRateOverride =
        [&parser](const QCommandLineOption& option, const char* name,
                  int& value) {
            if (!parser.isSet(option)) {
                return true;
            }
            bool ok = false;
            value = parser.value(option).toInt(&ok);
            if (!ok || value <= 0 || periodForRate(value) == 0) {
                std::fprintf(
                    stderr, "%s must be a positive rate with a non-zero microsecond period\n",
                    name);
                return false;
            }
            return true;
        };
    if (!parseRateOverride(displayOption, "--display-hz",
                           displayOverrideHz) ||
            !parseRateOverride(streamOption, "--stream-fps",
                               streamOverrideFps)) {
        return 2;
    }
    bool jobsOk = false;
    const int requestedJobs = parser.value(jobsOption).toInt(&jobsOk);
    if (!jobsOk || requestedJobs < 0 || requestedJobs > 64) {
        std::fprintf(stderr, "--jobs must be an integer from 0 through 64\n");
        return 2;
    }

    const QString tracePath = parser.positionalArguments().front();
    const auto pathsReferToSameName =
        [](const QString& left, const QString& right) {
            if (left.isEmpty() || right.isEmpty()) {
                return false;
            }
            const QString leftPath = QDir::cleanPath(
                QFileInfo(left).absoluteFilePath());
            const QString rightPath = QDir::cleanPath(
                QFileInfo(right).absoluteFilePath());
#ifdef Q_OS_WIN
            return leftPath.compare(
                rightPath, Qt::CaseInsensitive) == 0;
#else
            return leftPath == rightPath;
#endif
        };
    if ((parser.isSet(outputOption) &&
         pathsReferToSameName(tracePath, parser.value(outputOption))) ||
            (parser.isSet(timelineOption) &&
             pathsReferToSameName(tracePath, parser.value(timelineOption))) ||
            (parser.isSet(outputOption) && parser.isSet(timelineOption) &&
             pathsReferToSameName(
                 parser.value(outputOption),
                 parser.value(timelineOption)))) {
        std::fprintf(
            stderr,
            "Trace, JSON output, and timeline paths must be distinct\n");
        return 2;
    }
    QString error;

    VrrReplayConfiguration replayConfiguration;
    if (parser.isSet(configOption)) {
        QFile configFile(parser.value(configOption));
        if (!configFile.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "Unable to load replay config: %s\n",
                         qPrintable(configFile.errorString()));
            return 2;
        }
        const QByteArray configData = configFile.readAll();
        if (configFile.error() != QFileDevice::NoError) {
            std::fprintf(stderr, "Unable to read replay config: %s\n",
                         qPrintable(configFile.errorString()));
            return 2;
        }
        if (!loadVrrReplayConfiguration(
                configData, replayConfiguration, error)) {
            std::fprintf(stderr, "Unable to load replay config: %s\n",
                         qPrintable(error));
            return 2;
        }
    }
    else {
        replayConfiguration.scenarios.append(VrrReplayScenario {});
    }

    const QStringList requestedScenarios = parser.values(scenarioOption);
    if (!requestedScenarios.isEmpty()) {
        QList<VrrReplayScenario> filtered;
        for (const QString& requested : requestedScenarios) {
            const auto found = std::find_if(
                replayConfiguration.scenarios.cbegin(),
                replayConfiguration.scenarios.cend(),
                [&requested](const VrrReplayScenario& item) {
                    return item.name == requested;
                });
            if (found == replayConfiguration.scenarios.cend()) {
                std::fprintf(stderr, "Unknown replay scenario: %s\n",
                             qPrintable(requested));
                return 2;
            }
            filtered.append(*found);
        }
        replayConfiguration.scenarios = filtered;
    }

    if (replayConfiguration.scenarios.size() > 1) {
        if (parser.isSet(timelineOption)) {
            std::fprintf(stderr,
                         "--timeline requires selecting a single scenario\n");
            return 2;
        }
        const int idealThreadCount = std::max(1, QThread::idealThreadCount());
        const int automaticJobs = std::min(
            16, idealThreadCount);
        const int parallelJobs = std::min(
            static_cast<int>(replayConfiguration.scenarios.size()),
            requestedJobs == 0 ? automaticJobs : requestedJobs);
        struct BatchJob {
            int index = 0;
            QString name;
            std::unique_ptr<QProcess> process;
            QByteArray output;
            QByteArray errors;
        };
        std::vector<std::unique_ptr<BatchJob>> activeJobs;
        std::vector<QJsonObject> orderedResults(
            static_cast<size_t>(replayConfiguration.scenarios.size()));
        bool allPassed = true;
        int firstFailureExitCode = 0;
        int nextScenario = 0;
        QElapsedTimer batchTimer;
        batchTimer.start();
        const auto stopActiveJobs = [&activeJobs]() {
            for (const std::unique_ptr<BatchJob>& job : activeJobs) {
                if (job->process->state() == QProcess::NotRunning) {
                    continue;
                }
                job->process->terminate();
                if (!job->process->waitForFinished(2000)) {
                    job->process->kill();
                    job->process->waitForFinished(2000);
                }
            }
        };
        const auto scenarioArguments = [&](const QString& scenarioName) {
            QStringList arguments { tracePath, "--config",
                                    parser.value(configOption), "--scenario",
                                    scenarioName };
            for (const QString& overrideValue : parser.values(setOption))
                arguments << "--set" << overrideValue;
            if (parser.isSet(modeOption)) arguments << "--mode" << parser.value(modeOption);
            if (parser.isSet(displayOption)) arguments << "--display-hz" << parser.value(displayOption);
            if (parser.isSet(streamOption)) arguments << "--stream-fps" << parser.value(streamOption);
            if (parser.isSet(latchOption)) arguments << "--no-latch";
            if (parser.isSet(compareOption)) arguments << "--compare" << parser.value(compareOption);
            if (parser.isSet(controllerReadyOption))
                arguments << "--require-controller-ready";
            if (parser.isSet(diagnosticCaptureReadyOption))
                arguments << "--require-diagnostic-capture-ready";
            if (parser.isSet(rasterReadyOption))
                arguments << "--require-raster-ready";
            if (parser.isSet(counterfactualRefreshReadyOption))
                arguments << "--require-counterfactual-refresh-ready";
            return arguments;
        };
        while (nextScenario < replayConfiguration.scenarios.size() ||
               !activeJobs.empty()) {
            while (nextScenario < replayConfiguration.scenarios.size() &&
                   static_cast<int>(activeJobs.size()) < parallelJobs) {
                const VrrReplayScenario& scenario =
                    replayConfiguration.scenarios.at(nextScenario);
                auto job = std::make_unique<BatchJob>();
                job->index = nextScenario;
                job->name = scenario.name;
                job->process = std::make_unique<QProcess>();
                job->process->start(
                    QCoreApplication::applicationFilePath(),
                    scenarioArguments(scenario.name));
                if (!job->process->waitForStarted()) {
                    std::fprintf(
                        stderr, "Unable to start replay scenario %s: %s\n",
                        qPrintable(scenario.name),
                        qPrintable(job->process->errorString()));
                    stopActiveJobs();
                    return 1;
                }
                activeJobs.push_back(std::move(job));
                ++nextScenario;
            }
            bool completedJob = false;
            for (auto jobIt = activeJobs.begin();
                 jobIt != activeJobs.end();) {
                BatchJob& job = **jobIt;
                job.process->waitForFinished(1);
                job.output.append(job.process->readAllStandardOutput());
                job.errors.append(job.process->readAllStandardError());
                if (job.process->state() != QProcess::NotRunning) {
                    ++jobIt;
                    continue;
                }
                completedJob = true;
                QJsonParseError childError;
                const QJsonDocument childDocument = QJsonDocument::fromJson(
                    job.output, &childError);
                if (!childDocument.isObject()) {
                    std::fprintf(stderr, "Scenario %s failed: %s%s\n",
                                 qPrintable(job.name),
                                 qPrintable(childError.errorString()),
                                 job.errors.constData());
                    const int exitCode = job.process->exitCode();
                    stopActiveJobs();
                    return exitCode == 0 ? 1 : exitCode;
                }
                QJsonObject result = childDocument.object();
                result["scenario"] = job.name;
                result["exit_code"] = job.process->exitCode();
                orderedResults[static_cast<size_t>(job.index)] = result;
                allPassed = allPassed && job.process->exitCode() == 0;
                if (firstFailureExitCode == 0 &&
                        job.process->exitCode() != 0) {
                    firstFailureExitCode = job.process->exitCode();
                }
                jobIt = activeJobs.erase(jobIt);
            }
            if (!completedJob && !activeJobs.empty()) {
                QThread::msleep(1);
            }
        }
        QJsonArray scenarioResults;
        for (const QJsonObject& result : orderedResults) {
            scenarioResults.append(result);
        }
        QJsonObject batch;
        batch["config_schema"] = 1;
        batch["trace"] = QFileInfo(tracePath).fileName();
        batch["scenarios"] = scenarioResults;
        batch["passed"] = allPassed;
        batch["parallel_jobs"] = parallelJobs;
        batch["elapsed_ms"] = static_cast<double>(batchTimer.elapsed());
        const QByteArray output = QJsonDocument(batch).toJson(
            QJsonDocument::Indented);
        if (parser.isSet(outputOption)) {
            QFile outputFile(parser.value(outputOption));
            if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                    outputFile.write(output) != output.size() ||
                    !outputFile.flush()) {
                std::fprintf(stderr, "Unable to write replay batch: %s\n",
                             qPrintable(outputFile.errorString()));
                return 1;
            }
            outputFile.close();
            if (outputFile.error() != QFileDevice::NoError) {
                std::fprintf(stderr, "Unable to finish replay batch: %s\n",
                             qPrintable(outputFile.errorString()));
                return 1;
            }
        }
        else if (std::fwrite(
                     output.constData(), 1,
                     static_cast<size_t>(output.size()), stdout) !=
                     static_cast<size_t>(output.size()) ||
                 std::fflush(stdout) != 0) {
            std::fprintf(stderr, "Unable to write replay batch to stdout\n");
            return 1;
        }
        return allPassed ? 0 :
            (firstFailureExitCode != 0 ? firstFailureExitCode : 4);
    }

    VrrReplayScenario scenario = replayConfiguration.scenarios.front();
    if (parser.isSet(modeOption)) scenario.mode = parser.value(modeOption);
    if (scenario.mode != "fixed" && scenario.mode != "worker") {
        std::fprintf(stderr, "--mode must be fixed or worker\n"); return 2;
    }
    for (const QString& overrideValue : parser.values(setOption)) {
        if (!applyVrrReplayOverride(overrideValue, scenario, error)) {
            std::fprintf(stderr, "Invalid replay override: %s\n",
                         qPrintable(error)); return 2;
        }
    }
    const bool periodicEffectConfigured =
        scenario.execution.periodicStallUs != 0 ||
        scenario.execution.periodicRenderWakeDelayUs != 0 ||
        scenario.execution.periodicTargetWakeDelayUs != 0 ||
        scenario.execution.periodicPreparationStallUs != 0 ||
        scenario.execution.periodicSubmissionStallUs != 0 ||
        scenario.execution.periodicDisplayTransitionDelayUs != 0 ||
        scenario.execution.periodicSpacingGuardFeedbackUs != 0 ||
        scenario.execution.periodicSubmissionAdvanceUs != 0;
    if (periodicEffectConfigured !=
            (scenario.execution.periodicStallEveryFrames != 0)) {
        std::fprintf(
            stderr,
            "Periodic execution injection requires periodic_stall_every_frames and at least one periodic stage effect\n");
        return 2;
    }
    if ((scenario.execution.periodicStallEveryFrames == 0 &&
         (scenario.execution.periodicStallPhaseFrames != 0 ||
          scenario.execution.periodicStallBurstFrames != 1)) ||
            (scenario.execution.periodicStallEveryFrames != 0 &&
             (scenario.execution.periodicStallPhaseFrames >=
                  scenario.execution.periodicStallEveryFrames ||
              scenario.execution.periodicStallBurstFrames >
                  scenario.execution.periodicStallEveryFrames))) {
        std::fprintf(
            stderr,
            "Periodic execution phase must be below the period and burst must be between 1 and the period\n");
        return 2;
    }
    TraceReader reader(tracePath);
    if (!reader.open(error)) {
        std::fprintf(stderr, "Unable to open trace: %s\n",
                     qPrintable(error));
        return 1;
    }

    QByteArray headerLine;
    if (!reader.readLine(headerLine, error)) {
        std::fprintf(stderr, "Unable to read trace header: %s\n",
                     qPrintable(error));
        return 1;
    }
    const QList<QByteArray> traceHeader =
        headerLine.trimmed().split(',');
    Columns columns;
    if (!columns.resolve(traceHeader, error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    VrrRasterScanLineScaleInference nativeRasterScanLineScaleInference;
    if (!inferNativeRasterScanLineScale(
            tracePath, traceHeader, columns,
            nativeRasterScanLineScaleInference, error)) {
        std::fprintf(
            stderr,
            "Unable to infer native raster scan-line scale: %s\n",
            qPrintable(error));
        return 1;
    }
    QCryptographicHash decodedTraceHash(QCryptographicHash::Sha256);
    decodedTraceHash.addData(headerLine.trimmed());
    decodedTraceHash.addData(QByteArrayLiteral("\n"));

    QFile timelineFile;
    if (parser.isSet(timelineOption)) {
        timelineFile.setFileName(parser.value(timelineOption));
        if (!timelineFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                !writeTimelineHeader(timelineFile)) {
            std::fprintf(stderr, "Unable to create timeline: %s\n",
                         qPrintable(timelineFile.errorString()));
            return 1;
        }
    }

    Metrics metrics;
    metrics.nativeRasterScanLineScaleInference =
        nativeRasterScanLineScaleInference;
    metrics.externalRebaseTelemetryAvailable =
        columns.externalRebaseApplied >= 0;
    metrics.windowStateCauseTelemetryAvailable =
        columns.externalRebaseFlags >= 0 &&
        columns.midframeWindowStateFlags >= 0;
    metrics.presentTimingIntegrityTelemetryAvailable =
        columns.presentStartUs >= 0 &&
        columns.presentEndUs >= 0 &&
        columns.nativePresentStartUs >= 0 &&
        columns.nativePresentEndUs >= 0;
    metrics.presenterSubmissionTimingTelemetryAvailable =
        columns.presenterSubmissionTimeValid >= 0 &&
        columns.presenterSubmissionTimeUs >= 0 &&
        columns.presenterSubmissionTimeUsed >= 0;
    metrics.spacingCorrectionTelemetryAvailable =
        columns.spacingDeficitUs >= 0 &&
        columns.spacingGuardFeedbackUs >= 0 &&
        columns.spacingCorrected >= 0;
    metrics.spacingLifecycleTimingTelemetryAvailable =
        columns.spacingRecheckUs >= 0 &&
        columns.spacingCorrectedFloorUs >= 0;
    metrics.waitLifecycleTelemetryAvailable =
        columns.renderWaitInitialUs >= 0 &&
        columns.renderWaitActiveBudgetUs >= 0 &&
        columns.renderWaitCoarseSleepCount >= 0 &&
        columns.renderWaitCoarseRequestedTotalUs >= 0 &&
        columns.renderWaitCoarseRequestedWakeUs >= 0 &&
        columns.renderWaitCoarseReturnUs >= 0 &&
        columns.renderWaitCoarseClockStalled >= 0 &&
        columns.renderWaitActiveEntered >= 0 &&
        columns.renderWaitActiveStartUs >= 0 &&
        columns.renderWaitActiveLimitUs >= 0 &&
        columns.renderWaitActiveYieldCount >= 0 &&
        columns.renderWaitActiveClockStalled >= 0 &&
        columns.renderWaitActiveYieldLimitReached >= 0 &&
        columns.targetWaitInitialUs >= 0 &&
        columns.targetWaitActiveBudgetUs >= 0 &&
        columns.targetWaitCoarseSleepCount >= 0 &&
        columns.targetWaitCoarseRequestedTotalUs >= 0 &&
        columns.targetWaitCoarseRequestedWakeUs >= 0 &&
        columns.targetWaitCoarseReturnUs >= 0 &&
        columns.targetWaitCoarseClockStalled >= 0 &&
        columns.targetWaitActiveEntered >= 0 &&
        columns.targetWaitActiveStartUs >= 0 &&
        columns.targetWaitActiveLimitUs >= 0 &&
        columns.targetWaitActiveYieldCount >= 0 &&
        columns.targetWaitActiveClockStalled >= 0 &&
        columns.targetWaitActiveYieldLimitReached >= 0;
    metrics.completionQueueDepthTelemetryAvailable =
        columns.completionQueueDepth >= 0;
    metrics.gpuReadyNativeResultTelemetryAvailable =
        columns.gpuReadyAttempted >= 0 &&
        columns.gpuReadySignalResultValid >= 0 &&
        columns.gpuReadySignalResult >= 0 &&
        columns.gpuReadySetEventResultValid >= 0 &&
        columns.gpuReadySetEventResult >= 0 &&
        columns.gpuReadyWaitResultValid >= 0 &&
        columns.gpuReadyWaitResult >= 0;
    metrics.gpuReadyBoundsTelemetryAvailable =
        columns.gpuReadySignalStartUs >= 0 &&
        columns.gpuReadyPollStartUs >= 0 &&
        columns.gpuReadyPollEndUs >= 0 &&
        columns.gpuReadyFenceValue >= 0 &&
        columns.gpuReadyPollCompletedValue >= 0 &&
        columns.gpuReadyCompletedBeforeWait >= 0 &&
        columns.gpuReadyCompletionLowerBoundUs >= 0 &&
        columns.gpuReadyCompletionUpperBoundUs >= 0 &&
        columns.gpuReadyCompletionUncertaintyUs >= 0;
    metrics.gpuReadyStageTimingTelemetryAvailable =
        columns.gpuReadySignalEndUs >= 0 &&
        columns.gpuReadyFlushStartUs >= 0 &&
        columns.gpuReadyFlushEndUs >= 0 &&
        columns.gpuReadySetEventStartUs >= 0 &&
        columns.gpuReadySetEventEndUs >= 0;
    metrics.nativeOutcomeTelemetryAvailable =
        columns.nativeBackendValid >= 0 &&
        columns.nativeBackend >= 0 &&
        columns.nativePresentResultValid >= 0 &&
        columns.nativePresentResult >= 0 &&
        columns.submissionIdQueryResultValid >= 0 &&
        columns.submissionIdQueryResult >= 0 &&
        columns.frameStatsQueryResultValid >= 0 &&
        columns.frameStatsQueryResult >= 0 &&
        columns.latchRawSyncQpcValid >= 0 &&
        columns.latchRawSyncQpcTicks >= 0 &&
        columns.latchRawSyncQpcFrequency >= 0;
    metrics.postPresentQueryTimingTelemetryAvailable =
        columns.submissionIdQueryStartUs >= 0 &&
        columns.submissionIdQueryEndUs >= 0 &&
        columns.frameStatsQueryStartUs >= 0 &&
        columns.frameStatsQueryEndUs >= 0;
    metrics.nativePresentContractTelemetryAvailable =
        columns.nativePresentParametersValid >= 0 &&
        columns.nativePresentSyncInterval >= 0 &&
        columns.nativePresentFlags >= 0 &&
        columns.nativeVrrStateValid >= 0 &&
        columns.nativeTearingSupported >= 0 &&
        columns.nativeBorderlessFlipModel >= 0 &&
        columns.nativeSameGpuOutput >= 0 &&
        columns.nativeRenderAdapterLuidValid >= 0 &&
        columns.nativeRenderAdapterLuid >= 0 &&
        columns.nativeSwapChainAllowsTearing >= 0 &&
        columns.nativePresentReadyAvailable >= 0 &&
        columns.nativeForegroundWindow >= 0 &&
        columns.nativeVrrFallbackReason >= 0 &&
        columns.nativeDesktopMonitorCount >= 0;
    metrics.nativeDxgiCapabilityTelemetryAvailable =
        columns.nativeTearingFeatureQueryResultValid >= 0 &&
        columns.nativeTearingFeatureQueryResult >= 0 &&
        columns.nativeTearingFeatureAllowsTearing >= 0 &&
        columns.nativeSwapChainDescQueryResultValid >= 0 &&
        columns.nativeSwapChainDescQueryResult >= 0 &&
        columns.nativeSwapChainFlags >= 0 &&
        columns.nativeSwapChainSwapEffect >= 0 &&
        columns.nativeFullscreenStateQueryResultValid >= 0 &&
        columns.nativeFullscreenStateQueryResult >= 0 &&
        columns.nativeFullscreenExclusive >= 0 &&
        columns.nativeWindowFlags >= 0;
    metrics.nativeVblankVirtualizationTelemetryAvailable =
        columns.nativeVblankVirtualizationProbeComplete >= 0 &&
        columns.nativeVblankVirtualizationCallAvailable >= 0 &&
        columns.nativeVblankVirtualizationResultValid >= 0 &&
        columns.nativeVblankVirtualizationResult >= 0 &&
        columns.nativeVblankVirtualizationDisabled >= 0;
    metrics.nativeDisplayTimingTelemetryAvailable =
        columns.nativeDisplayConfigQueryResultValid >= 0 &&
        columns.nativeDisplayConfigQueryResult >= 0 &&
        columns.nativeDisplayPathValid >= 0 &&
        columns.nativeDisplayPathFlags >= 0 &&
        columns.nativeDisplayTargetAvailable >= 0 &&
        columns.nativeDisplaySourceAdapterLuid >= 0 &&
        columns.nativeDisplaySourceId >= 0 &&
        columns.nativeDisplayTargetAdapterLuid >= 0 &&
        columns.nativeDisplayTargetId >= 0 &&
        columns.nativeDisplayOutputTechnology >= 0 &&
        columns.nativeDisplayRotation >= 0 &&
        columns.nativeDisplayScaling >= 0 &&
        columns.nativeDisplayPathRefreshNumerator >= 0 &&
        columns.nativeDisplayPathRefreshDenominator >= 0 &&
        columns.nativeDisplaySignalValid >= 0 &&
        columns.nativeDisplaySignalPixelRateHz >= 0 &&
        columns.nativeDisplaySignalHSyncNumerator >= 0 &&
        columns.nativeDisplaySignalHSyncDenominator >= 0 &&
        columns.nativeDisplaySignalVSyncNumerator >= 0 &&
        columns.nativeDisplaySignalVSyncDenominator >= 0 &&
        columns.nativeDisplaySignalActiveWidth >= 0 &&
        columns.nativeDisplaySignalActiveHeight >= 0 &&
        columns.nativeDisplaySignalTotalWidth >= 0 &&
        columns.nativeDisplaySignalTotalHeight >= 0 &&
        columns.nativeDisplaySignalAdditionalInfoRaw >= 0 &&
        columns.nativeDisplaySignalScanLineOrdering >= 0;
    metrics.nativeRasterTelemetryAvailable =
        columns.nativeRasterSamplingRequested >= 0 &&
        columns.nativeRasterOpenResultValid >= 0 &&
        columns.nativeRasterOpenResult >= 0 &&
        columns.nativeRasterSourceValid >= 0 &&
        columns.nativeRasterVidPnSourceId >= 0 &&
        columns.nativeRasterBeforeQueryResultValid >= 0 &&
        columns.nativeRasterBeforeQueryResult >= 0 &&
        columns.nativeRasterBeforeQueryStartUs >= 0 &&
        columns.nativeRasterBeforeQueryEndUs >= 0 &&
        columns.nativeRasterBeforeInVerticalBlank >= 0 &&
        columns.nativeRasterBeforeScanLine >= 0 &&
        columns.nativeRasterAfterQueryResultValid >= 0 &&
        columns.nativeRasterAfterQueryResult >= 0 &&
        columns.nativeRasterAfterQueryStartUs >= 0 &&
        columns.nativeRasterAfterQueryEndUs >= 0 &&
        columns.nativeRasterAfterInVerticalBlank >= 0 &&
        columns.nativeRasterAfterScanLine >= 0;
    metrics.qpcCorrelationTelemetryAvailable =
        columns.latchQpcCorrelationValid >= 0 &&
        columns.latchQpcCorrelationReferenceTicks >= 0 &&
        columns.latchQpcCorrelationReferenceTimeUs >= 0 &&
        columns.latchQpcCorrelationSpanTicks >= 0;
    metrics.sessionAllowTearingTelemetryAvailable = columns.sessionAllowTearing >= 0;
    VrrSessionConfig capturedConfig;
    VrrSessionConfig simulatedConfig;
    VrrTimingParameters capturedParameters;
    QMap<int, QByteArray> capturedParameterValues;
    std::unique_ptr<VrrTimingController> referenceController;
    std::unique_ptr<VrrTimingController> simulatedController;
    bool capturedCanLatch = false;
    bool simulatedCanLatch = false;
    bool haveSimulatedSubmission = false;
    uint64_t priorSimulatedSubmissionUs = 0;
    bool haveRecordedSubmission = false;
    uint64_t priorRecordedSubmissionUs = 0;
    bool haveLatch = false;
    uint64_t priorLatchSubmission = 0;
    uint64_t priorPresentRefresh = 0;
    bool havePrePresentSyncAnchor = false;
    uint64_t priorPrePresentSyncRefreshSequence = 0;
    uint64_t priorPrePresentSyncSampleUs = 0;
    bool havePostPresentSyncAnchor = false;
    uint64_t priorPostPresentSyncRefreshSequence = 0;
    uint64_t priorPostPresentSyncSampleUs = 0;
    VrrRawQpcTranslationTracker rawSyncQpcTracker;
    VrrFreeRunningRefreshTracker counterfactualFreeRunningTracker;
    bool haveCounterfactualTransitionOrigin = false;
    uint64_t counterfactualTransitionOriginUs = 0;
    bool displayEpochNeedsPostObservation = false;
    uint64_t pendingMidframeWindowStateFlags = 0;
    bool expectedDeepBeforeStateKnown = false;
    bool expectedPresentCountBeforeValid = false;
    uint64_t expectedPresentCountBefore = 0;
    bool expectedFrameStatsBeforeValid = false;
    uint64_t expectedFrameStatsBeforePresentCount = 0;
    uint64_t expectedFrameStatsBeforeTimeUs = 0;
    uint64_t expectedFrameStatsBeforePresentRefreshSequence = 0;
    uint64_t expectedFrameStatsBeforeSyncRefreshSequence = 0;
    struct RasterSyncAnchor {
        uint64_t refreshSequence = 0;
        uint64_t timeUs = 0;
        uint64_t arrivalSequence = 0;
        bool postObservation = false;
    };
    std::deque<RasterSyncAnchor> rasterSyncAnchors;
    CadenceTracker observedCadenceTracker;
    CadenceTracker simulatedCadenceTracker;
    struct SubmissionBand {
        uint64_t id = 0;
        int rateHz = 0;
        uint64_t sourcePeriodUs = 0;
        uint64_t displayPeriodUs = 0;
        uint64_t recordedSubmissionUs = 0;
        uint64_t recordedPresentStartUs = 0;
        bool recordedLatched = false;
        bool simulatedSubmissionValid = false;
        uint64_t simulatedSubmissionUs = 0;
        uint64_t simulatedSourcePeriodUs = 0;
        bool simulatedLatched = false;
        uint64_t simulatedPresentTransportUs = 0;
        uint64_t simulatedDisplayTransitionDelayUs = 0;
        uint64_t simulatedSubmissionAdvanceAppliedUs = 0;
        bool exactPresentRefreshTimeValid = false;
        uint64_t exactPresentRefreshTimeUs = 0;
        bool recordedExactClassValid = false;
        VrrExactRefreshPhaseClass recordedExactClass =
            VrrExactRefreshPhaseClass::Unclassified;
        bool simulatedExactClassValid = false;
        VrrExactRefreshPhaseClass simulatedExactClass =
            VrrExactRefreshPhaseClass::Unclassified;
        bool recordedRasterValid = false;
        VrrRasterEnvelopeClass recordedRaster =
            VrrRasterEnvelopeClass::Unclassified;
        bool simulatedRasterValid = false;
        VrrRasterEnvelopeClass simulatedRaster =
            VrrRasterEnvelopeClass::Unclassified;
        bool recordedValidationCounted = false;
        bool simulatedValidationCounted = false;
        bool simulatedRefreshCompared = false;
    };
    std::deque<SubmissionBand> pendingSubmissionBands;

    QElapsedTimer timer;
    timer.start();
    QByteArray line;
    error.clear();
    while (reader.readLine(line, error)) {
        const QByteArray normalizedLine = line.trimmed();
        if (normalizedLine.startsWith("#vrr_trace_footer,")) {
            if (!line.endsWith('\n')) {
                std::fprintf(stderr,
                             "Trace ends in a partial clean-close footer\n");
                return 1;
            }
            if (!parseTraceFooter(
                    normalizedLine, metrics.traceFooter, error)) {
                std::fprintf(stderr, "Invalid trace footer: %s\n",
                             qPrintable(error));
                return 1;
            }
            continue;
        }
        if (metrics.traceFooter.present) {
            std::fprintf(stderr,
                         "Trace contains a data row after its clean-close footer\n");
            return 1;
        }
        decodedTraceHash.addData(normalizedLine);
        decodedTraceHash.addData(QByteArrayLiteral("\n"));
        const QList<QByteArray> fields = normalizedLine.split(',');
        if (fields.size() != columns.maximum() + 1) {
            std::fprintf(stderr, "Malformed trace row %llu\n",
                         static_cast<unsigned long long>(metrics.delivered + 2));
            return 1;
        }
        if (!validateTraceRowSyntax(traceHeader, fields, error)) {
            std::fprintf(
                stderr, "Malformed trace row %llu: %s\n",
                static_cast<unsigned long long>(metrics.delivered + 2),
                qPrintable(error));
            return 1;
        }
        const uint64_t traceSchema = unsignedField(fields,
                                                    columns.traceSchema);
        if (traceSchema != 3 && traceSchema != 4 && traceSchema != 5) {
            std::fprintf(stderr, "Unsupported trace schema on row %llu\n",
                         static_cast<unsigned long long>(metrics.delivered + 2));
            return 1;
        }
        if (traceSchema >= 4 && columns.spacingGuardFeedbackUs < 0) {
            std::fprintf(stderr,
                         "Schema 4 trace is missing spacing_guard_feedback_us\n");
            return 1;
        }
        if (scenario.mode == "worker" && traceSchema < 5) {
            std::fprintf(stderr,
                         "Worker simulation requires a schema 5 trace\n");
            return 2;
        }
        const int schema5StateColumns[] = {
            columns.readinessPhaseUs,
            columns.readinessDemandUs,
            columns.appliedReadinessReserveUs,
            columns.cadenceSampleCount,
            columns.rateCandidateSampleCount,
            columns.readinessSampleCount,
            columns.preparationSampleCount,
            columns.renderSchedulerSampleCount,
            columns.targetSchedulerSampleCount,
            columns.cleanSpacingFrames,
            columns.phaseErrorFrames,
            columns.readinessModelValid,
        };
        if (traceSchema >= 5 &&
                std::any_of(
                    std::begin(schema5StateColumns),
                    std::end(schema5StateColumns),
                    [](int column) { return column < 0; })) {
            std::fprintf(
                stderr,
                "Schema 5 trace is missing captured controller diagnostic state\n");
            return 1;
        }
        const int schema5TimingColumns[] = {
            columns.queueDepthAfter,
            columns.queueDiscontinuity,
            columns.submissionIdValid,
            columns.submissionId,
            columns.latchTimeUs,
            columns.latchSyncRefreshSequence,
            columns.deepTrace,
            columns.presentStartUs,
            columns.presenterSubmissionTimeUsed,
            columns.presentEndUs,
            columns.nativePresentTimingValid,
            columns.nativePresentStartUs,
            columns.nativePresentEndUs,
            columns.nativePresentCallUs,
            columns.presentCountBeforeValid,
            columns.presentCountBefore,
            columns.frameStatsBeforeValid,
            columns.frameStatsBeforePresentCount,
            columns.frameStatsBeforeTimeUs,
            columns.frameStatsBeforePresentRefreshSequence,
            columns.frameStatsBeforeSyncRefreshSequence,
            columns.gpuReadyTimingValid,
            columns.gpuReadyWaitStartUs,
            columns.gpuReadyTimeUs,
            columns.gpuReadyWaitUs,
            columns.decisionEndUs,
            columns.controllerCallUs,
            columns.staleCheckUs,
            columns.staleAgeUs,
            columns.renderWaitEntryUs,
            columns.renderWaitFinalUs,
            columns.renderWaitOvershootUs,
            columns.renderSchedulerDelayUs,
            columns.renderSchedulerDelayValid,
            columns.renderDeadlineAlreadyElapsed,
            columns.targetWaitEntryUs,
            columns.targetWaitFinalUs,
            columns.targetWaitOvershootUs,
            columns.targetSchedulerDelayUs,
            columns.targetSchedulerDelayValid,
            columns.targetDeadlineAlreadyElapsed,
            columns.spacingCheckUs,
            columns.presentationFloorUs,
            columns.correctionWaitStartUs,
            columns.correctionWaitEndUs,
            columns.spacingDeficitUs,
            columns.spacingCorrected,
            columns.completionQueueDepth,
            columns.terminalTimeUs,
        };
        if (traceSchema >= 5 &&
                std::any_of(
                    std::begin(schema5TimingColumns),
                    std::end(schema5TimingColumns),
                    [](int column) { return column < 0; })) {
            std::fprintf(
                stderr,
                "Schema 5 trace is missing captured lifecycle timing state\n");
            return 1;
        }
        if (metrics.traceSchema == 0) {
            metrics.traceSchema = traceSchema;
        }
        else if (metrics.traceSchema != traceSchema) {
            std::fprintf(stderr, "Trace schema changes within the capture\n");
            return 1;
        }

        ++metrics.delivered;
        const uint64_t arrivalSequence = unsignedField(
            fields, columns.arrivalSequence);
        const qint64 frameValue = signedField(fields, columns.frame);
        const uint64_t rtpTimestampValue = unsignedField(
            fields, columns.rtpTimestamp);
        if (frameValue < std::numeric_limits<int>::min() ||
                frameValue > std::numeric_limits<int>::max() ||
                rtpTimestampValue >
                    std::numeric_limits<uint32_t>::max()) {
            std::fprintf(
                stderr,
                "Trace row %llu has an out-of-range frame or RTP timestamp\n",
                static_cast<unsigned long long>(metrics.delivered + 1));
            return 1;
        }
        const int frameNumber = static_cast<int>(frameValue);
        const uint32_t rtpTimestamp =
            static_cast<uint32_t>(rtpTimestampValue);
        const uint64_t pacerArrivalUs = unsignedField(
            fields, columns.pacerArrivalUs);
        metrics.arrivalSequences.push_back(arrivalSequence);
        if (metrics.priorArrivalSequence != 0 &&
                arrivalSequence < metrics.priorArrivalSequence) {
            ++metrics.arrivalSequenceOutOfOrderTransitions;
        }
        metrics.priorArrivalSequence = arrivalSequence;
        if (metrics.firstArrivalUs == 0 || pacerArrivalUs < metrics.firstArrivalUs) {
            metrics.firstArrivalUs = pacerArrivalUs;
        }
        metrics.lastArrivalUs = std::max(metrics.lastArrivalUs, pacerArrivalUs);

        const QByteArray disposition = fields[columns.disposition];
        const QByteArray recordedTear = fields[columns.tearClassification];
        const bool rowAllowTearing = columns.sessionAllowTearing < 0 ||
            unsignedField(fields, columns.sessionAllowTearing) != 0;
        const bool deepTraceRow = optionalUnsignedField(
            fields, columns.deepTrace) != 0;
        metrics.deepTraceRows += deepTraceRow ? 1 : 0;
        const bool presented =
            unsignedField(fields, columns.presented) != 0;
        const bool cancelled =
            unsignedField(fields, columns.cancelled) != 0;
        const bool presenterSubmissionTimeDeclared =
            optionalUnsignedField(
                fields, columns.presenterSubmissionTimeValid) != 0;
        const uint64_t presenterSubmissionTimeUs =
            optionalUnsignedField(
                fields, columns.presenterSubmissionTimeUs);
        const bool presenterSubmissionTimeUsed =
            optionalUnsignedField(
                fields, columns.presenterSubmissionTimeUsed) != 0;
        const bool externalRebaseApplied =
            optionalUnsignedField(
                fields, columns.externalRebaseApplied) != 0;
        const uint64_t externalRebaseFlags =
            optionalUnsignedField(
                fields, columns.externalRebaseFlags);
        const uint64_t midframeWindowStateFlags =
            optionalUnsignedField(
                fields, columns.midframeWindowStateFlags);
        if (metrics.windowStateCauseTelemetryAvailable) {
            const uint64_t unknownFlags =
                (externalRebaseFlags |
                 midframeWindowStateFlags) &
                    ~kWindowStateChangeKnownMask;
            metrics.externalRebaseFlagRelationshipMismatchRows +=
                externalRebaseApplied !=
                    (externalRebaseFlags != 0) ? 1 : 0;
            metrics.unknownWindowStateFlagRows +=
                unknownFlags != 0 ? 1 : 0;
            if (externalRebaseFlags != 0) {
                ++metrics.externalRebaseFlagValues[
                    QByteArray::number(externalRebaseFlags)];
            }
            if (midframeWindowStateFlags != 0) {
                ++metrics.midframeWindowStateFlagValues[
                    QByteArray::number(
                        midframeWindowStateFlags)];
            }
            const bool decisionValidForEpoch =
                unsignedField(
                    fields, columns.decisionValid) != 0;
            if (decisionValidForEpoch &&
                    pendingMidframeWindowStateFlags != 0) {
                const bool carriedForward =
                    externalRebaseApplied &&
                    (externalRebaseFlags &
                        pendingMidframeWindowStateFlags) ==
                            pendingMidframeWindowStateFlags;
                metrics.externalRebaseFlagCarryForwardMismatchRows +=
                    carriedForward ? 0 : 1;
                pendingMidframeWindowStateFlags = 0;
            }
            pendingMidframeWindowStateFlags |=
                midframeWindowStateFlags;
            const bool midframeDisplayEpochInterrupt =
                (midframeWindowStateFlags &
                    kWindowStateChangeDisplayEpochMask) != 0;
            metrics.midframeDisplayEpochInterruptRows +=
                midframeDisplayEpochInterrupt ? 1 : 0;
            metrics.midframeDisplayEpochRelationshipMismatchRows +=
                midframeDisplayEpochInterrupt &&
                    (presented || !cancelled ||
                     (disposition != "interrupted" &&
                      disposition != "preparation_failed")) ? 1 : 0;
        }
        if (externalRebaseApplied) {
            // Do not carry a pre-transition display clock or cached native
            // state into the first frame of a new window/display epoch.
            rasterSyncAnchors.clear();
            pendingSubmissionBands.clear();
            havePrePresentSyncAnchor = false;
            havePostPresentSyncAnchor = false;
            rawSyncQpcTracker.reset();
            counterfactualFreeRunningTracker.reset();
            haveCounterfactualTransitionOrigin = false;
            haveLatch = false;
            displayEpochNeedsPostObservation = true;
            expectedDeepBeforeStateKnown = false;
            expectedPresentCountBeforeValid = false;
            expectedFrameStatsBeforeValid = false;
            metrics.haveNativeRenderAdapterLuidReference = false;
            metrics.haveNativeDisplayTimingReference = false;
            metrics.haveNativeDxgiCapabilityReference = false;
        }
        const bool submissionIdValid = optionalUnsignedField(
            fields, columns.submissionIdValid) != 0;
        const bool latchSampleValid = unsignedField(
            fields, columns.latchValid) != 0;
        const bool nativePresentTimingDeclared =
            optionalUnsignedField(
                fields, columns.nativePresentTimingValid) != 0;
        const uint64_t nativePresentStartUs = optionalUnsignedField(
            fields, columns.nativePresentStartUs);
        const uint64_t nativePresentEndUs = optionalUnsignedField(
            fields, columns.nativePresentEndUs);
        const bool presentCountBeforeDeclared =
            optionalUnsignedField(
                fields, columns.presentCountBeforeValid) != 0;
        const bool frameStatsBeforeDeclared =
            optionalUnsignedField(
                fields, columns.frameStatsBeforeValid) != 0;
        const bool gpuReadyAttempted =
            optionalUnsignedField(
                fields, columns.gpuReadyAttempted) != 0;
        const bool gpuReadySignalResultDeclared =
            optionalUnsignedField(
                fields, columns.gpuReadySignalResultValid) != 0;
        const int64_t gpuReadySignalResult =
            optionalSignedField(
                fields, columns.gpuReadySignalResult);
        const bool gpuReadySetEventResultDeclared =
            optionalUnsignedField(
                fields, columns.gpuReadySetEventResultValid) != 0;
        const int64_t gpuReadySetEventResult =
            optionalSignedField(
                fields, columns.gpuReadySetEventResult);
        const bool gpuReadyWaitResultDeclared =
            optionalUnsignedField(
                fields, columns.gpuReadyWaitResultValid) != 0;
        const uint64_t gpuReadyWaitResult =
            optionalUnsignedField(
                fields, columns.gpuReadyWaitResult);
        const bool gpuReadySignalSucceeded =
            gpuReadyAttempted &&
            gpuReadySignalResultDeclared &&
            gpuReadySignalResult >= 0;
        const bool gpuReadySetEventSucceeded =
            gpuReadySignalSucceeded &&
            gpuReadySetEventResultDeclared &&
            gpuReadySetEventResult >= 0;
        const bool gpuReadyTimingDeclared =
            optionalUnsignedField(
                fields, columns.gpuReadyTimingValid) != 0;
        const uint64_t gpuReadySignalStartUs =
            optionalUnsignedField(
                fields, columns.gpuReadySignalStartUs);
        const uint64_t gpuReadySignalEndUs =
            optionalUnsignedField(
                fields, columns.gpuReadySignalEndUs);
        const uint64_t gpuReadyFlushStartUs =
            optionalUnsignedField(
                fields, columns.gpuReadyFlushStartUs);
        const uint64_t gpuReadyFlushEndUs =
            optionalUnsignedField(
                fields, columns.gpuReadyFlushEndUs);
        const uint64_t gpuReadySetEventStartUs =
            optionalUnsignedField(
                fields, columns.gpuReadySetEventStartUs);
        const uint64_t gpuReadySetEventEndUs =
            optionalUnsignedField(
                fields, columns.gpuReadySetEventEndUs);
        const uint64_t gpuReadyPollStartUs =
            optionalUnsignedField(
                fields, columns.gpuReadyPollStartUs);
        const uint64_t gpuReadyPollEndUs =
            optionalUnsignedField(
                fields, columns.gpuReadyPollEndUs);
        const uint64_t gpuReadyFenceValue =
            optionalUnsignedField(
                fields, columns.gpuReadyFenceValue);
        const uint64_t gpuReadyPollCompletedValue =
            optionalUnsignedField(
                fields, columns.gpuReadyPollCompletedValue);
        const bool gpuReadyCompletedBeforeWait =
            optionalUnsignedField(
                fields, columns.gpuReadyCompletedBeforeWait) != 0;
        const uint64_t gpuReadyCompletionLowerBoundUs =
            optionalUnsignedField(
                fields, columns.gpuReadyCompletionLowerBoundUs);
        const uint64_t gpuReadyCompletionUpperBoundUs =
            optionalUnsignedField(
                fields, columns.gpuReadyCompletionUpperBoundUs);
        const uint64_t gpuReadyCompletionUncertaintyUs =
            optionalUnsignedField(
                fields, columns.gpuReadyCompletionUncertaintyUs);
        const uint64_t gpuReadyWaitStartUs =
            optionalUnsignedField(
                fields, columns.gpuReadyWaitStartUs);
        const uint64_t gpuReadyTimeUs =
            optionalUnsignedField(
                fields, columns.gpuReadyTimeUs);
        const uint64_t gpuReadyWaitUs =
            optionalUnsignedField(
                fields, columns.gpuReadyWaitUs);
        const uint64_t gpuReadinessAppliedUs =
            optionalUnsignedField(
                fields, columns.gpuReadinessAppliedUs);
        const bool nativeBackendDeclared =
            optionalUnsignedField(
                fields, columns.nativeBackendValid) != 0;
        const uint64_t nativeBackend = optionalUnsignedField(
            fields, columns.nativeBackend);
        // Vulkan's renderer reports image-local libplacebo completion polling
        // through the shared readiness timing fields. The D3D11 signal/event
        // and fence-value fields remain intentionally unavailable on those
        // rows. Native backend identity is already captured on every Vulkan
        // submission (including the neutral submit used for cancellation).
        const bool gpuReadyVulkanPoll =
            nativeBackendDeclared && nativeBackend == kNativeBackendVulkan;
        const bool nativePresentResultDeclared =
            optionalUnsignedField(
                fields, columns.nativePresentResultValid) != 0;
        const int64_t nativePresentResult = optionalSignedField(
            fields, columns.nativePresentResult);
        const bool nativePresentParametersDeclared =
            optionalUnsignedField(
                fields, columns.nativePresentParametersValid) != 0;
        const uint64_t nativePresentSyncInterval =
            optionalUnsignedField(
                fields, columns.nativePresentSyncInterval);
        const uint64_t nativePresentFlags = optionalUnsignedField(
            fields, columns.nativePresentFlags);
        const bool nativeVrrStateDeclared = optionalUnsignedField(
            fields, columns.nativeVrrStateValid) != 0;
        const bool nativeTearingSupported = optionalUnsignedField(
            fields, columns.nativeTearingSupported) != 0;
        const bool nativeBorderlessFlipModel = optionalUnsignedField(
            fields, columns.nativeBorderlessFlipModel) != 0;
        const bool nativeSameGpuOutput = optionalUnsignedField(
            fields, columns.nativeSameGpuOutput) != 0;
        const bool nativeRenderAdapterLuidDeclared =
            optionalUnsignedField(
                fields, columns.nativeRenderAdapterLuidValid) != 0;
        const uint64_t nativeRenderAdapterLuid =
            optionalUnsignedField(
                fields, columns.nativeRenderAdapterLuid);
        const bool nativeSwapChainAllowsTearing =
            optionalUnsignedField(
                fields, columns.nativeSwapChainAllowsTearing) != 0;
        const bool nativeTearingFeatureQueryResultDeclared =
            optionalUnsignedField(
                fields,
                columns.nativeTearingFeatureQueryResultValid) != 0;
        const int64_t nativeTearingFeatureQueryResult =
            optionalSignedField(
                fields, columns.nativeTearingFeatureQueryResult);
        const bool nativeTearingFeatureAllowsTearing =
            optionalUnsignedField(
                fields,
                columns.nativeTearingFeatureAllowsTearing) != 0;
        const bool nativeSwapChainDescQueryResultDeclared =
            optionalUnsignedField(
                fields,
                columns.nativeSwapChainDescQueryResultValid) != 0;
        const int64_t nativeSwapChainDescQueryResult =
            optionalSignedField(
                fields, columns.nativeSwapChainDescQueryResult);
        const uint64_t nativeSwapChainFlags =
            optionalUnsignedField(
                fields, columns.nativeSwapChainFlags);
        const uint64_t nativeSwapChainSwapEffect =
            optionalUnsignedField(
                fields, columns.nativeSwapChainSwapEffect);
        const bool nativeFullscreenStateQueryResultDeclared =
            optionalUnsignedField(
                fields,
                columns.nativeFullscreenStateQueryResultValid) != 0;
        const int64_t nativeFullscreenStateQueryResult =
            optionalSignedField(
                fields, columns.nativeFullscreenStateQueryResult);
        const bool nativeFullscreenExclusive =
            optionalUnsignedField(
                fields, columns.nativeFullscreenExclusive) != 0;
        const uint64_t nativeWindowFlags =
            optionalUnsignedField(
                fields, columns.nativeWindowFlags);
        const bool nativePresentReadyAvailable =
            optionalUnsignedField(
                fields, columns.nativePresentReadyAvailable) != 0;
        const bool nativeForegroundWindow = optionalUnsignedField(
            fields, columns.nativeForegroundWindow) != 0;
        const uint64_t nativeVrrFallbackReason =
            optionalUnsignedField(
                fields, columns.nativeVrrFallbackReason);
        const uint64_t nativeDesktopMonitorCount =
            optionalUnsignedField(
                fields, columns.nativeDesktopMonitorCount);
        const bool nativeVblankVirtualizationProbeComplete =
            optionalUnsignedField(
                fields,
                columns.nativeVblankVirtualizationProbeComplete) != 0;
        const bool nativeVblankVirtualizationCallAvailable =
            optionalUnsignedField(
                fields,
                columns.nativeVblankVirtualizationCallAvailable) != 0;
        const bool nativeVblankVirtualizationResultDeclared =
            optionalUnsignedField(
                fields,
                columns.nativeVblankVirtualizationResultValid) != 0;
        const int64_t nativeVblankVirtualizationResult =
            optionalSignedField(
                fields,
                columns.nativeVblankVirtualizationResult);
        const bool nativeVblankVirtualizationDisabled =
            optionalUnsignedField(
                fields,
                columns.nativeVblankVirtualizationDisabled) != 0;
        const bool nativeDisplayConfigQueryResultDeclared =
            optionalUnsignedField(
                fields,
                columns.nativeDisplayConfigQueryResultValid) != 0;
        const uint64_t nativeDisplayConfigQueryResult =
            optionalUnsignedField(
                fields, columns.nativeDisplayConfigQueryResult);
        const bool nativeDisplayPathDeclared =
            optionalUnsignedField(
                fields, columns.nativeDisplayPathValid) != 0;
        const uint64_t nativeDisplayPathFlags =
            optionalUnsignedField(
                fields, columns.nativeDisplayPathFlags);
        const bool nativeDisplayTargetAvailable =
            optionalUnsignedField(
                fields, columns.nativeDisplayTargetAvailable) != 0;
        const uint64_t nativeDisplaySourceAdapterLuid =
            optionalUnsignedField(
                fields, columns.nativeDisplaySourceAdapterLuid);
        const uint64_t nativeDisplaySourceId =
            optionalUnsignedField(
                fields, columns.nativeDisplaySourceId);
        const uint64_t nativeDisplayTargetAdapterLuid =
            optionalUnsignedField(
                fields, columns.nativeDisplayTargetAdapterLuid);
        const uint64_t nativeDisplayTargetId =
            optionalUnsignedField(
                fields, columns.nativeDisplayTargetId);
        const uint64_t nativeDisplayOutputTechnology =
            optionalUnsignedField(
                fields, columns.nativeDisplayOutputTechnology);
        const uint64_t nativeDisplayRotation =
            optionalUnsignedField(
                fields, columns.nativeDisplayRotation);
        const uint64_t nativeDisplayScaling =
            optionalUnsignedField(
                fields, columns.nativeDisplayScaling);
        const uint64_t nativeDisplayPathRefreshNumerator =
            optionalUnsignedField(
                fields,
                columns.nativeDisplayPathRefreshNumerator);
        const uint64_t nativeDisplayPathRefreshDenominator =
            optionalUnsignedField(
                fields,
                columns.nativeDisplayPathRefreshDenominator);
        const bool nativeDisplaySignalDeclared =
            optionalUnsignedField(
                fields, columns.nativeDisplaySignalValid) != 0;
        const uint64_t nativeDisplaySignalPixelRateHz =
            optionalUnsignedField(
                fields, columns.nativeDisplaySignalPixelRateHz);
        const uint64_t nativeDisplaySignalHSyncNumerator =
            optionalUnsignedField(
                fields,
                columns.nativeDisplaySignalHSyncNumerator);
        const uint64_t nativeDisplaySignalHSyncDenominator =
            optionalUnsignedField(
                fields,
                columns.nativeDisplaySignalHSyncDenominator);
        const uint64_t nativeDisplaySignalVSyncNumerator =
            optionalUnsignedField(
                fields,
                columns.nativeDisplaySignalVSyncNumerator);
        const uint64_t nativeDisplaySignalVSyncDenominator =
            optionalUnsignedField(
                fields,
                columns.nativeDisplaySignalVSyncDenominator);
        const uint64_t nativeDisplaySignalActiveWidth =
            optionalUnsignedField(
                fields, columns.nativeDisplaySignalActiveWidth);
        const uint64_t nativeDisplaySignalActiveHeight =
            optionalUnsignedField(
                fields, columns.nativeDisplaySignalActiveHeight);
        const uint64_t nativeDisplaySignalTotalWidth =
            optionalUnsignedField(
                fields, columns.nativeDisplaySignalTotalWidth);
        const uint64_t nativeDisplaySignalTotalHeight =
            optionalUnsignedField(
                fields, columns.nativeDisplaySignalTotalHeight);
        const uint64_t nativeDisplaySignalAdditionalInfoRaw =
            optionalUnsignedField(
                fields,
                columns.nativeDisplaySignalAdditionalInfoRaw);
        const uint64_t nativeDisplaySignalScanLineOrdering =
            optionalUnsignedField(
                fields,
                columns.nativeDisplaySignalScanLineOrdering);
        const bool nativeRasterSamplingRequested =
            optionalUnsignedField(
                fields, columns.nativeRasterSamplingRequested) != 0;
        const bool nativeRasterOpenResultDeclared =
            optionalUnsignedField(
                fields, columns.nativeRasterOpenResultValid) != 0;
        const int64_t nativeRasterOpenResult =
            optionalSignedField(
                fields, columns.nativeRasterOpenResult);
        const bool nativeRasterSourceDeclared =
            optionalUnsignedField(
                fields, columns.nativeRasterSourceValid) != 0;
        const uint64_t nativeRasterVidPnSourceId =
            optionalUnsignedField(
                fields, columns.nativeRasterVidPnSourceId);
        const bool nativeRasterBeforeQueryResultDeclared =
            optionalUnsignedField(
                fields,
                columns.nativeRasterBeforeQueryResultValid) != 0;
        const int64_t nativeRasterBeforeQueryResult =
            optionalSignedField(
                fields, columns.nativeRasterBeforeQueryResult);
        const uint64_t nativeRasterBeforeQueryStartUs =
            optionalUnsignedField(
                fields, columns.nativeRasterBeforeQueryStartUs);
        const uint64_t nativeRasterBeforeQueryEndUs =
            optionalUnsignedField(
                fields, columns.nativeRasterBeforeQueryEndUs);
        const bool nativeRasterBeforeInVerticalBlank =
            optionalUnsignedField(
                fields,
                columns.nativeRasterBeforeInVerticalBlank) != 0;
        const uint64_t nativeRasterBeforeScanLine =
            optionalUnsignedField(
                fields, columns.nativeRasterBeforeScanLine);
        const bool nativeRasterAfterQueryResultDeclared =
            optionalUnsignedField(
                fields,
                columns.nativeRasterAfterQueryResultValid) != 0;
        const int64_t nativeRasterAfterQueryResult =
            optionalSignedField(
                fields, columns.nativeRasterAfterQueryResult);
        const uint64_t nativeRasterAfterQueryStartUs =
            optionalUnsignedField(
                fields, columns.nativeRasterAfterQueryStartUs);
        const uint64_t nativeRasterAfterQueryEndUs =
            optionalUnsignedField(
                fields, columns.nativeRasterAfterQueryEndUs);
        const bool nativeRasterAfterInVerticalBlank =
            optionalUnsignedField(
                fields,
                columns.nativeRasterAfterInVerticalBlank) != 0;
        const uint64_t nativeRasterAfterScanLine =
            optionalUnsignedField(
                fields, columns.nativeRasterAfterScanLine);
        uint64_t nativeDisplaySignalPeriodPs = 0;
        const bool nativeDisplaySignalPeriodValid =
            vrrPeriodPicosecondsFromRefreshRational(
                nativeDisplaySignalVSyncNumerator,
                nativeDisplaySignalVSyncDenominator,
                nativeDisplaySignalPeriodPs);
        uint64_t nativeDisplaySignalActiveScanoutPs = 0;
        const bool nativeDisplaySignalActiveScanoutPsValid =
            vrrActiveScanoutPicosecondsFromSignal(
                nativeDisplaySignalVSyncNumerator,
                nativeDisplaySignalVSyncDenominator,
                nativeDisplaySignalActiveWidth,
                nativeDisplaySignalActiveHeight,
                nativeDisplaySignalTotalWidth,
                nativeDisplaySignalTotalHeight,
                nativeDisplaySignalActiveScanoutPs);
        uint64_t nativeDisplaySignalVerticalActivePs = 0;
        const bool nativeDisplaySignalVerticalActivePsValid =
            vrrActiveScanoutPicosecondsFromSignal(
                nativeDisplaySignalVSyncNumerator,
                nativeDisplaySignalVSyncDenominator,
                nativeDisplaySignalTotalWidth,
                nativeDisplaySignalActiveHeight,
                nativeDisplaySignalTotalWidth,
                nativeDisplaySignalTotalHeight,
                nativeDisplaySignalVerticalActivePs);
        const VrrDisplaySignalConsistency
            nativeDisplaySignalConsistency =
                evaluateVrrDisplaySignalConsistency(
                    nativeDisplaySignalPixelRateHz,
                    nativeDisplaySignalHSyncNumerator,
                    nativeDisplaySignalHSyncDenominator,
                    nativeDisplaySignalVSyncNumerator,
                    nativeDisplaySignalVSyncDenominator,
                    nativeDisplaySignalTotalWidth,
                    nativeDisplaySignalTotalHeight,
                    kDisplaySignalConsistencyTolerancePpm);
        VrrReplayDisplayParameters rasterDisplayParameters =
            scenario.display;
        if (rasterDisplayParameters.scanoutPeriodPs == 0 &&
                nativeDisplaySignalPeriodValid) {
            // Use the exact captured physical rational for both raster
            // classification and the propagated counterfactual. Integer
            // microsecond periods otherwise accumulate several microseconds
            // of phase error over even the bounded eight-refresh anchor age.
            rasterDisplayParameters.scanoutPeriodPs =
                nativeDisplaySignalPeriodPs;
        }
        if (rasterDisplayParameters.activeScanoutPs == 0 &&
                nativeDisplaySignalActiveScanoutPsValid) {
            rasterDisplayParameters.activeScanoutPs =
                nativeDisplaySignalActiveScanoutPs;
        }
        const bool rowLatchedPresent = optionalUnsignedField(
            fields, columns.latchedPresent) != 0;
        const bool submissionIdQueryResultDeclared =
            optionalUnsignedField(
                fields, columns.submissionIdQueryResultValid) != 0;
        const int64_t submissionIdQueryResult = optionalSignedField(
            fields, columns.submissionIdQueryResult);
        const uint64_t submissionIdQueryStartUs =
            optionalUnsignedField(
                fields, columns.submissionIdQueryStartUs);
        const uint64_t submissionIdQueryEndUs =
            optionalUnsignedField(
                fields, columns.submissionIdQueryEndUs);
        const bool frameStatsQueryResultDeclared =
            optionalUnsignedField(
                fields, columns.frameStatsQueryResultValid) != 0;
        const int64_t frameStatsQueryResult = optionalSignedField(
            fields, columns.frameStatsQueryResult);
        const uint64_t frameStatsQueryStartUs =
            optionalUnsignedField(
                fields, columns.frameStatsQueryStartUs);
        const uint64_t frameStatsQueryEndUs =
            optionalUnsignedField(
                fields, columns.frameStatsQueryEndUs);
        const bool rawSyncQpcDeclared =
            optionalUnsignedField(
                fields, columns.latchRawSyncQpcValid) != 0;
        const uint64_t rawSyncQpcTicks = optionalUnsignedField(
            fields, columns.latchRawSyncQpcTicks);
        const uint64_t rowRawSyncQpcFrequency = optionalUnsignedField(
            fields, columns.latchRawSyncQpcFrequency);
        const bool qpcCorrelationDeclared =
            optionalUnsignedField(
                fields, columns.latchQpcCorrelationValid) != 0;
        const uint64_t qpcCorrelationReferenceTicks =
            optionalUnsignedField(
                fields, columns.latchQpcCorrelationReferenceTicks);
        const uint64_t qpcCorrelationReferenceTimeUs =
            optionalUnsignedField(
                fields, columns.latchQpcCorrelationReferenceTimeUs);
        const uint64_t qpcCorrelationSpanTicks =
            optionalUnsignedField(
                fields, columns.latchQpcCorrelationSpanTicks);
        const uint64_t rowLatchTimeUs = optionalUnsignedField(
            fields, columns.latchTimeUs);
        metrics.validityPayloadMismatches +=
            !submissionIdValid &&
                optionalUnsignedField(fields, columns.submissionId) != 0 ?
                    1 : 0;
        metrics.validityPayloadMismatches +=
            !latchSampleValid &&
                (unsignedField(fields, columns.latchSubmissionId) != 0 ||
                 optionalUnsignedField(fields, columns.latchTimeUs) != 0 ||
                 unsignedField(
                     fields, columns.latchPresentRefreshSequence) != 0 ||
                 optionalUnsignedField(
                     fields, columns.latchSyncRefreshSequence) != 0) ?
                    1 : 0;
        metrics.validityPayloadMismatches +=
            !nativePresentTimingDeclared &&
                (optionalUnsignedField(
                     fields, columns.nativePresentStartUs) != 0 ||
                 optionalUnsignedField(
                     fields, columns.nativePresentEndUs) != 0 ||
                 unsignedField(fields, columns.nativePresentCallUs) != 0) ?
                    1 : 0;
        metrics.validityPayloadMismatches +=
            !presentCountBeforeDeclared &&
                optionalUnsignedField(
                    fields, columns.presentCountBefore) != 0 ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !frameStatsBeforeDeclared &&
                (optionalUnsignedField(
                     fields, columns.frameStatsBeforePresentCount) != 0 ||
                 optionalUnsignedField(
                     fields, columns.frameStatsBeforeTimeUs) != 0 ||
                 optionalUnsignedField(
                     fields,
                     columns.frameStatsBeforePresentRefreshSequence) != 0 ||
                 optionalUnsignedField(
                     fields,
                     columns.frameStatsBeforeSyncRefreshSequence) != 0) ?
                    1 : 0;
        if (metrics.gpuReadyNativeResultTelemetryAvailable) {
            metrics.validityPayloadMismatches +=
                !gpuReadySignalResultDeclared &&
                    gpuReadySignalResult != 0 ? 1 : 0;
            metrics.validityPayloadMismatches +=
                !gpuReadySetEventResultDeclared &&
                    gpuReadySetEventResult != 0 ? 1 : 0;
            metrics.validityPayloadMismatches +=
                !gpuReadyWaitResultDeclared &&
                    gpuReadyWaitResult != 0 ? 1 : 0;
            metrics.validityPayloadMismatches +=
                !gpuReadyAttempted &&
                    (gpuReadySignalStartUs != 0 ||
                     gpuReadySignalEndUs != 0 ||
                     gpuReadyFlushStartUs != 0 ||
                     gpuReadyFlushEndUs != 0 ||
                     gpuReadySetEventStartUs != 0 ||
                     gpuReadySetEventEndUs != 0 ||
                     gpuReadyPollStartUs != 0 ||
                     gpuReadyPollEndUs != 0 ||
                     gpuReadyFenceValue != 0 ||
                     gpuReadyPollCompletedValue != 0 ||
                     gpuReadyCompletedBeforeWait ||
                     gpuReadyCompletionLowerBoundUs != 0 ||
                     gpuReadyCompletionUpperBoundUs != 0 ||
                     gpuReadyCompletionUncertaintyUs != 0 ||
                     gpuReadyWaitStartUs != 0 ||
                     gpuReadyTimeUs != 0 ||
                     gpuReadyWaitUs != 0) ? 1 : 0;
            metrics.validityPayloadMismatches +=
                !gpuReadyWaitResultDeclared &&
                    (gpuReadyPollStartUs != 0 ||
                     gpuReadyPollEndUs != 0 ||
                     gpuReadyPollCompletedValue != 0 ||
                     gpuReadyCompletedBeforeWait ||
                     gpuReadyWaitStartUs != 0 ||
                     gpuReadyTimeUs != 0) ? 1 : 0;
            metrics.validityPayloadMismatches +=
                !gpuReadyTimingDeclared &&
                    (gpuReadyCompletionLowerBoundUs != 0 ||
                     gpuReadyCompletionUpperBoundUs != 0 ||
                     gpuReadyCompletionUncertaintyUs != 0 ||
                     gpuReadyWaitUs != 0) ? 1 : 0;
        }
        else {
            metrics.validityPayloadMismatches +=
                !gpuReadyTimingDeclared &&
                    (gpuReadySignalStartUs != 0 ||
                     gpuReadySignalEndUs != 0 ||
                     gpuReadyFlushStartUs != 0 ||
                     gpuReadyFlushEndUs != 0 ||
                     gpuReadySetEventStartUs != 0 ||
                     gpuReadySetEventEndUs != 0 ||
                     gpuReadyPollStartUs != 0 ||
                     gpuReadyPollEndUs != 0 ||
                     gpuReadyFenceValue != 0 ||
                     gpuReadyPollCompletedValue != 0 ||
                     gpuReadyCompletedBeforeWait ||
                     gpuReadyCompletionLowerBoundUs != 0 ||
                     gpuReadyCompletionUpperBoundUs != 0 ||
                     gpuReadyCompletionUncertaintyUs != 0 ||
                     gpuReadyWaitStartUs != 0 ||
                     gpuReadyTimeUs != 0 ||
                     gpuReadyWaitUs != 0) ? 1 : 0;
        }
        if (metrics.presenterSubmissionTimingTelemetryAvailable) {
            metrics.validityPayloadMismatches +=
                !presenterSubmissionTimeDeclared &&
                    presenterSubmissionTimeUs != 0 ? 1 : 0;
        }
        if (metrics.gpuReadyNativeResultTelemetryAvailable) {
            metrics.gpuReadyAttemptedRows +=
                gpuReadyAttempted ? 1 : 0;
            bool gpuReadyNativeSuccess = false;
            if (gpuReadyVulkanPoll) {
                // libplacebo texture polling has no D3D11 HRESULT/event/fence
                // stages. Its result is the shared wait result (0 means the
                // output texture became idle; nonzero means cancellation,
                // timeout, or a failed GPU observation).
                const bool noD3dStages =
                    !gpuReadySignalResultDeclared &&
                    !gpuReadySetEventResultDeclared &&
                    gpuReadySignalStartUs == 0 &&
                    gpuReadySignalEndUs == 0 &&
                    gpuReadyFlushStartUs == 0 &&
                    gpuReadyFlushEndUs == 0 &&
                    gpuReadySetEventStartUs == 0 &&
                    gpuReadySetEventEndUs == 0 &&
                    gpuReadyFenceValue == 0 &&
                    gpuReadyPollCompletedValue == 0 &&
                    !gpuReadyCompletedBeforeWait;
                const bool pollTimingPresent =
                    gpuReadyPollStartUs != 0 || gpuReadyPollEndUs != 0 ||
                    gpuReadyWaitStartUs != 0 || gpuReadyTimeUs != 0;
                const bool pollObservationValid =
                    gpuReadyPollStartUs != 0 &&
                    gpuReadyPollEndUs >= gpuReadyPollStartUs &&
                    gpuReadyWaitStartUs == gpuReadyPollStartUs &&
                    gpuReadyTimeUs >= gpuReadyWaitStartUs;
                const bool pollTimingValid =
                    !gpuReadyAttempted ? !pollTimingPresent &&
                        !gpuReadyWaitResultDeclared &&
                        !gpuReadyTimingDeclared :
                    gpuReadyWaitResultDeclared && pollObservationValid &&
                    (gpuReadyWaitResult == 0 ||
                     gpuReadyWaitResult == 1 ||
                     gpuReadyWaitResult == 2) &&
                    ((gpuReadyWaitResult == 0 && gpuReadyTimingDeclared) ||
                     (gpuReadyWaitResult != 0 && !gpuReadyTimingDeclared));
                gpuReadyNativeSuccess =
                    gpuReadyAttempted && pollTimingValid &&
                    gpuReadyWaitResult == 0 && gpuReadyTimingDeclared;
                metrics.gpuReadyNativeResultRelationshipMismatchRows +=
                    (noD3dStages && pollTimingValid) ? 0 : 1;
            }
            else {
                const VrrGpuReadyOperationAudit gpuReadyOperation =
                    evaluateVrrGpuReadyOperation(
                        gpuReadyAttempted,
                        gpuReadySignalResultDeclared, gpuReadySignalResult,
                        gpuReadySetEventResultDeclared, gpuReadySetEventResult,
                        gpuReadyWaitResultDeclared, gpuReadyWaitResult,
                        gpuReadyTimingDeclared,
                        gpuReadySignalStartUs, gpuReadyFenceValue);
                metrics.gpuReadyNativeResultRelationshipMismatchRows +=
                    gpuReadyOperation.relationshipValid ? 0 : 1;
                gpuReadyNativeSuccess = gpuReadyOperation.exactSuccess;
            }
            if (gpuReadySignalResultDeclared) {
                ++metrics.gpuReadySignalResults[
                    QByteArray::number(gpuReadySignalResult)];
            }
            if (gpuReadySetEventResultDeclared) {
                ++metrics.gpuReadySetEventResults[
                    QByteArray::number(gpuReadySetEventResult)];
            }
            if (gpuReadyWaitResultDeclared) {
                ++metrics.gpuReadyWaitResults[
                    QByteArray::number(gpuReadyWaitResult)];
            }
            metrics.presentedGpuReadyNativeSuccessRows +=
                presented && gpuReadyNativeSuccess ? 1 : 0;
        }
        metrics.validityPayloadMismatches +=
            !nativeBackendDeclared &&
                nativeBackend != 0 ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativePresentResultDeclared &&
                nativePresentResult != 0 ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativePresentParametersDeclared &&
                (nativePresentSyncInterval != 0 ||
                 nativePresentFlags != 0) ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativeVrrStateDeclared &&
                (nativeTearingSupported ||
                 nativeBorderlessFlipModel ||
                 nativeSameGpuOutput ||
                 nativeSwapChainAllowsTearing ||
                 nativePresentReadyAvailable ||
                 nativeForegroundWindow ||
                  nativeVrrFallbackReason != 0 ||
                  nativeDesktopMonitorCount != 0) ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativeRenderAdapterLuidDeclared &&
                nativeRenderAdapterLuid != 0 ? 1 : 0;
        if (metrics.nativeDxgiCapabilityTelemetryAvailable) {
            metrics.validityPayloadMismatches +=
                !nativeTearingFeatureQueryResultDeclared &&
                    (nativeTearingFeatureQueryResult != 0 ||
                     nativeTearingFeatureAllowsTearing) ? 1 : 0;
            metrics.validityPayloadMismatches +=
                !nativeSwapChainDescQueryResultDeclared &&
                    (nativeSwapChainDescQueryResult != 0 ||
                     nativeSwapChainFlags != 0 ||
                     nativeSwapChainSwapEffect != 0) ? 1 : 0;
            metrics.validityPayloadMismatches +=
                !nativeFullscreenStateQueryResultDeclared &&
                    (nativeFullscreenStateQueryResult != 0 ||
                     nativeFullscreenExclusive) ? 1 : 0;
        }
        metrics.validityPayloadMismatches +=
            !nativeVblankVirtualizationResultDeclared &&
                nativeVblankVirtualizationResult != 0 ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativeDisplayConfigQueryResultDeclared &&
                nativeDisplayConfigQueryResult != 0 ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativeDisplayPathDeclared &&
                (nativeDisplayPathFlags != 0 ||
                 nativeDisplayTargetAvailable ||
                 nativeDisplaySourceAdapterLuid != 0 ||
                 nativeDisplaySourceId != 0 ||
                 nativeDisplayTargetAdapterLuid != 0 ||
                 nativeDisplayTargetId != 0 ||
                 nativeDisplayOutputTechnology != 0 ||
                 nativeDisplayRotation != 0 ||
                 nativeDisplayScaling != 0 ||
                 nativeDisplayPathRefreshNumerator != 0 ||
                 nativeDisplayPathRefreshDenominator != 0) ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativeDisplaySignalDeclared &&
                (nativeDisplaySignalPixelRateHz != 0 ||
                 nativeDisplaySignalHSyncNumerator != 0 ||
                 nativeDisplaySignalHSyncDenominator != 0 ||
                 nativeDisplaySignalVSyncNumerator != 0 ||
                 nativeDisplaySignalVSyncDenominator != 0 ||
                 nativeDisplaySignalActiveWidth != 0 ||
                 nativeDisplaySignalActiveHeight != 0 ||
                 nativeDisplaySignalTotalWidth != 0 ||
                 nativeDisplaySignalTotalHeight != 0 ||
                 nativeDisplaySignalAdditionalInfoRaw != 0 ||
                  nativeDisplaySignalScanLineOrdering != 0) ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativeRasterOpenResultDeclared &&
                nativeRasterOpenResult != 0 ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativeRasterSourceDeclared &&
                nativeRasterVidPnSourceId != 0 ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativeRasterBeforeQueryResultDeclared &&
                (nativeRasterBeforeQueryResult != 0 ||
                 nativeRasterBeforeQueryStartUs != 0 ||
                 nativeRasterBeforeQueryEndUs != 0 ||
                 nativeRasterBeforeInVerticalBlank ||
                 nativeRasterBeforeScanLine != 0) ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !nativeRasterAfterQueryResultDeclared &&
                (nativeRasterAfterQueryResult != 0 ||
                 nativeRasterAfterQueryStartUs != 0 ||
                 nativeRasterAfterQueryEndUs != 0 ||
                 nativeRasterAfterInVerticalBlank ||
                 nativeRasterAfterScanLine != 0) ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !submissionIdQueryResultDeclared &&
                submissionIdQueryResult != 0 ? 1 : 0;
        metrics.validityPayloadMismatches +=
            !frameStatsQueryResultDeclared &&
                frameStatsQueryResult != 0 ? 1 : 0;
        metrics.validityPayloadMismatches +=
            (!rawSyncQpcDeclared &&
                (rawSyncQpcTicks != 0 ||
                 rowRawSyncQpcFrequency != 0)) ||
            (rawSyncQpcDeclared &&
                (rawSyncQpcTicks == 0 ||
                 rowRawSyncQpcFrequency == 0)) ? 1 : 0;
        metrics.validityPayloadMismatches +=
            (!qpcCorrelationDeclared &&
                (qpcCorrelationReferenceTicks != 0 ||
                 qpcCorrelationReferenceTimeUs != 0 ||
                 qpcCorrelationSpanTicks != 0)) ||
            (qpcCorrelationDeclared &&
                qpcCorrelationReferenceTicks == 0) ? 1 : 0;
        const bool normalPresentAttempt =
            disposition == "presented" ||
            disposition == "output_dropped";
        if (metrics.nativeOutcomeTelemetryAvailable) {
            const bool nativeBackendKnown =
                nativeBackend == kNativeBackendDxgi ||
                nativeBackend == kNativeBackendVulkan ||
                nativeBackend == kNativeBackendComposition;
            const bool nativeDxgiPresentAttempt =
                nativeBackendDeclared &&
                nativeBackend == kNativeBackendDxgi;
            const bool nativeVulkanPresentAttempt =
                nativeBackendDeclared &&
                nativeBackend == kNativeBackendVulkan;
            const bool nativePresentationAccepted =
                nativePresentResultDeclared &&
                nativePresentResult == 0;
            const bool submissionIdQuerySucceeded =
                submissionIdQueryResultDeclared &&
                submissionIdQueryResult == 0;
            const bool frameStatsQuerySucceeded =
                frameStatsQueryResultDeclared &&
                frameStatsQueryResult == 0;
            metrics.normalPresentAttemptRows +=
                normalPresentAttempt ? 1 : 0;
            metrics.nativePresentAttemptRows +=
                nativeBackendDeclared ? 1 : 0;
            metrics.nativeDxgiPresentAttemptRows +=
                nativeDxgiPresentAttempt ? 1 : 0;
            metrics.nativeVulkanPresentAttemptRows +=
                nativeVulkanPresentAttempt ? 1 : 0;
            if (nativeBackendDeclared) {
                ++metrics.nativeBackendCounts[
                    QByteArray::number(nativeBackend)];
            }
            if (nativePresentResultDeclared) {
                ++metrics.nativePresentResultValidRows;
                ++metrics.nativePresentResults[
                    QByteArray::number(nativePresentResult)];
            }
            if (submissionIdQueryResultDeclared) {
                ++metrics.submissionIdQueryResults[
                    QByteArray::number(submissionIdQueryResult)];
            }
            if (frameStatsQueryResultDeclared) {
                ++metrics.frameStatsQueryResults[
                    QByteArray::number(frameStatsQueryResult)];
            }
            metrics.presentedNativePresentResultValidRows +=
                presented && nativePresentResultDeclared ? 1 : 0;
            metrics.presentedSubmissionIdQueryResultValidRows +=
                presented && submissionIdQueryResultDeclared ? 1 : 0;
            metrics.presentedFrameStatsQueryResultValidRows +=
                presented && frameStatsQueryResultDeclared ? 1 : 0;
            bool nativeOutcomeRelationshipValid =
                nativeBackendDeclared == nativePresentResultDeclared &&
                (!nativeBackendDeclared || nativeBackendKnown) &&
                (!nativePresentResultDeclared ||
                 nativePresentationAccepted == presented);
            if (nativeDxgiPresentAttempt) {
                nativeOutcomeRelationshipValid =
                    nativeOutcomeRelationshipValid &&
                    normalPresentAttempt &&
                    (!presented ||
                     (submissionIdQueryResultDeclared &&
                      frameStatsQueryResultDeclared)) &&
                    (presented ||
                     (!submissionIdQueryResultDeclared &&
                      !frameStatsQueryResultDeclared)) &&
                    (!submissionIdQueryResultDeclared ||
                     (submissionIdQuerySucceeded == submissionIdValid)) &&
                    (!frameStatsQueryResultDeclared ||
                     frameStatsQuerySucceeded ||
                     (!latchSampleValid && !rawSyncQpcDeclared &&
                      (!metrics.qpcCorrelationTelemetryAvailable ||
                       !qpcCorrelationDeclared))) &&
                    (!latchSampleValid ||
                     (rawSyncQpcDeclared &&
                      (!metrics.qpcCorrelationTelemetryAvailable ||
                       qpcCorrelationDeclared)));
            }
            else if (nativeBackendDeclared && nativeBackend == kNativeBackendComposition) {
                // The presentation manager has its own present IDs and verified
                // display events. None of the DXGI query or flag fields apply.
                nativeOutcomeRelationshipValid = nativeOutcomeRelationshipValid &&
                    normalPresentAttempt && (presented == submissionIdValid) &&
                    !submissionIdQueryResultDeclared && !frameStatsQueryResultDeclared &&
                    !rawSyncQpcDeclared && !qpcCorrelationDeclared &&
                    (!latchSampleValid ||
                     optionalUnsignedField(fields, traceHeader.indexOf("latch_time_kind")) == 2);
            }
            else if (nativeVulkanPresentAttempt) {
                // Vulkan may submit an acquired image while cancelling it.
                // Wayland presentation feedback may also attach a libplacebo
                // submission ID and a display-event sample. DXGI
                // IDs/statistics and per-call DXGI flags are not valid
                // evidence for this backend.
                nativeOutcomeRelationshipValid =
                    nativeOutcomeRelationshipValid &&
                    (!submissionIdValid || presented) &&
                    (!latchSampleValid ||
                     (submissionIdValid &&
                      optionalUnsignedField(
                          fields, traceHeader.indexOf("latch_time_kind")) == 2)) &&
                    !submissionIdQueryResultDeclared &&
                    !frameStatsQueryResultDeclared &&
                    !rawSyncQpcDeclared &&
                    !qpcCorrelationDeclared;
            }
            else {
                nativeOutcomeRelationshipValid =
                    nativeOutcomeRelationshipValid &&
                    !presented &&
                    !submissionIdValid &&
                    !latchSampleValid &&
                    !submissionIdQueryResultDeclared &&
                    !frameStatsQueryResultDeclared &&
                    !rawSyncQpcDeclared &&
                    !qpcCorrelationDeclared;
            }
            metrics.nativeOutcomeRelationshipMismatchRows +=
                nativeOutcomeRelationshipValid ? 0 : 1;

            metrics.frameStatsSuccessWithoutRawSyncQpcRows +=
                nativeDxgiPresentAttempt &&
                frameStatsQuerySucceeded &&
                !rawSyncQpcDeclared ? 1 : 0;
            metrics.rawSyncQpcWithoutTranslatedRows +=
                rawSyncQpcDeclared && !latchSampleValid ? 1 : 0;
            metrics.qpcCorrelationRelationshipMismatchRows +=
                metrics.qpcCorrelationTelemetryAvailable &&
                rawSyncQpcDeclared != qpcCorrelationDeclared ? 1 : 0;
            if (rawSyncQpcDeclared) {
                ++metrics.rawSyncQpcValidRows;
                const VrrRawQpcTranslationResult translation =
                    rawSyncQpcTracker.observe(
                        rawSyncQpcTicks, rowRawSyncQpcFrequency,
                        latchSampleValid, rowLatchTimeUs,
                        kRawQpcTranslationToleranceUs);
                metrics.rawSyncQpcFrequencyMismatchRows +=
                    translation.frequencyMismatch ? 1 : 0;
                metrics.rawSyncQpcTranslationBaselines +=
                    translation.baselineEstablished ? 1 : 0;
                metrics.rawSyncQpcTranslationComparisons +=
                    translation.compared ? 1 : 0;
                metrics.rawSyncQpcTranslationMismatchRows +=
                    translation.translationMismatch ? 1 : 0;
            }
            if (qpcCorrelationDeclared) {
                ++metrics.qpcCorrelationValidRows;
                if (!metrics.haveQpcCorrelationReference) {
                    metrics.haveQpcCorrelationReference = true;
                    metrics.qpcCorrelationReferenceTicks =
                        qpcCorrelationReferenceTicks;
                    metrics.qpcCorrelationReferenceTimeUs =
                        qpcCorrelationReferenceTimeUs;
                    metrics.qpcCorrelationFrequency =
                        rowRawSyncQpcFrequency;
                    metrics.qpcCorrelationSpanTicks =
                        qpcCorrelationSpanTicks;
                }
                else if (metrics.qpcCorrelationReferenceTicks !=
                             qpcCorrelationReferenceTicks ||
                         metrics.qpcCorrelationReferenceTimeUs !=
                             qpcCorrelationReferenceTimeUs ||
                         metrics.qpcCorrelationFrequency !=
                             rowRawSyncQpcFrequency ||
                         metrics.qpcCorrelationSpanTicks !=
                             qpcCorrelationSpanTicks) {
                    ++metrics.qpcCorrelationReferenceMismatchRows;
                }
                const VrrQpcCorrelationResult correlation =
                    evaluateVrrQpcCorrelation(
                        rawSyncQpcTicks, rowRawSyncQpcFrequency,
                        rowLatchTimeUs,
                        qpcCorrelationReferenceTicks,
                        qpcCorrelationReferenceTimeUs,
                        qpcCorrelationSpanTicks,
                        kRawQpcTranslationToleranceUs);
                metrics.qpcCorrelationTranslationMismatchRows +=
                    (!correlation.translated ||
                     !correlation.matches) ? 1 : 0;
                metrics.qpcCorrelationUncertaintyInvalidRows +=
                    correlation.uncertaintyValid ? 0 : 1;
                if (correlation.uncertaintyValid) {
                    metrics.
                        qpcCorrelationHalfSpanUncertaintyUsMaximum =
                            std::max(
                                metrics.
                                    qpcCorrelationHalfSpanUncertaintyUsMaximum,
                                correlation.halfSpanUncertaintyUs);
                }
            }
        }
        if (metrics.nativeOutcomeTelemetryAvailable &&
                metrics.nativePresentContractTelemetryAvailable) {
            const bool nativeDxgiPresentAttempt =
                nativeBackendDeclared &&
                nativeBackend == kNativeBackendDxgi;
            metrics.nativePresentParametersValidRows +=
                nativePresentParametersDeclared ? 1 : 0;
            metrics.nativeVrrStateValidRows +=
                nativeVrrStateDeclared ? 1 : 0;
            metrics.nativeRenderAdapterLuidValidRows +=
                nativeRenderAdapterLuidDeclared ? 1 : 0;
            metrics.nativeForegroundWindowRows +=
                nativeVrrStateDeclared &&
                nativeForegroundWindow ? 1 : 0;
            metrics.nativeRenderAdapterLuidRelationshipMismatchRows +=
                nativeRenderAdapterLuidDeclared ==
                    nativeDxgiPresentAttempt ? 0 : 1;
            if (nativeRenderAdapterLuidDeclared) {
                if (!metrics.haveNativeRenderAdapterLuidReference) {
                    metrics.haveNativeRenderAdapterLuidReference = true;
                    metrics.nativeRenderAdapterLuid =
                        nativeRenderAdapterLuid;
                }
                else if (metrics.nativeRenderAdapterLuid !=
                             nativeRenderAdapterLuid) {
                    ++metrics.nativeRenderAdapterLuidSnapshotMismatchRows;
                }
            }
            const bool nativeRenderAdapterMatchesDisplaySource =
                nativeRenderAdapterLuidDeclared &&
                nativeDisplayPathDeclared &&
                nativeRenderAdapterLuid ==
                    nativeDisplaySourceAdapterLuid;
            metrics.nativeRenderAdapterIdentityMismatchRows +=
                nativeDxgiPresentAttempt &&
                (!nativeSameGpuOutput ||
                 !nativeRenderAdapterMatchesDisplaySource) ? 1 : 0;
            if (nativePresentParametersDeclared) {
                ++metrics.nativePresentFlags[
                    QByteArray::number(nativePresentFlags)];
            }
            if (nativeVrrStateDeclared) {
                ++metrics.nativeVrrFallbackReasons[
                    QByteArray::number(nativeVrrFallbackReason)];
                ++metrics.nativeDesktopMonitorCounts[
                    QByteArray::number(nativeDesktopMonitorCount)];
            }
            const bool nativePresentParametersValid =
                vrrDxgiPresentParametersValid(
                    nativePresentParametersDeclared, nativeDxgiPresentAttempt,
                    rowLatchedPresent, nativePresentSyncInterval,
                    nativePresentFlags, rowAllowTearing);
            metrics.nativePresentParameterMismatchRows +=
                nativePresentParametersValid ? 0 : 1;
            const bool nativeVrrStateValid =
                nativeVrrStateDeclared == nativeDxgiPresentAttempt &&
                (!nativeVrrStateDeclared ||
                 (nativeTearingSupported &&
                  nativeBorderlessFlipModel &&
                  nativeSameGpuOutput &&
                  nativeRenderAdapterMatchesDisplaySource &&
                  nativeSwapChainAllowsTearing &&
                  nativePresentReadyAvailable &&
                  nativeForegroundWindow &&
                  nativeVrrFallbackReason == 0 &&
                  nativeDesktopMonitorCount != 0));
            metrics.nativeVrrStateMismatchRows +=
                nativeVrrStateValid ? 0 : 1;
        }
        if (metrics.nativeDxgiCapabilityTelemetryAvailable) {
            const bool nativeDxgiPresentAttempt =
                nativeBackendDeclared &&
                nativeBackend == kNativeBackendDxgi;
            const VrrDxgiCapabilityAudit capability =
                evaluateVrrDxgiCapability(
                    nativeDxgiPresentAttempt,
                    nativeTearingFeatureQueryResultDeclared,
                    nativeTearingFeatureQueryResult,
                    nativeTearingFeatureAllowsTearing,
                    nativeTearingSupported,
                    nativeSwapChainDescQueryResultDeclared,
                    nativeSwapChainDescQueryResult,
                    nativeSwapChainFlags,
                    nativeSwapChainSwapEffect,
                    nativeSwapChainAllowsTearing,
                    nativeFullscreenStateQueryResultDeclared,
                    nativeFullscreenStateQueryResult,
                    nativeFullscreenExclusive,
                    nativeWindowFlags,
                    nativeBorderlessFlipModel);
            metrics.nativeDxgiCapabilityRelationshipMismatchRows +=
                capability.relationshipsValid ? 0 : 1;
            metrics.nativeTearingFeatureQueryResultValidRows +=
                nativeTearingFeatureQueryResultDeclared ? 1 : 0;
            metrics.nativeTearingFeatureQuerySuccessRows +=
                capability.featureQuerySucceeded ? 1 : 0;
            metrics.nativeTearingFeatureAllowsTearingRows +=
                nativeTearingFeatureQueryResultDeclared &&
                nativeTearingFeatureAllowsTearing ? 1 : 0;
            metrics.nativeSwapChainDescQueryResultValidRows +=
                nativeSwapChainDescQueryResultDeclared ? 1 : 0;
            metrics.nativeSwapChainDescQuerySuccessRows +=
                capability.descriptorQuerySucceeded ? 1 : 0;
            metrics.nativeSwapChainFlipModelRows +=
                capability.flipModel ? 1 : 0;
            metrics.nativeSwapChainAllowsTearingRows +=
                capability.swapChainAllowsTearing ? 1 : 0;
            metrics.nativeFullscreenStateQueryResultValidRows +=
                nativeFullscreenStateQueryResultDeclared ? 1 : 0;
            metrics.nativeFullscreenStateQuerySuccessRows +=
                capability.fullscreenQuerySucceeded ? 1 : 0;
            metrics.nativeWindowBorderlessRows +=
                capability.borderlessWindow ? 1 : 0;
            metrics.nativeWindowedSwapChainRows +=
                capability.fullscreenQuerySucceeded &&
                !nativeFullscreenExclusive ? 1 : 0;
            metrics.nativeDxgiCapabilityExactEligibleRows +=
                capability.exactEligible ? 1 : 0;

            if (nativeTearingFeatureQueryResultDeclared) {
                ++metrics.nativeTearingFeatureQueryResults[
                    QByteArray::number(
                        nativeTearingFeatureQueryResult)];
            }
            if (nativeSwapChainDescQueryResultDeclared) {
                ++metrics.nativeSwapChainDescQueryResults[
                    QByteArray::number(
                        nativeSwapChainDescQueryResult)];
                ++metrics.nativeSwapChainFlagsObserved[
                    QByteArray::number(nativeSwapChainFlags)];
                ++metrics.nativeSwapChainSwapEffects[
                    QByteArray::number(nativeSwapChainSwapEffect)];
            }
            if (nativeFullscreenStateQueryResultDeclared) {
                ++metrics.nativeFullscreenStateQueryResults[
                    QByteArray::number(
                        nativeFullscreenStateQueryResult)];
                ++metrics.nativeWindowFlagsObserved[
                    QByteArray::number(nativeWindowFlags)];
            }

            if (nativeDxgiPresentAttempt &&
                    capability.declarationsMatchBackend) {
                if (!metrics.haveNativeDxgiCapabilityReference) {
                    metrics.haveNativeDxgiCapabilityReference = true;
                    ++metrics.nativeDxgiCapabilitySnapshotEpochs;
                    metrics.nativeTearingFeatureQueryResult =
                        nativeTearingFeatureQueryResult;
                    metrics.nativeTearingFeatureAllowsTearing =
                        nativeTearingFeatureAllowsTearing;
                    metrics.nativeSwapChainDescQueryResult =
                        nativeSwapChainDescQueryResult;
                    metrics.nativeSwapChainFlags =
                        nativeSwapChainFlags;
                    metrics.nativeSwapChainSwapEffect =
                        nativeSwapChainSwapEffect;
                    metrics.nativeFullscreenStateQueryResult =
                        nativeFullscreenStateQueryResult;
                    metrics.nativeFullscreenExclusive =
                        nativeFullscreenExclusive;
                    metrics.nativeWindowFlags =
                        nativeWindowFlags;
                }
                else {
                    metrics.nativeWindowRawFlagDifferenceRows +=
                        metrics.nativeWindowFlags != nativeWindowFlags ?
                            1 : 0;
                    const bool windowCapabilityFlagsChanged =
                        ((metrics.nativeWindowFlags ^ nativeWindowFlags) &
                            kSdlWindowFullscreenDesktop) != 0;
                    if (metrics.nativeTearingFeatureQueryResult !=
                            nativeTearingFeatureQueryResult ||
                        metrics.nativeTearingFeatureAllowsTearing !=
                            nativeTearingFeatureAllowsTearing ||
                        metrics.nativeSwapChainDescQueryResult !=
                            nativeSwapChainDescQueryResult ||
                        metrics.nativeSwapChainFlags !=
                            nativeSwapChainFlags ||
                        metrics.nativeSwapChainSwapEffect !=
                            nativeSwapChainSwapEffect ||
                        metrics.nativeFullscreenStateQueryResult !=
                            nativeFullscreenStateQueryResult ||
                        metrics.nativeFullscreenExclusive !=
                            nativeFullscreenExclusive ||
                        windowCapabilityFlagsChanged) {
                        ++metrics.nativeDxgiCapabilitySnapshotMismatchRows;
                    }
                }
            }
        }
        if (metrics.nativeVblankVirtualizationTelemetryAvailable) {
            const bool nativeDxgiPresentAttempt =
                nativeBackendDeclared &&
                nativeBackend == kNativeBackendDxgi;
            metrics.nativeVblankVirtualizationProbeCompleteRows +=
                nativeVblankVirtualizationProbeComplete ? 1 : 0;
            metrics.nativeVblankVirtualizationCallAvailableRows +=
                nativeVblankVirtualizationCallAvailable ? 1 : 0;
            metrics.nativeVblankVirtualizationResultValidRows +=
                nativeVblankVirtualizationResultDeclared ? 1 : 0;
            metrics.nativeVblankVirtualizationSuccessRows +=
                nativeVblankVirtualizationResultDeclared &&
                nativeVblankVirtualizationResult == 0 ? 1 : 0;
            metrics.nativeVblankVirtualizationDisabledRows +=
                nativeVblankVirtualizationDisabled ? 1 : 0;
            if (nativeVblankVirtualizationResultDeclared) {
                ++metrics.nativeVblankVirtualizationResults[
                    QByteArray::number(
                        nativeVblankVirtualizationResult)];
            }

            const bool relationshipValid =
                nativeVblankVirtualizationProbeComplete ==
                    nativeDxgiPresentAttempt &&
                (!nativeVblankVirtualizationCallAvailable ||
                 nativeVblankVirtualizationProbeComplete) &&
                nativeVblankVirtualizationResultDeclared ==
                    nativeVblankVirtualizationCallAvailable &&
                nativeVblankVirtualizationDisabled ==
                    (nativeVblankVirtualizationResultDeclared &&
                     nativeVblankVirtualizationResult == 0);
            metrics.nativeVblankVirtualizationRelationshipMismatchRows +=
                relationshipValid ? 0 : 1;
        }
        if (metrics.nativeDisplayTimingTelemetryAvailable) {
            const bool nativeDxgiPresentAttempt =
                nativeBackendDeclared &&
                nativeBackend == kNativeBackendDxgi;
            metrics.nativeDisplayConfigQueryResultValidRows +=
                nativeDisplayConfigQueryResultDeclared ? 1 : 0;
            metrics.nativeDisplayConfigQuerySuccessRows +=
                nativeDisplayConfigQueryResultDeclared &&
                nativeDisplayConfigQueryResult == 0 ? 1 : 0;
            metrics.nativeDisplayPathValidRows +=
                nativeDisplayPathDeclared ? 1 : 0;
            metrics.nativeDisplayTargetAvailableRows +=
                nativeDisplayPathDeclared &&
                nativeDisplayTargetAvailable ? 1 : 0;
            metrics.nativeDisplaySignalValidRows +=
                nativeDisplaySignalDeclared ? 1 : 0;
            metrics.nativeDisplayDrrBoostRows +=
                nativeDisplayPathDeclared &&
                (nativeDisplayPathFlags &
                    kDisplayConfigPathBoostRefreshRate) != 0 ? 1 : 0;
            if (nativeDisplayPathDeclared) {
                metrics.nativeDisplayUnknownPathFlagRows +=
                    (nativeDisplayPathFlags &
                        ~kDisplayConfigPathKnownFlags) != 0 ? 1 : 0;
                metrics.nativeDisplayNonIdentityRotationRows +=
                    nativeDisplayRotation !=
                        kDisplayConfigRotationIdentity ? 1 : 0;
                metrics.nativeDisplayNonIdentityScalingRows +=
                    nativeDisplayScaling !=
                        kDisplayConfigScalingIdentity ? 1 : 0;
            }
            if (nativeDisplaySignalDeclared) {
                metrics.nativeDisplayNonProgressiveRows +=
                    nativeDisplaySignalScanLineOrdering !=
                        kDisplayConfigProgressiveScan ? 1 : 0;
                metrics.nativeDisplaySignalVsyncDividerRows +=
                    ((nativeDisplaySignalAdditionalInfoRaw >> 16) &
                        0x3fULL) != 0 ? 1 : 0;
                metrics.nativeDisplaySignalReservedInfoRows +=
                    ((nativeDisplaySignalAdditionalInfoRaw >> 22) &
                        0x3ffULL) != 0 ? 1 : 0;
                const uint64_t rowSignalRoundedHz =
                    nativeDisplaySignalVSyncDenominator == 0 ? 0 :
                        (nativeDisplaySignalVSyncNumerator +
                         nativeDisplaySignalVSyncDenominator / 2) /
                            nativeDisplaySignalVSyncDenominator;
                const int64_t rowCapturedDisplayHz = signedField(
                    fields, columns.displayRefreshHz);
                metrics.nativeDisplaySignalRateMismatchRows +=
                    rowCapturedDisplayHz <= 0 ||
                    rowSignalRoundedHz !=
                        static_cast<uint64_t>(
                            rowCapturedDisplayHz) ? 1 : 0;

                bool configuredPeriodMatches = true;
                if (scenario.display.scanoutPeriodPs != 0) {
                    const uint64_t differencePs =
                        scenario.display.scanoutPeriodPs >=
                                nativeDisplaySignalPeriodPs ?
                            scenario.display.scanoutPeriodPs -
                                nativeDisplaySignalPeriodPs :
                            nativeDisplaySignalPeriodPs -
                                scenario.display.scanoutPeriodPs;
                    configuredPeriodMatches =
                        nativeDisplaySignalPeriodValid &&
                        differencePs <= 1;
                }
                if (scenario.display.scanoutPeriodUs != 0) {
                    const uint64_t signalPeriodUs =
                        nativeDisplaySignalPeriodPs / 1000000ULL +
                        (nativeDisplaySignalPeriodPs %
                                 1000000ULL >= 500000ULL ?
                            1ULL : 0ULL);
                    const uint64_t differenceUs =
                        scenario.display.scanoutPeriodUs >=
                                signalPeriodUs ?
                            scenario.display.scanoutPeriodUs -
                                signalPeriodUs :
                            signalPeriodUs -
                                scenario.display.scanoutPeriodUs;
                    configuredPeriodMatches =
                        configuredPeriodMatches &&
                        nativeDisplaySignalPeriodValid &&
                        differenceUs <= 1;
                }
                metrics.configuredScanoutPeriodMismatchRows +=
                    configuredPeriodMatches ? 0 : 1;

                bool configuredActiveScanoutMatches = true;
                if (scenario.display.activeScanoutPs != 0) {
                    configuredActiveScanoutMatches =
                        nativeDisplaySignalActiveScanoutPsValid &&
                        scenario.display.activeScanoutPs ==
                            nativeDisplaySignalActiveScanoutPs;
                }
                if (scenario.display.activeScanoutUs != 0) {
                    const uint64_t signalActiveUs =
                        nativeDisplaySignalActiveScanoutPs / 1000000ULL +
                        (nativeDisplaySignalActiveScanoutPs %
                                 1000000ULL >= 500000ULL ?
                            1ULL : 0ULL);
                    const uint64_t differenceUs =
                        scenario.display.activeScanoutUs >=
                                signalActiveUs ?
                            scenario.display.activeScanoutUs -
                                signalActiveUs :
                            signalActiveUs -
                                scenario.display.activeScanoutUs;
                    configuredActiveScanoutMatches =
                        configuredActiveScanoutMatches &&
                        nativeDisplaySignalActiveScanoutPsValid &&
                        differenceUs <= 1;
                }
                metrics.configuredActiveScanoutMismatchRows +=
                    configuredActiveScanoutMatches ? 0 : 1;
            }
            if (nativeDisplayPathDeclared &&
                    nativeDisplaySignalDeclared) {
                metrics.nativeDisplayPathSignalRateMismatchRows +=
                    vrrRefreshRationalsEqual(
                        nativeDisplayPathRefreshNumerator,
                        nativeDisplayPathRefreshDenominator,
                        nativeDisplaySignalVSyncNumerator,
                        nativeDisplaySignalVSyncDenominator) ? 0 : 1;
            }

            const bool relationshipValid =
                nativeDisplayConfigQueryResultDeclared ==
                    nativeDxgiPresentAttempt &&
                (!nativeDisplayPathDeclared ||
                 (nativeDisplayConfigQueryResultDeclared &&
                  nativeDisplayConfigQueryResult == 0)) &&
                (!nativeDisplayTargetAvailable ||
                 nativeDisplayPathDeclared) &&
                (!nativeDisplaySignalDeclared ||
                 nativeDisplayPathDeclared) &&
                (!nativeDisplayPathDeclared ||
                 ((nativeDisplayPathFlags &
                       kDisplayConfigPathActive) != 0 &&
                  nativeDisplayRotation != 0 &&
                  nativeDisplayScaling != 0 &&
                  nativeDisplayPathRefreshNumerator != 0 &&
                  nativeDisplayPathRefreshDenominator != 0)) &&
                (!nativeDisplaySignalDeclared ||
                 (nativeDisplaySignalPixelRateHz != 0 &&
                  nativeDisplaySignalHSyncNumerator != 0 &&
                  nativeDisplaySignalHSyncDenominator != 0 &&
                  nativeDisplaySignalPeriodValid &&
                  nativeDisplaySignalConsistency.inputsValid &&
                  nativeDisplaySignalConsistency.withinTolerance &&
                  nativeDisplaySignalActiveWidth != 0 &&
                  nativeDisplaySignalActiveHeight != 0 &&
                  nativeDisplaySignalTotalWidth >=
                      nativeDisplaySignalActiveWidth &&
                  nativeDisplaySignalTotalHeight >=
                      nativeDisplaySignalActiveHeight &&
                  nativeDisplaySignalAdditionalInfoRaw <=
                      std::numeric_limits<uint32_t>::max()));
            metrics.nativeDisplayTimingRelationshipMismatchRows +=
                relationshipValid ? 0 : 1;

            if (nativeDisplayPathDeclared &&
                    nativeDisplaySignalDeclared &&
                    nativeDisplaySignalPeriodValid) {
                if (!metrics.haveNativeDisplayTimingReference) {
                    metrics.haveNativeDisplayTimingReference = true;
                    metrics.nativeDisplayPathFlags =
                        nativeDisplayPathFlags;
                    metrics.nativeDisplaySourceAdapterLuid =
                        nativeDisplaySourceAdapterLuid;
                    metrics.nativeDisplaySourceId =
                        nativeDisplaySourceId;
                    metrics.nativeDisplayTargetAdapterLuid =
                        nativeDisplayTargetAdapterLuid;
                    metrics.nativeDisplayTargetId =
                        nativeDisplayTargetId;
                    metrics.nativeDisplayOutputTechnology =
                        nativeDisplayOutputTechnology;
                    metrics.nativeDisplayRotation =
                        nativeDisplayRotation;
                    metrics.nativeDisplayScaling =
                        nativeDisplayScaling;
                    metrics.nativeDisplayPathRefreshNumerator =
                        nativeDisplayPathRefreshNumerator;
                    metrics.nativeDisplayPathRefreshDenominator =
                        nativeDisplayPathRefreshDenominator;
                    metrics.nativeDisplaySignalPixelRateHz =
                        nativeDisplaySignalPixelRateHz;
                    metrics.nativeDisplaySignalHSyncNumerator =
                        nativeDisplaySignalHSyncNumerator;
                    metrics.nativeDisplaySignalHSyncDenominator =
                        nativeDisplaySignalHSyncDenominator;
                    metrics.nativeDisplaySignalVSyncNumerator =
                        nativeDisplaySignalVSyncNumerator;
                    metrics.nativeDisplaySignalVSyncDenominator =
                        nativeDisplaySignalVSyncDenominator;
                    metrics.nativeDisplaySignalActiveWidth =
                        nativeDisplaySignalActiveWidth;
                    metrics.nativeDisplaySignalActiveHeight =
                        nativeDisplaySignalActiveHeight;
                    metrics.nativeDisplaySignalTotalWidth =
                        nativeDisplaySignalTotalWidth;
                    metrics.nativeDisplaySignalTotalHeight =
                        nativeDisplaySignalTotalHeight;
                    metrics.nativeDisplaySignalAdditionalInfoRaw =
                        nativeDisplaySignalAdditionalInfoRaw;
                    metrics.nativeDisplaySignalScanLineOrdering =
                        nativeDisplaySignalScanLineOrdering;
                    metrics.nativeDisplaySignalPeriodPs =
                        nativeDisplaySignalPeriodPs;
                }
                else if (metrics.nativeDisplayPathFlags !=
                             nativeDisplayPathFlags ||
                         metrics.nativeDisplaySourceAdapterLuid !=
                             nativeDisplaySourceAdapterLuid ||
                         metrics.nativeDisplaySourceId !=
                             nativeDisplaySourceId ||
                         metrics.nativeDisplayTargetAdapterLuid !=
                             nativeDisplayTargetAdapterLuid ||
                         metrics.nativeDisplayTargetId !=
                             nativeDisplayTargetId ||
                         metrics.nativeDisplayOutputTechnology !=
                             nativeDisplayOutputTechnology ||
                         metrics.nativeDisplayRotation !=
                             nativeDisplayRotation ||
                         metrics.nativeDisplayScaling !=
                             nativeDisplayScaling ||
                         metrics.nativeDisplayPathRefreshNumerator !=
                             nativeDisplayPathRefreshNumerator ||
                         metrics.nativeDisplayPathRefreshDenominator !=
                             nativeDisplayPathRefreshDenominator ||
                         metrics.nativeDisplaySignalPixelRateHz !=
                             nativeDisplaySignalPixelRateHz ||
                         metrics.nativeDisplaySignalHSyncNumerator !=
                             nativeDisplaySignalHSyncNumerator ||
                         metrics.nativeDisplaySignalHSyncDenominator !=
                             nativeDisplaySignalHSyncDenominator ||
                         metrics.nativeDisplaySignalVSyncNumerator !=
                             nativeDisplaySignalVSyncNumerator ||
                         metrics.nativeDisplaySignalVSyncDenominator !=
                             nativeDisplaySignalVSyncDenominator ||
                         metrics.nativeDisplaySignalActiveWidth !=
                             nativeDisplaySignalActiveWidth ||
                         metrics.nativeDisplaySignalActiveHeight !=
                             nativeDisplaySignalActiveHeight ||
                         metrics.nativeDisplaySignalTotalWidth !=
                             nativeDisplaySignalTotalWidth ||
                         metrics.nativeDisplaySignalTotalHeight !=
                             nativeDisplaySignalTotalHeight ||
                         metrics.nativeDisplaySignalAdditionalInfoRaw !=
                             nativeDisplaySignalAdditionalInfoRaw ||
                         metrics.nativeDisplaySignalScanLineOrdering !=
                             nativeDisplaySignalScanLineOrdering ||
                         metrics.nativeDisplaySignalPeriodPs !=
                             nativeDisplaySignalPeriodPs) {
                    ++metrics.nativeDisplayTimingSnapshotMismatchRows;
                }
            }
        }
        if (metrics.nativeRasterTelemetryAvailable) {
            const bool nativeDxgiPresentAttempt =
                nativeBackendDeclared &&
                nativeBackend == kNativeBackendDxgi;
            metrics.nativeRasterSamplingRequestedRows +=
                nativeRasterSamplingRequested ? 1 : 0;
            metrics.nativeRasterOpenResultValidRows +=
                nativeRasterOpenResultDeclared ? 1 : 0;
            metrics.nativeRasterSourceValidRows +=
                nativeRasterSourceDeclared ? 1 : 0;
            metrics.nativeRasterBeforeQueryResultValidRows +=
                nativeRasterBeforeQueryResultDeclared ? 1 : 0;
            metrics.nativeRasterBeforeQuerySuccessRows +=
                nativeRasterBeforeQueryResultDeclared &&
                nativeRasterBeforeQueryResult == 0 ? 1 : 0;
            metrics.nativeRasterBeforeVerticalBlankRows +=
                nativeRasterBeforeQueryResultDeclared &&
                nativeRasterBeforeQueryResult == 0 &&
                nativeRasterBeforeInVerticalBlank ? 1 : 0;
            metrics.nativeRasterBeforeActiveScanoutRows +=
                nativeRasterBeforeQueryResultDeclared &&
                nativeRasterBeforeQueryResult == 0 &&
                !nativeRasterBeforeInVerticalBlank ? 1 : 0;
            metrics.nativeRasterAfterQueryResultValidRows +=
                nativeRasterAfterQueryResultDeclared ? 1 : 0;
            metrics.nativeRasterAfterQuerySuccessRows +=
                nativeRasterAfterQueryResultDeclared &&
                nativeRasterAfterQueryResult == 0 ? 1 : 0;
            metrics.nativeRasterAfterVerticalBlankRows +=
                nativeRasterAfterQueryResultDeclared &&
                nativeRasterAfterQueryResult == 0 &&
                nativeRasterAfterInVerticalBlank ? 1 : 0;
            metrics.nativeRasterAfterActiveScanoutRows +=
                nativeRasterAfterQueryResultDeclared &&
                nativeRasterAfterQueryResult == 0 &&
                !nativeRasterAfterInVerticalBlank ? 1 : 0;
            if (nativeRasterBeforeQueryResultDeclared &&
                    nativeRasterBeforeQueryResult == 0) {
                metrics.nativeRasterBeforeScanLine.add(
                    nativeRasterBeforeScanLine);
                uint64_t normalizedScanLine = 0;
                const bool normalized =
                    metrics.nativeRasterScanLineScaleInference.valid &&
                    normalizeVrrRasterScanLine(
                        nativeRasterBeforeScanLine,
                        metrics.nativeRasterScanLineScaleInference.scale,
                        normalizedScanLine);
                if (normalized) {
                    metrics.nativeRasterBeforeNormalizedScanLine.add(
                        normalizedScanLine);
                }
                if (nativeDisplaySignalDeclared &&
                        nativeDisplaySignalTotalHeight != 0) {
                    ++metrics.nativeRasterSignalRangeCheckedSamples;
                    const uint64_t scanLineLimit =
                        nativeRasterBeforeInVerticalBlank ?
                            nativeDisplaySignalTotalHeight :
                            nativeDisplaySignalActiveHeight;
                    if (!normalized ||
                            scanLineLimit == 0 ||
                            normalizedScanLine >= scanLineLimit) {
                        ++metrics.nativeRasterSignalRangeMismatchSamples;
                    }
                    else if (!nativeRasterBeforeInVerticalBlank) {
                        metrics.
                            nativeRasterBeforeActiveScanoutPositionPpm.add(
                                normalizedScanLine *
                                    1000000ULL /
                                    nativeDisplaySignalActiveHeight);
                    }
                }
            }
            if (nativeRasterAfterQueryResultDeclared &&
                    nativeRasterAfterQueryResult == 0) {
                metrics.nativeRasterAfterScanLine.add(
                    nativeRasterAfterScanLine);
                uint64_t normalizedScanLine = 0;
                const bool normalized =
                    metrics.nativeRasterScanLineScaleInference.valid &&
                    normalizeVrrRasterScanLine(
                        nativeRasterAfterScanLine,
                        metrics.nativeRasterScanLineScaleInference.scale,
                        normalizedScanLine);
                if (normalized) {
                    metrics.nativeRasterAfterNormalizedScanLine.add(
                        normalizedScanLine);
                }
                if (nativeDisplaySignalDeclared &&
                        nativeDisplaySignalTotalHeight != 0) {
                    ++metrics.nativeRasterSignalRangeCheckedSamples;
                    const uint64_t scanLineLimit =
                        nativeRasterAfterInVerticalBlank ?
                            nativeDisplaySignalTotalHeight :
                            nativeDisplaySignalActiveHeight;
                    if (!normalized ||
                            scanLineLimit == 0 ||
                            normalizedScanLine >= scanLineLimit) {
                        ++metrics.nativeRasterSignalRangeMismatchSamples;
                    }
                    else if (!nativeRasterAfterInVerticalBlank) {
                        metrics.
                            nativeRasterAfterActiveScanoutPositionPpm.add(
                                normalizedScanLine *
                                    1000000ULL /
                                    nativeDisplaySignalActiveHeight);
                    }
                }
            }
            if (nativeRasterOpenResultDeclared) {
                ++metrics.nativeRasterOpenResults[
                    QByteArray::number(nativeRasterOpenResult)];
            }
            if (nativeRasterBeforeQueryResultDeclared) {
                ++metrics.nativeRasterBeforeQueryResults[
                    QByteArray::number(
                        nativeRasterBeforeQueryResult)];
            }
            if (nativeRasterAfterQueryResultDeclared) {
                ++metrics.nativeRasterAfterQueryResults[
                    QByteArray::number(
                        nativeRasterAfterQueryResult)];
            }

            const bool beforeQueryExpected =
                nativeDxgiPresentAttempt &&
                deepTraceRow &&
                nativeRasterSamplingRequested &&
                nativeRasterSourceDeclared;
            const bool afterQueryExpected = beforeQueryExpected;
            const bool relationshipValid =
                (!nativeRasterSamplingRequested ||
                 nativeDxgiPresentAttempt) &&
                (nativeRasterOpenResultDeclared ==
                    nativeRasterSamplingRequested) &&
                (nativeRasterSourceDeclared ==
                    (nativeRasterOpenResultDeclared &&
                     nativeRasterOpenResult == 0)) &&
                (!nativeRasterSourceDeclared ||
                 nativeRasterSamplingRequested) &&
                (nativeRasterBeforeQueryResultDeclared ==
                    beforeQueryExpected) &&
                (nativeRasterAfterQueryResultDeclared ==
                    afterQueryExpected) &&
                (!nativeRasterBeforeQueryResultDeclared ||
                 nativeRasterBeforeQueryResult == 0 ||
                 (!nativeRasterBeforeInVerticalBlank &&
                  nativeRasterBeforeScanLine == 0)) &&
                (!nativeRasterAfterQueryResultDeclared ||
                 nativeRasterAfterQueryResult == 0 ||
                 (!nativeRasterAfterInVerticalBlank &&
                  nativeRasterAfterScanLine == 0));
            metrics.nativeRasterRelationshipMismatchRows +=
                relationshipValid ? 0 : 1;

            bool timingOrderValid = true;
            if (nativeRasterBeforeQueryResultDeclared) {
                timingOrderValid =
                    nativePresentTimingDeclared &&
                    nativeRasterBeforeQueryStartUs != 0 &&
                    nativeRasterBeforeQueryStartUs <=
                        nativeRasterBeforeQueryEndUs &&
                    nativeRasterBeforeQueryEndUs <=
                        nativePresentStartUs;
                if (nativeRasterBeforeQueryEndUs >=
                        nativeRasterBeforeQueryStartUs) {
                    metrics.nativeRasterBeforeQueryDurationUs.add(
                        nativeRasterBeforeQueryEndUs -
                            nativeRasterBeforeQueryStartUs);
                }
                if (nativePresentTimingDeclared &&
                        nativePresentStartUs >=
                            nativeRasterBeforeQueryEndUs) {
                    metrics.nativeRasterBeforeToPresentUs.add(
                        nativePresentStartUs -
                            nativeRasterBeforeQueryEndUs);
                }
            }
            if (nativeRasterAfterQueryResultDeclared) {
                const bool afterTimingValid =
                    nativePresentTimingDeclared &&
                    nativeRasterAfterQueryStartUs != 0 &&
                    nativePresentEndUs <=
                        nativeRasterAfterQueryStartUs &&
                    nativeRasterAfterQueryStartUs <=
                        nativeRasterAfterQueryEndUs;
                timingOrderValid =
                    timingOrderValid && afterTimingValid;
                if (nativeRasterAfterQueryEndUs >=
                        nativeRasterAfterQueryStartUs) {
                    metrics.nativeRasterAfterQueryDurationUs.add(
                        nativeRasterAfterQueryEndUs -
                            nativeRasterAfterQueryStartUs);
                }
                if (nativePresentTimingDeclared &&
                        nativeRasterAfterQueryStartUs >=
                            nativePresentEndUs) {
                    metrics.nativeRasterPresentToAfterUs.add(
                        nativeRasterAfterQueryStartUs -
                            nativePresentEndUs);
                }
            }
            metrics.nativeRasterTimingOrderMismatchRows +=
                timingOrderValid ? 0 : 1;
            metrics.nativeRasterSourceIdMismatchRows +=
                nativeRasterSourceDeclared &&
                nativeDisplayPathDeclared &&
                nativeRasterVidPnSourceId !=
                    nativeDisplaySourceId ? 1 : 0;
        }
        if (submissionIdValid) {
            ++metrics.submissionIdValidRows;
        }
        if (submissionIdValid && presented) {
            const uint64_t recordedSubmissionUs =
                optionalUnsignedField(
                    fields, columns.submissionBoundaryUs);
            const uint64_t nativePresentStartUs =
                optionalUnsignedField(
                    fields, columns.nativePresentTimingValid) != 0 &&
                optionalUnsignedField(
                    fields, columns.nativePresentStartUs) != 0 ?
                    optionalUnsignedField(
                        fields, columns.nativePresentStartUs) :
                    0;
            const SubmissionBand submissionBand {
                optionalUnsignedField(fields, columns.submissionId),
                roundedRateForPeriod(unsignedField(
                    fields, columns.sourcePeriodUs)),
                unsignedField(fields, columns.sourcePeriodUs),
                unsignedField(fields, columns.displayPeriodUs),
                recordedSubmissionUs,
                nativePresentStartUs,
                optionalUnsignedField(fields, columns.latchedPresent) != 0 &&
                    unsignedField(fields, columns.canLatch) != 0,
            };
            if (haveLatch && submissionBand.id < priorLatchSubmission) {
                ++metrics.submissionSequenceResets;
                haveLatch = false;
                pendingSubmissionBands.clear();
            }
            if (!pendingSubmissionBands.empty() &&
                    submissionBand.id < pendingSubmissionBands.back().id) {
                ++metrics.submissionSequenceResets;
                pendingSubmissionBands.clear();
            }
            if (!pendingSubmissionBands.empty() &&
                    submissionBand.id == pendingSubmissionBands.back().id) {
                ++metrics.submissionSequenceDuplicates;
                pendingSubmissionBands.back() = submissionBand;
            }
            else {
                pendingSubmissionBands.push_back(submissionBand);
            }
            while (pendingSubmissionBands.size() >
                    kMaximumPendingSubmissionBands) {
                pendingSubmissionBands.pop_front();
            }
        }
        const bool queueAccepted =
            unsignedField(fields, columns.queueAccepted) != 0;
        const uint64_t queueDepthBefore = unsignedField(
            fields, columns.queueDepthBefore);
        const uint64_t queueDepthAfter = optionalUnsignedField(
            fields, columns.queueDepthAfter);
        const uint64_t completionQueueDepth = optionalUnsignedField(
            fields, columns.completionQueueDepth);
        const uint64_t expectedQueueDepthAfter = queueAccepted ?
            std::min(
                saturatingAdd(queueDepthBefore, 1),
                kCapturedWorkerQueueCapacity) :
            queueDepthBefore;
        const bool queueStateValid =
            queueDepthBefore <= kCapturedWorkerQueueCapacity &&
            queueDepthAfter <= kCapturedWorkerQueueCapacity &&
            queueDepthAfter == expectedQueueDepthAfter &&
            queueAccepted == (disposition != "arrival_rejected");
        metrics.queueStateMismatches += queueStateValid ? 0 : 1;
        if (metrics.completionQueueDepthTelemetryAvailable) {
            metrics.completionQueueDepthOutOfRangeRows +=
                completionQueueDepth <= kCapturedWorkerQueueCapacity ? 0 : 1;
            metrics.observedCompletionQueueDepth.add(completionQueueDepth);
        }
        if (scenario.mode == "worker") {
            ++metrics.workerArrivals;
            if (queueAccepted) {
                ++metrics.workerAccepted;
                if (queueDepthBefore >= scenario.worker.queueCapacity) {
                    ++metrics.workerCapacityEvictions;
                }
            }
        }
        ++metrics.dispositions[disposition];
        ++metrics.tearClassifications[recordedTear];
        if (unsignedField(fields, columns.dropped) != 0) {
            ++metrics.originalDrops;
        }
        if (unsignedField(fields, columns.tearRisk) != 0) {
            ++metrics.originalTearRisks;
        }

        const uint64_t decodeCompleteUs = unsignedField(
            fields, columns.decodeCompleteUs);
        const uint64_t decoderOutputUs = columns.decoderOutputUs >= 0 ?
            unsignedField(fields, columns.decoderOutputUs) : decodeCompleteUs;
        const uint64_t dequeueUs = unsignedField(fields, columns.dequeueUs);
        const uint64_t decisionUs = unsignedField(fields, columns.decisionUs);
        const bool rowDecisionValid =
            unsignedField(fields, columns.decisionValid) != 0;
        const bool readinessOutcome = disposition == "presented" || disposition == "output_dropped" ||
            disposition == "queue_capacity" || disposition == "stale" || disposition == "preparation_failed";
        if (readinessOutcome) {
            const int intendedColumn = traceHeader.indexOf("original_target_us");
            const uint64_t deadline = intendedColumn >= 0 ? unsignedField(fields, intendedColumn) :
                unsignedField(fields, columns.recordedTargetUs);
            const uint64_t ready = unsignedField(fields, columns.preparationEndUs);
            const uint64_t at = optionalUnsignedField(fields, columns.terminalTimeUs);
            metrics.observedReadiness.record(at ? at : pacerArrivalUs,
                rowDecisionValid ? positiveDifference(ready, deadline) : 0,
                disposition != "presented");
        }
        const bool dispositionRequiresDecision =
            disposition == "presented" ||
            disposition == "output_dropped" ||
            disposition == "interrupted" ||
            disposition == "stale" ||
            disposition == "preparation_failed";
        metrics.decisionDispositionMismatches +=
            rowDecisionValid != dispositionRequiresDecision ? 1 : 0;
        bool presentationDispositionValid = false;
        if (disposition == "presented") {
            presentationDispositionValid = presented && !cancelled;
        }
        else if (disposition == "output_dropped") {
            presentationDispositionValid = !presented || cancelled;
        }
        else if (disposition == "interrupted" ||
                 disposition == "preparation_failed") {
            presentationDispositionValid = cancelled;
        }
        else {
            presentationDispositionValid = !presented && !cancelled;
        }
        metrics.presentationDispositionMismatches +=
            presentationDispositionValid ? 0 : 1;
        metrics.dispositionDropFlagMismatches +=
            (unsignedField(fields, columns.dropped) != 0) !=
                (disposition != "presented") ? 1 : 0;
        metrics.dequeueDecisionPresenceMismatches +=
            ((dequeueUs != 0) != rowDecisionValid ||
             (decisionUs != 0) != rowDecisionValid) ? 1 : 0;
        const uint64_t recordedSourcePeriodUs = unsignedField(
            fields, columns.sourcePeriodUs);
        const QByteArray expectedSourceRateHz = QByteArray::number(
            recordedSourcePeriodUs == 0 ? 0.0 :
                1000000.0 /
                    static_cast<double>(recordedSourcePeriodUs),
            'f', 3);
        metrics.sourceRateDisplayMismatches +=
            fields[columns.sourceRateHz] != expectedSourceRateHz ? 1 : 0;
        metrics.decodeToArrivalOrderViolations +=
            decoderOutputUs == 0 || pacerArrivalUs < decoderOutputUs ? 1 : 0;
        metrics.decodeReadinessOrderViolations +=
            vrrDecodeReadinessOrderValid(decoderOutputUs, decodeCompleteUs,
                pacerArrivalUs, dequeueUs, decisionUs,
                optionalUnsignedField(fields, columns.decodeSyncWaitUs),
                rowDecisionValid,
                capturedParameters.playoutResponsiveBuffer >= 4) ? 0 : 1;
        metrics.arrivalToDequeueOrderViolations +=
            dequeueUs != 0 && dequeueUs < pacerArrivalUs ? 1 : 0;
        metrics.dequeueToDecisionOrderViolations +=
            rowDecisionValid &&
                (dequeueUs == 0 || decisionUs < dequeueUs) ? 1 : 0;
        VrrWaitLifecycleEvidence renderWaitEvidence;
        VrrWaitLifecycleEvidence targetWaitEvidence;
        if (rowDecisionValid) {
            const uint64_t renderWaitEntryUs = optionalUnsignedField(
                fields, columns.renderWaitEntryUs);
            const uint64_t renderWaitFinalUs = optionalUnsignedField(
                fields, columns.renderWaitFinalUs);
            const uint64_t renderWaitOvershootUs = optionalUnsignedField(
                fields, columns.renderWaitOvershootUs);
            const uint64_t renderSchedulerDelayUs = optionalUnsignedField(
                fields, columns.renderSchedulerDelayUs);
            const bool renderSchedulerDelayValid =
                optionalUnsignedField(
                    fields, columns.renderSchedulerDelayValid) != 0;
            const bool renderDeadlineAlreadyElapsed =
                optionalUnsignedField(
                    fields, columns.renderDeadlineAlreadyElapsed) != 0;
            renderWaitEvidence.callEntryUs = renderWaitEntryUs;
            renderWaitEvidence.deadlineUs = unsignedField(
                fields, columns.renderStartUs);
            renderWaitEvidence.initialNowUs = optionalUnsignedField(
                fields, columns.renderWaitInitialUs);
            renderWaitEvidence.finalNowUs = renderWaitFinalUs;
            renderWaitEvidence.activeWaitUs = optionalUnsignedField(
                fields, columns.renderWaitActiveBudgetUs);
            renderWaitEvidence.coarseSleepCount =
                optionalUnsignedField(
                    fields, columns.renderWaitCoarseSleepCount);
            renderWaitEvidence.coarseSleepRequestedUs =
                optionalUnsignedField(
                    fields,
                    columns.renderWaitCoarseRequestedTotalUs);
            renderWaitEvidence.coarseSleepRequestedWakeUs =
                optionalUnsignedField(
                    fields,
                    columns.renderWaitCoarseRequestedWakeUs);
            renderWaitEvidence.coarseSleepReturnUs =
                optionalUnsignedField(
                    fields, columns.renderWaitCoarseReturnUs);
            renderWaitEvidence.coarseSleepClockStalled =
                optionalUnsignedField(
                    fields,
                    columns.renderWaitCoarseClockStalled) != 0;
            renderWaitEvidence.activeWaitEntered =
                optionalUnsignedField(
                    fields, columns.renderWaitActiveEntered) != 0;
            renderWaitEvidence.activeWaitStartUs =
                optionalUnsignedField(
                    fields, columns.renderWaitActiveStartUs);
            renderWaitEvidence.activeWaitLimitUs =
                optionalUnsignedField(
                    fields, columns.renderWaitActiveLimitUs);
            renderWaitEvidence.activeWaitYieldCount =
                optionalUnsignedField(
                    fields, columns.renderWaitActiveYieldCount);
            renderWaitEvidence.activeWaitClockStalled =
                optionalUnsignedField(
                    fields,
                    columns.renderWaitActiveClockStalled) != 0;
            renderWaitEvidence.activeWaitYieldLimitReached =
                optionalUnsignedField(
                    fields,
                    columns.renderWaitActiveYieldLimitReached) != 0;
            renderWaitEvidence.schedulerDelayUs =
                renderSchedulerDelayUs;
            renderWaitEvidence.schedulerDelayValid =
                renderSchedulerDelayValid;
            renderWaitEvidence.deadlineAlreadyElapsed =
                renderDeadlineAlreadyElapsed;
            const bool renderWaitTelemetryPresent =
                renderWaitEntryUs != 0 || renderWaitFinalUs != 0 ||
                renderWaitOvershootUs != 0 ||
                renderSchedulerDelayUs != 0 ||
                renderSchedulerDelayValid ||
                renderDeadlineAlreadyElapsed;
            bool renderWaitTelemetryValid = true;
            if (renderWaitTelemetryPresent) {
                const uint64_t renderStartUs = unsignedField(
                    fields, columns.renderStartUs);
                renderWaitTelemetryValid =
                    renderStartUs != 0 &&
                    renderWaitEntryUs != 0 &&
                    renderWaitFinalUs >= renderWaitEntryUs &&
                    (renderSchedulerDelayValid ||
                     renderSchedulerDelayUs == 0);
                if (renderDeadlineAlreadyElapsed) {
                    renderWaitTelemetryValid =
                        renderWaitTelemetryValid &&
                        renderWaitFinalUs >= renderStartUs &&
                        renderWaitOvershootUs == 0;
                }
                else {
                    renderWaitTelemetryValid =
                        renderWaitTelemetryValid &&
                        renderWaitOvershootUs == positiveDifference(
                            renderWaitFinalUs, renderStartUs);
                }
            }
            metrics.renderWaitTelemetryMismatchRows +=
                renderWaitTelemetryValid ? 0 : 1;

            const uint64_t targetWaitEntryUs = optionalUnsignedField(
                fields, columns.targetWaitEntryUs);
            const uint64_t targetWaitFinalUs = optionalUnsignedField(
                fields, columns.targetWaitFinalUs);
            const uint64_t targetWaitOvershootUs = optionalUnsignedField(
                fields, columns.targetWaitOvershootUs);
            const uint64_t targetSchedulerDelayUs = optionalUnsignedField(
                fields, columns.targetSchedulerDelayUs);
            const bool targetSchedulerDelayValid =
                optionalUnsignedField(
                    fields, columns.targetSchedulerDelayValid) != 0;
            const bool targetDeadlineAlreadyElapsed =
                optionalUnsignedField(
                    fields, columns.targetDeadlineAlreadyElapsed) != 0;
            targetWaitEvidence.callEntryUs = targetWaitEntryUs;
            targetWaitEvidence.deadlineUs = unsignedField(
                fields, columns.recordedTargetUs);
            targetWaitEvidence.additionalWakeLeadUs =
                unsignedField(fields, columns.targetWakeLeadUs);
            targetWaitEvidence.initialNowUs = optionalUnsignedField(
                fields, columns.targetWaitInitialUs);
            targetWaitEvidence.finalNowUs = targetWaitFinalUs;
            targetWaitEvidence.activeWaitUs = optionalUnsignedField(
                fields, columns.targetWaitActiveBudgetUs);
            targetWaitEvidence.coarseSleepCount =
                optionalUnsignedField(
                    fields, columns.targetWaitCoarseSleepCount);
            targetWaitEvidence.coarseSleepRequestedUs =
                optionalUnsignedField(
                    fields,
                    columns.targetWaitCoarseRequestedTotalUs);
            targetWaitEvidence.coarseSleepRequestedWakeUs =
                optionalUnsignedField(
                    fields,
                    columns.targetWaitCoarseRequestedWakeUs);
            targetWaitEvidence.coarseSleepReturnUs =
                optionalUnsignedField(
                    fields, columns.targetWaitCoarseReturnUs);
            targetWaitEvidence.coarseSleepClockStalled =
                optionalUnsignedField(
                    fields,
                    columns.targetWaitCoarseClockStalled) != 0;
            targetWaitEvidence.activeWaitEntered =
                optionalUnsignedField(
                    fields, columns.targetWaitActiveEntered) != 0;
            targetWaitEvidence.activeWaitStartUs =
                optionalUnsignedField(
                    fields, columns.targetWaitActiveStartUs);
            targetWaitEvidence.activeWaitLimitUs =
                optionalUnsignedField(
                    fields, columns.targetWaitActiveLimitUs);
            targetWaitEvidence.activeWaitYieldCount =
                optionalUnsignedField(
                    fields, columns.targetWaitActiveYieldCount);
            targetWaitEvidence.activeWaitClockStalled =
                optionalUnsignedField(
                    fields,
                    columns.targetWaitActiveClockStalled) != 0;
            targetWaitEvidence.activeWaitYieldLimitReached =
                optionalUnsignedField(
                    fields,
                    columns.targetWaitActiveYieldLimitReached) != 0;
            targetWaitEvidence.schedulerDelayUs =
                targetSchedulerDelayUs;
            targetWaitEvidence.schedulerDelayValid =
                targetSchedulerDelayValid;
            targetWaitEvidence.deadlineAlreadyElapsed =
                targetDeadlineAlreadyElapsed;
            const bool targetWaitTelemetryPresent =
                targetWaitEntryUs != 0 || targetWaitFinalUs != 0 ||
                targetWaitOvershootUs != 0 ||
                targetSchedulerDelayUs != 0 ||
                targetSchedulerDelayValid ||
                targetDeadlineAlreadyElapsed;
            bool targetWaitTelemetryValid = true;
            if (targetWaitTelemetryPresent) {
                const uint64_t targetUs = unsignedField(
                    fields, columns.recordedTargetUs);
                targetWaitTelemetryValid =
                    targetUs != 0 &&
                    targetWaitEntryUs != 0 &&
                    targetWaitFinalUs >= targetWaitEntryUs &&
                    (targetSchedulerDelayValid ||
                     targetSchedulerDelayUs == 0) &&
                    (!targetDeadlineAlreadyElapsed ||
                     targetWaitFinalUs >= targetUs);
            }
            metrics.targetWaitTelemetryMismatchRows +=
                targetWaitTelemetryValid ? 0 : 1;
            if (metrics.waitLifecycleTelemetryAvailable &&
                    renderWaitTelemetryPresent) {
                ++metrics.renderWaitLifecycleRows;
                const VrrWaitLifecycleAudit audit =
                    evaluateVrrWaitLifecycle(
                        renderWaitEvidence,
                        kWaiterMaximumActiveWaitUs,
                        kWaiterMaximumAdditionalWakeLeadUs);
                metrics.renderWaitLifecycleRelationshipMismatchRows +=
                    audit.relationshipValid ? 0 : 1;
                metrics.renderWaitCleanCompletionRows +=
                    audit.cleanCompletion ? 1 : 0;
                metrics.renderWaitCoarseSleepRows +=
                    renderWaitEvidence.coarseSleepCount != 0 ? 1 : 0;
                metrics.renderWaitClockStallRows +=
                    renderWaitEvidence.coarseSleepClockStalled ||
                    renderWaitEvidence.activeWaitClockStalled ? 1 : 0;
                metrics.renderWaitYieldLimitRows +=
                    renderWaitEvidence.activeWaitYieldLimitReached ? 1 : 0;
                metrics.renderWaitEarlyReturnRows +=
                    audit.completedDeadline ? 0 : 1;
                if (renderWaitEvidence.coarseSleepCount != 0) {
                    metrics.observedRenderCoarseWakeOffset.add(
                        signedDifference(
                            renderWaitEvidence.coarseSleepReturnUs,
                            renderWaitEvidence.
                                coarseSleepRequestedWakeUs));
                }
                if (renderWaitEvidence.activeWaitEntered) {
                    metrics.observedRenderActiveYieldCount.add(
                        renderWaitEvidence.activeWaitYieldCount);
                }
            }
            if (metrics.waitLifecycleTelemetryAvailable &&
                    targetWaitTelemetryPresent &&
                    normalPresentAttempt) {
                ++metrics.targetWaitLifecycleRows;
                const VrrWaitLifecycleAudit audit =
                    evaluateVrrWaitLifecycle(
                        targetWaitEvidence,
                        kWaiterMaximumActiveWaitUs,
                        kWaiterMaximumAdditionalWakeLeadUs);
                metrics.targetWaitLifecycleRelationshipMismatchRows +=
                    audit.relationshipValid ? 0 : 1;
                metrics.targetWaitCleanCompletionRows +=
                    audit.cleanCompletion ? 1 : 0;
                metrics.targetWaitCoarseSleepRows +=
                    targetWaitEvidence.coarseSleepCount != 0 ? 1 : 0;
                metrics.targetWaitClockStallRows +=
                    targetWaitEvidence.coarseSleepClockStalled ||
                    targetWaitEvidence.activeWaitClockStalled ? 1 : 0;
                metrics.targetWaitYieldLimitRows +=
                    targetWaitEvidence.activeWaitYieldLimitReached ? 1 : 0;
                metrics.targetWaitEarlyReturnRows +=
                    audit.completedDeadline ? 0 : 1;
                if (targetWaitEvidence.coarseSleepCount != 0) {
                    metrics.observedTargetCoarseWakeOffset.add(
                        signedDifference(
                            targetWaitEvidence.coarseSleepReturnUs,
                            targetWaitEvidence.
                                coarseSleepRequestedWakeUs));
                }
                if (targetWaitEvidence.activeWaitEntered) {
                    metrics.observedTargetActiveYieldCount.add(
                        targetWaitEvidence.activeWaitYieldCount);
                }
            }
        }
        const uint64_t rowPrePresentSyncSampleUs = optionalUnsignedField(
            fields, columns.frameStatsBeforeTimeUs);
        const uint64_t rowPrePresentSyncRefreshSequence =
            optionalUnsignedField(
                fields, columns.frameStatsBeforeSyncRefreshSequence);
        metrics.prePresentAnchorsSuppressedByEpoch +=
            displayEpochNeedsPostObservation &&
                optionalUnsignedField(
                    fields, columns.frameStatsBeforeValid) != 0 &&
                rowPrePresentSyncSampleUs != 0 ? 1 : 0;
        const bool rawPrePresentSyncTimestampValid =
            !displayEpochNeedsPostObservation &&
            optionalUnsignedField(fields, columns.frameStatsBeforeValid) != 0 &&
            rowPrePresentSyncSampleUs != 0;
        metrics.prePresentAnchorMissingRefreshSequence +=
            rawPrePresentSyncTimestampValid &&
                rowPrePresentSyncRefreshSequence == 0 ? 1 : 0;
        const bool rawPrePresentSyncSampleValid =
            rawPrePresentSyncTimestampValid &&
            rowPrePresentSyncRefreshSequence != 0;
        bool prePresentSyncAnchorIntegrityValid =
            rawPrePresentSyncSampleValid;
        bool resetRasterSyncAnchorHistory = false;
        const auto mergeRasterSyncAnchor =
            [&](const RasterSyncAnchor& anchor) {
                const bool havePrevious =
                    !rasterSyncAnchors.empty();
                const RasterSyncAnchor previous =
                    havePrevious ?
                        rasterSyncAnchors.back() :
                        RasterSyncAnchor {};
                const VrrRasterSyncAnchorMergeResult merge =
                    evaluateVrrRasterSyncAnchorMerge(
                        havePrevious,
                        previous.refreshSequence,
                        previous.timeUs,
                        anchor.refreshSequence,
                        anchor.timeUs,
                        optionalUnsignedField(
                            fields, columns.displayPeriodUs),
                        kSyncAnchorTranslationJitterToleranceUs,
                        kSyncAnchorMinimumIntervalToleranceUs);
                switch (merge.status) {
                case VrrRasterSyncAnchorMergeStatus::
                        SequenceRegression:
                    ++metrics.unifiedAnchorSequenceRegressions;
                    break;
                case VrrRasterSyncAnchorMergeStatus::
                        SameRefreshTimestampMismatch:
                    ++metrics.
                        unifiedAnchorSameSequenceTimestampMismatches;
                    break;
                case VrrRasterSyncAnchorMergeStatus::
                        NonadvancingTimestamp:
                    ++metrics.unifiedAnchorNonadvancingTime;
                    break;
                case VrrRasterSyncAnchorMergeStatus::
                        ImplausiblyShortInterval:
                    ++metrics.unifiedAnchorImplausiblyShortIntervals;
                    break;
                case VrrRasterSyncAnchorMergeStatus::
                        ReplacedSameRefresh:
                    rasterSyncAnchors.back() = anchor;
                    return true;
                case VrrRasterSyncAnchorMergeStatus::Appended:
                    rasterSyncAnchors.push_back(anchor);
                    while (rasterSyncAnchors.size() >
                            kMaximumRasterSyncAnchors) {
                        rasterSyncAnchors.pop_front();
                    }
                    return true;
                }
                return false;
            };
        if (rawPrePresentSyncSampleValid) {
            if (rowPrePresentSyncSampleUs >
                    saturatingAdd(
                        decisionUs,
                        kSyncAnchorTranslationJitterToleranceUs)) {
                ++metrics.prePresentAnchorCausalOrderViolations;
                prePresentSyncAnchorIntegrityValid = false;
            }
            if (havePrePresentSyncAnchor) {
                if (rowPrePresentSyncRefreshSequence <
                        priorPrePresentSyncRefreshSequence) {
                    ++metrics.prePresentAnchorSequenceRegressions;
                    prePresentSyncAnchorIntegrityValid = false;
                    resetRasterSyncAnchorHistory = true;
                }
                const uint64_t timestampDifferenceUs = absoluteValue(
                    signedDifference(rowPrePresentSyncSampleUs,
                                     priorPrePresentSyncSampleUs));
                if (rowPrePresentSyncRefreshSequence ==
                        priorPrePresentSyncRefreshSequence) {
                    metrics.repeatedSyncAnchorTimestampJitter.add(
                        timestampDifferenceUs);
                    if (timestampDifferenceUs >
                            kSyncAnchorTranslationJitterToleranceUs) {
                        ++metrics.
                            prePresentAnchorTimestampJitterBeyondTolerance;
                        prePresentSyncAnchorIntegrityValid = false;
                    }
                }
                else if (rowPrePresentSyncRefreshSequence >
                        priorPrePresentSyncRefreshSequence) {
                    if (rowPrePresentSyncSampleUs <=
                            priorPrePresentSyncSampleUs) {
                        ++metrics.prePresentAnchorNonadvancingTime;
                        prePresentSyncAnchorIntegrityValid = false;
                        resetRasterSyncAnchorHistory = true;
                    }
                    else {
                        const uint64_t refreshDelta =
                            rowPrePresentSyncRefreshSequence -
                            priorPrePresentSyncRefreshSequence;
                        const uint64_t elapsedUs =
                            rowPrePresentSyncSampleUs -
                            priorPrePresentSyncSampleUs;
                        metrics.syncAnchorRefreshDelta.add(refreshDelta);
                        metrics.syncAnchorElapsedUs.add(elapsedUs);
                        const uint64_t meanIntervalUs =
                            elapsedUs / refreshDelta;
                        metrics.syncAnchorMeanIntervalUs.add(meanIntervalUs);
                        const uint64_t recordedDisplayPeriodUs =
                            optionalUnsignedField(
                                fields, columns.displayPeriodUs);
                        if (recordedDisplayPeriodUs != 0 &&
                                saturatingAdd(
                                    meanIntervalUs,
                                    kSyncAnchorMinimumIntervalToleranceUs) <
                                    recordedDisplayPeriodUs) {
                            ++metrics.
                                prePresentAnchorImplausiblyShortIntervals;
                            prePresentSyncAnchorIntegrityValid = false;
                            resetRasterSyncAnchorHistory = true;
                        }
                    }
                }
                if (rowPrePresentSyncSampleUs <
                        priorPrePresentSyncSampleUs &&
                         priorPrePresentSyncSampleUs -
                             rowPrePresentSyncSampleUs >
                                 kSyncAnchorTranslationJitterToleranceUs) {
                    ++metrics.prePresentAnchorTimeRegressions;
                    prePresentSyncAnchorIntegrityValid = false;
                    resetRasterSyncAnchorHistory = true;
                }
            }
            havePrePresentSyncAnchor = true;
            priorPrePresentSyncRefreshSequence =
                rowPrePresentSyncRefreshSequence;
            priorPrePresentSyncSampleUs = rowPrePresentSyncSampleUs;
            if (resetRasterSyncAnchorHistory) {
                rasterSyncAnchors.clear();
            }
            if (prePresentSyncAnchorIntegrityValid) {
                const RasterSyncAnchor anchor {
                    rowPrePresentSyncRefreshSequence,
                    rowPrePresentSyncSampleUs,
                    arrivalSequence,
                    false,
                };
                prePresentSyncAnchorIntegrityValid =
                    mergeRasterSyncAnchor(anchor);
            }
        }
        const uint64_t rowPostPresentSyncRefreshSequence =
            optionalUnsignedField(
                fields, columns.latchSyncRefreshSequence);
        bool postPresentSyncAnchorIntegrityValid = false;
        bool resetRasterSyncAnchorHistoryFromPost = false;
        if (latchSampleValid) {
            if (rowPostPresentSyncRefreshSequence == 0 ||
                    rowLatchTimeUs == 0) {
                ++metrics.postPresentAnchorMissingRefreshSequence;
            }
            else {
                postPresentSyncAnchorIntegrityValid = true;
                const uint64_t workerPresentEndUs =
                    optionalUnsignedField(
                        fields, columns.presentEndUs);
                if (workerPresentEndUs == 0 ||
                        rowLatchTimeUs > saturatingAdd(
                            workerPresentEndUs,
                            kSyncAnchorTranslationJitterToleranceUs)) {
                    ++metrics.postPresentAnchorCausalOrderViolations;
                    postPresentSyncAnchorIntegrityValid = false;
                }
                if (havePostPresentSyncAnchor) {
                    const uint64_t timestampDifferenceUs =
                        absoluteValue(signedDifference(
                            rowLatchTimeUs,
                            priorPostPresentSyncSampleUs));
                    if (rowPostPresentSyncRefreshSequence <
                            priorPostPresentSyncRefreshSequence) {
                        ++metrics.postPresentAnchorSequenceRegressions;
                        postPresentSyncAnchorIntegrityValid = false;
                        resetRasterSyncAnchorHistoryFromPost = true;
                    }
                    else if (rowPostPresentSyncRefreshSequence ==
                            priorPostPresentSyncRefreshSequence) {
                        metrics.repeatedPostSyncAnchorTimestampJitter.add(
                            timestampDifferenceUs);
                        if (timestampDifferenceUs >
                                kSyncAnchorTranslationJitterToleranceUs) {
                            ++metrics.
                                postPresentAnchorTimestampJitterBeyondTolerance;
                            postPresentSyncAnchorIntegrityValid = false;
                        }
                    }
                    else if (rowLatchTimeUs <=
                            priorPostPresentSyncSampleUs) {
                        ++metrics.postPresentAnchorNonadvancingTime;
                        postPresentSyncAnchorIntegrityValid = false;
                        resetRasterSyncAnchorHistoryFromPost = true;
                    }
                    else {
                        const uint64_t refreshDelta =
                            rowPostPresentSyncRefreshSequence -
                            priorPostPresentSyncRefreshSequence;
                        const uint64_t elapsedUs =
                            rowLatchTimeUs -
                            priorPostPresentSyncSampleUs;
                        const uint64_t meanIntervalUs =
                            elapsedUs / refreshDelta;
                        metrics.postSyncAnchorRefreshDelta.add(
                            refreshDelta);
                        metrics.postSyncAnchorElapsedUs.add(elapsedUs);
                        metrics.postSyncAnchorMeanIntervalUs.add(
                            meanIntervalUs);
                        const uint64_t recordedDisplayPeriodUs =
                            optionalUnsignedField(
                                fields, columns.displayPeriodUs);
                        if (recordedDisplayPeriodUs != 0 &&
                                saturatingAdd(
                                    meanIntervalUs,
                                    kSyncAnchorMinimumIntervalToleranceUs) <
                                    recordedDisplayPeriodUs) {
                            ++metrics.
                                postPresentAnchorImplausiblyShortIntervals;
                            postPresentSyncAnchorIntegrityValid = false;
                            resetRasterSyncAnchorHistoryFromPost = true;
                        }
                    }
                    if (rowLatchTimeUs <
                            priorPostPresentSyncSampleUs &&
                            priorPostPresentSyncSampleUs -
                                rowLatchTimeUs >
                                    kSyncAnchorTranslationJitterToleranceUs) {
                        ++metrics.postPresentAnchorTimeRegressions;
                        postPresentSyncAnchorIntegrityValid = false;
                        resetRasterSyncAnchorHistoryFromPost = true;
                    }
                }
                havePostPresentSyncAnchor = true;
                priorPostPresentSyncRefreshSequence =
                    rowPostPresentSyncRefreshSequence;
                priorPostPresentSyncSampleUs = rowLatchTimeUs;
                if (resetRasterSyncAnchorHistoryFromPost) {
                    rasterSyncAnchors.clear();
                }
                if (postPresentSyncAnchorIntegrityValid) {
                    const RasterSyncAnchor anchor {
                        rowPostPresentSyncRefreshSequence,
                        rowLatchTimeUs,
                        arrivalSequence,
                        true,
                    };
                    postPresentSyncAnchorIntegrityValid =
                        mergeRasterSyncAnchor(anchor);
                    if (postPresentSyncAnchorIntegrityValid) {
                        displayEpochNeedsPostObservation = false;
                    }
                }
            }
        }
        const auto validateNativeRasterModelSample =
            [&](bool queryResultDeclared, int64_t queryResult,
                uint64_t queryStartUs, uint64_t queryEndUs,
                bool inVerticalBlank, uint64_t scanLine) {
                if (!queryResultDeclared || queryResult != 0) {
                    return;
                }
                ++metrics.nativeRasterModelObservationSamples;
                uint64_t normalizedScanLine = 0;
                if (!metrics.nativeRasterScanLineScaleInference.valid ||
                        !normalizeVrrRasterScanLine(
                            scanLine,
                            metrics.
                                nativeRasterScanLineScaleInference.scale,
                            normalizedScanLine)) {
                    return;
                }
                if (queryStartUs == 0 ||
                        queryEndUs < queryStartUs) {
                    return;
                }
                const uint64_t queryDurationUs =
                    queryEndUs - queryStartUs;
                const uint64_t sampleTimeUs =
                    queryStartUs + queryDurationUs / 2;
                const RasterSyncAnchor* selectedAnchor = nullptr;
                for (auto it = rasterSyncAnchors.crbegin();
                        it != rasterSyncAnchors.crend(); ++it) {
                    if (it->timeUs <= sampleTimeUs) {
                        selectedAnchor = &*it;
                        break;
                    }
                }
                if (selectedAnchor == nullptr ||
                        selectedAnchor->timeUs == 0) {
                    return;
                }
                const uint64_t anchorTimeUs =
                    selectedAnchor->timeUs;
                ++metrics.nativeRasterModelAnchoredSamples;
                metrics.nativeRasterModelAnchorAgeUs.add(
                    sampleTimeUs - anchorTimeUs);
                const bool currentPostAnchor =
                    selectedAnchor->postObservation &&
                    selectedAnchor->arrivalSequence ==
                        arrivalSequence;
                metrics.nativeRasterModelCurrentPostAnchorSamples +=
                    currentPostAnchor ? 1 : 0;
                metrics.nativeRasterModelPriorAnchorSamples +=
                    currentPostAnchor ? 0 : 1;

                VrrReplayDisplayParameters observationParameters =
                    rasterDisplayParameters;
                observationParameters.presentTransportUs = 0;
                if (nativeDisplaySignalVerticalActivePsValid) {
                    // D3DKMT's InVerticalBlank transition occurs after the
                    // complete final active line. Tear exposure deliberately
                    // remains based on the final visible pixel instead.
                    observationParameters.activeScanoutUs = 0;
                    observationParameters.activeScanoutPs =
                        nativeDisplaySignalVerticalActivePs;
                }
                const uint64_t queryHalfSpanUs =
                    queryDurationUs / 2 +
                    queryDurationUs % 2;
                observationParameters.phaseUncertaintyUs =
                    saturatingAdd(
                        observationParameters.phaseUncertaintyUs,
                        saturatingAdd(queryHalfSpanUs, 1));
                const VrrRasterPhaseResult prediction =
                    evaluateVrrRasterPhase(
                        true, false, sampleTimeUs, anchorTimeUs,
                        optionalUnsignedField(
                            fields, columns.displayPeriodUs),
                        optionalUnsignedField(
                            fields, columns.sourcePeriodUs),
                        observationParameters);
                const bool observedActive = !inVerticalBlank;
                ++metrics.nativeRasterVrrLockedObservationCrossTable[
                    rasterObservationCrossKey(
                        observedActive, prediction.vrrLocked)];
                ++metrics.nativeRasterFreeRunningObservationCrossTable[
                    rasterObservationCrossKey(
                        observedActive, prediction.freeRunning)];

                const VrrRasterScanLineAudit vrrScanLineAudit =
                    evaluateVrrRasterScanLine(
                        inVerticalBlank, prediction.vrrLocked,
                        nativeDisplaySignalVerticalActivePsValid &&
                            prediction.vrrLockedPhaseValid,
                        prediction.vrrLockedPhasePs,
                        prediction.resolvedScanoutPeriodPs,
                        prediction.resolvedPhaseUncertaintyPs,
                        nativeDisplaySignalActiveHeight,
                        nativeDisplaySignalTotalHeight,
                        normalizedScanLine);
                const VrrRasterScanLineAudit freeRunningScanLineAudit =
                    evaluateVrrRasterScanLine(
                        inVerticalBlank, prediction.freeRunning,
                        nativeDisplaySignalVerticalActivePsValid &&
                            prediction.freeRunningPhaseValid,
                        prediction.freeRunningPhasePs,
                        prediction.resolvedScanoutPeriodPs,
                        prediction.resolvedPhaseUncertaintyPs,
                        nativeDisplaySignalActiveHeight,
                        nativeDisplaySignalTotalHeight,
                        normalizedScanLine);
                const bool vrrScanLineRequired =
                    observedActive &&
                    prediction.vrrLocked ==
                        VrrRasterPhaseState::Active;
                const bool freeRunningScanLineRequired =
                    observedActive &&
                    prediction.freeRunning ==
                        VrrRasterPhaseState::Active;
                const bool vrrComparable =
                    rasterStateComparable(prediction.vrrLocked);
                const bool vrrComparisonComplete =
                    vrrComparable &&
                    (!vrrScanLineRequired ||
                     vrrScanLineAudit.comparable);
                const bool freeRunningComparable =
                    rasterStateComparable(prediction.freeRunning);
                const bool freeRunningComparisonComplete =
                    freeRunningComparable &&
                    (!freeRunningScanLineRequired ||
                     freeRunningScanLineAudit.comparable);
                const bool vrrMatches =
                    vrrComparisonComplete &&
                    rasterStateMatchesObservation(
                        prediction.vrrLocked, observedActive) &&
                    (!vrrScanLineRequired ||
                     vrrScanLineAudit.matches);
                const bool freeRunningMatches =
                    freeRunningComparisonComplete &&
                    rasterStateMatchesObservation(
                        prediction.freeRunning, observedActive) &&
                    (!freeRunningScanLineRequired ||
                     freeRunningScanLineAudit.matches);
                metrics.nativeRasterVrrLockedComparableSamples +=
                    vrrComparisonComplete ? 1 : 0;
                metrics.nativeRasterVrrLockedContradictions +=
                    vrrComparisonComplete && !vrrMatches ? 1 : 0;
                metrics.nativeRasterFreeRunningComparableSamples +=
                    freeRunningComparisonComplete ? 1 : 0;
                metrics.nativeRasterFreeRunningContradictions +=
                    freeRunningComparisonComplete &&
                        !freeRunningMatches ? 1 : 0;
                metrics.nativeRasterEnvelopeComparableSamples +=
                    vrrComparisonComplete &&
                        freeRunningComparisonComplete ? 1 : 0;
                const bool envelopeContradiction =
                    vrrComparisonComplete &&
                    freeRunningComparisonComplete &&
                    !vrrMatches && !freeRunningMatches;
                metrics.nativeRasterEnvelopeContradictions +=
                    envelopeContradiction ? 1 : 0;

                if (vrrScanLineAudit.comparable) {
                    ++metrics.
                        nativeRasterVrrLockedScanLineComparableSamples;
                    metrics.nativeRasterVrrLockedScanLineMismatches +=
                        vrrScanLineAudit.matches ? 0 : 1;
                    metrics.nativeRasterVrrLockedPredictedScanLine.add(
                        vrrScanLineAudit.predictedScanLine);
                    metrics.
                        nativeRasterVrrLockedScanLineAbsoluteError.add(
                            vrrScanLineAudit.absoluteErrorLines);
                    metrics.nativeRasterVrrLockedScanLineResidual.add(
                        vrrScanLineAudit.signedErrorLines);
                    metrics.nativeRasterVrrLockedScanLineTolerance.add(
                        vrrScanLineAudit.toleranceLines);
                    const int sourceRateHz = roundedRateForPeriod(
                        optionalUnsignedField(
                            fields, columns.sourcePeriodUs));
                    forRateBands(
                        sourceRateHz,
                        [&](RateBandIndex index) {
                            NativeRasterScanLineBandMetrics& band =
                                metrics.
                                    nativeRasterVrrLockedScanLineBands[
                                        index];
                            ++band.comparableSamples;
                            band.mismatches +=
                                vrrScanLineAudit.matches ? 0 : 1;
                            band.absoluteError.add(
                                vrrScanLineAudit.absoluteErrorLines);
                            band.residual.add(
                                vrrScanLineAudit.signedErrorLines);
                            band.tolerance.add(
                                vrrScanLineAudit.toleranceLines);
                        });
                }
                if (freeRunningScanLineAudit.comparable) {
                    ++metrics.
                        nativeRasterFreeRunningScanLineComparableSamples;
                    metrics.nativeRasterFreeRunningScanLineMismatches +=
                        freeRunningScanLineAudit.matches ? 0 : 1;
                    metrics.nativeRasterFreeRunningPredictedScanLine.add(
                        freeRunningScanLineAudit.predictedScanLine);
                    metrics.
                        nativeRasterFreeRunningScanLineAbsoluteError.add(
                            freeRunningScanLineAudit.absoluteErrorLines);
                    metrics.nativeRasterFreeRunningScanLineResidual.add(
                        freeRunningScanLineAudit.signedErrorLines);
                    metrics.nativeRasterFreeRunningScanLineTolerance.add(
                        freeRunningScanLineAudit.toleranceLines);
                }
                if (vrrScanLineAudit.comparable ||
                        freeRunningScanLineAudit.comparable) {
                    ++metrics.
                        nativeRasterScanLineEnvelopeComparableSamples;
                    metrics.nativeRasterScanLineEnvelopeMatchedSamples +=
                        vrrScanLineAudit.matches ||
                            freeRunningScanLineAudit.matches ? 1 : 0;
                    metrics.
                        nativeRasterScanLineEnvelopeContradictions +=
                            envelopeContradiction ? 1 : 0;
                }
            };
        validateNativeRasterModelSample(
            nativeRasterBeforeQueryResultDeclared,
            nativeRasterBeforeQueryResult,
            nativeRasterBeforeQueryStartUs,
            nativeRasterBeforeQueryEndUs,
            nativeRasterBeforeInVerticalBlank,
            nativeRasterBeforeScanLine);
        validateNativeRasterModelSample(
            nativeRasterAfterQueryResultDeclared,
            nativeRasterAfterQueryResult,
            nativeRasterAfterQueryStartUs,
            nativeRasterAfterQueryEndUs,
            nativeRasterAfterInVerticalBlank,
            nativeRasterAfterScanLine);
        metrics.observedDecodeToArrival.addElapsed(pacerArrivalUs,
                                                   decoderOutputUs);
        metrics.observedArrivalToDequeue.addElapsed(dequeueUs,
                                                    pacerArrivalUs);
        metrics.observedDequeueToDecision.addElapsed(decisionUs, dequeueUs);
        const qint64 rowDisplayRefreshValue = signedField(
            fields, columns.displayRefreshHz);
        const qint64 rowStreamRateValue = signedField(
            fields, columns.streamRateHz);
        if (rowDisplayRefreshValue <= 0 ||
                rowDisplayRefreshValue >
                    std::numeric_limits<int>::max() ||
                rowStreamRateValue <= 0 ||
                rowStreamRateValue >
                    std::numeric_limits<int>::max()) {
            std::fprintf(
                stderr,
                "Trace row %llu has an invalid display or stream rate\n",
                static_cast<unsigned long long>(metrics.delivered + 1));
            return 1;
        }
        const int rowDisplayRefreshHz =
            static_cast<int>(rowDisplayRefreshValue);
        const int rowStreamRateHz =
            static_cast<int>(rowStreamRateValue);
        const bool rowCanLatch =
            unsignedField(fields, columns.canLatch) != 0;
        const bool rowAdditionalQueuedFrame = optionalUnsignedField(
            fields, columns.additionalQueuedFrame) != 0;
        if (periodForRate(rowDisplayRefreshHz) == 0 ||
                periodForRate(rowStreamRateHz) == 0) {
            std::fprintf(
                stderr,
                "Trace row %llu has an invalid display or stream rate\n",
                static_cast<unsigned long long>(metrics.delivered + 1));
            return 1;
        }
        if (rowDisplayRefreshHz > 0 &&
                unsignedField(fields, columns.displayPeriodUs) !=
                    periodForRate(rowDisplayRefreshHz)) {
            ++metrics.displayPeriodMismatchRows;
        }
        if (capturedConfig.displayRefreshHz == 0) {
            capturedConfig.displayRefreshHz = rowDisplayRefreshHz;
            capturedConfig.streamRateHz = rowStreamRateHz;
            capturedConfig.allowAdditionalQueuedFrame =
                rowAdditionalQueuedFrame;
            capturedConfig.allowTearing = rowAllowTearing;
            capturedCanLatch = rowCanLatch;
            if (traceSchema < 5) {
                // Schema-3/4 rows predate captured controller parameters.
                // Reconstruct the absolute latch policy used to record them
                // instead of inheriting current production defaults.
                capturedParameters.latchedPresentationHeadroomUs = 1500;
                capturedParameters.latchedPresentationExitHeadroomUs = 2000;
                capturedParameters.
                    latchedPresentationHeadroomPeriodNumerator = 0;
                capturedParameters.
                    latchedPresentationHeadroomPeriodDenominator = 1;
                capturedParameters.
                    latchedPresentationExitHeadroomPeriodNumerator = 0;
                capturedParameters.
                    latchedPresentationExitHeadroomPeriodDenominator = 1;
                capturedParameters.latchedPresentationBaseGuardExit = 1;
            }
            else {
                QJsonObject capturedControllerSnapshot;
                int expectedParameterColumns = 0;
                for (const QString& path : vrrReplayParameterNames()) {
                    if (!path.startsWith("controller.")) continue;
                    const auto column = columns.capturedParameterColumns.find(
                        path);
                    uint64_t capturedValue = 0;
                    if (column == columns.capturedParameterColumns.end()) {
                        // Preserve the policy semantics of schema-5 fields
                        // added after the initial release. Older captures use
                        // absolute latch thresholds, allow base-guard latch
                        // exit, and predate render-tail pacing budgets; new
                        // traces record all of these explicitly.
                        QString legacyValue;
                        if (path ==
                                "controller.render_baseline_percentile") {
                            legacyValue = "50";
                        }
                        else if (path ==
                                 "controller.pacing_latency_budget_divisor") {
                            legacyValue = "0";
                        }
                        else if (path.endsWith("_period_numerator")) {
                            legacyValue = "0";
                        }
                        else if (path.endsWith("_period_denominator")) {
                            legacyValue = "1";
                        }
                        else if (path ==
                                 "controller.latch_base_guard_exit") {
                            legacyValue = "1";
                        }
                        else if (path ==
                                 "controller.cadence_stability_latch_frames") {
                            // Captures made before cadence-instability
                            // protection must retain their recorded adaptive
                            // presentation decisions for exact replay.
                            legacyValue = "0";
                        }
                        else if (path ==
                                 "controller.source_playout_delay_us") {
                            // Older schema-5 captures predate the explicit
                            // source-clock playout reserve.
                            legacyValue = "0";
                        }
                        else if (path ==
                                 "controller.readiness_learning_window_us" ||
                                 path ==
                                 "controller.retain_readiness_on_phase_reset") {
                            // Older schema-5 captures used a fixed sample
                            // count and reacquired readiness after each phase
                            // reset.
                            legacyValue = "0";
                        }
                        else if (path ==
                                 "controller.timestamp_playout_enabled") {
                            // Older schema-5 captures predate timestamp
                            // playout and always projected a source clock.
                            legacyValue = "0";
                        }
                        else if (path ==
                                 "controller.playout_offset_window_us") {
                            legacyValue = "3000000";
                        }
                        else if (path ==
                                 "controller.playout_offset_slew_us") {
                            legacyValue = "20";
                        }
                        else if (path ==
                                 "controller.playout_offset_warmup_samples") {
                            legacyValue = "64";
                        }
                        else {
                            // Any parameter added after a capture was made
                            // takes its schema default: older captures ran
                            // the controller exactly as if the parameter
                            // did not exist, which is what the default
                            // expresses.
                            legacyValue =
                                vrrControllerParameterDefaultText(path);
                        }
                        bool validLegacyValue = false;
                        capturedValue = legacyValue.toULongLong(
                            &validLegacyValue);
                        if (legacyValue.isEmpty() || !validLegacyValue) {
                            std::fprintf(stderr,
                                         "Schema 5 trace is missing captured controller parameter: %s\n",
                                         qPrintable(path));
                            return 1;
                        }
                    }
                    else {
                        ++expectedParameterColumns;
                        capturedValue = unsignedField(
                            fields, column.value());
                        capturedParameterValues.insert(
                            column.value(), fields[column.value()]);
                    }
                    if (capturedValue > 9007199254740991ULL) {
                        std::fprintf(stderr,
                                     "Schema 5 trace has an inexact captured controller parameter: %s\n",
                                     qPrintable(path));
                        return 1;
                    }
                    capturedControllerSnapshot.insert(
                        path.section('.', 1, 1),
                        static_cast<double>(capturedValue));
                }
                if (columns.capturedParameterColumns.size() !=
                        expectedParameterColumns) {
                    std::fprintf(stderr,
                                 "Schema 5 trace is missing captured controller parameters\n");
                    return 1;
                }
                if (!applyVrrReplayControllerSnapshot(
                            capturedControllerSnapshot,
                            capturedParameters, error)) {
                    std::fprintf(stderr,
                                 "Schema 5 trace has invalid captured parameters: %s\n",
                                 qPrintable(error));
                    return 1;
                }
            }
            capturedConfig.latencyFix = capturedParameters.latencyFixEnabled != 0 &&
                capturedParameters.latencyFixAllRates == 0;
            capturedConfig.latencyMode = capturedParameters.latencyFixAllRates != 0 ?
                (capturedParameters.latencyFixDelayPeriodPerMille == 0 ? 2 : 1) : 0;
            simulatedConfig = capturedConfig;
            // Current policy is shared across backends. Exact replay below
            // still uses the captured parameters, including the retired Linux policy.
            simulatedConfig.readinessHitchFeedback = false;
            if (parser.isSet(displayOption)) {
                simulatedConfig.displayRefreshHz = displayOverrideHz;
            }
            if (parser.isSet(streamOption)) {
                simulatedConfig.streamRateHz = streamOverrideFps;
            }
            simulatedCanLatch = capturedCanLatch && !parser.isSet(latchOption);
            if (parser.isSet(exactOption)) {
                // The exact gate verifies the policy that produced the trace.
                // A normal session-policy replay deliberately uses today's
                // production defaults, which may differ from older captures.
                scenario.controller = capturedParameters;
            }
            else if (!scenario.controllerCustomized) {
                // Current preferences migrate the enabled legacy checkbox to
                // Balanced Target; exact/reference replay retains the recorded policy.
                if (simulatedConfig.latencyFix && simulatedConfig.latencyMode == 0) {
                    simulatedConfig.latencyFix = false;
                    simulatedConfig.latencyMode = 1;
                }
                scenario.controller = vrrTimingParametersForSession(
                    simulatedConfig);
            }
            referenceController = std::make_unique<VrrTimingController>(
                capturedConfig, capturedCanLatch, capturedParameters);
            simulatedController = std::make_unique<VrrTimingController>(
                simulatedConfig, simulatedCanLatch, scenario.controller);
            const int profileColumn = traceHeader.indexOf("playout_initial_profile");
            if (capturedParameters.playoutHistoryEnabled && profileColumn < 0) {
                std::fprintf(stderr, "History capture is missing its starting calibration\n");
                return 1;
            }
            if (profileColumn >= 0) {
                std::vector<int64_t> profile;
                if (!decodeVrrPlayoutProfile(fields[profileColumn], profile) ||
                    !referenceController->loadPlayoutHistory(profile)) {
                    std::fprintf(stderr, "Invalid starting calibration in capture\n");
                    return 1;
                }
                // A different policy learns its own error distribution; do not
                // reinterpret the old smoothed-slot histogram as FIFO readiness.
                if (capturedParameters.latencyFixEnabled == scenario.controller.latencyFixEnabled &&
                    capturedParameters.latencyFixAllRates == scenario.controller.latencyFixAllRates &&
                    capturedParameters.latencyFixDelayPeriodPerMille == scenario.controller.latencyFixDelayPeriodPerMille &&
                    referenceController->playoutHistory().version() == simulatedController->playoutHistory().version() &&
                    !simulatedController->loadPlayoutHistory(profile)) return 1;
                capturedParameterValues.insert(profileColumn, fields[profileColumn]);
            }
        }
        else {
            metrics.displayRefreshMismatchRows +=
                rowDisplayRefreshHz != capturedConfig.displayRefreshHz ? 1 : 0;
            metrics.streamRateMismatchRows +=
                rowStreamRateHz != capturedConfig.streamRateHz ? 1 : 0;
            metrics.additionalQueuedFrameMismatchRows +=
                rowAdditionalQueuedFrame !=
                    capturedConfig.allowAdditionalQueuedFrame ? 1 : 0;
            metrics.sessionAllowTearingMismatchRows +=
                rowAllowTearing != capturedConfig.allowTearing ? 1 : 0;
            metrics.latchCapabilityMismatchRows +=
                rowCanLatch != capturedCanLatch ? 1 : 0;
        }
        if (!capturedParameterValues.isEmpty()) {
            bool parameterRowMatches = true;
            for (auto it = capturedParameterValues.cbegin();
                    it != capturedParameterValues.cend(); ++it) {
                if (fields[it.key()] != it.value()) {
                    parameterRowMatches = false;
                    break;
                }
            }
            metrics.controllerParameterMismatchRows +=
                parameterRowMatches ? 0 : 1;
        }

        const bool recordedRefreshPhaseComparable =
            simulatedConfig.displayRefreshHz ==
                capturedConfig.displayRefreshHz;
        uint64_t rowPresentSequenceDelta = 0;
        uint64_t rowPresentRefreshDelta = 0;
        uint64_t rowScanoutAnomalyDelta = 0;
        uint64_t rowRepeatedRefreshDelta = 0;
        bool rowExactPresentRefreshTimestampValid = false;
        int64_t rowRecordedExactRefreshPhaseUs = 0;
        int64_t rowSimulatedExactRefreshPhaseUs = 0;
        int64_t rowRecordedExactActiveScanoutPhaseUs = 0;
        int64_t rowSimulatedExactActiveScanoutPhaseUs = 0;
        QByteArray rowRecordedExactRefreshClass;
        QByteArray rowSimulatedExactRefreshClass;
        const auto compareSimulatedToExactRefresh =
            [&](SubmissionBand& submission) {
                if (!recordedRefreshPhaseComparable ||
                        !submission.exactPresentRefreshTimeValid ||
                        !submission.simulatedSubmissionValid ||
                        submission.simulatedRefreshCompared) {
                    return;
                }

                const uint64_t exactRefreshUs =
                    submission.exactPresentRefreshTimeUs;
                VrrReplayDisplayParameters simulatedExactParameters =
                    rasterDisplayParameters;
                simulatedExactParameters.presentTransportUs =
                    submission.simulatedPresentTransportUs;
                const uint64_t simulatedModeledTransitionUs =
                    saturatingAdd(
                        submission.simulatedSubmissionUs,
                        submission.simulatedPresentTransportUs);
                const int64_t simulatedPhaseUs = signedDifference(
                    submission.simulatedSubmissionUs, exactRefreshUs);
                metrics.simulatedRecordedRefreshPhase.add(
                    simulatedPhaseUs);
                metrics.simulatedRecordedRefreshModeledPhase.add(
                    signedDifference(
                        simulatedModeledTransitionUs,
                        exactRefreshUs));
                metrics.simulatedRecordedActiveScanoutPhase.add(
                    signedDifference(
                        simulatedModeledTransitionUs,
                        saturatingAdd(
                            exactRefreshUs,
                            simulatedExactParameters.
                                syncToActiveScanoutUs)));
                rowSimulatedExactActiveScanoutPhaseUs =
                    signedDifference(
                        simulatedModeledTransitionUs,
                        saturatingAdd(
                            exactRefreshUs,
                            simulatedExactParameters.
                                syncToActiveScanoutUs));
                rowSimulatedExactRefreshPhaseUs = simulatedPhaseUs;
                const VrrExactRefreshPhaseClass simulatedClass =
                    evaluateVrrExactRefreshPhase(
                        submission.simulatedLatched,
                        submission.simulatedSubmissionUs,
                        exactRefreshUs,
                        periodForRate(
                            simulatedConfig.displayRefreshHz),
                        submission.simulatedSourcePeriodUs,
                        simulatedExactParameters);
                ++metrics.simulatedExactRefreshClassifications[
                    QByteArray(vrrExactRefreshPhaseClassName(
                        simulatedClass))];
                addExactRefreshPhaseToBands(
                    metrics.simulatedRateBands,
                    roundedRateForPeriod(
                        submission.simulatedSourcePeriodUs),
                    simulatedClass);
                if (submission.
                        simulatedSubmissionAdvanceAppliedUs != 0) {
                    addInjectedAdvanceExactRefreshOutcome(
                        metrics, simulatedClass);
                }
                if (submission.
                        simulatedDisplayTransitionDelayUs != 0) {
                    addInjectedDisplayTransitionExactRefreshOutcome(
                        metrics, simulatedClass);
                }
                rowSimulatedExactRefreshClass =
                    vrrExactRefreshPhaseClassName(simulatedClass);
                submission.simulatedExactClassValid = true;
                submission.simulatedExactClass = simulatedClass;
                if (submission.simulatedRasterValid &&
                        !submission.simulatedValidationCounted) {
                    metrics.simulatedRasterValidationContradictions +=
                        addRasterValidation(
                            metrics.simulatedRasterValidation,
                            submission.simulatedRaster,
                            simulatedClass) ? 1 : 0;
                    submission.simulatedValidationCounted = true;
                }
                ++metrics.simulatedRecordedRefreshComparisons;
                metrics.simulatedAfterRecordedRefresh +=
                    simulatedPhaseUs > 0 ? 1 : 0;
                const int64_t recordedPhaseUs = signedDifference(
                    submission.recordedPresentStartUs,
                    exactRefreshUs);
                metrics.exactPresentRefreshPhaseMatches +=
                    simulatedPhaseUs == recordedPhaseUs ? 1 : 0;
                submission.simulatedRefreshCompared = true;
            };
        if (unsignedField(fields, columns.latchValid) != 0) {
            ++metrics.latchValidRows;
            const uint64_t latchSubmission = unsignedField(
                fields, columns.latchSubmissionId);
            const uint64_t presentRefresh = unsignedField(
                fields, columns.latchPresentRefreshSequence);
            const uint64_t syncRefresh = optionalUnsignedField(
                fields, columns.latchSyncRefreshSequence);
            const uint64_t syncSampleUs = optionalUnsignedField(
                fields, columns.latchTimeUs);
            if (haveLatch && (latchSubmission < priorLatchSubmission ||
                    presentRefresh < priorPresentRefresh)) {
                ++metrics.latchSequenceResets;
                haveLatch = false;
            }
            int latchRateHz = 0;
            SubmissionBand* latchSubmissionInfo = nullptr;
            for (SubmissionBand& submission : pendingSubmissionBands) {
                if (submission.id == latchSubmission) {
                    latchRateHz = submission.rateHz;
                    latchSubmissionInfo = &submission;
                    break;
                }
            }
            if (haveLatch && latchSubmission > priorLatchSubmission &&
                    presentRefresh >= priorPresentRefresh) {
                const uint64_t presentDelta =
                    latchSubmission - priorLatchSubmission;
                const uint64_t refreshDelta =
                    presentRefresh - priorPresentRefresh;
                const uint64_t anomalies = presentDelta > refreshDelta ?
                    presentDelta - refreshDelta : 0;
                const uint64_t repeated = refreshDelta > presentDelta ?
                    refreshDelta - presentDelta : 0;
                rowPresentSequenceDelta = presentDelta;
                rowPresentRefreshDelta = refreshDelta;
                rowScanoutAnomalyDelta = anomalies;
                rowRepeatedRefreshDelta = repeated;
                if (presentDelta > refreshDelta) {
                    metrics.scanoutAnomalies += anomalies;
                }
                if (refreshDelta > presentDelta) {
                    metrics.repeatedRefreshes += repeated;
                }
                addScanoutOutcome(metrics.observedRateBands,
                                  latchRateHz, anomalies, repeated);
            }
            const bool freshLatch =
                !haveLatch || latchSubmission > priorLatchSubmission;
            if (freshLatch && latchSubmissionInfo != nullptr) {
                ++metrics.freshLatchSamplesMatchedToSubmission;
            }
            if (freshLatch && syncSampleUs != 0 && presentRefresh != 0 &&
                    presentRefresh == syncRefresh) {
                rowExactPresentRefreshTimestampValid = true;
                ++metrics.exactPresentRefreshTimestampSamples;
                if (latchSubmissionInfo != nullptr &&
                        latchSubmissionInfo->recordedPresentStartUs != 0) {
                    ++metrics.exactPresentRefreshCorrelations;
                    const int64_t recordedPhaseUs = signedDifference(
                        latchSubmissionInfo->recordedPresentStartUs,
                        syncSampleUs);
                    metrics.observedExactPresentRefreshPhase.add(
                        recordedPhaseUs);
                    metrics.observedExactPresentRefreshModeledPhase.add(
                        signedDifference(
                            saturatingAdd(
                                latchSubmissionInfo->recordedPresentStartUs,
                                scenario.display.presentTransportUs),
                            syncSampleUs));
                    metrics.observedExactPresentActiveScanoutPhase.add(
                        signedDifference(
                            saturatingAdd(
                                latchSubmissionInfo->recordedPresentStartUs,
                                scenario.display.presentTransportUs),
                            saturatingAdd(
                                syncSampleUs,
                                scenario.display.
                                    syncToActiveScanoutUs)));
                    rowRecordedExactActiveScanoutPhaseUs =
                        signedDifference(
                            saturatingAdd(
                                latchSubmissionInfo->recordedPresentStartUs,
                                scenario.display.presentTransportUs),
                            saturatingAdd(
                                syncSampleUs,
                                scenario.display.
                                    syncToActiveScanoutUs));
                    rowRecordedExactRefreshPhaseUs = recordedPhaseUs;
                    const VrrExactRefreshPhaseClass recordedClass =
                        evaluateVrrExactRefreshPhase(
                            latchSubmissionInfo->recordedLatched,
                            latchSubmissionInfo->recordedPresentStartUs,
                            syncSampleUs,
                            latchSubmissionInfo->displayPeriodUs,
                            latchSubmissionInfo->sourcePeriodUs,
                            rasterDisplayParameters);
                    ++metrics.observedExactRefreshClassifications[
                        QByteArray(vrrExactRefreshPhaseClassName(
                            recordedClass))];
                    addExactRefreshPhaseToBands(
                        metrics.observedRateBands,
                        latchSubmissionInfo->rateHz, recordedClass);
                    rowRecordedExactRefreshClass =
                        vrrExactRefreshPhaseClassName(recordedClass);
                    latchSubmissionInfo->recordedExactClassValid = true;
                    latchSubmissionInfo->recordedExactClass =
                        recordedClass;
                    if (latchSubmissionInfo->recordedRasterValid &&
                            !latchSubmissionInfo->
                                recordedValidationCounted) {
                        metrics.observedRasterValidationContradictions +=
                            addRasterValidation(
                                metrics.observedRasterValidation,
                                latchSubmissionInfo->recordedRaster,
                                recordedClass) ? 1 : 0;
                        latchSubmissionInfo->recordedValidationCounted =
                            true;
                    }
                    latchSubmissionInfo->exactPresentRefreshTimeValid = true;
                    latchSubmissionInfo->exactPresentRefreshTimeUs =
                        syncSampleUs;
                    compareSimulatedToExactRefresh(
                        *latchSubmissionInfo);
                }
                else {
                    ++metrics.invalidExactPresentRefreshCorrelations;
                }
            }
            if (freshLatch) {
                ++metrics.uniqueLatchSamples;
                haveLatch = true;
                priorLatchSubmission = latchSubmission;
                priorPresentRefresh = presentRefresh;
            }
            else {
                ++metrics.staleLatchSamples;
            }
            while (!pendingSubmissionBands.empty() &&
                    pendingSubmissionBands.front().id < latchSubmission) {
                pendingSubmissionBands.pop_front();
            }
        }

        TimelineDetails timelineDetails;
        timelineDetails.decoderOutputUs = decoderOutputUs;
        timelineDetails.recordedDecisionUs = decisionUs;
        timelineDetails.recordedRenderWaitOvershootUs =
            optionalUnsignedField(
                fields, columns.renderWaitOvershootUs);
        timelineDetails.recordedRenderSchedulerDelayUs =
            optionalUnsignedField(
                fields, columns.renderSchedulerDelayUs);
        timelineDetails.recordedRenderSchedulerDelayValid =
            optionalUnsignedField(
                fields, columns.renderSchedulerDelayValid) != 0;
        timelineDetails.recordedRenderDeadlineAlreadyElapsed =
            optionalUnsignedField(
                fields, columns.renderDeadlineAlreadyElapsed) != 0;
        timelineDetails.recordedTargetWaitOvershootUs =
            optionalUnsignedField(
                fields, columns.targetWaitOvershootUs);
        timelineDetails.recordedTargetSchedulerDelayUs =
            optionalUnsignedField(
                fields, columns.targetSchedulerDelayUs);
        timelineDetails.recordedTargetSchedulerDelayValid =
            optionalUnsignedField(
                fields, columns.targetSchedulerDelayValid) != 0;
        timelineDetails.recordedTargetDeadlineAlreadyElapsed =
            optionalUnsignedField(
                fields, columns.targetDeadlineAlreadyElapsed) != 0;
        timelineDetails.recordedSourcePeriodUs = unsignedField(
            fields, columns.sourcePeriodUs);
        timelineDetails.recordedSourceRateHz = roundedRateForPeriod(
            timelineDetails.recordedSourcePeriodUs);
        timelineDetails.recordedSpacingMarginUs = optionalSignedField(
            fields, columns.spacingMarginUs);
        timelineDetails.recordedSpacingDeficitUs = optionalUnsignedField(
            fields, columns.spacingDeficitUs);
        timelineDetails.recordedSpacingGuardFeedbackUs =
            optionalUnsignedField(
                fields, columns.spacingGuardFeedbackUs);
        timelineDetails.recordedSpacingCorrected = optionalUnsignedField(
            fields, columns.spacingCorrected) != 0;
        timelineDetails.recordedSpacingCheckUs =
            optionalUnsignedField(fields, columns.spacingCheckUs);
        timelineDetails.recordedPresentationFloorUs =
            optionalUnsignedField(
                fields, columns.presentationFloorUs);
        timelineDetails.recordedSpacingRecheckUs =
            optionalUnsignedField(fields, columns.spacingRecheckUs);
        timelineDetails.recordedSpacingCorrectedFloorUs =
            optionalUnsignedField(
                fields, columns.spacingCorrectedFloorUs);
        timelineDetails.recordedCorrectionWaitStartUs =
            optionalUnsignedField(
                fields, columns.correctionWaitStartUs);
        timelineDetails.recordedCorrectionWaitEndUs =
            optionalUnsignedField(
                fields, columns.correctionWaitEndUs);
        timelineDetails.guardUs = unsignedField(fields, columns.guardUs);
        timelineDetails.headroomUs = unsignedField(fields,
                                                    columns.headroomUs);
        timelineDetails.readinessBudgetUs = signedField(
            fields, columns.readinessBudgetUs);
        timelineDetails.readinessReserveUs = optionalUnsignedField(
            fields, columns.appliedReadinessReserveUs);
        timelineDetails.queueDepthBefore = unsignedField(
            fields, columns.queueDepthBefore);
        timelineDetails.queueDepthAfter = optionalUnsignedField(
            fields, columns.queueDepthAfter);
        timelineDetails.completionQueueDepth = completionQueueDepth;
        timelineDetails.queueDiscontinuity = optionalUnsignedField(
            fields, columns.queueDiscontinuity) != 0;
        timelineDetails.recordedPresenterSubmissionTimeValid =
            presenterSubmissionTimeDeclared;
        timelineDetails.recordedPresenterSubmissionTimeUs =
            presenterSubmissionTimeUs;
        timelineDetails.recordedPresenterSubmissionTimeUsed =
            presenterSubmissionTimeUsed;
        timelineDetails.recordedLatched = optionalUnsignedField(
            fields, columns.latchedPresent) != 0 &&
            unsignedField(fields, columns.canLatch) != 0;
        timelineDetails.latchValid = unsignedField(
            fields, columns.latchValid) != 0;
        timelineDetails.latchSubmissionId = unsignedField(
            fields, columns.latchSubmissionId);
        timelineDetails.latchTimeUs = optionalUnsignedField(
            fields, columns.latchTimeUs);
        timelineDetails.latchRawSyncQpcValid = rawSyncQpcDeclared;
        timelineDetails.latchRawSyncQpcTicks = rawSyncQpcTicks;
        timelineDetails.latchRawSyncQpcFrequency =
            rowRawSyncQpcFrequency;
        timelineDetails.latchQpcCorrelationValid =
            qpcCorrelationDeclared;
        timelineDetails.latchQpcCorrelationReferenceTicks =
            qpcCorrelationReferenceTicks;
        timelineDetails.latchQpcCorrelationReferenceTimeUs =
            qpcCorrelationReferenceTimeUs;
        timelineDetails.latchQpcCorrelationSpanTicks =
            qpcCorrelationSpanTicks;
        timelineDetails.latchPresentRefreshSequence = unsignedField(
            fields, columns.latchPresentRefreshSequence);
        timelineDetails.latchSyncRefreshSequence = optionalUnsignedField(
            fields, columns.latchSyncRefreshSequence);
        timelineDetails.prePresentSyncSampleValid =
            rawPrePresentSyncSampleValid;
        timelineDetails.prePresentSyncAnchorIntegrityValid =
            prePresentSyncAnchorIntegrityValid;
        timelineDetails.prePresentSyncSampleUs =
            rowPrePresentSyncSampleUs;
        timelineDetails.prePresentSyncRefreshSequence =
            rowPrePresentSyncRefreshSequence;
        timelineDetails.observedPresentSequenceDelta =
            rowPresentSequenceDelta;
        timelineDetails.observedPresentRefreshDelta =
            rowPresentRefreshDelta;
        timelineDetails.observedScanoutAnomalyDelta =
            rowScanoutAnomalyDelta;
        timelineDetails.observedRepeatedRefreshDelta =
            rowRepeatedRefreshDelta;
        timelineDetails.exactPresentRefreshTimestampValid =
            rowExactPresentRefreshTimestampValid;
        timelineDetails.recordedExactRefreshPhaseUs =
            rowRecordedExactRefreshPhaseUs;
        timelineDetails.simulatedExactRefreshPhaseUs =
            rowSimulatedExactRefreshPhaseUs;
        timelineDetails.recordedExactActiveScanoutPhaseUs =
            rowRecordedExactActiveScanoutPhaseUs;
        timelineDetails.simulatedExactActiveScanoutPhaseUs =
            rowSimulatedExactActiveScanoutPhaseUs;
        timelineDetails.recordedExactRefreshClass =
            rowRecordedExactRefreshClass;
        timelineDetails.simulatedExactRefreshClass =
            rowSimulatedExactRefreshClass;
        timelineDetails.recordedGpuReadyAttempted =
            gpuReadyAttempted;
        timelineDetails.recordedGpuReadySignalResultValid =
            gpuReadySignalResultDeclared;
        timelineDetails.recordedGpuReadySignalResult =
            gpuReadySignalResult;
        timelineDetails.recordedGpuReadySetEventResultValid =
            gpuReadySetEventResultDeclared;
        timelineDetails.recordedGpuReadySetEventResult =
            gpuReadySetEventResult;
        timelineDetails.recordedGpuReadyWaitResultValid =
            gpuReadyWaitResultDeclared;
        timelineDetails.recordedGpuReadyWaitResult =
            gpuReadyWaitResult;
        timelineDetails.recordedNativeBackendValid =
            nativeBackendDeclared;
        timelineDetails.recordedNativeBackend =
            nativeBackend;
        timelineDetails.recordedNativePresentResultValid =
            nativePresentResultDeclared;
        timelineDetails.recordedNativePresentResult =
            nativePresentResult;
        timelineDetails.recordedNativePresentParametersValid =
            nativePresentParametersDeclared;
        timelineDetails.recordedNativePresentSyncInterval =
            nativePresentSyncInterval;
        timelineDetails.recordedNativePresentFlags =
            nativePresentFlags;
        timelineDetails.recordedNativeVrrStateValid =
            nativeVrrStateDeclared;
        timelineDetails.recordedNativeTearingSupported =
            nativeTearingSupported;
        timelineDetails.recordedNativeBorderlessFlipModel =
            nativeBorderlessFlipModel;
        timelineDetails.recordedNativeSameGpuOutput =
            nativeSameGpuOutput;
        timelineDetails.recordedNativeRenderAdapterLuidValid =
            nativeRenderAdapterLuidDeclared;
        timelineDetails.recordedNativeRenderAdapterLuid =
            nativeRenderAdapterLuid;
        timelineDetails.recordedNativeSwapChainAllowsTearing =
            nativeSwapChainAllowsTearing;
        timelineDetails.recordedNativePresentReadyAvailable =
            nativePresentReadyAvailable;
        timelineDetails.recordedNativeForegroundWindow =
            nativeForegroundWindow;
        timelineDetails.recordedNativeVrrFallbackReason =
            nativeVrrFallbackReason;
        timelineDetails.recordedNativeDesktopMonitorCount =
            nativeDesktopMonitorCount;
        timelineDetails.recordedNativeVblankVirtualizationProbeComplete =
            nativeVblankVirtualizationProbeComplete;
        timelineDetails.recordedNativeVblankVirtualizationCallAvailable =
            nativeVblankVirtualizationCallAvailable;
        timelineDetails.recordedNativeVblankVirtualizationResultValid =
            nativeVblankVirtualizationResultDeclared;
        timelineDetails.recordedNativeVblankVirtualizationResult =
            nativeVblankVirtualizationResult;
        timelineDetails.recordedNativeVblankVirtualizationDisabled =
            nativeVblankVirtualizationDisabled;
        timelineDetails.recordedSubmissionIdQueryResultValid =
            submissionIdQueryResultDeclared;
        timelineDetails.recordedSubmissionIdQueryResult =
            submissionIdQueryResult;
        timelineDetails.recordedSubmissionIdQueryStartUs =
            submissionIdQueryStartUs;
        timelineDetails.recordedSubmissionIdQueryEndUs =
            submissionIdQueryEndUs;
        timelineDetails.recordedFrameStatsQueryResultValid =
            frameStatsQueryResultDeclared;
        timelineDetails.recordedFrameStatsQueryResult =
            frameStatsQueryResult;
        timelineDetails.recordedFrameStatsQueryStartUs =
            frameStatsQueryStartUs;
        timelineDetails.recordedFrameStatsQueryEndUs =
            frameStatsQueryEndUs;

        if (!rowDecisionValid) {
            const int zeroPayloadColumns[] = {
                columns.queueDiscontinuity,
                columns.sourceIntervalUs,
                columns.sourceTimeUs,
                columns.sourcePeriodUs,
                columns.readyOffsetUs,
                columns.readinessBudgetUs,
                columns.timingBudgetUs,
                columns.renderLeadUs,
                columns.gpuReadinessLeadUs,
                columns.renderWakeLeadUs,
                columns.targetWakeLeadUs,
                columns.guardUs,
                columns.headroomUs,
                columns.renderStartUs,
                columns.renderWaitFinalUs,
                columns.renderWaitOvershootUs,
                columns.renderSchedulerDelayUs,
                columns.renderSchedulerDelayValid,
                columns.renderDeadlineAlreadyElapsed,
                columns.renderWaitInitialUs,
                columns.renderWaitActiveBudgetUs,
                columns.renderWaitCoarseSleepCount,
                columns.renderWaitCoarseRequestedTotalUs,
                columns.renderWaitCoarseRequestedWakeUs,
                columns.renderWaitCoarseReturnUs,
                columns.renderWaitCoarseClockStalled,
                columns.renderWaitActiveEntered,
                columns.renderWaitActiveStartUs,
                columns.renderWaitActiveLimitUs,
                columns.renderWaitActiveYieldCount,
                columns.renderWaitActiveClockStalled,
                columns.renderWaitActiveYieldLimitReached,
                columns.targetWaitOvershootUs,
                columns.targetSchedulerDelayUs,
                columns.targetSchedulerDelayValid,
                columns.targetDeadlineAlreadyElapsed,
                columns.targetWaitInitialUs,
                columns.targetWaitActiveBudgetUs,
                columns.targetWaitCoarseSleepCount,
                columns.targetWaitCoarseRequestedTotalUs,
                columns.targetWaitCoarseRequestedWakeUs,
                columns.targetWaitCoarseReturnUs,
                columns.targetWaitCoarseClockStalled,
                columns.targetWaitActiveEntered,
                columns.targetWaitActiveStartUs,
                columns.targetWaitActiveLimitUs,
                columns.targetWaitActiveYieldCount,
                columns.targetWaitActiveClockStalled,
                columns.targetWaitActiveYieldLimitReached,
                columns.preparationStartUs,
                columns.preparationEndUs,
                columns.preparationUs,
                columns.recordedTargetUs,
                columns.targetWaitFinalUs,
                columns.presentStartUs,
                columns.submissionBoundaryUs,
                columns.presenterSubmissionTimeValid,
                columns.presenterSubmissionTimeUs,
                columns.presenterSubmissionTimeUsed,
                columns.presentEndUs,
                columns.presentCallUs,
                columns.submitErrorUs,
                columns.submissionSpacingUs,
                columns.spacingMarginUs,
                columns.spacingDeficitUs,
                columns.spacingGuardFeedbackUs,
                columns.spacingCorrected,
                columns.hadPriorSubmission,
                columns.submissionIdValid,
                columns.submissionId,
                columns.latchValid,
                columns.latchSubmissionId,
                columns.latchTimeUs,
                columns.latchPresentRefreshSequence,
                columns.latchSyncRefreshSequence,
                columns.latchedPresent,
                columns.usedRtpTimestamp,
                columns.cadenceEligible,
                columns.sourceRateChanged,
                columns.phaseDiscontinuity,
                columns.rebased,
                columns.externalRebaseApplied,
                columns.externalRebaseFlags,
                columns.midframeWindowStateFlags,
                columns.nativePresentTimingValid,
                columns.nativePresentStartUs,
                columns.nativePresentEndUs,
                columns.nativePresentCallUs,
                columns.presentCountBeforeValid,
                columns.presentCountBefore,
                columns.frameStatsBeforeValid,
                columns.frameStatsBeforePresentCount,
                columns.frameStatsBeforeTimeUs,
                columns.frameStatsBeforePresentRefreshSequence,
                columns.frameStatsBeforeSyncRefreshSequence,
                columns.gpuReadyAttempted,
                columns.gpuReadySignalResultValid,
                columns.gpuReadySignalResult,
                columns.gpuReadySetEventResultValid,
                columns.gpuReadySetEventResult,
                columns.gpuReadyWaitResultValid,
                columns.gpuReadyWaitResult,
                columns.gpuReadyTimingValid,
                columns.gpuReadySignalStartUs,
                columns.gpuReadySignalEndUs,
                columns.gpuReadyFlushStartUs,
                columns.gpuReadyFlushEndUs,
                columns.gpuReadySetEventStartUs,
                columns.gpuReadySetEventEndUs,
                columns.gpuReadyPollStartUs,
                columns.gpuReadyPollEndUs,
                columns.gpuReadyFenceValue,
                columns.gpuReadyPollCompletedValue,
                columns.gpuReadyCompletedBeforeWait,
                columns.gpuReadyCompletionLowerBoundUs,
                columns.gpuReadyCompletionUpperBoundUs,
                columns.gpuReadyCompletionUncertaintyUs,
                columns.gpuReadyWaitStartUs,
                columns.gpuReadyTimeUs,
                columns.gpuReadyWaitUs,
                columns.decisionEndUs,
                columns.controllerCallUs,
                columns.staleCheckUs,
                columns.staleAgeUs,
                columns.renderWaitEntryUs,
                columns.targetWaitEntryUs,
                columns.spacingCheckUs,
                columns.presentationFloorUs,
                columns.spacingRecheckUs,
                columns.spacingCorrectedFloorUs,
                columns.correctionWaitStartUs,
                columns.correctionWaitEndUs,
                columns.nativeBackendValid,
                columns.nativeBackend,
                columns.nativePresentResultValid,
                columns.nativePresentResult,
                columns.nativePresentParametersValid,
                columns.nativePresentSyncInterval,
                columns.nativePresentFlags,
                columns.nativeVrrStateValid,
                columns.nativeTearingSupported,
                columns.nativeBorderlessFlipModel,
                columns.nativeSameGpuOutput,
                columns.nativeRenderAdapterLuidValid,
                columns.nativeRenderAdapterLuid,
                columns.nativeSwapChainAllowsTearing,
                columns.nativeTearingFeatureQueryResultValid,
                columns.nativeTearingFeatureQueryResult,
                columns.nativeTearingFeatureAllowsTearing,
                columns.nativeSwapChainDescQueryResultValid,
                columns.nativeSwapChainDescQueryResult,
                columns.nativeSwapChainFlags,
                columns.nativeSwapChainSwapEffect,
                columns.nativeFullscreenStateQueryResultValid,
                columns.nativeFullscreenStateQueryResult,
                columns.nativeFullscreenExclusive,
                columns.nativeWindowFlags,
                columns.nativePresentReadyAvailable,
                columns.nativeForegroundWindow,
                columns.nativeVrrFallbackReason,
                columns.nativeDesktopMonitorCount,
                columns.nativeVblankVirtualizationProbeComplete,
                columns.nativeVblankVirtualizationCallAvailable,
                columns.nativeVblankVirtualizationResultValid,
                columns.nativeVblankVirtualizationResult,
                columns.nativeVblankVirtualizationDisabled,
                columns.nativeDisplayConfigQueryResultValid,
                columns.nativeDisplayConfigQueryResult,
                columns.nativeDisplayPathValid,
                columns.nativeDisplayPathFlags,
                columns.nativeDisplayTargetAvailable,
                columns.nativeDisplaySourceAdapterLuid,
                columns.nativeDisplaySourceId,
                columns.nativeDisplayTargetAdapterLuid,
                columns.nativeDisplayTargetId,
                columns.nativeDisplayOutputTechnology,
                columns.nativeDisplayRotation,
                columns.nativeDisplayScaling,
                columns.nativeDisplayPathRefreshNumerator,
                columns.nativeDisplayPathRefreshDenominator,
                columns.nativeDisplaySignalValid,
                columns.nativeDisplaySignalPixelRateHz,
                columns.nativeDisplaySignalHSyncNumerator,
                columns.nativeDisplaySignalHSyncDenominator,
                columns.nativeDisplaySignalVSyncNumerator,
                columns.nativeDisplaySignalVSyncDenominator,
                columns.nativeDisplaySignalActiveWidth,
                columns.nativeDisplaySignalActiveHeight,
                columns.nativeDisplaySignalTotalWidth,
                columns.nativeDisplaySignalTotalHeight,
                columns.nativeDisplaySignalAdditionalInfoRaw,
                columns.nativeDisplaySignalScanLineOrdering,
                columns.nativeRasterSamplingRequested,
                columns.nativeRasterOpenResultValid,
                columns.nativeRasterOpenResult,
                columns.nativeRasterSourceValid,
                columns.nativeRasterVidPnSourceId,
                columns.nativeRasterBeforeQueryResultValid,
                columns.nativeRasterBeforeQueryResult,
                columns.nativeRasterBeforeQueryStartUs,
                columns.nativeRasterBeforeQueryEndUs,
                columns.nativeRasterBeforeInVerticalBlank,
                columns.nativeRasterBeforeScanLine,
                columns.nativeRasterAfterQueryResultValid,
                columns.nativeRasterAfterQueryResult,
                columns.nativeRasterAfterQueryStartUs,
                columns.nativeRasterAfterQueryEndUs,
                columns.nativeRasterAfterInVerticalBlank,
                columns.nativeRasterAfterScanLine,
                columns.submissionIdQueryResultValid,
                columns.submissionIdQueryResult,
                columns.submissionIdQueryStartUs,
                columns.submissionIdQueryEndUs,
                columns.frameStatsQueryResultValid,
                columns.frameStatsQueryResult,
                columns.frameStatsQueryStartUs,
                columns.frameStatsQueryEndUs,
                columns.latchRawSyncQpcValid,
                columns.latchRawSyncQpcTicks,
                columns.latchRawSyncQpcFrequency,
                columns.latchQpcCorrelationValid,
                columns.latchQpcCorrelationReferenceTicks,
                columns.latchQpcCorrelationReferenceTimeUs,
                columns.latchQpcCorrelationSpanTicks,
                columns.readinessPhaseUs,
                columns.readinessDemandUs,
                columns.appliedReadinessReserveUs,
                columns.gpuReadinessAppliedUs,
                columns.cadenceSampleCount,
                columns.rateCandidateSampleCount,
                columns.readinessSampleCount,
                columns.preparationSampleCount,
                columns.renderSchedulerSampleCount,
                columns.targetSchedulerSampleCount,
                columns.cleanSpacingFrames,
                columns.phaseErrorFrames,
                columns.readinessModelValid,
            };
            const bool nonDecisionPayloadValid = std::all_of(
                std::begin(zeroPayloadColumns),
                std::end(zeroPayloadColumns),
                [&fields](int column) {
                    return column < 0 || fields[column] == "0";
                });
            metrics.nonDecisionPayloadMismatchRows +=
                nonDecisionPayloadValid ? 0 : 1;
            metrics.hadPriorSubmissionMismatches +=
                unsignedField(fields, columns.hadPriorSubmission) != 0 ?
                    1 : 0;
            metrics.submissionBoundaryMismatches +=
                unsignedField(fields, columns.submissionBoundaryUs) != 0 ?
                    1 : 0;
            metrics.submitErrorMismatches +=
                signedField(fields, columns.submitErrorUs) != 0 ? 1 : 0;
            metrics.submissionSpacingMismatches +=
                unsignedField(fields, columns.submissionSpacingUs) != 0 ?
                    1 : 0;
            metrics.spacingMarginMismatches +=
                signedField(fields, columns.spacingMarginUs) != 0 ? 1 : 0;
            metrics.tearClassificationMismatches +=
                recordedTear != "not_presented" ? 1 : 0;
            metrics.tearRiskMismatches +=
                unsignedField(fields, columns.tearRisk) != 0 ? 1 : 0;
            ++metrics.simulatedTearClassifications["not_presented"];
            if (readinessOutcome) {
                const auto terminal = optionalUnsignedField(fields, columns.terminalTimeUs);
                metrics.simulatedReadiness.record(terminal ? terminal : pacerArrivalUs, 0, true);
            }
            metrics.exactTearClassifications +=
                recordedTear == "not_presented" ? 1 : 0;
            if (timelineFile.isOpen() &&
                    !writeTimelineRow(timelineFile, arrivalSequence,
                        frameNumber,
                        disposition, decodeCompleteUs, pacerArrivalUs,
                        0, 0, 0, 0, recordedTear, "not_presented",
                        timelineDetails)) {
                std::fprintf(stderr, "Unable to write timeline: %s\n",
                             qPrintable(timelineFile.errorString()));
                return 1;
            }
            continue;
        }

        ++metrics.scheduled;
        metrics.observedPreparation.add(unsignedField(fields,
                                                       columns.preparationUs));
        metrics.observedPresentCall.add(unsignedField(fields,
                                                       columns.presentCallUs));
        if (optionalUnsignedField(fields,
                                  columns.nativePresentTimingValid) != 0) {
            ++metrics.nativePresentTimingValidRows;
            metrics.observedNativePresentCall.add(unsignedField(
                fields, columns.nativePresentCallUs));
        }
        metrics.presentCountBeforeValidRows += optionalUnsignedField(
            fields, columns.presentCountBeforeValid) != 0 ? 1 : 0;
        metrics.frameStatsBeforeValidRows += optionalUnsignedField(
            fields, columns.frameStatsBeforeValid) != 0 ? 1 : 0;
        if (columns.controllerCallUs >= 0)
            metrics.observedControllerCall.add(unsignedField(
                fields, columns.controllerCallUs));
        if (columns.staleAgeUs >= 0)
            metrics.observedStaleAge.add(unsignedField(
                fields, columns.staleAgeUs));
        if (columns.renderWaitEntryUs >= 0 &&
                columns.renderWaitFinalUs >= 0) {
            const uint64_t renderWaitEntryUs = unsignedField(
                fields, columns.renderWaitEntryUs);
            metrics.observedRenderWait.addElapsed(
                unsignedField(fields, columns.renderWaitFinalUs),
                renderWaitEntryUs);
            if (renderWaitEntryUs != 0 &&
                    columns.renderWaitOvershootUs >= 0) {
                metrics.observedRenderWaitOvershoot.add(unsignedField(
                    fields, columns.renderWaitOvershootUs));
            }
        }
        if (columns.renderSchedulerDelayValid >= 0 &&
                columns.renderSchedulerDelayUs >= 0 &&
                unsignedField(
                    fields, columns.renderSchedulerDelayValid) != 0) {
            metrics.observedRenderSchedulerDelay.add(unsignedField(
                fields, columns.renderSchedulerDelayUs));
        }
        metrics.renderDeadlineAlreadyElapsedRows +=
            optionalUnsignedField(
                fields, columns.renderDeadlineAlreadyElapsed) != 0 ? 1 : 0;
        if (columns.targetWaitEntryUs >= 0 &&
                columns.targetWaitFinalUs >= 0) {
            const uint64_t targetWaitEntryUs = unsignedField(
                fields, columns.targetWaitEntryUs);
            metrics.observedTargetWait.addElapsed(
                unsignedField(fields, columns.targetWaitFinalUs),
                targetWaitEntryUs);
            if (targetWaitEntryUs != 0 &&
                    columns.targetWaitOvershootUs >= 0) {
                metrics.observedTargetWaitOvershoot.add(unsignedField(
                    fields, columns.targetWaitOvershootUs));
            }
        }
        if (columns.targetSchedulerDelayValid >= 0 &&
                columns.targetSchedulerDelayUs >= 0 &&
                unsignedField(
                    fields, columns.targetSchedulerDelayValid) != 0) {
            metrics.observedTargetSchedulerDelay.add(unsignedField(
                fields, columns.targetSchedulerDelayUs));
        }
        metrics.targetDeadlineAlreadyElapsedRows +=
            optionalUnsignedField(
                fields, columns.targetDeadlineAlreadyElapsed) != 0 ? 1 : 0;
        if (columns.correctionWaitStartUs >= 0 &&
                columns.correctionWaitEndUs >= 0)
            metrics.observedCorrectionWait.addElapsed(
                unsignedField(fields, columns.correctionWaitEndUs),
                unsignedField(fields, columns.correctionWaitStartUs));
        PacedFrame frame(nullptr,
                         frameNumber,
                         rtpTimestamp,
                         unsignedField(fields, columns.rtpValid) != 0,
                         decoderOutputUs);
        frame.noteGpuReadyUs(decodeCompleteUs);
        frame.setDeliveryTimeline(
            optionalUnsignedField(fields, traceHeader.indexOf("frame_receive_us")),
            optionalUnsignedField(fields, traceHeader.indexOf("frame_reassembled_us")),
            optionalUnsignedField(fields, traceHeader.indexOf("decode_submit_us")));
        const bool hasPreparationTelemetry =
            unsignedField(fields, columns.preparationStartUs) != 0 ||
            unsignedField(fields, columns.preparationEndUs) != 0 ||
            unsignedField(fields, columns.preparationUs) != 0;
        const bool normalPresentationLifecycle =
            disposition == "presented" || disposition == "output_dropped";
        const bool interruptedLifecycle =
            disposition == "interrupted" ||
            disposition == "preparation_failed";
        const bool staleBeforeRenderLifecycle =
            disposition == "stale" &&
            optionalUnsignedField(fields, columns.renderWaitEntryUs) == 0;
        const bool staleAfterRenderLifecycle =
            disposition == "stale" &&
            optionalUnsignedField(fields, columns.renderWaitEntryUs) != 0;
        if (!normalPresentationLifecycle && !interruptedLifecycle &&
                !staleBeforeRenderLifecycle &&
                !staleAfterRenderLifecycle) {
            ++metrics.invalidControllerLifecycleRows;
        }
        if (normalPresentationLifecycle && deepTraceRow &&
                expectedDeepBeforeStateKnown) {
            ++metrics.deepBeforeStateEligibleRows;
            ++metrics.deepBeforeStateComparisons;
            const bool presentCountBeforeValid =
                optionalUnsignedField(
                    fields, columns.presentCountBeforeValid) != 0;
            const bool frameStatsBeforeValid =
                optionalUnsignedField(
                    fields, columns.frameStatsBeforeValid) != 0;
            metrics.deepBeforeStateValidityMismatches +=
                presentCountBeforeValid !=
                    expectedPresentCountBeforeValid ? 1 : 0;
            metrics.deepBeforeStateValidityMismatches +=
                frameStatsBeforeValid !=
                    expectedFrameStatsBeforeValid ? 1 : 0;
            if (presentCountBeforeValid &&
                    expectedPresentCountBeforeValid &&
                    optionalUnsignedField(
                        fields, columns.presentCountBefore) !=
                        expectedPresentCountBefore) {
                ++metrics.deepBeforePresentCountMismatches;
            }
            if (frameStatsBeforeValid &&
                    expectedFrameStatsBeforeValid &&
                    (optionalUnsignedField(
                        fields, columns.frameStatsBeforePresentCount) !=
                            expectedFrameStatsBeforePresentCount ||
                     optionalUnsignedField(
                        fields, columns.frameStatsBeforeTimeUs) !=
                            expectedFrameStatsBeforeTimeUs ||
                     optionalUnsignedField(
                        fields,
                        columns.frameStatsBeforePresentRefreshSequence) !=
                            expectedFrameStatsBeforePresentRefreshSequence ||
                     optionalUnsignedField(
                        fields,
                        columns.frameStatsBeforeSyncRefreshSequence) !=
                            expectedFrameStatsBeforeSyncRefreshSequence)) {
                ++metrics.deepBeforeFrameStatsMismatches;
            }
        }
        if (presented) {
            expectedDeepBeforeStateKnown = true;
            expectedPresentCountBeforeValid = submissionIdValid;
            if (submissionIdValid) {
                expectedPresentCountBefore = optionalUnsignedField(
                    fields, columns.submissionId);
            }
            const bool latchSampleValid =
                optionalUnsignedField(fields, columns.latchValid) != 0;
            expectedFrameStatsBeforeValid = latchSampleValid;
            if (latchSampleValid) {
                expectedFrameStatsBeforePresentCount =
                    optionalUnsignedField(
                        fields, columns.latchSubmissionId);
                expectedFrameStatsBeforeTimeUs = optionalUnsignedField(
                    fields, columns.latchTimeUs);
                expectedFrameStatsBeforePresentRefreshSequence =
                    optionalUnsignedField(
                        fields, columns.latchPresentRefreshSequence);
                expectedFrameStatsBeforeSyncRefreshSequence =
                    optionalUnsignedField(
                        fields, columns.latchSyncRefreshSequence);
            }
        }
        const uint64_t recordedDecisionEndUs = optionalUnsignedField(
            fields, columns.decisionEndUs);
        const uint64_t recordedStaleCheckUs = optionalUnsignedField(
            fields, columns.staleCheckUs);
        const uint64_t recordedRenderWaitEntryUs = optionalUnsignedField(
            fields, columns.renderWaitEntryUs);
        const uint64_t recordedRenderWaitFinalUs = optionalUnsignedField(
            fields, columns.renderWaitFinalUs);
        const uint64_t recordedTargetWaitEntryUs = optionalUnsignedField(
            fields, columns.targetWaitEntryUs);
        const uint64_t recordedTargetWaitFinalUs = optionalUnsignedField(
            fields, columns.targetWaitFinalUs);
        const uint64_t recordedSpacingCheckUs = optionalUnsignedField(
            fields, columns.spacingCheckUs);
        const uint64_t recordedPresentationFloorUs = optionalUnsignedField(
            fields, columns.presentationFloorUs);
        const uint64_t recordedSpacingRecheckUs = optionalUnsignedField(
            fields, columns.spacingRecheckUs);
        const uint64_t recordedSpacingCorrectedFloorUs =
            optionalUnsignedField(
                fields, columns.spacingCorrectedFloorUs);
        const uint64_t recordedCorrectionWaitStartUs =
            optionalUnsignedField(fields, columns.correctionWaitStartUs);
        const uint64_t recordedCorrectionWaitEndUs =
            optionalUnsignedField(fields, columns.correctionWaitEndUs);
        const uint64_t recordedSpacingDeficitUs =
            optionalUnsignedField(fields, columns.spacingDeficitUs);
        const uint64_t recordedSpacingGuardFeedbackUs =
            optionalUnsignedField(fields, columns.spacingGuardFeedbackUs);
        const bool recordedSpacingCorrected =
            optionalUnsignedField(fields, columns.spacingCorrected) != 0;
        const uint64_t recordedTerminalTimeUs = optionalUnsignedField(
            fields, columns.terminalTimeUs);
        if (traceSchema >= 5) {
            const uint64_t recordedControllerCallUs = unsignedField(
                fields, columns.controllerCallUs);
            const bool controllerCallOrderValid =
                recordedDecisionEndUs >= decisionUs;
            metrics.controllerCallOrderViolations +=
                controllerCallOrderValid ? 0 : 1;
            if (!controllerCallOrderValid ||
                    recordedDecisionEndUs - decisionUs !=
                        recordedControllerCallUs) {
                ++metrics.controllerCallDurationMismatchRows;
            }

            const bool staleCheckOrderValid =
                recordedStaleCheckUs >= recordedDecisionEndUs;
            metrics.staleCheckOrderViolations +=
                staleCheckOrderValid ? 0 : 1;
            const uint64_t expectedStaleAgeUs =
                recordedStaleCheckUs >= decodeCompleteUs ?
                    recordedStaleCheckUs - decodeCompleteUs : 0;
            metrics.staleAgeMismatchRows +=
                expectedStaleAgeUs != optionalUnsignedField(
                    fields, columns.staleAgeUs) ? 1 : 0;

            const bool renderWaitExpected = !staleBeforeRenderLifecycle;
            metrics.renderWaitExpectedRows +=
                renderWaitExpected ? 1 : 0;
            const bool renderWaitOrderValid = renderWaitExpected ?
                recordedRenderWaitEntryUs != 0 &&
                    recordedRenderWaitFinalUs >= recordedRenderWaitEntryUs &&
                    recordedRenderWaitEntryUs >= recordedStaleCheckUs :
                recordedRenderWaitEntryUs == 0 &&
                    recordedRenderWaitFinalUs == 0;
            metrics.waitBoundaryOrderViolations +=
                renderWaitOrderValid ? 0 : 1;

            const bool targetWaitPresent =
                recordedTargetWaitEntryUs != 0 ||
                recordedTargetWaitFinalUs != 0;
            metrics.targetWaitExpectedRows +=
                normalPresentationLifecycle ? 1 : 0;
            if ((normalPresentationLifecycle && !targetWaitPresent) ||
                    (targetWaitPresent &&
                     (recordedTargetWaitEntryUs == 0 ||
                      recordedTargetWaitFinalUs <
                         recordedTargetWaitEntryUs))) {
                ++metrics.waitBoundaryOrderViolations;
            }

            const bool submissionFloorPresent =
                recordedSpacingCheckUs != 0 ||
                recordedPresentationFloorUs != 0;
            if ((presented && !submissionFloorPresent) ||
                    (submissionFloorPresent &&
                     (recordedSpacingCheckUs == 0 ||
                      recordedPresentationFloorUs <
                        unsignedField(fields, columns.recordedTargetUs)))) {
                ++metrics.waitBoundaryOrderViolations;
            }

            const bool correctionWaitPresent =
                recordedCorrectionWaitStartUs != 0 ||
                recordedCorrectionWaitEndUs != 0;
            if (correctionWaitPresent &&
                    (recordedCorrectionWaitStartUs == 0 ||
                     recordedCorrectionWaitEndUs <
                        recordedCorrectionWaitStartUs ||
                     recordedCorrectionWaitStartUs <
                        recordedSpacingCheckUs)) {
                ++metrics.correctionWaitOrderViolations;
            }
            if (metrics.spacingCorrectionTelemetryAvailable) {
                const VrrSpacingCorrectionAudit spacingAudit =
                    evaluateVrrSpacingCorrection(
                        normalPresentationLifecycle,
                        recordedSpacingDeficitUs,
                        recordedSpacingGuardFeedbackUs,
                        recordedSpacingCorrected,
                        correctionWaitPresent);
                metrics.spacingCorrectionRelationshipMismatchRows +=
                    spacingAudit.relationshipValid ? 0 : 1;
                metrics.spacingCorrectedRows +=
                    recordedSpacingCorrected ? 1 : 0;
                metrics.observedSpacingDeficit.add(
                    recordedSpacingDeficitUs);
            }
        }
        const bool periodicStallApplies =
            scenario.execution.periodicStallUs != 0 ||
            (hasPreparationTelemetry &&
                (scenario.execution.periodicRenderWakeDelayUs != 0 ||
                 scenario.execution.periodicPreparationStallUs != 0)) ||
            (normalPresentationLifecycle &&
                (scenario.execution.periodicTargetWakeDelayUs != 0 ||
                 scenario.execution.periodicSpacingGuardFeedbackUs != 0)) ||
            (presented &&
                (scenario.execution.periodicSubmissionStallUs != 0 ||
                 scenario.execution.periodicSubmissionAdvanceUs != 0 ||
                 scenario.execution.
                    periodicDisplayTransitionDelayUs != 0));
        const bool injectPeriodicStall =
            periodicStallApplies &&
            vrrPeriodicInjectionSelected(
                metrics.scheduled,
                scenario.execution.periodicStallEveryFrames,
                scenario.execution.periodicStallPhaseFrames,
                scenario.execution.periodicStallBurstFrames);
        const uint64_t periodicDecisionStallUs = injectPeriodicStall ?
            scenario.execution.periodicStallUs : 0;
        const uint64_t periodicRenderWakeDelayUs =
            injectPeriodicStall && hasPreparationTelemetry ?
                scenario.execution.periodicRenderWakeDelayUs : 0;
        const uint64_t periodicTargetWakeDelayUs =
            injectPeriodicStall && normalPresentationLifecycle ?
                scenario.execution.periodicTargetWakeDelayUs : 0;
        const uint64_t periodicPreparationStallUs =
            injectPeriodicStall && hasPreparationTelemetry ?
            scenario.execution.periodicPreparationStallUs : 0;
        const uint64_t periodicSubmissionStallUs =
            injectPeriodicStall && presented ?
            scenario.execution.periodicSubmissionStallUs : 0;
        const uint64_t periodicSpacingGuardFeedbackUs =
            injectPeriodicStall && normalPresentationLifecycle ?
                scenario.execution.periodicSpacingGuardFeedbackUs : 0;
        const uint64_t periodicSubmissionAdvanceUs =
            injectPeriodicStall && presented ?
            scenario.execution.periodicSubmissionAdvanceUs : 0;
        const uint64_t periodicDisplayTransitionDelayUs =
            injectPeriodicStall && presented ?
                scenario.execution.
                    periodicDisplayTransitionDelayUs : 0;
        const uint64_t injectedDecisionDelayUs = saturatingAdd(
            scenario.execution.decisionDelayUs, periodicDecisionStallUs);
        const uint64_t requestedRenderWakeDelayUs = saturatingAdd(
            hasPreparationTelemetry ?
                scenario.execution.renderWakeDelayUs : 0,
            periodicRenderWakeDelayUs);
        const uint64_t requestedTargetWakeDelayUs = saturatingAdd(
            normalPresentationLifecycle ?
                scenario.execution.targetWakeDelayUs : 0,
            periodicTargetWakeDelayUs);
        const uint64_t injectedPreparationDelayUs = saturatingAdd(
            hasPreparationTelemetry ?
                scenario.execution.preparationDelayUs : 0,
            periodicPreparationStallUs);
        const uint64_t injectedSubmissionDelayUs = saturatingAdd(
            presented ? scenario.execution.submissionDelayUs : 0,
            periodicSubmissionStallUs);
        const uint64_t injectedSpacingGuardFeedbackUs = saturatingAdd(
            normalPresentationLifecycle ?
                scenario.execution.spacingGuardFeedbackUs : 0,
            periodicSpacingGuardFeedbackUs);
        const uint64_t injectedSubmissionAdvanceUs = saturatingAdd(
            presented ? scenario.execution.submissionAdvanceUs : 0,
            periodicSubmissionAdvanceUs);
        const uint64_t requestedDisplayTransitionDelayUs =
            saturatingAdd(
                presented ?
                    scenario.execution.displayTransitionDelayUs : 0,
                periodicDisplayTransitionDelayUs);
        const uint64_t simulatedPresentTransportUs =
            saturatingAdd(
                rasterDisplayParameters.presentTransportUs,
                requestedDisplayTransitionDelayUs);
        const uint64_t injectedDisplayTransitionDelayUs =
            simulatedPresentTransportUs -
                rasterDisplayParameters.presentTransportUs;
        // Worker-occupancy decision model. The recorded decision instant
        // embeds the recorded schedule of the previous frame: the single
        // pacing worker dequeues the next frame only after it has submitted
        // the previous one. Re-derive the instant from the simulated previous
        // submission so a candidate that presents earlier frees the worker
        // earlier and one that presents later holds it longer. When the
        // simulated and recorded previous submissions coincide, as they do
        // for the unchanged reference policy, this reproduces the recorded
        // instant exactly.
        uint64_t occupancyDecisionUs = decisionUs;
        if (rowDecisionValid && decisionUs != 0 &&
                simulatedController->hasLastSubmission() &&
                referenceController->hasLastSubmission()) {
            const uint64_t recordedPreviousSubmissionUs =
                referenceController->lastSubmissionUs();
            const uint64_t simulatedPreviousSubmissionUs =
                simulatedController->lastSubmissionUs();
            const uint64_t idleLatencyUs = decisionUs >= pacerArrivalUs ?
                decisionUs - pacerArrivalUs : 0;
            if (decisionUs >= recordedPreviousSubmissionUs) {
                const uint64_t postSubmissionGapUs =
                    decisionUs - recordedPreviousSubmissionUs;
                const bool workerWasBusy =
                    postSubmissionGapUs < idleLatencyUs;
                if (workerWasBusy) {
                    metrics.modeledBusyGapUs = postSubmissionGapUs;
                    occupancyDecisionUs = saturatingAdd(
                        simulatedPreviousSubmissionUs,
                        postSubmissionGapUs);
                    if (metrics.modeledIdleLatencyValid) {
                        occupancyDecisionUs = vrrBusyWorkerDecisionUs(
                            pacerArrivalUs, decisionUs,
                            simulatedPreviousSubmissionUs, postSubmissionGapUs,
                            metrics.modeledIdleLatencyUs);
                    }
                }
                else {
                    metrics.modeledIdleLatencyUs =
                        metrics.modeledIdleLatencyValid ?
                            std::min(metrics.modeledIdleLatencyUs,
                                     idleLatencyUs) : idleLatencyUs;
                    metrics.modeledIdleLatencyValid = true;
                    if (simulatedPreviousSubmissionUs >
                            recordedPreviousSubmissionUs) {
                        occupancyDecisionUs = std::max(
                            decisionUs,
                            saturatingAdd(simulatedPreviousSubmissionUs,
                                          metrics.modeledBusyGapUs));
                    }
                }
                metrics.occupancyDecisionShiftUs.add(absoluteValue(
                    signedDifference(occupancyDecisionUs, decisionUs)));
            }
        }
        const uint64_t simulatedDecisionUs = saturatingAdd(
            occupancyDecisionUs, injectedDecisionDelayUs);
        timelineDetails.simulatedDecisionUs = simulatedDecisionUs;
        timelineDetails.injectedDecisionDelayUs =
            injectedDecisionDelayUs;
        timelineDetails.requestedRenderWakeDelayUs =
            requestedRenderWakeDelayUs;
        timelineDetails.requestedTargetWakeDelayUs =
            requestedTargetWakeDelayUs;
        timelineDetails.injectedPreparationDelayUs =
            injectedPreparationDelayUs;
        timelineDetails.injectedSubmissionDelayUs =
            injectedSubmissionDelayUs;
        timelineDetails.injectedSpacingGuardFeedbackUs =
            injectedSpacingGuardFeedbackUs;
        timelineDetails.injectedSubmissionAdvanceRequestedUs =
            injectedSubmissionAdvanceUs;
        timelineDetails.injectedDisplayTransitionDelayUs =
            injectedDisplayTransitionDelayUs;
        metrics.injectedPeriodicStalls += injectPeriodicStall ? 1 : 0;
        metrics.injectedDecisionDelayUs = saturatingAdd(
            metrics.injectedDecisionDelayUs, injectedDecisionDelayUs);
        metrics.requestedRenderWakeDelayUs = saturatingAdd(
            metrics.requestedRenderWakeDelayUs,
            requestedRenderWakeDelayUs);
        metrics.renderWakeDelayRequestedFrames +=
            requestedRenderWakeDelayUs != 0 ? 1 : 0;
        metrics.requestedTargetWakeDelayUs = saturatingAdd(
            metrics.requestedTargetWakeDelayUs,
            requestedTargetWakeDelayUs);
        metrics.targetWakeDelayRequestedFrames +=
            requestedTargetWakeDelayUs != 0 ? 1 : 0;
        metrics.injectedPreparationDelayUs = saturatingAdd(
            metrics.injectedPreparationDelayUs,
            injectedPreparationDelayUs);
        metrics.injectedSubmissionDelayUs = saturatingAdd(
            metrics.injectedSubmissionDelayUs,
            injectedSubmissionDelayUs);
        metrics.injectedDisplayTransitionDelayUs = saturatingAdd(
            metrics.injectedDisplayTransitionDelayUs,
            injectedDisplayTransitionDelayUs);
        metrics.injectedDisplayTransitionDelayFrames +=
            injectedDisplayTransitionDelayUs != 0 ? 1 : 0;
        metrics.injectedSpacingGuardFeedbackUs = saturatingAdd(
            metrics.injectedSpacingGuardFeedbackUs,
            injectedSpacingGuardFeedbackUs);
        metrics.injectedSpacingGuardFeedbackFrames +=
            injectedSpacingGuardFeedbackUs != 0 ? 1 : 0;
        metrics.injectedSubmissionAdvanceRequestedUs = saturatingAdd(
            metrics.injectedSubmissionAdvanceRequestedUs,
            injectedSubmissionAdvanceUs);
        const bool capturedDecisionRebased =
            optionalUnsignedField(fields, columns.rebased) != 0;
        timelineDetails.recordedExternalRebaseApplied =
            externalRebaseApplied;
        timelineDetails.recordedExternalRebaseFlags =
            externalRebaseFlags;
        timelineDetails.recordedMidframeWindowStateFlags =
            midframeWindowStateFlags;
        if (externalRebaseApplied) {
            referenceController->rebase();
            simulatedController->rebase();
            ++metrics.capturedRebaseEventsReplayed;
        }
        const VrrTimingDecision referenceDecision =
            referenceController->schedule(frame, decisionUs);
        const VrrTimingDecision simulatedDecision =
            simulatedController->schedule(frame, simulatedDecisionUs);
        timelineDetails.simulatedSourcePeriodUs =
            simulatedDecision.sourcePeriodUs;
        timelineDetails.simulatedReadyOffsetUs =
            simulatedDecision.readyOffsetUs;
        timelineDetails.simulatedRenderLeadUs =
            simulatedDecision.renderLeadUs;
        timelineDetails.simulatedReadinessBudgetUs =
            simulatedDecision.readinessBudgetUs;
        timelineDetails.simulatedSourceTimeUs =
            simulatedDecision.sourceTimeUs;
        timelineDetails.simulatedPlayoutDelayUs =
            simulatedDecision.playoutDelayUs;
        timelineDetails.simulatedCadenceSmoothingUs =
            simulatedDecision.cadenceSmoothingUs;
        timelineDetails.simulatedCadenceEligible =
            simulatedDecision.cadenceEligible;
        timelineDetails.simulatedSourceRateChanged =
            simulatedDecision.sourceRateChanged;
        timelineDetails.simulatedPhaseDiscontinuity =
            simulatedDecision.phaseDiscontinuity;
        timelineDetails.simulatedSourceRateHz = roundedRateForPeriod(
            simulatedDecision.sourcePeriodUs);
        timelineDetails.simulatedLatched =
            simulatedDecision.latchedPresentation && simulatedCanLatch;
        const uint64_t recordedTargetUs = unsignedField(
            fields, columns.recordedTargetUs);
        const uint64_t referenceTargetDrift = absoluteValue(
            signedDifference(referenceDecision.targetUs, recordedTargetUs));
        const int originalColumn = traceHeader.indexOf("original_target_us");
        if (originalColumn >= 0 && referenceDecision.originalTargetUs !=
                unsignedField(fields, originalColumn)) {
            std::fprintf(stderr,
                "Original deadline diverged on frame %d: replay=%llu recorded=%llu source=%llu period=%llu smoothing=%lld buffer=%llu\n",
                frameNumber, (unsigned long long)referenceDecision.originalTargetUs,
                (unsigned long long)unsignedField(fields, originalColumn),
                (unsigned long long)referenceDecision.sourceTimeUs,
                (unsigned long long)referenceDecision.sourcePeriodUs,
                (long long)referenceDecision.cadenceSmoothingUs,
                (unsigned long long)referenceDecision.playoutDelayUs);
            return 3;
        }
        metrics.referenceTargetDrift.add(referenceTargetDrift);
        if (capturedParameters.playoutPredictionEnabled) {
            const std::pair<const char*, uint64_t> predictions[] = {
                {"original_scanout_us", referenceDecision.originalScanoutUs},
                {"predicted_scanout_us", referenceDecision.predictedScanoutUs},
                {"compositor_lead_us", referenceDecision.compositorLeadUs},
                {"recovery_headroom_us", referenceDecision.recoveryHeadroomUs}
            };
            for (const auto& prediction : predictions) {
                const int column = traceHeader.indexOf(prediction.first);
                if (column < 0 || optionalUnsignedField(fields, column) != prediction.second) {
                    std::fprintf(stderr, "Prediction drift: %s on frame %d\n", prediction.first, frameNumber);
                    ++metrics.invalidControllerLifecycleRows;
                }
            }
        }
        if (capturedParameters.playoutSmoothnessFeedbackEnabled) {
            const std::pair<const char*, uint64_t> feedback[] = {
                {"smoothness_protection_us", referenceDecision.smoothnessProtectionUs},
                {"requested_playout_delay_us", referenceDecision.requestedPlayoutDelayUs},
                {"submission_smoothness_samples", referenceDecision.submissionSmoothnessSamples},
                {"submission_smoothness_misses", referenceDecision.submissionSmoothnessMisses},
                {"native_smoothness_samples", referenceDecision.nativeSmoothnessSamples},
                {"native_smoothness_misses", referenceDecision.nativeSmoothnessMisses},
                {"playout_capacity_limited", referenceDecision.playoutCapacityLimited}
            };
            for (const auto& value : feedback) {
                const int column = traceHeader.indexOf(value.first);
                if (column < 0 || optionalUnsignedField(fields, column) != value.second) {
                    std::fprintf(stderr, "Smoothness feedback drift: %s on frame %d\n", value.first, frameNumber);
                    ++metrics.invalidControllerLifecycleRows;
                }
            }
        }
        metrics.lastFeedbackDecision = simulatedDecision;
        metrics.feedbackCapacityLimitedFrames += simulatedDecision.playoutCapacityLimited;
        metrics.exactReferenceTargets += referenceTargetDrift == 0 ? 1 : 0;
        metrics.referenceSourceIntervalDrift.add(absoluteValue(
            signedDifference(
                referenceDecision.sourceIntervalUs,
                unsignedField(fields, columns.sourceIntervalUs))));
        metrics.referenceSourceTimeDrift.add(absoluteValue(signedDifference(
            referenceDecision.sourceTimeUs,
            unsignedField(fields, columns.sourceTimeUs))));
        metrics.referenceSourcePeriodDrift.add(absoluteValue(signedDifference(
            referenceDecision.sourcePeriodUs,
            unsignedField(fields, columns.sourcePeriodUs))));
        metrics.referenceReadyOffsetDrift.add(absoluteDifference(
            referenceDecision.readyOffsetUs,
            signedField(fields, columns.readyOffsetUs)));
        metrics.referenceReadinessBudgetDrift.add(absoluteDifference(
            referenceDecision.readinessBudgetUs,
            signedField(fields, columns.readinessBudgetUs)));
        metrics.referenceTimingBudgetDrift.add(absoluteValue(signedDifference(
            referenceDecision.timingBudgetUs,
            unsignedField(fields, columns.timingBudgetUs))));
        metrics.referenceRenderLeadDrift.add(absoluteValue(signedDifference(
            referenceDecision.renderLeadUs,
            unsignedField(fields, columns.renderLeadUs))));
        if (columns.gpuReadinessLeadUs >= 0) {
            if (referenceDecision.gpuReadinessLeadUs !=
                    unsignedField(fields, columns.gpuReadinessLeadUs)) {
                ++metrics.invalidControllerLifecycleRows;
            }
        }
        metrics.referenceRenderWakeLeadDrift.add(absoluteValue(
            signedDifference(referenceDecision.renderWakeLeadUs,
                unsignedField(fields, columns.renderWakeLeadUs))));
        metrics.referenceTargetWakeLeadDrift.add(absoluteValue(
            signedDifference(referenceDecision.targetWakeLeadUs,
                unsignedField(fields, columns.targetWakeLeadUs))));
        metrics.referenceGuardDrift.add(absoluteValue(signedDifference(
            referenceDecision.guardUs,
            unsignedField(fields, columns.guardUs))));
        metrics.referenceHeadroomDrift.add(absoluteValue(signedDifference(
            referenceDecision.headroomUs,
            unsignedField(fields, columns.headroomUs))));
        metrics.referenceRenderStartDrift.add(absoluteValue(signedDifference(
            referenceDecision.renderStartUs,
            unsignedField(fields, columns.renderStartUs))));
        metrics.referenceLatchedPresentationMismatches +=
            referenceDecision.latchedPresentation !=
                (unsignedField(fields, columns.latchedPresent) != 0) ? 1 : 0;
        metrics.referenceUsedRtpTimestampMismatches +=
            referenceDecision.usedRtpTimestamp !=
                (unsignedField(fields, columns.usedRtpTimestamp) != 0) ? 1 : 0;
        metrics.referenceCadenceEligibleMismatches +=
            referenceDecision.cadenceEligible !=
                (unsignedField(fields, columns.cadenceEligible) != 0) ? 1 : 0;
        metrics.referenceSourceRateChangedMismatches +=
            referenceDecision.sourceRateChanged !=
                (unsignedField(fields, columns.sourceRateChanged) != 0) ? 1 : 0;
        metrics.referencePhaseDiscontinuityMismatches +=
            referenceDecision.phaseDiscontinuity !=
                (unsignedField(fields, columns.phaseDiscontinuity) != 0) ?
                    1 : 0;
        metrics.referenceRebasedMismatches +=
            referenceDecision.rebased != capturedDecisionRebased ? 1 : 0;
        metrics.simulatedTargetDrift.add(absoluteValue(signedDifference(
            simulatedDecision.targetUs, recordedTargetUs)));

        const uint64_t recordedPreparationStartUs = unsignedField(
            fields, columns.preparationStartUs);
        const uint64_t recordedPreparationEndUs = unsignedField(
            fields, columns.preparationEndUs);
        const uint64_t recordedRenderStartUs = unsignedField(
            fields, columns.renderStartUs);
        const uint64_t recordedSubmissionUs = unsignedField(
            fields, columns.submissionBoundaryUs);
        const uint64_t recordedPresentStartUs = optionalUnsignedField(
            fields, columns.presentStartUs);
        const uint64_t recordedPresentEndUs = optionalUnsignedField(
            fields, columns.presentEndUs);
        const uint64_t recordedPresentCallUs = unsignedField(
            fields, columns.presentCallUs);
        const uint64_t preparationUs = unsignedField(fields,
                                                      columns.preparationUs);
        const uint64_t simulatedPreparationUs =
            hasPreparationTelemetry ?
                saturatingAdd(
                    preparationUs, injectedPreparationDelayUs) : 0;
        const uint64_t renderWaitEntryOffsetUs =
            recordedRenderWaitEntryUs >= decisionUs ?
                recordedRenderWaitEntryUs - decisionUs : 0;
        const uint64_t simulatedRenderWaitEntryUs = saturatingAdd(
            simulatedDecisionUs, renderWaitEntryOffsetUs);
        const bool exactRenderWaitLifecycle =
            metrics.waitLifecycleTelemetryAvailable &&
            renderWaitEvidence.callEntryUs != 0;
        const VrrWakeDelayInjectionResult renderWakeInjection =
            exactRenderWaitLifecycle ?
                evaluateVrrRecordedWakeDelayInjection(
                    renderWaitEvidence,
                    simulatedRenderWaitEntryUs,
                    simulatedDecision.renderStartUs,
                    0,
                    kWaiterMaximumActiveWaitUs,
                    kWaiterMaximumAdditionalWakeLeadUs,
                    requestedRenderWakeDelayUs) :
                evaluateVrrWakeDelayInjection(
                    simulatedRenderWaitEntryUs,
                    simulatedDecision.renderStartUs,
                    kWaiterMaximumActiveWaitUs, 0,
                    kWaiterMaximumAdditionalWakeLeadUs,
                    requestedRenderWakeDelayUs);
        const uint64_t injectedRenderWakeDelayUs =
            renderWakeInjection.appliedDelayUs;
        timelineDetails.renderWakeDelayEligible =
            renderWakeInjection.coarseSleepExpected;
        timelineDetails.renderWaitPathCompared =
            exactRenderWaitLifecycle;
        timelineDetails.renderWaitPathMatchesCaptured =
            exactRenderWaitLifecycle &&
            renderWakeInjection.recordedPathMatchesCandidate;
        timelineDetails.renderWaitRecordedFinalResidualUsed =
            exactRenderWaitLifecycle &&
            renderWakeInjection.usedRecordedFinalResidual;
        timelineDetails.injectedRenderWakeDelayUs =
            injectedRenderWakeDelayUs;
        timelineDetails.absorbedRenderWakeDelayUs =
            renderWakeInjection.absorbedDelayUs;
        timelineDetails.renderWakeExecutionDelayUs =
            renderWakeInjection.executionDelayUs;
        metrics.injectedRenderWakeDelayUs = saturatingAdd(
            metrics.injectedRenderWakeDelayUs,
            injectedRenderWakeDelayUs);
        metrics.suppressedRenderWakeDelayUs = saturatingAdd(
            metrics.suppressedRenderWakeDelayUs,
            renderWakeInjection.suppressedDelayUs);
        metrics.absorbedRenderWakeDelayUs = saturatingAdd(
            metrics.absorbedRenderWakeDelayUs,
            renderWakeInjection.absorbedDelayUs);
        metrics.renderWakeExecutionDelayUs = saturatingAdd(
            metrics.renderWakeExecutionDelayUs,
            renderWakeInjection.executionDelayUs);
        metrics.renderWakeDelayEligibleFrames +=
            requestedRenderWakeDelayUs != 0 &&
                renderWakeInjection.coarseSleepExpected ? 1 : 0;
        metrics.simulatedRenderWaitPathComparisons +=
            exactRenderWaitLifecycle ? 1 : 0;
        metrics.simulatedRenderWaitPathMatches +=
            exactRenderWaitLifecycle &&
                renderWakeInjection.recordedPathMatchesCandidate ? 1 : 0;
        metrics.simulatedRenderWaitRecordedFinalResidualRows +=
            exactRenderWaitLifecycle &&
                renderWakeInjection.usedRecordedFinalResidual ? 1 : 0;

        const uint64_t recordedRenderFloorUs =
            exactRenderWaitLifecycle ?
                std::max(
                    decisionUs, renderWaitEvidence.finalNowUs) :
                std::max(decisionUs, recordedRenderStartUs);
        const uint64_t prepareStartResidualUs =
            recordedPreparationStartUs >= recordedRenderFloorUs ?
                recordedPreparationStartUs - recordedRenderFloorUs : 0;
        const uint64_t simulatedPreparationStartUs = saturatingAdd(
            exactRenderWaitLifecycle ?
                std::max(
                    simulatedDecisionUs,
                    renderWakeInjection.simulatedFinalUs) :
                std::max(
                    simulatedDecisionUs,
                    simulatedDecision.renderStartUs),
            saturatingAdd(
                prepareStartResidualUs,
                exactRenderWaitLifecycle ?
                    0 : injectedRenderWakeDelayUs));
        const uint64_t simulatedPreparationEndUs = saturatingAdd(
            simulatedPreparationStartUs, simulatedPreparationUs);
        const uint64_t targetWaitEntryOffsetUs =
            recordedTargetWaitEntryUs >= recordedPreparationEndUs ?
                recordedTargetWaitEntryUs -
                    recordedPreparationEndUs : 0;
        const uint64_t simulatedTargetWaitEntryUs = saturatingAdd(
            simulatedPreparationEndUs, targetWaitEntryOffsetUs);
        const bool exactTargetWaitLifecycle =
            metrics.waitLifecycleTelemetryAvailable &&
            targetWaitEvidence.callEntryUs != 0;
        const VrrWakeDelayInjectionResult targetWakeInjection =
            exactTargetWaitLifecycle ?
                evaluateVrrRecordedWakeDelayInjection(
                    targetWaitEvidence,
                    simulatedTargetWaitEntryUs,
                    simulatedDecision.targetUs,
                    simulatedDecision.targetWakeLeadUs,
                    kWaiterMaximumActiveWaitUs,
                    kWaiterMaximumAdditionalWakeLeadUs,
                    requestedTargetWakeDelayUs) :
                evaluateVrrWakeDelayInjection(
                    simulatedTargetWaitEntryUs,
                    simulatedDecision.targetUs,
                    kWaiterMaximumActiveWaitUs,
                    simulatedDecision.targetWakeLeadUs,
                    kWaiterMaximumAdditionalWakeLeadUs,
                    requestedTargetWakeDelayUs);
        const uint64_t injectedTargetWakeDelayUs =
            targetWakeInjection.appliedDelayUs;
        timelineDetails.targetWakeDelayEligible =
            targetWakeInjection.coarseSleepExpected;
        timelineDetails.targetWaitPathCompared =
            exactTargetWaitLifecycle;
        timelineDetails.targetWaitPathMatchesCaptured =
            exactTargetWaitLifecycle &&
            targetWakeInjection.recordedPathMatchesCandidate;
        timelineDetails.targetWaitRecordedFinalResidualUsed =
            exactTargetWaitLifecycle &&
            targetWakeInjection.usedRecordedFinalResidual;
        timelineDetails.injectedTargetWakeDelayUs =
            injectedTargetWakeDelayUs;
        timelineDetails.absorbedTargetWakeDelayUs =
            targetWakeInjection.absorbedDelayUs;
        timelineDetails.targetWakeExecutionDelayUs =
            targetWakeInjection.executionDelayUs;
        metrics.injectedTargetWakeDelayUs = saturatingAdd(
            metrics.injectedTargetWakeDelayUs,
            injectedTargetWakeDelayUs);
        metrics.suppressedTargetWakeDelayUs = saturatingAdd(
            metrics.suppressedTargetWakeDelayUs,
            targetWakeInjection.suppressedDelayUs);
        metrics.absorbedTargetWakeDelayUs = saturatingAdd(
            metrics.absorbedTargetWakeDelayUs,
            targetWakeInjection.absorbedDelayUs);
        metrics.targetWakeExecutionDelayUs = saturatingAdd(
            metrics.targetWakeExecutionDelayUs,
            targetWakeInjection.executionDelayUs);
        metrics.targetWakeDelayEligibleFrames +=
            requestedTargetWakeDelayUs != 0 &&
                targetWakeInjection.coarseSleepExpected ? 1 : 0;
        metrics.simulatedTargetWaitPathComparisons +=
            exactTargetWaitLifecycle ? 1 : 0;
        metrics.simulatedTargetWaitPathMatches +=
            exactTargetWaitLifecycle &&
                targetWakeInjection.recordedPathMatchesCandidate ? 1 : 0;
        metrics.simulatedTargetWaitRecordedFinalResidualRows +=
            exactTargetWaitLifecycle &&
                targetWakeInjection.usedRecordedFinalResidual ? 1 : 0;

        if (hasPreparationTelemetry) {
            const uint64_t acquire = optionalUnsignedField(fields, traceHeader.indexOf("prepare_timing_valid")) ?
                optionalUnsignedField(fields, traceHeader.indexOf("prepare_acquire_us")) : 0;
            const bool gpuReadyCompleted = gpuReadyTimingDeclared &&
                gpuReadyWaitResultDeclared && gpuReadyWaitResult == 0 &&
                gpuReadyTimeUs >= gpuReadyWaitStartUs;
            referenceController->notePreparationDuration(
                preparationUs, acquire, recordedPreparationEndUs,
                gpuReadyCompleted ? gpuReadyWaitUs : 0);
            simulatedController->notePreparationDuration(
                simulatedPreparationUs, acquire, simulatedPreparationEndUs,
                gpuReadyCompleted ? gpuReadyWaitUs : 0);
            referenceController->noteGpuReadyWait(
                gpuReadyWaitUs, gpuReadyCompleted, gpuReadyTimeUs);
            simulatedController->noteGpuReadyWait(
                gpuReadyWaitUs, gpuReadyCompleted, simulatedPreparationEndUs);
        }
        const bool spacingHadPriorSubmission =
            referenceController->hasLastSubmission();
        const uint64_t spacingPriorSubmissionUs =
            referenceController->lastSubmissionUs();
        const uint64_t spacingEarliestBeforeCleanUs =
            referenceController->earliestSubmissionUs();
        uint64_t derivedSpacingGuardFeedbackUs =
            recordedSpacingGuardFeedbackUs;
        if (normalPresentationLifecycle &&
                metrics.spacingLifecycleTimingTelemetryAvailable) {
            const uint64_t minimumUntornUs =
                spacingHadPriorSubmission ?
                    saturatingAdd(
                        spacingPriorSubmissionUs,
                        unsignedField(
                            fields, columns.displayPeriodUs)) :
                    0;
            derivedSpacingGuardFeedbackUs =
                spacingHadPriorSubmission &&
                    recordedSpacingRecheckUs < minimumUntornUs ?
                minimumUntornUs - recordedSpacingRecheckUs : 0;
        }
        if (normalPresentationLifecycle) {
            // The worker first records a clean boundary and then, only if its
            // second clock check finds a deficit, applies that correction.
            // Schema 3 stores the combined wait correction, not whether the
            // second check also fired the guard-learning callback. Treating
            // every first-check wait as feedback incorrectly inflates guard.
            referenceController->noteSpacingDeficit(0);
            simulatedController->noteSpacingDeficit(0);
            if (derivedSpacingGuardFeedbackUs != 0) {
                referenceController->noteSpacingDeficit(
                    derivedSpacingGuardFeedbackUs);
            }
            const uint64_t simulatedSpacingGuardFeedbackUs =
                std::max(
                    derivedSpacingGuardFeedbackUs,
                    injectedSpacingGuardFeedbackUs);
            if (simulatedSpacingGuardFeedbackUs != 0) {
                simulatedController->noteSpacingDeficit(
                    simulatedSpacingGuardFeedbackUs);
            }
            referenceController->noteSchedulerDelays(
                unsignedField(fields, columns.renderWaitOvershootUs),
                unsignedField(fields, columns.targetSchedulerDelayUs),
                unsignedField(fields, columns.targetSchedulerDelayValid) != 0);
            const uint64_t simulatedRenderSchedulerFeedbackUs =
                exactRenderWaitLifecycle ?
                    (renderWakeInjection.deadlineInFuture ?
                        positiveDifference(
                            renderWakeInjection.simulatedFinalUs,
                            simulatedDecision.renderStartUs) :
                        0) :
                    (renderWakeInjection.deadlineInFuture ?
                        saturatingAdd(
                            unsignedField(
                                fields,
                                columns.renderWaitOvershootUs),
                            injectedRenderWakeDelayUs) :
                        0);
            const bool simulatedTargetSchedulerDelayValid =
                exactTargetWaitLifecycle ?
                    targetWakeInjection.schedulerDelayValid :
                    targetWakeInjection.coarseSleepExpected;
            const uint64_t simulatedTargetSchedulerFeedbackUs =
                simulatedTargetSchedulerDelayValid ?
                    (exactTargetWaitLifecycle ?
                        targetWakeInjection.schedulerDelayUs :
                        saturatingAdd(
                            unsignedField(
                                fields,
                                columns.targetSchedulerDelayUs),
                            injectedTargetWakeDelayUs)) :
                    0;
            simulatedController->noteSchedulerDelays(
                simulatedRenderSchedulerFeedbackUs,
                simulatedTargetSchedulerFeedbackUs,
                simulatedTargetSchedulerDelayValid);
        }
        const uint64_t spacingEarliestAfterFeedbackUs =
            referenceController->earliestSubmissionUs();

        const bool lifecycleRecordsSubmission =
            normalPresentationLifecycle || interruptedLifecycle;
        const bool expectedHadPriorSubmission =
            lifecycleRecordsSubmission &&
            referenceController->hasLastSubmission();
        const bool recordedHadPriorSubmission =
            unsignedField(fields, columns.hadPriorSubmission) != 0;
        metrics.hadPriorSubmissionMismatches +=
            recordedHadPriorSubmission != expectedHadPriorSubmission ? 1 : 0;
        metrics.submissionBoundaryMismatches +=
            !presented && recordedSubmissionUs != 0 ? 1 : 0;
        const int64_t expectedSubmitErrorUs = presented ?
            signedDifference(recordedSubmissionUs, recordedTargetUs) : 0;
        metrics.submitErrorMismatches +=
            signedField(fields, columns.submitErrorUs) !=
                expectedSubmitErrorUs ? 1 : 0;
        uint64_t expectedSubmissionSpacingUs = 0;
        int64_t expectedSpacingMarginUs = 0;
        if (presented && expectedHadPriorSubmission) {
            const uint64_t priorSubmissionUs =
                referenceController->lastSubmissionUs();
            expectedSubmissionSpacingUs =
                recordedSubmissionUs >= priorSubmissionUs ?
                    recordedSubmissionUs - priorSubmissionUs : 0;
            expectedSpacingMarginUs = signedDifference(
                recordedSubmissionUs,
                saturatingAdd(
                    priorSubmissionUs,
                    unsignedField(fields, columns.displayPeriodUs)));
        }
        metrics.submissionSpacingMismatches +=
            unsignedField(fields, columns.submissionSpacingUs) !=
                expectedSubmissionSpacingUs ? 1 : 0;
        metrics.spacingMarginMismatches +=
            signedField(fields, columns.spacingMarginUs) !=
                expectedSpacingMarginUs ? 1 : 0;
        const bool recordedLatchedRequest =
            unsignedField(fields, columns.latchedPresent) != 0;
        const QByteArray expectedRecordedTear =
            simulatedTearClassification(
                presented, recordedLatchedRequest, rowCanLatch,
                expectedHadPriorSubmission, recordedSubmissionUs,
                referenceController->lastSubmissionUs(),
                unsignedField(fields, columns.displayPeriodUs));
        metrics.tearClassificationMismatches +=
            recordedTear != expectedRecordedTear ? 1 : 0;
        const bool expectedTearRisk =
            presented && !recordedLatchedRequest &&
            expectedHadPriorSubmission && expectedSpacingMarginUs < 0;
        metrics.tearRiskMismatches +=
            (unsignedField(fields, columns.tearRisk) != 0) !=
                expectedTearRisk ? 1 : 0;
        const bool hasPresentOperation =
            recordedPresentStartUs != 0 || recordedPresentEndUs != 0 ||
            recordedPresentCallUs != 0;
        if (metrics.presenterSubmissionTimingTelemetryAvailable) {
            const uint64_t submissionOperationStartUs =
                hasPresentOperation ? recordedPresentStartUs :
                                      recordedPreparationStartUs;
            const uint64_t submissionOperationEndUs =
                hasPresentOperation ? recordedPresentEndUs :
                                      recordedPreparationEndUs;
            const VrrPresenterSubmissionAudit submissionAudit =
                evaluateVrrPresenterSubmission(
                    presented,
                    presenterSubmissionTimeDeclared,
                    presenterSubmissionTimeUs,
                    submissionOperationStartUs,
                    submissionOperationEndUs,
                    presenterSubmissionTimeUsed,
                    recordedSubmissionUs);
            metrics.presenterSubmissionTimingRelationshipMismatchRows +=
                submissionAudit.relationshipValid ? 0 : 1;
            metrics.presenterSubmissionTimestampUsedRows +=
                presenterSubmissionTimeUsed ? 1 : 0;
        }
        if (metrics.spacingLifecycleTimingTelemetryAvailable) {
            const VrrSpacingLifecycleTimingAudit spacingTimingAudit =
                evaluateVrrSpacingLifecycleTiming(
                    normalPresentationLifecycle,
                    spacingHadPriorSubmission,
                    spacingPriorSubmissionUs,
                    unsignedField(fields, columns.displayPeriodUs),
                    recordedTargetUs,
                    spacingEarliestBeforeCleanUs,
                    spacingEarliestAfterFeedbackUs,
                    recordedSpacingCheckUs,
                    recordedPresentationFloorUs,
                    recordedSpacingRecheckUs,
                    recordedSpacingDeficitUs,
                    recordedSpacingGuardFeedbackUs,
                    recordedSpacingCorrected,
                    recordedSpacingCorrectedFloorUs,
                    recordedCorrectionWaitStartUs,
                    recordedCorrectionWaitEndUs,
                    recordedPresentStartUs);
            metrics.spacingLifecycleTimingRelationshipMismatchRows +=
                spacingTimingAudit.relationshipValid ? 0 : 1;
            metrics.spacingLifecycleTimingValidatedRows +=
                normalPresentationLifecycle ? 1 : 0;
            if (normalPresentationLifecycle &&
                    recordedSpacingRecheckUs >=
                        recordedSpacingCheckUs) {
                metrics.observedSpacingCheckToRecheck.add(
                    recordedSpacingRecheckUs -
                        recordedSpacingCheckUs);
            }
        }
        const bool targetWaitPresent =
            recordedTargetWaitEntryUs != 0 ||
            recordedTargetWaitFinalUs != 0;
        const bool submissionFloorPresent =
            recordedSpacingCheckUs != 0 ||
            recordedPresentationFloorUs != 0;
        const bool correctionWaitPresent =
            recordedCorrectionWaitStartUs != 0 ||
            recordedCorrectionWaitEndUs != 0;
        if (traceSchema >= 5) {
            if (targetWaitPresent &&
                    recordedTargetWaitEntryUs <
                        recordedPreparationEndUs) {
                ++metrics.waitBoundaryOrderViolations;
            }
            if (submissionFloorPresent &&
                    recordedSpacingCheckUs <
                        std::max(recordedPreparationEndUs,
                                 recordedTargetWaitFinalUs)) {
                ++metrics.waitBoundaryOrderViolations;
            }
        }
        bool presentOperationOrderValid = false;
        bool presentOperationDurationValid = false;
        if (hasPresentOperation || presented) {
            presentOperationOrderValid =
                recordedPresentStartUs != 0 &&
                recordedPresentEndUs >= recordedPresentStartUs &&
                recordedPresentStartUs >= decisionUs &&
                (!hasPreparationTelemetry ||
                 recordedPresentStartUs >= recordedPreparationEndUs) &&
                (!targetWaitPresent ||
                 recordedPresentStartUs >= recordedTargetWaitFinalUs) &&
                (!submissionFloorPresent ||
                 (recordedPresentStartUs >= recordedSpacingCheckUs &&
                  recordedPresentStartUs >=
                      recordedPresentationFloorUs)) &&
                (!correctionWaitPresent ||
                 recordedPresentStartUs >= recordedCorrectionWaitEndUs);
            metrics.presentOperationOrderViolations +=
                presentOperationOrderValid ? 0 : 1;
            presentOperationDurationValid =
                recordedPresentEndUs >= recordedPresentStartUs &&
                recordedPresentEndUs - recordedPresentStartUs ==
                    recordedPresentCallUs;
            metrics.presentOperationDurationMismatchRows +=
                presentOperationDurationValid ? 0 : 1;
        }
        const bool gpuReadyTimingValid = gpuReadyTimingDeclared;
        bool gpuReadyBoundsValid = false;
        timelineDetails.recordedGpuReadyTimingValid =
            gpuReadyTimingValid;
        if (metrics.gpuReadyStageTimingTelemetryAvailable &&
                !gpuReadyVulkanPoll) {
            const VrrGpuReadyStageTimingAudit stageTimingAudit =
                evaluateVrrGpuReadyStageTiming(
                    recordedPreparationStartUs,
                    recordedPreparationEndUs,
                    gpuReadyAttempted,
                    gpuReadySignalSucceeded,
                    gpuReadySetEventSucceeded,
                    gpuReadySignalStartUs,
                    gpuReadySignalEndUs,
                    gpuReadyFlushStartUs,
                    gpuReadyFlushEndUs,
                    gpuReadySetEventStartUs,
                    gpuReadySetEventEndUs,
                    gpuReadyPollStartUs,
                    gpuReadyPollEndUs,
                    gpuReadyWaitStartUs,
                    gpuReadyTimeUs);
            metrics.gpuReadyStageTimingRelationshipMismatchRows +=
                stageTimingAudit.relationshipValid ? 0 : 1;
            if (gpuReadyAttempted &&
                    gpuReadySignalEndUs >=
                        gpuReadySignalStartUs) {
                metrics.observedGpuReadySignalCall.add(
                    gpuReadySignalEndUs -
                        gpuReadySignalStartUs);
            }
            if (gpuReadySignalSucceeded &&
                    gpuReadyFlushEndUs >=
                        gpuReadyFlushStartUs) {
                metrics.observedGpuReadyFlushCall.add(
                    gpuReadyFlushEndUs -
                        gpuReadyFlushStartUs);
            }
            if (gpuReadySignalSucceeded &&
                    gpuReadySetEventEndUs >=
                        gpuReadySetEventStartUs) {
                metrics.observedGpuReadySetEventCall.add(
                    gpuReadySetEventEndUs -
                        gpuReadySetEventStartUs);
            }
        }
        const bool gpuReadyWaitOperationObserved =
            metrics.gpuReadyNativeResultTelemetryAvailable ?
                gpuReadyWaitResultDeclared : gpuReadyTimingValid;
        if (gpuReadyWaitOperationObserved) {
            bool gpuReadyOrderValid =
                gpuReadyWaitStartUs != 0 &&
                gpuReadyTimeUs >= gpuReadyWaitStartUs &&
                gpuReadyWaitStartUs >= recordedPreparationStartUs &&
                gpuReadyTimeUs <= recordedPreparationEndUs;
            if (metrics.gpuReadyBoundsTelemetryAvailable &&
                    !gpuReadyVulkanPoll) {
                gpuReadyOrderValid =
                    gpuReadyOrderValid &&
                    gpuReadySignalStartUs != 0 &&
                    gpuReadyPollStartUs >= gpuReadySignalStartUs &&
                    gpuReadyPollEndUs >= gpuReadyPollStartUs &&
                    gpuReadyWaitStartUs >= gpuReadyPollEndUs;
            }
            else if (gpuReadyVulkanPoll && gpuReadyAttempted) {
                gpuReadyOrderValid =
                    gpuReadyOrderValid &&
                    gpuReadyPollStartUs != 0 &&
                    gpuReadyPollEndUs >= gpuReadyPollStartUs &&
                    gpuReadyWaitStartUs == gpuReadyPollStartUs;
            }
            metrics.gpuReadyOrderViolations +=
                gpuReadyOrderValid ? 0 : 1;
        }
        if (gpuReadyTimingValid) {
            ++metrics.gpuReadyTimingValidRows;
            metrics.observedGpuReadyWait.add(gpuReadyWaitUs);
            if (gpuReadyTimeUs >= gpuReadyWaitStartUs &&
                    gpuReadyTimeUs - gpuReadyWaitStartUs !=
                        gpuReadyWaitUs) {
                ++metrics.gpuReadyDurationMismatchRows;
            }
            if (metrics.gpuReadyBoundsTelemetryAvailable &&
                    !gpuReadyVulkanPoll) {
                const VrrGpuCompletionBounds expectedBounds =
                    evaluateVrrGpuCompletionBounds(
                        recordedPreparationStartUs,
                        recordedPreparationEndUs,
                        gpuReadySignalStartUs,
                        gpuReadyPollStartUs,
                        gpuReadyPollEndUs,
                        gpuReadyFenceValue,
                        gpuReadyPollCompletedValue,
                        gpuReadyCompletedBeforeWait,
                        gpuReadyWaitStartUs,
                        gpuReadyTimeUs);
                metrics.gpuReadyFenceRelationshipMismatchRows +=
                    expectedBounds.fenceRelationshipValid ? 0 : 1;
                gpuReadyBoundsValid =
                    expectedBounds.valid &&
                    gpuReadyCompletionLowerBoundUs ==
                        expectedBounds.lowerBoundUs &&
                    gpuReadyCompletionUpperBoundUs ==
                        expectedBounds.upperBoundUs &&
                    gpuReadyCompletionUncertaintyUs ==
                        expectedBounds.uncertaintyUs;
                metrics.gpuReadyBoundsDerivationMismatchRows +=
                    gpuReadyBoundsValid ? 0 : 1;
                if (gpuReadyBoundsValid) {
                    metrics.observedGpuReadyPollFenceLag.add(
                        gpuReadyFenceValue -
                            gpuReadyPollCompletedValue);
                    metrics.observedGpuReadyCompletionUncertainty.add(
                        gpuReadyCompletionUncertaintyUs);
                    metrics.
                        observedGpuReadySignalToCompletionUpperBound.add(
                            gpuReadyCompletionUpperBoundUs -
                                gpuReadySignalStartUs);
                }
            }
            else if (gpuReadyVulkanPoll) {
                // The writer derives the shared completion bracket directly
                // from the texture-poll interval. Validate that derivation
                // without applying the D3D11 fence-value relationship.
                const bool boundsValid =
                    gpuReadyCompletionLowerBoundUs ==
                        gpuReadyPollStartUs &&
                    gpuReadyCompletionUpperBoundUs ==
                        gpuReadyTimeUs &&
                    gpuReadyCompletionUpperBoundUs >=
                        gpuReadyCompletionLowerBoundUs &&
                    gpuReadyCompletionUncertaintyUs ==
                        gpuReadyCompletionUpperBoundUs -
                            gpuReadyCompletionLowerBoundUs;
                metrics.gpuReadyBoundsDerivationMismatchRows +=
                    boundsValid ? 0 : 1;
            }
        }
        if (columns.gpuReadinessAppliedUs >= 0 &&
                gpuReadinessAppliedUs != gpuReadyWaitUs) {
            // The applied diagnostic is the same completed wait already
            // derived from the stage timestamps. Keep both fields tied so a
            // trace cannot claim a different cost than the one used for
            // readiness adaptation.
            ++metrics.gpuReadyDurationMismatchRows;
        }
        timelineDetails.recordedGpuReadySignalStartUs =
            gpuReadySignalStartUs;
        timelineDetails.recordedGpuReadySignalEndUs =
            gpuReadySignalEndUs;
        timelineDetails.recordedGpuReadyFlushStartUs =
            gpuReadyFlushStartUs;
        timelineDetails.recordedGpuReadyFlushEndUs =
            gpuReadyFlushEndUs;
        timelineDetails.recordedGpuReadySetEventStartUs =
            gpuReadySetEventStartUs;
        timelineDetails.recordedGpuReadySetEventEndUs =
            gpuReadySetEventEndUs;
        timelineDetails.recordedGpuReadyPollStartUs =
            gpuReadyPollStartUs;
        timelineDetails.recordedGpuReadyPollEndUs =
            gpuReadyPollEndUs;
        timelineDetails.recordedGpuReadyFenceValue =
            gpuReadyFenceValue;
        timelineDetails.recordedGpuReadyPollCompletedValue =
            gpuReadyPollCompletedValue;
        timelineDetails.recordedGpuReadyCompletedBeforeWait =
            gpuReadyCompletedBeforeWait;
        timelineDetails.recordedGpuReadyCompletionBoundsValid =
            gpuReadyBoundsValid;
        timelineDetails.recordedGpuReadyCompletionLowerBoundUs =
            gpuReadyCompletionLowerBoundUs;
        timelineDetails.recordedGpuReadyCompletionUpperBoundUs =
            gpuReadyCompletionUpperBoundUs;
        timelineDetails.recordedGpuReadyCompletionUncertaintyUs =
            gpuReadyCompletionUncertaintyUs;
        timelineDetails.recordedGpuReadyWaitStartUs =
            gpuReadyWaitStartUs;
        timelineDetails.recordedGpuReadyWaitReturnUs =
            gpuReadyTimeUs;
        timelineDetails.recordedGpuReadyWaitUs =
            gpuReadyWaitUs;
        if (hasPreparationTelemetry || presented) {
            const bool preparationOrderValid =
                recordedPreparationStartUs != 0 &&
                recordedPreparationEndUs >= recordedPreparationStartUs &&
                recordedPreparationStartUs >=
                    std::max({
                        decisionUs,
                        recordedRenderStartUs,
                        recordedRenderWaitFinalUs,
                    });
            metrics.preparationOrderViolations +=
                preparationOrderValid ? 0 : 1;
            if (preparationOrderValid &&
                    recordedPreparationEndUs -
                        recordedPreparationStartUs != preparationUs) {
                ++metrics.preparationDurationMismatchRows;
            }
        }
        const uint64_t nativePresentCallUs = unsignedField(
            fields, columns.nativePresentCallUs);
        const bool nativePresentTimingValid =
            nativePresentTimingDeclared &&
            metrics.presentTimingIntegrityTelemetryAvailable &&
            nativePresentStartUs != 0;
        if (nativePresentTimingDeclared) {
            const bool nativePresentOrderValid =
                nativePresentStartUs != 0 &&
                nativePresentEndUs >= nativePresentStartUs &&
                presentOperationOrderValid &&
                nativePresentStartUs >= recordedPresentStartUs &&
                nativePresentEndUs <= recordedPresentEndUs;
            metrics.nativePresentOrderViolations +=
                nativePresentOrderValid ? 0 : 1;
            if (nativePresentEndUs < nativePresentStartUs ||
                    nativePresentEndUs - nativePresentStartUs !=
                        nativePresentCallUs) {
                ++metrics.nativePresentDurationMismatchRows;
            }
        }
        if (metrics.postPresentQueryTimingTelemetryAvailable) {
            const bool expectedPostPresentQueryTiming =
                deepTraceRow &&
                presented &&
                nativeBackendDeclared &&
                nativeBackend == kNativeBackendDxgi;
            const uint64_t priorObservationEndUs =
                nativeRasterAfterQueryResultDeclared ?
                    nativeRasterAfterQueryEndUs :
                    nativePresentEndUs;
            const VrrPostPresentQueryTimingAudit queryTimingAudit =
                evaluateVrrPostPresentQueryTiming(
                    expectedPostPresentQueryTiming,
                    submissionIdQueryResultDeclared,
                    frameStatsQueryResultDeclared,
                    priorObservationEndUs,
                    submissionIdQueryStartUs,
                    submissionIdQueryEndUs,
                    frameStatsQueryStartUs,
                    frameStatsQueryEndUs,
                    recordedPresentEndUs);
            metrics.postPresentQueryTimingRelationshipMismatchRows +=
                queryTimingAudit.relationshipValid ? 0 : 1;
            metrics.presentedPostPresentQueryTimingValidRows +=
                expectedPostPresentQueryTiming &&
                    queryTimingAudit.relationshipValid ? 1 : 0;
            if (expectedPostPresentQueryTiming &&
                    submissionIdQueryEndUs >=
                        submissionIdQueryStartUs) {
                metrics.observedSubmissionIdQueryCall.add(
                    submissionIdQueryEndUs -
                        submissionIdQueryStartUs);
            }
            if (expectedPostPresentQueryTiming &&
                    frameStatsQueryEndUs >=
                        frameStatsQueryStartUs) {
                metrics.observedFrameStatsQueryCall.add(
                    frameStatsQueryEndUs -
                        frameStatsQueryStartUs);
            }
            if (expectedPostPresentQueryTiming &&
                    recordedPresentEndUs >= nativePresentEndUs) {
                metrics.observedPostPresentObservationTail.add(
                    recordedPresentEndUs -
                        nativePresentEndUs);
            }
        }
        if (traceSchema >= 5) {
            const uint64_t terminalFloorUs = std::max({
                recordedDecisionEndUs,
                recordedStaleCheckUs,
                recordedRenderWaitFinalUs,
                recordedPreparationEndUs,
                recordedTargetWaitFinalUs,
                recordedCorrectionWaitEndUs,
                recordedPresentEndUs,
            });
            metrics.terminalTimeOrderViolations +=
                recordedTerminalTimeUs < terminalFloorUs ? 1 : 0;
        }
        timelineDetails.recordedNativePresentTimingValid =
            nativePresentTimingValid;
        timelineDetails.recordedNativePresentStartUs =
            nativePresentTimingValid ? nativePresentStartUs : 0;
        timelineDetails.recordedNativePresentBoundaryDeltaUs =
            nativePresentTimingValid ?
                signedDifference(
                    timelineDetails.recordedNativePresentStartUs,
                    recordedSubmissionUs) : 0;
        if (presented) {
            if (haveRecordedSubmission &&
                    recordedSubmissionUs < priorRecordedSubmissionUs) {
                ++metrics.submissionTimestampRegressions;
            }
            haveRecordedSubmission = true;
            priorRecordedSubmissionUs = recordedSubmissionUs;
            ++metrics.presentedFrames;
            metrics.presentedPresentOperationIntegrityRows +=
                presentOperationOrderValid &&
                    presentOperationDurationValid ? 1 : 0;
            metrics.presentedSubmissionIdValidRows +=
                submissionIdValid ? 1 : 0;
            metrics.presentedNativePresentTimingValidRows +=
                nativePresentTimingValid ? 1 : 0;
            metrics.presentedGpuReadyTimingValidRows +=
                gpuReadyTimingValid ? 1 : 0;
            metrics.presentedGpuReadyBoundsValidRows +=
                gpuReadyBoundsValid ? 1 : 0;
            metrics.presentedRawPrePresentAnchorValidRows +=
                rawPrePresentSyncSampleValid ? 1 : 0;
            if (nativePresentTimingValid) {
                const int64_t boundaryDeltaUs =
                    timelineDetails.recordedNativePresentBoundaryDeltaUs;
                ++metrics.nativePresentBoundaryComparisons;
                metrics.nativePresentBoundaryMismatches +=
                    boundaryDeltaUs != 0 ? 1 : 0;
                metrics.observedNativePresentBoundaryDelta.add(
                    boundaryDeltaUs);
            }
        }

        uint64_t simulatedSubmissionUs = 0;
        if (presented) {
            const uint64_t recordedSubmissionFloorUs = std::max({
                recordedPreparationEndUs,
                recordedTargetUs,
                exactTargetWaitLifecycle ?
                    targetWaitEvidence.finalNowUs : 0,
                referenceController->earliestSubmissionUs(),
            });
            uint64_t submissionResidualUs = 0;
            if (recordedSubmissionUs >= recordedSubmissionFloorUs) {
                submissionResidualUs =
                    recordedSubmissionUs - recordedSubmissionFloorUs;
            }
            else {
                ++metrics.invalidExecutionResiduals;
            }
            const uint64_t simulatedSubmissionFloorUs = std::max({
                simulatedPreparationEndUs,
                simulatedDecision.targetUs,
                exactTargetWaitLifecycle ?
                    targetWakeInjection.simulatedFinalUs : 0,
                simulatedController->earliestSubmissionUs(),
            });
            const uint64_t nominalSimulatedSubmissionUs = saturatingAdd(
                simulatedSubmissionFloorUs,
                saturatingAdd(
                    submissionResidualUs,
                    exactTargetWaitLifecycle ?
                        injectedSubmissionDelayUs :
                        saturatingAdd(
                            injectedTargetWakeDelayUs,
                            injectedSubmissionDelayUs)));
            const VrrRasterProbeOverheadRemovalResult
                rasterProbeOverheadRemoval =
                    evaluateVrrRasterProbeOverheadRemoval(
                        scenario.execution.
                            removePrePresentRasterProbeOverhead != 0,
                        nativeRasterBeforeQueryResultDeclared,
                        nativeRasterBeforeQueryResult,
                        nativeRasterBeforeQueryStartUs,
                        nativeRasterBeforeQueryEndUs,
                        nativePresentTimingValid,
                        nativePresentStartUs,
                        nominalSimulatedSubmissionUs,
                        simulatedSubmissionFloorUs);
            timelineDetails.rasterProbeOverheadRemovalRequested =
                rasterProbeOverheadRemoval.requested;
            timelineDetails.rasterProbeOverheadAvailable =
                rasterProbeOverheadRemoval.evidenceAvailable;
            timelineDetails.measuredRasterProbeDurationUs =
                rasterProbeOverheadRemoval.measuredProbeDurationUs;
            timelineDetails.removedRasterProbeOverheadUs =
                rasterProbeOverheadRemoval.appliedRemovalUs;
            timelineDetails.rasterProbeOverheadRemovalClamped =
                rasterProbeOverheadRemoval.clampedBySubmissionFloor;
            metrics.rasterProbeOverheadRemovalRequestedFrames +=
                rasterProbeOverheadRemoval.requested ? 1 : 0;
            metrics.rasterProbeOverheadRemovalAvailableFrames +=
                rasterProbeOverheadRemoval.requested &&
                    rasterProbeOverheadRemoval.evidenceAvailable ? 1 : 0;
            metrics.rasterProbeOverheadRemovalMissingFrames +=
                rasterProbeOverheadRemoval.requested &&
                    !rasterProbeOverheadRemoval.evidenceAvailable ? 1 : 0;
            if (rasterProbeOverheadRemoval.requested &&
                    rasterProbeOverheadRemoval.evidenceAvailable) {
                metrics.measuredRasterProbeDurationUs = saturatingAdd(
                    metrics.measuredRasterProbeDurationUs,
                    rasterProbeOverheadRemoval.measuredProbeDurationUs);
            }
            metrics.removedRasterProbeOverheadUs = saturatingAdd(
                metrics.removedRasterProbeOverheadUs,
                rasterProbeOverheadRemoval.appliedRemovalUs);
            metrics.rasterProbeOverheadRemovalClampedFrames +=
                rasterProbeOverheadRemoval.clampedBySubmissionFloor ? 1 : 0;
            const VrrSubmissionAdvanceResult submissionAdvance =
                applyVrrSubmissionAdvance(
                    rasterProbeOverheadRemoval.submissionUs,
                    simulatedPreparationEndUs,
                    injectedSubmissionAdvanceUs);
            simulatedSubmissionUs = submissionAdvance.submissionUs;
            const uint64_t appliedSubmissionAdvanceUs =
                submissionAdvance.appliedAdvanceUs;
            timelineDetails.injectedSubmissionAdvanceAppliedUs =
                appliedSubmissionAdvanceUs;
            metrics.injectedSubmissionAdvanceAppliedUs = saturatingAdd(
                metrics.injectedSubmissionAdvanceAppliedUs,
                appliedSubmissionAdvanceUs);
            metrics.injectedSubmissionAdvanceFrames +=
                appliedSubmissionAdvanceUs != 0 ? 1 : 0;
            metrics.injectedSubmissionAdvanceClampedFrames +=
                submissionAdvance.clampedByReadiness ? 1 : 0;
        }

        const bool hadPriorSimulatedSubmission = haveSimulatedSubmission;
        if (readinessOutcome) {
            metrics.simulatedReadiness.record(std::max(simulatedSubmissionUs, simulatedPreparationEndUs),
                positiveDifference(simulatedPreparationEndUs, simulatedDecision.originalTargetUs),
                disposition != "presented");
        }
        const QByteArray simulatedTear = simulatedTearClassification(
            presented, simulatedDecision.latchedPresentation,
            simulatedCanLatch, hadPriorSimulatedSubmission,
            simulatedSubmissionUs, priorSimulatedSubmissionUs,
            periodForRate(simulatedConfig.displayRefreshHz));
        ++metrics.simulatedTearClassifications[simulatedTear];
        metrics.exactTearClassifications +=
            simulatedTear == recordedTear ? 1 : 0;
        if (timelineDetails.injectedSubmissionAdvanceAppliedUs != 0 &&
                simulatedTear == "adaptive_interval_violation") {
            ++metrics.injectedAdvanceIntervalViolations;
        }

        VrrRasterPhaseResult observedRaster;
        VrrRasterPhaseResult simulatedRaster;
        if (presented) {
            if (!timelineDetails.recordedLatched &&
                    rawPrePresentSyncSampleValid) {
                ++metrics.adaptiveRawPrePresentAnchorValidRows;
            }
            const uint64_t recordedPresentBoundaryUs =
                nativePresentTimingValid ?
                    optionalUnsignedField(fields,
                        columns.nativePresentStartUs) :
                    recordedSubmissionUs;
            VrrReplayDisplayParameters simulatedRasterParameters =
                rasterDisplayParameters;
            simulatedRasterParameters.presentTransportUs =
                simulatedPresentTransportUs;
            const uint64_t recordedRasterTransitionUs =
                saturatingAdd(
                    recordedPresentBoundaryUs,
                    rasterDisplayParameters.presentTransportUs);
            const uint64_t simulatedRasterTransitionUs =
                saturatingAdd(
                    simulatedSubmissionUs,
                    simulatedRasterParameters.presentTransportUs);
            const auto selectRasterSyncAnchor =
                [&rasterSyncAnchors](uint64_t transitionUs) {
                    for (auto it = rasterSyncAnchors.crbegin();
                            it != rasterSyncAnchors.crend(); ++it) {
                        if (it->timeUs <= transitionUs) {
                            return it->timeUs;
                        }
                    }
                    return uint64_t(0);
                };
            const uint64_t recordedRasterAnchorUs =
                selectRasterSyncAnchor(recordedRasterTransitionUs);
            const uint64_t simulatedRasterAnchorUs =
                selectRasterSyncAnchor(simulatedRasterTransitionUs);
            if (!timelineDetails.recordedLatched &&
                    recordedRasterAnchorUs != 0) {
                ++metrics.adaptivePrePresentAnchorValidRows;
            }
            if (!(simulatedDecision.latchedPresentation &&
                    simulatedCanLatch) &&
                    simulatedRasterAnchorUs != 0) {
                ++metrics.simulatedAdaptivePrePresentAnchorValidRows;
            }
            metrics.observedRasterAnchorFallbacks +=
                !timelineDetails.recordedLatched &&
                recordedRasterAnchorUs != 0 &&
                recordedRasterAnchorUs != rowPrePresentSyncSampleUs ? 1 : 0;
            metrics.simulatedRasterAnchorFallbacks +=
                !(simulatedDecision.latchedPresentation &&
                    simulatedCanLatch) &&
                simulatedRasterAnchorUs != 0 &&
                simulatedRasterAnchorUs != rowPrePresentSyncSampleUs ? 1 : 0;
            observedRaster = evaluateVrrRasterPhase(
                true, timelineDetails.recordedLatched,
                recordedPresentBoundaryUs, recordedRasterAnchorUs,
                unsignedField(fields, columns.displayPeriodUs),
                timelineDetails.recordedSourcePeriodUs,
                rasterDisplayParameters);
            simulatedRaster = evaluateVrrRasterPhase(
                true, simulatedDecision.latchedPresentation &&
                    simulatedCanLatch,
                simulatedSubmissionUs, simulatedRasterAnchorUs,
                periodForRate(simulatedConfig.displayRefreshHz),
                simulatedDecision.sourcePeriodUs,
                simulatedRasterParameters);
            addRasterEnvelope(metrics.observedRasterEnvelope, observedRaster);
            addRasterEnvelope(metrics.simulatedRasterEnvelope, simulatedRaster);
            timelineDetails.recordedRaster = observedRaster;
            timelineDetails.simulatedRaster = simulatedRaster;
            addRasterEnvelopeToBands(metrics.observedRateBands,
                timelineDetails.recordedSourceRateHz, observedRaster.envelope);
            addRasterEnvelopeToBands(metrics.simulatedRateBands,
                timelineDetails.simulatedSourceRateHz,
                simulatedRaster.envelope);
            const bool simulatedLatched =
                simulatedDecision.latchedPresentation &&
                simulatedCanLatch;
            if (scenario.mode != "fixed") {
                counterfactualFreeRunningTracker.reset();
                haveCounterfactualTransitionOrigin = false;
            }
            else if (simulatedLatched) {
                counterfactualFreeRunningTracker.reset();
                haveCounterfactualTransitionOrigin = false;
                ++metrics.counterfactualFreeRunningLatchedResets;
            }
            else {
                ++metrics.counterfactualFreeRunningAdaptiveRows;
                timelineDetails.simulatedFreeRunningRefreshEligible = true;
                const uint64_t counterfactualPeriodPs =
                    scenario.display.scanoutPeriodPs != 0 ?
                        scenario.display.scanoutPeriodPs :
                        nativeDisplaySignalPeriodValid ?
                            nativeDisplaySignalPeriodPs :
                            saturatingMultiply(
                                simulatedRaster.resolvedScanoutPeriodUs,
                                1000000ULL);
                if (!haveCounterfactualTransitionOrigin) {
                    haveCounterfactualTransitionOrigin = true;
                    counterfactualTransitionOriginUs =
                        simulatedRaster.modeledTransitionUs;
                }
                const bool absoluteTimeRegression =
                    simulatedRaster.modeledTransitionUs <
                        counterfactualTransitionOriginUs;
                const uint64_t transitionDeltaUs =
                    absoluteTimeRegression ? 0 :
                        simulatedRaster.modeledTransitionUs -
                            counterfactualTransitionOriginUs;
                const uint64_t transitionPs = saturatingMultiply(
                    transitionDeltaUs, 1000000ULL);
                const bool anchorDeltaValid =
                    simulatedRaster.modeledTransitionUs >=
                        simulatedRaster.anchorTimeUs;
                const uint64_t anchorDeltaPs = saturatingMultiply(
                    anchorDeltaValid ?
                        simulatedRaster.modeledTransitionUs -
                            simulatedRaster.anchorTimeUs : 0,
                    1000000ULL);
                const uint64_t phaseUncertaintyPs = saturatingMultiply(
                    scenario.display.phaseUncertaintyUs, 1000000ULL);
                const bool transitionConversionValid =
                    !absoluteTimeRegression &&
                    transitionPs != std::numeric_limits<uint64_t>::max() &&
                    phaseUncertaintyPs !=
                        std::numeric_limits<uint64_t>::max();
                const bool anchorDeltaConversionValid =
                    anchorDeltaValid &&
                    anchorDeltaPs !=
                        std::numeric_limits<uint64_t>::max();
                const bool phaseReferenceValid =
                    simulatedRaster.freeRunning !=
                        VrrRasterPhaseState::Unclassified &&
                    counterfactualPeriodPs != 0 &&
                    transitionConversionValid &&
                    anchorDeltaConversionValid;
                const uint64_t phaseReferencePs =
                    phaseReferenceValid ?
                        anchorDeltaPs % counterfactualPeriodPs : 0;
                const VrrFreeRunningRefreshResult refreshResult =
                    counterfactualFreeRunningTracker.observe(
                        transitionPs,
                        transitionConversionValid ?
                            counterfactualPeriodPs : 0,
                        phaseReferenceValid,
                        phaseReferencePs,
                        phaseUncertaintyPs);
                if (!transitionConversionValid) {
                    haveCounterfactualTransitionOrigin = false;
                }
                timelineDetails.simulatedFreeRunningRefreshBaseline =
                    refreshResult.baselineEstablished;
                timelineDetails.simulatedFreeRunningRefreshComparison =
                    refreshResult.compared;
                timelineDetails.simulatedFreeRunningPropagatedPhasePs =
                    refreshResult.propagatedPhase;
                timelineDetails.
                    simulatedFreeRunningPhaseReferenceCompared =
                        refreshResult.phaseReferenceCompared;
                timelineDetails.
                    simulatedFreeRunningPhaseReferenceDifferencePs =
                        refreshResult.phaseReferenceDifference;
                timelineDetails.simulatedFreeRunningRefreshDeltaLower =
                    refreshResult.refreshDeltaLower;
                timelineDetails.simulatedFreeRunningRefreshDelta =
                    refreshResult.refreshDelta;
                timelineDetails.simulatedFreeRunningRefreshDeltaUpper =
                    refreshResult.refreshDeltaUpper;
                timelineDetails.
                    simulatedFreeRunningScanoutAnomalyLower =
                        refreshResult.scanoutAnomalyLower;
                timelineDetails.simulatedFreeRunningScanoutAnomaly =
                    refreshResult.scanoutAnomaly;
                timelineDetails.
                    simulatedFreeRunningScanoutAnomalyUpper =
                        refreshResult.scanoutAnomalyUpper;
                timelineDetails.
                    simulatedFreeRunningRepeatedRefreshLower =
                        refreshResult.repeatedRefreshLower;
                timelineDetails.simulatedFreeRunningRepeatedRefresh =
                    refreshResult.repeatedRefresh;
                timelineDetails.
                    simulatedFreeRunningRepeatedRefreshUpper =
                        refreshResult.repeatedRefreshUpper;

                metrics.counterfactualFreeRunningBaselines +=
                    refreshResult.baselineEstablished ? 1 : 0;
                metrics.counterfactualFreeRunningComparisons +=
                    refreshResult.compared ? 1 : 0;
                metrics.counterfactualFreeRunningUnseededRows +=
                    !refreshResult.baselineEstablished &&
                    !refreshResult.compared ? 1 : 0;
                metrics.counterfactualFreeRunningTimeRegressions +=
                    refreshResult.timeRegression ||
                        absoluteTimeRegression ? 1 : 0;
                metrics.counterfactualFreeRunningPeriodChanges +=
                    refreshResult.periodChanged ? 1 : 0;
                metrics.counterfactualFreeRunningConversionFailures +=
                    transitionConversionValid ? 0 : 1;
                metrics.counterfactualFreeRunningScanoutAnomalyLower =
                    saturatingAdd(
                        metrics.
                            counterfactualFreeRunningScanoutAnomalyLower,
                        refreshResult.scanoutAnomalyLower);
                metrics.counterfactualFreeRunningScanoutAnomalies =
                    saturatingAdd(
                        metrics.counterfactualFreeRunningScanoutAnomalies,
                        refreshResult.scanoutAnomaly);
                metrics.counterfactualFreeRunningScanoutAnomalyUpper =
                    saturatingAdd(
                        metrics.
                            counterfactualFreeRunningScanoutAnomalyUpper,
                        refreshResult.scanoutAnomalyUpper);
                metrics.counterfactualFreeRunningRepeatedRefreshLower =
                    saturatingAdd(
                        metrics.
                            counterfactualFreeRunningRepeatedRefreshLower,
                        refreshResult.repeatedRefreshLower);
                metrics.counterfactualFreeRunningRepeatedRefreshes =
                    saturatingAdd(
                        metrics.
                            counterfactualFreeRunningRepeatedRefreshes,
                        refreshResult.repeatedRefresh);
                metrics.counterfactualFreeRunningRepeatedRefreshUpper =
                    saturatingAdd(
                        metrics.
                            counterfactualFreeRunningRepeatedRefreshUpper,
                        refreshResult.repeatedRefreshUpper);
                if (refreshResult.phaseReferenceCompared) {
                    metrics.
                        counterfactualFreeRunningPhaseReferenceDifferencePs.add(
                            refreshResult.phaseReferenceDifference);
                }
                if (refreshResult.compared) {
                    addScanoutOutcome(
                        metrics.simulatedRateBands,
                        timelineDetails.simulatedSourceRateHz,
                        refreshResult.scanoutAnomaly,
                        refreshResult.repeatedRefresh);
                }
            }
            if (timelineDetails.injectedSubmissionAdvanceAppliedUs != 0) {
                switch (simulatedRaster.envelope) {
                case VrrRasterEnvelopeClass::CertainActive:
                    ++metrics.injectedAdvanceRasterCertainActive;
                    break;
                case VrrRasterEnvelopeClass::PossibleActive:
                    ++metrics.injectedAdvanceRasterPossibleActive;
                    break;
                case VrrRasterEnvelopeClass::InactiveInBothModels:
                    ++metrics.injectedAdvanceRasterInactive;
                    break;
                case VrrRasterEnvelopeClass::LatchedSuppressed:
                    ++metrics.injectedAdvanceRasterLatchedSuppressed;
                    break;
                case VrrRasterEnvelopeClass::Unclassified:
                    ++metrics.injectedAdvanceRasterUnclassified;
                    break;
                }
            }
            if (timelineDetails.injectedDisplayTransitionDelayUs != 0) {
                switch (simulatedRaster.envelope) {
                case VrrRasterEnvelopeClass::CertainActive:
                    ++metrics.
                        injectedDisplayTransitionRasterCertainActive;
                    break;
                case VrrRasterEnvelopeClass::PossibleActive:
                    ++metrics.
                        injectedDisplayTransitionRasterPossibleActive;
                    break;
                case VrrRasterEnvelopeClass::InactiveInBothModels:
                    ++metrics.injectedDisplayTransitionRasterInactive;
                    break;
                case VrrRasterEnvelopeClass::LatchedSuppressed:
                    ++metrics.
                        injectedDisplayTransitionRasterLatchedSuppressed;
                    break;
                case VrrRasterEnvelopeClass::Unclassified:
                    ++metrics.
                        injectedDisplayTransitionRasterUnclassified;
                    break;
                }
            }
            metrics.exactRasterEnvelopeClassifications +=
                observedRaster.envelope == simulatedRaster.envelope ? 1 : 0;

            const bool recordedDiscontinuity = optionalUnsignedField(
                fields, columns.rebased) != 0 || optionalUnsignedField(
                    fields, columns.phaseDiscontinuity) != 0;
            timelineDetails.recordedCadence = observedCadenceTracker.observe(
                unsignedField(fields, columns.sourceTimeUs),
                recordedSubmissionUs, recordedDiscontinuity);
            timelineDetails.simulatedCadence = simulatedCadenceTracker.observe(
                simulatedDecision.sourceTimeUs, simulatedSubmissionUs,
                simulatedDecision.rebased ||
                    simulatedDecision.phaseDiscontinuity);
            if (hadPriorSimulatedSubmission) {
                const uint64_t simulatedSpacingUs =
                    simulatedSubmissionUs >= priorSimulatedSubmissionUs ?
                        simulatedSubmissionUs - priorSimulatedSubmissionUs : 0;
                timelineDetails.simulatedSpacingMarginUs = signedDifference(
                    simulatedSpacingUs,
                    periodForRate(simulatedConfig.displayRefreshHz));
            }

            const int64_t pairedDelta = signedDifference(
                simulatedSubmissionUs, recordedSubmissionUs);
            metrics.pairedSubmissionDelta.add(pairedDelta);
            metrics.pairedAbsoluteSubmissionDelta.add(absoluteValue(
                pairedDelta));

            // Sender-spacing cadence for the recorded run, the candidate and
            // a stock-style present-on-render emulation.
            if (unsignedField(fields, columns.rtpValid) != 0) {
                if (metrics.senderClockValid) {
                    metrics.senderUnwrappedTicks += static_cast<uint32_t>(
                        rtpTimestamp - metrics.senderPriorRtpTimestamp);
                }
                metrics.senderPriorRtpTimestamp = rtpTimestamp;
                metrics.senderClockValid = true;
                const uint64_t senderUs =
                    metrics.senderUnwrappedTicks * 1000ULL / 90ULL;
                const uint64_t simulatedDisplayPeriodUs = periodForRate(
                    simulatedConfig.displayRefreshHz);
                const uint64_t recordedRenderLeadUs = unsignedField(
                    fields, columns.renderLeadUs);
                const bool recordedLeadJump =
                    metrics.observedPriorRenderLeadValid &&
                    absoluteValue(signedDifference(
                        recordedRenderLeadUs,
                        metrics.observedPriorRenderLeadUs)) >
                        SenderCadenceTracker::kRenderLeadJumpUs;
                metrics.observedPriorRenderLeadUs = recordedRenderLeadUs;
                metrics.observedPriorRenderLeadValid = true;
                const bool simulatedLeadJump =
                    metrics.simulatedPriorRenderLeadValid &&
                    absoluteValue(signedDifference(
                        simulatedDecision.renderLeadUs,
                        metrics.simulatedPriorRenderLeadUs)) >
                        SenderCadenceTracker::kRenderLeadJumpUs;
                metrics.simulatedPriorRenderLeadUs =
                    simulatedDecision.renderLeadUs;
                metrics.simulatedPriorRenderLeadValid = true;
                const bool simulatedLateArrival =
                    scenario.controller.timestampPlayoutEnabled != 0 &&
                    simulatedDecision.readyOffsetUs > static_cast<int64_t>(
                        simulatedDecision.playoutDelayUs);
                metrics.simulatedPlayoutDelayUs.add(
                    simulatedDecision.playoutDelayUs);
                bool recordedLateArrival = false;
                if (columns.playoutDelayUs >= 0) {
                    const uint64_t recordedPlayoutDelayUs = unsignedField(
                        fields, columns.playoutDelayUs);
                    metrics.observedPlayoutDelayUs.add(
                        recordedPlayoutDelayUs);
                    recordedLateArrival =
                        capturedParameters.timestampPlayoutEnabled != 0 &&
                        signedField(fields, columns.readyOffsetUs) >
                            static_cast<qint64>(recordedPlayoutDelayUs);
                }
                metrics.observedSenderCadence.observe(
                    senderUs, decodeCompleteUs, recordedSubmissionUs,
                    recordedLateArrival, recordedLeadJump,
                    simulatedDisplayPeriodUs);
                metrics.simulatedSenderCadence.observe(
                    senderUs, decodeCompleteUs, simulatedSubmissionUs,
                    simulatedLateArrival, simulatedLeadJump,
                    simulatedDisplayPeriodUs);
                metrics.stockSenderCadence.observe(
                    senderUs, decodeCompleteUs,
                    saturatingAdd(decodeCompleteUs,
                                  saturatingAdd(preparationUs,
                                                recordedPresentCallUs)),
                    false, false, simulatedDisplayPeriodUs);
            }
            addCadenceFrame(metrics.observedRateBands,
                timelineDetails.recordedSourceRateHz, decoderOutputUs,
                recordedSubmissionUs, timelineDetails.recordedCadence,
                recordedTear == "adaptive_interval_violation");
            addCadenceFrame(metrics.simulatedRateBands,
                timelineDetails.simulatedSourceRateHz, decoderOutputUs,
                simulatedSubmissionUs, timelineDetails.simulatedCadence,
                simulatedTear == "adaptive_interval_violation");
            metrics.observedJerkAnomalies.observe(recordedSubmissionUs,
                timelineDetails.recordedCadence.jerkValid &&
                    absoluteValue(timelineDetails.recordedCadence.jerkUs) >=
                        kJerkAnomalyThresholdUs);
            metrics.simulatedJerkAnomalies.observe(simulatedSubmissionUs,
                timelineDetails.simulatedCadence.jerkValid &&
                    absoluteValue(timelineDetails.simulatedCadence.jerkUs) >=
                        kJerkAnomalyThresholdUs);

            metrics.exactSimulatedSubmissions +=
                simulatedSubmissionUs == recordedSubmissionUs ? 1 : 0;
            metrics.simulatedSubmissionDrift.add(absoluteValue(
                signedDifference(simulatedSubmissionUs,
                                 recordedSubmissionUs)));
            metrics.observedAbsoluteSubmitError.add(absoluteValue(
                signedField(fields, columns.submitErrorUs)));
            metrics.simulatedAbsoluteSubmitError.add(absoluteValue(
                signedDifference(simulatedSubmissionUs,
                                 simulatedDecision.targetUs)));
            metrics.observedDecodeToSubmission.addElapsed(
                recordedSubmissionUs, decoderOutputUs);
            metrics.observedArrivalToSubmission.addElapsed(
                recordedSubmissionUs, pacerArrivalUs);
            metrics.observedDecisionToSubmission.addElapsed(
                recordedSubmissionUs, decisionUs);
            metrics.observedProjectedSourceToSubmission.addElapsed(
                recordedSubmissionUs,
                unsignedField(fields, columns.sourceTimeUs));
            metrics.observedSubmissionSpacing.add(unsignedField(
                fields, columns.submissionSpacingUs));
            metrics.simulatedDecodeToSubmission.addElapsed(
                simulatedSubmissionUs, decoderOutputUs);
            metrics.simulatedArrivalToSubmission.addElapsed(
                simulatedSubmissionUs, pacerArrivalUs);
            metrics.simulatedDecisionToSubmission.addElapsed(
                simulatedSubmissionUs, simulatedDecisionUs);
            metrics.simulatedProjectedSourceToSubmission.addElapsed(
                simulatedSubmissionUs, simulatedDecision.sourceTimeUs);

            if (simulatedDecision.latchedPresentation &&
                    simulatedCanLatch) {
                ++metrics.simulatedLatchedFrames;
            }
            if (hadPriorSimulatedSubmission) {
                const uint64_t spacingUs =
                    simulatedSubmissionUs >= priorSimulatedSubmissionUs ?
                        simulatedSubmissionUs - priorSimulatedSubmissionUs : 0;
                metrics.simulatedSubmissionSpacing.add(spacingUs);
                metrics.simulatedCadenceError.add(absoluteValue(
                    signedDifference(spacingUs,
                                     simulatedDecision.sourcePeriodUs)));
                if (simulatedTear == "adaptive_interval_violation") {
                    ++metrics.simulatedTearRisks;
                }
            }
            haveSimulatedSubmission = true;
            priorSimulatedSubmissionUs = simulatedSubmissionUs;

            if (submissionIdValid) {
                const uint64_t submissionId = optionalUnsignedField(
                    fields, columns.submissionId);
                for (SubmissionBand& submission : pendingSubmissionBands) {
                    if (submission.id != submissionId) {
                        continue;
                    }
                    submission.simulatedSubmissionValid = true;
                    submission.simulatedSubmissionUs = simulatedSubmissionUs;
                    submission.simulatedSourcePeriodUs =
                        simulatedDecision.sourcePeriodUs;
                    submission.simulatedLatched =
                        simulatedDecision.latchedPresentation &&
                        simulatedCanLatch;
                    submission.simulatedPresentTransportUs =
                        simulatedPresentTransportUs;
                    submission.simulatedDisplayTransitionDelayUs =
                        injectedDisplayTransitionDelayUs;
                    submission.simulatedSubmissionAdvanceAppliedUs =
                        timelineDetails.
                            injectedSubmissionAdvanceAppliedUs;
                    submission.recordedRasterValid = true;
                    submission.recordedRaster = observedRaster.envelope;
                    submission.simulatedRasterValid = true;
                    submission.simulatedRaster = simulatedRaster.envelope;
                    if (submission.recordedExactClassValid &&
                            !submission.recordedValidationCounted) {
                        metrics.observedRasterValidationContradictions +=
                            addRasterValidation(
                                metrics.observedRasterValidation,
                                submission.recordedRaster,
                                submission.recordedExactClass) ? 1 : 0;
                        submission.recordedValidationCounted = true;
                    }
                    if (submission.simulatedExactClassValid &&
                            !submission.simulatedValidationCounted) {
                        metrics.simulatedRasterValidationContradictions +=
                            addRasterValidation(
                                metrics.simulatedRasterValidation,
                                submission.simulatedRaster,
                                submission.simulatedExactClass) ? 1 : 0;
                        submission.simulatedValidationCounted = true;
                    }
                    const bool exactRefreshAlreadyCompared =
                        submission.simulatedRefreshCompared;
                    compareSimulatedToExactRefresh(submission);
                    if (!exactRefreshAlreadyCompared &&
                            submission.simulatedRefreshCompared) {
                        timelineDetails.simulatedExactRefreshPhaseUs =
                            rowSimulatedExactRefreshPhaseUs;
                        timelineDetails.
                            simulatedExactActiveScanoutPhaseUs =
                            rowSimulatedExactActiveScanoutPhaseUs;
                        timelineDetails.simulatedExactRefreshClass =
                            rowSimulatedExactRefreshClass;
                    }
                    break;
                }
            }
        }

        if (staleBeforeRenderLifecycle || staleAfterRenderLifecycle) {
            // Both stale paths write their trace row before mutating the
            // controller. Compare the captured learned state at that same
            // point, then reproduce the mutation that affects the next row.
            addReferenceControllerDiagnostics(
                metrics, referenceController->diagnostics(), fields, columns);
            if (staleAfterRenderLifecycle) {
                if (referenceController->latencyFixActive() ||
                    referenceController->parameters().playoutMetronomeEnabled)
                    referenceController->noteSubmission(false, false, 0);
                else
                    referenceController->rebase();
                if (simulatedController->latencyFixActive() ||
                    simulatedController->parameters().playoutMetronomeEnabled)
                    simulatedController->noteSubmission(false, false, 0);
                else
                    simulatedController->rebase();
            }
            else {
                referenceController->noteSubmission(
                    presented, cancelled, recordedSubmissionUs);
                simulatedController->noteSubmission(
                    presented, cancelled, simulatedSubmissionUs);
            }
        }
        else {
            referenceController->noteSubmission(presented, cancelled,
                                                 recordedSubmissionUs);
            simulatedController->noteSubmission(presented, cancelled,
                                                 simulatedSubmissionUs);
            addReferenceControllerDiagnostics(
                metrics, referenceController->diagnostics(), fields, columns);
        }

        if (optionalUnsignedField(fields, columns.presentEndUs) && !staleBeforeRenderLifecycle && !staleAfterRenderLifecycle) {
            const auto field = [&](const char* name) { return optionalUnsignedField(fields, traceHeader.indexOf(name)); };
            Vrr13::PresentationObservation observation;
            observation.smoothness = referenceController->smoothnessSample(referenceDecision);
            observation.submitted = presented && !cancelled;
            observation.idValid = field("submission_id_valid") != 0;
            observation.id = field("submission_id");
            observation.submission = recordedSubmissionUs;
            observation.ready = field("prepare_end_us");
            observation.deadline = referenceDecision.originalScanoutUs;
            observation.latched = referenceDecision.latchedPresentation;
            observation.dxgi = field("native_backend") == kNativeBackendDxgi;
            const bool fixedPresentationMode = traceHeader.contains("presentation_uncertainty_us") &&
                (field("native_backend") == kNativeBackendVulkan ||
                 field("native_backend") == kNativeBackendComposition);
            if (fixedPresentationMode) observation.latched = false;
            const uint64_t frequency = field("latch_raw_sync_qpc_frequency_hz");
            observation.sampleValid = field("latch_valid") &&
                (!observation.dxgi || (field("latch_qpc_correlation_valid") && frequency));
            observation.sampleId = field("latch_submission_id");
            observation.sampleTime = field("latch_time_us");
            observation.timeKind = static_cast<Vrr13::PresentationTimeKind>(field("latch_time_kind"));
            observation.observed = field("present_end_us");
            observation.presentRefresh = field("latch_present_refresh_seq");
            observation.syncRefresh = field("latch_sync_refresh_seq");
            observation.uncertainty = frequency ? field("latch_qpc_correlation_span_ticks") * 1000000 / frequency :
                field("presentation_uncertainty_us");
            referenceController->notePresentation(observation);
            // Recorded presentation latency is an external service sample, not
            // proof of the candidate's actual scanout. Move it with its submission.
            observation.timelineShift = signedDifference(simulatedSubmissionUs, recordedSubmissionUs);
            observation.latched = fixedPresentationMode ? false : simulatedDecision.latchedPresentation;
            observation.deadline = observation.timelineShift >= 0 ?
                simulatedDecision.originalScanoutUs - std::min(simulatedDecision.originalScanoutUs, uint64_t(observation.timelineShift)) :
                simulatedDecision.originalScanoutUs + uint64_t(-observation.timelineShift);
            observation.smoothness = simulatedController->smoothnessSample(simulatedDecision);
            simulatedController->notePresentation(observation);
        }

        if (timelineFile.isOpen() &&
                !writeTimelineRow(timelineFile, arrivalSequence,
                    frameNumber,
                    disposition, decodeCompleteUs, pacerArrivalUs,
                    recordedTargetUs, simulatedDecision.targetUs,
                    recordedSubmissionUs, simulatedSubmissionUs,
                    recordedTear, simulatedTear, timelineDetails)) {
            std::fprintf(stderr, "Unable to write timeline: %s\n",
                         qPrintable(timelineFile.errorString()));
            return 1;
        }
    }
    if (!error.isEmpty()) {
        std::fprintf(stderr, "Trace read failed: %s\n", qPrintable(error));
        return 1;
    }
    if (!metrics.arrivalSequences.empty()) {
        std::sort(metrics.arrivalSequences.begin(),
                  metrics.arrivalSequences.end());
        metrics.firstArrivalSequence = metrics.arrivalSequences.front();
        metrics.lastArrivalSequence = metrics.arrivalSequences.back();
        uint64_t priorSequence = metrics.firstArrivalSequence;
        for (size_t i = 1; i < metrics.arrivalSequences.size(); ++i) {
            const uint64_t sequence = metrics.arrivalSequences[i];
            if (sequence == priorSequence) {
                ++metrics.arrivalSequenceDuplicates;
            }
            else {
                if (sequence - priorSequence > 1) {
                    metrics.arrivalSequenceGaps = saturatingAdd(
                        metrics.arrivalSequenceGaps,
                        sequence - priorSequence - 1);
                }
                priorSequence = sequence;
            }
        }
    }
    if (timelineFile.isOpen()) {
        timelineFile.close();
        if (timelineFile.error() != QFileDevice::NoError) {
            std::fprintf(stderr, "Unable to finish timeline: %s\n",
                         qPrintable(timelineFile.errorString()));
            return 1;
        }
    }

    QJsonObject summary = summaryObject(
        metrics, timer.elapsed(), tracePath, QFileInfo(tracePath).size(),
        QString::fromLatin1(decodedTraceHash.result().toHex()),
        capturedConfig.displayRefreshHz, capturedConfig.streamRateHz,
        simulatedConfig.displayRefreshHz, simulatedConfig.streamRateHz,
        capturedConfig.allowAdditionalQueuedFrame,
        capturedConfig.allowTearing, simulatedCanLatch, capturedParameters.playoutResponsiveBuffer,
        scenario);
    bool comparisonCompatible = true;
    if (parser.isSet(compareOption)) {
        QFile baselineFile(parser.value(compareOption));
        if (!baselineFile.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "Unable to read comparison summary: %s\n",
                         qPrintable(baselineFile.errorString()));
            return 1;
        }
        QJsonParseError parseError;
        const QByteArray baselineData = baselineFile.readAll();
        if (baselineFile.error() != QFileDevice::NoError) {
            std::fprintf(stderr, "Unable to read comparison summary: %s\n",
                         qPrintable(baselineFile.errorString()));
            return 1;
        }
        const QJsonDocument baselineDocument = QJsonDocument::fromJson(
            baselineData, &parseError);
        if (!baselineDocument.isObject()) {
            std::fprintf(stderr, "Invalid comparison summary: %s\n",
                         qPrintable(parseError.errorString()));
            return 1;
        }
        const QJsonObject baseline = baselineDocument.object();
        QJsonObject deltas;
        QJsonArray incompatibilities;
        const QString currentTraceHash = summary.value("capture").toObject().
            value("normalized_decoded_csv_sha256").toString();
        const QString baselineTraceHash = baseline.value("capture").toObject().
            value("normalized_decoded_csv_sha256").toString();
        const QString currentModel = summary.value("model").toString();
        const QString baselineModel = baseline.value("model").toString();
        const QJsonObject currentSimulation =
            summary.value("simulation").toObject();
        const QJsonObject baselineSimulation =
            baseline.value("simulation").toObject();
        const QJsonObject currentDisplayModel = currentSimulation.value(
            "resolved_parameters").toObject().value("display").toObject();
        const QJsonObject baselineDisplayModel = baselineSimulation.value(
            "resolved_parameters").toObject().value("display").toObject();
        const bool traceCompatible =
            !currentTraceHash.isEmpty() &&
            currentTraceHash == baselineTraceHash;
        const bool modelCompatible =
            !currentModel.isEmpty() && currentModel == baselineModel;
        const bool displayModelCompatible =
            !currentDisplayModel.isEmpty() &&
            currentDisplayModel == baselineDisplayModel;
        const bool ratesCompatible =
            currentSimulation.value("display_hz") ==
                baselineSimulation.value("display_hz") &&
            currentSimulation.value("stream_fps") ==
                baselineSimulation.value("stream_fps");
        const bool replayModeCompatible =
            !currentSimulation.value("mode").toString().isEmpty() &&
            currentSimulation.value("mode") ==
                baselineSimulation.value("mode");
        if (!traceCompatible) {
            incompatibilities.append(
                "decoded trace content hash is absent or different");
        }
        if (!modelCompatible) {
            incompatibilities.append(
                "replay model version is absent or different");
        }
        if (!displayModelCompatible) {
            incompatibilities.append(
                "display calibration model is absent or different");
        }
        if (!ratesCompatible) {
            incompatibilities.append(
                "candidate display or stream rate is different");
        }
        if (!replayModeCompatible) {
            incompatibilities.append(
                "replay mode is absent or different");
        }
        comparisonCompatible = traceCompatible && modelCompatible &&
            displayModelCompatible && ratesCompatible &&
            replayModeCompatible;
        deltas["comparable"] = comparisonCompatible;
        deltas["trace_content_match"] = traceCompatible;
        deltas["model_version_match"] = modelCompatible;
        deltas["display_model_match"] = displayModelCompatible;
        deltas["display_and_stream_rates_match"] = ratesCompatible;
        deltas["replay_mode_match"] = replayModeCompatible;
        deltas["incompatibilities"] = incompatibilities;
        deltas["baseline_trace_sha256"] = baselineTraceHash;
        deltas["candidate_trace_sha256"] = currentTraceHash;
        deltas["baseline_model"] = baselineModel;
        deltas["candidate_model"] = currentModel;
        deltas["baseline_parameter_fingerprint"] = baseline.value(
            "simulation").toObject().value("parameter_fingerprint");
        deltas["candidate_parameter_fingerprint"] = summary.value(
            "simulation").toObject().value("parameter_fingerprint");
        const char* lowerIsBetter[] = {
            "replay_tear_risks",
            "replay_raster_exposure_lower_bound",
            "replay_raster_exposure_upper_bound",
            "replay_raster_unclassified",
            "replay_equality_anchored_active_exposures",
            "replay_equality_anchored_possible_exposures",
            "replay_equality_anchored_unclassified",
            "replay_free_running_scanout_anomaly_upper",
            "replay_free_running_repeated_refresh_upper",
            "replay_abs_submit_error_p95_us",
            "replay_cadence_error_p95_us",
            "replay_decode_to_submission_p95_us",
            "replay_arrival_to_submission_p95_us",
            "replay_decode_to_submission_stddev_us",
            "replay_40_116_cadence_residual_p95_us",
            "replay_40_116_cadence_residual_p99_us",
            "replay_40_116_jerk_p95_us",
            "replay_40_116_jerk_p99_us",
            "replay_60_100_cadence_residual_p95_us",
            "replay_60_100_cadence_residual_p99_us",
            "replay_60_100_jerk_p95_us",
            "replay_60_100_jerk_p99_us",
            "paired_abs_submission_delta_p99_us",
        };
        int improved = 0;
        int regressed = 0;
        int compared = 0;
        if (comparisonCompatible) {
            for (const char* metric : lowerIsBetter) {
                if (!summary.value(metric).isDouble() ||
                        !baseline.value(metric).isDouble()) {
                    continue;
                }
                const double delta = summary.value(metric).toDouble() -
                    baseline.value(metric).toDouble();
                deltas[QString::fromLatin1(metric) + "_delta"] = delta;
                improved += delta < -1e-9 ? 1 : 0;
                regressed += delta > 1e-9 ? 1 : 0;
                ++compared;
            }
        }
        deltas["compared_metrics"] = compared;
        deltas["skipped_metrics"] =
            static_cast<int>(std::size(lowerIsBetter)) - compared;
        deltas["improved_metrics"] = improved;
        deltas["regressed_metrics"] = regressed;
        deltas["verdict"] = !comparisonCompatible || compared == 0 ?
            "not_comparable" :
            regressed == 0 && improved != 0 ? "better" :
            improved == 0 && regressed != 0 ? "worse" :
            improved == 0 && regressed == 0 ? "unchanged" : "mixed";
        summary["comparison"] = deltas;
    }

    const bool assertionsPassed = evaluateAssertions(scenario, summary);
    const bool baselineExact = summary.value("fidelity").toObject().value(
        "baseline_exact").toBool();
    const QJsonObject diagnosticReadiness = summary.value(
        "diagnostic_readiness").toObject();
    const bool controllerReplayReady = diagnosticReadiness.value(
        "controller_replay_ready").toBool();
    const bool diagnosticCaptureReady = diagnosticReadiness.value(
        "diagnostic_capture_ready").toBool();
    const bool rasterSimulationReady = diagnosticReadiness.value(
        "raster_simulation_ready").toBool();
    const bool counterfactualRefreshTimelineReady =
        diagnosticReadiness.value(
            "counterfactual_refresh_timeline_available").toBool();
    const QByteArray output = QJsonDocument(summary).toJson(
        QJsonDocument::Indented);
    if (parser.isSet(outputOption)) {
        QFile outputFile(parser.value(outputOption));
        if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                outputFile.write(output) != output.size() ||
                !outputFile.flush()) {
            std::fprintf(stderr, "Unable to write replay summary: %s\n",
                         qPrintable(outputFile.errorString()));
            return 1;
        }
        outputFile.close();
        if (outputFile.error() != QFileDevice::NoError) {
            std::fprintf(stderr, "Unable to finish replay summary: %s\n",
                         qPrintable(outputFile.errorString()));
            return 1;
        }
    }
    else if (std::fwrite(
                 output.constData(), 1,
                 static_cast<size_t>(output.size()), stdout) !=
                 static_cast<size_t>(output.size()) ||
             std::fflush(stdout) != 0) {
        std::fprintf(stderr, "Unable to write replay summary to stdout\n");
        return 1;
    }

    if (!comparisonCompatible) {
        std::fprintf(
            stderr,
            "Comparison is not valid; inspect comparison.incompatibilities\n");
        return 5;
    }
    if (parser.isSet(controllerReadyOption) && !controllerReplayReady) {
        std::fprintf(
            stderr,
            "Controller replay readiness failed; inspect diagnostic_readiness.gates\n");
        return 6;
    }
    if (parser.isSet(diagnosticCaptureReadyOption) &&
            !diagnosticCaptureReady) {
        std::fprintf(
            stderr,
            "Diagnostic capture readiness failed; inspect diagnostic_readiness.gates\n");
        return 8;
    }
    if (parser.isSet(rasterReadyOption) && !rasterSimulationReady) {
        std::fprintf(
            stderr,
            "Raster simulation readiness failed; inspect diagnostic_readiness.gates\n");
        return 7;
    }
    if (parser.isSet(counterfactualRefreshReadyOption) &&
            !counterfactualRefreshTimelineReady) {
        std::fprintf(
            stderr,
            "Counterfactual refresh readiness failed; inspect diagnostic_readiness.gates\n");
        return 9;
    }
    if (parser.isSet(exactOption) && !baselineExact) {
        std::fprintf(stderr,
                     "Exact baseline validation failed; inspect the fidelity object\n");
        return 3;
    }
    return assertionsPassed ? 0 : 4;
}
