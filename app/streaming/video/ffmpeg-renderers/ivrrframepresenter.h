// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <cstdint>
#include "pacer/vrr/presentationtiming.h"

struct AVFrame;

// Renderer-facing VRR contract. Pacing timestamps and sender metadata never
// cross this boundary: the presenter only prepares, adaptively presents, or
// abandons a decoded image.
enum class VrrFallbackReason : uint8_t {
    NoFallback,
    IneffectiveVsync,
    InvalidRefresh,
    UnsupportedRenderer,
    MainThreadRenderer,
    WindowsVulkan,
    AdaptivePresentationUnavailable,
    InsufficientHeadroom,
    InitializationFailed,
};

enum class VrrNativePresentationBackend : uint8_t {
    Unknown,
    Dxgi,
    Vulkan,
    Composition,
};

// Observation-only D3DKMT raster sample bracketed on the Moonlight clock.
// queryResult is the exact signed NTSTATUS. A zero result makes the blanking
// and scan-line payload valid; no field implies that a Present took effect at
// this raster position.
struct VrrNativeRasterSample {
    bool queryResultValid = false;
    int64_t queryResult = 0;
    uint64_t queryStartUs = 0;
    uint64_t queryEndUs = 0;
    bool inVerticalBlank = false;
    uint32_t scanLine = 0;
};

inline const char* vrrFallbackReasonName(VrrFallbackReason reason)
{
    switch (reason) {
    case VrrFallbackReason::NoFallback:
        return "none";
    case VrrFallbackReason::IneffectiveVsync:
        return "ineffective V-sync";
    case VrrFallbackReason::InvalidRefresh:
        return "invalid display refresh";
    case VrrFallbackReason::UnsupportedRenderer:
        return "unsupported renderer";
    case VrrFallbackReason::MainThreadRenderer:
        return "main-thread renderer";
    case VrrFallbackReason::WindowsVulkan:
        return "Windows Vulkan is not supported";
    case VrrFallbackReason::AdaptivePresentationUnavailable:
        return "adaptive presentation is unavailable";
    case VrrFallbackReason::InsufficientHeadroom:
        return "stream rate leaves insufficient adaptive-refresh headroom";
    case VrrFallbackReason::InitializationFailed:
        return "VRR initialization failed";
    }

    return "unknown fallback";
}

// presented means the native API accepted a display transition. cancelled
// describes why the operation ended, not whether the platform submitted: some
// APIs must submit an acquired image even while abandoning it.
struct VrrPresentFeedback {
    bool presented = false;
    bool cancelled = false;
    bool submissionTimeValid = false;
    uint64_t submissionTimeUs = 0;
    // Backend and signed native result for the actual presentation call.
    // Both validity flags are set whenever that call was attempted, including
    // cancellation and failure paths. Vulkan's libplacebo wrapper exposes
    // only a boolean result, represented as 0 for success and -1 for failure.
    bool nativeBackendValid = false;
    VrrNativePresentationBackend nativeBackend =
        VrrNativePresentationBackend::Unknown;
    bool nativePresentResultValid = false;
    int64_t nativePresentResult = 0;
    // Exact native Present parameters and the renderer eligibility snapshot
    // used for that call. They distinguish an intended immediate/tearing
    // transition from a synchronized one without inferring flags later.
    bool nativePresentParametersValid = false;
    uint32_t nativePresentSyncInterval = 0;
    uint32_t nativePresentFlags = 0;
    bool nativeVrrStateValid = false;
    bool nativeTearingSupported = false;
    bool nativeBorderlessFlipModel = false;
    bool nativeSameGpuOutput = false;
    // Preserve the actual render-device adapter identity so replay can
    // independently compare it with the DisplayConfig source adapter instead
    // of trusting the renderer's index-derived same-GPU boolean.
    bool nativeRenderAdapterLuidValid = false;
    uint64_t nativeRenderAdapterLuid = 0;
    bool nativeSwapChainAllowsTearing = false;
    // Raw DXGI/window capability evidence used to derive the booleans above.
    // These cached setup/display-transition queries avoid adding COM calls to
    // each Present while still making the eligibility decision auditable.
    bool nativeTearingFeatureQueryResultValid = false;
    int64_t nativeTearingFeatureQueryResult = 0;
    bool nativeTearingFeatureAllowsTearing = false;
    bool nativeSwapChainDescQueryResultValid = false;
    int64_t nativeSwapChainDescQueryResult = 0;
    uint32_t nativeSwapChainFlags = 0;
    uint32_t nativeSwapChainSwapEffect = 0;
    bool nativeFullscreenStateQueryResultValid = false;
    int64_t nativeFullscreenStateQueryResult = 0;
    bool nativeFullscreenExclusive = false;
    uint32_t nativeWindowFlags = 0;
    bool nativePresentReadyAvailable = false;
    bool nativeForegroundWindow = false;
    VrrFallbackReason nativeVrrFallbackReason =
        VrrFallbackReason::NoFallback;
    uint32_t nativeDesktopMonitorCount = 0;
    // DXGIDisableVBlankVirtualization() is probed and, when available, called
    // once at process startup before Qt can create a swapchain. Preserve the
    // exact signed HRESULT so replay can prove that native vblank timing was
    // requested successfully instead of inferring it from display rates.
    bool nativeVblankVirtualizationProbeComplete = false;
    bool nativeVblankVirtualizationCallAvailable = false;
    bool nativeVblankVirtualizationResultValid = false;
    int64_t nativeVblankVirtualizationResult = 0;
    bool nativeVblankVirtualizationDisabled = false;
    // Cached Windows CCD evidence for the display path containing the
    // Moonlight HWND. The path refresh can be a virtual/desktop rate while
    // signalVSync is the physical mode rate; retaining both exposes Dynamic
    // Refresh Rate instead of rounding either value to an integer Hz.
    bool nativeDisplayConfigQueryResultValid = false;
    uint32_t nativeDisplayConfigQueryResult = 0;
    bool nativeDisplayPathValid = false;
    uint32_t nativeDisplayPathFlags = 0;
    bool nativeDisplayTargetAvailable = false;
    uint64_t nativeDisplaySourceAdapterLuid = 0;
    uint32_t nativeDisplaySourceId = 0;
    uint64_t nativeDisplayTargetAdapterLuid = 0;
    uint32_t nativeDisplayTargetId = 0;
    uint32_t nativeDisplayOutputTechnology = 0;
    uint32_t nativeDisplayRotation = 0;
    uint32_t nativeDisplayScaling = 0;
    uint32_t nativeDisplayPathRefreshNumerator = 0;
    uint32_t nativeDisplayPathRefreshDenominator = 0;
    bool nativeDisplaySignalValid = false;
    uint64_t nativeDisplaySignalPixelRateHz = 0;
    uint32_t nativeDisplaySignalHSyncNumerator = 0;
    uint32_t nativeDisplaySignalHSyncDenominator = 0;
    uint32_t nativeDisplaySignalVSyncNumerator = 0;
    uint32_t nativeDisplaySignalVSyncDenominator = 0;
    uint32_t nativeDisplaySignalActiveWidth = 0;
    uint32_t nativeDisplaySignalActiveHeight = 0;
    uint32_t nativeDisplaySignalTotalWidth = 0;
    uint32_t nativeDisplaySignalTotalHeight = 0;
    uint32_t nativeDisplaySignalAdditionalInfoRaw = 0;
    uint32_t nativeDisplaySignalScanLineOrdering = 0;
    // MOONLIGHT_VRR_ALIGN enables two observation-only scan-position queries
    // bracketing the native Present. This deliberately does not spin, wait,
    // or alter presentation policy. The setup result and VidPn source ID bind
    // the samples to the same Windows display path captured above.
    bool nativeRasterSamplingRequested = false;
    bool nativeRasterOpenResultValid = false;
    int64_t nativeRasterOpenResult = 0;
    bool nativeRasterSourceValid = false;
    uint32_t nativeRasterVidPnSourceId = 0;
    VrrNativeRasterSample nativeRasterBeforePresent;
    VrrNativeRasterSample nativeRasterAfterPresent;

    // Optional presentation observation. The sample may describe an earlier
    // submission than this one; submissionId/latchSubmissionId associate the
    // two. Backends with a real presentation timestamp may put it in
    // latchTimeUs. DXGI does not expose that timestamp: its latchTimeUs is
    // SyncQPCTime, paired with latchRefreshSequence (SyncRefreshCount), and
    // must not be interpreted as the time of latchPresentRefreshSequence
    // (PresentRefreshCount). This is observation only: it must never gate,
    // delay, or fail a submission.
    bool submissionIdValid = false;
    uint64_t submissionId = 0;
    // Signed result from the native submission-ID query, independent of
    // whether the query returned a usable ID.
    bool submissionIdQueryResultValid = false;
    int64_t submissionIdQueryResult = 0;
    // Deep diagnostics bracket the two post-Present DXGI observations
    // independently. Their combined cost otherwise looks like renderer or
    // worker occupancy and cannot be reproduced honestly during replay.
    uint64_t submissionIdQueryStartUs = 0;
    uint64_t submissionIdQueryEndUs = 0;
    bool latchSampleValid = false;
    Vrr13::PresentationTimeKind latchTimeKind = Vrr13::PresentationTimeKind::Unavailable;
    uint64_t latchSubmissionId = 0;
    uint64_t latchTimeUs = 0;
    // Clock conversion uncertainty for non-DXGI presentation timestamps.
    uint64_t presentationUncertaintyUs = 0;
    // DXGI PresentRefreshCount (when available) identifies the v-blank at
    // which this image reached the monitor. It is distinct from the periodic
    // SyncRefreshCount/QPC clock sample below.
    uint64_t latchPresentRefreshSequence = 0;
    uint64_t latchRefreshSequence = 0;
    // Signed result from the native frame-statistics query. Raw QPC evidence
    // is retained separately so cross-clock translation can be audited.
    bool frameStatsQueryResultValid = false;
    int64_t frameStatsQueryResult = 0;
    uint64_t frameStatsQueryStartUs = 0;
    uint64_t frameStatsQueryEndUs = 0;
    bool latchRawSyncQpcValid = false;
    uint64_t latchRawSyncQpcTicks = 0;
    uint64_t latchRawSyncQpcFrequency = 0;
    // Stable process-wide QPC-to-Limelight clock correlation used for the
    // translation above. The bracket span records how far QPC advanced around
    // the reference LiGetMicroseconds() sample, making its alignment
    // uncertainty auditable rather than implicit.
    bool latchQpcCorrelationValid = false;
    uint64_t latchQpcCorrelationReferenceTicks = 0;
    uint64_t latchQpcCorrelationReferenceTimeUs = 0;
    uint64_t latchQpcCorrelationSpanTicks = 0;

    // Opt-in, observation-only diagnostics around the native Present call.
    // The before fields may reuse the backend's most recent post-Present
    // observation to avoid adding synchronous native queries to the hot path.
    // These fields must never affect presentation policy.
    bool nativePresentTimingValid = false;
    uint64_t nativePresentStartUs = 0;
    uint64_t nativePresentEndUs = 0;
    bool presentCountBeforeValid = false;
    uint64_t presentCountBefore = 0;
    bool frameStatsBeforeValid = false;
    uint64_t frameStatsBeforePresentCount = 0;
    uint64_t frameStatsBeforeTimeUs = 0;
    uint64_t frameStatsBeforePresentRefreshSequence = 0;
    uint64_t frameStatsBeforeRefreshSequence = 0;
    // Optional renderer-readiness timing. A backend that queues GPU work in
    // prepareFrame() reports the CPU wait around its completion primitive. The
    // wait return is only an upper bound on the actual GPU completion instant.
    // The signal/poll bracket below lets replay derive a conservative lower
    // bound too, rather than pretending the CPU wake timestamp is exact. The
    // D3D11 signal/event and fence-value fields are populated only by its
    // native fence path; Vulkan uses the poll timestamps and leaves those
    // fields unavailable. Exact native operation results remain available on
    // failed preparation rows too, so a readiness failure cannot collapse into
    // an unexplained generic cancellation. `gpuReadyTimingValid` is true only
    // for a completed readiness sample; failed attempts may retain their raw
    // observation timestamps while leaving the bit clear.
    bool gpuReadyAttempted = false;
    bool gpuReadySignalResultValid = false;
    int64_t gpuReadySignalResult = 0;
    bool gpuReadySetEventResultValid = false;
    int64_t gpuReadySetEventResult = 0;
    bool gpuReadyWaitResultValid = false;
    uint32_t gpuReadyWaitResult = 0;
    bool gpuReadyTimingValid = false;
    uint64_t gpuReadySignalStartUs = 0;
    uint64_t gpuReadySignalEndUs = 0;
    uint64_t gpuReadyFlushStartUs = 0;
    uint64_t gpuReadyFlushEndUs = 0;
    uint64_t gpuReadySetEventStartUs = 0;
    uint64_t gpuReadySetEventEndUs = 0;
    uint64_t gpuReadyPollStartUs = 0;
    uint64_t gpuReadyPollEndUs = 0;
    uint64_t gpuReadyFenceValue = 0;
    uint64_t gpuReadyPollCompletedValue = 0;
    bool gpuReadyCompletedBeforeWait = false;
    uint64_t gpuReadyWaitStartUs = 0;
    uint64_t gpuReadyTimeUs = 0;
};

// Per-present request from the platform-neutral controller. When the learned
// source cadence leaves too little adaptive-refresh headroom for safe
// immediate flips, the controller asks for a latched (non-tearing) present:
// at near-refresh rates the cadence cost of latching is a few repeated frames
// per second while immediate flips tear. The same request is supplied before
// preparation so swapchain-based backends can select the mode before acquiring
// an image. Backends must advertise latch support only when they honor it.
struct VrrPresentRequest {
    bool latchedPresentation = false;
    bool collectDiagnostics = false;
};

struct VrrPrepareResult {
    bool prepared = false;
    // True only when all backend GPU reads from the decoder-owned AVFrame
    // completed before prepareFrame() returned. The worker may then release
    // that frame before waiting for the presentation target, avoiding decoder
    // surface-pool backpressure while the prepared swap-chain image is held.
    bool sourceFrameReusable = false;
    // Some acquired images (notably Vulkan swapchain frames) can only be
    // abandoned by submitting them. The worker owns any required wait.
    bool cancellationMaySubmit = false;
    // Where the preparation spent its time, when the backend can split it:
    // waiting for the decoder's GPU work, acquiring the presentation image,
    // rendering (including source import), and flushing the GPU queue.
    bool timingValid = false;
    uint64_t decodeSyncUs = 0;
    uint64_t acquireUs = 0;
    uint64_t renderUs = 0;
    uint64_t flushUs = 0;
    VrrPresentFeedback feedback;
};

class IVrrFramePresenter {
public:
    virtual ~IVrrFramePresenter() = default;

    // Some backends select native protection per present; persistent Vulkan
    // Mailbox already provides it. Vulkan Immediate/FIFO return false and keep
    // the controller's software floor. A latch request must never recreate the
    // swapchain or replace an acquired image to change presentation mode.
    virtual bool canLatchAdaptivePresent() const
    {
        return false;
    }

    // Startup eligibility only. NoFallback means the presenter supports a worker-
    // thread split prepare/present path using its adaptive presentation mode.
    virtual VrrFallbackReason checkSupport() const = 0;

    // Block until the decoder's GPU work for this frame has completed, so
    // the pacer sees the frame's true readiness and preparation never waits
    // on the decoder. Returns the microseconds spent waiting; zero when the
    // backend cannot tell or the frame was already complete.
    virtual uint64_t waitForDecode(AVFrame*)
    {
        return 0;
    }

    // Fence-based backends need the boundary captured for this particular
    // output, rather than a later decoder signal. Existing frame-based
    // implementations retain their readiness handling through this overload.
    virtual uint64_t waitForDecode(AVFrame* frame, uint64_t)
    {
        return waitForDecode(frame);
    }

    // May acquire a swapchain image and submit rendering work, but must not
    // intentionally pace or wait for the worker's presentation target.
    // Called on the decoder-output thread immediately before a frame enters
    // the pacing queue. Backends with asynchronous decode can insert a fence
    // here and return an opaque value that identifies this frame's decode
    // boundary. Zero means no backend-specific boundary is available.
    virtual uint64_t captureDecodeBoundary()
    {
        return 0;
    }

    virtual VrrPrepareResult prepareFrame(AVFrame* frame,
                                          uint64_t decodeBoundary) = 0;

    // Mode selection belongs inside the measured preparation interval, before
    // image acquisition. Backends that select at Present keep the existing
    // two-argument preparation path.
    virtual VrrPrepareResult prepareFrame(AVFrame* frame,
                                          uint64_t decodeBoundary,
                                          const VrrPresentRequest&)
    {
        return prepareFrame(frame, decodeBoundary);
    }

    // Presents the prepared image using the backend's adaptive presentation
    // path without intentionally waiting.
    virtual VrrPresentFeedback presentAdaptive(
        const VrrPresentRequest& request) = 0;

    // Releases a prepared image. The result must report a native submission
    // when the platform cannot abandon an acquired image without submitting.
    virtual VrrPresentFeedback cancelFrame()
    {
        VrrPresentFeedback feedback;
        feedback.cancelled = true;
        return feedback;
    }

    virtual void setSuspended(bool)
    {
    }

    // Called only when creating the VRR worker failed, before any frame was
    // handed to it or a legacy render worker was started.
    virtual bool restoreFixedPresentation(VrrFallbackReason)
    {
        return false;
    }
};
