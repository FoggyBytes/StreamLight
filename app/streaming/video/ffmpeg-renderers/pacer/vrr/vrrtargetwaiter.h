// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <cstdint>
#include <functional>

// Hooks make the waiter deterministic in tests and, more importantly, let the
// VRR worker supply the exact monotonic epoch used for decode-complete times
// (normally the same LiGetMicroseconds-style clock that constructed
// PacedFrame).  The default is SDL's monotonic performance-counter epoch and
// is suitable only when callers build deadlines from monotonicNowUs().
struct VrrTargetWaiterHooks {
    std::function<uint64_t()> nowUs;
    std::function<void(uint64_t)> sleepForUs;
    std::function<void()> yield;
};

struct VrrTargetWaitResult {
    uint64_t initialNowUs = 0;
    uint64_t finalNowUs = 0;
    uint64_t activeWaitUs = 0;
    uint64_t coarseSleepCount = 0;
    uint64_t coarseSleepRequestedUs = 0;
    uint64_t coarseSleepRequestedWakeUs = 0;
    uint64_t coarseSleepReturnUs = 0;
    bool coarseSleepClockStalled = false;
    bool activeWaitEntered = false;
    uint64_t activeWaitStartUs = 0;
    uint64_t activeWaitLimitUs = 0;
    uint64_t activeWaitYieldCount = 0;
    bool activeWaitClockStalled = false;
    bool activeWaitYieldLimitReached = false;
    // Additional early wake needed beyond the fixed active margin, measured
    // at the coarse sleep boundary. Unlike final overshoot, this observation
    // remains meaningful after an early-wake correction succeeds.
    uint64_t schedulerDelayUs = 0;
    bool schedulerDelayValid = false;
    bool deadlineAlreadyElapsed = false;
};

class VrrTargetWaiter {
public:
    static constexpr uint64_t kMaximumActiveWaitUs = 500;
    // Cap the active region so learned scheduler correction cannot turn a
    // near-refresh stream into a multi-millisecond TIME_CRITICAL yield loop.
    static constexpr uint64_t kMaximumAdditionalWakeLeadUs = 500;

    explicit VrrTargetWaiter(VrrTargetWaiterHooks hooks = {});

    // Wait until an absolute deadline in the supplied clock's epoch. Learned
    // scheduler delay may extend the early-wake/active region, but remains
    // bounded even if a test hook or platform scheduler fails to advance.
    VrrTargetWaitResult waitUntil(uint64_t deadlineUs,
                                  uint64_t additionalWakeLeadUs = 0) const;

    // Default monotonic source for callers that also use this method to stamp
    // decode completion.  Integrations using another epoch must provide a
    // matching nowUs hook instead.
    static uint64_t monotonicNowUs();

private:
    static void defaultSleepForUs(uint64_t durationUs);
    static void defaultYield();

    VrrTargetWaiterHooks m_Hooks;
};
