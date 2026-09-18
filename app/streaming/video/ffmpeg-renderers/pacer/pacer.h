#pragma once

#include "../../decoder.h"
#include "../renderer.h"
#include "pacertelemetry.h"
#include "vrr/vrrtypes.h"

#include <QQueue>
#include <QMutex>
#include <QWaitCondition>

#include <memory>

class VrrPacingWorker;

// The maximum number of frames pacer will ever hold is:
// - 3 frames in the pacing queue
// - 1 frame removed from the render queue in the process of rendering
// - 1 frame for deferred free
#define PACER_MAX_OUTSTANDING_FRAMES (3 + 1 + 1)

class IVsyncSource {
public:
    virtual ~IVsyncSource() {}
    virtual bool initialize(SDL_Window* window, int displayFps) = 0;

    // Asynchronous sources produce callbacks on their own, while synchronous
    // sources require calls to waitForVsync().
    virtual bool isAsync() = 0;

    virtual void waitForVsync() {
        // Synchronous sources must implement waitForVsync()!
        SDL_assert(false);
    }
};

class Pacer
{
public:
    Pacer(IFFmpegRenderer* renderer);

    ~Pacer();

    // Stop all producer threads before a final telemetry snapshot is merged
    // by the decoder. It is safe to call this more than once.
    void shutdown();

    PacerTelemetrySnapshot telemetrySnapshot() const;

    // Shown by the VRR overlay line (6.0.0). nullptr when VRR was not requested or runs.
    const char* vrrFallbackReason() const { return m_VrrFallbackReason; }

    // Only the active VRR worker consumes the decoder-facing pacing metadata.
    void submitFrame(PacedFrame&& frame);

    void submitFrame(AVFrame* frame);

    bool isVrrActive() const;

    bool initialize(SDL_Window* window, int maxVideoFps,
                    bool enablePacing, bool enableVsync,
                    bool enableVrr, int vrrDisplayRefreshHz,
                    bool smoothVrrFrameTiming = true,
                    const QString& calibrationKey = QString(),
                    int vrrLatencyMode = 0);

    void notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info);

    void signalVsync();

    void renderOnMainThread();

private:
    static int vsyncThread(void* context);

    static int renderThread(void* context);

    void handleVsync(int timeUntilNextVsyncMillis);

    void enqueueFrameForRenderingAndUnlock(AVFrame* frame);

    void renderFrame(AVFrame* frame);

    void dropFrameForEnqueue(QQueue<AVFrame*>& queue);

    QQueue<AVFrame*> m_RenderQueue;
    QQueue<AVFrame*> m_PacingQueue;
    QQueue<int> m_PacingQueueHistory;
    QQueue<int> m_RenderQueueHistory;
    QMutex m_FrameQueueLock;
    QWaitCondition m_RenderQueueNotEmpty;
    QWaitCondition m_PacingQueueNotEmpty;
    QWaitCondition m_VsyncSignalled;
    SDL_Thread* m_RenderThread;
    SDL_Thread* m_VsyncThread;
    AVFrame* m_DeferredFreeFrame;
    bool m_Stopping;
    bool m_Shutdown;

    IVsyncSource* m_VsyncSource;
    IFFmpegRenderer* m_VsyncRenderer;
    int m_MaxVideoFps;
    int m_DisplayFps;
    int m_RendererAttributes;
    PacerTelemetry m_Telemetry;
    std::unique_ptr<VrrPacingWorker> m_VrrWorker;
    // Why a requested VRR fell back to fixed pacing, as vrrFallbackReasonName() spells it —
    // a string literal, set in initialize() before any reader exists. nullptr otherwise.
    const char* m_VrrFallbackReason = nullptr;
};
