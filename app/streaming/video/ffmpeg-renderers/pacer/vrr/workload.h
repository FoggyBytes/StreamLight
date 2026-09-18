// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once
#include "reserve.h"

namespace Vrr13 {
// Decoder backlog is measured before the pacer's intentional waits. Retain a
// bounded episode until it drains; sustained overload cannot be fixed by adding
// queue latency. Receive jitter without decoder backlog is learned immediately.
class WorkloadEpisode {
public:
    WorkloadEpisode() { m_Pending.reserve(512); }
    void observe(Reserve& reserve, int64_t required, int64_t applied, int64_t at,
                 uint64_t decoderQueueUs, uint64_t sourcePeriodUs) {
        if (decoderQueueUs > sourcePeriodUs) {
            if (!m_Start) m_Start = at;
            if (at - m_Start >= 2 * Reserve::Second || m_Pending.size() == 512) {
                m_Overloaded = true;
                m_Pending.clear();
            }
            if (!m_Overloaded) m_Pending.push_back({required, applied, at});
            return;
        }
        for (const auto& sample : m_Pending)
            reserve.observe(sample.required, sample.applied, sample.at);
        m_Pending.clear();
        m_Start = 0;
        m_Overloaded = false;
        reserve.observe(required, applied, at);
    }
    void reset() { m_Pending.clear(); m_Start = 0; m_Overloaded = false; }
private:
    struct Sample { int64_t required, applied, at; };
    std::vector<Sample> m_Pending;
    int64_t m_Start = 0;
    bool m_Overloaded = false;
};
}
