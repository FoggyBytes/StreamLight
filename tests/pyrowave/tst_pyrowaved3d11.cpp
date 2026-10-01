// Exercises the Windows client decode path end to end without a host: frames
// from the vendored PyroWave encoder go through PyroWaveDecoder into the
// D3D11-owned shared surfaces, synchronized by the shared decode and release
// fences exactly as D3D11VARenderer uses them. The planes are read back on the
// D3D11 side and compared with the encoder input. Frames are released out of
// order so surface recycling waits on real release-fence values.

#include "../../app/streaming/video/ffmpeg-renderers/d3d11pyrowave.h"
#include "../../app/streaming/video/pyrowave/pyrowavedecoder.h"

#include <vulkan/vulkan.h>
#include <pyrowave.h>

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

int g_Failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_Failures;
    }
}

void putU32(std::vector<uint8_t>& out, uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        out.push_back(uint8_t(value >> (8 * i)));
    }
}

struct Source {
    int width, height;
    bool chroma444;
    std::vector<uint8_t> planes[3];

    int planeWidth(int plane) const { return (plane == 0 || chroma444) ? width : width / 2; }
    int planeHeight(int plane) const { return (plane == 0 || chroma444) ? height : height / 2; }

    void fill(int frameIndex)
    {
        for (int plane = 0; plane < 3; plane++) {
            planes[plane].resize(size_t(planeWidth(plane)) * planeHeight(plane));
            for (int y = 0; y < planeHeight(plane); y++) {
                for (int x = 0; x < planeWidth(plane); x++) {
                    int value = plane == 0 ? (x * 2 + y + frameIndex * 11) % 200 + 20
                                           : 128 + ((x + y * plane + frameIndex * 7) % 48) - 24;
                    planes[plane][size_t(y) * planeWidth(plane) + x] = uint8_t(value);
                }
            }
        }
    }

    pyrowave_cpu_buffer buffer()
    {
        pyrowave_cpu_buffer buf = {};
        for (int plane = 0; plane < 3; plane++) {
            buf.data[plane] = planes[plane].data();
            buf.row_stride_in_bytes[plane] = size_t(planeWidth(plane));
            buf.plane_size_in_bytes[plane] = planes[plane].size();
        }
        buf.width = width;
        buf.height = height;
        buf.format = chroma444 ? PYROWAVE_CPU_BUFFER_FORMAT_YUV444P : PYROWAVE_CPU_BUFFER_FORMAT_YUV420P;
        return buf;
    }
};

// Record framing as the host sends it (strict order is enough for the parser)
std::vector<uint8_t> encodeFrame(pyrowave_encoder encoder, Source& source, size_t budget)
{
    auto input = source.buffer();
    pyrowave_rate_control rate = { budget };
    if (pyrowave_encoder_encode_cpu_synchronous(encoder, &input, &rate) != PYROWAVE_SUCCESS) {
        return {};
    }

    const size_t shard = 1376;
    size_t count = 0;
    pyrowave_encoder_compute_num_packets_with_padding(encoder, shard, 8, &count);
    std::vector<pyrowave_packet> packets(count);
    std::vector<uint8_t> bitstream(budget + (1 << 20));
    size_t written = 0;
    pyrowave_encoder_packetize_with_padding(encoder, packets.data(), shard, 8, &written,
                                            bitstream.data(), bitstream.size());

    std::vector<uint8_t> out;
    size_t boundary = shard - 8;
    for (size_t i = 0; i < written; i++) {
        const auto& packet = packets[i];
        const size_t remaining = boundary - out.size();
        if (packet.size > remaining && packet.size <= shard && remaining >= 8) {
            putU32(out, 0xFFFFFFFFu);
            putU32(out, uint32_t((remaining - 8) / 4));
            out.resize(out.size() + (remaining - 8), 0);
        }
        out.insert(out.end(), bitstream.begin() + packet.offset, bitstream.begin() + packet.offset + packet.size);
        while (boundary <= out.size()) {
            boundary += shard;
        }
    }
    return out;
}

double comparePlane(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* view,
                    const std::vector<uint8_t>& expected, int width, int height, bool r16)
{
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = r16 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) {
        return 0;
    }
    context->CopyResource(staging.Get(), resource.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return 0;
    }

    double sum = 0;
    for (int y = 0; y < height; y++) {
        const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch;
        for (int x = 0; x < width; x++) {
            // Decoded values are normalized, so an R16 plane holds value * 257
            const double actual = r16 ? reinterpret_cast<const uint16_t*>(row)[x] / 257.0 : row[x];
            const double diff = actual - expected[size_t(y) * width + x];
            sum += diff * diff;
        }
    }
    context->Unmap(staging.Get(), 0);

    const double mse = sum / (double(width) * height);
    return mse == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

void runCase(int width, int height, bool chroma444, bool tenBit)
{
    const std::string name = std::to_string(width) + "x" + std::to_string(height) +
                             (chroma444 ? " 4:4:4" : " 4:2:0") + (tenBit ? " R16" : " R8");

    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    factory->EnumAdapters1(0, &adapter);
    DXGI_ADAPTER_DESC1 adapterDesc;
    adapter->GetDesc1(&adapterDesc);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 2,
                                 D3D11_SDK_VERSION, &device, nullptr, &context))) {
        expect(false, name + ": D3D11 device");
        return;
    }
    ComPtr<ID3D11Device5> device5;
    ComPtr<ID3D11DeviceContext4> context4;
    device.As(&device5);
    context.As(&context4);

    D3D11PyroWaveSurfaces surfaces;
    if (!surfaces.initialize(device5.Get(), adapterDesc.AdapterLuid, width, height, chroma444, tenBit)) {
        expect(false, name + ": surface pool");
        return;
    }

    PyroWaveDecoder decoder;
    PyroWaveDecoder::Config config;
    config.width = width;
    config.height = height;
    config.chroma444 = chroma444;
    config.tenBit = tenBit;
    if (!decoder.initialize(config, &surfaces)) {
        expect(false, name + ": decoder initialization");
        return;
    }

    pyrowave_device encodeDevice = nullptr;
    pyrowave_create_default_device(&encodeDevice);
    pyrowave_encoder_create_info encoderInfo = {};
    encoderInfo.device = encodeDevice;
    encoderInfo.width = width;
    encoderInfo.height = height;
    encoderInfo.chroma = chroma444 ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420;
    pyrowave_encoder encoder = nullptr;
    pyrowave_encoder_create(&encoderInfo, &encoder);

    const size_t budget = size_t(double(width) * height * 1.6 / 8 * (chroma444 ? 1.625 : 1.0));

    Source source {width, height, chroma444};
    std::deque<AVFrame*> held;
    double worstPsnr = 99;
    int decoded = 0;

    for (int frameIndex = 0; frameIndex < 40; frameIndex++) {
        source.fill(frameIndex);
        const auto framed = encodeFrame(encoder, source, budget);

        AVFrame* frame = av_frame_alloc();
        if (!decoder.decode(framed.data(), framed.size(), {}, 0, frame)) {
            expect(false, name + ": decode frame " + std::to_string(frameIndex) + ": " + decoder.lastError());
            av_frame_free(&frame);
            continue;
        }
        decoded++;

        auto* ref = PyroWaveFrameRef::fromFrame(frame);
        expect(ref != nullptr, name + ": frame carries a surface reference");
        if (ref == nullptr) {
            av_frame_free(&frame);
            continue;
        }

        // The renderer's CPU readiness wait, then its GPU wait and reads
        ID3D11Fence* fence = surfaces.decodeFence();
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        fence->SetEventOnCompletion(ref->decodeFenceValue, event);
        expect(WaitForSingleObject(event, 2000) == WAIT_OBJECT_0, name + ": decode fence completes");
        CloseHandle(event);

        expect(surfaces.waitForDecode(context4.Get(), ref), name + ": GPU wait queued");
        const auto* views = surfaces.planeViews(ref->surface);
        for (int plane = 0; plane < 3; plane++) {
            const double quality = comparePlane(device.Get(), context.Get(), (*views)[plane].Get(),
                                                source.planes[plane], source.planeWidth(plane),
                                                source.planeHeight(plane), tenBit);
            worstPsnr = (std::min)(worstPsnr, quality);
        }
        expect(surfaces.signalRelease(context4.Get(), ref), name + ": release signalled");

        // Hold a few frames like the pacer does, releasing them out of order
        held.push_back(frame);
        if (held.size() > 4) {
            size_t victim = frameIndex % 3 == 0 ? 1 : 0;
            AVFrame* old = held[victim];
            held.erase(held.begin() + victim);
            av_frame_free(&old);
        }
    }

    while (!held.empty()) {
        AVFrame* old = held.front();
        held.pop_front();
        av_frame_free(&old);
    }

    expect(decoded == 40, name + ": decoded all frames");
    expect(worstPsnr > 30.0, name + ": worst plane PSNR " + std::to_string(worstPsnr));
    std::printf("%s: %d frames through D3D11 surfaces, worst plane PSNR %.1f dB\n",
                name.c_str(), decoded, worstPsnr);

    pyrowave_encoder_destroy(encoder);
    pyrowave_device_destroy(encodeDevice);
}

}

int main(int, char**)
{
    runCase(1920, 1080, false, false);
    runCase(1920, 1080, false, true);
    runCase(1280, 720, true, false);
    runCase(2560, 1440, true, true);

    if (g_Failures == 0) {
        std::printf("PyroWave D3D11 decode path: all checks passed\n");
    }
    return g_Failures ? 1 : 0;
}
