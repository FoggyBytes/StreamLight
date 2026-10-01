// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <QMutex>
#include <QWaitCondition>

// A preparation ticket refers to the same admitted image as the pacing queue;
// it is not an additional frame admission. Only the preparation thread writes
// timing, before complete(). Consumers may read it only after wait() succeeds.
class VrrPreparedFrame {
public:
    virtual ~VrrPreparedFrame() = default;

    struct Timing {
        uint64_t startUs = 0;
        uint64_t decodeReadyUs = 0;
        uint64_t decodeWaitUs = 0;
        uint64_t renderStartUs = 0;
        uint64_t renderEndUs = 0;
        uint64_t readyUs = 0;
    } timing;

    void cancel() { m_Cancelled.store(true); }
    bool cancelled() const { return m_Cancelled.load(); }

    virtual bool wait(const std::function<bool()>& interrupted)
    {
        QMutexLocker lock(&m_Lock);
        while (!m_Complete && !cancelled() && !interrupted()) {
            m_Changed.wait(&m_Lock, 1);
        }
        if (interrupted()) cancel();
        return m_Complete && m_Succeeded && !cancelled();
    }

    void complete(bool succeeded)
    {
        QMutexLocker lock(&m_Lock);
        m_Succeeded = succeeded;
        m_Complete = true;
        m_Changed.wakeAll();
    }

private:
    QMutex m_Lock;
    QWaitCondition m_Changed;
    std::atomic_bool m_Cancelled { false };
    bool m_Complete = false;
    bool m_Succeeded = false;
};
