// PyroWave Test (7.0.0). Derived from Nonary/moonlight-qt's app/streaming/video/pyrowave/
// pyrowavecalibrator.cpp at master f96b0524 (14/10/2026), by Chase Payne, GPLv3. Marked "From
// Nonary" below: the D3D11 test renderer, the synthetic source, the record framing, the format
// tester and calibrateFormat(), and the network searches inside run(), with his comments. The
// one change inside them is in Renderer::create(): pyrowave_device_confirm_interop_support()
// where his tree calls pyrowave_device_confirm_d3d11_interop_support(), which our PyroWave
// library (vrr18's) does not have — it is the check our stream path uses. Everything else (one
// format, the flow, per-host storage, the strings) is StreamLight's.

// Before anything pulls in windows.h: our pyrowavebitrate.h (vrr18's) calls std::min/std::max
// without the parentheses Nonary's master later added, and the min/max macros break it.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "pyrowavetest.h"

#include "backend/computermanager.h"
#include "backend/networkbuffers.h"
#include "backend/nvhttp.h"
#include "settings/appsettings.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"
#include "streaming/video/pyrowave/pyrowavebandwidth.h"
#include "streaming/video/pyrowave/pyrowavebitrate.h"
#include "streaming/video/pyrowave/pyrowavecalibrationpolicy.h"
#include "streaming/video/pyrowave/pyrowavelinkpolicy.h"

#include <QDateTime>
#include <QHostAddress>
#include <QMetaObject>
#include <QReadLocker>
#include <QSettings>
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(HAVE_PYROWAVE) && defined(Q_OS_WIN32)
#include "streaming/video/pyrowave/pyrowavedecoder.h"
#include "streaming/video/pyrowave/pyrowaveframing.h"
#include "streaming/video/ffmpeg-renderers/d3d11pyrowave.h"
#include <vulkan/vulkan.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <pyrowave.h>
#define PYROWAVE_TEST_AVAILABLE 1
#endif

namespace {

// Per host, under pyrowavetest/<uuid>/. Settings keys rather than NvComputer fields: a result
// is this device's measurement of a path, not something the host says about itself.
const char* const k_Group = "pyrowavetest";

QString targetName(int target)
{
    switch (target) {
    case PyroWaveTest::Minimum:  return QStringLiteral("Minimum");
    case PyroWaveTest::Moderate: return QStringLiteral("Moderate");
    case PyroWaveTest::Maximum:  return QStringLiteral("Maximum");
    default:                     return QStringLiteral("Recommended");
    }
}

struct Format {
    int width = 0;
    int height = 0;
    int fps = 0;
    bool chroma444 = false;
    bool hdr = false;
    bool pyroWave = false;
    int bitrateKbps = 0;
    int packetSize = 0;
};

Format effectiveFormat(const QString& hostUuid)
{
    // Settings, then the host's active profile — the cascade a launch uses, without the
    // per-game layer (the test is for the host, not for one game).
    StreamingPreferences* p = StreamingPreferences::get()->clone(nullptr);
    applyAppOverride(p, HostProfileManager::get()->activeOverride(hostUuid));
    Format f;
    f.width = p->width;
    f.height = p->height;
    f.fps = p->fps;
    f.chroma444 = p->enableYUV444;
    f.hdr = p->enableHdr;
    f.pyroWave = p->videoCodecConfig == StreamingPreferences::VCC_FORCE_PYROWAVE;
    f.bitrateKbps = p->bitrateKbps;
    f.packetSize = p->packetSize;
    delete p;
    return f;
}

QString formatText(const Format& f)
{
    return QStringLiteral("%1×%2 · %3 FPS · %4%5")
        .arg(f.width).arg(f.height).arg(f.fps)
        .arg(f.chroma444 ? QStringLiteral("YUV 4:4:4") : QStringLiteral("YUV 4:2:0"))
        .arg(f.hdr ? QStringLiteral(" · HDR") : QString());
}

#ifdef PYROWAVE_TEST_AVAILABLE

// ═══ From Nonary ═══════════════════════════════════════════════════════════════════════════

// Grades on the codec author's quality scale (see pyrowavebitrate.h): his
// good-quality level, and the level below which the picture is noticeably
// softer than it.
constexpr double kFullQualityDb = kPyroWaveGoodQualityDb;
constexpr double kReducedQualityDb = 32.0;
// The regression's lowest level. A format that falls behind at this bitrate
// falls behind at any useful one.
constexpr double kFloorQualityDb = PYROWAVE_REGRESSION_MIN_PSNR_HVS_M_H;
// Decoded frames waiting to be drawn, as a stream's decoder thread runs ahead
// of its renderer.
constexpr size_t kQueuedFrames = 3;
// Seconds of frames timed per probe, at least kMinimumTimedFrames, and the
// share whose cost must fit the frame period.
constexpr int kTimedSeconds = 3;
constexpr int kMinimumTimedFrames = 300;
constexpr double kCostPercentile = 0.99;

// The D3D11 check only waits for decode completion, on the decoding thread,
// so decoding may use three quarters of each frame period and rendering the
// rest
constexpr bool kConcurrentDraw = false;
constexpr double kPeriodShare = 0.75;

struct Sample {
    int width = 0;
    int height = 0;
    bool chroma444 = false;
    bool hdr = false;
    bool valid = false;
    bool keepsUp = false;
    // Share of the frame period its kCostPercentile frame uses
    double share = 0;
    // The link capped the bitrate below the author's recommendation
    bool linkLimited = false;
    // The bitrate was lowered below the cap so this device keeps up
    bool deviceLimited = false;
    int guideKbps = 0;
    int targetKbps = 0; // Requested image bitrate before network/device limits.
    int bitrateKbps = 0; // Applied total wire budget, including FEC.
    int imageKbps = 0;
    double qualityDb = 0;
    // At imageKbps or, when the format can't keep up, the lowest bitrate
    // tried: the kCostPercentile GPU time per frame, and its share of the
    // frame period
    double frameMs = 0;
    double load = 0;
    QString error;
};

int roundDownKbps(double kbps)
{
    return int(std::floor(kbps / 5000.0)) * 5000;
}

// Use the same D3D11 texture and fence sharing as a Windows stream. A one-pixel
// D3D11 readback waits for decode completion and verifies renderer access.
struct Renderer {
    Microsoft::WRL::ComPtr<ID3D11Device5> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    LUID adapterLuid = {};
    std::unique_ptr<D3D11PyroWaveSurfaces> pool;
    HANDLE event = nullptr;

    bool create()
    {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
        for (UINT index = 0;; ++index) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 description = {};
            if (FAILED(adapter->GetDesc1(&description)) ||
                    (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            Microsoft::WRL::ComPtr<ID3D11Device> baseDevice;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> baseContext;
            const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
            if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                         levels, 2, D3D11_SDK_VERSION, &baseDevice,
                                         nullptr, &baseContext)) ||
                    FAILED(baseDevice.As(&device)) || FAILED(baseContext.As(&context))) {
                device.Reset();
                context.Reset();
                continue;
            }
            static_assert(sizeof(pyrowave_luid) == sizeof(LUID), "LUID size mismatch");
            pyrowave_device decodeDevice = nullptr;
            const bool compatible = pyrowave_create_device_by_compat(
                0, 0, nullptr, nullptr,
                reinterpret_cast<const pyrowave_luid*>(&description.AdapterLuid),
                &decodeDevice) == PYROWAVE_SUCCESS;
            // StreamLight: the generic interop check our stream path uses, in place of
            // Nonary's newer per-format pyrowave_device_confirm_d3d11_interop_support().
            // Each format is still validated for real in Decoder::initialize.
            const bool interop = compatible && pyrowave_device_confirm_interop_support(decodeDevice);
            if (decodeDevice) pyrowave_device_destroy(decodeDevice);
            if (!interop) {
                device.Reset();
                context.Reset();
                continue;
            }
            adapterLuid = description.AdapterLuid;
            event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            return event != nullptr;
        }
        return false;
    }

    bool prepare(int width, int height, bool chroma444, bool hdr)
    {
        pool.reset();
        staging.Reset();
        pool = std::make_unique<D3D11PyroWaveSurfaces>();
        if (!pool->initialize(device.Get(), adapterLuid, width, height, chroma444, hdr)) return false;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = 1;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = hdr ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging));
    }

    bool waitForFrame(const AVFrame* frame)
    {
        auto* ref = PyroWaveFrameRef::fromFrame(frame);
        if (!ref || !pool || !pool->decodeFence() ||
                FAILED(pool->decodeFence()->SetEventOnCompletion(ref->decodeFenceValue, event)) ||
                WaitForSingleObject(event, 2000) != WAIT_OBJECT_0 ||
                !pool->waitForDecode(context.Get(), ref)) return false;
        const auto* views = pool->planeViews(ref->surface);
        if (!views) return false;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        (*views)[2]->GetResource(&resource);
        const D3D11_BOX box = {0, 0, 0, 1, 1, 1};
        context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, resource.Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
        context->Unmap(staging.Get(), 0);
        return pool->signalRelease(context.Get(), ref);
    }

    bool present(AVFrame* frame, bool)
    {
        return waitForFrame(frame);
    }

    ~Renderer()
    {
        pool.reset();
        if (event) CloseHandle(event);
    }
};

struct Planes {
    int width;
    int height;
    bool chroma444;
    std::vector<uint8_t> y, cb, cr;

    Planes(int w, int h, bool c444) : width(w), height(h), chroma444(c444)
    {
        const int cw = c444 ? w : w / 2;
        const int ch = c444 ? h : h / 2;
        y.resize(size_t(w) * h);
        cb.resize(size_t(cw) * ch);
        cr.resize(size_t(cw) * ch);
        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                int value = (col * 3 + row * 2 + 17) & 255;
                if (((col / 64) + (row / 64) + 1) % 7 == 0) value = 235;
                y[size_t(row) * w + col] = uint8_t(std::clamp(value, 16, 235));
            }
        }
        for (int row = 0; row < ch; ++row) {
            for (int col = 0; col < cw; ++col) {
                cb[size_t(row) * cw + col] = uint8_t(96 + (col + 5) % 64);
                cr[size_t(row) * cw + col] = uint8_t(96 + (row * 2 + 3) % 64);
            }
        }
    }

    pyrowave_cpu_buffer buffer()
    {
        const int cw = chroma444 ? width : width / 2;
        pyrowave_cpu_buffer result = {};
        result.width = width;
        result.height = height;
        result.format = chroma444 ? PYROWAVE_CPU_BUFFER_FORMAT_YUV444P : PYROWAVE_CPU_BUFFER_FORMAT_YUV420P;
        result.data[0] = y.data();
        result.data[1] = cb.data();
        result.data[2] = cr.data();
        result.row_stride_in_bytes[0] = size_t(width);
        result.row_stride_in_bytes[1] = result.row_stride_in_bytes[2] = size_t(cw);
        result.plane_size_in_bytes[0] = y.size();
        result.plane_size_in_bytes[1] = cb.size();
        result.plane_size_in_bytes[2] = cr.size();
        return result;
    }
};

void appendU32(std::vector<uint8_t>& out, uint32_t value)
{
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (8 * i)));
}

void appendPadding(std::vector<uint8_t>& out, size_t bytes)
{
    appendU32(out, 0xffffffffu);
    appendU32(out, uint32_t((bytes - 8) / 4));
    out.resize(out.size() + bytes - 8, 0);
}

// Repack the codec's records as a host-style frame, with the coarsest level
// first and no record boundary crossing an RTP payload.
std::vector<uint8_t> frameRecords(const std::vector<uint8_t>& bitstream,
                                  const std::vector<pyrowave_packet>& packets,
                                  size_t shard, uint32_t coarseBlocks)
{
    struct Record { size_t offset; size_t size; bool coarse; };
    std::vector<Record> records;
    for (const auto& packet : packets) {
        for (size_t pos = packet.offset; pos < packet.offset + packet.size;) {
            uint32_t first = 0, second = 0;
            std::memcpy(&first, bitstream.data() + pos, 4);
            std::memcpy(&second, bitstream.data() + pos + 4, 4);
            const bool header = (first & 0x80000000u) != 0;
            const size_t size = header ? 8 : size_t((first >> 16) & 0xfff) * 4;
            if (size < 8 || pos + size > packet.offset + packet.size) return {};
            records.push_back({pos, size, header || (second >> 8) < coarseBlocks});
            pos += size;
        }
    }
    if (records.empty()) return {};
    std::vector<uint8_t> out;
    auto remaining = [&] { return shard - (out.size() + 8) % shard; };
    auto place = [&](const Record& record) {
        out.insert(out.end(), bitstream.begin() + record.offset,
                   bitstream.begin() + record.offset + record.size);
    };
    place(records.front());
    for (bool coarse : {true, false}) {
        for (size_t i = 1; i < records.size(); ++i) {
            if (records[i].coarse == coarse && records[i].size + 8 > shard) {
                if (records[i].size % shard == remaining() - 4) appendPadding(out, 8);
                place(records[i]);
            }
        }
        for (size_t i = 1; i < records.size(); ++i) {
            if (records[i].coarse == coarse && records[i].size + 8 <= shard) {
                if (records[i].size > remaining() || remaining() - records[i].size == 4)
                    appendPadding(out, remaining());
                place(records[i]);
            }
        }
    }
    return out;
}

struct Probe {
    bool ok = false;
    // Mean GPU time per frame, used to stop a format early and to compare
    // bitrates
    double meanMs = 0;
    // The kCostPercentile GPU time per frame, and its share of the frame period
    double frameMs = 0;
    double load = 0;
    // Frames are served slower than they arrive, so the queue only grows
    bool overloaded = false;
    QString error;
};

double percentile(std::vector<double> values, double share)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const size_t index = (std::min)(values.size() - 1,
                                    size_t(std::ceil(double(values.size()) * share)) - 1);
    return values[index];
}

// One format's encoder, decoder and render path; probe() measures one bitrate.
class FormatTester {
public:
    FormatTester(pyrowave_device device, Renderer& renderer, int width, int height, int fps,
                 bool chroma444, bool hdr, int packetSize, const std::atomic<bool>& cancelled)
        : m_Renderer(renderer), m_Width(width), m_Height(height), m_Fps(fps),
          m_Chroma444(chroma444), m_Hdr(hdr), m_PacketSize(packetSize), m_Cancelled(cancelled), m_Source(width, height, chroma444)
    {
        pyrowave_encoder_create_info info = {};
        info.device = device;
        info.width = width;
        info.height = height;
        info.chroma = chroma444 ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420;
        if (pyrowave_encoder_create(&info, &m_Encoder) != PYROWAVE_SUCCESS) {
            m_Error = QStringLiteral("Could not initialize encoder");
            return;
        }
        if (!renderer.prepare(width, height, chroma444, hdr)) {
            m_Error = QStringLiteral("Could not create renderer surfaces");
            return;
        }
        PyroWaveDecoder::Config config;
        config.width = width;
        config.height = height;
        config.chroma444 = chroma444;
        config.tenBit = hdr;
        const bool initialized = m_Decoder.initialize(config, renderer.pool.get());
        if (!initialized) {
            m_Error = QStringLiteral("Could not initialize decoder");
        }
    }

    ~FormatTester()
    {
        if (m_Encoder) pyrowave_encoder_destroy(m_Encoder);
    }

    QString error() const { return m_Error; }

    // A synthetic frame encoded at the bitrate's per-frame budget, decoded and
    // drawn back to back. A short run warms clocks and compiles shaders and
    // stops a format that can't sustain the frame rate at all; unless quick,
    // a timed run follows.
    Probe probe(int bitrateKbps, bool quick = false)
    {
        Probe probe;
        if (!encode(bitrateKbps)) {
            probe.error = m_Error;
            return probe;
        }
        const double periodMs = 1000.0 / m_Fps;
        std::vector<double> serviceMs;
        if (!run((std::max)(24, m_Fps / 2), serviceMs)) {
            probe.error = runError();
            return probe;
        }
        auto summarize = [&] {
            probe.meanMs = std::accumulate(serviceMs.begin(), serviceMs.end(), 0.0) / serviceMs.size();
            probe.frameMs = percentile(serviceMs, kCostPercentile);
            probe.load = probe.frameMs / periodMs;
            probe.overloaded = probe.meanMs >= periodMs * kPeriodShare;
        };
        summarize();
        probe.ok = true;
        if (probe.overloaded || quick) {
            return probe;
        }
        if (!run((std::max)(kMinimumTimedFrames, m_Fps * kTimedSeconds), serviceMs)) {
            probe.ok = false;
            probe.error = runError();
            return probe;
        }
        summarize();
        return probe;
    }

private:
    bool encode(int bitrateKbps)
    {
        auto input = m_Source.buffer();
        pyrowave_rate_control rate = { size_t(double(bitrateKbps) * 1000.0 / m_Fps / 8.0) };
        if (pyrowave_encoder_encode_cpu_synchronous(m_Encoder, &input, &rate) != PYROWAVE_SUCCESS) {
            m_Error = QStringLiteral("Could not encode test image");
            return false;
        }
        size_t packetCount = 0;
        const size_t shard = size_t(m_PacketSize - 16);
        if (pyrowave_encoder_compute_num_packets_with_padding(m_Encoder, shard, 8, &packetCount) != PYROWAVE_SUCCESS) {
            m_Error = QStringLiteral("Could not packetize test image");
            return false;
        }
        std::vector<pyrowave_packet> packets(packetCount);
        std::vector<uint8_t> bitstream(rate.maximum_bitstream_size + 1024 * 1024);
        size_t written = 0;
        if (pyrowave_encoder_packetize_with_padding(m_Encoder, packets.data(), shard, 8, &written,
                                                    bitstream.data(), bitstream.size()) != PYROWAVE_SUCCESS) {
            m_Error = QStringLiteral("Could not packetize test image");
            return false;
        }
        packets.resize(written);
        m_Framed = frameRecords(bitstream, packets, shard,
                                PyroWaveFraming::coarseBlockCount({m_Width, m_Height, m_Chroma444}));
        if (m_Framed.empty()) {
            m_Error = QStringLiteral("Could not packetize test image");
            return false;
        }
        return true;
    }

    QString runError() const
    {
        const QString error = QString::fromStdString(m_Decoder.lastError());
        return error.isEmpty() ? QStringLiteral("Could not draw the decoded frame") : error;
    }

    // Decodes feed a queue of kQueuedFrames that is drawn in order, on a
    // second thread when kConcurrentDraw. The decoder blocks while the queue
    // is full, so the interval between two frames finishing is the second
    // one's cost to the pipeline.
    bool run(int frames, std::vector<double>& serviceMs)
    {
        using Clock = std::chrono::steady_clock;
        std::mutex lock;
        std::condition_variable wake;
        std::deque<AVFrame*> queued;
        bool decoding = true;
        std::atomic<bool> ok {true};
        std::vector<Clock::time_point> finished;
        finished.reserve(size_t(frames));

        // Only one thread draws at a time: the drawer, then this one
        auto draw = [&](AVFrame* frame) {
            if (!m_Renderer.present(frame, m_Hdr)) ok = false;
            av_frame_free(&frame);
            finished.push_back(Clock::now());
        };
        auto drawQueued = [&] {
            for (;;) {
                AVFrame* frame = nullptr;
                {
                    std::unique_lock<std::mutex> guard(lock);
                    wake.wait(guard, [&] { return !decoding || !queued.empty(); });
                    if (queued.empty()) return;
                    frame = queued.front();
                    queued.pop_front();
                }
                wake.notify_all();
                draw(frame);
            }
        };

        std::thread drawer;
        if (kConcurrentDraw) drawer = std::thread(drawQueued);
        for (int i = 0; i < frames && ok; ++i) {
            if (m_Cancelled.load()) {
                ok = false;
                break;
            }
            AVFrame* frame = av_frame_alloc();
            if (!frame || !m_Decoder.decode(m_Framed.data(), m_Framed.size(), {}, 0, frame)) {
                av_frame_free(&frame);
                ok = false;
                break;
            }
            if (kConcurrentDraw) {
                std::unique_lock<std::mutex> guard(lock);
                wake.wait(guard, [&] { return queued.size() < kQueuedFrames; });
                queued.push_back(frame);
                guard.unlock();
                wake.notify_all();
            }
            else {
                queued.push_back(frame);
                if (queued.size() > kQueuedFrames) {
                    draw(queued.front());
                    queued.pop_front();
                }
            }
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            decoding = false;
        }
        wake.notify_all();
        if (drawer.joinable()) {
            drawer.join();
        }
        while (!queued.empty()) {
            draw(queued.front());
            queued.pop_front();
        }
        serviceMs.clear();
        for (size_t i = 1; i < finished.size(); ++i) {
            serviceMs.push_back(std::chrono::duration<double, std::milli>(finished[i] - finished[i - 1]).count());
        }
        return ok && !serviceMs.empty();
    }

    Renderer& m_Renderer;
    int m_Width;
    int m_Height;
    int m_Fps;
    bool m_Chroma444;
    bool m_Hdr;
    int m_PacketSize;
    const std::atomic<bool>& m_Cancelled;
    Planes m_Source;
    pyrowave_encoder m_Encoder = nullptr;
    PyroWaveDecoder m_Decoder;
    std::vector<uint8_t> m_Framed;
    QString m_Error;
};

// Test the selected target directly, lowering it only when fresh measurements
// establish a useful reduction in device cost.
// StreamLight: onMeasure is called before every timed measurement, for the progress line.
Sample calibrateFormat(pyrowave_device device, Renderer& renderer, int width, int height, int fps,
                       bool chroma444, bool hdr, int linkCapKbps, int linkSpeedKbps,
                       PyroWaveCalibration::Target target,
                       const pyrowave::bandwidth::transport_t& transport,
                       const std::atomic<bool>& cancelled,
                       const std::function<void(int)>& onMeasure)
{
    Sample sample;
    sample.width = width;
    sample.height = height;
    sample.chroma444 = chroma444;
    sample.hdr = hdr;
    sample.guideKbps = pyroWaveRecommendedKbps(width, height, fps, chroma444, hdr);
    const int wireCapKbps = PyroWaveCalibration::wireTarget(target, linkCapKbps, linkSpeedKbps,
        PyroWaveCalibration::roundUp(pyrowave::bandwidth::total_kbps(sample.guideKbps, fps, transport)));
    const int imageCapKbps = PyroWaveCalibration::imageCapacity(wireCapKbps, fps, transport);
    if (imageCapKbps < 5000) {
        sample.error = QStringLiteral("Not enough stable bandwidth after FEC and packet overhead");
        return sample;
    }
    sample.targetKbps = PyroWaveCalibration::imageTarget(target, sample.guideKbps,
                                                        PyroWaveLink::maximumKbps);
    const int topKbps = PyroWaveCalibration::imageTarget(target, sample.guideKbps, imageCapKbps);
    if (target > PyroWaveCalibration::Recommended) sample.targetKbps = topKbps;
    sample.linkLimited = topKbps < (target <= PyroWaveCalibration::Recommended ?
                                   sample.targetKbps : sample.guideKbps);
    const auto wireKbps = [&](int imageKbps) {
        return PyroWaveCalibration::roundUp(pyrowave::bandwidth::total_kbps(imageKbps, fps, transport));
    };
    const auto quality = [&](int kbps) {
        return pyroWaveQualityDb(width, height, fps, chroma444, hdr, kbps);
    };
    const double availableMs = 1000.0 / fps * kPeriodShare;
    const auto keepsUp = [&](const Probe& probe) {
        return probe.ok && !probe.overloaded && probe.frameMs <= availableMs;
    };

    FormatTester tester(device, renderer, width, height, fps, chroma444, hdr, transport.packetsize, cancelled);
    if (!tester.error().isEmpty()) {
        sample.error = tester.error();
        return sample;
    }
    // A stall elsewhere on the system can land in any timed run, so a miss by
    // a format whose throughput keeps up is measured once more and the better
    // run kept.
    const auto measure = [&](int kbps) {
        onMeasure(kbps);
        Probe probe = tester.probe(kbps);
        if (probe.ok && !probe.overloaded && !keepsUp(probe) && !cancelled.load()) {
            const Probe again = tester.probe(kbps);
            if (again.ok && (keepsUp(again) || again.frameMs < probe.frameMs)) probe = again;
        }
        return probe;
    };
    const int floorKbps = roundDownKbps(pyroWaveKbpsForQuality(width, height, fps, chroma444, hdr,
                                                             kFloorQualityDb));
    const auto selected = PyroWaveCalibration::searchDevice(topKbps, sample.guideKbps, floorKbps,
        measure, [&](int kbps) { return tester.probe(kbps, true); }, keepsUp,
        [&] { return cancelled.load(); });
    if (!selected.cost.ok) {
        sample.error = selected.cost.error;
        return sample;
    }
    sample.valid = true;
    sample.keepsUp = keepsUp(selected.cost);
    sample.deviceLimited = selected.deviceLimited;
    sample.share = selected.cost.frameMs / availableMs;
    sample.imageKbps = selected.imageKbps;
    sample.bitrateKbps = wireKbps(selected.imageKbps);
    sample.qualityDb = quality(selected.imageKbps);
    sample.frameMs = selected.cost.frameMs;
    sample.load = selected.cost.load;
    return sample;
}

// ═══ End of Nonary's code ══════════════════════════════════════════════════════════════════

#endif // PYROWAVE_TEST_AVAILABLE

} // namespace

PyroWaveTest* PyroWaveTest::get(QQmlEngine*)
{
    static PyroWaveTest* instance = new PyroWaveTest();
    return instance;
}

PyroWaveTest::PyroWaveTest(QObject* parent) : QObject(parent) {}

PyroWaveTest::~PyroWaveTest()
{
    if (m_Worker) {
        if (m_Cancel) m_Cancel->store(true);
        m_Worker->wait();
    }
}

QVariantMap PyroWaveTest::settingsFor(const QString& hostUuid) const
{
    const Format f = effectiveFormat(hostUuid);
    QString where = tr("Settings");
    const int slot = HostProfileManager::get()->active(hostUuid);
    if (slot >= 0 && HostProfileManager::get()->settings(hostUuid, slot).hasBitrate) {
        where = tr("profile “%1”").arg(HostProfileManager::get()->name(hostUuid, slot));
    }
    return {{QStringLiteral("format"), formatText(f)},
            {QStringLiteral("pyroWave"), f.pyroWave},
            {QStringLiteral("bitrateKbps"), f.bitrateKbps},
            {QStringLiteral("bitrateWhere"), where}};
}

QVariantMap PyroWaveTest::savedResult(const QString& hostUuid) const
{
    QSettings s;
    s.beginGroup(QLatin1String(k_Group));
    s.beginGroup(hostUuid);
    if (!s.contains(QStringLiteral("bitrateKbps"))) return {};
    QVariantMap m;
    for (const QString& key : s.childKeys()) m.insert(key, s.value(key));
    // "14/10" for the tile and the popup, from the stored ISO date
    const QDateTime when = QDateTime::fromString(m.value(QStringLiteral("testedAt")).toString(), Qt::ISODate);
    m.insert(QStringLiteral("testedDay"), when.isValid() ? when.toLocalTime().toString(QStringLiteral("dd/MM")) : QString());
    return m;
}

int PyroWaveTest::paceMbpsFor(const QString& hostUuid, int clientLinkMbps)
{
    QSettings s;
    s.beginGroup(QLatin1String(k_Group));
    s.beginGroup(hostUuid);
    const int pace = s.value(QStringLiteral("paceMbps"), 0).toInt();
    const int link = s.value(QStringLiteral("clientLinkMbps"), 0).toInt();
    // Measured on another wired link (a dock, a different port, Wi-Fi now): not this path's.
    return pace > 0 && link > 0 && link == clientLinkMbps ? pace : 0;
}

void PyroWaveTest::show(const QString& hostUuid)
{
    if (m_Worker) return;
    m_HostUuid = hostUuid;
    m_Result = savedResult(hostUuid);
    m_Phase = m_Result.isEmpty() ? 0 : 3;
    m_Progress = 0;
    m_Status.clear();
    m_Error.clear();
    emit changed();
}

void PyroWaveTest::setStatus(const QString& status, double progress)
{
    m_Status = status;
    m_Progress = std::clamp(progress, 0.0, 0.97);
    emit changed();
}

void PyroWaveTest::fail(const QString& error)
{
    m_Phase = 4;
    m_Error = error;
    m_Status.clear();
    emit changed();
}

void PyroWaveTest::finish(const QVariantMap& result)
{
    QSettings s;
    s.beginGroup(QLatin1String(k_Group));
    s.remove(m_HostUuid);
    s.beginGroup(m_HostUuid);
    for (auto it = result.constBegin(); it != result.constEnd(); ++it) s.setValue(it.key(), it.value());
    s.endGroup();
    s.endGroup();
    s.sync();
    m_Result = savedResult(m_HostUuid);
    m_Phase = 3;
    m_Progress = 1;
    m_Status.clear();
    emit changed();
}

void PyroWaveTest::cancel()
{
    if (m_Cancel) m_Cancel->store(true);
}

QString PyroWaveTest::applyBitrate(const QString& hostUuid, int kbps)
{
    kbps = std::clamp(kbps, 500, StreamingPreferences::getMaxBitrate(StreamingPreferences::VCC_FORCE_PYROWAVE));
    HostProfileManager* profiles = HostProfileManager::get();
    const int slot = profiles->active(hostUuid);
    if (slot >= 0) {
        AppOverride ov = profiles->settings(hostUuid, slot);
        if (ov.hasBitrate) {
            ov.bitrateKbps = kbps;
            profiles->setSettings(hostUuid, slot, ov);
            return tr("in profile “%1”").arg(profiles->name(hostUuid, slot));
        }
    }
    StreamingPreferences* prefs = StreamingPreferences::get();
    prefs->bitrateKbps = kbps;
    prefs->save();
    emit prefs->bitrateChanged();
    return tr("in Settings");
}

void PyroWaveTest::start(ComputerManager* manager, const QString& hostUuid, int target)
{
    if (m_Worker) return;
    m_HostUuid = hostUuid;
    m_Result.clear();
    m_Error.clear();
    m_Progress = 0;

    if (Session::get() != nullptr) {
        fail(tr("Finish the stream first."));
        return;
    }
    NvComputer* host = nullptr;
    for (NvComputer* computer : (manager ? manager->getComputers() : QVector<NvComputer*>())) {
        QReadLocker lock(&computer->lock);
        if (computer->uuid == hostUuid) {
            host = computer;
            break;
        }
    }
    if (!host) {
        fail(tr("The host is gone."));
        return;
    }
    NvAddress address;
    uint16_t httpsPort = 0;
    QSslCertificate certificate;
    QString hostName;
    bool useTrueUid = false;
    {
        QReadLocker lock(&host->lock);
        if (host->state != NvComputer::CS_ONLINE || host->pairState != NvComputer::PS_PAIRED) {
            fail(tr("%1 is not online and paired.").arg(host->name));
            return;
        }
        if (!(host->serverCodecModeSupport & SCM_PYROWAVE)) {
            fail(tr("%1 doesn't offer PyroWave.").arg(host->name));
            return;
        }
        address = host->activeAddress;
        httpsPort = host->activeHttpsPort;
        certificate = host->serverCert;
        hostName = host->name;
        useTrueUid = !host->isNvidiaServerSoftware;
    }
    // The pace is a property of the wired path; over Tailscale or a VPN there is none to keep.
    if (host->getActiveAddressReachability() != NvComputer::RI_LAN) {
        fail(tr("The test runs on the local network only."));
        return;
    }

    const Format format = effectiveFormat(hostUuid);
    int packetSize = format.packetSize ? format.packetSize : 1392;
    if (packetSize < 256 || packetSize > 1392 || packetSize % 4 != 0) {
        fail(tr("The packet size must be between 256 and 1392 bytes, in steps of 4."));
        return;
    }
    if (format.fps < 10 || format.fps > 240 || format.width <= 0 || format.height <= 0) {
        fail(tr("Choose a resolution and a frame rate first."));
        return;
    }
    const int clientLinkMbps = NetworkBuffers::routedWiredLinkMbps(QHostAddress(address.address()));

#ifndef PYROWAVE_TEST_AVAILABLE
    Q_UNUSED(target);
    Q_UNUSED(useTrueUid);
    Q_UNUSED(certificate);
    Q_UNUSED(httpsPort);
    Q_UNUSED(clientLinkMbps);
    fail(tr("This device can't decode PyroWave."));
#else
    const auto chosen = static_cast<PyroWaveCalibration::Target>(std::clamp(target, int(Minimum), int(Maximum)));
    m_Phase = 1;
    setStatus(tr("Asking %1").arg(hostName), 0);

    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    m_Cancel = cancelled;
    QPointer<PyroWaveTest> self(this);
    // Thread-safe status line: queued onto the GUI thread, dropped once cancelled.
    const auto report = [self, cancelled](int phase, const QString& text, double progress) {
        QMetaObject::invokeMethod(self.data(), [self, cancelled, phase, text, progress] {
            if (!self || cancelled->load()) return;
            self->m_Phase = phase;
            self->setStatus(text, progress);
        }, Qt::QueuedConnection);
    };

    m_Worker = QThread::create([self, report, cancelled, chosen, format, packetSize, clientLinkMbps,
                                address, httpsPort, certificate, useTrueUid, hostName] {
        QString error;
        QVariantMap out;
        try {
            // ── Step 1: the network ─────────────────────────────────────────────────────
            pyrowave::bandwidth::transport_t transport;
            transport.packetsize = packetSize;
            NvHTTP http(address, httpsPort, certificate, useTrueUid);
            const QString serverInfo = http.getServerInfo(NvHTTP::NVLL_ERROR);
            if (NvHTTP::getXmlString(serverInfo, "PyroWaveUdpProbeVersion") != "1" ||
                NvHTTP::getXmlString(serverInfo, "PyroWaveWireBudgetVersion") != "1" ||
                NvHTTP::getXmlString(serverInfo, "PyroWaveUdpHandshakeVersion") != "1" ||
                NvHTTP::getXmlString(serverInfo, "PyroWaveUdpProbeBurstVersion") != "1") {
                throw std::runtime_error(QObject::tr("%1 needs Vibeshine or Vibepollo 2.0.1 or later.")
                                         .arg(hostName).toStdString());
            }
            bool validFec = false, validParity = false;
            const int fec = NvHTTP::getXmlString(serverInfo, "PyroWaveCriticalFecPercentage").toInt(&validFec);
            const int parity = NvHTTP::getXmlString(serverInfo, "PyroWaveMinParityShards").toInt(&validParity);
            if (!validFec || fec < 0 || fec > 100 || !validParity || parity < 0 || parity > 255) {
                throw std::runtime_error("The host sent an invalid FEC budget.");
            }
            transport.critical_fec_percentage = fec;
            transport.min_parity_shards = parity;
            const int hostLinkMbps = NvHTTP::getXmlString(serverInfo, "PyroWaveHostLinkMbps").toInt();

            // From Nonary: the capacity search, capped by both known wired links.
            int capKbps = PyroWaveLink::maximumKbps;
            if (hostLinkMbps > 0) capKbps = (std::min)(capKbps, int((std::min)(hostLinkMbps, 3000) * 1000));
            if (clientLinkMbps > 0) capKbps = (std::min)(capKbps, int((std::min)(clientLinkMbps, 3000) * 1000));
            int probes = 0;
            const auto probeCapacity = [&](int kbps) {
                report(1, QObject::tr("Sending %1 Mbps").arg(kbps / 1000), 0.6 * (std::min)(1.0, ++probes / 10.0));
                const auto result = http.probePyroWaveUdp(kbps, packetSize, *cancelled, true);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "PyroWave Test: capacity %d kbps: sent %u/%u, received %u, loss %.3f%%, worst 100ms %.3f%%, "
                    "delay p99 %.2f ms, growth %.2f ms: capacity %s (%s)",
                    kbps, result.sent, result.expected, result.received, result.lossPercent,
                    result.worstWindowLossPercent, result.delayP99Ms, result.delayGrowthMs,
                    result.capacityQualified() ? "pass" : "fail", result.failureReason());
                return result;
            };
            const PyroWaveLink::Result link = PyroWaveLink::search(capKbps, capKbps, probeCapacity,
                                                                  [&] { return cancelled->load(); }, false);
            if (cancelled->load()) throw std::runtime_error("cancelled");
            if (!link.capacityQualified()) {
                throw std::runtime_error(QObject::tr("The network lost too many packets even at %1 Mbps.")
                                         .arg(link.requestedKbps / 1000).toStdString());
            }

            // From Nonary: the pace, with frames as large as the chosen target would send.
            int paceLinkMbps = 0;
            for (int known : {hostLinkMbps, clientLinkMbps}) {
                if (known > 0) paceLinkMbps = paceLinkMbps ? (std::min)(paceLinkMbps, known) : known;
            }
            paceLinkMbps = (std::min)(paceLinkMbps, 9999);
            const int linkSpeedKbps = paceLinkMbps * 1000;
            const int guideKbps = pyroWaveRecommendedKbps(format.width, format.height, format.fps,
                                                          format.chroma444, format.hdr);
            int budgetKbps = link.requestedKbps;
            {
                const int wireCap = PyroWaveCalibration::wireTarget(chosen, link.requestedKbps, linkSpeedKbps,
                    PyroWaveCalibration::roundUp(pyrowave::bandwidth::total_kbps(guideKbps, format.fps, transport)));
                const int image = PyroWaveCalibration::imageTarget(chosen, guideKbps,
                    PyroWaveCalibration::imageCapacity(wireCap, format.fps, transport));
                const int wire = PyroWaveCalibration::roundUp(pyrowave::bandwidth::total_kbps(image, format.fps, transport));
                if (wire >= PyroWaveLink::minimumKbps) budgetKbps = (std::min)(budgetKbps, wire);
            }
            PyroWaveLink::PaceResult pace;
            int frameCapKbps = link.requestedKbps;
            // Without a known wired link speed on either side there is no pace to search for;
            // the host keeps its own default, and the budget stands on its own.
            if (paceLinkMbps > 0) {
                const int burstFps = std::clamp(format.fps, 10, 500);
                int paceProbes = 0;
                pace = PyroWaveLink::searchPace(paceLinkMbps * 1000, budgetKbps / 4 * 5,
                    [&](int paceKbps) {
                    report(1, QObject::tr("Sending %1 Mbps as %2 FPS frames").arg(budgetKbps / 1000).arg(burstFps),
                           0.6 + 0.4 * (std::min)(1.0, ++paceProbes / 6.0));
                    const auto result = http.probePyroWaveUdp(budgetKbps, packetSize, *cancelled, true,
                                                              burstFps, (std::max)(paceKbps, budgetKbps));
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "PyroWave Test: pace %d kbps, %d kbps as %d FPS frames: sent %u/%u, received %u, loss %.3f%%, "
                        "frames losing more than 10%% %.3f%%, missing tails %.3f%%: %s",
                        paceKbps, budgetKbps, burstFps, result.sent, result.expected, result.received,
                        result.lossPercent, result.excessiveLossFramePercent(), result.missingTailFramePercent(),
                        PyroWaveLink::paceQualified(result) ? "pass" : "fail");
                    return result;
                }, [&] { return cancelled->load(); });
                if (cancelled->load()) throw std::runtime_error("cancelled");
                frameCapKbps = budgetKbps;
            }
            const int linkCapKbps = (std::min)(link.requestedKbps, frameCapKbps);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "PyroWave Test: %s, budget %d kbps (frame-tested %d), pace %d kbps%s, links host %d / client %d Mbps",
                        qPrintable(hostName), link.requestedKbps, linkCapKbps, pace.paceKbps,
                        pace.qualified ? "" : " (best available)", hostLinkMbps, clientLinkMbps);

            // ── Step 2: this device's decoder ───────────────────────────────────────────
            report(2, QObject::tr("Decoding %1×%2").arg(format.width).arg(format.height), 0);
            pyrowave_device device = nullptr;
            if (pyrowave_create_default_device(&device) != PYROWAVE_SUCCESS) {
                throw std::runtime_error(QObject::tr("This device has no Vulkan device for PyroWave.").toStdString());
            }
            Sample sample;
            {
                Renderer renderer;
                if (!renderer.create()) {
                    pyrowave_device_destroy(device);
                    throw std::runtime_error(QObject::tr("This device's GPU can't decode PyroWave.").toStdString());
                }
                int measures = 0;
                sample = calibrateFormat(device, renderer, format.width, format.height, format.fps,
                                         format.chroma444, format.hdr, linkCapKbps, linkSpeedKbps, chosen,
                                         transport, *cancelled, [&](int kbps) {
                    report(2, QObject::tr("Decoding %1×%2 at %3 Mbps").arg(format.width).arg(format.height)
                                  .arg(PyroWaveCalibration::roundUp(pyrowave::bandwidth::total_kbps(kbps, format.fps, transport)) / 1000),
                           (std::min)(1.0, ++measures / 6.0));
                });
            }
            pyrowave_device_destroy(device);
            if (cancelled->load()) throw std::runtime_error("cancelled");
            if (!sample.valid) {
                throw std::runtime_error((sample.error.isEmpty() ? QObject::tr("The decoder test failed.") : sample.error).toStdString());
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "PyroWave Test: %s %s: %d wire kbps (%d image, guide %d), %.1f dB, GPU p99 %.2f ms/frame (%.0f%%)%s%s%s",
                        qPrintable(formatText(format)), qPrintable(targetName(chosen)), sample.bitrateKbps,
                        sample.imageKbps, sample.guideKbps, sample.qualityDb, sample.frameMs, sample.load * 100,
                        sample.keepsUp ? "" : ", falls behind", sample.linkLimited ? ", network-limited" : "",
                        sample.deviceLimited ? ", device-limited" : "");

            out = {{QStringLiteral("target"), int(chosen)},
                   {QStringLiteral("targetName"), targetName(chosen)},
                   {QStringLiteral("format"), formatText(format)},
                   {QStringLiteral("bitrateKbps"), sample.bitrateKbps},
                   {QStringLiteral("qualityDb"), qRound(sample.qualityDb * 10) / 10.0},
                   {QStringLiteral("quality"), sample.qualityDb >= kFullQualityDb - 0.01 ? QStringLiteral("full")
                                               : sample.qualityDb >= kReducedQualityDb ? QStringLiteral("reduced")
                                               : QStringLiteral("low")},
                   {QStringLiteral("keepsUp"), sample.keepsUp},
                   {QStringLiteral("linkLimited"), sample.linkLimited},
                   {QStringLiteral("deviceLimited"), sample.deviceLimited},
                   {QStringLiteral("frameMs"), qRound(sample.frameMs * 100) / 100.0},
                   {QStringLiteral("budgetMbps"), link.requestedKbps / 1000},
                   {QStringLiteral("paceMbps"), pace.paceKbps / 1000},
                   {QStringLiteral("paceQualified"), pace.qualified},
                   {QStringLiteral("hostLinkMbps"), hostLinkMbps},
                   {QStringLiteral("clientLinkMbps"), clientLinkMbps},
                   {QStringLiteral("testedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
        }
        catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(self.data(), [self, cancelled, error, out] {
            if (!self) return;
            if (cancelled->load()) {
                self->m_Phase = 0;
                self->m_Progress = 0;
                self->m_Status.clear();
                emit self->changed();
            }
            else if (!error.isEmpty()) self->fail(error);
            else self->finish(out);
        }, Qt::QueuedConnection);
    });
    connect(m_Worker, &QThread::finished, this, [this] {
        if (m_Worker) m_Worker->deleteLater();
        m_Worker = nullptr;
        m_Cancel.reset();
        emit changed();
    });
    m_Worker->start();
#endif
}
