// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <algorithm>
#include <cstdint>

namespace Vrr13 {
// Attribute an output spacing error to unfinished pre-presentation work.
// An absolute readiness prediction, host jitter, or native blocking alone
// cannot authorize additional buffering.
class ReadinessFeedback {
public:
    struct Sample {
        uint64_t frame = 0, intended = 0, submitted = 0, ready = 0, buffer = 0;
        bool eligible = false;
    };
    struct Result { bool interval = false; uint64_t demand = 0; };
    Result observe(const Sample& s, uint64_t tolerance) {
        if (!s.eligible || !s.intended || !s.submitted) { reset(); return {}; }
        const auto previous = m_Previous;
        const bool adjacent = m_HavePrevious && s.frame == previous.frame + 1 &&
            s.submitted > previous.submitted && s.intended > previous.intended;
        m_Previous = s;
        m_HavePrevious = true;
        if (!adjacent) return {};
        const auto actual = s.submitted - previous.submitted;
        const auto intended = s.intended - previous.intended;
        if (actual >= 1000000 || intended >= 1000000) return {};
        const auto error = std::max(actual, intended) - std::min(actual, intended);
        const auto& delayed = actual >= intended ? s : previous;
        const auto lateness = delayed.ready > delayed.intended ?
            delayed.ready - delayed.intended : 0;
        // Retain the earlier frame's buffer when a late frame is followed by
        // catch-up, so the same miss cannot charge today's buffer twice.
        const auto useful = std::min(error, lateness);
        return {true, useful > tolerance ?
            std::min<uint64_t>(200000, delayed.buffer + useful - tolerance) : 0};
    }
    void reset() { m_HavePrevious = false; }
private:
    Sample m_Previous;
    bool m_HavePrevious = false;
};
}
