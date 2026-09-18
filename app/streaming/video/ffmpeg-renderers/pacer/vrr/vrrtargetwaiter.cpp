// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "vrrtargetwaiter.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace {

uint64_t saturatingAdd(uint64_t left, uint64_t right)
{
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    return left > maximum - right ? maximum : left + right;
}

} // namespace

VrrTargetWaiter::VrrTargetWaiter(VrrTargetWaiterHooks hooks) :
    m_Hooks(std::move(hooks))
{
    if (!m_Hooks.nowUs) {
        m_Hooks.nowUs = []() { return monotonicNowUs(); };
    }
    if (!m_Hooks.sleepForUs) {
        m_Hooks.sleepForUs = [](uint64_t durationUs) {
            defaultSleepForUs(durationUs);
        };
    }
    if (!m_Hooks.yield) {
        m_Hooks.yield = []() { defaultYield(); };
    }
}

VrrTargetWaitResult VrrTargetWaiter::waitUntil(
    uint64_t deadlineUs, uint64_t additionalWakeLeadUs) const
{
    VrrTargetWaitResult result;
    uint64_t nowUs = m_Hooks.nowUs();
    result.initialNowUs = nowUs;
    const uint64_t boundedWakeLeadUs = std::min(
        additionalWakeLeadUs, kMaximumAdditionalWakeLeadUs);
    const uint64_t activeWaitUs = saturatingAdd(kMaximumActiveWaitUs,
                                                 boundedWakeLeadUs);
    result.activeWaitUs = activeWaitUs;
    if (nowUs >= deadlineUs) {
        result.deadlineAlreadyElapsed = true;
        result.finalNowUs = nowUs;
        return result;
    }

    unsigned int stagnantCoarseSleeps = 0;
    while (nowUs < deadlineUs) {
        const uint64_t remainingUs = deadlineUs - nowUs;
        if (remainingUs <= activeWaitUs) {
            break;
        }

        // Wake before the target by the fixed active margin plus any bounded
        // delay learned from earlier scheduler overshoots. The remaining
        // interval is deliberately delegated to the active path below.
        const uint64_t coarseSleepUs =
            remainingUs > activeWaitUs ? remainingUs - activeWaitUs : 1;
        const uint64_t requestedWakeUs = saturatingAdd(nowUs, coarseSleepUs);
        ++result.coarseSleepCount;
        result.coarseSleepRequestedUs = saturatingAdd(
            result.coarseSleepRequestedUs, coarseSleepUs);
        result.coarseSleepRequestedWakeUs = requestedWakeUs;
        m_Hooks.sleepForUs(coarseSleepUs);

        const uint64_t afterSleepUs = m_Hooks.nowUs();
        result.coarseSleepReturnUs = afterSleepUs;
        result.schedulerDelayValid = true;
        if (afterSleepUs > requestedWakeUs) {
            const uint64_t coarseOvershootUs =
                afterSleepUs - requestedWakeUs;
            const uint64_t additionalLeadUs =
                coarseOvershootUs > kMaximumActiveWaitUs ?
                    coarseOvershootUs - kMaximumActiveWaitUs : 0;
            result.schedulerDelayUs = std::max(
                result.schedulerDelayUs, additionalLeadUs);
        }
        if (afterSleepUs <= nowUs) {
            // A broken test hook or a platform sleep that was interrupted
            // before it yielded must not leave a pacing worker spinning for
            // an unbounded interval.
            if (++stagnantCoarseSleeps >= 2) {
                nowUs = afterSleepUs;
                result.coarseSleepClockStalled = true;
                break;
            }
        }
        else {
            stagnantCoarseSleeps = 0;
            nowUs = afterSleepUs;
        }
    }

    if (nowUs < deadlineUs) {
        const uint64_t activeStartUs = nowUs;
        const uint64_t activeLimitUs = saturatingAdd(
            activeStartUs, activeWaitUs);
        result.activeWaitEntered = true;
        result.activeWaitStartUs = activeStartUs;
        result.activeWaitLimitUs = activeLimitUs;
        unsigned int stagnantYields = 0;
        unsigned int yieldCount = 0;

        // The monotonic active-time limit is the real safety bound. A fixed
        // yield-count limit is CPU- and scheduler-dependent: on a fast
        // machine it can expire hundreds of microseconds early even though
        // the clock is advancing normally. The stagnant-clock detector below
        // remains the bounded escape for broken hooks or a stopped clock.
        while (nowUs < deadlineUs && nowUs < activeLimitUs) {
            m_Hooks.yield();
            ++yieldCount;
            result.activeWaitYieldCount = yieldCount;
            const uint64_t afterYieldUs = m_Hooks.nowUs();
            if (afterYieldUs <= nowUs) {
                if (++stagnantYields >= 64) {
                    result.activeWaitClockStalled = true;
                    break;
                }
            }
            else {
                stagnantYields = 0;
                nowUs = afterYieldUs;
            }
        }
        result.activeWaitYieldLimitReached = false;

        result.finalNowUs = nowUs;
        return result;
    }

    result.finalNowUs = nowUs;
    return result;
}

uint64_t VrrTargetWaiter::monotonicNowUs()
{
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    if (frequency != 0) {
        const Uint64 counter = SDL_GetPerformanceCounter();
        return (counter / frequency) * 1000000ULL +
            (counter % frequency) * 1000000ULL / frequency;
    }

    static const std::chrono::steady_clock::time_point origin =
        std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - origin).count());
}

void VrrTargetWaiter::defaultSleepForUs(uint64_t durationUs)
{
    if (durationUs == 0) {
        return;
    }

#ifdef _WIN32
    struct WaitableTimer {
        WaitableTimer() :
            handle(CreateWaitableTimerExW(nullptr, nullptr,
                                          CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_MODIFY_STATE | SYNCHRONIZE))
        {
        }

        ~WaitableTimer()
        {
            if (handle != nullptr) {
                CloseHandle(handle);
            }
        }

        HANDLE handle;
    };

    // std::this_thread::sleep_for() can overshoot sub-frame deadlines by a
    // large fraction of Windows' timer tick. Reuse one high-resolution timer
    // per pacing thread so the bounded final yield starts before the target
    // instead of after the presentation window has already closed.
    thread_local WaitableTimer timer;
    if (timer.handle != nullptr) {
        LARGE_INTEGER dueTime;
        const uint64_t maximumUs =
            static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max() / 10);
        const uint64_t boundedDurationUs = std::min(durationUs, maximumUs);
        dueTime.QuadPart = -static_cast<LONGLONG>(boundedDurationUs * 10);
        if (SetWaitableTimerEx(timer.handle, &dueTime, 0, nullptr, nullptr,
                               nullptr, 0) &&
                WaitForSingleObject(timer.handle, INFINITE) == WAIT_OBJECT_0) {
            return;
        }
    }
#endif

    std::this_thread::sleep_for(std::chrono::microseconds(durationUs));
}

void VrrTargetWaiter::defaultYield()
{
    std::this_thread::yield();
}
