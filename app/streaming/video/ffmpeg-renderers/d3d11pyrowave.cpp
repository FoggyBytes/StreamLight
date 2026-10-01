// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "d3d11pyrowave.h"

#include <SDL.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

bool D3D11PyroWaveSurfaces::initialize(ID3D11Device5* device, const LUID& adapterLuid,
                                       int width, int height, bool chroma444, bool tenBit)
{
    HRESULT hr;

    m_AdapterLuid = adapterLuid;
    m_PlaneFormat = tenBit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;

    const UINT chromaWidth = chroma444 ? width : (width + 1) / 2;
    const UINT chromaHeight = chroma444 ? height : (height + 1) / 2;

    m_Surfaces.resize(k_SurfaceCount);
    for (auto& surface : m_Surfaces) {
        for (int plane = 0; plane < 3; plane++) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = plane == 0 ? width : chromaWidth;
            desc.Height = plane == 0 ? height : chromaHeight;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = m_PlaneFormat;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            // Vulkan writes the planes as storage images, which needs UAV support
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

            hr = device->CreateTexture2D(&desc, nullptr, &surface.planes[plane]);
            if (FAILED(hr)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "PyroWave: creating a %ux%u plane texture failed: %x",
                             desc.Width, desc.Height, hr);
                return false;
            }

            hr = device->CreateShaderResourceView(surface.planes[plane].Get(), nullptr, &surface.views[plane]);
            if (FAILED(hr)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "PyroWave: creating a plane SRV failed: %x", hr);
                return false;
            }
        }
    }

    hr = device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_DecodeFence));
    if (SUCCEEDED(hr)) {
        hr = device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_ReleaseFence));
    }
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave: creating the shared fences failed: %x", hr);
        return false;
    }

    return true;
}

bool D3D11PyroWaveSurfaces::pyroWaveAdapterLuid(uint8_t luid[8])
{
    static_assert(sizeof(LUID) == 8, "LUID is 8 bytes");
    memcpy(luid, &m_AdapterLuid, sizeof(m_AdapterLuid));
    return true;
}

bool D3D11PyroWaveSurfaces::exportPyroWaveSurface(int index, PyroWaveSharedPlane planes[3])
{
    if (index < 0 || index >= (int)m_Surfaces.size()) {
        return false;
    }

    for (int plane = 0; plane < 3; plane++) {
        ComPtr<IDXGIResource1> resource;
        HANDLE handle = nullptr;
        HRESULT hr = m_Surfaces[index].planes[plane].As(&resource);
        if (SUCCEEDED(hr)) {
            // GENERIC_ALL matches upstream PyroWave's tested D3D11 imports
            hr = resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle);
        }
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "PyroWave: sharing surface %d plane %d failed: %x", index, plane, hr);
            for (int i = 0; i < plane; i++) {
                CloseHandle(reinterpret_cast<HANDLE>(planes[i].handle));
                planes[i].handle = 0;
            }
            return false;
        }

        D3D11_TEXTURE2D_DESC desc;
        m_Surfaces[index].planes[plane]->GetDesc(&desc);
        planes[plane].handle = reinterpret_cast<uintptr_t>(handle);
        planes[plane].width = desc.Width;
        planes[plane].height = desc.Height;
        planes[plane].format = m_PlaneFormat == DXGI_FORMAT_R16_UNORM ?
            PyroWavePlaneFormat::R16Unorm : PyroWavePlaneFormat::R8Unorm;
    }

    return true;
}

uintptr_t D3D11PyroWaveSurfaces::exportFence(ID3D11Fence* fence)
{
    HANDLE handle = nullptr;
    HRESULT hr = fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave: sharing a fence failed: %x", hr);
        return 0;
    }
    return reinterpret_cast<uintptr_t>(handle);
}

uintptr_t D3D11PyroWaveSurfaces::exportPyroWaveDecodeFence()
{
    return exportFence(m_DecodeFence.Get());
}

uintptr_t D3D11PyroWaveSurfaces::exportPyroWaveReleaseFence()
{
    return exportFence(m_ReleaseFence.Get());
}

bool D3D11PyroWaveSurfaces::waitForDecode(ID3D11DeviceContext4* context, const PyroWaveFrameRef* ref)
{
    HRESULT hr = context->Wait(m_DecodeFence.Get(), ref->decodeFenceValue);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave: decode fence Wait() failed: %x (target=%llu)",
                     hr, (unsigned long long)ref->decodeFenceValue);
        return false;
    }
    return true;
}

bool D3D11PyroWaveSurfaces::signalRelease(ID3D11DeviceContext4* context, PyroWaveFrameRef* ref)
{
    const UINT64 value = ++m_ReleaseValue;
    HRESULT hr = context->Signal(m_ReleaseFence.Get(), value);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave: release fence Signal() failed: %x (value=%llu)",
                     hr, (unsigned long long)value);
        return false;
    }

    // Submit now: the decoder may already be waiting on this value on the GPU,
    // and a Signal left in the context would only be sent at the next Present.
    context->Flush();
    ref->noteRelease(value);
    return true;
}

const std::array<ComPtr<ID3D11ShaderResourceView>, 3>* D3D11PyroWaveSurfaces::planeViews(int surface) const
{
    if (surface < 0 || surface >= (int)m_Surfaces.size()) {
        return nullptr;
    }
    return &m_Surfaces[surface].views;
}
