// For D3D11_DECODER_PROFILE values
#include <initguid.h>

#include "d3d11va.h"
#include "d3d11bindpolicy.h"
#include "d3d11fencewait.h"
#include "dxutil.h"
#include "path.h"
#include "utils.h"
#include "windowsvblankvirtualization.h"

#include "streaming/streamutils.h"
#include "streaming/session.h"

#include <SDL_syswm.h>
#include <Limelight.h>

#include <dwmapi.h>

#include <cwchar>
#include <limits>

using Microsoft::WRL::ComPtr;

// Standard DXVA GUIDs for HEVC RExt profiles (redefined for compatibility with pre-24H2 SDKs)
DEFINE_GUID(k_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN_444,   0x4008018f, 0xf537, 0x4b36, 0x98, 0xcf, 0x61, 0xaf, 0x8a, 0x2c, 0x1a, 0x33);
DEFINE_GUID(k_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10_444, 0x0dabeffa, 0x4458, 0x4602, 0xbc, 0x03, 0x07, 0x95, 0x65, 0x9d, 0x61, 0x7c);

typedef struct _VERTEX
{
    float x, y;
    float tu, tv;
} VERTEX, *PVERTEX;

#define CSC_MATRIX_RAW_ELEMENT_COUNT 9
#define CSC_MATRIX_PACKED_ELEMENT_COUNT 12
#define OFFSETS_ELEMENT_COUNT 3

typedef struct _CSC_CONST_BUF
{
    // CscMatrix value from above but packed and scaled
    float cscMatrix[CSC_MATRIX_PACKED_ELEMENT_COUNT];

    // YUV offset values
    float offsets[OFFSETS_ELEMENT_COUNT];

    // Padding float to end 16-byte boundary
    float padding;

    // Chroma offset values
    float chromaOffset[2];

    // Max UV coordinates to avoid sampling alignment padding
    float chromaUVMax[2];
} CSC_CONST_BUF, *PCSC_CONST_BUF;
static_assert(sizeof(CSC_CONST_BUF) % 16 == 0, "Constant buffer sizes must be a multiple of 16");

static const std::array<const char*, D3D11VARenderer::PixelShaders::_COUNT> k_VideoShaderNames =
{
    "d3d11_yuv420_pixel.fxc",
    "d3d11_ayuv_pixel.fxc",
    "d3d11_y410_pixel.fxc",
};

namespace {

bool isBorderlessFullscreenWindow(SDL_Window* window)
{
    if (window == nullptr) {
        return false;
    }

    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) ==
        SDL_WINDOW_FULLSCREEN_DESKTOP;
}

uint64_t packLuid(const LUID& luid)
{
    return static_cast<uint64_t>(luid.LowPart) |
        (static_cast<uint64_t>(
             static_cast<uint32_t>(luid.HighPart)) << 32);
}

struct VrrQpcClockCorrelation {
    LONGLONG referenceQpc = 0;
    LONGLONG qpcFrequency = 0;
    uint64_t referenceTimeUs = 0;
    uint64_t bracketSpanTicks = 0;
    bool valid = false;
};

const VrrQpcClockCorrelation& vrrQpcClockCorrelation()
{
    static const VrrQpcClockCorrelation correlation = []() {
        VrrQpcClockCorrelation value;
        // Ensure the Limelight clock origin is established before bracketing
        // the QPC sample used for the stable cross-clock correlation.
        (void)LiGetMicroseconds();
        LARGE_INTEGER frequency;
        LARGE_INTEGER before;
        LARGE_INTEGER after;
        if (!QueryPerformanceFrequency(&frequency) ||
                frequency.QuadPart <= 0) {
            return value;
        }
        // Retain the native frequency even if the cross-clock correlation
        // cannot be completed. A successful DXGI statistics query can then
        // still emit independently auditable raw SyncQPCTime evidence.
        value.qpcFrequency = frequency.QuadPart;
        if (!QueryPerformanceCounter(&before)) {
            return value;
        }
        value.referenceTimeUs = LiGetMicroseconds();
        if (!QueryPerformanceCounter(&after) ||
                after.QuadPart < before.QuadPart) {
            return value;
        }
        value.bracketSpanTicks = static_cast<uint64_t>(
            after.QuadPart - before.QuadPart);
        value.referenceQpc = before.QuadPart +
            (after.QuadPart - before.QuadPart) / 2;
        value.valid = true;
        return value;
    }();
    return correlation;
}

bool translateVrrSyncQpcTime(LARGE_INTEGER syncQpcTime,
                             uint64_t& translatedTimeUs,
                             uint64_t& qpcFrequency)
{
    translatedTimeUs = 0;
    qpcFrequency = 0;
    const VrrQpcClockCorrelation& correlation =
        vrrQpcClockCorrelation();
    if (correlation.qpcFrequency > 0) {
        qpcFrequency = static_cast<uint64_t>(correlation.qpcFrequency);
    }
    if (!correlation.valid) {
        return false;
    }
    if (syncQpcTime.QuadPart <= 0) {
        return false;
    }

    const uint64_t deltaTicks = syncQpcTime.QuadPart >=
            correlation.referenceQpc ?
        static_cast<uint64_t>(
            syncQpcTime.QuadPart - correlation.referenceQpc) :
        static_cast<uint64_t>(
            correlation.referenceQpc - syncQpcTime.QuadPart);
    const uint64_t frequency = static_cast<uint64_t>(
        correlation.qpcFrequency);
    const uint64_t wholeSeconds = deltaTicks / frequency;
    const uint64_t remainderTicks = deltaTicks % frequency;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    if (wholeSeconds > maximum / 1000000ULL ||
            remainderTicks > maximum / 1000000ULL) {
        return false;
    }
    const uint64_t wholeUs = wholeSeconds * 1000000ULL;
    const uint64_t remainderUs =
        remainderTicks * 1000000ULL / frequency;
    if (remainderUs > maximum - wholeUs) {
        return false;
    }
    const uint64_t deltaUs = wholeUs + remainderUs;
    if (syncQpcTime.QuadPart < correlation.referenceQpc) {
        if (deltaUs > correlation.referenceTimeUs) {
            return false;
        }
        translatedTimeUs = correlation.referenceTimeUs - deltaUs;
    }
    else {
        if (deltaUs > maximum - correlation.referenceTimeUs) {
            return false;
        }
        translatedTimeUs = correlation.referenceTimeUs + deltaUs;
    }
    return true;
}

}

D3D11VARenderer::D3D11VARenderer(int decoderSelectionPass)
    : IFFmpegRenderer(RendererType::D3D11VA),
      m_DecoderSelectionPass(decoderSelectionPass),
      m_DevicesWithFL11Support(0),
      m_DevicesWithCodecSupport(0),
      m_AdapterIndex(-1),
      m_RenderAdapterIndex(-1),
      m_VrrRenderAdapterLuidValid(false),
      m_VrrRenderAdapterLuid(0),
      m_LastColorTrc(AVCOL_TRC_UNSPECIFIED),
      m_AllowTearing(false),
      m_SyncInterval(0),
      m_AdapterVendorId(0),
      m_CadenceDiagEnabled(false),
      m_CadenceRefreshTrusted(true),
      m_DisplayHz(0),
      m_CadenceLock(0),
      m_HavePublishedCadence(false),
      m_CadenceSequence(0),
      m_VrrTearingSupported(false),
      m_VrrBorderlessFlipModel(false),
      m_VrrSameGpuOutput(false),
      m_VrrSwapChainAllowsTearing(false),
      m_VrrTearingFeatureQueryResultValid(false),
      m_VrrTearingFeatureQueryResult(0),
      m_VrrTearingFeatureAllowsTearing(false),
      m_VrrSwapChainDescQueryResultValid(false),
      m_VrrSwapChainDescQueryResult(0),
      m_VrrSwapChainFlags(0),
      m_VrrSwapChainSwapEffect(0),
      m_VrrFullscreenStateQueryResultValid(false),
      m_VrrFullscreenStateQueryResult(0),
      m_VrrFullscreenExclusive(false),
      m_VrrWindowFlags(0),
      m_VrrDesktopMonitorCount(0),
      m_VrrWindowHandle(nullptr),
      m_VrrDisplayTiming(),
      m_VrrRasterSamplingRequested(false),
      m_VrrRasterOpenResultValid(false),
      m_VrrRasterOpenResult(0),
      m_VrrRasterSourceValid(false),
      m_VrrRasterAdapter(0),
      m_VrrRasterVidPnSourceId(0),
      m_VrrSuspended(false),
      m_VrrFallbackReason(VrrFallbackReason::InitializationFailed),
      m_VrrFramePrepared(false),
      m_VrrContextLocked(false),
      m_VrrPresentReadyFenceValue(0),
      m_VrrPresentReadyFenceEvent(nullptr),
      m_VrrPresentReadyAvailable(false),
      m_VrrGpuReadyAttempted(false),
      m_VrrGpuReadySignalResultValid(false),
      m_VrrGpuReadySignalResult(0),
      m_VrrGpuReadySetEventResultValid(false),
      m_VrrGpuReadySetEventResult(0),
      m_VrrGpuReadyWaitResultValid(false),
      m_VrrGpuReadyWaitResult(0),
      m_VrrGpuReadyTimingValid(false),
      m_VrrGpuReadySignalStartUs(0),
      m_VrrGpuReadySignalEndUs(0),
      m_VrrGpuReadyFlushStartUs(0),
      m_VrrGpuReadyFlushEndUs(0),
      m_VrrGpuReadySetEventStartUs(0),
      m_VrrGpuReadySetEventEndUs(0),
      m_VrrGpuReadyPollStartUs(0),
      m_VrrGpuReadyPollEndUs(0),
      m_VrrGpuReadyFenceValue(0),
      m_VrrGpuReadyPollCompletedValue(0),
      m_VrrGpuReadyCompletedBeforeWait(false),
      m_VrrGpuReadyWaitStartUs(0),
      m_VrrGpuReadyTimeUs(0),
      m_VrrPriorPresentCountValid(false),
      m_VrrPriorPresentCount(0),
      m_VrrPriorFrameStatsValid(false),
      m_VrrPriorFrameStatsPresentCount(0),
      m_VrrPriorFrameStatsTimeUs(0),
      m_VrrPriorFrameStatsPresentRefreshSequence(0),
      m_VrrPriorFrameStatsRefreshSequence(0),
      m_OverlayLock(0),
      m_HwDeviceContext(nullptr)
{
    SDL_zero(m_Cadence);
    SDL_zero(m_PublishedCadence);
    m_Cadence.minVblanks = -1;

    m_ContextLock = SDL_CreateMutex();
    DwmEnableMMCSS(TRUE);
}

D3D11VARenderer::~D3D11VARenderer()
{
    DwmEnableMMCSS(FALSE);

    // The VRR worker may be cancelled after preparation but before Present.
    // Release the retained D3D context lock before destroying it or any
    // back-buffer objects.
    cancelFrame();
    closeVrrRasterSource();
    SDL_DestroyMutex(m_ContextLock);

    m_VideoVertexBuffer.Reset();
    for (auto& shader : m_VideoPixelShaders) {
        shader.Reset();
    }

    for (auto& textureSrvs : m_VideoTextureResourceViews) {
        for (auto& srv : textureSrvs) {
            srv.Reset();
        }
    }

    m_VideoTexture.Reset();

    // Before the arrays below: these hold references to the same objects, and everything
    // overlay-side has to be gone before the device is.
    for (auto& last : m_LastOverlay) {
        last = {};
    }

    for (auto& buffer : m_OverlayVertexBuffers) {
        buffer.Reset();
    }

    for (auto& srv : m_OverlayTextureResourceViews) {
        srv.Reset();
    }

    for (auto& texture : m_OverlayTextures) {
        texture.Reset();
    }

    m_OverlayPixelShader.Reset();

    m_OverlayBlendState.Reset();
    m_VideoBlendState.Reset();

    m_DecodeD2RFence.Reset();
    m_DecodeR2DFence.Reset();
    m_RenderD2RFence.Reset();
    m_RenderR2DFence.Reset();

    m_VrrPresentReadyFence.Reset();
    if (m_VrrPresentReadyFenceEvent != nullptr) {
        CloseHandle(m_VrrPresentReadyFenceEvent);
        m_VrrPresentReadyFenceEvent = nullptr;
    }

    m_RenderTargetView.Reset();
    m_CompositionPresenter.reset();
    m_SwapChain.Reset();

    m_RenderSharedTextureArray.Reset();

    av_buffer_unref(&m_HwDeviceContext);
    m_DecodeDevice.Reset();
    m_DecodeDeviceContext.Reset();

    // Force destruction of the swapchain immediately
    if (m_RenderDeviceContext != nullptr) {
        m_RenderDeviceContext->ClearState();
        m_RenderDeviceContext->Flush();
    }

    m_RenderDevice.Reset();
    m_RenderDeviceContext.Reset();
    m_Factory.Reset();
}

bool D3D11VARenderer::createSharedFencePair(UINT64 initialValue, ID3D11Device5* dev1, ID3D11Device5* dev2, ComPtr<ID3D11Fence>& dev1Fence, ComPtr<ID3D11Fence>& dev2Fence)
{
    HRESULT hr;
    D3D11_FENCE_FLAG flags;

    flags = D3D11_FENCE_FLAG_SHARED;
    if (m_FenceType == SupportedFenceType::NonMonitored) {
        flags |= D3D11_FENCE_FLAG_NON_MONITORED;
    }

    hr = dev1->CreateFence(initialValue, flags, IID_PPV_ARGS(&dev1Fence));
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device5::CreateFence() failed: %x",
                     hr);
        return false;
    }

    HANDLE fenceHandle;
    hr = dev1Fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fenceHandle);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Fence::CreateSharedHandle() failed: %x",
                     hr);
        dev1Fence.Reset();
        return false;
    }

    hr = dev2->OpenSharedFence(fenceHandle, IID_PPV_ARGS(&dev2Fence));
    CloseHandle(fenceHandle);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device5::OpenSharedFence() failed: %x",
                     hr);
        dev1Fence.Reset();
        return false;
    }

    return true;
}

bool D3D11VARenderer::setupSharedDevice(IDXGIAdapter1* adapter)
{
    const D3D_FEATURE_LEVEL supportedFeatureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    bool success = false;

    // We don't support cross-device sharing without fences
    if (m_FenceType == SupportedFenceType::None) {
        return false;
    }

    // If we're going to use separate devices for decoding and rendering, create the decoding device
    hr = D3D11CreateDevice(adapter,
                           D3D_DRIVER_TYPE_UNKNOWN,
                           nullptr,
                           D3D11_CREATE_DEVICE_VIDEO_SUPPORT
#ifdef QT_DEBUG
                               | D3D11_CREATE_DEVICE_DEBUG
#endif
                           ,
                           supportedFeatureLevels,
                           ARRAYSIZE(supportedFeatureLevels),
                           D3D11_SDK_VERSION,
                           &device,
                           &featureLevel,
                           &deviceContext);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3D11CreateDevice() failed: %x",
                     hr);
        return false;
    }

    hr = device.As(&m_DecodeDevice);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::QueryInterface(ID3D11Device1) failed: %x",
                     hr);
        goto Exit;
    }

    hr = deviceContext.As(&m_DecodeDeviceContext);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11DeviceContext::QueryInterface(ID3D11DeviceContext1) failed: %x",
                     hr);
        goto Exit;
    }

    // Create our decode->render fence
    m_D2RFenceValue = 1;
    if (!createSharedFencePair(0, m_DecodeDevice.Get(), m_RenderDevice.Get(), m_DecodeD2RFence, m_RenderD2RFence)) {
        goto Exit;
    }

    // Create our render->decode fence
    m_R2DFenceValue = 1;
    if (!createSharedFencePair(0, m_DecodeDevice.Get(), m_RenderDevice.Get(), m_DecodeR2DFence, m_RenderR2DFence)) {
        goto Exit;
    }

    success = true;
Exit:
    if (!success) {
        m_DecodeD2RFence.Reset();
        m_RenderD2RFence.Reset();
        m_DecodeR2DFence.Reset();
        m_RenderR2DFence.Reset();
        m_DecodeDevice.Reset();
    }

    return success;
}

bool D3D11VARenderer::createDeviceByAdapterIndex(int adapterIndex, bool* adapterNotFound)
{
    const D3D_FEATURE_LEVEL supportedFeatureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    const bool tryComposition = m_CompositionRequested && D3D11CompositionPresenter::runtimeSupported();
    bool success = false;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 adapterDesc;
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    LARGE_INTEGER umdVersion;

    SDL_assert(!m_RenderDevice);
    SDL_assert(!m_RenderDeviceContext);
    SDL_assert(!m_DecodeDevice);
    SDL_assert(!m_DecodeDeviceContext);

    hr = m_Factory->EnumAdapters1(adapterIndex, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND) {
        // Expected at the end of enumeration
        goto Exit;
    }
    else if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIFactory::EnumAdapters1() failed: %x",
                     hr);
        goto Exit;
    }

    hr = adapter->GetDesc1(&adapterDesc);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIAdapter::GetDesc() failed: %x",
                     hr);
        goto Exit;
    }

    if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        // Skip the WARP device. We know it will fail.
        goto Exit;
    }

    // Query the GPU driver version
    hr = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umdVersion);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIAdapter::CheckInterfaceSupport() failed: %x",
                     hr);
        goto Exit;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Detected GPU %d: %S (%x:%x) (driver: %u.%u.%u.%u)",
                adapterIndex,
                adapterDesc.Description,
                adapterDesc.VendorId,
                adapterDesc.DeviceId,
                HIWORD(umdVersion.HighPart),
                LOWORD(umdVersion.HighPart),
                HIWORD(umdVersion.LowPart),
                LOWORD(umdVersion.LowPart));

    hr = D3D11CreateDevice(adapter.Get(),
                           D3D_DRIVER_TYPE_UNKNOWN,
                           nullptr,
                           D3D11_CREATE_DEVICE_VIDEO_SUPPORT
                       #ifdef QT_DEBUG
                               | D3D11_CREATE_DEVICE_DEBUG
                       #endif
                               | (tryComposition ? D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                                  D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS : 0)
                           ,
                           supportedFeatureLevels,
                           ARRAYSIZE(supportedFeatureLevels),
                           D3D11_SDK_VERSION,
                           &device,
                           &featureLevel,
                           &deviceContext);
    if (tryComposition && (FAILED(hr) || !D3D11CompositionPresenter::deviceSupported(device.Get()))) {
        // A Windows 11 version alone does not establish driver support. The
        // legacy device should retain its normal driver threading policy.
        deviceContext.Reset();
        device.Reset();
        hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                              D3D11_CREATE_DEVICE_VIDEO_SUPPORT
                       #ifdef QT_DEBUG
                              | D3D11_CREATE_DEVICE_DEBUG
                       #endif
                              ,
                              supportedFeatureLevels, ARRAYSIZE(supportedFeatureLevels), D3D11_SDK_VERSION,
                              &device, &featureLevel, &deviceContext);
    }
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3D11CreateDevice() failed: %x",
                     hr);
        goto Exit;
    }
    else if (adapterDesc.VendorId == 0x8086 && featureLevel <= D3D_FEATURE_LEVEL_11_0 && !qEnvironmentVariableIntValue("D3D11VA_ENABLED")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Avoiding D3D11VA on old pre-FL11.1 Intel GPU. Set D3D11VA_ENABLED=1 to override.");
        goto Exit;
    }
    else if (featureLevel >= D3D_FEATURE_LEVEL_11_0) {
        // Remember that we found a non-software D3D11 devices with support for
        // feature level 11.0 or later (Fermi, Terascale 2, or Ivy Bridge and later)
        m_DevicesWithFL11Support++;
    }

    hr = device.As(&m_RenderDevice);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::QueryInterface(ID3D11Device1) failed: %x",
                     hr);
        goto Exit;
    }

    hr = deviceContext.As(&m_RenderDeviceContext);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11DeviceContext::QueryInterface(ID3D11DeviceContext1) failed: %x",
                     hr);
        goto Exit;
    }

    // Check which fence types are supported by this GPU
    {
        m_FenceType = SupportedFenceType::None;

        ComPtr<IDXGIAdapter4> adapter4;
        if (SUCCEEDED(adapter.As(&adapter4))) {
            DXGI_ADAPTER_DESC3 desc3;
            if (SUCCEEDED(adapter4->GetDesc3(&desc3))) {
                if (desc3.Flags & DXGI_ADAPTER_FLAG3_SUPPORT_MONITORED_FENCES) {
                    // Monitored fences must be used when they are supported
                    m_FenceType = SupportedFenceType::Monitored;
                }
                else if (desc3.Flags & DXGI_ADAPTER_FLAG3_SUPPORT_NON_MONITORED_FENCES) {
                    // Non-monitored fences must only be used when monitored fences are unsupported
                    m_FenceType = SupportedFenceType::NonMonitored;
                }
            }
        }
    }

    bool separateDevices;
    if (Utils::getEnvironmentVariableOverride("D3D11VA_FORCE_SEPARATE_DEVICES", &separateDevices)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Using D3D11VA_FORCE_SEPARATE_DEVICES to override default logic");
    }
    else {
        D3D11_FEATURE_DATA_D3D11_OPTIONS d3d11Options;

        // Check if cross-device sharing works for YUV textures and fences are supported
        hr = m_RenderDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &d3d11Options, sizeof(d3d11Options));
        separateDevices = SUCCEEDED(hr) && d3d11Options.ExtendedResourceSharing && m_FenceType != SupportedFenceType::None;

        if (separateDevices) {
            // Use minimum precision support to differentiate Vega and later from Polaris and earlier
            D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT minPrecSupport;
            hr = m_RenderDevice->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &minPrecSupport, sizeof(minPrecSupport));
            if (FAILED(hr)) {
                minPrecSupport = {};
            }

            // This texture array sharing codepath is quite prone to driver bugs.
            //
            // Broken GPU vendors/cards/drivers include:
            // - Moore Threads (texture is all zero/green)
            // - Qualcomm (decoding is unstable/slow on QC710)
            // - AMD prior to Vega (Polaris cards display corrupt output - see #2003,
            //                      HD 5570 drivers deadlock with shared texture arrays)
            // - Nvidia drivers prior to ~471.11 (Earlier drivers display all zero/green,
            //                                    We approximate by requiring WDDM 3.0+)
            //
            // Due to all these issues, we will only use this path for Intel/AMD and NVIDIA where we know it
            // provides tangible benefits (performance for the former and VRR support for the latter).
            separateDevices = adapterDesc.VendorId == 0x8086 || // Intel
                              (adapterDesc.VendorId == 0x10DE && HIWORD(umdVersion.HighPart) >= 30) || // NVIDIA WDDM 3.0+ (PCI ID)
                              adapterDesc.VendorId == 'ADVN' || // NVIDIA (WoA)
                              (adapterDesc.VendorId == 0x1002 && (minPrecSupport.PixelShaderMinPrecision & D3D11_SHADER_MIN_PRECISION_16_BIT)); // AMD Vega+
        }
    }

    // If we're going to use separate devices for decoding and rendering, create the decoding device
    if (!separateDevices || !setupSharedDevice(adapter.Get())) {
        m_DecodeDevice = m_RenderDevice;
        m_DecodeDeviceContext = m_RenderDeviceContext;
        separateDevices = false;
    }

    if (Utils::getEnvironmentVariableOverride("D3D11VA_FORCE_BIND", &m_BindDecoderOutputTextures)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Using D3D11VA_FORCE_BIND to override default bind/copy logic");
    }
    else {
        // Preserve existing Intel/separate-device binding; avoid the extra
        // full-frame copy on eligible single-device streams only at 4K+.
        // From Nonary v6.1.0-vrr17.1 (4261d7d9, ba1501a4, 596129b2). Not gated on
        // VRR: a 4K stream on a single AMD/NVIDIA device binds in every pacing mode.
        m_BindDecoderOutputTextures = d3d11ShouldBindDecoderOutputTextures(
            adapterDesc.VendorId == 0x8086, separateDevices,
            m_FenceType != SupportedFenceType::None ||
                featureLevel >= D3D_FEATURE_LEVEL_11_1,
            m_DecoderParams.width, m_DecoderParams.height);
    }

    // Captured here because this is the only place the adapter we settled on is known.
    // The cadence diagnostic uses it to decide whether the presentation refresh counter
    // can be believed - see initialize().
    m_AdapterVendorId = adapterDesc.VendorId;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Decoder texture access: %s (fence: %s)",
                m_BindDecoderOutputTextures ? "bind" : "copy",
                m_FenceType == SupportedFenceType::Monitored ? "monitored" :
                    (m_FenceType == SupportedFenceType::NonMonitored ? "non-monitored" : "unsupported"));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using %s device for decoding and rendering",
                separateDevices ? "separate" : "shared");

    if (!checkDecoderSupport(adapter.Get())) {
        goto Exit;
    }
    else {
        // Remember that we found a device with support for decoding this codec
        m_DevicesWithCodecSupport++;
    }

    m_RenderAdapterIndex = adapterIndex;
    m_VrrRenderAdapterLuidValid = true;
    m_VrrRenderAdapterLuid = packLuid(adapterDesc.AdapterLuid);
    success = true;

Exit:
    if (adapterNotFound != nullptr) {
        *adapterNotFound = !adapter;
    }
    if (!success) {
        m_RenderDeviceContext.Reset();
        m_RenderDevice.Reset();
        m_DecodeDeviceContext.Reset();
        m_DecodeDevice.Reset();
    }
    return success;
}

bool D3D11VARenderer::initialize(PDECODER_PARAMETERS params)
{
    int outputIndex;
    HRESULT hr;

    m_DecoderParams = *params;
    // The composition API supplies display events, but it does not implement
    // the controller's per-frame tearing/synchronized presentation choice.
    // Keep it available for capture comparisons without replacing DXGI VRR.
    m_CompositionRequested = params->enableVrr &&
        qgetenv("MOONLIGHT_VRR_COMPOSITION") == "1";

    if (qgetenv("D3D11VA_ENABLED") == "0") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11VA is disabled by environment variable");
        return false;
    }

    if (!SDL_DXGIGetOutputInfo(SDL_GetWindowDisplayIndex(params->window),
                               &m_AdapterIndex, &outputIndex)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_DXGIGetOutputInfo() failed: %s",
                     SDL_GetError());
        return false;
    }
    hr = CreateDXGIFactory2(
#ifdef QT_DEBUG
        DXGI_CREATE_FACTORY_DEBUG,
#else
        0,
#endif
        __uuidof(IDXGIFactory5),
        (void**)&m_Factory);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CreateDXGIFactory() failed: %x",
                     hr);
        return false;
    }

    // First try the adapter corresponding to the display where our window resides.
    // This will let us avoid a copy if the display GPU has the required decoder.
    if (!createDeviceByAdapterIndex(m_AdapterIndex)) {
        // If that didn't work, we'll try all GPUs in order until we find one
        // or run out of GPUs (DXGI_ERROR_NOT_FOUND from EnumAdapters())
        bool adapterNotFound = false;
        for (int i = 0; !adapterNotFound; i++) {
            if (i == m_AdapterIndex) {
                // Don't try the same GPU again
                continue;
            }

            if (createDeviceByAdapterIndex(i, &adapterNotFound)) {
                // This GPU worked! Continue initialization.
                break;
            }
        }

        if (adapterNotFound) {
            SDL_assert(!m_RenderDevice);
            SDL_assert(!m_RenderDeviceContext);
            return false;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = 0;

    // 3 front buffers (default GetMaximumFrameLatency() count)
    // + 1 back buffer
    // + 1 extra for DWM to hold on to for DirectFlip
    //
    // Even though we allocate 3 front buffers for pre-rendered frames,
    // they won't actually increase presentation latency because we
    // always use SyncInterval 0 which replaces the last one.
    //
    // IDXGIDevice1 has a SetMaximumFrameLatency() function, but counter-
    // intuitively we must avoid it to reduce latency. If we set our max
    // frame latency to 1 on thedevice, our SyncInterval 0 Present() calls
    // will block on DWM (acting like SyncInterval 1) rather than doing
    // the non-blocking present we expect.
    //
    // NB: 3 total buffers seems sufficient on NVIDIA hardware but
    // causes performance issues (buffer starvation) on AMD GPUs.
    swapChainDesc.BufferCount = 3 + 1 + 1;

    // Use the current window size as the swapchain size
    SDL_GetWindowSize(params->window, (int*)&swapChainDesc.Width, (int*)&swapChainDesc.Height);

    m_DisplayWidth = swapChainDesc.Width;
    m_DisplayHeight = swapChainDesc.Height;

    // ⚠️ The swapchain is sized from the window BEFORE the launch curtain lifts, and the
    // window is created hidden (5.0.0), so with the curtain on this can run twenty seconds
    // before the window is really on screen and in exclusive fullscreen; the resize below
    // is what corrects it. A [size-diag] probe measured this on 22/08/2026 while chasing
    // "the first stream shows a magnified quarter of the picture": logical and physical
    // window sizes matched, the resize always fired with the right numbers, and the fault
    // turned out to be host-side (Vibeshine failing to apply its virtual display scale, so
    // its capture ran at 3840x2160 against a 1920x1080 desktop). Nothing to fix here — but
    // if that symptom ever returns, this is the first place to instrument again.

    if (params->videoFormat & VIDEO_FORMAT_MASK_10BIT) {
        swapChainDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    }
    else {
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    initializeVrrPresentationState(params->window, &swapChainDesc);

    // DXVA2 may let us take over for FSE V-sync off cases. However, if we don't have DXGI_FEATURE_PRESENT_ALLOW_TEARING
    // then we should not attempt to do this unless there's no other option (HDR, DXVA2 failed in pass 1, etc).
    if (!m_AllowTearing && !params->enableVsync && m_DecoderSelectionPass == 0 && !(params->videoFormat & VIDEO_FORMAT_MASK_10BIT) &&
            (SDL_GetWindowFlags(params->window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Defaulting to DXVA2 for FSE without DXGI_FEATURE_PRESENT_ALLOW_TEARING support");
        return false;
    }

    // ── Fractional V-Sync (5.6.0 experiment, issue #11) ───────────────────────────
    //
    // History first, because this line has been here before and was taken out.
    //
    // A DXGI sync interval of 2..4 lived here from 3.4.0 to 5.1.3 as "hardware frame
    // pacing". It answered issue #2 (judder at 60 FPS on a 120 Hz panel) and it went on
    // to cause issue #9: holding frames for two V-blanks makes Present() queue rather
    // than replace, the queue settled at DXGI's default maximum frame latency of 3, and
    // from then on every Present() blocked - measured on an Ally as the wait going from
    // 0.4 ms to a sawtooth between 4 and 17 ms, permanently, while the cadence stayed a
    // flawless 2:2. Two releases tried to fix the queue from inside (5.1.1's waitable
    // swapchain, 5.1.2's frame-latency cap) and both were rolled back as worse than the
    // thing they treated. 5.2.0 removed the whole mechanism.
    //
    // ⚠️ What is different this time, and it is the entire point: the old version also
    // announced RENDERER_ATTRIBUTE_SELF_PACING, which switched the software Pacer OFF.
    // With no Pacer and a sync interval, the render loop's ONLY clock was Present()
    // blocking - the render thread ran as fast as Present let it, and where the queue
    // happened to settle in the first seconds became a permanent, invisible frame of
    // latency. That attribute is gone (renderer.h) and is not coming back: here the
    // Pacer keeps running on its own V-blank source and the render thread is clocked by
    // vblank, with the interval applied underneath it.
    //
    // That arrangement - Pacer on, interval 2 - is exactly what @Soladus has been
    // running for months, because Special-K's "PresentationInterval = 2" rewrites the
    // sync interval in our Present() call from outside the process and cannot possibly
    // know about our Pacer. His words for it: "1/2 vsync is my cure-all for every
    // problem". So the combination is known to work in the field; what has never
    // shipped is us doing it ourselves.
    //
    // ⚠️ The measurement that decides whether this stays is presentWaitMs on the Cadence
    // overlay line. A flat sub-millisecond wait for a whole session is the pass; the
    // wait settling at a frame period and staying there is issue #9 returning, and the
    // answer then is to remove this again rather than to treat the queue from inside,
    // which is the mistake 5.1.1 and 5.1.2 already made once each.
    //
    // Constraints, unchanged from the old implementation: DXGI accepts 1..4, and the
    // ratio has to be a whole number - so at 60 FPS this applies on 120, 180 and 240 Hz
    // and not on 144 (2.4x).
    //
    // ⚠️ The constraint is on the RATIO, not on the panel, and saying it the other way
    // round was wrong in three places before @Soladus pointed it out on issue #11: a
    // 144 Hz screen is a clean 2x at 72 FPS, 3x at 48 and 4x at 36, and since 5.5.0 the
    // frame rate row has a Custom pill that can ask for any of them. What the panel
    // cannot do is 60 FPS. That matters beyond wording: the 144 Hz gap was the last
    // surviving argument for Match refresh rate, and it is narrower than it looked.
    m_SyncInterval = 0;
    m_DisplayHz = StreamUtils::getDisplayRefreshRate(params->window);
    // ⚠️ !params->enableVrr is a belt to the braces: Session already switched fractional
    // off for a VRR session (mutual exclusion lives there, in one place). If both ever
    // arrive true, VRR wins here too — a fractional interval on the VRR present path
    // would fight the adaptive pacing rather than do nothing.
    if (params->fractionalVsync && !params->enableVrr &&
            params->enableVsync && params->enableFramePacing &&
            !m_AllowTearing && !params->testOnly) {
        int fps = params->frameRate;
        if (fps > 0 && m_DisplayHz >= fps * 2) {
            int n = (m_DisplayHz + fps / 2) / fps;  // round(displayHz / fps)
            int diff = m_DisplayHz - n * fps;       // allow +/-1 Hz of reporting slack
            if (n >= 2 && n <= 4 && diff <= 1 && diff >= -1) {
                m_SyncInterval = n;
            }
        }
    }

    // Logged either way, and with the inputs, because "I turned it on and nothing
    // happened" is the most likely report and the ratio is the usual reason.
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Fractional V-Sync: %s, sync interval %d (display %d Hz, stream %d FPS, "
                "V-sync %s, pacing %s, tearing %s)",
                m_SyncInterval >= 2 ? "ACTIVE"
                                    : (params->fractionalVsync ? "requested but not applicable" : "off"),
                m_SyncInterval, m_DisplayHz, params->frameRate,
                params->enableVsync ? "on" : "off",
                params->enableFramePacing ? "on" : "off",
                m_AllowTearing ? "on" : "off");

    // The cadence diagnostic runs whenever its overlay line is switched on, whether or
    // not the interval is in use: the OFF case is the baseline the ON case is judged
    // against, and an instrument that only exists on one side of a comparison is worth
    // very little. STREAMLIGHT_PACING_DIAG=1 forces it on for a session with no overlay.
    m_CadenceDiagEnabled = !params->testOnly &&
                           ((StreamingPreferences::get()->overlayItems & StreamingPreferences::OI_CADENCE) != 0 ||
                            qEnvironmentVariableIntValue("STREAMLIGHT_PACING_DIAG") != 0);

    // ⚠️ On Intel the presentation refresh counter does not track the panel: measured on
    // an Arc handheld, PresentRefreshCount advanced by one per present regardless of the
    // interval, producing a "cadence slip" on every single frame. The wait and queue
    // figures do not come from that counter and stay honest, so the V-blank half of the
    // line is dropped rather than the whole instrument.
    if (m_CadenceDiagEnabled && m_AdapterVendorId == 0x8086) {
        m_CadenceRefreshTrusted = false;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[pacing] V-blank figures suppressed on this adapter: its presentation "
                    "counter disagrees with the panel, so cadence and slips are not reported");
    }

    if (m_CadenceDiagEnabled) {
        m_Cadence.windowStartUs = LiGetMicroseconds();
        m_Cadence.minVblanks = -1;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[pacing] presentation cadence diagnostics enabled");
    }

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(params->window, &info);
    SDL_assert(info.subsystem == SDL_SYSWM_WINDOWS);

    // Always use windowed or borderless windowed mode.. SDL does mode-setting for us in
    // full-screen exclusive mode (SDL_WINDOW_FULLSCREEN), so this actually works out okay.
    ComPtr<IDXGISwapChain1> swapChain;
    hr = m_Factory->CreateSwapChainForHwnd(m_RenderDevice.Get(),
                                           info.info.win.window,
                                           &swapChainDesc,
                                           nullptr,
                                           nullptr,
                                           &swapChain);

    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIFactory::CreateSwapChainForHwnd() failed: %x",
                     hr);
        return false;
    }

    hr = swapChain.As(&m_SwapChain);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGISwapChain::QueryInterface(IDXGISwapChain4) failed: %x",
                     hr);
        return false;
    }

    // ⚠️ Deliberately no SetMaximumFrameLatency() call here, and now for upstream's own
    // reason rather than ours: see the comment on BufferCount above, which explains that
    // capping the latency at 1 makes our non-blocking SyncInterval 0 presents block on
    // DWM instead. 5.1.2 briefly capped the queue at 2 on the hardware-paced branch;
    // 5.1.3 removed the cap and 5.2.0 removed the branch, so there is no longer any
    // configuration in which this renderer wants a frame-latency cap.

    // Disable Alt+Enter, PrintScreen, and window message snooping. This makes
    // it safe to run the renderer on a separate rendering thread rather than
    // requiring the main (message loop) thread.
    hr = m_Factory->MakeWindowAssociation(info.info.win.window, DXGI_MWA_NO_WINDOW_CHANGES);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIFactory::MakeWindowAssociation() failed: %x",
                     hr);
        return false;
    }

    // Query the created swapchain rather than trusting the requested
    // descriptor. Driver/runtime normalization and fullscreen state are part
    // of the capability evidence and must be settled before VRR starts.
    refreshVrrDisplayState();

    if (m_CompositionRequested &&
            m_VrrFallbackReason == VrrFallbackReason::NoFallback && m_VrrDisplayTiming.pathValid) {
        LUID adapter = {};
        adapter.LowPart = static_cast<DWORD>(m_VrrDisplayTiming.sourceAdapterLuid);
        adapter.HighPart = static_cast<LONG>(m_VrrDisplayTiming.sourceAdapterLuid >> 32);
        const HRESULT compositionResult = m_CompositionPresenter.initialize(
            m_RenderDevice.Get(), info.info.win.window, swapChainDesc.Width, swapChainDesc.Height,
            swapChainDesc.Format, adapter, m_VrrDisplayTiming.sourceId);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Windows presentation timing: %s (result: %x)",
                    SUCCEEDED(compositionResult) ? "composition manager active" : "DXGI estimate fallback",
                    compositionResult);
    }

    if (m_DecoderParams.enableVrr && m_VrrFallbackReason == VrrFallbackReason::NoFallback) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR backend enabled: refresh=%d Hz, presentation=%s",
                    m_DecoderParams.vrrDisplayRefreshHz,
                    m_CompositionPresenter.active() ? "composition diagnostic (native ordering)" :
                        "DXGI (per-frame tearing/synchronized, estimated timing)");
    }

    {
        m_HwDeviceContext = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!m_HwDeviceContext) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to allocate D3D11VA device context");
            return false;
        }

        AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwDeviceContext->data;
        AVD3D11VADeviceContext* d3d11vaDeviceContext = (AVD3D11VADeviceContext*)deviceContext->hwctx;

        // FFmpeg will take ownership of these pointers, so we use CopyTo() to bump the ref count
        m_DecodeDevice.CopyTo(&d3d11vaDeviceContext->device);
        m_DecodeDeviceContext.CopyTo(&d3d11vaDeviceContext->device_context);

        // Set lock functions that we will use to synchronize with FFmpeg's usage of our device context
        d3d11vaDeviceContext->lock = lockContext;
        d3d11vaDeviceContext->unlock = unlockContext;
        d3d11vaDeviceContext->lock_ctx = this;

        int err = av_hwdevice_ctx_init(m_HwDeviceContext);
        if (err < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to initialize D3D11VA device context: %d",
                         err);
            return false;
        }
    }

    if (!setupRenderingResources()) {
        return false;
    }

    return true;
}

bool D3D11VARenderer::prepareDecoderContext(AVCodecContext* context, AVDictionary**)
{
    context->hw_device_ctx = av_buffer_ref(m_HwDeviceContext);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using D3D11VA accelerated renderer");

    return true;
}

bool D3D11VARenderer::prepareDecoderContextInGetFormat(AVCodecContext *context, AVPixelFormat pixelFormat)
{
    // Create a new hardware frames context suitable for decoding our specified format
    av_buffer_unref(&context->hw_frames_ctx);
    int err = avcodec_get_hw_frames_parameters(context, m_HwDeviceContext, pixelFormat, &context->hw_frames_ctx);
    if (err < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to get hwframes context parameters: %d",
                     err);
        return false;
    }

    auto framesContext = (AVHWFramesContext*)context->hw_frames_ctx->data;
    auto d3d11vaFramesContext = (AVD3D11VAFramesContext*)framesContext->hwctx;

    // If we're binding output textures directly, we need to add the SRV bind flag
    if (m_BindDecoderOutputTextures) {
        d3d11vaFramesContext->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }

    // If we're using separate decode and render devices, we need to create shared textures
    if (m_DecodeDevice != m_RenderDevice) {
        d3d11vaFramesContext->MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    }

    // Mimic the logic in ff_decode_get_hw_frames_ctx() which adds an extra 3 frames
    if (framesContext->initial_pool_size) {
        framesContext->initial_pool_size += 3;
    }

    err = av_hwframe_ctx_init(context->hw_frames_ctx);
    if (err < 0) {
        av_buffer_unref(&context->hw_frames_ctx);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed initialize hwframes context: %d",
                     err);
        return false;
    }

    if (!setupFrameRenderingResources(framesContext)) {
        av_buffer_unref(&context->hw_frames_ctx);
        return false;
    }

    return true;
}

void D3D11VARenderer::renderFrame(AVFrame* frame)
{
    // Acquire the context lock for rendering to prevent concurrent
    // access from inside FFmpeg's decoding code
    if (m_DecodeDevice == m_RenderDevice) {
        lockContext(this);
    }

    // Nonary's refactor: preparation and the final Present are helpers shared with the
    // VRR backend. prepareFrameForPresent() does the clear, the bind, the video, the
    // overlays and the colourspace switch that used to sit inline here.
    bool prepared = prepareFrameForPresent(frame);

    // ⚠️ 5.6.0 Fractional V-Sync (§64) survives this refactor in exactly ONE line: the
    // sync interval. Nonary always passes 0 here. m_SyncInterval is 0 by default and 2..4
    // only when the fractional gate in initialize() applied, so passing 0 would delete the
    // feature in silence while leaving its setting, its host-profile override and its log
    // line in place — the worst possible way for it to disappear.
    //
    // The timestamps stay tight around the Present so the diagnostic measures the block on
    // V-blank alone, not the submission work before it. Frame statistics are read after
    // the context lock is released below: GetFrameStatistics() has no business holding it.
    uint64_t presentStartUs = m_CadenceDiagEnabled ? LiGetMicroseconds() : 0;
    HRESULT hr = prepared ? presentPreparedFrame({static_cast<unsigned int>(m_SyncInterval), legacyPresentFlags()})
                          : E_FAIL;
    uint64_t presentEndUs = m_CadenceDiagEnabled ? LiGetMicroseconds() : 0;

    if (m_DecodeDevice == m_RenderDevice) {
        // Release the context lock
        unlockContext(this);
    }

    if (FAILED(hr)) {
        if (prepared) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "IDXGISwapChain::Present() failed: %x",
                         hr);
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "D3D11 frame preparation failed");
        }

        // The card may have been removed or crashed. Reset the decoder.
        queueRenderDeviceReset();
        return;
    }

    if (m_CadenceDiagEnabled) {
        sampleCadence(presentStartUs, presentEndUs);
    }
}

// Reads back what the display pipeline did with the present we just made: how many
// V-blanks the previous frame occupied and how deep the DXGI present queue is sitting.
//
// ⚠️ Nothing here logs, and nothing here may ever log. This runs on the render thread,
// inside the span the Pacer reports as "rendering time"; the 5.1.x version wrote its
// summary from this path with SDL_LogInfo and inflated the number it existed to explain,
// which is why 5.2.0 deleted it outright. Closed windows are published under
// m_CadenceLock and written out by whoever reads them.
void D3D11VARenderer::sampleCadence(uint64_t presentStartUs, uint64_t presentEndUs)
{
    CadenceDiag& c = m_Cadence;

    DXGI_FRAME_STATISTICS stats;
    HRESULT hr = m_SwapChain->GetFrameStatistics(&stats);
    if (hr == DXGI_ERROR_FRAME_STATISTICS_DISJOINT) {
        // The presentation counters were reset out from under us: a mode change, a
        // full-screen transition, or the display going to standby and coming back.
        c.disjointCount++;
        c.haveBaseline = false;
        return;
    }
    else if (FAILED(hr)) {
        // Some drivers simply do not answer. Stop asking rather than pay for a failing
        // call once per frame for the rest of the session.
        if (++c.consecutiveFailures >= 60) {
            m_CadenceDiagEnabled = false;
        }
        return;
    }

    c.consecutiveFailures = 0;
    c.presentCalls++;

    uint64_t waitUs = presentEndUs - presentStartUs;
    c.waitSumUs += waitUs;
    if (c.presentCalls == 1 || waitUs < c.waitMinUs) {
        c.waitMinUs = waitUs;
    }
    if (waitUs > c.waitMaxUs) {
        c.waitMaxUs = waitUs;
    }

    if (m_DisplayHz > 0) {
        // Half a frame period, not one and a half. Present() blocking *at all* is the
        // failure signature, and the 5.1.2 bar sat above the worst case ever measured -
        // 18 ms against a 25 ms threshold - so that counter read zero while the bug was
        // at full strength.
        uint64_t framePeriodUs = (1000000ULL * (m_SyncInterval >= 1 ? m_SyncInterval : 1)) / m_DisplayHz;
        if (waitUs > framePeriodUs / 2) {
            c.waitOverCount++;
        }
    }

    if (c.haveBaseline) {
        c.presentsSubmitted++;

        uint32_t deltaPresents = stats.PresentCount - c.lastStats.PresentCount;
        uint32_t deltaRefreshes = stats.PresentRefreshCount - c.lastStats.PresentRefreshCount;

        // Both bounds reject a wrapped or garbage delta rather than letting one bad
        // reading poison the mean for the whole window. The refresh bound is not
        // optional: the counters can restart without a DISJOINT being reported, and an
        // unsigned subtraction across that turns into billions.
        if (deltaPresents > 0 && deltaPresents < 1000 && deltaRefreshes < 1000) {
            c.sumPresents += deltaPresents;
            c.sumRefreshes += deltaRefreshes;

            // Only a single completed present tells us exactly what one frame got.
            if (deltaPresents == 1) {
                if (c.minVblanks < 0 || (int)deltaRefreshes < c.minVblanks) {
                    c.minVblanks = (int)deltaRefreshes;
                }
                if ((int)deltaRefreshes > c.maxVblanks) {
                    c.maxVblanks = (int)deltaRefreshes;
                }

                // A frame held for a different number of V-blanks than we asked for is
                // the difference between an ugly average and a visible stutter, so they
                // are counted separately instead of only moving the mean.
                if (m_CadenceRefreshTrusted && m_SyncInterval >= 2 &&
                        (int)deltaRefreshes != m_SyncInterval) {
                    c.slips++;
                }
            }
        }

        // How far ahead of the display we are: presents we have handed to DXGI against
        // the ones it reports as done with. If the queue settles at a depth when the
        // pipeline starts and stays there, this is the number that shows it.
        //
        // NB: an earlier version subtracted the two refresh counters in the statistics
        // instead. That reads a constant zero on real hardware - they are sampled at the
        // same instant - so it measured nothing at all.
        uint32_t completedPresents = stats.PresentCount - c.baselinePresentCount;
        int queueDepth = (int)((int64_t)c.presentsSubmitted - (int64_t)completedPresents);
        if (queueDepth >= 0 && queueDepth < 100) {
            if (c.queueSamples == 0 || queueDepth < c.queueMin) {
                c.queueMin = queueDepth;
            }
            if (c.queueSamples == 0 || queueDepth > c.queueMax) {
                c.queueMax = queueDepth;
            }
            c.queueSum += queueDepth;
            c.queueSamples++;
        }
    }
    else {
        // First reading, or the first after the counters were reset: this present is the
        // one the queue depth will be measured from.
        c.baselinePresentCount = stats.PresentCount;
        c.presentsSubmitted = 1;
    }

    c.lastStats = stats;
    c.haveBaseline = true;

    if (presentEndUs - c.windowStartUs >= 1000000) {
        flushCadenceWindow(presentEndUs);
    }
}

// Closes a one-second window and publishes it. Min and max travel with the mean on
// purpose: the mean is what hid issue #9 in every report it appeared in.
void D3D11VARenderer::flushCadenceWindow(uint64_t nowUs)
{
    CadenceDiag& c = m_Cadence;

    double windowSecs = (double)(nowUs - c.windowStartUs) / 1000000.0;
    double vblanksPerFrame = c.sumPresents != 0 ? (double)c.sumRefreshes / (double)c.sumPresents : 0.0;
    double queueDepth = c.queueSamples != 0 ? (double)c.queueSum / (double)c.queueSamples : 0.0;
    double waitAvgMs = c.presentCalls != 0 ? (double)c.waitSumUs / (double)c.presentCalls / 1000.0 : 0.0;

    SDL_AtomicLock(&m_CadenceLock);
    // A negative cadence is how the reader is told there is no V-blank figure worth
    // drawing; the wait and queue beside it are still real. See initialize().
    m_PublishedCadence.vblanksPerFrame = m_CadenceRefreshTrusted ? vblanksPerFrame : -1.0;
    m_PublishedCadence.minVblanks = (m_CadenceRefreshTrusted && c.minVblanks > 0) ? c.minVblanks : 0;
    m_PublishedCadence.maxVblanks = m_CadenceRefreshTrusted ? c.maxVblanks : 0;
    m_PublishedCadence.queueDepthVblanks = queueDepth;
    m_PublishedCadence.queueMin = c.queueSamples != 0 ? c.queueMin : 0;
    m_PublishedCadence.queueMax = c.queueSamples != 0 ? c.queueMax : 0;
    m_PublishedCadence.presentWaitMs = waitAvgMs;
    m_PublishedCadence.presentWaitMinMs = c.presentCalls != 0 ? (double)c.waitMinUs / 1000.0 : 0.0;
    m_PublishedCadence.presentWaitMaxMs = (double)c.waitMaxUs / 1000.0;
    m_PublishedCadence.windowSecs = windowSecs;
    m_PublishedCadence.presentCalls = c.presentCalls;
    m_PublishedCadence.blockedPresents = c.waitOverCount;
    m_PublishedCadence.cadenceSlips = c.slips;
    m_PublishedCadence.disjointCount = c.disjointCount;
    m_PublishedCadence.syncInterval = m_SyncInterval;
    m_PublishedCadence.sequence = ++m_CadenceSequence;
    m_HavePublishedCadence = (c.presentCalls != 0);
    SDL_AtomicUnlock(&m_CadenceLock);

    // Start a new window, carrying over what has to survive it: the statistics baseline
    // (or every window would throw away its first frame) and the disjoint count, which
    // is a session total rather than a per-window one.
    DXGI_FRAME_STATISTICS lastStats = c.lastStats;
    bool haveBaseline = c.haveBaseline;
    uint32_t disjointCount = c.disjointCount;
    uint64_t presentsSubmitted = c.presentsSubmitted;
    uint32_t baselinePresentCount = c.baselinePresentCount;

    SDL_zero(c);

    c.lastStats = lastStats;
    c.haveBaseline = haveBaseline;
    c.disjointCount = disjointCount;
    c.presentsSubmitted = presentsSubmitted;
    c.baselinePresentCount = baselinePresentCount;
    c.minVblanks = -1;
    c.windowStartUs = nowUs;
}

bool D3D11VARenderer::getPacingMeasurement(PPACING_MEASUREMENT measurement)
{
    if (!m_CadenceDiagEnabled) {
        return false;
    }

    SDL_AtomicLock(&m_CadenceLock);
    bool haveMeasurement = m_HavePublishedCadence;
    if (haveMeasurement) {
        *measurement = m_PublishedCadence;
    }
    SDL_AtomicUnlock(&m_CadenceLock);

    return haveMeasurement;
}

void D3D11VARenderer::renderOverlay(Overlay::OverlayType type)
{
    if (!Session::get()->getOverlayManager().isOverlayEnabled(type)) {
        // Forget what we last drew. An overlay that has been switched off must not be able
        // to reappear on a frame that misses the lock, and there is no reason to keep its
        // texture alive for the rest of the session.
        m_LastOverlay[type] = {};
        return;
    }

    ComPtr<ID3D11Texture2D> overlayTexture;
    ComPtr<ID3D11Buffer> overlayVertexBuffer;
    ComPtr<ID3D11ShaderResourceView> overlayTextureResourceView;

    // ⚠️ A frame that cannot take the lock draws the previous frame's objects instead of
    // skipping. Skipping means the overlay is absent for that frame, and one absent frame
    // is a blink - the stats overlay is rebuilt once a second, so it blinked about that
    // often. The references we hold here are what keep the objects alive even if the
    // update thread releases its own in the meantime.
    if (SDL_AtomicTryLock(&m_OverlayLock)) {
        overlayTexture = m_OverlayTextures[type];
        overlayVertexBuffer = m_OverlayVertexBuffers[type];
        overlayTextureResourceView = m_OverlayTextureResourceViews[type];
        SDL_AtomicUnlock(&m_OverlayLock);

        // Remember them for the frames that miss. Assigning unconditionally is deliberate:
        // reading a null trio (an overlay just enabled, or one whose texture failed to
        // build) must clear the memory rather than resurrect what was there before.
        m_LastOverlay[type].texture = overlayTexture;
        m_LastOverlay[type].vertexBuffer = overlayVertexBuffer;
        m_LastOverlay[type].resourceView = overlayTextureResourceView;
    }
    else {
        overlayTexture = m_LastOverlay[type].texture;
        overlayVertexBuffer = m_LastOverlay[type].vertexBuffer;
        overlayTextureResourceView = m_LastOverlay[type].resourceView;
    }

    if (!overlayTexture) {
        return;
    }

    // If there was a texture, there must also be a vertex buffer and SRV
    SDL_assert(overlayVertexBuffer);
    SDL_assert(overlayTextureResourceView);

    // Bind vertex buffer
    UINT stride = sizeof(VERTEX);
    UINT offset = 0;
    m_RenderDeviceContext->IASetVertexBuffers(0, 1, overlayVertexBuffer.GetAddressOf(), &stride, &offset);

    // Bind pixel shader and resources
    m_RenderDeviceContext->PSSetShader(m_OverlayPixelShader.Get(), nullptr, 0);
    m_RenderDeviceContext->PSSetShaderResources(0, 1, overlayTextureResourceView.GetAddressOf());

    // Draw the overlay with alpha blending
    m_RenderDeviceContext->OMSetBlendState(m_OverlayBlendState.Get(), nullptr, 0xffffffff);
    m_RenderDeviceContext->DrawIndexed(6, 0, 0);
    m_RenderDeviceContext->OMSetBlendState(m_VideoBlendState.Get(), nullptr, 0xffffffff);
}

void D3D11VARenderer::bindVideoVertexBuffer(bool frameChanged, AVFrame* frame)
{
    if (frameChanged || !m_VideoVertexBuffer) {
        // Scale video to the window size while preserving aspect ratio
        SDL_Rect src, dst;
        src.x = src.y = 0;
        src.w = frame->width;
        src.h = frame->height;
        dst.x = dst.y = 0;
        dst.w = m_DisplayWidth;
        dst.h = m_DisplayHeight;
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        // Convert screen space to normalized device coordinates
        SDL_FRect renderRect;
        StreamUtils::screenSpaceToNormalizedDeviceCoords(&dst, &renderRect, m_DisplayWidth, m_DisplayHeight);

        // Don't sample from the alignment padding area
        auto framesContext = (AVHWFramesContext*)frame->hw_frames_ctx->data;
        float uMax = (float)frame->width / framesContext->width;
        float vMax = (float)frame->height / framesContext->height;

        VERTEX verts[] =
        {
            {renderRect.x, renderRect.y, 0, vMax},
            {renderRect.x, renderRect.y+renderRect.h, 0, 0},
            {renderRect.x+renderRect.w, renderRect.y, uMax, vMax},
            {renderRect.x+renderRect.w, renderRect.y+renderRect.h, uMax, 0},
        };

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(verts);
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = 0;
        vbDesc.MiscFlags = 0;
        vbDesc.StructureByteStride = sizeof(VERTEX);

        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = verts;

        HRESULT hr = m_RenderDevice->CreateBuffer(&vbDesc, &vbData, &m_VideoVertexBuffer);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateBuffer() failed: %x",
                         hr);
            return;
        }
    }

    // Bind video rendering vertex buffer
    UINT stride = sizeof(VERTEX);
    UINT offset = 0;
    m_RenderDeviceContext->IASetVertexBuffers(0, 1, m_VideoVertexBuffer.GetAddressOf(), &stride, &offset);
}

void D3D11VARenderer::bindColorConversion(bool frameChanged, AVFrame* frame)
{
    bool yuv444 = (m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_YUV444);
    auto framesContext = (AVHWFramesContext*)frame->hw_frames_ctx->data;

    if (yuv444) {
        // We'll need to use one of the 4:4:4 shaders for this pixel format
        switch (m_TextureFormat)
        {
        case DXGI_FORMAT_AYUV:
            m_RenderDeviceContext->PSSetShader(m_VideoPixelShaders[PixelShaders::GENERIC_AYUV].Get(), nullptr, 0);
            break;
        case DXGI_FORMAT_Y410:
            m_RenderDeviceContext->PSSetShader(m_VideoPixelShaders[PixelShaders::GENERIC_Y410].Get(), nullptr, 0);
            break;
        default:
            SDL_assert(false);
        }
    }
    else {
        // We'll need to use the generic 4:2:0 shader for this colorspace and color range combo
        m_RenderDeviceContext->PSSetShader(m_VideoPixelShaders[PixelShaders::GENERIC_YUV_420].Get(), nullptr, 0);
    }

    // If nothing has changed since last frame, we're done
    if (!frameChanged) {
        return;
    }

    D3D11_BUFFER_DESC constDesc = {};
    constDesc.ByteWidth = sizeof(CSC_CONST_BUF);
    constDesc.Usage = D3D11_USAGE_IMMUTABLE;
    constDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constDesc.CPUAccessFlags = 0;
    constDesc.MiscFlags = 0;

    CSC_CONST_BUF constBuf = {};
    std::array<float, 9> cscMatrix;
    std::array<float, 3> yuvOffsets;
    getFramePremultipliedCscConstants(frame, cscMatrix, yuvOffsets);

    std::copy(yuvOffsets.cbegin(), yuvOffsets.cend(), constBuf.offsets);

    // We need to adjust our CSC matrix to be column-major and with float3 vectors
    // padded with a float in between each of them to adhere to HLSL requirements.
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            constBuf.cscMatrix[i * 4 + j] = cscMatrix[j * 3 + i];
        }
    }

    std::array<float, 2> chromaOffset;
    getFrameChromaCositingOffsets(frame, chromaOffset);
    constBuf.chromaOffset[0] = chromaOffset[0] / framesContext->width;
    constBuf.chromaOffset[1] = chromaOffset[1] / framesContext->height;

    // Limit chroma texcoords to avoid sampling from alignment texels
    constBuf.chromaUVMax[0] = frame->width != framesContext->width ?
                                  ((float)(frame->width - 1) / framesContext->width) : 1.0f;
    constBuf.chromaUVMax[1] = frame->height != (int)framesContext->height ?
                                  ((float)(frame->height - 1) / framesContext->height) : 1.0f;

    D3D11_SUBRESOURCE_DATA constData = {};
    constData.pSysMem = &constBuf;

    ComPtr<ID3D11Buffer> constantBuffer;
    HRESULT hr = m_RenderDevice->CreateBuffer(&constDesc, &constData, &constantBuffer);
    if (SUCCEEDED(hr)) {
        m_RenderDeviceContext->PSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateBuffer() failed: %x",
                     hr);
        return;
    }
}

uint64_t D3D11VARenderer::captureDecodeBoundary()
{
    if (m_DecodeDevice == m_RenderDevice ||
            m_DecodeDeviceContext == nullptr ||
            m_DecodeD2RFence == nullptr) {
        return 0;
    }

    // This runs at decoder output, before a successor can enqueue more decode
    // commands. Preserve that exact GPU boundary so rendering this frame does
    // not accidentally wait for every newer decode already in flight.
    lockContext(this);
    const UINT64 fenceValue = m_D2RFenceValue++;
    const HRESULT hr = m_DecodeDeviceContext->Signal(
        m_DecodeD2RFence.Get(), fenceValue);
    unlockContext(this);

    if (FAILED(hr)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR decode-boundary Signal() failed: %x", hr);
        return 0;
    }

    return fenceValue;
}

uint64_t D3D11VARenderer::waitForDecode(AVFrame*, uint64_t decodeBoundary)
{
    if (decodeBoundary == 0 || m_DecodeD2RFence == nullptr ||
            m_VrrPresentReadyFenceEvent == nullptr || !m_VrrPresentReadyAvailable) {
        return 0;
    }

    const auto startUs = LiGetMicroseconds();
    const auto completed = m_DecodeD2RFence->GetCompletedValue();
    if (completed != (std::numeric_limits<UINT64>::max)() && completed >= decodeBoundary)
        return 0;

    // The decoder context already signalled this value at frame admission.
    // No context lock or new signal is needed here. Waiting on the captured
    // fence before scheduling prevents asynchronous decode from inflating
    // both the render lead and the FIFO rendering-cost predictor.
    const bool useEvent = m_FenceType == SupportedFenceType::Monitored;
    HRESULT eventResult = S_OK;
    if (useEvent) {
        // Decode and render waits are sequential on this worker. The shared
        // auto-reset event is only a wake hint; either fence's delayed event
        // may wake us, but only the requested fence value can finish the wait.
        eventResult = m_DecodeD2RFence->SetEventOnCompletion(
            decodeBoundary, m_VrrPresentReadyFenceEvent);
    }
    DWORD lastEventResult = WAIT_TIMEOUT;
    const auto result = D3D11FenceWait::wait(decodeBoundary, LiGetMicroseconds,
        [&] { return m_DecodeD2RFence->GetCompletedValue(); },
        [&](unsigned timeoutMs) {
            if (!useEvent || FAILED(eventResult)) {
                Sleep(timeoutMs);
                return true;
            }
            lastEventResult = WaitForSingleObject(m_VrrPresentReadyFenceEvent, timeoutMs);
            return lastEventResult == WAIT_OBJECT_0 || lastEventResult == WAIT_TIMEOUT;
        });
    if (result.status != D3D11FenceWait::Status::Complete) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "D3D11 VRR decode-ready fence wait failed (target=%llu completed=%llu device=%x)",
            static_cast<unsigned long long>(decodeBoundary),
            static_cast<unsigned long long>(result.completedValue),
            m_DecodeDevice->GetDeviceRemovedReason());
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "D3D11 VRR decode-ready wait detail: stop=%s elapsed_us=%llu wait_calls=%u event_used=%d event_setup=%x event=%lu render_device=%x",
            D3D11FenceWait::stopReasonName(result.stopReason),
            static_cast<unsigned long long>(result.elapsedUs), result.waitCalls,
            useEvent && SUCCEEDED(eventResult), eventResult,
            static_cast<unsigned long>(lastEventResult),
            m_RenderDevice->GetDeviceRemovedReason());
        m_VrrPresentReadyAvailable = false;
        m_VrrFallbackReason = VrrFallbackReason::AdaptivePresentationUnavailable;
        queueRenderDeviceReset();
        return 0; // Failure must never advertise GPU readiness.
    }
    return LiGetMicroseconds() - startUs;
}

void D3D11VARenderer::renderVideo(AVFrame* frame, uint64_t decodeBoundary)
{
    // Insert a fence to force the render context to wait for the decode context to finish writing
    if (m_DecodeDevice != m_RenderDevice) {
        SDL_assert(m_DecodeD2RFence);
        SDL_assert(m_RenderD2RFence);

        if (decodeBoundary != 0) {
            // captureDecodeBoundary() inserted this signal before any later
            // frame could add decoder work. Wait for this frame only.
            const HRESULT hr = m_RenderDeviceContext->Wait(m_RenderD2RFence.Get(),
                                                           decodeBoundary);
            if (FAILED(hr)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 decode-to-render Wait() failed: %x (target=%llu)",
                    hr, static_cast<unsigned long long>(decodeBoundary));
            }
        }
        else {
            // Legacy rendering and a failed boundary capture retain the
            // conservative render-time signal.
            bool acquiredContextLock = false;
            if (!m_VrrContextLocked) {
                lockContext(this);
                acquiredContextLock = true;
            }
            const UINT64 fenceValue = m_D2RFenceValue++;
            const HRESULT signalResult = m_DecodeDeviceContext->Signal(
                m_DecodeD2RFence.Get(), fenceValue);
            if (SUCCEEDED(signalResult)) {
                const HRESULT waitResult = m_RenderDeviceContext->Wait(
                    m_RenderD2RFence.Get(), fenceValue);
                if (FAILED(waitResult)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "D3D11 decode-to-render Wait() failed: %x (target=%llu)",
                        waitResult, static_cast<unsigned long long>(fenceValue));
                }
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 decode-to-render Signal() failed: %x (target=%llu)",
                    signalResult, static_cast<unsigned long long>(fenceValue));
            }
            if (acquiredContextLock) {
                unlockContext(this);
            }
        }
    }

    UINT srvIndex;
    if (m_BindDecoderOutputTextures) {
        // Our indexing logic depends on a direct mapping into m_VideoTextureResourceViews
        // based on the texture index provided by FFmpeg.
        srvIndex = (uintptr_t)frame->data[1];
        SDL_assert(srvIndex < m_VideoTextureResourceViews.size());
        if (srvIndex >= m_VideoTextureResourceViews.size()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unexpected texture index: %u",
                         srvIndex);
            return;
        }
    }
    else {
        // Copy this frame into our video texture
        m_RenderDeviceContext->CopySubresourceRegion1(m_VideoTexture.Get(), 0, 0, 0, 0,
                                                      m_RenderSharedTextureArray.Get(),
                                                      (int)(intptr_t)frame->data[1],
                                                      nullptr, D3D11_COPY_DISCARD);

        // SRV 0 is always mapped to the video texture
        srvIndex = 0;
    }

    bool frameChanged = hasFrameFormatChanged(frame);

    // Bind our vertex buffer
    bindVideoVertexBuffer(frameChanged, frame);

    // Bind our CSC shader (and constant buffer, if required)
    bindColorConversion(frameChanged, frame);

    // Bind SRVs for this frame
    ID3D11ShaderResourceView* frameSrvs[] = { m_VideoTextureResourceViews[srvIndex][0].Get(), m_VideoTextureResourceViews[srvIndex][1].Get() };
    m_RenderDeviceContext->PSSetShaderResources(0, 2, frameSrvs);

    // Draw the video
    m_RenderDeviceContext->DrawIndexed(6, 0, 0);

    // Unbind SRVs for this frame
    ID3D11ShaderResourceView* nullSrvs[2] = {};
    m_RenderDeviceContext->PSSetShaderResources(0, 2, nullSrvs);

    // Insert a fence to force the decode context to wait for the render context to finish reading
    if (m_DecodeDevice != m_RenderDevice) {
        SDL_assert(m_DecodeR2DFence);
        SDL_assert(m_RenderR2DFence);

        // Because Pacer keeps a reference to the current frame until the next frame is rendered,
        // we insert a wait for the previous frame's fence value rather than the current one.
        // This means the fence should generally not cause a pipeline bubble for the decoder
        // unless rendering is taking much longer than expected.
        const HRESULT signalResult = m_RenderDeviceContext->Signal(m_RenderR2DFence.Get(), m_R2DFenceValue);
        if (SUCCEEDED(signalResult)) {
            bool acquiredContextLock = false;
            if (!m_VrrContextLocked) {
                lockContext(this);
                acquiredContextLock = true;
            }
            SDL_assert(m_R2DFenceValue > 0);
            const HRESULT waitResult = m_DecodeDeviceContext->Wait(
                m_DecodeR2DFence.Get(), m_R2DFenceValue - 1);
            if (FAILED(waitResult)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 render-to-decode Wait() failed: %x (target=%llu)",
                    waitResult, static_cast<unsigned long long>(m_R2DFenceValue - 1));
            }
            if (acquiredContextLock) {
                unlockContext(this);
            }
            m_R2DFenceValue++;
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "D3D11 render-to-decode Signal() failed: %x (target=%llu)",
                signalResult, static_cast<unsigned long long>(m_R2DFenceValue));
        }
    }
}

// This function must NOT use any DXGI or ID3D11DeviceContext methods
// since it can be called on an arbitrary thread!
void D3D11VARenderer::notifyOverlayUpdated(Overlay::OverlayType type)
{
    HRESULT hr;

    SDL_Surface* newSurface = Session::get()->getOverlayManager().getUpdatedOverlaySurface(type);
    bool overlayEnabled = Session::get()->getOverlayManager().isOverlayEnabled(type);
    if (newSurface == nullptr && overlayEnabled) {
        // The overlay is enabled and there is no new surface. Leave the old texture alone.
        return;
    }

    // If the overlay is disabled, clear it out and we're done. The objects are moved into
    // locals so that the D3D releases happen once the spinlock is back up.
    if (!overlayEnabled) {
        SDL_AtomicLock(&m_OverlayLock);
        ComPtr<ID3D11Texture2D> oldTexture = std::move(m_OverlayTextures[type]);
        ComPtr<ID3D11Buffer> oldVertexBuffer = std::move(m_OverlayVertexBuffers[type]);
        ComPtr<ID3D11ShaderResourceView> oldTextureResourceView = std::move(m_OverlayTextureResourceViews[type]);
        SDL_AtomicUnlock(&m_OverlayLock);

        SDL_FreeSurface(newSurface);
        return;
    }

    // ⚠️ The old objects stay in place while the new ones are built, and are swapped in one
    // go at the end. They used to be moved out here, at the top, which left
    // m_OverlayTextures[type] null for the whole of CreateTexture2D +
    // CreateShaderResourceView + createOverlayVertexBuffer - uploading roughly a megabyte
    // of surface among them. The render thread would take the lock during that window,
    // find nothing, and draw no overlay: the long half of the blink, and the half the
    // lock itself never explained. The lock is now held only for the swap.

    // Create a texture with our pixel data
    SDL_assert(!SDL_MUSTLOCK(newSurface));
    SDL_assert(newSurface->format->format == SDL_PIXELFORMAT_ARGB8888);

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = newSurface->w;
    texDesc.Height = newSurface->h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA texData = {};
    texData.pSysMem = newSurface->pixels;
    texData.SysMemPitch = newSurface->pitch;

    ComPtr<ID3D11Texture2D> newTexture;
    hr = m_RenderDevice->CreateTexture2D(&texDesc, &texData, &newTexture);
    if (FAILED(hr)) {
        SDL_FreeSurface(newSurface);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateTexture2D() failed: %x",
                     hr);
        return;
    }

    ComPtr<ID3D11ShaderResourceView> newTextureResourceView;
    hr = m_RenderDevice->CreateShaderResourceView((ID3D11Resource*)newTexture.Get(), nullptr, &newTextureResourceView);
    if (FAILED(hr)) {
        SDL_FreeSurface(newSurface);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateShaderResourceView() failed: %x",
                     hr);
        return;
    }

    ComPtr<ID3D11Buffer> newVertexBuffer;
    if (!createOverlayVertexBuffer(type, newSurface->w, newSurface->h, newVertexBuffer)) {
        SDL_FreeSurface(newSurface);
        return;
    }

    // The surface is no longer required
    SDL_FreeSurface(newSurface);
    newSurface = nullptr;

    SDL_AtomicLock(&m_OverlayLock);
    // The outgoing objects are moved into locals rather than dropped here, so that their
    // D3D releases happen below, with the spinlock already back up.
    ComPtr<ID3D11Buffer> oldVertexBuffer = std::move(m_OverlayVertexBuffers[type]);
    ComPtr<ID3D11Texture2D> oldTexture = std::move(m_OverlayTextures[type]);
    ComPtr<ID3D11ShaderResourceView> oldTextureResourceView = std::move(m_OverlayTextureResourceViews[type]);

    m_OverlayVertexBuffers[type] = std::move(newVertexBuffer);
    m_OverlayTextures[type] = std::move(newTexture);
    m_OverlayTextureResourceViews[type] = std::move(newTextureResourceView);
    SDL_AtomicUnlock(&m_OverlayLock);
}

bool D3D11VARenderer::createOverlayVertexBuffer(Overlay::OverlayType type, int width, int height, ComPtr<ID3D11Buffer>& newVertexBuffer)
{
    SDL_FRect renderRect = {};

    // Small inset so the cards don't sit flush against the screen edges.
    const int kEdgeMargin = 12;

    if (type == Overlay::OverlayStatusUpdate) {
        // Bottom-left: in this coord system y=0 is the bottom edge
        renderRect.x = 0;
        renderRect.y = 0;
    }
    else if (type == Overlay::OverlayDebug || type == Overlay::OverlayStreamSettings) {
        // Opposite top corners — the stats card takes the one the user chose, the settings
        // panel the other. y=0 is the bottom edge here, so the top of the screen is a full
        // overlay height down from m_DisplayHeight.
        renderRect.x = (float)Overlay::OverlayManager::getOverlayOriginX(type, m_DisplayWidth, width, kEdgeMargin);
        renderRect.y = (float)(m_DisplayHeight - height - kEdgeMargin);
    }

    renderRect.w = width;
    renderRect.h = height;

    // Convert screen space to normalized device coordinates
    StreamUtils::screenSpaceToNormalizedDeviceCoords(&renderRect, m_DisplayWidth, m_DisplayHeight);

    VERTEX verts[] =
    {
        {renderRect.x, renderRect.y, 0, 1},
        {renderRect.x, renderRect.y+renderRect.h, 0, 0},
        {renderRect.x+renderRect.w, renderRect.y, 1, 1},
        {renderRect.x+renderRect.w, renderRect.y+renderRect.h, 1, 0},
    };

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(verts);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = 0;
    vbDesc.MiscFlags = 0;
    vbDesc.StructureByteStride = sizeof(VERTEX);

    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = verts;

    HRESULT hr = m_RenderDevice->CreateBuffer(&vbDesc, &vbData, &newVertexBuffer);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateBuffer() failed: %x",
                     hr);
        return false;
    }

    return true;
}

bool D3D11VARenderer::notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO stateInfo)
{
    if (stateInfo->stateChangeFlags & WINDOW_STATE_CHANGE_DISPLAY) {
        if (m_CompositionPresenter.active()) {
            // Recreate the manager for the new output. Its statistics identity
            // and directly displayable allocations belong to the old output.
            return false;
        }
        int adapterIndex, outputIndex;
        if (!SDL_DXGIGetOutputInfo(stateInfo->displayIndex,
                                   &adapterIndex, &outputIndex)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_DXGIGetOutputInfo() failed: %s",
                         SDL_GetError());
            return false;
        }

        // If the window moved to a different GPU, recreate the renderer
        // to see if we can use that new GPU for decoding
        if (adapterIndex != m_AdapterIndex) {
            return false;
        }

        // If an adapter was added or removed, we can't trust that our
        // old indexes are still valid for comparison.
        if (!m_Factory->IsCurrent()) {
            return false;
        }

        // A same-GPU display move keeps this renderer alive. Serialize the
        // refreshed swapchain eligibility with VRR preparation and Present.
        lockContext(this);
        if (m_VrrFramePrepared) {
            // The worker may be waiting with a prepared back buffer while the
            // decoder-facing context is intentionally unlocked. Discard that
            // image before changing the display epoch.
            releasePreparedVrrFrame();
        }
        refreshVrrDisplayState();
        unlockContext(this);

        // We've handled this state change
        stateInfo->stateChangeFlags &= ~WINDOW_STATE_CHANGE_DISPLAY;
    }

    if (stateInfo->stateChangeFlags & WINDOW_STATE_CHANGE_SIZE) {
        // Resize our swapchain and reconstruct size-dependent resources

        DXGI_SWAP_CHAIN_DESC1 swapchainDesc;
        m_SwapChain->GetDesc1(&swapchainDesc);

        // Lock the context to avoid concurrent rendering
        lockContext(this);

        if (m_VrrFramePrepared) {
            // ResizeBuffers requires every back-buffer binding to be released.
            // The worker will observe the missing prepared frame and cancel
            // its stale presentation decision.
            releasePreparedVrrFrame();
        }

        m_DisplayWidth = stateInfo->width;
        m_DisplayHeight = stateInfo->height;

        // Release the video vertex buffer so we will upload a new one after resize
        m_VideoVertexBuffer.Reset();

        // Create new vertex buffers for active overlays.
        //
        // The remembered objects go too: theirs are the pre-resize vertex buffers, and a
        // frame that missed the lock right after a resize would otherwise draw the overlay
        // at its old screen position. Safe to touch here without the render thread's
        // knowledge because we hold the context lock, which renderFrame() also takes.
        for (auto& last : m_LastOverlay) {
            last = {};
        }

        SDL_AtomicLock(&m_OverlayLock);
        for (size_t i = 0; i < m_OverlayVertexBuffers.size(); i++) {
            if (!m_OverlayTextures[i]) {
                continue;
            }

            D3D11_TEXTURE2D_DESC textureDesc;
            m_OverlayTextures[i]->GetDesc(&textureDesc);
            createOverlayVertexBuffer((Overlay::OverlayType)i, textureDesc.Width, textureDesc.Height, m_OverlayVertexBuffers[i]);
        }
        SDL_AtomicUnlock(&m_OverlayLock);

        // We must release all references to the back buffer
        m_RenderTargetView.Reset();
        m_RenderDeviceContext->Flush();

        HRESULT hr = m_SwapChain->ResizeBuffers(0, stateInfo->width, stateInfo->height, DXGI_FORMAT_UNKNOWN, swapchainDesc.Flags);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "IDXGISwapChain::ResizeBuffers() failed: %x",
                         hr);
            unlockContext(this);
            return false;
        }

        if (m_CompositionPresenter.active()) {
            hr = m_CompositionPresenter.resize(stateInfo->width, stateInfo->height, swapchainDesc.Format);
            if (FAILED(hr)) {
                unlockContext(this);
                return false;
            }
        }

        // Reset swapchain-dependent resources (RTV, viewport, etc)
        if (!setupSwapchainDependentResources()) {
            unlockContext(this);
            return false;
        }

        // A same-monitor mode switch can arrive only as SIZE_CHANGED while
        // retaining the same rounded refresh rate. Refresh the physical
        // DisplayConfig signal and D3DKMT source after ResizeBuffers so a
        // trace cannot silently carry the old active/total geometry into the
        // new mode.
        refreshVrrDisplayState();

        unlockContext(this);

        // We've handled this state change
        stateInfo->stateChangeFlags &= ~WINDOW_STATE_CHANGE_SIZE;
    }

    // Check if we've handled all state changes
    return stateInfo->stateChangeFlags == 0;
}

bool D3D11VARenderer::checkDecoderSupport(IDXGIAdapter* adapter)
{
    HRESULT hr;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;

    DXGI_ADAPTER_DESC adapterDesc;
    hr = adapter->GetDesc(&adapterDesc);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIAdapter::GetDesc() failed: %x",
                     hr);
        return false;
    }

    // Derive a ID3D11VideoDevice from our ID3D11Device.
    hr = m_RenderDevice.As(&videoDevice);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::QueryInterface(ID3D11VideoDevice) failed: %x",
                     hr);
        return false;
    }

    // Check if the format is supported by this decoder
    BOOL supported;
    switch (m_DecoderParams.videoFormat)
    {
    case VIDEO_FORMAT_H264:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_H264_VLD_NOFGT, DXGI_FORMAT_NV12, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support H.264 decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support H.264 decoding to NV12 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_H264_HIGH8_444:
        // Unsupported by DXVA
        return false;

    case VIDEO_FORMAT_H265:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_HEVC_VLD_MAIN, DXGI_FORMAT_NV12, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC decoding to NV12 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_H265_MAIN10:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10, DXGI_FORMAT_P010, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main10 decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main10 decoding to P010 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_H265_REXT8_444:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&k_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN_444, DXGI_FORMAT_AYUV, &supported)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main 444 8-bit decoding via D3D11VA");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main 444 8-bit decoding to AYUV format");
            return false;
        }
        break;

    case VIDEO_FORMAT_H265_REXT10_444:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&k_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10_444, DXGI_FORMAT_Y410, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main 444 10-bit decoding via D3D11VA");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main 444 10-bit decoding to Y410 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_AV1_MAIN8:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0, DXGI_FORMAT_NV12, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 decoding to NV12 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_AV1_MAIN10:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0, DXGI_FORMAT_P010, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 Main 10-bit decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 Main 10-bit decoding to P010 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_AV1_HIGH8_444:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_AV1_VLD_PROFILE1, DXGI_FORMAT_AYUV, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 High 444 8-bit decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 High 444 8-bit decoding to AYUV format");
            return false;
        }
        break;

    case VIDEO_FORMAT_AV1_HIGH10_444:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_AV1_VLD_PROFILE1, DXGI_FORMAT_Y410, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 High 444 10-bit decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 High 444 10-bit decoding to Y410 format");
            return false;
        }
        break;

    default:
        SDL_assert(false);
        return false;
    }

    if (DXUtil::isFormatHybridDecodedByHardware(m_DecoderParams.videoFormat, adapterDesc.VendorId, adapterDesc.DeviceId)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "GPU decoding for format %x is blocked due to hardware limitations",
                    m_DecoderParams.videoFormat);
        return false;
    }

    return true;
}

int D3D11VARenderer::getRendererAttributes()
{
    int attributes = 0;

    // This renderer supports HDR
    attributes |= RENDERER_ATTRIBUTE_HDR_SUPPORT;

    // ⚠️ Nothing is announced here for fractional V-Sync, and that is the design rather
    // than an omission. 5.1.x returned RENDERER_ATTRIBUTE_SELF_PACING whenever the sync
    // interval was >= 2, which took the software Pacer away and left Present() as the
    // render loop's only clock - the arrangement issue #9 grew out of. Here the interval
    // is applied underneath a Pacer that keeps running, which is what Special-K does to
    // us from outside and what this experiment exists to reproduce natively. Adding an
    // attribute here would undo the whole thing.

    // This renderer requires frame pacing to synchronize with VBlank when we're in full-screen.
    // In windowed mode, we will render as fast we can and DWM will grab whatever is latest at the
    // time unless the user opts for pacing. We will use pacing in full-screen mode and normal DWM
    // sequencing in full-screen desktop mode to behave similarly to the DXVA2 renderer.
    if ((SDL_GetWindowFlags(m_DecoderParams.window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN) {
        attributes |= RENDERER_ATTRIBUTE_FORCE_PACING;
    }

    return attributes;
}

int D3D11VARenderer::getDecoderCapabilities()
{
    return CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC |
           CAPABILITY_REFERENCE_FRAME_INVALIDATION_AV1;
}

IFFmpegRenderer::InitFailureReason D3D11VARenderer::getInitFailureReason()
{
    // In the specific case where we found at least one D3D11 hardware device but none of the
    // enumerated devices have support for the specified codec, tell the FFmpeg decoder not to
    // bother trying other hwaccels. We don't want to try loading D3D9 if the device doesn't
    // even have hardware support for the codec.
    //
    // NB: We use feature level 11.0 support as a gate here because we want to avoid returning
    // this failure reason in cases where we might have an extremely old GPU with support for
    // DXVA2 on D3D9 but not D3D11VA on D3D11. I'm unsure if any such drivers/hardware exists,
    // but better be safe than sorry.
    //
    // NB2: We're also assuming that no GPU exists which lacks any D3D11 driver but has drivers
    // for non-DX APIs like Vulkan. I believe this is a Windows Logo requirement so it should be
    // safe to assume.
    //
    // NB3: Sigh, there *are* GPUs drivers with greater codec support available via Vulkan than
    // D3D11VA even when both D3D11 and Vulkan APIs are supported. This is the case for HEVC RExt
    // profiles that were not supported by Microsoft until the Windows 11 24H2 SDK. Don't report
    // that hardware support is missing for YUV444 profiles since the Vulkan driver may support it.
    if (m_DevicesWithFL11Support != 0 && m_DevicesWithCodecSupport == 0 && !(m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_YUV444)) {
        return InitFailureReason::NoHardwareSupport;
    }
    else {
        return InitFailureReason::Unknown;
    }
}

void D3D11VARenderer::lockContext(void *lock_ctx)
{
    auto me = (D3D11VARenderer*)lock_ctx;

    SDL_LockMutex(me->m_ContextLock);
}

void D3D11VARenderer::unlockContext(void *lock_ctx)
{
    auto me = (D3D11VARenderer*)lock_ctx;

    SDL_UnlockMutex(me->m_ContextLock);
}

void D3D11VARenderer::initializeVrrPresentationState(SDL_Window* window,
                                                       DXGI_SWAP_CHAIN_DESC1* swapChainDesc)
{
    m_AllowTearing = false;
    m_VrrTearingSupported = false;
    m_VrrBorderlessFlipModel = false;
    m_VrrSameGpuOutput = false;
    m_VrrSwapChainAllowsTearing = false;
    m_VrrTearingFeatureQueryResultValid = false;
    m_VrrTearingFeatureQueryResult = 0;
    m_VrrTearingFeatureAllowsTearing = false;
    m_VrrSwapChainDescQueryResultValid = false;
    m_VrrSwapChainDescQueryResult = 0;
    m_VrrSwapChainFlags = 0;
    m_VrrSwapChainSwapEffect = 0;
    m_VrrFullscreenStateQueryResultValid = false;
    m_VrrFullscreenStateQueryResult = 0;
    m_VrrFullscreenExclusive = false;
    m_VrrWindowFlags = window != nullptr ?
        SDL_GetWindowFlags(window) : 0;
    m_VrrDesktopMonitorCount = static_cast<UINT>(
        std::max(0, GetSystemMetrics(SM_CMONITORS)));
    m_VrrWindowHandle = nullptr;
    m_VrrDisplayTiming = {};
    closeVrrRasterSource();
    const char* rasterSamplingEnv = SDL_getenv("MOONLIGHT_VRR_ALIGN");
    m_VrrRasterSamplingRequested =
        rasterSamplingEnv != nullptr &&
        rasterSamplingEnv[0] == '1' &&
        rasterSamplingEnv[1] == '\0';
    SDL_SysWMinfo windowInfo;
    SDL_VERSION(&windowInfo.version);
    if (window != nullptr &&
            SDL_GetWindowWMInfo(window, &windowInfo) &&
            windowInfo.subsystem == SDL_SYSWM_WINDOWS) {
        m_VrrWindowHandle = windowInfo.info.win.window;
    }
    m_VrrSuspended = false;
    m_VrrFallbackReason = VrrFallbackReason::NoFallback;
    m_VrrPresentReadyAvailable = false;
    m_VrrPriorPresentCountValid = false;
    m_VrrPriorPresentCount = 0;
    m_VrrPriorFrameStatsValid = false;
    m_VrrPriorFrameStatsPresentCount = 0;
    m_VrrPriorFrameStatsTimeUs = 0;
    m_VrrPriorFrameStatsPresentRefreshSequence = 0;
    m_VrrPriorFrameStatsRefreshSequence = 0;

    if (m_DecoderParams.enableVrr) {
        // Prime the process-wide correlation and display snapshot during
        // renderer setup so the first deeply traced Present does not pay
        // one-time initialization cost on the pacing thread.
        (void)vrrQpcClockCorrelation();
        refreshVrrDisplayTiming();
    }

    // Preserve the legacy non-VSync path while also creating an
    // allow-tearing flip swapchain for an explicitly requested VRR session.
    // The latter is required even though the session's effective V-Sync is
    // true: the VRR worker always uses immediate presentation and owns the
    // complete mathematical pacing policy.
    if (!m_DecoderParams.enableVsync || m_DecoderParams.enableVrr) {
        BOOL allowTearing = FALSE;
        HRESULT hr = m_Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                    &allowTearing,
                                                    sizeof(allowTearing));
        m_VrrTearingFeatureQueryResultValid = true;
        m_VrrTearingFeatureQueryResult = static_cast<int64_t>(hr);
        m_VrrTearingFeatureAllowsTearing = allowTearing != FALSE;
        if (SUCCEEDED(hr) && allowTearing) {
            m_VrrTearingSupported = true;
            swapChainDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            m_VrrSwapChainAllowsTearing = true;

            // Do not alter legacy V-Sync semantics.  Only the existing
            // non-VSync path uses this flag from renderFrame().
            if (!m_DecoderParams.enableVsync) {
                m_AllowTearing = true;
            }
        }
        else if (!m_DecoderParams.enableVsync) {
            if (SUCCEEDED(hr)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "OS/GPU doesn't support DXGI_FEATURE_PRESENT_ALLOW_TEARING");
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "IDXGIFactory::CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING) failed: %x",
                             hr);
            }
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "D3D11 VRR unavailable: DXGI_FEATURE_PRESENT_ALLOW_TEARING is unavailable (%x)",
                        hr);
        }
    }

    if (!m_DecoderParams.enableVrr) {
        return;
    }

    m_VrrPresentReadyAvailable = initializeVrrPresentReadyFence();
    if (!m_VrrPresentReadyAvailable) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR unavailable: GPU present-ready fencing is unavailable");
    }

    m_VrrBorderlessFlipModel = isBorderlessFullscreenWindow(window) &&
        (swapChainDesc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD ||
         swapChainDesc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
    m_VrrSameGpuOutput = m_RenderAdapterIndex >= 0 &&
        m_RenderAdapterIndex == m_AdapterIndex;
    m_VrrFallbackReason = evaluateVrrEligibility(false);
    switch (m_VrrFallbackReason) {
    case VrrFallbackReason::NoFallback:
        break;
    case VrrFallbackReason::IneffectiveVsync:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                     "D3D11 VRR unavailable: effective V-sync is disabled");
        break;
    case VrrFallbackReason::UnsupportedRenderer:
        if (!m_VrrBorderlessFlipModel) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "D3D11 VRR unavailable: a borderless flip-model swapchain is required");
        }
        else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "D3D11 VRR unavailable: render adapter %d does not own output adapter %d",
                        m_RenderAdapterIndex,
                        m_AdapterIndex);
        }
        break;
    case VrrFallbackReason::InvalidRefresh:
        // Session already performs the strict query exactly once and preserves
        // the result for every renderer recreation. Do not silently substitute
        // a new display value or the legacy 60 Hz fallback here.
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR unavailable: the active display has no valid refresh rate");
        break;
    case VrrFallbackReason::MainThreadRenderer:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR unavailable: the renderer cannot stay on the pacing worker thread");
        break;
    case VrrFallbackReason::AdaptivePresentationUnavailable:
        if (!m_VrrPresentReadyAvailable) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "D3D11 VRR unavailable: GPU present-ready fencing is unavailable");
        }
        else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "D3D11 VRR unavailable: DXGI tearing support is unavailable");
        }
        break;
    case VrrFallbackReason::InitializationFailed:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR unavailable: the flip-model swapchain lacks DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING");
        break;
    default:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR unavailable: %s",
                    vrrFallbackReasonName(m_VrrFallbackReason));
        break;
    }
}

void D3D11VARenderer::refreshVrrDisplayState()
{
    if (!m_DecoderParams.enableVrr) {
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    HRESULT swapChainDescResult = E_POINTER;
    if (m_SwapChain != nullptr) {
        swapChainDescResult = m_SwapChain->GetDesc1(&swapChainDesc);
    }
    m_VrrSwapChainDescQueryResultValid = m_SwapChain != nullptr;
    m_VrrSwapChainDescQueryResult =
        static_cast<int64_t>(swapChainDescResult);
    const bool gotSwapChainDesc = swapChainDescResult == S_OK;
    m_VrrSwapChainFlags =
        gotSwapChainDesc ? swapChainDesc.Flags : 0;
    m_VrrSwapChainSwapEffect =
        gotSwapChainDesc ?
            static_cast<uint32_t>(swapChainDesc.SwapEffect) : 0;

    BOOL fullscreenExclusive = FALSE;
    HRESULT fullscreenStateResult = E_POINTER;
    if (m_SwapChain != nullptr) {
        fullscreenStateResult = m_SwapChain->GetFullscreenState(
            &fullscreenExclusive, nullptr);
    }
    m_VrrFullscreenStateQueryResultValid = m_SwapChain != nullptr;
    m_VrrFullscreenStateQueryResult =
        static_cast<int64_t>(fullscreenStateResult);
    m_VrrFullscreenExclusive =
        fullscreenStateResult == S_OK &&
        fullscreenExclusive != FALSE;
    m_VrrWindowFlags = m_DecoderParams.window != nullptr ?
        SDL_GetWindowFlags(m_DecoderParams.window) : 0;
    m_VrrBorderlessFlipModel = gotSwapChainDesc &&
        fullscreenStateResult == S_OK &&
        !m_VrrFullscreenExclusive &&
        (m_VrrWindowFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) ==
            SDL_WINDOW_FULLSCREEN_DESKTOP &&
        (swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD ||
         swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
    m_VrrSwapChainAllowsTearing = gotSwapChainDesc &&
        (swapChainDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
    m_VrrSameGpuOutput = m_RenderAdapterIndex >= 0 &&
        m_RenderAdapterIndex == m_AdapterIndex;
    m_VrrDesktopMonitorCount = static_cast<UINT>(
        std::max(0, GetSystemMetrics(SM_CMONITORS)));
    refreshVrrDisplayTiming();

    VrrFallbackReason previousReason = m_VrrFallbackReason;
    // Keep the established display-update precedence: an output mismatch is
    // reported before a stale refresh snapshot, whereas initial setup logs the
    // stricter refresh qualification first.
    m_VrrFallbackReason = evaluateVrrEligibility(true);

    if (m_VrrFallbackReason != previousReason) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR display update: %s",
                    vrrFallbackReasonName(m_VrrFallbackReason));
    }
}

VrrFallbackReason D3D11VARenderer::evaluateVrrEligibility(
    bool prioritizeOutputCompatibility)
{
    if (!m_DecoderParams.enableVsync) {
        return VrrFallbackReason::IneffectiveVsync;
    }
    if (!m_VrrBorderlessFlipModel ||
        (prioritizeOutputCompatibility && !m_VrrSameGpuOutput)) {
        return VrrFallbackReason::UnsupportedRenderer;
    }
    if (m_DecoderParams.vrrDisplayRefreshHz <= 0) {
        return VrrFallbackReason::InvalidRefresh;
    }
    if (!m_VrrSameGpuOutput) {
        return VrrFallbackReason::UnsupportedRenderer;
    }
    if (!isRenderThreadSupported()) {
        return VrrFallbackReason::MainThreadRenderer;
    }
    if (!m_VrrPresentReadyAvailable) {
        return VrrFallbackReason::AdaptivePresentationUnavailable;
    }
    if (!m_VrrTearingSupported) {
        return VrrFallbackReason::AdaptivePresentationUnavailable;
    }
    if (!m_VrrSwapChainAllowsTearing) {
        return VrrFallbackReason::InitializationFailed;
    }

    return VrrFallbackReason::NoFallback;
}

void D3D11VARenderer::releasePreparedVrrFrame()
{
    m_CompositionPresenter.cancel();
    // Present() unbinds the render target itself.  A cancellation does not,
    // so explicitly remove the context's reference to the back buffer before
    // a resize/device reset can tear it down.
    if ((m_VrrFramePrepared || m_VrrContextLocked) &&
            m_RenderDeviceContext != nullptr) {
        m_RenderDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    }

    m_VrrFramePrepared = false;
    m_VrrGpuReadyAttempted = false;
    m_VrrGpuReadySignalResultValid = false;
    m_VrrGpuReadySignalResult = 0;
    m_VrrGpuReadySetEventResultValid = false;
    m_VrrGpuReadySetEventResult = 0;
    m_VrrGpuReadyWaitResultValid = false;
    m_VrrGpuReadyWaitResult = 0;
    m_VrrGpuReadyTimingValid = false;
    m_VrrGpuReadySignalStartUs = 0;
    m_VrrGpuReadySignalEndUs = 0;
    m_VrrGpuReadyFlushStartUs = 0;
    m_VrrGpuReadyFlushEndUs = 0;
    m_VrrGpuReadySetEventStartUs = 0;
    m_VrrGpuReadySetEventEndUs = 0;
    m_VrrGpuReadyPollStartUs = 0;
    m_VrrGpuReadyPollEndUs = 0;
    m_VrrGpuReadyFenceValue = 0;
    m_VrrGpuReadyPollCompletedValue = 0;
    m_VrrGpuReadyCompletedBeforeWait = false;
    m_VrrGpuReadyWaitStartUs = 0;
    m_VrrGpuReadyTimeUs = 0;

    if (m_VrrContextLocked) {
        unlockContext(this);
        m_VrrContextLocked = false;
    }
}

void D3D11VARenderer::populateVrrGpuReadyFeedback(
    VrrPresentFeedback& feedback) const
{
    feedback.gpuReadyAttempted = m_VrrGpuReadyAttempted;
    feedback.gpuReadySignalResultValid =
        m_VrrGpuReadySignalResultValid;
    feedback.gpuReadySignalResult = m_VrrGpuReadySignalResult;
    feedback.gpuReadySetEventResultValid =
        m_VrrGpuReadySetEventResultValid;
    feedback.gpuReadySetEventResult = m_VrrGpuReadySetEventResult;
    feedback.gpuReadyWaitResultValid =
        m_VrrGpuReadyWaitResultValid;
    feedback.gpuReadyWaitResult = m_VrrGpuReadyWaitResult;
    feedback.gpuReadyTimingValid = m_VrrGpuReadyTimingValid;
    feedback.gpuReadySignalStartUs = m_VrrGpuReadySignalStartUs;
    feedback.gpuReadySignalEndUs = m_VrrGpuReadySignalEndUs;
    feedback.gpuReadyFlushStartUs = m_VrrGpuReadyFlushStartUs;
    feedback.gpuReadyFlushEndUs = m_VrrGpuReadyFlushEndUs;
    feedback.gpuReadySetEventStartUs =
        m_VrrGpuReadySetEventStartUs;
    feedback.gpuReadySetEventEndUs =
        m_VrrGpuReadySetEventEndUs;
    feedback.gpuReadyPollStartUs = m_VrrGpuReadyPollStartUs;
    feedback.gpuReadyPollEndUs = m_VrrGpuReadyPollEndUs;
    feedback.gpuReadyFenceValue = m_VrrGpuReadyFenceValue;
    feedback.gpuReadyPollCompletedValue =
        m_VrrGpuReadyPollCompletedValue;
    feedback.gpuReadyCompletedBeforeWait =
        m_VrrGpuReadyCompletedBeforeWait;
    feedback.gpuReadyWaitStartUs = m_VrrGpuReadyWaitStartUs;
    feedback.gpuReadyTimeUs = m_VrrGpuReadyTimeUs;
}

void D3D11VARenderer::queueRenderDeviceReset()
{
    SDL_Event event = {};
    event.type = SDL_RENDER_DEVICE_RESET;
    SDL_PushEvent(&event);
}

bool D3D11VARenderer::prepareFrameForPresent(AVFrame* frame,
                                             uint64_t decodeBoundary)
{
    if (frame == nullptr || m_RenderDeviceContext == nullptr ||
            m_RenderTargetView == nullptr || m_SwapChain == nullptr) {
        return false;
    }

    if (m_CompositionPresenter.active()) {
        ComPtr<ID3D11RenderTargetView> view;
        const HRESULT acquireResult = m_CompositionPresenter.acquire(&view);
        if (acquireResult != S_OK) {
            if (FAILED(acquireResult)) queueRenderDeviceReset();
            return false;
        }
        m_RenderTargetView = view;
    }

    // Clear the back buffer.
    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_RenderDeviceContext->ClearRenderTargetView(m_RenderTargetView.Get(), clearColor);

    // Bind the back buffer. This needs to be done each time because Present()
    // unbinds the render target view.
    m_RenderDeviceContext->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), nullptr);

    // Render the video and overlays.  This is the complete preparation phase
    // shared by the legacy and VRR paths; only the final Present is split out.
    renderVideo(frame, decodeBoundary);
    for (int i = 0; i < Overlay::OverlayMax; i++) {
        renderOverlay((Overlay::OverlayType)i);
    }

    if (frame->color_trc != m_LastColorTrc) {
        HRESULT hr;
        if (frame->color_trc == AVCOL_TRC_SMPTE2084) {
            hr = m_CompositionPresenter.active() ?
                m_CompositionPresenter.setColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) :
                m_SwapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
            if (FAILED(hr)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "IDXGISwapChain::SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) failed: %x",
                             hr);
            }
        }
        else {
            hr = m_CompositionPresenter.active() ?
                m_CompositionPresenter.setColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) :
                m_SwapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
            if (FAILED(hr)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "IDXGISwapChain::SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) failed: %x",
                             hr);
            }
        }

        m_LastColorTrc = frame->color_trc;
    }

    return true;
}

bool D3D11VARenderer::initializeVrrPresentReadyFence()
{
    m_VrrPresentReadyFence.Reset();
    if (m_VrrPresentReadyFenceEvent != nullptr) {
        CloseHandle(m_VrrPresentReadyFenceEvent);
        m_VrrPresentReadyFenceEvent = nullptr;
    }
    m_VrrPresentReadyFenceValue = 0;

    HRESULT hr = m_RenderDevice->CreateFence(
        0, D3D11_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&m_VrrPresentReadyFence));
    if (SUCCEEDED(hr)) {
        m_VrrPresentReadyFenceEvent = CreateEventW(
            nullptr, FALSE, FALSE, nullptr);
    }

    if (FAILED(hr) || m_VrrPresentReadyFenceEvent == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR could not create the present-ready fence: %x",
                    hr);
        m_VrrPresentReadyFence.Reset();
        if (m_VrrPresentReadyFenceEvent != nullptr) {
            CloseHandle(m_VrrPresentReadyFenceEvent);
            m_VrrPresentReadyFenceEvent = nullptr;
        }
        return false;
    }

    return true;
}

bool D3D11VARenderer::waitForVrrPresentReady(uint64_t decodeBoundary)
{
    m_VrrGpuReadyAttempted = false;
    m_VrrGpuReadySignalResultValid = false;
    m_VrrGpuReadySignalResult = 0;
    m_VrrGpuReadySetEventResultValid = false;
    m_VrrGpuReadySetEventResult = 0;
    m_VrrGpuReadyWaitResultValid = false;
    m_VrrGpuReadyWaitResult = 0;
    m_VrrGpuReadyTimingValid = false;
    m_VrrGpuReadySignalStartUs = 0;
    m_VrrGpuReadySignalEndUs = 0;
    m_VrrGpuReadyFlushStartUs = 0;
    m_VrrGpuReadyFlushEndUs = 0;
    m_VrrGpuReadySetEventStartUs = 0;
    m_VrrGpuReadySetEventEndUs = 0;
    m_VrrGpuReadyPollStartUs = 0;
    m_VrrGpuReadyPollEndUs = 0;
    m_VrrGpuReadyFenceValue = 0;
    m_VrrGpuReadyPollCompletedValue = 0;
    m_VrrGpuReadyCompletedBeforeWait = false;
    m_VrrGpuReadyWaitStartUs = 0;
    m_VrrGpuReadyTimeUs = 0;
    if (!m_VrrPresentReadyAvailable ||
            m_VrrPresentReadyFence == nullptr ||
            m_VrrPresentReadyFenceEvent == nullptr) {
        return false;
    }

    // A tearing Present does not become scanout-visible until all GPU work
    // targeting its back buffer is complete. vrr8's PresentMon diagnosis
    // measured roughly 2.4 ms between the CPU call and display when this
    // fence was absent, which made timing the CPU call itself insufficient.
    // Finish the frame first so the worker's later target hold operates on an
    // actual flip-ready boundary.
    const UINT64 fenceValue = ++m_VrrPresentReadyFenceValue;
    m_VrrGpuReadyAttempted = true;
    m_VrrGpuReadyFenceValue = fenceValue;
    m_VrrGpuReadySignalStartUs = LiGetMicroseconds();
    HRESULT hr = m_RenderDeviceContext->Signal(
        m_VrrPresentReadyFence.Get(), fenceValue);
    m_VrrGpuReadySignalEndUs = LiGetMicroseconds();
    m_VrrGpuReadySignalResultValid = true;
    m_VrrGpuReadySignalResult = static_cast<int64_t>(hr);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3D11 VRR present-ready Signal() failed: %x", hr);
        m_VrrPresentReadyAvailable = false;
        return false;
    }

    m_VrrGpuReadyFlushStartUs = LiGetMicroseconds();
    m_RenderDeviceContext->Flush();
    m_VrrGpuReadyFlushEndUs = LiGetMicroseconds();
    m_VrrGpuReadySetEventStartUs = LiGetMicroseconds();
    hr = m_VrrPresentReadyFence->SetEventOnCompletion(
        fenceValue, m_VrrPresentReadyFenceEvent);
    m_VrrGpuReadySetEventEndUs = LiGetMicroseconds();
    m_VrrGpuReadySetEventResultValid = true;
    m_VrrGpuReadySetEventResult = static_cast<int64_t>(hr);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3D11 VRR present-ready SetEventOnCompletion() failed: %x",
                     hr);
        m_VrrPresentReadyAvailable = false;
        return false;
    }

    // This mutex is also FFmpeg's decode-device lock. The GPU fence no longer
    // needs CPU access to either immediate context, so never hold the mutex
    // while waiting. On separate devices, retaining it would prevent the
    // decoder thread from capturing the next frame's boundary and collapse
    // the intended decode/render overlap.
    const bool releaseContextWhileWaiting = m_VrrContextLocked;
    if (releaseContextWhileWaiting) {
        unlockContext(this);
        m_VrrContextLocked = false;
    }

    // GetCompletedValue() is a nonblocking observation. If the target value
    // is still incomplete, its call start is a conservative lower bound for
    // the eventual completion. If it is already complete, Signal() call start
    // remains the only defensible lower bound and the poll end is the upper
    // bound. Subsequent polls may tighten completion internally, but retaining
    // this initial bracket keeps the trace's existing conservative bounds.
    m_VrrGpuReadyPollStartUs = LiGetMicroseconds();
    const UINT64 completedValue =
        m_VrrPresentReadyFence->GetCompletedValue();
    m_VrrGpuReadyPollCompletedValue = completedValue;
    m_VrrGpuReadyPollEndUs = LiGetMicroseconds();
    m_VrrGpuReadyCompletedBeforeWait = completedValue >= fenceValue;
    m_VrrGpuReadyWaitStartUs = LiGetMicroseconds();
    DWORD lastEventResult = WAIT_TIMEOUT;
    const auto fenceWait = D3D11FenceWait::wait(fenceValue, LiGetMicroseconds,
        [&] { return m_VrrPresentReadyFence->GetCompletedValue(); },
        [&](unsigned timeoutMs) {
            lastEventResult = WaitForSingleObject(m_VrrPresentReadyFenceEvent, timeoutMs);
            return lastEventResult == WAIT_OBJECT_0 || lastEventResult == WAIT_TIMEOUT;
        });
    // This is the result of the complete fence wait, including completion
    // polling. An individual event timeout is not a fence timeout.
    const DWORD waitResult = fenceWait.status == D3D11FenceWait::Status::Complete ? WAIT_OBJECT_0 :
        fenceWait.status == D3D11FenceWait::Status::Timeout ? WAIT_TIMEOUT : WAIT_FAILED;
    m_VrrGpuReadyWaitResultValid = true;
    m_VrrGpuReadyWaitResult = waitResult;
    m_VrrGpuReadyTimeUs = LiGetMicroseconds();

    if (releaseContextWhileWaiting) {
        lockContext(this);
        m_VrrContextLocked = true;
    }

    if (waitResult != WAIT_OBJECT_0) {
        const uint64_t lockReacquireUs = LiGetMicroseconds() - m_VrrGpuReadyTimeUs;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3D11 VRR present-ready fence wait failed or timed out: %lu (target=%llu completed=%llu event=%lu device=%x)",
                     static_cast<unsigned long>(waitResult),
                     static_cast<unsigned long long>(fenceValue),
                     static_cast<unsigned long long>(fenceWait.completedValue),
                     static_cast<unsigned long>(lastEventResult),
                     m_RenderDevice->GetDeviceRemovedReason());
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "D3D11 VRR present-ready wait detail: stop=%s elapsed_us=%llu wait_calls=%u signal_us=%llu flush_us=%llu event_setup_us=%llu lock_reacquire_us=%llu decode_device=%x separate=%d bind=%d frame_decode_target=%llu",
            D3D11FenceWait::stopReasonName(fenceWait.stopReason),
            static_cast<unsigned long long>(fenceWait.elapsedUs), fenceWait.waitCalls,
            static_cast<unsigned long long>(m_VrrGpuReadySignalEndUs - m_VrrGpuReadySignalStartUs),
            static_cast<unsigned long long>(m_VrrGpuReadyFlushEndUs - m_VrrGpuReadyFlushStartUs),
            static_cast<unsigned long long>(m_VrrGpuReadySetEventEndUs - m_VrrGpuReadySetEventStartUs),
            static_cast<unsigned long long>(lockReacquireUs),
            m_DecodeDevice->GetDeviceRemovedReason(), m_DecodeDevice != m_RenderDevice,
            m_BindDecoderOutputTextures, static_cast<unsigned long long>(decodeBoundary));
        if (m_DecodeDevice != m_RenderDevice) {
            // Failure-only snapshots, observed after reacquiring the context lock.
            // These are not simultaneous GPU observations. A newer decode signal
            // may already be queued; next_signal is not this frame's dependency.
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "D3D11 VRR fence snapshot: d2r_decode=%llu d2r_render=%llu d2r_next_signal=%llu r2d_render=%llu r2d_decode=%llu r2d_next_signal=%llu",
                static_cast<unsigned long long>(m_DecodeD2RFence->GetCompletedValue()),
                static_cast<unsigned long long>(m_RenderD2RFence->GetCompletedValue()),
                static_cast<unsigned long long>(m_D2RFenceValue),
                static_cast<unsigned long long>(m_RenderR2DFence->GetCompletedValue()),
                static_cast<unsigned long long>(m_DecodeR2DFence->GetCompletedValue()),
                static_cast<unsigned long long>(m_R2DFenceValue));
        }
        m_VrrPresentReadyAvailable = false;
        return false;
    }

    m_VrrGpuReadyTimingValid = true;
    return true;
}

HRESULT D3D11VARenderer::presentPreparedFrame(
    const DxgiPresentParameters& parameters)
{
    if (m_SwapChain == nullptr) {
        return E_FAIL;
    }

    if (m_CompositionPresenter.active()) {
        return m_CompositionPresenter.present(m_CompositionPresentId);
    }
    return parameters.present(*m_SwapChain.Get());
}

UINT D3D11VARenderer::legacyPresentFlags() const
{
    if (m_AllowTearing) {
        SDL_assert(!m_DecoderParams.enableVsync);
        return DXGI_PRESENT_ALLOW_TEARING;
    }

    return 0;
}

IVrrFramePresenter* D3D11VARenderer::getVrrFramePresenter()
{
    return this;
}

VrrFallbackReason D3D11VARenderer::checkSupport() const
{
    if (m_VrrFallbackReason != VrrFallbackReason::NoFallback) {
        return m_VrrFallbackReason;
    }

    return m_DecoderParams.enableVrr && m_SwapChain != nullptr &&
        m_VrrSwapChainAllowsTearing ? VrrFallbackReason::NoFallback :
        VrrFallbackReason::InitializationFailed;
}

VrrPrepareResult D3D11VARenderer::prepareFrame(AVFrame* frame,
                                               uint64_t decodeBoundary)
{
    VrrPrepareResult result;
    if (m_VrrSuspended || frame == nullptr) {
        return result;
    }

    // The contract guarantees one worker, but make an accidental second
    // preparation recoverable instead of leaking a retained context lock.
    if (m_VrrFramePrepared) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11 VRR discarded an unpresented prepared frame");
        result.feedback = cancelFrame();
        return result;
    }

    // Serialize rendering preparation with decode and swap-chain state.
    // renderVideo() recognizes this retained lock for the separate-device
    // fence calls, so it never recursively locks the SDL mutex.
    lockContext(this);
    m_VrrContextLocked = true;

    // Display-state changes share this lock with preparation through Present.
    // Check eligibility only after taking it so a UI callback cannot replace
    // swapchain state concurrently.
    if (checkSupport() != VrrFallbackReason::NoFallback) {
        releasePreparedVrrFrame();
        return result;
    }

    if (!prepareFrameForPresent(frame, decodeBoundary)) {
        releasePreparedVrrFrame();
        return result;
    }

    if (!waitForVrrPresentReady(decodeBoundary)) {
        m_VrrFallbackReason = VrrFallbackReason::AdaptivePresentationUnavailable;
        populateVrrGpuReadyFeedback(result.feedback);
        result.feedback.cancelled = true;
        releasePreparedVrrFrame();
        queueRenderDeviceReset();
        return result;
    }

    // The shared-device fence wait temporarily releases the renderer lock.
    // Revalidate state after reacquiring it before publishing this frame.
    if (m_VrrSuspended || checkSupport() != VrrFallbackReason::NoFallback) {
        populateVrrGpuReadyFeedback(result.feedback);
        result.feedback.cancelled = true;
        releasePreparedVrrFrame();
        return result;
    }

    HRESULT deviceReason = m_RenderDevice->GetDeviceRemovedReason();
    if (FAILED(deviceReason)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3D11 VRR preparation detected device loss: %x",
                     deviceReason);
        populateVrrGpuReadyFeedback(result.feedback);
        result.feedback.cancelled = true;
        releasePreparedVrrFrame();
        queueRenderDeviceReset();
        return result;
    }

    m_VrrFramePrepared = true;
    result.prepared = true;
    // The successful path carries the same fence bracket as failure paths so
    // the pacer can learn a bounded GPU-readiness head start from real waits.
    populateVrrGpuReadyFeedback(result.feedback);
    // waitForVrrPresentReady() proves that rendering has finished reading the
    // decoder surface. Present() consumes only the prepared back buffer, so
    // let the worker recycle the source AVFrame before its target wait.
    result.sourceFrameReusable = true;

    // Never hold the FFmpeg/D3D context mutex while the pacing worker waits
    // for its presentation target. Keeping it here serializes D3D11VA decode
    // behind pacing and is especially damaging at 4K high refresh rates.
    unlockContext(this);
    m_VrrContextLocked = false;
    return result;
}

VrrPresentFeedback D3D11VARenderer::presentAdaptive(
    const VrrPresentRequest& request)
{
    VrrPresentFeedback feedback;

    if (!m_VrrContextLocked) {
        lockContext(this);
        m_VrrContextLocked = true;
    }

    if (!m_VrrFramePrepared || m_VrrSuspended) {
        return cancelFrame();
    }

    populateVrrGpuReadyFeedback(feedback);

    if (request.collectDiagnostics) {
        // Reuse the observation made after the previous Present. This is the
        // most recent state available before this call, and avoids adding two
        // extra synchronous DXGI queries to every deeply traced frame.
        if (m_VrrPriorPresentCountValid) {
            feedback.presentCountBeforeValid = true;
            feedback.presentCountBefore = m_VrrPriorPresentCount;
        }
        if (m_VrrPriorFrameStatsValid) {
            feedback.frameStatsBeforeValid = true;
            feedback.frameStatsBeforePresentCount =
                m_VrrPriorFrameStatsPresentCount;
            feedback.frameStatsBeforeTimeUs = m_VrrPriorFrameStatsTimeUs;
            feedback.frameStatsBeforePresentRefreshSequence =
                m_VrrPriorFrameStatsPresentRefreshSequence;
            feedback.frameStatsBeforeRefreshSequence =
                m_VrrPriorFrameStatsRefreshSequence;
        }
    }

    if (m_CompositionPresenter.active()) {
        feedback.nativeBackendValid = true;
        feedback.nativeBackend = VrrNativePresentationBackend::Composition;
        feedback.nativePresentTimingValid = true;
        feedback.nativePresentStartUs = LiGetMicroseconds();
        const HRESULT hr = m_CompositionPresenter.present(m_CompositionPresentId);
        feedback.nativePresentEndUs = LiGetMicroseconds();
        feedback.nativePresentResultValid = true;
        feedback.nativePresentResult = static_cast<int64_t>(hr);
        feedback.presented = hr == S_OK;
        feedback.cancelled = !feedback.presented;
        feedback.submissionTimeValid = feedback.presented;
        feedback.submissionTimeUs = feedback.presented ? feedback.nativePresentStartUs : 0;
        feedback.submissionIdValid = feedback.presented;
        feedback.submissionId = feedback.presented ? m_CompositionPresentId : 0;
        D3D11CompositionPresenter::DisplayedFrame displayed;
        if (m_CompositionPresenter.pollDisplayedFrame(LiGetMicroseconds, displayed)) {
            feedback.latchSampleValid = true;
            feedback.latchTimeKind = Vrr13::PresentationTimeKind::DisplayEvent;
            feedback.latchSubmissionId = displayed.id;
            feedback.latchTimeUs = displayed.clock.timeUs;
            feedback.presentationUncertaintyUs = displayed.clock.uncertaintyUs;
        }
        if (!m_CompositionModeLogged && (m_CompositionPresenter.independentFrames() ||
                                        m_CompositionPresenter.composedFrames() >= 120)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Windows presentation timing observed: %llu independent-flip events, %llu composed events, %llu rejected timestamps (QPC clock)",
                        static_cast<unsigned long long>(m_CompositionPresenter.independentFrames()),
                        static_cast<unsigned long long>(m_CompositionPresenter.composedFrames()),
                        static_cast<unsigned long long>(m_CompositionPresenter.rejectedDisplayFrames()));
            m_CompositionModeLogged = true;
        }
        releasePreparedVrrFrame();
        if (FAILED(hr)) queueRenderDeviceReset();
        return feedback;
    }

    // The risk decision is per frame. Sync interval 1 protects risky frames;
    // other frames retain interval-zero replacement semantics. Active DXGI
    // VRR always permits tearing for those adaptive submissions.
    const auto presentParameters = DxgiPresentParameters::adaptive(
        request.latchedPresentation, DXGI_PRESENT_ALLOW_TEARING);
    feedback.nativeBackendValid = true;
    feedback.nativeBackend = VrrNativePresentationBackend::Dxgi;
    feedback.nativePresentParametersValid = true;
    feedback.nativePresentSyncInterval = presentParameters.syncInterval;
    feedback.nativePresentFlags = presentParameters.flags;
    feedback.nativeVrrStateValid = true;
    feedback.nativeTearingSupported = m_VrrTearingSupported;
    feedback.nativeBorderlessFlipModel = m_VrrBorderlessFlipModel;
    feedback.nativeSameGpuOutput = m_VrrSameGpuOutput;
    feedback.nativeRenderAdapterLuidValid =
        m_VrrRenderAdapterLuidValid;
    feedback.nativeRenderAdapterLuid =
        m_VrrRenderAdapterLuid;
    feedback.nativeSwapChainAllowsTearing =
        m_VrrSwapChainAllowsTearing;
    feedback.nativeTearingFeatureQueryResultValid =
        m_VrrTearingFeatureQueryResultValid;
    feedback.nativeTearingFeatureQueryResult =
        m_VrrTearingFeatureQueryResult;
    feedback.nativeTearingFeatureAllowsTearing =
        m_VrrTearingFeatureAllowsTearing;
    feedback.nativeSwapChainDescQueryResultValid =
        m_VrrSwapChainDescQueryResultValid;
    feedback.nativeSwapChainDescQueryResult =
        m_VrrSwapChainDescQueryResult;
    feedback.nativeSwapChainFlags = m_VrrSwapChainFlags;
    feedback.nativeSwapChainSwapEffect =
        m_VrrSwapChainSwapEffect;
    feedback.nativeFullscreenStateQueryResultValid =
        m_VrrFullscreenStateQueryResultValid;
    feedback.nativeFullscreenStateQueryResult =
        m_VrrFullscreenStateQueryResult;
    feedback.nativeFullscreenExclusive =
        m_VrrFullscreenExclusive;
    feedback.nativeWindowFlags = m_VrrWindowFlags;
    feedback.nativePresentReadyAvailable =
        m_VrrPresentReadyAvailable;
    feedback.nativeForegroundWindow =
        m_VrrWindowHandle != nullptr &&
        GetForegroundWindow() == m_VrrWindowHandle;
    feedback.nativeVrrFallbackReason = m_VrrFallbackReason;
    feedback.nativeDesktopMonitorCount = m_VrrDesktopMonitorCount;
    const WindowsVblankVirtualization::Snapshot
        vblankVirtualization =
            WindowsVblankVirtualization::snapshot();
    feedback.nativeVblankVirtualizationProbeComplete =
        vblankVirtualization.probeComplete;
    feedback.nativeVblankVirtualizationCallAvailable =
        vblankVirtualization.callAvailable;
    feedback.nativeVblankVirtualizationResultValid =
        vblankVirtualization.resultValid;
    feedback.nativeVblankVirtualizationResult =
        vblankVirtualization.result;
    feedback.nativeVblankVirtualizationDisabled =
        vblankVirtualization.disabled;
    feedback.nativeDisplayConfigQueryResultValid =
        m_VrrDisplayTiming.queryResultValid;
    feedback.nativeDisplayConfigQueryResult =
        m_VrrDisplayTiming.queryResult;
    feedback.nativeDisplayPathValid = m_VrrDisplayTiming.pathValid;
    feedback.nativeDisplayPathFlags = m_VrrDisplayTiming.pathFlags;
    feedback.nativeDisplayTargetAvailable =
        m_VrrDisplayTiming.targetAvailable;
    feedback.nativeDisplaySourceAdapterLuid =
        m_VrrDisplayTiming.sourceAdapterLuid;
    feedback.nativeDisplaySourceId =
        m_VrrDisplayTiming.sourceId;
    feedback.nativeDisplayTargetAdapterLuid =
        m_VrrDisplayTiming.targetAdapterLuid;
    feedback.nativeDisplayTargetId =
        m_VrrDisplayTiming.targetId;
    feedback.nativeDisplayOutputTechnology =
        m_VrrDisplayTiming.outputTechnology;
    feedback.nativeDisplayRotation =
        m_VrrDisplayTiming.rotation;
    feedback.nativeDisplayScaling =
        m_VrrDisplayTiming.scaling;
    feedback.nativeDisplayPathRefreshNumerator =
        m_VrrDisplayTiming.pathRefreshNumerator;
    feedback.nativeDisplayPathRefreshDenominator =
        m_VrrDisplayTiming.pathRefreshDenominator;
    feedback.nativeDisplaySignalValid =
        m_VrrDisplayTiming.signalValid;
    feedback.nativeDisplaySignalPixelRateHz =
        m_VrrDisplayTiming.signalPixelRateHz;
    feedback.nativeDisplaySignalHSyncNumerator =
        m_VrrDisplayTiming.signalHSyncNumerator;
    feedback.nativeDisplaySignalHSyncDenominator =
        m_VrrDisplayTiming.signalHSyncDenominator;
    feedback.nativeDisplaySignalVSyncNumerator =
        m_VrrDisplayTiming.signalVSyncNumerator;
    feedback.nativeDisplaySignalVSyncDenominator =
        m_VrrDisplayTiming.signalVSyncDenominator;
    feedback.nativeDisplaySignalActiveWidth =
        m_VrrDisplayTiming.signalActiveWidth;
    feedback.nativeDisplaySignalActiveHeight =
        m_VrrDisplayTiming.signalActiveHeight;
    feedback.nativeDisplaySignalTotalWidth =
        m_VrrDisplayTiming.signalTotalWidth;
    feedback.nativeDisplaySignalTotalHeight =
        m_VrrDisplayTiming.signalTotalHeight;
    feedback.nativeDisplaySignalAdditionalInfoRaw =
        m_VrrDisplayTiming.signalAdditionalInfoRaw;
    feedback.nativeDisplaySignalScanLineOrdering =
        m_VrrDisplayTiming.signalScanLineOrdering;
    feedback.nativeRasterSamplingRequested =
        m_VrrRasterSamplingRequested;
    feedback.nativeRasterOpenResultValid =
        m_VrrRasterOpenResultValid;
    feedback.nativeRasterOpenResult = m_VrrRasterOpenResult;
    feedback.nativeRasterSourceValid = m_VrrRasterSourceValid;
    feedback.nativeRasterVidPnSourceId =
        m_VrrRasterVidPnSourceId;
    if (request.collectDiagnostics &&
            m_VrrRasterSamplingRequested &&
            m_VrrRasterSourceValid) {
        feedback.nativeRasterBeforePresent = sampleVrrRaster();
    }
    const uint64_t submissionTimeUs = LiGetMicroseconds();
    HRESULT hr = presentPreparedFrame(presentParameters);
    const uint64_t nativePresentEndUs = LiGetMicroseconds();
    if (request.collectDiagnostics &&
            m_VrrRasterSamplingRequested &&
            m_VrrRasterSourceValid) {
        feedback.nativeRasterAfterPresent = sampleVrrRaster();
    }
    feedback.nativePresentResultValid = true;
    feedback.nativePresentResult = static_cast<int64_t>(hr);
    if (request.collectDiagnostics) {
        feedback.nativePresentTimingValid = true;
        feedback.nativePresentStartUs = submissionTimeUs;
        feedback.nativePresentEndUs = nativePresentEndUs;
    }

    if (FAILED(hr)) {
        // Release before notifying the UI about device loss, but not before
        // all successful-Present observations below. Keeping the retained
        // renderer lock across the ID/statistics queries prevents a window or
        // mode reset from replacing the swapchain between Present() and the
        // evidence that is supposed to describe that same call.
        releasePreparedVrrFrame();
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGISwapChain::Present() failed on D3D11 VRR path: %x",
                     hr);
        queueRenderDeviceReset();
        feedback.cancelled = true;
        return feedback;
    }
    if (hr != S_OK) {
        // In particular, DXGI_STATUS_OCCLUDED is a successful HRESULT but
        // does not mean that the image reached a monitor.
        releasePreparedVrrFrame();
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGISwapChain::Present() returned non-display status on D3D11 VRR path: %x",
                     hr);
        feedback.cancelled = true;
        return feedback;
    }

    feedback.presented = true;
    feedback.submissionTimeValid = true;
    feedback.submissionTimeUs = submissionTimeUs;

    // Observation-only presentation feedback. DXGI frame statistics describe
    // the most recent present that reached the screen, which may lag this
    // submission by several frames. PresentRefreshCount identifies its
    // refresh, but DXGI does not provide that refresh's timestamp.
    // SyncQPCTime is instead paired with SyncRefreshCount and is translated
    // onto the shared pacing clock as a raster-clock anchor. Never treat it as
    // the time of PresentRefreshCount. Failure (for example,
    // FRAME_STATISTICS_DISJOINT after a mode change or a composed swapchain)
    // leaves the sample invalid.
    UINT lastPresentCount = 0;
    if (request.collectDiagnostics) {
        feedback.submissionIdQueryStartUs = LiGetMicroseconds();
    }
    const HRESULT presentCountResult =
        m_SwapChain->GetLastPresentCount(&lastPresentCount);
    if (request.collectDiagnostics) {
        feedback.submissionIdQueryEndUs = LiGetMicroseconds();
    }
    feedback.submissionIdQueryResultValid = true;
    feedback.submissionIdQueryResult =
        static_cast<int64_t>(presentCountResult);
    if (presentCountResult == S_OK) {
        feedback.submissionIdValid = true;
        feedback.submissionId = lastPresentCount;
        m_VrrPriorPresentCountValid = true;
        m_VrrPriorPresentCount = lastPresentCount;
    }
    else {
        m_VrrPriorPresentCountValid = false;
    }

    DXGI_FRAME_STATISTICS frameStats = {};
    uint64_t syncSampleTimeUs = 0;
    uint64_t syncQpcFrequency = 0;
    if (request.collectDiagnostics) {
        feedback.frameStatsQueryStartUs = LiGetMicroseconds();
    }
    const HRESULT frameStatsResult =
        m_SwapChain->GetFrameStatistics(&frameStats);
    if (request.collectDiagnostics) {
        feedback.frameStatsQueryEndUs = LiGetMicroseconds();
    }
    feedback.frameStatsQueryResultValid = true;
    feedback.frameStatsQueryResult =
        static_cast<int64_t>(frameStatsResult);
    const bool syncQpcTranslated =
        frameStatsResult == S_OK &&
        translateVrrSyncQpcTime(
            frameStats.SyncQPCTime, syncSampleTimeUs,
            syncQpcFrequency);
    if (frameStatsResult == S_OK &&
            frameStats.SyncQPCTime.QuadPart > 0 &&
            syncQpcFrequency != 0) {
        feedback.latchRawSyncQpcValid = true;
        feedback.latchRawSyncQpcTicks = static_cast<uint64_t>(
            frameStats.SyncQPCTime.QuadPart);
        feedback.latchRawSyncQpcFrequency = syncQpcFrequency;
        const VrrQpcClockCorrelation& correlation =
            vrrQpcClockCorrelation();
        if (correlation.valid &&
                correlation.referenceQpc > 0) {
            feedback.latchQpcCorrelationValid = true;
            feedback.latchQpcCorrelationReferenceTicks =
                static_cast<uint64_t>(correlation.referenceQpc);
            feedback.latchQpcCorrelationReferenceTimeUs =
                correlation.referenceTimeUs;
            feedback.latchQpcCorrelationSpanTicks =
                correlation.bracketSpanTicks;
        }
    }
    if (syncQpcTranslated) {
        feedback.latchSampleValid = true;
        feedback.latchTimeKind = Vrr13::PresentationTimeKind::RefreshReference;
        // Retain the schema-5 field name for compatibility. Its DXGI
        // semantics are the SyncRefreshCount clock sample documented above.
        feedback.latchTimeUs = syncSampleTimeUs;
        feedback.latchSubmissionId = frameStats.PresentCount;
        feedback.latchPresentRefreshSequence =
            frameStats.PresentRefreshCount;
        feedback.latchRefreshSequence = frameStats.SyncRefreshCount;
        m_VrrPriorFrameStatsValid = true;
        m_VrrPriorFrameStatsPresentCount = frameStats.PresentCount;
        m_VrrPriorFrameStatsTimeUs = syncSampleTimeUs;
        m_VrrPriorFrameStatsPresentRefreshSequence =
            frameStats.PresentRefreshCount;
        m_VrrPriorFrameStatsRefreshSequence = frameStats.SyncRefreshCount;
    }
    else {
        m_VrrPriorFrameStatsValid = false;
    }

    // Present(), both raster samples, and the DXGI ID/statistics queries are
    // now one state-consistent observation. Release only after their evidence
    // has been copied into feedback.
    releasePreparedVrrFrame();
    return feedback;
}

VrrPresentFeedback D3D11VARenderer::cancelFrame()
{
    VrrPresentFeedback feedback;
    if (!m_VrrContextLocked) {
        lockContext(this);
        m_VrrContextLocked = true;
    }
    populateVrrGpuReadyFeedback(feedback);
    releasePreparedVrrFrame();
    feedback.cancelled = true;
    return feedback;
}

bool D3D11VARenderer::restoreFixedPresentation(VrrFallbackReason reason)
{
    // This is called synchronously only if the pacing worker could not start,
    // before it has prepared a frame.  ALLOW_TEARING is a swapchain capability,
    // not a requirement for every Present, so the existing swapchain safely
    // supports the legacy fixed path with Present(0, 0).  Do not recreate it.
    cancelFrame();
    if (m_CompositionPresenter.active()) {
        m_CompositionPresenter.reset();
        m_RenderTargetView.Reset();
        if (!setupSwapchainDependentResources()) {
            return false;
        }
        m_LastColorTrc = AVCOL_TRC_UNSPECIFIED;
    }
    m_VrrSuspended = false;
    m_DecoderParams.enableVrr = false;
    m_VrrFallbackReason = reason == VrrFallbackReason::NoFallback ?
        VrrFallbackReason::InitializationFailed : reason;
    return true;
}

void D3D11VARenderer::setSuspended(bool suspended)
{
    m_VrrSuspended = suspended;
    if (suspended) {
        // Do not carry a pre-suspension scanout observation into the first
        // diagnostic sample after a window/mode transition.
        m_VrrPriorPresentCountValid = false;
        m_VrrPriorFrameStatsValid = false;
    }
}

void D3D11VARenderer::refreshVrrDisplayTiming()
{
    m_VrrDisplayTiming = {};
    closeVrrRasterSource();
    m_VrrDisplayTiming.queryResultValid = true;
    if (m_VrrWindowHandle == nullptr) {
        m_VrrDisplayTiming.queryResult = ERROR_INVALID_WINDOW_HANDLE;
        return;
    }

    MONITORINFOEXW monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(
        m_VrrWindowHandle, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr ||
            !GetMonitorInfoW(monitor, &monitorInfo)) {
        m_VrrDisplayTiming.queryResult = GetLastError();
        return;
    }

    if (m_VrrRasterSamplingRequested) {
        D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openAdapter = {};
        wcsncpy_s(openAdapter.DeviceName, monitorInfo.szDevice, _TRUNCATE);
        const NTSTATUS openResult =
            D3DKMTOpenAdapterFromGdiDisplayName(&openAdapter);
        m_VrrRasterOpenResultValid = true;
        m_VrrRasterOpenResult = static_cast<int64_t>(openResult);
        if (openResult == 0) {
            m_VrrRasterSourceValid = true;
            m_VrrRasterAdapter = openAdapter.hAdapter;
            m_VrrRasterVidPnSourceId = openAdapter.VidPnSourceId;
        }
    }

    // Windows 11 can expose a virtual desktop refresh independently from the
    // physical signal refresh. Prefer that view, then fall back for older
    // systems that reject the newer query flag.
    constexpr UINT32 queryFlagSets[] = {
        QDC_ONLY_ACTIVE_PATHS |
            QDC_VIRTUAL_MODE_AWARE |
            QDC_VIRTUAL_REFRESH_RATE_AWARE,
        QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE,
        QDC_ONLY_ACTIVE_PATHS,
    };
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG queryResult = ERROR_INVALID_PARAMETER;
    for (UINT32 flags : queryFlagSets) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            UINT32 pathCount = 0;
            UINT32 modeCount = 0;
            queryResult = GetDisplayConfigBufferSizes(
                flags, &pathCount, &modeCount);
            if (queryResult != ERROR_SUCCESS) {
                break;
            }
            paths.assign(pathCount, DISPLAYCONFIG_PATH_INFO {});
            modes.assign(modeCount, DISPLAYCONFIG_MODE_INFO {});
            queryResult = QueryDisplayConfig(
                flags, &pathCount,
                paths.empty() ? nullptr : paths.data(),
                &modeCount,
                modes.empty() ? nullptr : modes.data(),
                nullptr);
            if (queryResult == ERROR_SUCCESS) {
                paths.resize(pathCount);
                modes.resize(modeCount);
                break;
            }
            if (queryResult != ERROR_INSUFFICIENT_BUFFER) {
                break;
            }
        }
        if (queryResult == ERROR_SUCCESS ||
                queryResult != ERROR_INVALID_PARAMETER) {
            break;
        }
    }
    m_VrrDisplayTiming.queryResult =
        static_cast<uint32_t>(queryResult);
    if (queryResult != ERROR_SUCCESS) {
        return;
    }

    VrrDisplayTimingSnapshot matchedTiming;
    uint32_t matchedPaths = 0;
    for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = path.sourceInfo.adapterId;
        sourceName.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sourceName.header) !=
                ERROR_SUCCESS ||
                _wcsicmp(sourceName.viewGdiDeviceName,
                         monitorInfo.szDevice) != 0) {
            continue;
        }

        ++matchedPaths;
        matchedTiming.queryResultValid = true;
        matchedTiming.queryResult = ERROR_SUCCESS;
        matchedTiming.pathValid = true;
        matchedTiming.pathFlags = path.flags;
        matchedTiming.targetAvailable =
            path.targetInfo.targetAvailable != FALSE;
        matchedTiming.sourceAdapterLuid =
            packLuid(path.sourceInfo.adapterId);
        matchedTiming.sourceId = path.sourceInfo.id;
        matchedTiming.targetAdapterLuid =
            packLuid(path.targetInfo.adapterId);
        matchedTiming.targetId = path.targetInfo.id;
        matchedTiming.outputTechnology =
            static_cast<uint32_t>(
                path.targetInfo.outputTechnology);
        matchedTiming.rotation =
            static_cast<uint32_t>(path.targetInfo.rotation);
        matchedTiming.scaling =
            static_cast<uint32_t>(path.targetInfo.scaling);
        matchedTiming.pathRefreshNumerator =
            path.targetInfo.refreshRate.Numerator;
        matchedTiming.pathRefreshDenominator =
            path.targetInfo.refreshRate.Denominator;

        const bool virtualMode =
            (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0;
        const UINT32 targetModeIndex = virtualMode ?
            path.targetInfo.targetModeInfoIdx :
            path.targetInfo.modeInfoIdx;
        const UINT32 invalidTargetModeIndex = virtualMode ?
            DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID :
            DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        if (targetModeIndex == invalidTargetModeIndex ||
                targetModeIndex >= modes.size()) {
            continue;
        }
        const DISPLAYCONFIG_MODE_INFO& mode = modes[targetModeIndex];
        if (mode.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET ||
                mode.id != path.targetInfo.id ||
                mode.adapterId.HighPart !=
                    path.targetInfo.adapterId.HighPart ||
                mode.adapterId.LowPart !=
                    path.targetInfo.adapterId.LowPart) {
            continue;
        }

        const DISPLAYCONFIG_VIDEO_SIGNAL_INFO& signal =
            mode.targetMode.targetVideoSignalInfo;
        matchedTiming.signalValid = true;
        matchedTiming.signalPixelRateHz = signal.pixelRate;
        matchedTiming.signalHSyncNumerator =
            signal.hSyncFreq.Numerator;
        matchedTiming.signalHSyncDenominator =
            signal.hSyncFreq.Denominator;
        matchedTiming.signalVSyncNumerator =
            signal.vSyncFreq.Numerator;
        matchedTiming.signalVSyncDenominator =
            signal.vSyncFreq.Denominator;
        matchedTiming.signalActiveWidth = signal.activeSize.cx;
        matchedTiming.signalActiveHeight = signal.activeSize.cy;
        matchedTiming.signalTotalWidth = signal.totalSize.cx;
        matchedTiming.signalTotalHeight = signal.totalSize.cy;
        // Preserve the complete union word. Bits 0..15 are videoStandard,
        // bits 16..21 are the Miracast VSync divider, and the remainder is
        // reserved. Keeping the raw value avoids hiding future driver data.
        matchedTiming.signalAdditionalInfoRaw = signal.videoStandard;
        matchedTiming.signalScanLineOrdering =
            static_cast<uint32_t>(signal.scanLineOrdering);
    }

    // A cloned source can map the same GDI name to more than one target.
    // Refuse to guess which physical signal owns the window.
    if (matchedPaths == 1) {
        m_VrrDisplayTiming = matchedTiming;
    }
}

void D3D11VARenderer::closeVrrRasterSource()
{
    if (m_VrrRasterSourceValid) {
        D3DKMT_CLOSEADAPTER closeAdapter = {};
        closeAdapter.hAdapter = m_VrrRasterAdapter;
        D3DKMTCloseAdapter(&closeAdapter);
    }
    m_VrrRasterOpenResultValid = false;
    m_VrrRasterOpenResult = 0;
    m_VrrRasterSourceValid = false;
    m_VrrRasterAdapter = 0;
    m_VrrRasterVidPnSourceId = 0;
}

VrrNativeRasterSample D3D11VARenderer::sampleVrrRaster() const
{
    VrrNativeRasterSample sample;
    if (!m_VrrRasterSamplingRequested ||
            !m_VrrRasterSourceValid) {
        return sample;
    }

    D3DKMT_GETSCANLINE query = {};
    query.hAdapter = m_VrrRasterAdapter;
    query.VidPnSourceId = m_VrrRasterVidPnSourceId;
    sample.queryStartUs = LiGetMicroseconds();
    const NTSTATUS queryResult = D3DKMTGetScanLine(&query);
    sample.queryEndUs = LiGetMicroseconds();
    sample.queryResultValid = true;
    sample.queryResult = static_cast<int64_t>(queryResult);
    if (queryResult == 0) {
        sample.inVerticalBlank = query.InVerticalBlank != FALSE;
        sample.scanLine = query.ScanLine;
    }
    return sample;
}

bool D3D11VARenderer::setupRenderingResources()
{
    HRESULT hr;

    m_RenderDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // We use a common vertex shader for all pixel shaders
    {
        QByteArray vertexShaderBytecode = Path::readDataFile("d3d11_vertex.fxc");

        ComPtr<ID3D11VertexShader> vertexShader;
        hr = m_RenderDevice->CreateVertexShader(vertexShaderBytecode.constData(), vertexShaderBytecode.length(), nullptr, &vertexShader);
        if (SUCCEEDED(hr)) {
            m_RenderDeviceContext->VSSetShader(vertexShader.Get(), nullptr, 0);
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateVertexShader() failed: %x",
                         hr);
            return false;
        }

        const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ComPtr<ID3D11InputLayout> inputLayout;
        hr = m_RenderDevice->CreateInputLayout(vertexDesc, ARRAYSIZE(vertexDesc), vertexShaderBytecode.constData(), vertexShaderBytecode.length(), &inputLayout);
        if (SUCCEEDED(hr)) {
            m_RenderDeviceContext->IASetInputLayout(inputLayout.Get());
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateInputLayout() failed: %x",
                         hr);
            return false;
        }
    }

    {
        QByteArray overlayPixelShaderBytecode = Path::readDataFile("d3d11_overlay_pixel.fxc");

        hr = m_RenderDevice->CreatePixelShader(overlayPixelShaderBytecode.constData(), overlayPixelShaderBytecode.length(), nullptr, &m_OverlayPixelShader);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreatePixelShader() failed: %x",
                         hr);
            return false;
        }
    }

    for (int i = 0; i < PixelShaders::_COUNT; i++)
    {
        QByteArray videoPixelShaderBytecode = Path::readDataFile(k_VideoShaderNames[i]);

        hr = m_RenderDevice->CreatePixelShader(videoPixelShaderBytecode.constData(), videoPixelShaderBytecode.length(), nullptr, &m_VideoPixelShaders[i]);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreatePixelShader() failed: %x",
                         hr);
            return false;
        }
    }

    // We use a common sampler for all pixel shaders
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        ComPtr<ID3D11SamplerState> sampler;
        hr = m_RenderDevice->CreateSamplerState(&samplerDesc,  &sampler);
        if (SUCCEEDED(hr)) {
            m_RenderDeviceContext->PSSetSamplers(0, 1, sampler.GetAddressOf());
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateSamplerState() failed: %x",
                         hr);
            return false;
        }
    }

    // We use a common index buffer for all geometry
    {
        const int indexes[] = {0, 1, 2, 3, 2, 1};
        D3D11_BUFFER_DESC indexBufferDesc = {};
        indexBufferDesc.ByteWidth = sizeof(indexes);
        indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        indexBufferDesc.CPUAccessFlags = 0;
        indexBufferDesc.MiscFlags = 0;
        indexBufferDesc.StructureByteStride = sizeof(int);

        D3D11_SUBRESOURCE_DATA indexBufferData = {};
        indexBufferData.pSysMem = indexes;
        indexBufferData.SysMemPitch = sizeof(int);

        ComPtr<ID3D11Buffer> indexBuffer;
        hr = m_RenderDevice->CreateBuffer(&indexBufferDesc, &indexBufferData, &indexBuffer);
        if (SUCCEEDED(hr)) {
            m_RenderDeviceContext->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateBuffer() failed: %x",
                         hr);
            return false;
        }
    }

    // Create our overlay blend state
    {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        hr = m_RenderDevice->CreateBlendState(&blendDesc, &m_OverlayBlendState);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateBlendState() failed: %x",
                         hr);
            return false;
        }
    }

    // Create and bind our video blend state
    {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        hr = m_RenderDevice->CreateBlendState(&blendDesc, &m_VideoBlendState);
        if (SUCCEEDED(hr)) {
            m_RenderDeviceContext->OMSetBlendState(m_VideoBlendState.Get(), nullptr, 0xffffffff);
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateBlendState() failed: %x",
                         hr);
            return false;
        }
    }

    if (!setupSwapchainDependentResources()) {
        return false;
    }

    return true;
}

bool D3D11VARenderer::setupSwapchainDependentResources()
{
    HRESULT hr;

    // Create our render target view
    {
        ComPtr<ID3D11Resource> backBufferResource;
        hr = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBufferResource));
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "IDXGISwapChain::GetBuffer() failed: %x",
                         hr);
            return false;
        }

        hr = m_RenderDevice->CreateRenderTargetView(backBufferResource.Get(), nullptr, &m_RenderTargetView);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateRenderTargetView() failed: %x",
                         hr);
            return false;
        }
    }

    // Set a viewport that fills the window
    {
        D3D11_VIEWPORT viewport;

        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = m_DisplayWidth;
        viewport.Height = m_DisplayHeight;
        viewport.MinDepth = 0;
        viewport.MaxDepth = 1;

        m_RenderDeviceContext->RSSetViewports(1, &viewport);
    }

    return true;
}

// NB: This can be called more than once (and with different frame dimensions!)
bool D3D11VARenderer::setupFrameRenderingResources(AVHWFramesContext* framesContext)
{
    auto d3d11vaFramesContext = (AVD3D11VAFramesContext*)framesContext->hwctx;

    // Open the decoder texture array on the renderer device if we're using separate devices
    if (m_DecodeDevice != m_RenderDevice) {
        ComPtr<IDXGIResource1> dxgiDecoderResource;

        HRESULT hr = d3d11vaFramesContext->texture_infos->texture->QueryInterface(IID_PPV_ARGS(&dxgiDecoderResource));
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Texture2D::QueryInterface(IDXGIResource1) failed: %x",
                         hr);
            return false;
        }

        HANDLE sharedHandle;
        hr = dxgiDecoderResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &sharedHandle);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "IDXGIResource1::CreateSharedHandle() failed: %x",
                         hr);
            return false;
        }

        hr = m_RenderDevice->OpenSharedResource1(sharedHandle, IID_PPV_ARGS(&m_RenderSharedTextureArray));
        CloseHandle(sharedHandle);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device1::OpenSharedResource1() failed: %x",
                         hr);
            return false;
        }
    }
    else {
        d3d11vaFramesContext->texture_infos->texture->AddRef();
        m_RenderSharedTextureArray.Attach(d3d11vaFramesContext->texture_infos->texture);
    }

    // Query the format of the underlying texture array
    D3D11_TEXTURE2D_DESC textureDesc;
    m_RenderSharedTextureArray->GetDesc(&textureDesc);
    m_TextureFormat = textureDesc.Format;

    if (m_BindDecoderOutputTextures) {
        // Create SRVs for all textures in the decoder pool
        if (!setupTexturePoolViews(framesContext)) {
            return false;
        }
    }
    else {
        // Create our internal texture to copy and render
        if (!setupVideoTexture(framesContext)) {
            return false;
        }
    }

    return true;
}

std::vector<DXGI_FORMAT> D3D11VARenderer::getVideoTextureSRVFormats()
{
    if (m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_YUV444) {
        // YUV 4:4:4 formats don't use a second SRV
        return { (m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_10BIT) ?
                    DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM };
    }
    else if (m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_10BIT) {
        return { DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16G16_UNORM };
    }
    else {
        return { DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM };
    }
}

bool D3D11VARenderer::setupVideoTexture(AVHWFramesContext* framesContext)
{
    SDL_assert(!m_BindDecoderOutputTextures);

    HRESULT hr;
    D3D11_TEXTURE2D_DESC texDesc = {};

    texDesc.Width = framesContext->width;
    texDesc.Height = framesContext->height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = m_TextureFormat;
    texDesc.SampleDesc.Quality = 0;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    hr = m_RenderDevice->CreateTexture2D(&texDesc, nullptr, &m_VideoTexture);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateTexture2D() failed: %x",
                     hr);
        return false;
    }

    // We will only have one set of SRVs
    m_VideoTextureResourceViews.resize(1);

    // Create SRVs for the texture
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    size_t srvIndex = 0;
    for (DXGI_FORMAT srvFormat : getVideoTextureSRVFormats()) {
        SDL_assert(srvIndex < m_VideoTextureResourceViews[0].size());

        srvDesc.Format = srvFormat;
        hr = m_RenderDevice->CreateShaderResourceView(m_VideoTexture.Get(), &srvDesc, &m_VideoTextureResourceViews[0][srvIndex]);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateShaderResourceView() failed: %x",
                         hr);
            return false;
        }

        srvIndex++;
    }

    return true;
}

bool D3D11VARenderer::setupTexturePoolViews(AVHWFramesContext* framesContext)
{
    AVD3D11VAFramesContext* d3d11vaFramesContext = (AVD3D11VAFramesContext*)framesContext->hwctx;

    SDL_assert(m_BindDecoderOutputTextures);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.ArraySize = 1;

    m_VideoTextureResourceViews.resize(framesContext->initial_pool_size);

    // Create luminance and chrominance SRVs for each texture in the pool
    for (int i = 0; i < framesContext->initial_pool_size; i++) {
        HRESULT hr;

        // Our rendering logic depends on the texture index working to map into our SRV array
        SDL_assert(i == d3d11vaFramesContext->texture_infos[i].index);

        srvDesc.Texture2DArray.FirstArraySlice = d3d11vaFramesContext->texture_infos[i].index;

        size_t srvIndex = 0;
        for (DXGI_FORMAT srvFormat : getVideoTextureSRVFormats()) {
            SDL_assert(srvIndex < m_VideoTextureResourceViews[i].size());

            srvDesc.Format = srvFormat;
            hr = m_RenderDevice->CreateShaderResourceView(m_RenderSharedTextureArray.Get(),
                                                          &srvDesc,
                                                          &m_VideoTextureResourceViews[i][srvIndex]);
            if (FAILED(hr)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "ID3D11Device::CreateShaderResourceView() failed: %x",
                             hr);
                return false;
            }

            srvIndex++;
        }
    }

    return true;
}

QString D3D11VARenderer::getCalibrationIdentity()
{
    ComPtr<IDXGIDevice> device;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    LARGE_INTEGER driver{};
    if (!m_RenderDevice || FAILED(m_RenderDevice.As(&device)) ||
        FAILED(device->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc)) ||
        FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver))) return {};
    // Retain the historical enabled-policy identity so existing calibration
    // from the former default setting remains applicable.
    return QString("D3D11|%1|%2|%3|%4|%5|%6|%7|allow-tearing=%8")
        .arg(desc.VendorId).arg(desc.DeviceId).arg(desc.SubSysId).arg(desc.Revision)
        .arg(driver.QuadPart).arg(m_DecodeDevice == m_RenderDevice)
        .arg(m_CompositionPresenter.active() ? "composition" : "dxgi")
        .arg(1);
}
