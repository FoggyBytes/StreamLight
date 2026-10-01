// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace VrrCatchUp {
// A soft cadence floor, not a new source clock or extra buffer demand. Start
// with gentle recovery, then continuously use more available display headroom
// as replaceable queue age approaches the existing two-period stale limit.
inline uint64_t floorUs(uint64_t lastSubmission, uint64_t sourcePeriod,
                        uint64_t displayFloorPeriod, uint64_t queueAge,
                        uint64_t recoveryPerMille)
{
    if (!lastSubmission || !sourcePeriod || !displayFloorPeriod ||
            displayFloorPeriod >= sourcePeriod || !recoveryPerMille ||
            sourcePeriod > 1000000)
        return 0;
    const uint64_t headroom = sourcePeriod - displayFloorPeriod;
    const uint64_t gentle = std::min(headroom,
        sourcePeriod * std::min<uint64_t>(recoveryPerMille, 1000) / 1000);
    const uint64_t pressure = queueAge > sourcePeriod ?
        std::min(sourcePeriod, queueAge - sourcePeriod) : 0;
    const uint64_t recovery = gentle + (headroom - gentle) * pressure / sourcePeriod;
    const uint64_t interval = sourcePeriod - recovery;
    return lastSubmission > std::numeric_limits<uint64_t>::max() - interval ?
        std::numeric_limits<uint64_t>::max() : lastSubmission + interval;
}
}
