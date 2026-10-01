// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include "streaming/video/pyrowave/pyrowavesurfaces.h"

#include <d3d11_4.h>
#include <wrl/client.h>

#include <array>
#include <vector>

// D3D11 side of the PyroWave surface pool. The textures and both timeline
// fences are created on the renderer's device and shared with the PyroWave
// Vulkan decoder through NT handles:
//
//   decode fence:  Vulkan signals after writing a surface; the render context
//                  waits for that value before sampling.
//   release fence: the render context signals after its last read of a
//                  surface; Vulkan waits for it before overwriting.
class D3D11PyroWaveSurfaces : public IPyroWaveSurfacePool
{
public:
    // Enough for the pacer's outstanding frames, the VRR worker's extra
    // queued frame, and one decode plus one render in flight.
    static constexpr int k_SurfaceCount = 10;

    bool initialize(ID3D11Device5* device, const LUID& adapterLuid,
                    int width, int height, bool chroma444, bool tenBit);

    // IPyroWaveSurfacePool
    bool pyroWaveAdapterLuid(uint8_t luid[8]) override;
    int pyroWaveSurfaceCount() const override { return (int)m_Surfaces.size(); }
    bool exportPyroWaveSurface(int index, PyroWaveSharedPlane planes[3]) override;
    uintptr_t exportPyroWaveDecodeFence() override;
    uintptr_t exportPyroWaveReleaseFence() override;

    // Queues a GPU wait for the frame's decode on the render context.
    bool waitForDecode(ID3D11DeviceContext4* context, const PyroWaveFrameRef* ref);

    // Signals the release fence after the frame's planes were read, records the
    // value in the frame and flushes so the decoder's GPU wait can resolve.
    bool signalRelease(ID3D11DeviceContext4* context, PyroWaveFrameRef* ref);

    // Shader resource views for the Y, Cb and Cr planes of a surface.
    const std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 3>* planeViews(int surface) const;

    ID3D11Fence* decodeFence() const { return m_DecodeFence.Get(); }

private:
    uintptr_t exportFence(ID3D11Fence* fence);

    struct Surface {
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 3> planes;
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 3> views;
    };

    LUID m_AdapterLuid = {};
    DXGI_FORMAT m_PlaneFormat = DXGI_FORMAT_R8_UNORM;
    std::vector<Surface> m_Surfaces;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_DecodeFence;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_ReleaseFence;
    UINT64 m_ReleaseValue = 0;
};
