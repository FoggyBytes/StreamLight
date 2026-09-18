// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <atomic>
#include <cstdint>

namespace WindowsVblankVirtualization {

struct Snapshot {
    bool probeComplete = false;
    bool callAvailable = false;
    bool resultValid = false;
    int64_t result = 0;
    bool disabled = false;
};

namespace Detail {

constexpr uint64_t kProbeComplete = 1ULL << 0;
constexpr uint64_t kCallAvailable = 1ULL << 1;
constexpr uint64_t kResultValid = 1ULL << 2;
constexpr unsigned kResultShift = 32;

inline std::atomic<uint64_t> encodedState { 0 };

}

inline void recordUnavailable()
{
    Detail::encodedState.store(
        Detail::kProbeComplete, std::memory_order_release);
}

inline void recordResult(int64_t result)
{
    const uint64_t resultBits =
        static_cast<uint32_t>(result);
    Detail::encodedState.store(
        Detail::kProbeComplete |
            Detail::kCallAvailable |
            Detail::kResultValid |
            (resultBits << Detail::kResultShift),
        std::memory_order_release);
}

inline Snapshot snapshot()
{
    const uint64_t encoded =
        Detail::encodedState.load(std::memory_order_acquire);
    Snapshot value;
    value.probeComplete =
        (encoded & Detail::kProbeComplete) != 0;
    value.callAvailable =
        (encoded & Detail::kCallAvailable) != 0;
    value.resultValid =
        (encoded & Detail::kResultValid) != 0;
    if (value.resultValid) {
        const uint32_t resultBits = static_cast<uint32_t>(
            encoded >> Detail::kResultShift);
        value.result = static_cast<int64_t>(resultBits);
        if ((resultBits & 0x80000000U) != 0) {
            value.result -= (1LL << 32);
        }
    }
    value.disabled = value.resultValid && value.result == 0;
    return value;
}

}
