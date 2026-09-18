// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include "vrrtimingcontroller.h"
#include <limits>

// Call only when a newer successor is available. Never discard the sole image
// after a host stall. Shared by the worker and the all-arrival queue simulation.
namespace VrrFrameDropPolicy {
inline uint64_t maximumAgeUs(const VrrTimingDecision& decision, bool metronome, bool latencyFix)
{
    // One source interval is normal occupancy for a single worker that waits
    // on the preceding frame's target. Using it as the stale threshold makes
    // a refresh-rate stream alternate present/drop as soon as its successor
    // arrives. Lower-latency modes still measure age from admission, but they
    // must tolerate two intervals before replacing valid decoded work.
    (void) latencyFix;
    const uint64_t periods = metronome ? 4 : 2;
    return decision.sourcePeriodUs > std::numeric_limits<uint64_t>::max() / periods ?
        std::numeric_limits<uint64_t>::max() : decision.sourcePeriodUs * periods;
}

inline bool beforeRender(const VrrTimingDecision& decision, uint64_t displayPeriodUs,
                         uint64_t ageUs, bool metronome, bool latencyFix)
{
    const bool oversupply = displayPeriodUs != 0 && decision.sourcePeriodUs <= displayPeriodUs;
    const bool floorBacklog = (oversupply || latencyFix) &&
        decision.presentationFloorPushUs > decision.sourcePeriodUs / 2;
    return decision.sourcePeriodUs != 0 &&
        (ageUs > maximumAgeUs(decision, metronome, latencyFix) ||
         (metronome && decision.missedTicks != 0) || floorBacklog);
}

inline bool afterRenderWait(const VrrTimingDecision& decision, uint64_t ageOriginUs,
                            uint64_t nowUs, bool metronome, bool latencyFix)
{
    const uint64_t originUs = latencyFix ? ageOriginUs : decision.targetUs;
    const uint64_t limitUs = maximumAgeUs(decision, metronome, latencyFix);
    return limitUs != 0 && nowUs > originUs && nowUs - originUs > limitUs;
}
}
