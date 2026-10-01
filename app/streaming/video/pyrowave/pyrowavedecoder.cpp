// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "pyrowavedecoder.h"

#include <vulkan/vulkan.h>
#include <pyrowave.h>

#include <Limelight.h>
#include <SDL.h>
#include <chrono>

#ifdef _WIN32
#include <array>
#include <deque>
#include <mutex>
#include <windows.h>
#endif

namespace {

constexpr unsigned kFrameContexts = 4;

class DecodePhaseTimer {
public:
    explicit DecodePhaseTimer(PyroWaveDecoder::DecodeDiagnostics* diagnostics)
        : m_Diagnostics(diagnostics)
    {
        if (m_Diagnostics) {
            *m_Diagnostics = {};
            m_Start = Clock::now();
        }
    }
    ~DecodePhaseTimer() { finish(); }
    void next(unsigned phase) { finish(); m_Phase = phase; }
private:
    using Clock = std::chrono::steady_clock;
    void finish()
    {
        if (m_Diagnostics) {
            const auto now = Clock::now();
            m_Diagnostics->phaseUs[m_Phase] +=
                std::chrono::duration_cast<std::chrono::microseconds>(now - m_Start).count();
            m_Start = now;
        }
    }
    PyroWaveDecoder::DecodeDiagnostics* m_Diagnostics;
    unsigned m_Phase = 0;
    Clock::time_point m_Start;
};

#ifdef _WIN32
// Returned to the decoder when the last reference to a frame is dropped.
struct SurfaceFreeList {
    std::mutex lock;
    // Surface index and the release fence value that must complete before the
    // decoder may overwrite it (0 if the renderer never sampled it).
    std::deque<std::pair<int, uint64_t>> free;

    void push(int surface, uint64_t releaseValue)
    {
        std::lock_guard<std::mutex> guard(lock);
        free.emplace_back(surface, releaseValue);
    }

    bool pop(int& surface, uint64_t& releaseValue)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (free.empty()) {
            return false;
        }
        surface = free.front().first;
        releaseValue = free.front().second;
        free.pop_front();
        return true;
    }
};

struct FrameOwner {
    std::shared_ptr<SurfaceFreeList> freeList;
};

void freeFrameRef(void* opaque, uint8_t* data)
{
    auto owner = static_cast<FrameOwner*>(opaque);
    auto ref = reinterpret_cast<PyroWaveFrameRef*>(data);

    owner->freeList->push(ref->surface, ref->releaseFenceValue.load(std::memory_order_acquire));

    delete ref;
    delete owner;
}

void closeOsHandle(uintptr_t handle)
{
    if (handle != 0) {
        CloseHandle(reinterpret_cast<HANDLE>(handle));
    }
}

VkFormat toVkFormat(PyroWavePlaneFormat format)
{
    switch (format) {
    case PyroWavePlaneFormat::R16Unorm:
        return VK_FORMAT_R16_UNORM;
    case PyroWavePlaneFormat::R8Unorm:
    default:
        return VK_FORMAT_R8_UNORM;
    }
}
#endif

const char* resultString(pyrowave_result result)
{
    switch (result) {
    case PYROWAVE_SUCCESS: return "success";
    case PYROWAVE_TIMEOUT: return "timeout";
    case PYROWAVE_ERROR_GENERIC: return "generic error";
    case PYROWAVE_ERROR_INVALID_ARGUMENT: return "invalid argument";
    case PYROWAVE_ERROR_OUT_OF_HOST_MEMORY: return "out of host memory";
    case PYROWAVE_ERROR_OUT_OF_DEVICE_MEMORY: return "out of device memory";
    case PYROWAVE_ERROR_NO_VULKAN: return "no Vulkan";
    case PYROWAVE_ERROR_NOT_IMPLEMENTED: return "not implemented";
    case PYROWAVE_ERROR_UNSUPPORTED_EXTERNAL_HANDLE: return "unsupported external handle";
    case PYROWAVE_ERROR_FAILED_EXTERNAL_HANDLE: return "failed external handle";
    default: return "unknown error";
    }
}

}

struct PyroWaveDecoder::Impl {
    Config config;
    PyroWaveFraming::StreamGeometry geometry {};

    pyrowave_device device = nullptr;
    pyrowave_decoder decoder = nullptr;
#ifdef _WIN32
    pyrowave_sync_object decodeSync = nullptr;
    pyrowave_sync_object releaseSync = nullptr;
    uint64_t decodeValue = 0;

    struct Surface {
        std::array<pyrowave_image, 3> images {};
        pyrowave_gpu_buffers buffers {};
    };
    std::vector<Surface> surfaces;
    std::shared_ptr<SurfaceFreeList> freeList = std::make_shared<SurfaceFreeList>();
#else
    IPyroWaveVulkanPool* vulkanPool = nullptr;
#endif

    PyroWaveFraming::Frame parsed;
    bool reportDiagnostics = false;

    ~Impl()
    {
        // Destroying the decoder waits for its GPU work to finish.
        if (decoder != nullptr) {
            pyrowave_decoder_destroy(decoder);
        }
        // Report before destroying imported Windows images: each image teardown
        // advances the codec's frame context and would dilute these averages.
        if (device != nullptr && reportDiagnostics) {
            pyrowave_device_report_performance_stats(device, [](void*, const char* message) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "PyroWave GPU history (per codec frame context): %s", message);
            }, nullptr, false);
        }
#ifdef _WIN32
        for (auto& surface : surfaces) {
            for (auto image : surface.images) {
                if (image != nullptr) {
                    pyrowave_image_destroy(image);
                }
            }
        }
        if (decodeSync != nullptr) {
            pyrowave_sync_object_destroy(decodeSync);
        }
        if (releaseSync != nullptr) {
            pyrowave_sync_object_destroy(releaseSync);
        }
#endif
        if (device != nullptr) {
            pyrowave_device_destroy(device);
        }
    }

#ifdef _WIN32
    bool importFence(uintptr_t handle, pyrowave_sync_object& sync)
    {
        if (handle == 0) {
            return false;
        }

        pyrowave_sync_object_create_info info = {};
        info.device = device;
        info.external_handle = handle;
        // A D3D11 fence is the same object as a D3D12 fence on Windows 10+.
        info.handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
        info.semaphore_type = VK_SEMAPHORE_TYPE_TIMELINE;

        const pyrowave_result result = pyrowave_sync_object_create(&info, &sync);
        if (result != PYROWAVE_SUCCESS) {
            // The implementation only takes ownership of the handle on success
            closeOsHandle(handle);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "PyroWave: importing a shared fence failed: %s",
                         resultString(result));
            return false;
        }
        return true;
    }

    bool importSurface(IPyroWaveSurfacePool* pool, int index)
    {
        PyroWaveSharedPlane planes[3];
        if (!pool->exportPyroWaveSurface(index, planes)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "PyroWave: renderer could not export surface %d", index);
            return false;
        }

        Surface surface;
        bool ok = true;
        for (int plane = 0; plane < 3; plane++) {
            if (!ok) {
                closeOsHandle(planes[plane].handle);
                continue;
            }

            VkImageCreateInfo imageInfo = {};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = toVkFormat(planes[plane].format);
            imageInfo.extent = { planes[plane].width, planes[plane].height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            pyrowave_image_create_info info = {};
            info.device = device;
            info.external_handle = planes[plane].handle;
            info.handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
            info.image_create_info = &imageInfo;

            pyrowave_result result = pyrowave_image_create(&info, &surface.images[plane]);
            if (result != PYROWAVE_SUCCESS) {
                closeOsHandle(planes[plane].handle);
                surface.images[plane] = nullptr;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "PyroWave: importing surface %d plane %d failed: %s",
                             index, plane, resultString(result));
                ok = false;
                continue;
            }

            result = pyrowave_image_get_image_view(surface.images[plane],
                                                   VK_IMAGE_ASPECT_COLOR_BIT,
                                                   VK_IMAGE_USAGE_STORAGE_BIT,
                                                   &surface.buffers.planes[plane]);
            if (result != PYROWAVE_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "PyroWave: creating a view of surface %d plane %d failed: %s",
                             index, plane, resultString(result));
                ok = false;
            }
        }

        if (!ok) {
            for (auto image : surface.images) {
                if (image != nullptr) {
                    pyrowave_image_destroy(image);
                }
            }
            return false;
        }

        surfaces.push_back(surface);
        return true;
    }
#endif
};

PyroWaveDecoder::PyroWaveDecoder() = default;

PyroWaveDecoder::~PyroWaveDecoder() = default;

bool PyroWaveDecoder::hasAsynchronousOutput() const
{
#ifdef _WIN32
    return m_Impl != nullptr;
#else
    return m_Impl && m_Impl->vulkanPool != nullptr;
#endif
}

bool PyroWaveDecoder::initialize(const Config& config, IPyroWaveSurfacePool* pool)
{
    SDL_assert(!m_Impl);

    if (config.width <= 0 || config.height <= 0) {
        return false;
    }
#ifdef _WIN32
    if (pool == nullptr) {
        return false;
    }
#else
    (void)pool;
#endif
    if (!config.chroma444 && ((config.width | config.height) & 1)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave: 4:2:0 streams need an even width and height (%dx%d)",
                     config.width, config.height);
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->geometry = { config.width, config.height, config.chroma444 };

    uint32_t major = 0, minor = 0, patch = 0;
    pyrowave_get_api_version(&major, &minor, &patch);

#ifdef _WIN32
    uint8_t luid[8];
    if (!pool->pyroWaveAdapterLuid(luid)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave: renderer adapter identity is unavailable");
        return false;
    }

    static_assert(sizeof(pyrowave_luid) == sizeof(luid), "LUID size mismatch");
    pyrowave_result result = pyrowave_create_device_by_compat(
        0, 0, nullptr, nullptr, reinterpret_cast<const pyrowave_luid*>(luid), &impl->device);
    if (result != PYROWAVE_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave: no Vulkan device for the renderer's adapter: %s",
                     resultString(result));
        return false;
    }

    if (!pyrowave_device_confirm_interop_support(impl->device)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave: the Vulkan driver cannot import D3D11 textures and fences");
        return false;
    }
#else
    pyrowave_result result = PYROWAVE_ERROR_NO_VULKAN;
    PyroWaveVulkanDevice shared;
    if (config.vulkanPool != nullptr && config.vulkanPool->pyroWaveVulkanDevice(shared)) {
        pyrowave_device_create_info deviceInfo = {};
        deviceInfo.GetInstanceProcAddr = shared.getInstanceProcAddr;
        deviceInfo.instance = shared.instance;
        deviceInfo.physical_device = shared.physicalDevice;
        deviceInfo.device = shared.device;
        deviceInfo.instance_create_info = shared.instanceInfo;
        deviceInfo.device_create_info = shared.deviceInfo;
        deviceInfo.queue_lock_callback = shared.lockQueues;
        deviceInfo.queue_unlock_callback = shared.unlockQueues;
        deviceInfo.userdata = shared.userdata;
        result = pyrowave_create_device(&deviceInfo, &impl->device);
        if (result == PYROWAVE_SUCCESS) {
            impl->vulkanPool = config.vulkanPool;
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "PyroWave: cannot share the renderer's Vulkan device (%s); reading frames back instead",
                        resultString(result));
            impl->device = nullptr;
        }
    }
    if (impl->device == nullptr) {
        result = pyrowave_create_default_device(&impl->device);
        if (result != PYROWAVE_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "PyroWave: no usable Vulkan decode device: %s", resultString(result));
            return false;
        }
    }
#endif
    // With two frame contexts, a decode blocks this thread until the GPU has
    // finished the decode submitted two frames earlier. A brief GPU hiccup
    // then ages queued frames past the stale limit and they are skipped. More
    // contexts queue the work instead; the pacer sees late frames and decides.
    if (pyrowave_device_set_frame_contexts(impl->device, kFrameContexts) != PYROWAVE_SUCCESS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "PyroWave: keeping the default frame context count");
    }

    pyrowave_decoder_create_info decoderInfo = {};
    decoderInfo.device = impl->device;
    decoderInfo.width = config.width;
    decoderInfo.height = config.height;
    decoderInfo.chroma = config.chroma444 ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420;
    decoderInfo.fragment_path = pyrowave_decoder_device_prefers_fragment_path(impl->device);
    // Keep PyroWave's default graphics queue. A separate compute queue still
    // competes for shader cores with the compositor after our Present returns.
    // In the Deck 4K capture, delayed Wayland presentations tracked completion
    // of the next async decode even though this frame was already GPU-ready.
    // Shared surfaces and timeline synchronization remain asynchronous; only
    // the queue selection changes here.
    result = pyrowave_decoder_create(&decoderInfo, &impl->decoder);
    if (result != PYROWAVE_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave: decoder creation failed: %s",
                     resultString(result));
        return false;
    }

    // Both pools keep their fixed-size output images alive until this decoder
    // is destroyed. Reuse their Vulkan views instead of creating and retiring
    // three new views (and associated descriptors) for every frame.
#ifdef _WIN32
    pyrowave_decoder_set_output_view_cache(impl->decoder, true);
#else
    pyrowave_decoder_set_output_view_cache(impl->decoder, impl->vulkanPool != nullptr);
#endif

#ifdef _WIN32
    if (!impl->importFence(pool->exportPyroWaveDecodeFence(), impl->decodeSync) ||
            !impl->importFence(pool->exportPyroWaveReleaseFence(), impl->releaseSync)) {
        return false;
    }

    for (int i = 0; i < pool->pyroWaveSurfaceCount(); i++) {
        if (!impl->importSurface(pool, i)) {
            return false;
        }
        impl->freeList->push(i, 0);
    }
#endif

#ifdef _WIN32
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "PyroWave decoder ready: %dx%d %s %d-bit, %d surfaces, API %u.%u.%u, bitstream %s%s",
                config.width, config.height,
                config.chroma444 ? "4:4:4" : "4:2:0",
                config.tenBit ? 10 : 8,
                (int)impl->surfaces.size(),
                major, minor, patch, PYROWAVE_BITSTREAM_ID,
                decoderInfo.fragment_path ? " (fragment path)" : "");
#else
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "PyroWave decoder ready: %dx%d %s %d-bit, %s, API %u.%u.%u, bitstream %s%s",
                config.width, config.height,
                config.chroma444 ? "4:4:4" : "4:2:0",
                config.tenBit ? 10 : 8,
                impl->vulkanPool ? "shared renderer surfaces, graphics queue" :
                                   "Vulkan readback",
                major, minor, patch, PYROWAVE_BITSTREAM_ID,
                decoderInfo.fragment_path ? " (fragment path)" : "");
#endif

    m_Impl = std::move(impl);
    return true;
}

bool PyroWaveDecoder::decode(const uint8_t* data, size_t size,
                             const std::vector<PyroWaveFraming::Segment>& packets, size_t criticalPackets,
                             AVFrame* frame, DecodeDiagnostics* diagnostics)
{
    Impl& impl = *m_Impl;
    DecodePhaseTimer phase(diagnostics);
    impl.reportDiagnostics |= diagnostics != nullptr;

    m_LastFramePartial = false;
    if (!PyroWaveFraming::parse(data, size, packets, criticalPackets, impl.geometry, impl.parsed, m_LastError)) {
        return false;
    }
    m_LastFraming = impl.parsed.framing;
    if (diagnostics) {
        diagnostics->receivedBlocks = impl.parsed.blockRecords;
        diagnostics->announcedBlocks = impl.parsed.announcedBlocks;
        diagnostics->paddingBytes = impl.parsed.paddingBytes;
        diagnostics->partial = impl.parsed.partial;
        for (const auto& span : impl.parsed.spans) diagnostics->payloadBytes += span.size;
    }
    phase.next(1);

    // Every frame is independent. Clearing first keeps the 3-bit sequence
    // counter from treating a frame after a long drop as stale.
    pyrowave_decoder_clear(impl.decoder);
    for (const auto& span : impl.parsed.spans) {
        const pyrowave_result result = pyrowave_decoder_push_packet(impl.decoder, data + span.offset, span.size);
        if (result != PYROWAVE_SUCCESS) {
            m_LastError = std::string("decoder rejected a packet: ") + resultString(result);
            return false;
        }
    }

    if (!impl.parsed.partial) {
        if (!pyrowave_decoder_decode_is_ready(impl.decoder, false)) {
            m_LastError = "frame is incomplete";
            return false;
        }
    }
    else {
        // A partial frame decodes whenever its coarsest wavelet level is
        // intact. The host protects that level with parity and nothing else,
        // so every other loss decodes as blur over the area it covered.
        // The parser checks the coarsest level: PyroWave's own check cannot tell
        // a lost block from an all-zero one that was never sent.
        if (!impl.parsed.coarseLevelIntact) {
            m_LastError = "part of the coarsest wavelet level was lost";
            return false;
        }
        if (!pyrowave_decoder_decode_is_ready_with_sideband(impl.decoder, true, 0, 0.0f, nullptr, 0)) {
            m_LastError = "no decodable blocks arrived (" + std::to_string(impl.parsed.blockRecords) +
                          " of " + std::to_string(impl.parsed.announcedBlocks) + " blocks)";
            return false;
        }
    }
    m_LastFramePartial = impl.parsed.partial;
    phase.next(2);

#ifndef _WIN32
    if (impl.vulkanPool != nullptr) {
        PyroWaveVulkanSurface surface;
        if (!impl.vulkanPool->holdPyroWaveSurface(impl.config.width, impl.config.height,
                                                  impl.config.chroma444, impl.config.tenBit, surface)) {
            m_LastError = "no free output surface";
            return false;
        }

        pyrowave_gpu_buffers buffers = {};
        for (int plane = 0; plane < 3; ++plane) {
            auto& view = buffers.planes[plane];
            view.image = surface.images[plane];
            view.width = surface.widths[plane];
            view.height = surface.heights[plane];
            view.image_format = view.view_format = surface.format;
            view.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            view.swizzle = VK_COMPONENT_SWIZZLE_IDENTITY;
            view.layout = VK_IMAGE_LAYOUT_GENERAL;
        }
        pyrowave_gpu_sync_operation acquire = {};
        acquire.sync = { surface.ready, surface.readyValue };
        pyrowave_gpu_sync_operation release = {};
        release.sync = { surface.done, surface.doneValue };

        phase.next(3);
        const pyrowave_result result = diagnostics ?
            pyrowave_decoder_decode_gpu_buffer_with_context_timing(
                impl.decoder, &acquire, &release, &buffers, &diagnostics->contextWaitUs) :
            pyrowave_decoder_decode_gpu_buffer(impl.decoder, &acquire, &release, &buffers);
        phase.next(4);
        if (!impl.vulkanPool->releasePyroWaveSurface(surface, result == PYROWAVE_SUCCESS, frame)) {
            m_LastError = result == PYROWAVE_SUCCESS ?
                "cannot reference the output surface" :
                std::string("GPU decode failed: ") + resultString(result);
            return false;
        }
        frame->chroma_location = AVCHROMA_LOC_CENTER;
        frame->flags |= AV_FRAME_FLAG_KEY;
        return true;
    }

    // R16 UNORM planes span the full 16-bit range, which is how FFmpeg's
    // 16-bit formats are normalized.
    frame->width = impl.config.width;
    frame->height = impl.config.height;
    frame->format = impl.config.tenBit ?
        (impl.config.chroma444 ? AV_PIX_FMT_YUV444P16 : AV_PIX_FMT_YUV420P16) :
        (impl.config.chroma444 ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUV420P);
    if (av_frame_get_buffer(frame, 32) < 0) {
        m_LastError = "cannot allocate decoded frame";
        return false;
    }

    pyrowave_cpu_buffer output = {};
    output.width = frame->width;
    output.height = frame->height;
    output.format = impl.config.tenBit ?
        (impl.config.chroma444 ? PYROWAVE_CPU_BUFFER_FORMAT_YUV444P16 :
                                 PYROWAVE_CPU_BUFFER_FORMAT_YUV420P16) :
        (impl.config.chroma444 ? PYROWAVE_CPU_BUFFER_FORMAT_YUV444P :
                                 PYROWAVE_CPU_BUFFER_FORMAT_YUV420P);
    for (int plane = 0; plane < 3; ++plane) {
        const int planeHeight = plane != 0 && !impl.config.chroma444 ?
            frame->height / 2 : frame->height;
        output.data[plane] = frame->data[plane];
        output.row_stride_in_bytes[plane] = size_t(frame->linesize[plane]);
        output.plane_size_in_bytes[plane] = output.row_stride_in_bytes[plane] * planeHeight;
    }
    phase.next(3);
    const pyrowave_result result = pyrowave_decoder_decode_cpu_buffer_synchronous(
        impl.decoder, &output);
    phase.next(4);
    if (result != PYROWAVE_SUCCESS) {
        m_LastError = std::string("GPU decode/readback failed: ") + resultString(result);
        return false;
    }

    frame->chroma_location = AVCHROMA_LOC_CENTER;
    frame->flags |= AV_FRAME_FLAG_KEY;
    return true;
#else
    int surface;
    uint64_t releaseValue;
    if (!impl.freeList->pop(surface, releaseValue)) {
        m_LastError = "no free output surface";
        return false;
    }

    std::array<pyrowave_gpu_external_reference, 3> acquireImages;
    std::array<pyrowave_gpu_external_reference, 3> releaseImages;
    for (int plane = 0; plane < 3; plane++) {
        // The previous contents are overwritten, so discard them
        acquireImages[plane] = { impl.surfaces[surface].images[plane], VK_QUEUE_FAMILY_IGNORED };
        releaseImages[plane] = { impl.surfaces[surface].images[plane], VK_QUEUE_FAMILY_EXTERNAL };
    }

    pyrowave_gpu_sync_operation acquire = {};
    acquire.images = acquireImages.data();
    acquire.num_images = acquireImages.size();
    if (releaseValue != 0) {
        // Wait on the GPU until the renderer finished its last read
        acquire.sync.semaphore = pyrowave_sync_object_get_semaphore(impl.releaseSync);
        acquire.sync.value = releaseValue;
    }

    const uint64_t decodeValue = impl.decodeValue + 1;
    pyrowave_gpu_sync_operation release = {};
    release.images = releaseImages.data();
    release.num_images = releaseImages.size();
    release.sync.semaphore = pyrowave_sync_object_get_semaphore(impl.decodeSync);
    release.sync.value = decodeValue;

    phase.next(3);
    const pyrowave_result result = pyrowave_decoder_decode_gpu_buffer(
        impl.decoder, &acquire, &release, &impl.surfaces[surface].buffers);
    phase.next(4);
    if (result != PYROWAVE_SUCCESS) {
        impl.freeList->push(surface, releaseValue);
        m_LastError = std::string("decode submission failed: ") + resultString(result);
        return false;
    }
    impl.decodeValue = decodeValue;

    auto ref = new PyroWaveFrameRef();
    ref->surface = surface;
    ref->decodeFenceValue = decodeValue;
    auto owner = new FrameOwner { impl.freeList };

    frame->buf[0] = av_buffer_create(reinterpret_cast<uint8_t*>(ref), sizeof(*ref),
                                     freeFrameRef, owner, 0);
    if (frame->buf[0] == nullptr) {
        // The GPU will still write this surface; the release value it needs
        // is unchanged because nobody sampled it.
        impl.freeList->push(surface, releaseValue);
        delete ref;
        delete owner;
        m_LastError = "out of memory";
        return false;
    }

    frame->width = impl.config.width;
    frame->height = impl.config.height;
    if (impl.config.tenBit) {
        frame->format = impl.config.chroma444 ? AV_PIX_FMT_YUV444P10 : AV_PIX_FMT_YUV420P10;
    }
    else {
        frame->format = impl.config.chroma444 ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUV420P;
    }
    // Hosts average each 2x2 quad for 4:2:0 chroma
    frame->chroma_location = AVCHROMA_LOC_CENTER;
    frame->flags |= AV_FRAME_FLAG_KEY;

    return true;
#endif
}
