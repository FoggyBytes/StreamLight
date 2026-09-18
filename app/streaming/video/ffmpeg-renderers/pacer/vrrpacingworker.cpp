// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "vrr/profile.h"
#include "vrr/profilecodec.h"
#include "vrrpacingworker.h"

#include "vrr/vrrtargetwaiter.h"
#include "vrr/vrrtimingcontroller.h"
#include "vrr/vrrframedroppolicy.h"

#include <Limelight.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>

extern "C" {
#include <libavutil/frame.h>
}

#include <algorithm>
#include <limits>
#include <thread>
#include <utility>

namespace {

// Keep enough decoded successors to absorb the short gap-then-burst delivery
// pattern seen near the panel ceiling. Capacity remains bounded and evicts the
// oldest queued successor under sustained pressure, so it cannot accumulate
// an unbounded latency backlog.
constexpr size_t kMaximumQueuedFrames = VrrMaximumQueuedFrames;
// ~64 seconds of rows at 120 FPS. When the writer thread cannot keep up the
// pacing thread drops rows rather than ever waiting on diagnostics.
// Each compressed chunk covers only a few seconds, limiting crash loss while
// still turning repeated timestamps and controller state into very small,
// infrequent physical writes.
constexpr int kTraceChunkBytes = 256 * 1024;
// A decode sync shorter than this did not wait on the GPU; the frame keeps
// the decoder's completion time as its readiness.
constexpr uint64_t kDecodeSyncNoticeUs = 200;
// Always preserve at least an hour, including the maximum supported 480 FPS
// stream cadence. The physical cap takes effect only after that duration, so
// an unusually incompressible trace remains complete rather than silently
// trading away replay fidelity. Chunk compression keeps normal captures far
// below this limit.
constexpr uint64_t kMinimumTraceDurationUs = 60ULL * 60ULL * 1000000ULL;
constexpr uint64_t kMaximumTraceBytes = 512ULL * 1024ULL * 1024ULL;
constexpr char kTraceMagic[] = "MLVRR1\n";
#define VRR_TRACE_PARAMETER_HEADER(type, jsonName, memberName, defaultValue) \
    ",param_" #jsonName
constexpr char kTraceHeader[] =
    "trace_schema,arrival_sequence,frame,rtp_timestamp,rtp_valid,decode_complete_us,"
    "frame_receive_us,frame_reassembled_us,decode_submit_us,pacer_arrival_us,"
    "arrival_queue_depth_before,arrival_queue_depth_after,queue_accepted,dequeue_us,queue_discontinuity,decision_valid,decision_us,"
    "display_refresh_hz,stream_rate_hz,additional_queued_frame,display_period_us,can_latch_present,sender_interval_us,source_rate_hz,source_period_us,"
    "source_time_us,ready_offset_us,readiness_budget_us,timing_budget_us,render_lead_us,gpu_readiness_lead_us,"
    "render_wake_lead_us,target_wake_lead_us,guard_us,headroom_us,render_start_us,render_wait_final_us,render_wait_overshoot_us,"
    "render_scheduler_delay_us,render_scheduler_delay_valid,render_deadline_already_elapsed,"
    "render_wait_initial_us,render_wait_active_budget_us,render_wait_coarse_sleep_count,render_wait_coarse_requested_total_us,render_wait_coarse_requested_wake_us,render_wait_coarse_return_us,render_wait_coarse_clock_stalled,render_wait_active_entered,render_wait_active_start_us,render_wait_active_limit_us,render_wait_active_yield_count,render_wait_active_clock_stalled,render_wait_active_yield_limit_reached,"
    "prepare_start_us,prepare_end_us,prepare_us,target_us,target_wait_final_us,target_wait_overshoot_us,target_scheduler_delay_us,target_scheduler_delay_valid,target_deadline_already_elapsed,"
    "target_wait_initial_us,target_wait_active_budget_us,target_wait_coarse_sleep_count,target_wait_coarse_requested_total_us,target_wait_coarse_requested_wake_us,target_wait_coarse_return_us,target_wait_coarse_clock_stalled,target_wait_active_entered,target_wait_active_start_us,target_wait_active_limit_us,target_wait_active_yield_count,target_wait_active_clock_stalled,target_wait_active_yield_limit_reached,"
    "present_start_us,submission_boundary_us,presenter_submission_time_valid,presenter_submission_time_us,presenter_submission_time_used,present_end_us,present_call_us,submit_error_us,submission_spacing_us,"
    "spacing_margin_us,spacing_deficit_us,spacing_guard_feedback_us,spacing_corrected,had_prior_submission,tear_classification,tear_risk,completion_queue_depth,disposition,dropped,presented,cancelled,"
    "submission_id_valid,submission_id,latch_valid,latch_submission_id,latch_time_us,latch_time_kind,latch_present_refresh_seq,latch_sync_refresh_seq,latched_present,"
    "used_rtp_timestamp,cadence_eligible,source_rate_changed,phase_discontinuity,rebased,external_rebase_applied,external_rebase_flags,midframe_window_state_flags,deep_trace,"
    "native_present_timing_valid,native_present_start_us,native_present_end_us,native_present_call_us,"
    "present_count_before_valid,present_count_before,frame_stats_before_valid,frame_stats_before_present_count,frame_stats_before_time_us,frame_stats_before_present_refresh_seq,frame_stats_before_sync_refresh_seq,"
    "gpu_ready_attempted,gpu_ready_signal_result_valid,gpu_ready_signal_result,gpu_ready_set_event_result_valid,gpu_ready_set_event_result,gpu_ready_wait_result_valid,gpu_ready_wait_result,"
    "gpu_ready_timing_valid,gpu_ready_signal_start_us,gpu_ready_signal_end_us,gpu_ready_flush_start_us,gpu_ready_flush_end_us,gpu_ready_set_event_start_us,gpu_ready_set_event_end_us,gpu_ready_poll_start_us,gpu_ready_poll_end_us,gpu_ready_fence_value,gpu_ready_poll_completed_value,gpu_ready_completed_before_wait,gpu_ready_completion_lower_bound_us,gpu_ready_completion_upper_bound_us,gpu_ready_completion_uncertainty_us,gpu_ready_wait_start_us,gpu_ready_time_us,gpu_ready_wait_us,"
    "decision_end_us,controller_call_us,stale_check_us,stale_age_us,render_wait_entry_us,target_wait_entry_us,spacing_check_us,presentation_floor_us,spacing_recheck_us,spacing_corrected_floor_us,correction_wait_start_us,correction_wait_end_us,terminal_time_us,"
    "native_backend_valid,native_backend,native_present_result_valid,native_present_result,native_present_parameters_valid,native_present_sync_interval,native_present_flags,"
    "native_vrr_state_valid,native_tearing_supported,native_borderless_flip_model,native_same_gpu_output,native_render_adapter_luid_valid,native_render_adapter_luid,native_swap_chain_allows_tearing,"
    "native_tearing_feature_query_result_valid,native_tearing_feature_query_result,native_tearing_feature_allows_tearing,native_swap_chain_desc_query_result_valid,native_swap_chain_desc_query_result,native_swap_chain_flags,native_swap_chain_swap_effect,native_fullscreen_state_query_result_valid,native_fullscreen_state_query_result,native_fullscreen_exclusive,native_window_flags,"
    "native_present_ready_available,native_foreground_window,native_vrr_fallback_reason,native_desktop_monitor_count,"
    "native_vblank_virtualization_probe_complete,native_vblank_virtualization_call_available,native_vblank_virtualization_result_valid,native_vblank_virtualization_result,native_vblank_virtualization_disabled,"
    "native_display_config_query_result_valid,native_display_config_query_result,native_display_path_valid,native_display_path_flags,native_display_target_available,native_display_source_adapter_luid,native_display_source_id,native_display_target_adapter_luid,native_display_target_id,native_display_output_technology,native_display_rotation,native_display_scaling,native_display_path_refresh_numerator,native_display_path_refresh_denominator,"
    "native_display_signal_valid,native_display_signal_pixel_rate_hz,native_display_signal_hsync_numerator,native_display_signal_hsync_denominator,native_display_signal_vsync_numerator,native_display_signal_vsync_denominator,native_display_signal_active_width,native_display_signal_active_height,native_display_signal_total_width,native_display_signal_total_height,native_display_signal_additional_info_raw,native_display_signal_scanline_ordering,"
    "native_raster_sampling_requested,native_raster_open_result_valid,native_raster_open_result,native_raster_source_valid,native_raster_vidpn_source_id,"
    "native_raster_before_query_result_valid,native_raster_before_query_result,native_raster_before_query_start_us,native_raster_before_query_end_us,native_raster_before_in_vertical_blank,native_raster_before_scanline,"
    "native_raster_after_query_result_valid,native_raster_after_query_result,native_raster_after_query_start_us,native_raster_after_query_end_us,native_raster_after_in_vertical_blank,native_raster_after_scanline,"
    "submission_id_query_result_valid,submission_id_query_result,submission_id_query_start_us,submission_id_query_end_us,frame_stats_query_result_valid,frame_stats_query_result,frame_stats_query_start_us,frame_stats_query_end_us,latch_raw_sync_qpc_valid,latch_raw_sync_qpc_ticks,latch_raw_sync_qpc_frequency_hz,"
    "latch_qpc_correlation_valid,latch_qpc_correlation_reference_ticks,latch_qpc_correlation_reference_time_us,latch_qpc_correlation_span_ticks,"
    "readiness_phase_us,readiness_demand_us,applied_readiness_reserve_us,render_baseline_us,render_insurance_us,gpu_readiness_applied_us,pacing_latency_budget_us,cadence_sample_count,rate_candidate_sample_count,readiness_sample_count,preparation_sample_count,render_scheduler_sample_count,target_scheduler_sample_count,clean_spacing_frames,phase_error_frames,readiness_model_valid,playout_delay_us,cadence_smoothing_us,missed_ticks,"
    "decode_sync_wait_us,prepare_timing_valid,prepare_decode_sync_us,prepare_acquire_us,prepare_render_us,prepare_flush_us,"
    "gap_fills_before,gap_fill_last_us,original_target_us,playout_initial_profile,original_scanout_us,predicted_scanout_us,compositor_lead_us,recovery_headroom_us,smoothness_protection_us,requested_playout_delay_us,submission_smoothness_samples,submission_smoothness_misses,native_smoothness_samples,native_smoothness_misses,playout_capacity_limited,presentation_uncertainty_us"
    VRR_TIMING_PARAMETER_FIELDS(VRR_TRACE_PARAMETER_HEADER)
    ",decoder_output_us,session_latency_mode,session_readiness_hitch_feedback,calibration_loaded,initial_cached_samples,history_version,history_state_valid,history_samples,history_misses,history_duration_us,history_can_release"
    ",session_latency_oscillation,latency_test_phase,session_allow_tearing"
    "\n";
#undef VRR_TRACE_PARAMETER_HEADER
constexpr uint32_t kVrrWindowStateMask =
    WINDOW_STATE_CHANGE_SIZE |
    WINDOW_STATE_CHANGE_DISPLAY |
    WINDOW_STATE_CHANGE_MINIMIZED |
    WINDOW_STATE_CHANGE_RESTORED |
    WINDOW_STATE_CHANGE_SUSPENDED;
constexpr uint32_t kVrrDisplayEpochStateMask =
    WINDOW_STATE_CHANGE_SIZE |
    WINDOW_STATE_CHANGE_DISPLAY;

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
    if (difference > static_cast<uint64_t>(
                         std::numeric_limits<int64_t>::max())) {
        return std::numeric_limits<int64_t>::min();
    }
    return -static_cast<int64_t>(difference);
}

uint64_t positiveDifference(uint64_t actualUs, uint64_t targetUs)
{
    return actualUs > targetUs ? actualUs - targetUs : 0;
}

uint64_t saturatingAdd(uint64_t left, uint64_t right)
{
    return left > std::numeric_limits<uint64_t>::max() - right ?
        std::numeric_limits<uint64_t>::max() : left + right;
}

uint64_t submissionBoundaryUs(const VrrPresentFeedback& feedback,
                              uint64_t operationStartUs,
                              uint64_t operationEndUs,
                              bool& usedPresenterSubmissionTime)
{
    usedPresenterSubmissionTime = false;
    if (!feedback.presented) {
        return 0;
    }

    // A backend may wait for physical scanout inside presentAdaptive(). Use
    // its exact native-call timestamp only when it belongs to this operation;
    // stale or cross-epoch feedback must not poison every future spacing floor.
    if (feedback.submissionTimeValid &&
            operationStartUs <= operationEndUs &&
            feedback.submissionTimeUs >= operationStartUs &&
            feedback.submissionTimeUs <= operationEndUs) {
        usedPresenterSubmissionTime = true;
        return feedback.submissionTimeUs;
    }

    // Presenters without native timing are required to be thin. Anchoring at
    // entry prevents an unrelated blocking return from adding a display period
    // to every subsequent frame.
    return operationStartUs;
}

#ifdef _WIN32
bool isUncPath(const char* path)
{
    return path != nullptr &&
        ((path[0] == '\\' && path[1] == '\\') ||
         (path[0] == '/' && path[1] == '/'));
}
#endif

} // namespace

VrrPacingWorker::VrrPacingWorker(IVrrFramePresenter* presenter,
                                 const VrrSessionConfig& config,
                                 PacerTelemetry* telemetry) :
    m_Presenter(presenter),
    m_Telemetry(telemetry),
    m_Config(config),
    m_CanLatchPresentation(presenter != nullptr &&
                           presenter->canLatchAdaptivePresent()),
    m_TimingController(std::make_unique<VrrTimingController>(
        config, m_CanLatchPresentation,
        vrrTimingParametersForSession(config)))
{
    const char* deepTraceEnv = SDL_getenv("MOONLIGHT_VRR_DEEP_TRACE");
    m_DeepTraceEnabled = deepTraceEnv != nullptr && deepTraceEnv[0] == '1';

    VrrTargetWaiterHooks hooks;
    hooks.nowUs = []() {
        return LiGetMicroseconds();
    };
    hooks.yield = []() {
        std::this_thread::yield();
    };
    m_TargetWaiter = std::make_unique<VrrTargetWaiter>(std::move(hooks));
}

VrrPacingWorker::~VrrPacingWorker()
{
    {
        QMutexLocker lock(&m_FrameQueueLock);
        m_Stopping.store(true);
        m_FrameQueueNotEmpty.wakeAll();
    }

    if (m_WorkerThread != nullptr) {
        SDL_WaitThread(m_WorkerThread, nullptr);
        m_WorkerThread = nullptr;
    }

    discardQueuedFrames(false, TraceDisposition::ShutdownDiscard);
    closeTrace();
    if (m_WorkerStarted && !m_Config.calibrationKey.empty() && !m_CalibrationInvalidated.load()) {
        // Current DXGI observations do not always carry the presentation instant.
        // Until native coverage can qualify a run, cache history only, never
        // promote inferred success into a proven low-latency startup.
        Vrr13::saveProfile(QString::fromStdString(m_Config.calibrationPath),
                          QString::fromStdString(m_Config.calibrationKey),
                          m_TimingController->playoutHistory());
    }
}

bool VrrPacingWorker::start()
{
    if (m_Presenter == nullptr ||
        m_Presenter->checkSupport() != VrrFallbackReason::NoFallback) {
        return false;
    }

    if (!m_Config.calibrationKey.empty()) {
        Vrr13::Reserve prior(m_TimingController->playoutHistory().version());
        if (Vrr13::loadProfile(QString::fromStdString(m_Config.calibrationPath),
                              QString::fromStdString(m_Config.calibrationKey), prior)) {
            m_CalibrationLoaded = m_TimingController->loadPlayoutHistory(prior.profile());
        }
    }
    m_InitialPlayoutProfile = encodeVrrPlayoutProfile(m_TimingController->playoutHistory());
    m_InitialCachedSamples = m_TimingController->playoutHistory().cachedEvidence();
    m_HistoryVersion = m_TimingController->playoutHistory().version();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "VRR capture policy: latency_mode=%d readiness_hitch_feedback=%d history_version=%d calibration_loaded=%d cached_samples=%llu",
                m_Config.latencyMode, int(m_Config.readinessHitchFeedback),
                m_HistoryVersion, int(m_CalibrationLoaded),
                static_cast<unsigned long long>(m_InitialCachedSamples));

    // Enable capture before the producer can submit its first frame. Opening
    // from run() left a small startup race that made session replay incomplete.
    openTraceIfRequested();

    m_WorkerThread = SDL_CreateThread(VrrPacingWorker::threadProc,
                                      "PacerVRR", this);
    if (m_WorkerThread == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create VRR pacing worker: %s", SDL_GetError());
        closeTrace();
        return false;
    }

    m_WorkerStarted = true;
    if (m_Telemetry != nullptr) {
        m_Telemetry->beginVrrSession();
    }

    return true;
}

void VrrPacingWorker::submit(PacedFrame&& frame)
{
    if (!frame) {
        return;
    }

    // Capture an asynchronous decoder boundary before a newer frame can add
    // work behind it. A later render must wait for this frame, not everything
    // the decoder happened to queue before the pacing worker ran.
    frame.setDecodeBoundary(m_Presenter->captureDecodeBoundary());

    QueuedFrame incoming;
    incoming.frame = std::move(frame);
    // Admission age is pacing state even when no diagnostic capture is open.
    incoming.trace.arrivalUs = LiGetMicroseconds();
    if (m_TraceAcceptingRows.load()) {
        incoming.trace.arrivalSequence =
            m_TraceArrivalSequence.fetch_add(1) + 1;
    }

    QueuedFrame droppedFrame;
    TraceDisposition droppedDisposition = TraceDisposition::ArrivalRejected;
    bool queuedFrame = false;
    {
        QMutexLocker lock(&m_FrameQueueLock);
        incoming.trace.queueDepthBefore = m_FrameQueue.size();
        // Close the race where suspension can begin after the optimistic
        // check above but before this producer acquires the queue lock.
        if (m_Stopping.load() || m_Suspended.load()) {
            incoming.trace.queueDepthAfter = m_FrameQueue.size();
            droppedFrame = std::move(incoming);
        }
        else {
            if (m_FrameQueue.size() >= kMaximumQueuedFrames) {
                droppedFrame = std::move(m_FrameQueue.front());
                m_FrameQueue.pop_front();
                droppedDisposition = TraceDisposition::QueueCapacity;
                // Without a rebase, a multi-frame RTP jump to the freshest
                // successor can be mistaken for time still left to wait.
                m_QueueDiscontinuity.store(true);
            }
            incoming.trace.queueAccepted = true;
            m_FrameQueue.emplace_back(std::move(incoming));
            m_FrameQueue.back().trace.queueDepthAfter = m_FrameQueue.size();
            queuedFrame = true;
        }
        m_FrameQueueDepth.store(m_FrameQueue.size(), std::memory_order_relaxed);
    }

    if (droppedFrame) {
        recordFrameCompletion(droppedFrame, VrrTimingDecision {},
                   VrrPresentFeedback {}, FrameTelemetry {},
                   droppedDisposition, false);
        noteDrop();
    }
    if (queuedFrame) {
        m_FrameQueueNotEmpty.wakeOne();
    }
}

void VrrPacingWorker::notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info)
{
    if (info == nullptr) {
        return;
    }

    const uint32_t flags = info->stateChangeFlags & kVrrWindowStateMask;
    if (flags & kVrrDisplayEpochStateMask) m_CalibrationInvalidated.store(true);
    if (flags == 0) {
        return;
    }

    if (flags & (WINDOW_STATE_CHANGE_MINIMIZED | WINDOW_STATE_CHANGE_SUSPENDED)) {
        m_Suspended.store(true);
        discardQueuedFrames(true, TraceDisposition::SuspensionDiscard);
    }
    if (flags & WINDOW_STATE_CHANGE_RESTORED) {
        m_Suspended.store(false);
    }

    m_PendingWindowStateFlags.fetch_or(flags);
    m_FrameQueueNotEmpty.wakeAll();
}

int VrrPacingWorker::threadProc(void* context)
{
    return static_cast<VrrPacingWorker*>(context)->run();
}

int VrrPacingWorker::run()
{
#if SDL_VERSION_ATLEAST(2, 0, 9)
    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL) < 0) {
#else
    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) < 0) {
#endif
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to set VRR pacing worker priority: %s",
                    SDL_GetError());
    }

    while (!isStopping()) {
        consumeWindowStateNotifications();

        QueuedFrame queuedFrame;
        bool queueDiscontinuity = false;
        if (!dequeueFrame(queuedFrame, queueDiscontinuity)) {
            break;
        }
        queuedFrame.trace.dequeueUs = LiGetMicroseconds();
        queuedFrame.trace.queueDiscontinuity = queueDiscontinuity;
        PacedFrame& frame = queuedFrame.frame;

        consumeWindowStateNotifications();
        if (isStopping()) {
            if (frame) {
                recordFrameCompletion(queuedFrame, VrrTimingDecision {},
                           VrrPresentFeedback {}, FrameTelemetry {},
                           TraceDisposition::ShutdownDiscard, false);
                noteDrop();
            }
            continue;
        }
        if (presentationSuspended()) {
            if (frame) {
                recordFrameCompletion(queuedFrame, VrrTimingDecision {},
                           VrrPresentFeedback {}, FrameTelemetry {},
                           TraceDisposition::SuspensionDiscard, false);
                noteDrop();
            }
            // dequeueFrame() wakes without a frame so suspension can be
            // delivered to the backend. Once delivered, block here until a
            // restore or shutdown notification changes the atomic state.
            QMutexLocker lock(&m_FrameQueueLock);
            while (!isStopping() && m_Suspended.load()) {
                m_FrameQueueNotEmpty.wait(&m_FrameQueueLock);
            }
            continue;
        }
        if (!frame) {
            // dequeueFrame() also wakes the worker without a frame so window
            // state can be delivered while suspended.
            continue;
        }

        bool externalRebaseApplied = false;
        uint32_t externalRebaseFlags = 0;
        if (m_RebaseOnNextFrame) {
            m_TimingController->rebase();
            m_RebaseOnNextFrame = false;
            externalRebaseFlags = m_RebaseOnNextFrameFlags;
            m_RebaseOnNextFrameFlags = 0;
            externalRebaseApplied = true;
        }
        // A local latest-frame queue replacement is not a source epoch
        // change. Frame-number and cumulative RTP movement let the timing
        // controller advance across the omitted frame without throwing away
        // its learned cadence.
        (void) queueDiscontinuity;

        // The CPU reports decode completion before the GPU has finished the
        // frame. Wait for it here, where the worker would otherwise idle, so
        // readiness and the lateness the calibrator learns from are real and
        // the preparation never blocks on the decoder.
        const uint64_t decodeSyncWaitUs = m_Presenter->waitForDecode(
            frame.frame(), frame.decodeBoundary());
        if (decodeSyncWaitUs > kDecodeSyncNoticeUs) {
            // The wait can overlap time already spent in the pacing queue.
            // Keep that queue residence out of the readiness model by adding
            // only the blocking fence cost to immutable decoder output.
            frame.noteGpuReadyUs(decodeSyncWaitUs >
                    std::numeric_limits<uint64_t>::max() - frame.decoderOutputUs() ?
                std::numeric_limits<uint64_t>::max() :
                frame.decoderOutputUs() + decodeSyncWaitUs);
        }

        const uint64_t decisionTimeUs = LiGetMicroseconds();
        VrrTimingDecision decision = m_TimingController->schedule(
            frame, decisionTimeUs);
        FrameTelemetry telemetry;
        telemetry.decodeSyncWaitUs = decodeSyncWaitUs;
        telemetry.decisionTimeUs = decisionTimeUs;
        telemetry.decisionEndUs = LiGetMicroseconds();
        telemetry.externalRebaseApplied = externalRebaseApplied;
        telemetry.externalRebaseFlags = externalRebaseFlags;

        // schedule() deliberately clamps an overdue target to the current
        // one-slot deadline. That is right for the newest frame, but it makes
        // the later target-relative stale check unable to see time already
        // spent waiting in this worker's transport queue. One interval of age
        // is ordinary single-worker occupancy, so two source intervals are
        // tolerated. Sustained overload still drops older frames whenever a
        // fresher successor exists, and queue capacity remains the hard bound.
        // A third interval was once granted to a "smoothness" option; on a
        // render-bound client it only deepened the standing backlog.
        const uint64_t scheduleNowUs = LiGetMicroseconds();
        telemetry.staleCheckUs = scheduleNowUs;
        const uint64_t scheduleAgeUs = scheduleNowUs >=
                frame.decodeCompleteUs() ?
            scheduleNowUs - frame.decodeCompleteUs() : 0;
        telemetry.staleAgeUs = scheduleAgeUs;
        const bool metronome =
            m_TimingController->parameters().playoutMetronomeEnabled != 0;
        const bool latencyFix = m_TimingController->latencyFixActive();
        // The optional near-ceiling policy measures transport occupancy from
        // admission. Other modes retain their existing GPU-readiness origin
        // for stale-work policy; reporting always uses immutable decoder
        // output below.
        const uint64_t ageOriginUs = latencyFix ?
            queuedFrame.trace.arrivalUs : frame.decodeCompleteUs();
        const uint64_t ageUs = positiveDifference(scheduleNowUs, ageOriginUs);
        if (hasQueuedFrame() && VrrFrameDropPolicy::beforeRender(
                decision, m_TimingController->displayPeriodUs(), ageUs, metronome, latencyFix)) {
            recordFrameCompletion(queuedFrame, decision, VrrPresentFeedback {}, telemetry,
                       TraceDisposition::Stale);
            noteDrop();
            m_TimingController->noteSubmission(false, false, 0);
            continue;
        }

        telemetry.renderWaitEntryUs = LiGetMicroseconds();
        const VrrTargetWaitResult renderWait =
            m_TargetWaiter->waitUntil(decision.renderStartUs);
        telemetry.renderWait = renderWait;
        telemetry.renderWaitFinalUs = renderWait.finalNowUs;
        telemetry.renderSchedulerDelayUs = renderWait.schedulerDelayUs;
        telemetry.renderSchedulerDelayValid = renderWait.schedulerDelayValid;
        telemetry.renderDeadlineAlreadyElapsed =
            renderWait.deadlineAlreadyElapsed;
        // A deadline that was already in the past is not an OS wake delay.
        // This matters when a decoded frame arrives after its projected render
        // start: readiness/render telemetry owns that lateness instead.
        telemetry.renderWaitOvershootUs = renderWait.deadlineAlreadyElapsed ?
            0 : positiveDifference(renderWait.finalNowUs,
                                   decision.renderStartUs);
        uint32_t midframeWindowStateFlags =
            consumeWindowStateNotifications();
        telemetry.midframeWindowStateFlags |=
            midframeWindowStateFlags;
        bool displayEpochInterrupted =
            (midframeWindowStateFlags &
                kVrrDisplayEpochStateMask) != 0;
        if (displayEpochInterrupted ||
                presentationSuspended() || isStopping()) {
            telemetry.presentStartUs = LiGetMicroseconds();
            VrrPresentFeedback feedback = m_Presenter->cancelFrame();
            telemetry.presentEndUs = LiGetMicroseconds();
            telemetry.presentDurationUs =
                telemetry.presentEndUs >= telemetry.presentStartUs ?
                    telemetry.presentEndUs - telemetry.presentStartUs : 0;
            feedback.cancelled = true;
            recordSubmission(decision, feedback, telemetry.presentStartUs,
                             telemetry.presentEndUs,
                             telemetry);
            if (m_Telemetry != nullptr) {
                m_Telemetry->recordVrrOutcome(feedback.presented,
                                              feedback.cancelled);
            }
            recordFrameCompletion(queuedFrame, decision, feedback, telemetry,
                       TraceDisposition::Interrupted);
            noteDrop();
            continue;
        }

        // A frame can become stale while the worker waits for its render
        // start. Leave the surface unprepared and let the next iteration start
        // fresh rather than rendering an avoidably old image.
        uint64_t nowUs = LiGetMicroseconds();
        if (hasQueuedFrame() && VrrFrameDropPolicy::afterRenderWait(
                decision, ageOriginUs, nowUs, metronome, latencyFix)) {
            recordFrameCompletion(queuedFrame, decision, VrrPresentFeedback {}, telemetry,
                       TraceDisposition::Stale);
            noteDrop();
            if (metronome || latencyFix) {
                // The tick is freed for the successor; the clock mapping is
                // still valid and a rebase would only restart its warm-up.
                m_TimingController->noteSubmission(false, false, 0);
            }
            else {
                m_TimingController->rebase();
            }
            continue;
        }

        VrrPresentRequest presentRequest;
        presentRequest.latchedPresentation = decision.latchedPresentation;
        presentRequest.collectDiagnostics = m_DeepTraceEnabled;

        telemetry.preparationStartUs = LiGetMicroseconds();
        const VrrPrepareResult preparation =
            m_Presenter->prepareFrame(frame.frame(), frame.decodeBoundary(),
                                     presentRequest);
        telemetry.preparationEndUs = LiGetMicroseconds();
        telemetry.preparationDurationUs =
            telemetry.preparationEndUs >= telemetry.preparationStartUs ?
                telemetry.preparationEndUs - telemetry.preparationStartUs : 0;
        telemetry.prepareTimingValid = preparation.timingValid;
        telemetry.prepareDecodeSyncUs = preparation.decodeSyncUs;
        telemetry.prepareAcquireUs = preparation.acquireUs;
        telemetry.prepareRenderUs = preparation.renderUs;
        telemetry.prepareFlushUs = preparation.flushUs;
        const bool gpuReadyCompleted =
            preparation.feedback.gpuReadyTimingValid &&
            preparation.feedback.gpuReadyWaitResultValid &&
            preparation.feedback.gpuReadyWaitResult == 0 &&
            preparation.feedback.gpuReadyTimeUs >=
                preparation.feedback.gpuReadyWaitStartUs;
        const uint64_t gpuReadyWaitUs = gpuReadyCompleted ?
            preparation.feedback.gpuReadyTimeUs -
                preparation.feedback.gpuReadyWaitStartUs : 0;
        m_TimingController->notePreparationDuration(
            telemetry.preparationDurationUs,
            preparation.timingValid ? preparation.acquireUs : 0,
            telemetry.preparationEndUs, gpuReadyWaitUs);
        m_TimingController->noteGpuReadyWait(
            gpuReadyWaitUs, gpuReadyCompleted,
            preparation.feedback.gpuReadyTimeUs);

        midframeWindowStateFlags =
            consumeWindowStateNotifications();
        telemetry.midframeWindowStateFlags |=
            midframeWindowStateFlags;
        displayEpochInterrupted =
            displayEpochInterrupted ||
            (midframeWindowStateFlags &
                kVrrDisplayEpochStateMask) != 0;
        if (!preparation.prepared || displayEpochInterrupted ||
                presentationSuspended() || isStopping()) {
            VrrPresentFeedback feedback = preparation.feedback;
            uint64_t submissionOperationStartUs =
                telemetry.preparationStartUs;
            uint64_t submissionOperationEndUs =
                telemetry.preparationEndUs;
            const bool mustCancel = !feedback.presented &&
                (preparation.prepared || preparation.cancellationMaySubmit ||
                 !feedback.cancelled);
            if (mustCancel) {
                // A presenter may need to submit an acquired image in order
                // to abandon it. It reports only that neutral fact; the worker
                // owns the target and display-spacing policy.
                if (preparation.cancellationMaySubmit) {
                    waitForSubmissionFloor(decision, telemetry);
                }
                telemetry.presentStartUs = LiGetMicroseconds();
                VrrPresentFeedback cancelFeedback = m_Presenter->cancelFrame();
                telemetry.presentEndUs = LiGetMicroseconds();
                telemetry.presentDurationUs =
                    telemetry.presentEndUs >= telemetry.presentStartUs ?
                        telemetry.presentEndUs - telemetry.presentStartUs : 0;
                // The cancellation call is the authoritative final backend
                // outcome even when its native submit/fence operation fails.
                // Keeping only successful cancellation submissions hid the
                // exact failure evidence from diagnostic traces.
                feedback = cancelFeedback;
                if (cancelFeedback.nativeBackendValid) {
                    submissionOperationStartUs = telemetry.presentStartUs;
                    submissionOperationEndUs = telemetry.presentEndUs;
                }
            }
            feedback.cancelled = true;
            recordSubmission(decision, feedback, submissionOperationStartUs,
                             submissionOperationEndUs,
                             telemetry);
            if (m_Telemetry != nullptr) {
                m_Telemetry->recordVrrOutcome(feedback.presented,
                                              feedback.cancelled);
            }
            recordFrameCompletion(queuedFrame, decision, feedback, telemetry,
                       preparation.prepared ? TraceDisposition::Interrupted :
                                              TraceDisposition::PreparationFailed);
            noteDrop();
            deferFrame(std::move(frame));
            continue;
        }

        if (preparation.sourceFrameReusable) {
            // The backend has completed every GPU read from this decoder
            // surface. Release it before the target wait so high-resolution
            // pacing cannot exhaust the decoder surface pool.
            AVFrame* reusableFrame = frame.release();
            av_frame_free(&reusableFrame);
        }

        telemetry.targetWaitEntryUs = LiGetMicroseconds();
        const VrrTargetWaitResult targetWait =
            m_TargetWaiter->waitUntil(decision.targetUs,
                                      decision.targetWakeLeadUs);
        telemetry.targetWait = targetWait;
        telemetry.targetWaitFinalUs = targetWait.finalNowUs;
        telemetry.targetWaitOvershootUs = targetWait.deadlineAlreadyElapsed ?
            0 : positiveDifference(targetWait.finalNowUs,
                                   decision.targetUs);
        telemetry.targetSchedulerDelayUs = targetWait.schedulerDelayUs;
        telemetry.targetSchedulerDelayValid =
            targetWait.schedulerDelayValid;
        telemetry.targetDeadlineAlreadyElapsed =
            targetWait.deadlineAlreadyElapsed;
        midframeWindowStateFlags =
            consumeWindowStateNotifications();
        telemetry.midframeWindowStateFlags |=
            midframeWindowStateFlags;
        displayEpochInterrupted =
            displayEpochInterrupted ||
            (midframeWindowStateFlags &
                kVrrDisplayEpochStateMask) != 0;
        if (displayEpochInterrupted ||
                presentationSuspended() || isStopping()) {
            if (preparation.cancellationMaySubmit) {
                waitForSubmissionFloor(decision, telemetry);
            }
            telemetry.presentStartUs = LiGetMicroseconds();
            VrrPresentFeedback feedback = m_Presenter->cancelFrame();
            telemetry.presentEndUs = LiGetMicroseconds();
            telemetry.presentDurationUs =
                telemetry.presentEndUs >= telemetry.presentStartUs ?
                    telemetry.presentEndUs - telemetry.presentStartUs : 0;
            feedback.cancelled = true;
            recordSubmission(decision, feedback, telemetry.presentStartUs,
                             telemetry.presentEndUs,
                             telemetry);
            if (m_Telemetry != nullptr) {
                m_Telemetry->recordVrrOutcome(feedback.presented,
                                              feedback.cancelled);
            }
            recordFrameCompletion(queuedFrame, decision, feedback, telemetry,
                       TraceDisposition::Interrupted);
            noteDrop();
            deferFrame(std::move(frame));
            continue;
        }

        // Recheck both mathematical floors immediately before Present. The
        // waiter deliberately has a bounded active phase, so a pathological
        // clock must not turn an early return into an early submission.
        uint64_t beforePresentUs = LiGetMicroseconds();
        telemetry.spacingCheckUs = beforePresentUs;
        uint64_t earliestSubmissionUs =
            m_TimingController->earliestSubmissionUs();
        m_TimingController->noteSpacingDeficit(0);
        if (earliestSubmissionUs != 0 &&
                beforePresentUs < earliestSubmissionUs) {
            telemetry.spacingDeficitUs =
                earliestSubmissionUs - beforePresentUs;
            telemetry.spacingCorrected = true;
        }

        const uint64_t presentationFloorUs = std::max(decision.targetUs,
                                                       earliestSubmissionUs);
        telemetry.presentationFloorUs = presentationFloorUs;
        while (beforePresentUs < presentationFloorUs) {
            m_TargetWaiter->waitUntil(presentationFloorUs);
            beforePresentUs = LiGetMicroseconds();
        }

        const bool hadPriorSubmission =
            m_TimingController->hasLastSubmission();
        const uint64_t priorSubmissionUs =
            m_TimingController->lastSubmissionUs();
        telemetry.spacingRecheckUs = LiGetMicroseconds();
        telemetry.presentStartUs = telemetry.spacingRecheckUs;
        if (hadPriorSubmission) {
            telemetry.presentSpacingUs =
                telemetry.presentStartUs >= priorSubmissionUs ?
                    telemetry.presentStartUs - priorSubmissionUs : 0;
            const uint64_t minimumUntornUs = priorSubmissionUs +
                m_TimingController->displayPeriodUs();
            telemetry.spacingMarginUs = signedDifference(
                telemetry.presentStartUs, minimumUntornUs);

            // A second check protects against a clock anomaly between the
            // first check and the actual call boundary.
            if (telemetry.spacingMarginUs < 0) {
                const uint64_t deficitUs = static_cast<uint64_t>(
                    -(telemetry.spacingMarginUs + 1)) + 1;
                telemetry.spacingDeficitUs = std::max(
                    telemetry.spacingDeficitUs, deficitUs);
                telemetry.spacingGuardFeedbackUs = deficitUs;
                telemetry.spacingCorrected = true;
                m_TimingController->noteSpacingDeficit(deficitUs);
                const uint64_t correctedFloorUs =
                    m_TimingController->earliestSubmissionUs();
                telemetry.spacingCorrectedFloorUs = correctedFloorUs;
                telemetry.correctionWaitStartUs = LiGetMicroseconds();
                telemetry.presentStartUs = LiGetMicroseconds();
                while (telemetry.presentStartUs < correctedFloorUs) {
                    m_TargetWaiter->waitUntil(correctedFloorUs);
                    telemetry.presentStartUs = LiGetMicroseconds();
                }
                telemetry.correctionWaitEndUs = telemetry.presentStartUs;
                telemetry.presentSpacingUs =
                    telemetry.presentStartUs >= priorSubmissionUs ?
                        telemetry.presentStartUs - priorSubmissionUs : 0;
                telemetry.spacingMarginUs = signedDifference(
                    telemetry.presentStartUs, minimumUntornUs);
            }
        }

        // The mathematical floor and its correction can add another
        // display-period wait after the primary target checkpoint. Drain
        // notifications once more at the actual submission boundary so a
        // minimize or a renderer-accepted display epoch cannot slip through
        // that final wait and be presented under the old decision.
        midframeWindowStateFlags =
            consumeWindowStateNotifications();
        telemetry.midframeWindowStateFlags |=
            midframeWindowStateFlags;
        displayEpochInterrupted =
            displayEpochInterrupted ||
            (midframeWindowStateFlags &
                kVrrDisplayEpochStateMask) != 0;
        if (displayEpochInterrupted ||
                presentationSuspended() || isStopping()) {
            telemetry.presentStartUs = LiGetMicroseconds();
            VrrPresentFeedback feedback = m_Presenter->cancelFrame();
            telemetry.presentEndUs = LiGetMicroseconds();
            telemetry.presentDurationUs =
                telemetry.presentEndUs >= telemetry.presentStartUs ?
                    telemetry.presentEndUs - telemetry.presentStartUs : 0;
            feedback.cancelled = true;
            recordSubmission(decision, feedback, telemetry.presentStartUs,
                             telemetry.presentEndUs,
                             telemetry);
            if (m_Telemetry != nullptr) {
                m_Telemetry->recordVrrOutcome(feedback.presented,
                                              feedback.cancelled);
            }
            recordFrameCompletion(queuedFrame, decision, feedback, telemetry,
                       TraceDisposition::Interrupted);
            noteDrop();
            deferFrame(std::move(frame));
            continue;
        }

        m_TimingController->noteSchedulerDelays(
            telemetry.renderWaitOvershootUs,
            targetWait.schedulerDelayUs,
            targetWait.schedulerDelayValid);

        telemetry.presentStartUs = LiGetMicroseconds();
        VrrPresentFeedback feedback =
            m_Presenter->presentAdaptive(presentRequest);
        telemetry.presentEndUs = LiGetMicroseconds();
        telemetry.presentDurationUs =
            telemetry.presentEndUs >= telemetry.presentStartUs ?
                telemetry.presentEndUs - telemetry.presentStartUs : 0;
        recordSubmission(decision, feedback, telemetry.presentStartUs,
                         telemetry.presentEndUs,
                         telemetry);
        if (m_Telemetry != nullptr) {
            VrrTelemetrySample sample;
            sample.queueResidenceUs = positiveDifference(queuedFrame.trace.dequeueUs,
                                                         queuedFrame.trace.arrivalUs);
            sample.decodeWaitUs = telemetry.decodeSyncWaitUs;
            sample.bufferUs = decision.playoutDelayUs;
            sample.submissionUs = telemetry.submissionBoundaryUs;
            sample.motionDiscontinuity = decision.rebased || externalRebaseApplied;
            sample.decisionTimeUs = decisionTimeUs;
            sample.clientProcessingTimeUs =
                telemetry.presentEndUs >= frame.decoderOutputUs() ?
                    telemetry.presentEndUs - frame.decoderOutputUs() : 0;
            sample.renderingTimeUs = telemetry.preparationDurationUs +
                telemetry.presentDurationUs;
            const uint64_t readinessDeadlineUs =
                m_TimingController->parameters().playoutResponsiveBuffer >= 3 ?
                decision.originalTargetUs : decision.targetUs;
            sample.prepareLate = telemetry.preparationEndUs > readinessDeadlineUs;
            sample.cadenceIntervals = m_TimingController->nativeCadenceIntervals();
            sample.cadenceHitches = m_TimingController->nativeCadenceHitches();
            sample.estimatedCadenceIntervals = m_TimingController->estimatedCadenceIntervals();
            sample.estimatedCadenceHitches = m_TimingController->estimatedCadenceHitches();
            sample.preparationLatenessUs = sample.prepareLate ?
                telemetry.preparationEndUs - readinessDeadlineUs : 0;
            sample.targetWaitEntryLate = !sample.prepareLate &&
                telemetry.preparationEndUs < decision.targetUs &&
                targetWait.deadlineAlreadyElapsed;
            sample.submitErrorUs = telemetry.submitErrorUs;
            sample.spacingCorrected = telemetry.spacingCorrected;
            sample.presented = feedback.presented;
            sample.cancelled = feedback.cancelled;
            sample.readinessBudgetUs = decision.readinessBudgetUs;
            sample.timingBudgetUs = decision.timingBudgetUs;
            sample.renderLeadUs = decision.renderLeadUs;
            sample.renderWakeLeadUs = decision.renderWakeLeadUs;
            sample.targetWakeLeadUs = decision.targetWakeLeadUs;
            sample.guardUs = decision.guardUs;
            sample.sourcePeriodUs = decision.sourcePeriodUs;
            m_Telemetry->recordVrrFrame(sample);
        }

        const bool outputDropped = !feedback.presented || feedback.cancelled;
        if (outputDropped) {
            noteDrop();
        }
        recordFrameCompletion(queuedFrame, decision, feedback, telemetry,
                   outputDropped ?
                       TraceDisposition::OutputDropped :
                       TraceDisposition::Presented);
        deferFrame(std::move(frame));
    }

    // Release any native state retained between preparation and presentation.
    m_Presenter->cancelFrame();
    return 0;
}

bool VrrPacingWorker::dequeueFrame(QueuedFrame& frame,
                                   bool& queueDiscontinuity)
{
    QMutexLocker lock(&m_FrameQueueLock);
    while (!isStopping() && !m_Suspended.load() && m_FrameQueue.empty()) {
        m_FrameQueueNotEmpty.wait(&m_FrameQueueLock);
    }

    if (isStopping()) {
        return false;
    }
    if (m_Suspended.load()) {
        return true;
    }

    queueDiscontinuity = m_QueueDiscontinuity.exchange(false);
    frame = std::move(m_FrameQueue.front());
    m_FrameQueue.pop_front();
    m_FrameQueueDepth.store(m_FrameQueue.size(), std::memory_order_relaxed);
    return true;
}

bool VrrPacingWorker::hasQueuedFrame()
{
    QMutexLocker lock(&m_FrameQueueLock);
    return !m_FrameQueue.empty();
}

void VrrPacingWorker::discardQueuedFrames(
    bool countDrops, TraceDisposition disposition)
{
    std::deque<QueuedFrame> discardedFrames;
    {
        QMutexLocker lock(&m_FrameQueueLock);
        discardedFrames.swap(m_FrameQueue);
        m_FrameQueueDepth.store(0, std::memory_order_relaxed);
        m_QueueDiscontinuity.store(false);
    }

    for (const QueuedFrame& frame : discardedFrames) {
        recordFrameCompletion(frame, VrrTimingDecision {}, VrrPresentFeedback {},
                   FrameTelemetry {}, disposition, false);
        if (countDrops) {
            noteDrop();
        }
    }
}

uint32_t VrrPacingWorker::consumeWindowStateNotifications()
{
    uint32_t consumedFlags =
        m_PendingWindowStateFlags.exchange(0);
    if (consumedFlags == 0) {
        return 0;
    }

    // Minimize/restore can race while this worker is draining notifications.
    // Reconcile to the authoritative atomic state rather than applying a stale
    // flag snapshot in event order.
    bool suspended = m_Suspended.load();
    while (true) {
        const uint32_t newerFlags = m_PendingWindowStateFlags.exchange(0);
        consumedFlags |= newerFlags;
        const bool latestSuspended = m_Suspended.load();
        if (newerFlags == 0 && latestSuspended == suspended) {
            break;
        }
        suspended = latestSuspended;
    }

    if (suspended != m_PresenterSuspended) {
        m_Presenter->setSuspended(suspended);
        m_PresenterSuspended = suspended;
    }
    m_RebaseOnNextFrame = true;
    m_RebaseOnNextFrameFlags |= consumedFlags;
    return consumedFlags;
}

bool VrrPacingWorker::presentationSuspended() const
{
    // If UI state changed just after notification draining, either side being
    // suspended is enough to prevent a misclassified finish. The next loop
    // reconciles the presenter to the newest authoritative state.
    return m_Suspended.load() || m_PresenterSuspended;
}

bool VrrPacingWorker::isStopping() const
{
    return m_Stopping.load();
}

void VrrPacingWorker::waitForSubmissionFloor(
    const VrrTimingDecision& decision, FrameTelemetry& telemetry)
{
    const uint64_t earliestSubmissionUs =
        m_TimingController->earliestSubmissionUs();
    const uint64_t submissionFloorUs = std::max(decision.targetUs,
                                                 earliestSubmissionUs);
    uint64_t nowUs = LiGetMicroseconds();
    if (telemetry.targetWaitEntryUs == 0) {
        telemetry.targetWaitEntryUs = nowUs;
    }
    telemetry.spacingCheckUs = nowUs;
    telemetry.presentationFloorUs = submissionFloorUs;
    if (nowUs >= submissionFloorUs) {
        telemetry.targetWaitFinalUs = std::max(
            telemetry.targetWaitFinalUs, nowUs);
        telemetry.targetDeadlineAlreadyElapsed = true;
        return;
    }

    if (earliestSubmissionUs != 0 && nowUs < earliestSubmissionUs) {
        telemetry.spacingDeficitUs = std::max(
            telemetry.spacingDeficitUs, earliestSubmissionUs - nowUs);
        telemetry.spacingCorrected = true;
    }

    const VrrTargetWaitResult wait = m_TargetWaiter->waitUntil(
        submissionFloorUs, decision.targetWakeLeadUs);
    telemetry.targetWaitFinalUs = std::max(telemetry.targetWaitFinalUs,
                                            wait.finalNowUs);
    telemetry.targetDeadlineAlreadyElapsed =
        telemetry.targetDeadlineAlreadyElapsed || wait.deadlineAlreadyElapsed;
    if (wait.schedulerDelayValid) {
        telemetry.targetSchedulerDelayUs = std::max(
            telemetry.targetSchedulerDelayUs, wait.schedulerDelayUs);
        telemetry.targetSchedulerDelayValid = true;
    }
    if (!wait.deadlineAlreadyElapsed) {
        telemetry.targetWaitOvershootUs = std::max(
            telemetry.targetWaitOvershootUs,
            positiveDifference(wait.finalNowUs, submissionFloorUs));
    }

    // The waiter deliberately bounds each active phase. Re-enter it until the
    // shared monotonic clock confirms the floor; an incomplete wait must never
    // become permission to submit.
    nowUs = LiGetMicroseconds();
    while (nowUs < submissionFloorUs) {
        m_TargetWaiter->waitUntil(submissionFloorUs);
        nowUs = LiGetMicroseconds();
    }
    telemetry.targetWaitFinalUs = std::max(
        telemetry.targetWaitFinalUs, nowUs);
    if (!wait.deadlineAlreadyElapsed) {
        telemetry.targetWaitOvershootUs = std::max(
            telemetry.targetWaitOvershootUs,
            positiveDifference(nowUs, submissionFloorUs));
    }
}

void VrrPacingWorker::recordSubmission(
    const VrrTimingDecision& decision,
    const VrrPresentFeedback& feedback,
    uint64_t operationStartUs,
    uint64_t operationEndUs,
    FrameTelemetry& telemetry)
{
    telemetry.submissionBoundaryUs = submissionBoundaryUs(
        feedback, operationStartUs, operationEndUs,
        telemetry.usedPresenterSubmissionTime);
    telemetry.hadPriorSubmission =
        m_TimingController->hasLastSubmission();

    if (feedback.presented) {
        telemetry.submitErrorUs = signedDifference(
            telemetry.submissionBoundaryUs, decision.targetUs);

        if (telemetry.hadPriorSubmission) {
            const uint64_t priorSubmissionUs =
                m_TimingController->lastSubmissionUs();
            telemetry.presentSpacingUs =
                telemetry.submissionBoundaryUs >= priorSubmissionUs ?
                    telemetry.submissionBoundaryUs - priorSubmissionUs : 0;
            const uint64_t minimumUntornUs = priorSubmissionUs +
                m_TimingController->displayPeriodUs();
            telemetry.spacingMarginUs = signedDifference(
                telemetry.submissionBoundaryUs, minimumUntornUs);
        }
    }

    m_TimingController->noteSubmission(
        feedback.presented, feedback.cancelled,
        telemetry.submissionBoundaryUs);
    Vrr13::PresentationObservation observation;
    observation.smoothness = m_TimingController->smoothnessSample(decision);
    observation.submitted = feedback.presented && !feedback.cancelled;
    observation.idValid = feedback.submissionIdValid;
    observation.id = feedback.submissionId;
    observation.submission = telemetry.submissionBoundaryUs;
    observation.ready = telemetry.preparationEndUs;
    observation.deadline = decision.originalScanoutUs;
    // Vulkan and composition do not select DXGI latch modes. A requested
    // latch transition must not reset their feedback matching history.
    observation.latched = (feedback.nativeBackend == VrrNativePresentationBackend::Vulkan ||
                           feedback.nativeBackend == VrrNativePresentationBackend::Composition) ?
        false : decision.latchedPresentation;
    observation.dxgi = feedback.nativeBackend == VrrNativePresentationBackend::Dxgi;
    observation.sampleValid = feedback.latchSampleValid &&
        (!observation.dxgi || (feedback.latchQpcCorrelationValid && feedback.latchRawSyncQpcFrequency));
    observation.sampleId = feedback.latchSubmissionId;
    observation.sampleTime = feedback.latchTimeUs;
    observation.timeKind = feedback.latchTimeKind;
    observation.observed = operationEndUs;
    observation.presentRefresh = feedback.latchPresentRefreshSequence;
    observation.syncRefresh = feedback.latchRefreshSequence;
    observation.uncertainty = feedback.latchRawSyncQpcFrequency ?
        feedback.latchQpcCorrelationSpanTicks * 1000000 / feedback.latchRawSyncQpcFrequency :
        feedback.presentationUncertaintyUs;
    m_TimingController->notePresentation(observation);
}

void VrrPacingWorker::deferFrame(PacedFrame&& frame)
{
    // Keep the last frame alive until a subsequent result so a decoder-owned
    // surface cannot be recycled while GPU work from this present still reads
    // it. The move assignment frees the older deferred frame outside queues.
    m_DeferredFrame = std::move(frame);
}

void VrrPacingWorker::noteDrop()
{
    if (m_Telemetry != nullptr) {
        m_Telemetry->recordVrrDrop();
    }
}

void VrrPacingWorker::recordFrameCompletion(const QueuedFrame& queuedFrame,
                                 const VrrTimingDecision& decision,
                                 const VrrPresentFeedback& feedback,
                                 const FrameTelemetry& telemetry,
                                 TraceDisposition disposition,
                                 bool decisionValid)
{
    // Publish every playback outcome once, even when disk tracing is disabled
    // or drops a row. Intentional window/shutdown discards are not playback.
    const bool playbackOutcome = disposition == TraceDisposition::Presented ||
        disposition == TraceDisposition::OutputDropped ||
        disposition == TraceDisposition::QueueCapacity ||
        disposition == TraceDisposition::Stale ||
        disposition == TraceDisposition::PreparationFailed;
    if (m_Telemetry && playbackOutcome) {
        const auto& parameters = m_TimingController->parameters();
        const uint64_t deadlineUs = parameters.playoutResponsiveBuffer >= 3 ?
            decision.originalTargetUs : decision.targetUs;
        const bool dropped = disposition != TraceDisposition::Presented;
        // Controller state belongs to the worker. Producer-side drop rows
        // preserve the last published interval result instead of reading it.
        const bool intervalValid = decisionValid && parameters.playoutResponsiveBuffer >= 6;
        const auto intervalStats = intervalValid ? m_TimingController->intervalStats() : Vrr13::IntervalBuffer::Stats{};
        m_Telemetry->recordVrrReadiness(LiGetMicroseconds(),
            decisionValid ? positiveDifference(telemetry.preparationEndUs, deadlineUs) : 0,
            dropped, parameters.playoutOnTimeTargetPerMillion,
            decision.playoutCapacityLimited, decisionValid,
            parameters.playoutResponsiveBuffer >= 4,
            parameters.playoutResponsiveBuffer >= 5,
            parameters.playoutResponsiveBuffer >= 6, intervalValid ? &intervalStats : nullptr);
    }
    if (queuedFrame.trace.arrivalSequence == 0) {
        return;
    }
    struct ProducerGuard {
        std::atomic_uint& active;
        explicit ProducerGuard(std::atomic_uint& value) : active(value) { ++active; }
        ~ProducerGuard() { --active; }
    } producer(m_TraceProducersActive);
    if (!m_TraceAcceptingRows.load()) {
        m_TraceDroppedRows.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const PacedFrame& frame = queuedFrame.frame;
    TraceRow row;
    row.frameNumber = frame.frameNumber();
    row.rtpTimestamp = frame.rtpTimestamp();
    row.timestampValid = frame.timestampValid();
    row.decodeCompleteUs = frame.decodeCompleteUs();
    row.decoderOutputUs = frame.decoderOutputUs();
    row.latencyMode = m_Config.latencyMode;
    row.receiveUs = frame.receiveUs();
    row.reassembledUs = frame.reassembledUs();
    row.decodeSubmitUs = frame.decodeSubmitUs();
    row.input = queuedFrame.trace;
    row.decision = decision;
    // submit() and window notifications may emit terminal rows from threads
    // other than the pacing worker. The controller's learned-state containers
    // are worker-owned and are not safe to inspect concurrently. A valid
    // decision proves this row is on the worker path; producer-side terminal
    // rows deliberately carry zero/unavailable controller diagnostics.
    row.diagnostics = decisionValid ?
        m_TimingController->diagnostics() : VrrTimingDiagnostics {};
    if (decisionValid) {
        // Only scalar reads on the controller-owning thread. Do not calculate
        // histogram quantiles or serialize calibration on the delivery path.
        const auto& history = m_TimingController->playoutHistory();
        row.historySamples = history.evidence();
        row.historyMisses = history.misses();
        row.historyDurationUs = static_cast<uint64_t>(history.duration() / 1000);
        row.historyCanRelease = history.canRelease();
    }
    row.feedback = feedback;
    row.telemetry = telemetry;
    row.completionQueueDepth =
        m_FrameQueueDepth.load(std::memory_order_relaxed);
    row.disposition = disposition;
    row.decisionValid = decisionValid;
    row.terminalTimeUs = LiGetMicroseconds();

    if (m_TraceQueue->push(std::move(row)))
        m_TraceRowsEnqueued.fetch_add(1, std::memory_order_relaxed);
    else
        m_TraceDroppedRows.fetch_add(1, std::memory_order_relaxed);
}

int VrrPacingWorker::traceThreadProc(void* context)
{
    return static_cast<VrrPacingWorker*>(context)->traceRun();
}

int VrrPacingWorker::traceRun()
{
    TraceRow row;
    while (true) {
        if (m_TraceQueue->pop(row)) {
            if (m_TraceAcceptingRows.load() ||
                    (m_TraceStopping.load() && !m_TraceWriteFailed && !m_TraceSizeCapped))
                writeTraceRow(row);
            continue;
        }
        if (m_TraceStopping.load() && m_TraceProducersActive.load() == 0) {
            // A producer could have published between the first empty read
            // and the active-count check. Drain that final row before exiting.
            if (m_TraceQueue->pop(row)) {
                if (!m_TraceWriteFailed && !m_TraceSizeCapped) writeTraceRow(row);
                continue;
            }
            if (m_TraceFormat == TraceFormat::ChunkedCompressed) {
                flushTraceChunk();
            }
            return 0;
        }
        // Poll only the background writer. Producers never wait for it or
        // contend with a reader-held mutex while delivering a frame.
        SDL_Delay(2);
    }
}

void VrrPacingWorker::writeTraceRow(const TraceRow& row)
{
    m_TraceLatestArrivalUs = std::max(m_TraceLatestArrivalUs,
                                      row.input.arrivalUs);
    const VrrTimingDecision& decision = row.decision;
    const VrrTimingDiagnostics& diagnostics = row.diagnostics;
    // The writer must never read mutable controller parameters. Reconstruct
    // this row's policy from immutable session settings and its captured mode.
    auto traceConfig = m_Config;
    traceConfig.latencyMode = row.latencyMode;
    const VrrTimingParameters parameters = vrrTimingParametersForSession(traceConfig);
    const VrrPresentFeedback& feedback = row.feedback;
    const FrameTelemetry& telemetry = row.telemetry;
    const uint64_t nativePresentDurationUs =
        feedback.nativePresentTimingValid &&
        feedback.nativePresentEndUs >= feedback.nativePresentStartUs ?
            feedback.nativePresentEndUs - feedback.nativePresentStartUs : 0;
    const uint64_t gpuReadyWaitUs = feedback.gpuReadyTimingValid &&
        feedback.gpuReadyTimeUs >= feedback.gpuReadyWaitStartUs ?
            feedback.gpuReadyTimeUs - feedback.gpuReadyWaitStartUs : 0;
    const uint64_t gpuReadyCompletionLowerBoundUs =
        feedback.gpuReadyTimingValid ?
            (feedback.gpuReadyCompletedBeforeWait ?
                 feedback.gpuReadySignalStartUs :
                 feedback.gpuReadyPollStartUs) :
            0;
    const uint64_t gpuReadyCompletionUpperBoundUs =
        feedback.gpuReadyTimingValid ?
            (feedback.gpuReadyCompletedBeforeWait ?
                 feedback.gpuReadyPollEndUs :
                 feedback.gpuReadyTimeUs) :
            0;
    const uint64_t gpuReadyCompletionUncertaintyUs =
        gpuReadyCompletionUpperBoundUs >=
                gpuReadyCompletionLowerBoundUs ?
            gpuReadyCompletionUpperBoundUs -
                gpuReadyCompletionLowerBoundUs :
            0;
    const uint64_t displayPeriodUs = m_Config.displayRefreshHz > 0 ?
        (1000000ULL + static_cast<uint64_t>(m_Config.displayRefreshHz) / 2) /
            static_cast<uint64_t>(m_Config.displayRefreshHz) : 0;

    QByteArray line;
    line.reserve(2048);
    auto separator = [&line]() {
        if (!line.isEmpty()) {
            line.append(',');
        }
    };
    auto addUnsigned = [&line, &separator](uint64_t value) {
        separator();
        line.append(QByteArray::number(value));
    };
    auto addSigned = [&line, &separator](int64_t value) {
        separator();
        line.append(QByteArray::number(value));
    };
    auto addBool = [&addUnsigned](bool value) {
        addUnsigned(value ? 1 : 0);
    };
    auto addText = [&line, &separator](const char* value) {
        separator();
        line.append(value);
    };

    addUnsigned(5);
    addUnsigned(row.input.arrivalSequence);
    addSigned(row.frameNumber);
    addUnsigned(row.rtpTimestamp);
    addBool(row.timestampValid);
    addUnsigned(row.decodeCompleteUs);
    addUnsigned(row.receiveUs);
    addUnsigned(row.reassembledUs);
    addUnsigned(row.decodeSubmitUs);
    addUnsigned(row.input.arrivalUs);
    addUnsigned(row.input.queueDepthBefore);
    addUnsigned(row.input.queueDepthAfter);
    addBool(row.input.queueAccepted);
    addUnsigned(row.input.dequeueUs);
    addBool(row.input.queueDiscontinuity);
    addBool(row.decisionValid);
    addUnsigned(telemetry.decisionTimeUs);
    addSigned(m_Config.displayRefreshHz);
    addSigned(m_Config.streamRateHz);
    addBool(m_Config.allowAdditionalQueuedFrame);
    addUnsigned(displayPeriodUs);
    addBool(m_CanLatchPresentation);
    addUnsigned(decision.sourceIntervalUs);
    separator();
    line.append(QByteArray::number(
        decision.sourcePeriodUs == 0 ? 0.0 :
            1000000.0 / static_cast<double>(decision.sourcePeriodUs), 'f', 3));
    addUnsigned(decision.sourcePeriodUs);
    addUnsigned(decision.sourceTimeUs);
    addSigned(decision.readyOffsetUs);
    addSigned(decision.readinessBudgetUs);
    addUnsigned(decision.timingBudgetUs);
    addUnsigned(decision.renderLeadUs);
    addUnsigned(decision.gpuReadinessLeadUs);
    addUnsigned(decision.renderWakeLeadUs);
    addUnsigned(decision.targetWakeLeadUs);
    addUnsigned(decision.guardUs);
    addUnsigned(decision.headroomUs);
    addUnsigned(decision.renderStartUs);
    addUnsigned(telemetry.renderWaitFinalUs);
    addUnsigned(telemetry.renderWaitOvershootUs);
    addUnsigned(telemetry.renderSchedulerDelayUs);
    addBool(telemetry.renderSchedulerDelayValid);
    addBool(telemetry.renderDeadlineAlreadyElapsed);
    addUnsigned(telemetry.renderWait.initialNowUs);
    addUnsigned(telemetry.renderWait.activeWaitUs);
    addUnsigned(telemetry.renderWait.coarseSleepCount);
    addUnsigned(telemetry.renderWait.coarseSleepRequestedUs);
    addUnsigned(telemetry.renderWait.coarseSleepRequestedWakeUs);
    addUnsigned(telemetry.renderWait.coarseSleepReturnUs);
    addBool(telemetry.renderWait.coarseSleepClockStalled);
    addBool(telemetry.renderWait.activeWaitEntered);
    addUnsigned(telemetry.renderWait.activeWaitStartUs);
    addUnsigned(telemetry.renderWait.activeWaitLimitUs);
    addUnsigned(telemetry.renderWait.activeWaitYieldCount);
    addBool(telemetry.renderWait.activeWaitClockStalled);
    addBool(telemetry.renderWait.activeWaitYieldLimitReached);
    addUnsigned(telemetry.preparationStartUs);
    addUnsigned(telemetry.preparationEndUs);
    addUnsigned(telemetry.preparationDurationUs);
    addUnsigned(decision.targetUs);
    addUnsigned(telemetry.targetWaitFinalUs);
    addUnsigned(telemetry.targetWaitOvershootUs);
    addUnsigned(telemetry.targetSchedulerDelayUs);
    addBool(telemetry.targetSchedulerDelayValid);
    addBool(telemetry.targetDeadlineAlreadyElapsed);
    addUnsigned(telemetry.targetWait.initialNowUs);
    addUnsigned(telemetry.targetWait.activeWaitUs);
    addUnsigned(telemetry.targetWait.coarseSleepCount);
    addUnsigned(telemetry.targetWait.coarseSleepRequestedUs);
    addUnsigned(telemetry.targetWait.coarseSleepRequestedWakeUs);
    addUnsigned(telemetry.targetWait.coarseSleepReturnUs);
    addBool(telemetry.targetWait.coarseSleepClockStalled);
    addBool(telemetry.targetWait.activeWaitEntered);
    addUnsigned(telemetry.targetWait.activeWaitStartUs);
    addUnsigned(telemetry.targetWait.activeWaitLimitUs);
    addUnsigned(telemetry.targetWait.activeWaitYieldCount);
    addBool(telemetry.targetWait.activeWaitClockStalled);
    addBool(telemetry.targetWait.activeWaitYieldLimitReached);
    addUnsigned(telemetry.presentStartUs);
    addUnsigned(telemetry.submissionBoundaryUs);
    addBool(feedback.submissionTimeValid);
    addUnsigned(feedback.submissionTimeUs);
    addBool(telemetry.usedPresenterSubmissionTime);
    addUnsigned(telemetry.presentEndUs);
    addUnsigned(telemetry.presentDurationUs);
    addSigned(telemetry.submitErrorUs);
    addUnsigned(telemetry.presentSpacingUs);
    addSigned(telemetry.spacingMarginUs);
    addUnsigned(telemetry.spacingDeficitUs);
    addUnsigned(telemetry.spacingGuardFeedbackUs);
    addBool(telemetry.spacingCorrected);
    addBool(telemetry.hadPriorSubmission);
    addText(tearClassification(row));
    addBool(feedback.presented && !decision.latchedPresentation &&
            telemetry.hadPriorSubmission && telemetry.spacingMarginUs < 0);
    addUnsigned(row.completionQueueDepth);
    addText(traceDispositionName(row.disposition));
    addBool(row.disposition != TraceDisposition::Presented);
    addBool(feedback.presented);
    addBool(feedback.cancelled);
    addBool(feedback.submissionIdValid);
    addUnsigned(feedback.submissionId);
    addBool(feedback.latchSampleValid);
    addUnsigned(feedback.latchSubmissionId);
    addUnsigned(feedback.latchTimeUs);
    addUnsigned(static_cast<uint64_t>(feedback.latchTimeKind));
    addUnsigned(feedback.latchPresentRefreshSequence);
    addUnsigned(feedback.latchRefreshSequence);
    addBool(decision.latchedPresentation);
    addBool(decision.usedRtpTimestamp);
    addBool(decision.cadenceEligible);
    addBool(decision.sourceRateChanged);
    addBool(decision.phaseDiscontinuity);
    addBool(decision.rebased);
    addBool(telemetry.externalRebaseApplied);
    addUnsigned(telemetry.externalRebaseFlags);
    addUnsigned(telemetry.midframeWindowStateFlags);
    addBool(m_DeepTraceEnabled);
    addBool(feedback.nativePresentTimingValid);
    addUnsigned(feedback.nativePresentStartUs);
    addUnsigned(feedback.nativePresentEndUs);
    addUnsigned(nativePresentDurationUs);
    addBool(feedback.presentCountBeforeValid);
    addUnsigned(feedback.presentCountBefore);
    addBool(feedback.frameStatsBeforeValid);
    addUnsigned(feedback.frameStatsBeforePresentCount);
    addUnsigned(feedback.frameStatsBeforeTimeUs);
    addUnsigned(feedback.frameStatsBeforePresentRefreshSequence);
    addUnsigned(feedback.frameStatsBeforeRefreshSequence);
    addBool(feedback.gpuReadyAttempted);
    addBool(feedback.gpuReadySignalResultValid);
    addSigned(feedback.gpuReadySignalResult);
    addBool(feedback.gpuReadySetEventResultValid);
    addSigned(feedback.gpuReadySetEventResult);
    addBool(feedback.gpuReadyWaitResultValid);
    addUnsigned(feedback.gpuReadyWaitResult);
    addBool(feedback.gpuReadyTimingValid);
    addUnsigned(feedback.gpuReadySignalStartUs);
    addUnsigned(feedback.gpuReadySignalEndUs);
    addUnsigned(feedback.gpuReadyFlushStartUs);
    addUnsigned(feedback.gpuReadyFlushEndUs);
    addUnsigned(feedback.gpuReadySetEventStartUs);
    addUnsigned(feedback.gpuReadySetEventEndUs);
    addUnsigned(feedback.gpuReadyPollStartUs);
    addUnsigned(feedback.gpuReadyPollEndUs);
    addUnsigned(feedback.gpuReadyFenceValue);
    addUnsigned(feedback.gpuReadyPollCompletedValue);
    addBool(feedback.gpuReadyCompletedBeforeWait);
    addUnsigned(gpuReadyCompletionLowerBoundUs);
    addUnsigned(gpuReadyCompletionUpperBoundUs);
    addUnsigned(gpuReadyCompletionUncertaintyUs);
    addUnsigned(feedback.gpuReadyWaitStartUs);
    addUnsigned(feedback.gpuReadyTimeUs);
    addUnsigned(gpuReadyWaitUs);
    addUnsigned(telemetry.decisionEndUs);
    addUnsigned(telemetry.decisionEndUs >= telemetry.decisionTimeUs ?
        telemetry.decisionEndUs - telemetry.decisionTimeUs : 0);
    addUnsigned(telemetry.staleCheckUs);
    addUnsigned(telemetry.staleAgeUs);
    addUnsigned(telemetry.renderWaitEntryUs);
    addUnsigned(telemetry.targetWaitEntryUs);
    addUnsigned(telemetry.spacingCheckUs);
    addUnsigned(telemetry.presentationFloorUs);
    addUnsigned(telemetry.spacingRecheckUs);
    addUnsigned(telemetry.spacingCorrectedFloorUs);
    addUnsigned(telemetry.correctionWaitStartUs);
    addUnsigned(telemetry.correctionWaitEndUs);
    addUnsigned(row.terminalTimeUs);
    addBool(feedback.nativeBackendValid);
    addUnsigned(static_cast<uint32_t>(feedback.nativeBackend));
    addBool(feedback.nativePresentResultValid);
    addSigned(feedback.nativePresentResult);
    addBool(feedback.nativePresentParametersValid);
    addUnsigned(feedback.nativePresentSyncInterval);
    addUnsigned(feedback.nativePresentFlags);
    addBool(feedback.nativeVrrStateValid);
    addBool(feedback.nativeTearingSupported);
    addBool(feedback.nativeBorderlessFlipModel);
    addBool(feedback.nativeSameGpuOutput);
    addBool(feedback.nativeRenderAdapterLuidValid);
    addUnsigned(feedback.nativeRenderAdapterLuid);
    addBool(feedback.nativeSwapChainAllowsTearing);
    addBool(feedback.nativeTearingFeatureQueryResultValid);
    addSigned(feedback.nativeTearingFeatureQueryResult);
    addBool(feedback.nativeTearingFeatureAllowsTearing);
    addBool(feedback.nativeSwapChainDescQueryResultValid);
    addSigned(feedback.nativeSwapChainDescQueryResult);
    addUnsigned(feedback.nativeSwapChainFlags);
    addUnsigned(feedback.nativeSwapChainSwapEffect);
    addBool(feedback.nativeFullscreenStateQueryResultValid);
    addSigned(feedback.nativeFullscreenStateQueryResult);
    addBool(feedback.nativeFullscreenExclusive);
    addUnsigned(feedback.nativeWindowFlags);
    addBool(feedback.nativePresentReadyAvailable);
    addBool(feedback.nativeForegroundWindow);
    addUnsigned(static_cast<uint32_t>(
        feedback.nativeVrrFallbackReason));
    addUnsigned(feedback.nativeDesktopMonitorCount);
    addBool(feedback.nativeVblankVirtualizationProbeComplete);
    addBool(feedback.nativeVblankVirtualizationCallAvailable);
    addBool(feedback.nativeVblankVirtualizationResultValid);
    addSigned(feedback.nativeVblankVirtualizationResult);
    addBool(feedback.nativeVblankVirtualizationDisabled);
    addBool(feedback.nativeDisplayConfigQueryResultValid);
    addUnsigned(feedback.nativeDisplayConfigQueryResult);
    addBool(feedback.nativeDisplayPathValid);
    addUnsigned(feedback.nativeDisplayPathFlags);
    addBool(feedback.nativeDisplayTargetAvailable);
    addUnsigned(feedback.nativeDisplaySourceAdapterLuid);
    addUnsigned(feedback.nativeDisplaySourceId);
    addUnsigned(feedback.nativeDisplayTargetAdapterLuid);
    addUnsigned(feedback.nativeDisplayTargetId);
    addUnsigned(feedback.nativeDisplayOutputTechnology);
    addUnsigned(feedback.nativeDisplayRotation);
    addUnsigned(feedback.nativeDisplayScaling);
    addUnsigned(feedback.nativeDisplayPathRefreshNumerator);
    addUnsigned(feedback.nativeDisplayPathRefreshDenominator);
    addBool(feedback.nativeDisplaySignalValid);
    addUnsigned(feedback.nativeDisplaySignalPixelRateHz);
    addUnsigned(feedback.nativeDisplaySignalHSyncNumerator);
    addUnsigned(feedback.nativeDisplaySignalHSyncDenominator);
    addUnsigned(feedback.nativeDisplaySignalVSyncNumerator);
    addUnsigned(feedback.nativeDisplaySignalVSyncDenominator);
    addUnsigned(feedback.nativeDisplaySignalActiveWidth);
    addUnsigned(feedback.nativeDisplaySignalActiveHeight);
    addUnsigned(feedback.nativeDisplaySignalTotalWidth);
    addUnsigned(feedback.nativeDisplaySignalTotalHeight);
    addUnsigned(feedback.nativeDisplaySignalAdditionalInfoRaw);
    addUnsigned(feedback.nativeDisplaySignalScanLineOrdering);
    addBool(feedback.nativeRasterSamplingRequested);
    addBool(feedback.nativeRasterOpenResultValid);
    addSigned(feedback.nativeRasterOpenResult);
    addBool(feedback.nativeRasterSourceValid);
    addUnsigned(feedback.nativeRasterVidPnSourceId);
    addBool(feedback.nativeRasterBeforePresent.queryResultValid);
    addSigned(feedback.nativeRasterBeforePresent.queryResult);
    addUnsigned(feedback.nativeRasterBeforePresent.queryStartUs);
    addUnsigned(feedback.nativeRasterBeforePresent.queryEndUs);
    addBool(feedback.nativeRasterBeforePresent.inVerticalBlank);
    addUnsigned(feedback.nativeRasterBeforePresent.scanLine);
    addBool(feedback.nativeRasterAfterPresent.queryResultValid);
    addSigned(feedback.nativeRasterAfterPresent.queryResult);
    addUnsigned(feedback.nativeRasterAfterPresent.queryStartUs);
    addUnsigned(feedback.nativeRasterAfterPresent.queryEndUs);
    addBool(feedback.nativeRasterAfterPresent.inVerticalBlank);
    addUnsigned(feedback.nativeRasterAfterPresent.scanLine);
    addBool(feedback.submissionIdQueryResultValid);
    addSigned(feedback.submissionIdQueryResult);
    addUnsigned(feedback.submissionIdQueryStartUs);
    addUnsigned(feedback.submissionIdQueryEndUs);
    addBool(feedback.frameStatsQueryResultValid);
    addSigned(feedback.frameStatsQueryResult);
    addUnsigned(feedback.frameStatsQueryStartUs);
    addUnsigned(feedback.frameStatsQueryEndUs);
    addBool(feedback.latchRawSyncQpcValid);
    addUnsigned(feedback.latchRawSyncQpcTicks);
    addUnsigned(feedback.latchRawSyncQpcFrequency);
    addBool(feedback.latchQpcCorrelationValid);
    addUnsigned(feedback.latchQpcCorrelationReferenceTicks);
    addUnsigned(feedback.latchQpcCorrelationReferenceTimeUs);
    addUnsigned(feedback.latchQpcCorrelationSpanTicks);
    addSigned(diagnostics.readinessPhaseUs);
    addUnsigned(diagnostics.readinessDemandUs);
    addUnsigned(diagnostics.appliedReadinessReserveUs);
    addUnsigned(diagnostics.renderBaselineUs);
    addUnsigned(diagnostics.renderInsuranceUs);
    // This field is the completed present-ready wait for this row. The
    // learned lead used by the next decision is recorded separately as
    // gpu_readiness_lead_us above.
    addUnsigned(gpuReadyWaitUs);
    addUnsigned(diagnostics.pacingLatencyBudgetUs);
    addUnsigned(diagnostics.cadenceSamples);
    addUnsigned(diagnostics.rateCandidateSamples);
    addUnsigned(diagnostics.readinessSamples);
    addUnsigned(diagnostics.preparationSamples);
    addUnsigned(diagnostics.renderSchedulerSamples);
    addUnsigned(diagnostics.targetSchedulerSamples);
    addUnsigned(diagnostics.cleanSpacingFrames);
    addUnsigned(diagnostics.phaseErrorFrames);
    addBool(diagnostics.readinessModelValid);
    addUnsigned(decision.playoutDelayUs);
    addSigned(decision.cadenceSmoothingUs);
    addUnsigned(decision.missedTicks);
    addUnsigned(telemetry.decodeSyncWaitUs);
    addBool(telemetry.prepareTimingValid);
    addUnsigned(telemetry.prepareDecodeSyncUs);
    addUnsigned(telemetry.prepareAcquireUs);
    addUnsigned(telemetry.prepareRenderUs);
    addUnsigned(telemetry.prepareFlushUs);
    // Reserved historical gap-fill columns preserve trace compatibility.
    addUnsigned(0);
    addUnsigned(0);
    addUnsigned(decision.originalTargetUs);
    separator();
    line.append(m_InitialPlayoutProfile);
    addUnsigned(decision.originalScanoutUs);
    addUnsigned(decision.predictedScanoutUs);
    addUnsigned(decision.compositorLeadUs);
    addUnsigned(decision.recoveryHeadroomUs);
    addUnsigned(decision.smoothnessProtectionUs);
    addUnsigned(decision.requestedPlayoutDelayUs);
    addUnsigned(decision.submissionSmoothnessSamples);
    addUnsigned(decision.submissionSmoothnessMisses);
    addUnsigned(decision.nativeSmoothnessSamples);
    addUnsigned(decision.nativeSmoothnessMisses);
    addUnsigned(decision.playoutCapacityLimited);
    addUnsigned(feedback.presentationUncertaintyUs);
#define VRR_ADD_TRACE_PARAMETER(type, jsonName, memberName, defaultValue) \
    addUnsigned(static_cast<uint64_t>(parameters.memberName));
    VRR_TIMING_PARAMETER_FIELDS(VRR_ADD_TRACE_PARAMETER)
#undef VRR_ADD_TRACE_PARAMETER
    addUnsigned(row.decoderOutputUs);
    addSigned(row.latencyMode);
    addUnsigned(m_Config.readinessHitchFeedback);
    addUnsigned(m_CalibrationLoaded);
    addUnsigned(m_InitialCachedSamples);
    addSigned(m_HistoryVersion);
    addUnsigned(row.decisionValid);
    addUnsigned(row.historySamples);
    addUnsigned(row.historyMisses);
    addUnsigned(row.historyDurationUs);
    addUnsigned(row.historyCanRelease);
    addUnsigned(0); // Retired oscillation diagnostic column.
    addUnsigned(0);
    addUnsigned(m_Config.allowTearing);
    line.append('\n');

    if (m_TraceFormat == TraceFormat::ChunkedCompressed) {
        m_TraceDecodedHash.addData(line);
        m_TraceChunk.append(line);
        if (m_TraceChunk.size() >= kTraceChunkBytes) {
            flushTraceChunk();
        }
        return;
    }

    const size_t bytesWritten = std::fwrite(
        line.constData(), 1, static_cast<size_t>(line.size()), m_TraceFile);
    m_TraceBytesWritten += bytesWritten;
    if (bytesWritten != static_cast<size_t>(line.size())) {
        m_TraceWriteFailed = true;
        m_TraceAcceptingRows.store(false);
    }
    else {
        m_TraceDecodedHash.addData(line);
        if (m_TraceBytesWritten >= kMaximumTraceBytes &&
                minimumTraceDurationCaptured()) {
            m_TraceSizeCapped = true;
            m_TraceAcceptingRows.store(false);
        }
    }
}

void VrrPacingWorker::flushTraceChunk(bool enforceSizeCap)
{
    if (m_TraceChunk.isEmpty() || m_TraceFile == nullptr) {
        return;
    }

    const QByteArray compressed = qCompress(m_TraceChunk, 6);
    const uint32_t compressedBytes = static_cast<uint32_t>(compressed.size());
    const uint64_t recordBytes = sizeof(compressedBytes) + compressedBytes;

    const unsigned char lengthBytes[4] = {
        static_cast<unsigned char>(compressedBytes & 0xff),
        static_cast<unsigned char>((compressedBytes >> 8) & 0xff),
        static_cast<unsigned char>((compressedBytes >> 16) & 0xff),
        static_cast<unsigned char>((compressedBytes >> 24) & 0xff),
    };
    const size_t lengthWritten = std::fwrite(
        lengthBytes, 1, sizeof(lengthBytes), m_TraceFile);
    const size_t payloadWritten = std::fwrite(
        compressed.constData(), 1, static_cast<size_t>(compressed.size()),
        m_TraceFile);
    if (lengthWritten != sizeof(lengthBytes) ||
        payloadWritten != static_cast<size_t>(compressed.size())) {
        m_TraceWriteFailed = true;
        m_TraceAcceptingRows.store(false);
    }
    else {
        m_TraceBytesWritten += recordBytes;
        // A completed chunk is independently recoverable after a crash. This
        // is a low-frequency write performed only by the background thread.
        if (std::fflush(m_TraceFile) != 0) {
            m_TraceWriteFailed = true;
            m_TraceAcceptingRows.store(false);
        }
        else if (enforceSizeCap &&
                 m_TraceBytesWritten >= kMaximumTraceBytes &&
                 minimumTraceDurationCaptured()) {
            m_TraceSizeCapped = true;
            m_TraceAcceptingRows.store(false);
        }
    }
    m_TraceChunk.clear();
}

bool VrrPacingWorker::minimumTraceDurationCaptured() const
{
    return m_TraceLatestArrivalUs >= m_TraceStartUs &&
        m_TraceLatestArrivalUs - m_TraceStartUs >= kMinimumTraceDurationUs;
}

const char* VrrPacingWorker::traceDispositionName(
    TraceDisposition disposition)
{
    switch (disposition) {
    case TraceDisposition::Presented:
        return "presented";
    case TraceDisposition::OutputDropped:
        return "output_dropped";
    case TraceDisposition::QueueCapacity:
        return "queue_capacity";
    case TraceDisposition::ArrivalRejected:
        return "arrival_rejected";
    case TraceDisposition::SuspensionDiscard:
        return "suspension_discard";
    case TraceDisposition::ShutdownDiscard:
        return "shutdown_discard";
    case TraceDisposition::Interrupted:
        return "interrupted";
    case TraceDisposition::Stale:
        return "stale";
    case TraceDisposition::PreparationFailed:
        return "preparation_failed";
    }
    return "unknown";
}

const char* VrrPacingWorker::tearClassification(const TraceRow& row) const
{
    if (!row.feedback.presented) {
        return "not_presented";
    }
    if (row.decision.latchedPresentation && m_CanLatchPresentation) {
        return "confirmed_safe_latched";
    }
    if (!row.telemetry.hadPriorSubmission) {
        return "first_submission_unknown";
    }
    if (row.telemetry.spacingMarginUs < 0) {
        return "adaptive_interval_violation";
    }
    // This proves that the client respected the panel-period floor. It is not
    // a literal hardware tear observation: a nonfunctional VRR path or an
    // unreported scanout transition can still require external validation.
    return "adaptive_interval_safe";
}

void VrrPacingWorker::openTraceIfRequested()
{
    const char* tracePath = SDL_getenv("MOONLIGHT_VRR_TRACE");
    if (tracePath == nullptr || tracePath[0] == '\0') {
        return;
    }

#ifdef _WIN32
    // A buffered stdio stream still flushes synchronously when its buffer
    // fills. Keep diagnostic I/O off the time-critical worker's network path.
    if (isUncPath(tracePath)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MOONLIGHT_VRR_TRACE must use a local path; refusing network trace: %s",
                    tracePath);
        return;
    }

#endif

    // The launcher owns one path for the application lifetime, while each
    // reconnect creates a new worker. Preserve the completed connection before
    // reusing that path so launchers and their latest-trace links still name
    // the current capture. A failed archive must never fall through to truncate.
    QFile previousTrace(QString::fromLocal8Bit(tracePath));
    if (previousTrace.exists()) {
        const QFileInfo traceInfo(previousTrace);
        const QString extension = traceInfo.suffix().isEmpty() ? QString() :
            QStringLiteral(".") + traceInfo.suffix();
        QString archivePath;
        for (unsigned connection = 1; ; ++connection) {
            archivePath = traceInfo.absoluteDir().filePath(
                traceInfo.completeBaseName() +
                QStringLiteral("-connection-%1").arg(connection) + extension);
            if (!QFileInfo::exists(archivePath)) break;
        }
        if (!previousTrace.rename(archivePath)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to preserve previous VRR trace; tracing disabled: %s",
                        tracePath);
            return;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "VRR trace: preserved earlier connection at %s",
                    QFile::encodeName(archivePath).constData());
    }

#ifdef _WIN32
    // Use the checked CRT variant on Windows so enabling diagnostics does not
    // introduce a deprecation warning in the normal application build.
    if (fopen_s(&m_TraceFile, tracePath, "wb") != 0) {
        m_TraceFile = nullptr;
    }
#else
    m_TraceFile = std::fopen(tracePath, "wb");
#endif
    if (m_TraceFile == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to open MOONLIGHT_VRR_TRACE file: %s", tracePath);
        return;
    }

    // A .csv suffix explicitly requests the directly readable compatibility
    // format. The recommended .vrrtrace format compresses independent chunks
    // and typically reduces a full session by an order of magnitude.
    m_TraceFormat = QByteArray(tracePath).toLower().endsWith(".csv") ?
        TraceFormat::Csv : TraceFormat::ChunkedCompressed;

    // Amortize local diagnostic writes instead of flushing on every frame.
    // fclose() commits the CSV tail; compressed chunks flush independently.
    std::setvbuf(m_TraceFile, nullptr, _IOFBF, 1024 * 1024);

    m_TraceBytesWritten = 0;
    m_TraceChunk.clear();
    m_TraceQueue = std::make_unique<Vrr13::TraceQueue<TraceRow, 8192>>();
    m_TraceDecodedHash.reset();
    if (m_TraceFormat == TraceFormat::ChunkedCompressed) {
        const size_t magicBytes = sizeof(kTraceMagic) - 1;
        if (std::fwrite(kTraceMagic, 1, magicBytes, m_TraceFile) != magicBytes) {
            std::fclose(m_TraceFile);
            m_TraceFile = nullptr;
            return;
        }
        m_TraceBytesWritten = magicBytes;
        m_TraceChunk.append(kTraceHeader);
    }
    else {
        const size_t headerBytes = sizeof(kTraceHeader) - 1;
        if (std::fwrite(kTraceHeader, 1, headerBytes, m_TraceFile) !=
                headerBytes) {
            std::fclose(m_TraceFile);
            m_TraceFile = nullptr;
            return;
        }
        m_TraceBytesWritten = headerBytes;
    }
    m_TraceDecodedHash.addData(
        kTraceHeader, sizeof(kTraceHeader) - 1);

    // All formatting and I/O happen on this thread; the pacing worker only
    // enqueues row copies. Without it, a buffered flush would periodically
    // stall the TIME_CRITICAL thread and perturb the timing being measured.
    m_TraceStopping.store(false);
    m_TraceSizeCapped = false;
    m_TraceWriteFailed = false;
    m_TraceDroppedRows.store(0, std::memory_order_relaxed);
    m_TraceRowsEnqueued.store(0, std::memory_order_relaxed);
    m_TraceArrivalSequence.store(0);
    m_TraceStartUs = LiGetMicroseconds();
    m_TraceLatestArrivalUs = m_TraceStartUs;
    m_TraceThread = SDL_CreateThread(VrrPacingWorker::traceThreadProc,
                                     "VrrTrace", this);
    if (m_TraceThread == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Disabling VRR trace: writer thread failed: %s",
                    SDL_GetError());
        std::fclose(m_TraceFile);
        m_TraceFile = nullptr;
        m_TraceChunk.clear();
        return;
    }
    m_TraceAcceptingRows.store(true);
}

void VrrPacingWorker::closeTrace()
{
    // Close admission before asking the writer to drain. Otherwise a producer
    // could enqueue after the writer observes an empty stopping queue, leaving
    // a normal shutdown with an unwritten row behind the footer.
    if (m_TraceThread != nullptr) {
        m_TraceAcceptingRows.store(false);
        m_TraceStopping.store(true);
        SDL_WaitThread(m_TraceThread, nullptr);
        m_TraceThread = nullptr;
    }
    else {
        m_TraceAcceptingRows.store(false);
    }

    const size_t droppedRows =
        m_TraceDroppedRows.exchange(0, std::memory_order_relaxed);
    const uint64_t rowsEnqueued =
        m_TraceRowsEnqueued.exchange(0, std::memory_order_relaxed);
    const uint64_t arrivalsAllocated =
        m_TraceArrivalSequence.load(std::memory_order_relaxed);
    if (m_TraceFile != nullptr) {
        if (m_TraceFormat == TraceFormat::Csv &&
                std::fflush(m_TraceFile) != 0) {
            m_TraceWriteFailed = true;
        }
        const QByteArray footer =
            QByteArrayLiteral(
                "#vrr_trace_footer,format_version=2,clean_shutdown=1,"
                "arrival_sequence_allocated=") +
            QByteArray::number(arrivalsAllocated) +
            QByteArrayLiteral(",rows_enqueued=") +
            QByteArray::number(rowsEnqueued) +
            QByteArrayLiteral(",rows_dropped=") +
            QByteArray::number(static_cast<qulonglong>(droppedRows)) +
            QByteArrayLiteral(",size_capped=") +
            QByteArray::number(m_TraceSizeCapped ? 1 : 0) +
            QByteArrayLiteral(",write_failed=") +
            QByteArray::number(m_TraceWriteFailed ? 1 : 0) +
            QByteArrayLiteral(",decoded_sha256=") +
            m_TraceDecodedHash.result().toHex() +
            QByteArrayLiteral("\n");
        if (m_TraceFormat == TraceFormat::ChunkedCompressed) {
            m_TraceChunk.append(footer);
            // The footer is metadata, not a captured row. Crossing the size
            // threshold by these few bytes did not truncate the capture.
            flushTraceChunk(false);
        }
        else {
            const size_t footerBytes = static_cast<size_t>(footer.size());
            if (std::fwrite(
                    footer.constData(), 1, footerBytes, m_TraceFile) !=
                    footerBytes ||
                    std::fflush(m_TraceFile) != 0) {
                m_TraceWriteFailed = true;
            }
        }
    }
    if (droppedRows != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VRR trace dropped %zu rows to protect pacing",
                    droppedRows);
    }

    if (m_TraceSizeCapped) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VRR trace was capped at 512 MiB after preserving at least one hour");
        m_TraceSizeCapped = false;
    }
    if (m_TraceWriteFailed) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VRR trace reported a write failure");
    }

    if (m_TraceFile != nullptr) {
        if (std::fclose(m_TraceFile) != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "VRR trace close reported a write failure");
        }
        m_TraceFile = nullptr;
    }
    m_TraceChunk.clear();
    m_TraceWriteFailed = false;
}
