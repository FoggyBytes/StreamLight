// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

// Contract between the PyroWave decoder (Vulkan) and a renderer that owns the
// decoded planes. The renderer allocates a pool of shareable plane surfaces and
// two timeline fences in its own graphics API; the decoder imports them into its
// Vulkan device, decodes into a free surface, and signals the decode fence.
// The renderer waits for that value before sampling and signals the release
// fence after its last read, which the decoder waits for before reusing the
// surface. Neither side blocks the CPU on the other.

#include <atomic>
#include <cstdint>

extern "C" {
#include <libavutil/frame.h>
}

enum class PyroWavePlaneFormat {
    R8Unorm,
    R16Unorm,
};

struct PyroWaveSharedPlane {
    // OS handle (Windows NT HANDLE) that the decoder takes ownership of.
    uintptr_t handle = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    PyroWavePlaneFormat format = PyroWavePlaneFormat::R8Unorm;
};

class IPyroWaveSurfacePool {
public:
    virtual ~IPyroWaveSurfacePool() = default;

    // Identity of the adapter that owns the surfaces (8-byte Windows LUID).
    virtual bool pyroWaveAdapterLuid(uint8_t luid[8]) = 0;

    virtual int pyroWaveSurfaceCount() const = 0;

    // Returns fresh OS handles for the three planes (Y, Cb, Cr) of a surface.
    // The caller owns them and must close them if it does not consume them.
    virtual bool exportPyroWaveSurface(int index, PyroWaveSharedPlane planes[3]) = 0;

    // Fresh OS handles for the shared timeline fences. The decoder signals the
    // decode fence; the renderer signals the release fence.
    virtual uintptr_t exportPyroWaveDecodeFence() = 0;
    virtual uintptr_t exportPyroWaveReleaseFence() = 0;
};

// Owned by each PyroWave AVFrame through frame->buf[0]. Freeing the last
// reference returns the surface to the decoder with the release fence value
// that makes it safe to overwrite.
struct PyroWaveFrameRef {
    static constexpr uint32_t k_Magic = 0x50595257; // "PYRW"

    uint32_t magic = k_Magic;
    int surface = -1;
    // Value of the decode fence signalled when the planes are written.
    uint64_t decodeFenceValue = 0;
    // Highest release fence value the renderer signalled after sampling.
    std::atomic<uint64_t> releaseFenceValue {0};

    static PyroWaveFrameRef* fromFrame(const AVFrame* frame)
    {
        if (frame == nullptr || frame->buf[0] == nullptr ||
                frame->buf[0]->size != sizeof(PyroWaveFrameRef)) {
            return nullptr;
        }
        auto ref = reinterpret_cast<PyroWaveFrameRef*>(frame->buf[0]->data);
        return ref->magic == k_Magic ? ref : nullptr;
    }

    void noteRelease(uint64_t value)
    {
        uint64_t current = releaseFenceValue.load(std::memory_order_relaxed);
        while (current < value &&
               !releaseFenceValue.compare_exchange_weak(current, value, std::memory_order_release)) {
        }
    }
};

#ifdef __linux__
#include <vulkan/vulkan.h>

// Linux contract: the renderer shares its own VkDevice with the decoder and
// lends it plane images it owns. Timeline semaphores order the renderer's
// transition into VK_IMAGE_LAYOUT_GENERAL, the decode, and the renderer's
// later reads, so neither side waits on the CPU.
struct PyroWaveVulkanDevice {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    // The create infos describe what is enabled on the shared handles and
    // stay valid for the lifetime of the pool.
    const VkInstanceCreateInfo* instanceInfo = nullptr;
    const VkDeviceCreateInfo* deviceInfo = nullptr;
    // Serializes the decoder's queue submissions with the renderer's.
    void (*lockQueues)(void* userdata) = nullptr;
    void (*unlockQueues)(void* userdata) = nullptr;
    void* userdata = nullptr;
    // The device info includes a compute family separate from graphics, so
    // decoding can run alongside rendering.
    bool asyncCompute = false;
};

struct PyroWaveVulkanSurface {
    int index = -1;
    VkImage images[3] = {};
    uint32_t widths[3] = {};
    uint32_t heights[3] = {};
    VkFormat format = VK_FORMAT_UNDEFINED;
    // Write the planes once ready reaches readyValue; signal doneValue on done
    // when they are decoded. Each semaphore has a single signaller.
    VkSemaphore ready = VK_NULL_HANDLE;
    uint64_t readyValue = 0;
    VkSemaphore done = VK_NULL_HANDLE;
    uint64_t doneValue = 0;
};

class IPyroWaveVulkanPool {
public:
    virtual ~IPyroWaveVulkanPool() = default;

    virtual bool pyroWaveVulkanDevice(PyroWaveVulkanDevice& device) = 0;

    // Lends the planes of a free surface. Returns false if every surface is
    // still referenced by a frame.
    virtual bool holdPyroWaveSurface(int width, int height, bool chroma444, bool sixteenBit,
                                     PyroWaveVulkanSurface& surface) = 0;

    // Returns the planes to the renderer. When decoded is true, frame receives
    // format, size and a reference that keeps the surface in use until freed;
    // otherwise the surface goes straight back to the pool.
    virtual bool releasePyroWaveSurface(const PyroWaveVulkanSurface& surface, bool decoded,
                                        AVFrame* frame) = 0;
};
#endif
