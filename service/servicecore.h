#pragma once

#include "controlpipe.h"
#include "eventwatch.h"
#include "powerpolicy.h"
#include "tvstore.h"

#include <QJsonObject>

#include <memory>
#include <mutex>

// The service's state: the paired TV, the policy that acts on it, and the pipe through which
// StreamLight reads and changes both. Power notices from the host (service or foreground window)
// come in here; with no TV paired they are dropped and only the pipe is alive.
class ServiceCore
{
public:
    ServiceCore() = default;
    ~ServiceCore();
    ServiceCore(const ServiceCore&) = delete;
    ServiceCore& operator=(const ServiceCore&) = delete;

    bool start(bool echo);
    void stop();

    bool hasTv() const;
    ULONGLONG lastActivity() const { return m_Pipe ? m_Pipe->lastActivity() : 0; }

    void suspending();
    void resumed();
    void shutdownRequested(const QString& source, bool restart, const QString& type);
    void booted(int bootType);

private:
    QJsonObject handle(const QJsonObject& request, const ControlPipe::Send& event);
    QJsonObject status() const;
    QJsonObject discover();
    QJsonObject pair(const QJsonObject& request, const ControlPipe::Send& event);
    QJsonObject set(const QJsonObject& request);
    QJsonObject forget();
    QJsonObject tvCommand(bool on);
    QJsonObject restartAnnounced();

    // Replaces the policy with one for this record (none when it is not paired). m_Lock held.
    void apply(const TvRecord& tv);

    mutable std::recursive_mutex m_Lock;
    TvRecord m_Tv;
    std::unique_ptr<PowerPolicy> m_Policy;
    std::unique_ptr<EventWatch> m_Events;
    std::unique_ptr<ControlPipe> m_Pipe;
    bool m_StartedAtBoot = false;
    bool m_Started = false;
};
