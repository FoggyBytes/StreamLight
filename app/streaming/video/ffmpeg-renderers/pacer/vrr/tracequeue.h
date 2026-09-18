// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace Vrr13 {
// Bounded multiple-producer/single-consumer queue. The reader never takes a
// producer lock. A producer reserves one slot, copies it, then publishes it;
// the reader cannot observe a partially written row. Allocate before capture.
template<class T, size_t Capacity>
class TraceQueue {
public:
    TraceQueue() : m_Slots(new Slot[Capacity]) {
        static_assert(Capacity > 1 && (Capacity & (Capacity - 1)) == 0,
                      "Trace queue capacity must be a power of two");
        for (size_t i = 0; i < Capacity; ++i) m_Slots[i].sequence.store(i);
    }
    bool push(T&& value) {
        auto position = m_Write.load(std::memory_order_relaxed);
        // Bound contention work as well as memory. A full queue still drops
        // diagnostic rows instead of ever blocking frame delivery.
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            auto& slot = m_Slots[position & (Capacity - 1)];
            const auto sequence = slot.sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<intptr_t>(sequence - position);
            if (difference < 0) return false;
            if (difference == 0 && m_Write.compare_exchange_weak(position, position + 1,
                    std::memory_order_relaxed)) {
                slot.value = std::move(value);
                slot.sequence.store(position + 1, std::memory_order_release);
                return true;
            }
            position = m_Write.load(std::memory_order_relaxed);
        }
        return false;
    }
    bool pop(T& value) {
        auto& slot = m_Slots[m_Read & (Capacity - 1)];
        if (slot.sequence.load(std::memory_order_acquire) != m_Read + 1) return false;
        value = std::move(slot.value);
        slot.sequence.store(m_Read + Capacity, std::memory_order_release);
        ++m_Read;
        return true;
    }
private:
    struct Slot { std::atomic_size_t sequence{0}; T value{}; };
    std::unique_ptr<Slot[]> m_Slots;
    std::atomic_size_t m_Write{0};
    size_t m_Read = 0;
};
}
