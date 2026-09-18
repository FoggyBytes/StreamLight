// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

// Event notifications wake the CPU; only the fence value proves that a decoder
// surface/back buffer is safe to reuse. Keep the original 50 ms total budget,
// but check completion between short blocking waits so a delayed notification
// cannot manufacture a 50 ms stall followed by a decoder reset.
namespace D3D11FenceWait {
enum class Status { Complete, Timeout, WaitFailed, DeviceRemoved };
enum class StopReason { Completed, Deadline, IterationLimit, ClockReversed, NativeWaitFailed, DeviceRemoved };
inline const char* stopReasonName(StopReason reason)
{
    switch (reason) {
    case StopReason::Completed: return "completed";
    case StopReason::Deadline: return "deadline";
    case StopReason::IterationLimit: return "iteration-limit";
    case StopReason::ClockReversed: return "clock-reversed";
    case StopReason::NativeWaitFailed: return "native-wait-failed";
    case StopReason::DeviceRemoved: return "device-removed";
    }
    return "unknown";
}
struct Result {
    Status status;
    uint64_t completedValue;
    StopReason stopReason;
    uint64_t elapsedUs;
    unsigned waitCalls;
};

template<class Clock, class Poll, class Wait>
Result wait(uint64_t target, Clock&& clock, Poll&& poll, Wait&& waitEvent)
{
    const auto start = clock();
    uint64_t completed = 0;
    const auto finish = [&](Status status, StopReason reason, unsigned calls, uint64_t now) {
        return Result{status, completed, reason, now >= start ? now - start : 0, calls};
    };
    // The iteration bound also handles a stalled clock or stale signalled
    // event. Normally each unsuccessful event wait blocks for one millisecond.
    for (unsigned attempts = 0; attempts <= 100; ++attempts) {
        completed = poll();
        if (completed == (std::numeric_limits<uint64_t>::max)())
            return finish(Status::DeviceRemoved, StopReason::DeviceRemoved, attempts, clock());
        if (completed >= target)
            return finish(Status::Complete, StopReason::Completed, attempts, clock());
        const auto now = clock();
        if (now < start)
            return finish(Status::Timeout, StopReason::ClockReversed, attempts, now);
        if (now - start >= 50000)
            return finish(Status::Timeout, StopReason::Deadline, attempts, now);
        if (attempts == 100)
            return finish(Status::Timeout, StopReason::IterationLimit, attempts, now);
        if (!waitEvent(1))
            return finish(Status::WaitFailed, StopReason::NativeWaitFailed, attempts + 1, clock());
    }
    return finish(Status::Timeout, StopReason::IterationLimit, 100, clock());
}
}
