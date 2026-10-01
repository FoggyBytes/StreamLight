// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include "pyrowaveframing.h"
#include "pyrowavesurfaces.h"

#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
}

// Decodes PyroWave frames on Vulkan. Windows shares GPU surfaces with the
// renderer. Linux decodes on the renderer's own VkDevice into planes it lends
// (IPyroWaveVulkanPool), or without one reads planar frames back to the CPU.
// Not thread safe: decode() must be called from one thread. Frames it produces
// may be freed from any thread.
class PyroWaveDecoder
{
public:
    struct Config {
        int width = 0;
        int height = 0;
        bool chroma444 = false;
        bool tenBit = false;
#ifndef _WIN32
        // Optional; without it frames are read back into system memory
        IPyroWaveVulkanPool* vulkanPool = nullptr;
#endif
    };

    PyroWaveDecoder();
    ~PyroWaveDecoder();

    PyroWaveDecoder(const PyroWaveDecoder&) = delete;
    PyroWaveDecoder& operator=(const PyroWaveDecoder&) = delete;

    bool initialize(const Config& config, IPyroWaveSurfacePool* pool);

    // Shared surfaces return after submission; CPU readback returns complete.
    bool hasAsynchronousOutput() const;

    // CPU wall times, not GPU execution times. Submission can wait for older work.
    struct DecodeDiagnostics {
        uint64_t phaseUs[5] = {}; // parse, push/validate, acquire, submit, release
        uint64_t contextWaitUs = 0; // portion of submit inside Granite::next_frame_context
        uint64_t payloadBytes = 0;
        uint32_t receivedBlocks = 0;
        uint32_t announcedBlocks = 0;
        uint32_t paddingBytes = 0;
        bool partial = false;
    };

    // Parses and decodes one frame. With shared surfaces the GPU work is only
    // submitted and the renderer orders its reads after it; the readback path
    // waits and returns a planar AVFrame in system memory. Returns false if
    // the frame was dropped.
    // packets maps the frame's RTP packets and which of them were lost (empty
    // for a frame that arrived whole); what survived is decoded when the
    // coarsest wavelet level did, which the host announces as the first
    // criticalPackets packets (0 if it did not).
    bool decode(const uint8_t* data, size_t size,
                const std::vector<PyroWaveFraming::Segment>& packets, size_t criticalPackets,
                AVFrame* frame, DecodeDiagnostics* diagnostics = nullptr);

    // Why the last call failed, for logging.
    const std::string& lastError() const { return m_LastError; }

    // Whether the last decoded frame was missing records
    bool lastFramePartial() const { return m_LastFramePartial; }

    // Framing seen in the most recent successfully parsed frame.
    PyroWaveFraming::Framing lastFraming() const { return m_LastFraming; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
    std::string m_LastError;
    PyroWaveFraming::Framing m_LastFraming = PyroWaveFraming::Framing::Records;
    bool m_LastFramePartial = false;
};
