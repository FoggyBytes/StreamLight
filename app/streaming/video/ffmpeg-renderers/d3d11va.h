#pragma once

#include "renderer.h"

#include <d3d11_4.h>
#include <dxgi1_6.h>

extern "C" {
#include <libavutil/hwcontext_d3d11va.h>
}

#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

class D3D11VARenderer : public IFFmpegRenderer
{
public:
    D3D11VARenderer(int decoderSelectionPass);
    virtual ~D3D11VARenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary**) override;
    virtual bool prepareDecoderContextInGetFormat(AVCodecContext* context, AVPixelFormat pixelFormat) override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual void notifyOverlayUpdated(Overlay::OverlayType) override;
    virtual bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO stateInfo) override;
    virtual int getRendererAttributes() override;
    virtual bool getPacingMeasurement(PPACING_MEASUREMENT measurement) override;
    virtual int getDecoderCapabilities() override;
    virtual InitFailureReason getInitFailureReason() override;

    enum PixelShaders {
        GENERIC_YUV_420,
        GENERIC_AYUV,
        GENERIC_Y410,
        _COUNT
    };

private:
    static void lockContext(void* lock_ctx);
    static void unlockContext(void* lock_ctx);
    void sampleCadence(uint64_t presentStartUs, uint64_t presentEndUs);
    void flushCadenceWindow(uint64_t nowUs);

    bool setupRenderingResources();
    std::vector<DXGI_FORMAT> getVideoTextureSRVFormats();
    bool setupFrameRenderingResources(AVHWFramesContext* framesContext);
    bool setupSwapchainDependentResources();
    bool setupVideoTexture(AVHWFramesContext* framesContext); // for !m_BindDecoderOutputTextures
    bool setupTexturePoolViews(AVHWFramesContext* framesContext); // for m_BindDecoderOutputTextures
    void renderOverlay(Overlay::OverlayType type);
    bool createOverlayVertexBuffer(Overlay::OverlayType type, int width, int height, Microsoft::WRL::ComPtr<ID3D11Buffer>& newVertexBuffer);
    void bindColorConversion(bool frameChanged, AVFrame* frame);
    void bindVideoVertexBuffer(bool frameChanged, AVFrame* frame);
    void renderVideo(AVFrame* frame);
    bool checkDecoderSupport(IDXGIAdapter* adapter);
    bool createDeviceByAdapterIndex(int adapterIndex, bool* adapterNotFound = nullptr);
    bool setupSharedDevice(IDXGIAdapter1* adapter);
    bool createSharedFencePair(UINT64 initialValue,
                               ID3D11Device5* dev1, ID3D11Device5* dev2,
                               Microsoft::WRL::ComPtr<ID3D11Fence>& dev1Fence,
                               Microsoft::WRL::ComPtr<ID3D11Fence>& dev2Fence);

    int m_DecoderSelectionPass;
    int m_DevicesWithFL11Support;
    int m_DevicesWithCodecSupport;

    enum class SupportedFenceType {
        None,
        NonMonitored,
        Monitored,
    };

    Microsoft::WRL::ComPtr<IDXGIFactory5> m_Factory;
    int m_AdapterIndex;
    Microsoft::WRL::ComPtr<ID3D11Device5> m_RenderDevice, m_DecodeDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> m_RenderDeviceContext, m_DecodeDeviceContext;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_RenderSharedTextureArray;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_VideoBlendState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_OverlayBlendState;

    SupportedFenceType m_FenceType;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_DecodeD2RFence, m_RenderD2RFence;
    UINT64 m_D2RFenceValue;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_DecodeR2DFence, m_RenderR2DFence;
    UINT64 m_R2DFenceValue;
    SDL_mutex* m_ContextLock;
    bool m_BindDecoderOutputTextures;

    DECODER_PARAMETERS m_DecoderParams;
    DXGI_FORMAT m_TextureFormat;
    int m_DisplayWidth;
    int m_DisplayHeight;
    AVColorTransferCharacteristic m_LastColorTrc;

    bool m_AllowTearing;

    // V-blanks to hold each presented frame (DXGI sync interval). 0 = present
    // immediately, which is upstream Moonlight's behaviour and still the default.
    // 2..4 when the 5.6.0 fractional V-Sync experiment is on and the panel runs at a
    // whole multiple of the stream's frame rate.
    int m_SyncInterval;

    // --- Presentation cadence diagnostics (issues #9 and #11) ------------------
    // Measures what the display pipeline actually did with our presents: how many
    // V-blanks each frame occupied, how deep the DXGI present queue settled, and how
    // long Present() blocked. Inert unless the Cadence overlay line is switched on or
    // STREAMLIGHT_PACING_DIAG=1 is set.
    //
    // ⚠️ The 5.2.0 removal note said, correctly, that the old version of this wrote its
    // summary with SDL_LogInfo from inside renderFrame() — a locked, flushed file write
    // inside the very span the Pacer reports as "rendering time", costing most exactly
    // when the fault was present. That is fixed here rather than reintroduced: nothing
    // on this path logs. Each closed window is published under m_CadenceLock and the
    // line is written by whoever reads it (ffmpeg.cpp, on the decoder thread). What
    // remains on the render thread is a QPC pair and one GetFrameStatistics() call.
    struct CadenceDiag {
        DXGI_FRAME_STATISTICS lastStats;
        bool haveBaseline;          // lastStats is usable for a delta
        int consecutiveFailures;

        uint64_t windowStartUs;
        uint32_t presentCalls;      // Present() calls made in this window
        uint64_t sumRefreshes;      // V-blanks consumed by completed presents
        uint64_t sumPresents;       // completed presents those V-blanks belong to
        // ⚠️ 5.1.x also kept a histogram of V-blanks per frame here. It is not back: min,
        // max and the slip count answer the question it was built for — "did every frame
        // get the same number of V-blanks" — and a buffer nobody reads is how the last
        // version of this file grew fields the log had stopped printing.
        int minVblanks;
        int maxVblanks;

        uint64_t waitSumUs;
        uint64_t waitMinUs;
        uint64_t waitMaxUs;
        uint32_t waitOverCount;     // presents blocked for more than half a frame period
        uint32_t slips;             // frames held for a different count than we asked for

        // Present() calls made since the baseline, against the count DXGI reports
        // as completed: the difference is how deep the present queue is sitting.
        uint64_t presentsSubmitted;
        uint32_t baselinePresentCount;

        int64_t queueSum;
        uint32_t queueSamples;
        int queueMin;
        int queueMax;

        uint32_t disjointCount;
    };

    // PCI vendor of the adapter we ended up rendering on, captured where the adapter is
    // chosen because that is the only place it is known. Used to decide whether the
    // refresh counter can be believed - see initialize().
    UINT m_AdapterVendorId;

    bool m_CadenceDiagEnabled;

    // False when the adapter's presentation refresh counter disagrees with the panel, in
    // which case the V-blank figures are omitted while the wait and queue figures - which
    // do not come from that counter - are still reported.
    bool m_CadenceRefreshTrusted;

    int m_DisplayHz;
    CadenceDiag m_Cadence;

    // Published once per window for whoever wants to read it, which is the decoder
    // thread while renderFrame() writes from the render thread.
    SDL_SpinLock m_CadenceLock;
    PACING_MEASUREMENT m_PublishedCadence;
    bool m_HavePublishedCadence;
    unsigned long long m_CadenceSequence;

    std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>, PixelShaders::_COUNT> m_VideoPixelShaders;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_VideoVertexBuffer;

    // Only valid if !m_BindDecoderOutputTextures
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_VideoTexture;

    // Only index 0 is valid if !m_BindDecoderOutputTextures
    std::vector<std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2>> m_VideoTextureResourceViews;

    SDL_SpinLock m_OverlayLock;
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, Overlay::OverlayMax> m_OverlayVertexBuffers;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, Overlay::OverlayMax> m_OverlayTextures;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, Overlay::OverlayMax> m_OverlayTextureResourceViews;

    // The objects last drawn for each overlay. A frame that cannot take m_OverlayLock
    // draws these again rather than dropping the overlay for that frame - see
    // renderOverlay(). Owned by the render thread; the only other toucher is the resize
    // path, which holds the context lock and therefore cannot run concurrently with it.
    struct LastOverlay {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resourceView;
    };
    std::array<LastOverlay, Overlay::OverlayMax> m_LastOverlay;

    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_OverlayPixelShader;

    AVBufferRef* m_HwDeviceContext;
};

