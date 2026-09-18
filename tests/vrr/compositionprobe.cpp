#include "../../app/streaming/video/ffmpeg-renderers/d3d11composition.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
uint64_t clockUs()
{
    static const auto frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    static const auto epoch = [] { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return 1 + uint64_t(now.QuadPart - epoch) * 1000000 / frequency;
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CLOSE || (message == WM_KEYDOWN && wparam == VK_ESCAPE)) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

uint64_t percentile(std::vector<uint64_t> values, size_t percent)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[(values.size() - 1) * percent / 100];
}
}

// This is a manual fullscreen hardware test, deliberately not a tst_* target.
// --help is noninteractive; --run is required to open a window.
int wmain(int argc, wchar_t** argv)
{
    bool run = false, check = false;
    unsigned fps = 116, seconds = 10;
    const wchar_t* output = L"composition-probe.json";
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--run")) run = true;
        else if (!wcscmp(argv[i], L"--check")) check = true;
        else if (!wcscmp(argv[i], L"--fps") && i + 1 < argc) fps = wcstoul(argv[++i], nullptr, 10);
        else if (!wcscmp(argv[i], L"--seconds") && i + 1 < argc) seconds = wcstoul(argv[++i], nullptr, 10);
        else if (!wcscmp(argv[i], L"--output") && i + 1 < argc) output = argv[++i];
        else if (wcscmp(argv[i], L"--help")) { std::fprintf(stderr, "Unknown or incomplete option\n"); return 1; }
    }
    if (!run && !check) {
        std::puts("compositionprobe --run [--fps 116] [--seconds 10] [--output result.json]\n"
                  "Manual Windows 11 fullscreen presentation test. Escape ends the test.\n"
                  "Reports independent-flip timing and latency; does not prove optical VRR or tear freedom.");
        std::puts("compositionprobe --check: verify runtime, driver, and buffer setup without showing a window.");
        return 0;
    }
    if (fps < 30 || fps > 360 || seconds < 3 || seconds > 60) return 1;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.hInstance = instance;
    wc.lpfnWndProc = windowProcedure;
    wc.lpszClassName = L"MoonlightCompositionProbe";
    if (!RegisterClassW(&wc)) return 1;
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) return 1;
    const UINT width = monitor.rcMonitor.right - monitor.rcMonitor.left;
    const UINT height = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"Moonlight presentation test (Escape to close)",
        WS_POPUP, monitor.rcMonitor.left, monitor.rcMonitor.top, width, height,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    if (run) ShowWindow(window, SW_SHOW);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    ComPtr<ID3D11Device5> device5;
    ComPtr<ID3D11DeviceContext4> context4;
    ComPtr<ID3D11Fence> fence;
    if (SUCCEEDED(hr)) hr = device.As(&device5);
    if (SUCCEEDED(hr)) hr = context.As(&context4);
    if (SUCCEEDED(hr)) hr = device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) { std::fprintf(stderr, "D3D11 setup failed: %08lx\n", static_cast<unsigned long>(hr)); return 1; }

    if (check && !run) {
        std::printf("Runtime supported: %d; device supports independent flip: %d\n",
                    D3D11CompositionPresenter::runtimeSupported(),
                    D3D11CompositionPresenter::deviceSupported(device.Get()));
        bool passed = true;
        for (const auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM}) {
            D3D11CompositionPresenter presenter;
            hr = presenter.initialize(device.Get(), window, width, height, format, {}, 0);
            std::printf("Presentation setup, format %u: %08lx\n", unsigned(format),
                        static_cast<unsigned long>(hr));
            passed = passed && SUCCEEDED(hr);
        }
        DestroyWindow(window);
        return passed ? 0 : 1;
    }

    // Resolve the active path for the primary monitor, not an assumed source 0.
    UINT pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return 1;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return 1;
    const DISPLAYCONFIG_PATH_INFO* selected = nullptr;
    for (UINT i = 0; i < pathCount; ++i) {
        const auto index = paths[i].sourceInfo.modeInfoIdx;
        if (index < modeCount && modes[index].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE &&
                modes[index].sourceMode.position.x == monitor.rcMonitor.left &&
                modes[index].sourceMode.position.y == monitor.rcMonitor.top) {
            if (selected) { std::fprintf(stderr, "Cloned/ambiguous outputs are not supported by this probe\n"); return 1; }
            selected = &paths[i];
        }
    }
    if (!selected || !selected->targetInfo.refreshRate.Numerator) return 1;
    const uint64_t refreshUs = uint64_t(selected->targetInfo.refreshRate.Denominator) * 1000000 /
        selected->targetInfo.refreshRate.Numerator;
    if (!refreshUs || 1000000 / fps < refreshUs) return 1;
    D3D11CompositionPresenter presenter;
    hr = presenter.initialize(device.Get(), window, width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
        selected->sourceInfo.adapterId, selected->sourceInfo.id);
    if (FAILED(hr)) { std::fprintf(stderr, "Composition setup failed: %08lx\n", static_cast<unsigned long>(hr)); return 1; }

    HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!ready || !timer) return 1;
    struct Submission { uint64_t id = 0, at = 0; };
    std::array<Submission, 128> pending = {};
    std::vector<uint64_t> latencies, intervals;
    uint64_t lastDisplayed = 0, lastDisplayedId = 0, submitted = 0, measuredSubmissions = 0, unavailable = 0;
    const uint64_t start = clockUs();
    uint64_t number = 0;
    bool stopped = false;
    while (!stopped && clockUs() - start < uint64_t(seconds) * 1000000) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) stopped = true;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (stopped) break;
        const uint64_t target = start + ++number * 1000000 / fps;
        ComPtr<ID3D11RenderTargetView> view;
        hr = presenter.acquire(&view);
        if (hr == S_FALSE) ++unavailable;
        else if (FAILED(hr)) break;
        else {
            const float background[4] = {0.08f, 0.08f, 0.08f, 1.0f};
            const float bar[4] = {0.7f, 0.7f, 0.7f, 1.0f};
            context4->ClearRenderTargetView(view.Get(), background);
            const LONG x = static_cast<LONG>(number * 8 % width);
            const D3D11_RECT rect = {x, 0, std::min<LONG>(x + 80, width), static_cast<LONG>(height)};
            context4->ClearView(view.Get(), bar, &rect, 1);
            hr = context4->Signal(fence.Get(), number);
            context4->Flush();
            if (SUCCEEDED(hr)) hr = fence->SetEventOnCompletion(number, ready);
            if (FAILED(hr) || WaitForSingleObject(ready, 50) != WAIT_OBJECT_0) { hr = E_FAIL; break; }
        }
        const auto now = clockUs();
        if (target > now) {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>((target - now) * 10);
            if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) ||
                    WaitForSingleObject(timer, 100) != WAIT_OBJECT_0) { hr = E_FAIL; break; }
        }
        if (view) {
            const auto at = clockUs();
            uint64_t id;
            hr = presenter.present(id);
            if (FAILED(hr)) break;
            pending[id % pending.size()] = {id, at};
            ++submitted;
            if (at >= start + 1000000) ++measuredSubmissions;
        }
        D3D11CompositionPresenter::DisplayedFrame frame;
        while (presenter.pollDisplayedFrame(clockUs, frame)) {
            auto& p = pending[frame.id % pending.size()];
            if (p.id == frame.id && p.at >= start + 1000000 && frame.clock.timeUs >= p.at) {
                latencies.push_back(frame.clock.timeUs - p.at);
                if (lastDisplayed && frame.id == lastDisplayedId + 1 && frame.clock.timeUs > lastDisplayed)
                    intervals.push_back(frame.clock.timeUs - lastDisplayed);
                lastDisplayed = frame.clock.timeUs;
                lastDisplayedId = frame.id;
            }
        }
    }
    const bool coverage = measuredSubmissions && latencies.size() * 100 >= measuredSubmissions * 90;
    const bool belowOneRefresh = coverage && percentile(latencies, 99) < refreshUs;
    FILE* report = _wfopen(output, L"w");
    if (report) {
        std::fprintf(report,
            "{\n  \"submitted\": %llu, \"steady_submitted\": %llu, \"measured\": %llu,\n"
            "  \"independent_flip_frames\": %llu, \"composed_frames\": %llu, \"unavailable_buffers\": %llu,\n"
            "  \"rejected_display_timestamps\": %llu,\n"
            "  \"display_period_us\": %llu, \"submit_to_display_p50_us\": %llu, \"submit_to_display_p95_us\": %llu, \"submit_to_display_p99_us\": %llu,\n"
            "  \"display_interval_p50_us\": %llu, \"display_interval_p99_us\": %llu,\n"
            "  \"coverage_at_least_90_percent\": %s, \"p99_service_below_one_refresh\": %s,\n"
            "  \"optical_vrr_verified\": false, \"run_completed\": %s\n}\n",
            static_cast<unsigned long long>(submitted), static_cast<unsigned long long>(measuredSubmissions),
            static_cast<unsigned long long>(latencies.size()), static_cast<unsigned long long>(presenter.independentFrames()),
            static_cast<unsigned long long>(presenter.composedFrames()), static_cast<unsigned long long>(unavailable),
            static_cast<unsigned long long>(presenter.rejectedDisplayFrames()),
            static_cast<unsigned long long>(refreshUs), static_cast<unsigned long long>(percentile(latencies, 50)),
            static_cast<unsigned long long>(percentile(latencies, 95)), static_cast<unsigned long long>(percentile(latencies, 99)),
            static_cast<unsigned long long>(percentile(intervals, 50)), static_cast<unsigned long long>(percentile(intervals, 99)),
            coverage ? "true" : "false", belowOneRefresh ? "true" : "false",
            SUCCEEDED(hr) && !stopped ? "true" : "false");
        std::fclose(report);
    }
    presenter.reset();
    CloseHandle(ready);
    CloseHandle(timer);
    DestroyWindow(window);
    return report && SUCCEEDED(hr) && !stopped && belowOneRefresh ? 0 : 2;
}
