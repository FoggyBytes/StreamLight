// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <cstdint>
#include <limits>

// Composition presentation timestamps use system-relative 100 ns units.
// Correlate against QPC converted to those units, inside a worker clock bracket.
// QueryInterruptTimePrecise can have a different epoch on a live Windows system.
// Re-correlating each observation also avoids carrying an epoch across sleep.
struct PresentationClockSample {
    uint64_t timeUs = 0;
    uint64_t uncertaintyUs = 0;

    static uint64_t qpcTo100ns(uint64_t ticks, uint64_t frequency)
    {
        constexpr uint64_t units = 10000000;
        constexpr uint64_t max = (std::numeric_limits<uint64_t>::max)();
        if (!frequency || frequency > max / units || ticks / frequency > max / units) {
            return 0;
        }
        const uint64_t whole = ticks / frequency * units;
        const uint64_t fractional = ticks % frequency * units / frequency;
        return whole > max - fractional ? 0 : whole + fractional;
    }

    static PresentationClockSample translate(uint64_t displayed100ns,
                                            uint64_t reference100ns,
                                            uint64_t beforeUs,
                                            uint64_t afterUs)
    {
        if (!displayed100ns || displayed100ns > reference100ns ||
                afterUs < beforeUs || afterUs - beforeUs > 500) {
            return {};
        }
        const uint64_t age100ns = reference100ns - displayed100ns;
        // The live feedback matcher only admits events from the last 100 ms.
        if (age100ns > 1000000) {
            return {};
        }
        const uint64_t midpointUs = beforeUs + (afterUs - beforeUs) / 2;
        const uint64_t ageUs = age100ns / 10;
        if (ageUs >= midpointUs) {
            return {};
        }
        return {midpointUs - ageUs, (afterUs - beforeUs + 1) / 2 + 1};
    }
};
