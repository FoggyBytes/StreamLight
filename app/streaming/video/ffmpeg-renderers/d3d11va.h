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
    virtual int getFramePacingSyncInterval() override { return m_SyncInterval; }
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
    void sampleCadence(uint64_t presentStartUs, uint64_t presentEndUs, int64_t qpcAfterPresent);
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
    // immediately (default). >=2 gives low-FPS streams a hardware-locked cadence
    // on high-refresh displays when "Smooth low-FPS streams" is enabled.
    int m_SyncInterval;

    // --- Frame pacing diagnostics (issue #9) ---------------------------------
    // Measures what the display pipeline actually did with our presents: how many
    // V-blanks each frame occupied, how deep the DXGI present queue settled, and
    // how long Present() blocked. Entirely inert unless STREAMLIGHT_PACING_DIAG=1,
    // because it costs a GetFrameStatistics() call and a log line per second.
    static const int k_CadenceBuckets = 8;

    struct CadenceDiag {
        DXGI_FRAME_STATISTICS lastStats;
        bool haveBaseline;          // lastStats is usable for a delta
        int consecutiveFailures;

        uint64_t windowStartUs;
        uint32_t presentCalls;      // Present() calls made in this window
        uint64_t sumRefreshes;      // V-blanks consumed by completed presents
        uint64_t sumPresents;       // completed presents those V-blanks belong to
        uint32_t buckets[k_CadenceBuckets];
        int minVblanks;
        int maxVblanks;

        uint64_t waitSumUs;
        uint64_t waitMinUs;
        uint64_t waitMaxUs;
        uint32_t waitOverCount;     // presents blocked for more than 1.5 frame periods


        // Present() calls made since the baseline, against the count DXGI reports
        // as completed: the difference is how deep the present queue is sitting.
        uint64_t presentsSubmitted;
        uint32_t baselinePresentCount;

        int64_t queueSum;
        uint32_t queueSamples;
        int queueMin;
        int queueMax;

        uint64_t phaseSumUs;
        uint32_t phaseSamples;

        uint32_t disjointCount;
        uint32_t slipsLogged;
    };

    bool m_CadenceDiagEnabled;

    // Emission state for the [pacing] log, kept out of CadenceDiag because it spans
    // windows rather than belonging to one — that struct is wiped every second and
    // only a named few fields survive it.
    uint64_t m_CadenceLastLogUs;
    double m_CadenceLastLogWaitMs;
    int m_DisplayHz;
    int64_t m_QpcFrequency;
    CadenceDiag m_Cadence;

    // Published once per window for the performance overlay, which reads it from
    // the decoder thread while renderFrame() writes from the render thread.
    SDL_SpinLock m_CadenceLock;
    PACING_MEASUREMENT m_PublishedCadence;
    bool m_HavePublishedCadence;

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

