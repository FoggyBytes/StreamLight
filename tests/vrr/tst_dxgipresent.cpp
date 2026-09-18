#include "../../app/streaming/video/ffmpeg-renderers/dxgipresent.h"
#include "../../app/streaming/video/ffmpeg-renderers/d3d11fencewait.h"

#include <cstdio>

namespace {
struct FakeSwapChain
{
    unsigned int interval = 99;
    unsigned int flags = 99;
    unsigned int calls = 0;
    long result = 0;

    long Present(unsigned int newInterval, unsigned int newFlags)
    {
        interval = newInterval;
        flags = newFlags;
        ++calls;
        return result;
    }
};
}

int main()
{
    constexpr unsigned int allowTearing = 0x200;
    FakeSwapChain swapChain;
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };
    for (const auto readyAt : {0ULL, 3000ULL, 50000ULL, 100000ULL}) {
        uint64_t now = 0;
        const auto result = D3D11FenceWait::wait(7, [&] { return now; },
            [&] { return now >= readyAt ? 7ULL : 6ULL; },
            [&](unsigned timeoutMs) { now += timeoutMs * 1000; return true; });
        check(result.status == (readyAt <= 50000 ? D3D11FenceWait::Status::Complete :
            D3D11FenceWait::Status::Timeout),
            "missing event notifications must not hide completed GPU work or admit incomplete work");
        check(now == std::min<uint64_t>(readyAt, 50000),
            "completion polling must recover promptly and retain the 50 ms total timeout");
        check(result.elapsedUs == now && result.waitCalls == now / 1000 &&
              result.stopReason == (readyAt <= 50000 ? D3D11FenceWait::StopReason::Completed :
                  D3D11FenceWait::StopReason::Deadline),
              "wait diagnostics must distinguish elapsed deadline from successful completion");
    }
    {
        uint64_t now = 0;
        unsigned calls = 0;
        const auto result = D3D11FenceWait::wait(7, [&] { return now; },
            [&] { return now >= 3000 ? 7ULL : 6ULL; },
            [&](unsigned timeoutMs) { if (calls++) now += timeoutMs * 1000; return true; });
        check(result.status == D3D11FenceWait::Status::Complete && now == 3000 && calls == 4,
              "a stale signalled event must not release a still-incomplete frame");
    }
    check(D3D11FenceWait::wait(7, [] { return 0ULL; }, [] { return 6ULL; },
          [](unsigned) { return false; }).status == D3D11FenceWait::Status::WaitFailed,
          "native wait failures must propagate");
    check(D3D11FenceWait::wait(7, [] { return 0ULL; },
          [] { return std::numeric_limits<uint64_t>::max(); },
          [](unsigned) { return true; }).status == D3D11FenceWait::Status::DeviceRemoved,
          "the device-removed fence sentinel must never count as GPU completion");
    check(D3D11FenceWait::wait(7, [] { return 0ULL; }, [] { return 6ULL; },
          [](unsigned) { return true; }).status == D3D11FenceWait::Status::Timeout,
          "stale events and a stalled clock must not loop forever");
    {
        const auto result = D3D11FenceWait::wait(7, [] { return 0ULL; }, [] { return 6ULL; },
            [](unsigned) { return true; });
        check(result.stopReason == D3D11FenceWait::StopReason::IterationLimit &&
              result.elapsedUs == 0 && result.waitCalls == 100,
              "an iteration guard must not be diagnosed as a 50 ms GPU stall");
    }
    {
        uint64_t now = 1000;
        const auto result = D3D11FenceWait::wait(7, [&] { return now; }, [] { return 6ULL; },
            [&](unsigned) { now = 999; return true; });
        check(result.status == D3D11FenceWait::Status::Timeout &&
              result.stopReason == D3D11FenceWait::StopReason::ClockReversed &&
              result.elapsedUs == 0 && result.waitCalls == 1,
              "a reversed clock must report its cause without unsigned elapsed-time underflow");
    }
    {
        uint64_t now = 0;
        const auto result = D3D11FenceWait::wait(7, [&] { return now; }, [] { return 6ULL; },
            [&](unsigned) { now += 100; return false; });
        check(result.status == D3D11FenceWait::Status::WaitFailed &&
              result.stopReason == D3D11FenceWait::StopReason::NativeWaitFailed &&
              result.elapsedUs == 100 && result.waitCalls == 1,
              "native wait failure diagnostics must count the failing call and its elapsed time");
    }
    const auto submit = [&](DxgiPresentParameters parameters,
                            unsigned int interval, unsigned int flags) {
        const auto previousCalls = swapChain.calls;
        check(parameters.present(swapChain) == swapChain.result,
              "native Present result must propagate unchanged");
        check(swapChain.calls == previousCalls + 1,
              "one frame must issue exactly one native Present");
        check(swapChain.interval == interval && swapChain.flags == flags,
              "native Present received the wrong interval or flags");
        check(swapChain.interval == parameters.syncInterval &&
                  swapChain.flags == parameters.flags,
              "native arguments must match the parameters reported in telemetry");
    };

    // Switch both ways: the latched interval must not be silently replaced by
    // zero, and tearing must never carry over into a synchronized submission.
    submit(DxgiPresentParameters::adaptive(true, allowTearing), 1, 0);
    submit(DxgiPresentParameters::adaptive(false, allowTearing), 0, allowTearing);
    submit(DxgiPresentParameters::adaptive(true, allowTearing), 1, 0);

    // Preserve the legacy software-paced caller's explicit interval-zero path.
    submit({0, 0}, 0, 0);
    submit({0, allowTearing}, 0, allowTearing);

    // Both errors and non-display success statuses belong to the caller.
    swapChain.result = -1;
    submit(DxgiPresentParameters::adaptive(true, allowTearing), 1, 0);
    swapChain.result = 1;
    submit(DxgiPresentParameters::adaptive(false, allowTearing), 0, allowTearing);
    return failures == 0 ? 0 : 1;
}
