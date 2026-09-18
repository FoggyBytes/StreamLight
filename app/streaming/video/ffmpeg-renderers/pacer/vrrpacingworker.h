// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include "../../decoder.h"
#include "../ivrrframepresenter.h"
#include "pacertelemetry.h"
#include "vrr/vrrtargetwaiter.h"
#include "vrr/vrrtypes.h"
#include "vrr/vrrtimingcontroller.h"
#include "vrr/tracequeue.h"

#include <atomic>
#include <cstdio>
#include <deque>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QCryptographicHash>
#include <QMutex>
#include <QWaitCondition>

class VrrTimingController;

// The complete greenfield VRR execution path lives in this one worker.  It
// owns a bounded queue and the renderer context from preparation through
// presentation; fixed-VSync and unpaced Pacer behavior never enter it.
class VrrPacingWorker {
public:
    VrrPacingWorker(IVrrFramePresenter* presenter,
                    const VrrSessionConfig& config,
                    PacerTelemetry* telemetry);
    ~VrrPacingWorker();

    VrrPacingWorker(const VrrPacingWorker&) = delete;
    VrrPacingWorker& operator=(const VrrPacingWorker&) = delete;

    bool start();

    void submit(PacedFrame&& frame);

    // Calls from the UI/main thread only manipulate worker-owned state. All
    // backend notifications are delivered by the worker itself.
    void notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info);

private:
    enum class TraceDisposition : uint8_t {
        Presented,
        OutputDropped,
        QueueCapacity,
        ArrivalRejected,
        SuspensionDiscard,
        ShutdownDiscard,
        Interrupted,
        Stale,
        PreparationFailed,
    };

    struct FrameTraceContext {
        uint64_t arrivalSequence = 0;
        uint64_t arrivalUs = 0;
        uint64_t dequeueUs = 0;
        size_t queueDepthBefore = 0;
        size_t queueDepthAfter = 0;
        bool queueAccepted = false;
        bool queueDiscontinuity = false;
    };

    struct QueuedFrame {
        PacedFrame frame;
        FrameTraceContext trace;

        explicit operator bool() const
        {
            return static_cast<bool>(frame);
        }
    };

    struct FrameTelemetry {
        uint64_t decisionTimeUs = 0;
        uint64_t decisionEndUs = 0;
        bool externalRebaseApplied = false;
        uint32_t externalRebaseFlags = 0;
        uint32_t midframeWindowStateFlags = 0;
        uint64_t staleCheckUs = 0;
        uint64_t staleAgeUs = 0;
        uint64_t renderWaitEntryUs = 0;
        uint64_t renderWaitFinalUs = 0;
        uint64_t renderWaitOvershootUs = 0;
        VrrTargetWaitResult renderWait;
        uint64_t renderSchedulerDelayUs = 0;
        bool renderSchedulerDelayValid = false;
        bool renderDeadlineAlreadyElapsed = false;
        uint64_t preparationStartUs = 0;
        uint64_t preparationEndUs = 0;
        uint64_t preparationDurationUs = 0;
        uint64_t decodeSyncWaitUs = 0;
        bool prepareTimingValid = false;
        uint64_t prepareDecodeSyncUs = 0;
        uint64_t prepareAcquireUs = 0;
        uint64_t prepareRenderUs = 0;
        uint64_t prepareFlushUs = 0;
        uint64_t targetWaitEntryUs = 0;
        uint64_t targetWaitOvershootUs = 0;
        uint64_t targetWaitFinalUs = 0;
        uint64_t targetSchedulerDelayUs = 0;
        VrrTargetWaitResult targetWait;
        uint64_t presentStartUs = 0;
        uint64_t submissionBoundaryUs = 0;
        uint64_t presentEndUs = 0;
        uint64_t presentDurationUs = 0;
        uint64_t presentSpacingUs = 0;
        int64_t submitErrorUs = 0;
        int64_t spacingMarginUs = 0;
        uint64_t spacingDeficitUs = 0;
        uint64_t spacingGuardFeedbackUs = 0;
        uint64_t spacingCheckUs = 0;
        uint64_t presentationFloorUs = 0;
        uint64_t spacingRecheckUs = 0;
        uint64_t spacingCorrectedFloorUs = 0;
        uint64_t correctionWaitStartUs = 0;
        uint64_t correctionWaitEndUs = 0;
        bool targetSchedulerDelayValid = false;
        bool targetDeadlineAlreadyElapsed = false;
        bool hadPriorSubmission = false;
        bool usedPresenterSubmissionTime = false;
        bool spacingCorrected = false;
    };

    // Diagnostics must not perturb the measurement. The pacing thread only
    // copies a row into a preallocated bounded queue through a non-blocking
    // handoff; a separate writer thread owns all formatting and file I/O.
    struct TraceRow {
        int frameNumber = 0;
        uint32_t rtpTimestamp = 0;
        bool timestampValid = false;
        uint64_t decodeCompleteUs = 0;
        uint64_t decoderOutputUs = 0;
        int latencyMode = 0;
        uint64_t historySamples = 0;
        uint64_t historyMisses = 0;
        uint64_t historyDurationUs = 0;
        bool historyCanRelease = false;
        uint64_t receiveUs = 0;
        uint64_t reassembledUs = 0;
        uint64_t decodeSubmitUs = 0;
        FrameTraceContext input;
        VrrTimingDecision decision;
        VrrTimingDiagnostics diagnostics;
        VrrPresentFeedback feedback;
        FrameTelemetry telemetry;
        size_t completionQueueDepth = 0;
        TraceDisposition disposition = TraceDisposition::OutputDropped;
        bool decisionValid = false;
        uint64_t terminalTimeUs = 0;
    };

    enum class TraceFormat : uint8_t {
        Csv,
        ChunkedCompressed,
    };

    static int threadProc(void* context);
    static int traceThreadProc(void* context);

    int run();
    bool dequeueFrame(QueuedFrame& frame, bool& queueDiscontinuity);
    bool hasQueuedFrame();
    void discardQueuedFrames(bool countDrops,
                             TraceDisposition disposition);
    uint32_t consumeWindowStateNotifications();
    bool presentationSuspended() const;
    bool isStopping() const;
    void waitForSubmissionFloor(const VrrTimingDecision& decision,
                                FrameTelemetry& telemetry);
    void recordSubmission(const VrrTimingDecision& decision,
                          const VrrPresentFeedback& feedback,
                          uint64_t operationStartUs,
                          uint64_t operationEndUs,
                          FrameTelemetry& telemetry);
    void deferFrame(PacedFrame&& frame);
    void noteDrop();
    void recordFrameCompletion(const QueuedFrame& frame,
                    const VrrTimingDecision& decision,
                    const VrrPresentFeedback& feedback,
                    const FrameTelemetry& telemetry,
                    TraceDisposition disposition,
                    bool decisionValid = true);
    void openTraceIfRequested();
    void closeTrace();
    int traceRun();
    void writeTraceRow(const TraceRow& row);
    void flushTraceChunk(bool enforceSizeCap = true);
    bool minimumTraceDurationCaptured() const;
    static const char* traceDispositionName(TraceDisposition disposition);
    const char* tearClassification(const TraceRow& row) const;

    IVrrFramePresenter* m_Presenter;
    PacerTelemetry* m_Telemetry;
    VrrSessionConfig m_Config;
    bool m_CanLatchPresentation = false;
    bool m_WorkerStarted = false;
    std::atomic_bool m_CalibrationInvalidated { false };
    QByteArray m_InitialPlayoutProfile;
    bool m_CalibrationLoaded = false;
    uint64_t m_InitialCachedSamples = 0;
    int m_HistoryVersion = 0;

    std::unique_ptr<VrrTimingController> m_TimingController;
    std::unique_ptr<VrrTargetWaiter> m_TargetWaiter;

    QMutex m_FrameQueueLock;
    QWaitCondition m_FrameQueueNotEmpty;
    std::deque<QueuedFrame> m_FrameQueue;
    std::atomic_size_t m_FrameQueueDepth { 0 };
    PacedFrame m_DeferredFrame;
    // The last presented image, re-presented inside a host gap.
    SDL_Thread* m_WorkerThread = nullptr;
    std::atomic_bool m_Stopping { false };
    std::atomic_bool m_Suspended { false };
    // Capacity eviction marks a local discontinuity while retaining source
    // timestamps so the cadence model can advance across omitted frames.
    std::atomic_bool m_QueueDiscontinuity { false };
    std::atomic_uint32_t m_PendingWindowStateFlags { 0 };
    bool m_PresenterSuspended = false;
    bool m_RebaseOnNextFrame = false;
    uint32_t m_RebaseOnNextFrameFlags = 0;
    bool m_DeepTraceEnabled = false;
    std::FILE* m_TraceFile = nullptr;
    SDL_Thread* m_TraceThread = nullptr;
    std::unique_ptr<Vrr13::TraceQueue<TraceRow, 8192>> m_TraceQueue;
    std::atomic_bool m_TraceStopping { false };
    std::atomic_uint m_TraceProducersActive { 0 };
    std::atomic_bool m_TraceAcceptingRows { false };
    std::atomic_uint64_t m_TraceArrivalSequence { 0 };
    std::atomic_size_t m_TraceDroppedRows { 0 };
    std::atomic_uint64_t m_TraceRowsEnqueued { 0 };
    uint64_t m_TraceBytesWritten = 0;
    uint64_t m_TraceStartUs = 0;
    uint64_t m_TraceLatestArrivalUs = 0;
    bool m_TraceSizeCapped = false;
    bool m_TraceWriteFailed = false;
    TraceFormat m_TraceFormat = TraceFormat::Csv;
    QByteArray m_TraceChunk;
    QCryptographicHash m_TraceDecodedHash {
        QCryptographicHash::Sha256
    };
};
