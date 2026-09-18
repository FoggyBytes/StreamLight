// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include "presentationclock.h"

#include <d3d11_4.h>
#include <dcomp.h>
#include <presentation.h>
#include <wrl/client.h>
#include <array>

// A presentation manager owns directly displayable render targets. The caller
// uses its existing shaders and GPU-ready fence, then presents at its existing
// pacing deadline. This class never waits for a refresh or a statistics event.
// All methods are serialized by the renderer's context lock.
class D3D11CompositionPresenter {
public:
    struct DisplayedFrame {
        uint64_t id = 0;
        PresentationClockSample clock;
    };

    ~D3D11CompositionPresenter();
    static bool runtimeSupported();
    static bool deviceSupported(ID3D11Device* device);
    HRESULT initialize(ID3D11Device* device, HWND window, UINT width, UINT height,
                       DXGI_FORMAT format, LUID adapter, UINT sourceId);
    void reset();
    bool active() const { return m_Manager != nullptr; }
    HRESULT resize(UINT width, UINT height, DXGI_FORMAT format);
    HRESULT acquire(ID3D11RenderTargetView** view);
    void cancel() { m_Acquired = m_Buffers.size(); }
    HRESULT setColorSpace(DXGI_COLOR_SPACE_TYPE colorSpace);
    HRESULT present(uint64_t& id);
    bool pollDisplayedFrame(uint64_t (*clockUs)(), DisplayedFrame& frame);
    uint64_t composedFrames() const { return m_ComposedFrames; }
    uint64_t independentFrames() const { return m_IndependentFrames; }
    uint64_t rejectedDisplayFrames() const { return m_RejectedDisplayFrames; }

private:
    struct Buffer {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
        Microsoft::WRL::ComPtr<IPresentationBuffer> presentation;
    };
    // Buffer storage is not a queue target. Only available buffers are used;
    // old pending presents are canceled before posting the newest ready one.
    std::array<Buffer, 5> m_Buffers;
    size_t m_Acquired = m_Buffers.size();
    size_t m_NextBuffer = 0;
    HMODULE m_Module = nullptr;
    HANDLE m_SurfaceHandle = nullptr;
    HANDLE m_LostEvent = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
    Microsoft::WRL::ComPtr<IDCompositionDevice> m_Composition;
    Microsoft::WRL::ComPtr<IDCompositionTarget> m_Target;
    Microsoft::WRL::ComPtr<IDCompositionVisual> m_Visual;
    Microsoft::WRL::ComPtr<IPresentationManager> m_Manager;
    Microsoft::WRL::ComPtr<IPresentationSurface> m_Surface;
    LUID m_Adapter = {};
    UINT m_SourceId = 0;
    uint64_t m_LastDisplayedId = 0;
    uint64_t m_ComposedFrames = 0;
    uint64_t m_IndependentFrames = 0;
    uint64_t m_RejectedDisplayFrames = 0;
};
