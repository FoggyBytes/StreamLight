#include "powerpolicy.h"

#include "servicelog.h"

#include <QDateTime>

namespace
{

constexpr int k_ShutdownBudgetMs = 5000;
// A shutdown that is still not happening after this long was cancelled (shutdown /a).
constexpr qint64 k_ShutdownWindowMs = 3 * 60 * 1000;

QString describe(const TvRecord& tv)
{
    return QStringLiteral("HDMI %1; on when the PC starts %2, off when it shuts down %3, only if showing this PC %4")
            .arg(tv.input).arg(tv.onAtStart).arg(tv.offAtShutdown).arg(tv.offOnlyIfShowingPc);
}

}

PowerPolicy::PowerPolicy(const TvRecord& tv, bool startedAtBoot)
    : m_Rules(tv),
      m_Tv(tv)
{
    ServiceLog::write(QStringLiteral("policy: %1; started at boot %2").arg(describe(tv)).arg(startedAtBoot));
    // At boot the network may not be up yet: the controller keeps waking the TV for a while.
    if (startedAtBoot && m_Rules.onAtStart)
        m_Tv.turnOn(QStringLiteral("boot"));
}

void PowerPolicy::suspending()
{
    std::lock_guard<std::mutex> lock(m_ShutdownLock);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // A suspend right after a shut down is Fast Startup's way of shutting down (8.5.0).
    m_SuspendedAfterShutdown = m_LastWasShutdown && m_ShutdownHandledAt
                               && now - m_ShutdownHandledAt < k_ShutdownWindowMs;
    // A new suspend ends the boot before it: a "TV already on for this boot" left over from it
    // (a Kernel-Boot 27 that never came) must not stop the next boot from turning the TV on.
    m_OnForThisBoot = false;
    ServiceLog::write(m_SuspendedAfterShutdown
                              ? QStringLiteral("policy: suspending after a shut down — Fast Startup; the TV comes on at the resume")
                              : QStringLiteral("policy: going to sleep, TV left as it is"));
}

void PowerPolicy::resumed()
{
    std::lock_guard<std::mutex> lock(m_ShutdownLock);
    m_ShutdownHandledAt = 0;
    m_LastWasShutdown = false;
    const bool boot = m_SuspendedAfterShutdown;
    m_SuspendedAfterShutdown = false;
    if (!boot) {
        ServiceLog::write(QStringLiteral("policy: resumed, TV left as it is"));
        return;
    }
    // 8.5.0: the Fast Startup boot, recognised here rather than when Kernel-Boot 27 is delivered.
    ServiceLog::write(QStringLiteral("policy: resumed from a Fast Startup shut down — a boot"));
    if (m_Rules.onAtStart) {
        m_OnForThisBoot = true;
        m_Tv.turnOn(QStringLiteral("Fast Startup boot, at the resume"));
    }
}

void PowerPolicy::shutdownRequested(const QString& source, bool restart, const QString& type)
{
    {
        std::lock_guard<std::mutex> lock(m_ShutdownLock);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_ShutdownHandledAt && now - m_ShutdownHandledAt < k_ShutdownWindowMs) {
            ServiceLog::write(QStringLiteral("policy: %1: shutdown already handled").arg(source));
            return;
        }
        m_ShutdownHandledAt = now;
        m_LastWasShutdown = !restart;
    }
    if (restart) {
        ServiceLog::write(QStringLiteral("policy: %1: restart (\"%2\"), TV left on").arg(source, type));
        return;
    }
    // An unrecognised type is treated as a shutdown: better a TV turned off by mistake than one
    // left on all night.
    ServiceLog::write(QStringLiteral("policy: %1: shutdown (\"%2\")").arg(source, type));
    if (m_Rules.offAtShutdown)
        m_Tv.turnOffNow(QStringLiteral("shutdown"), m_Rules.offOnlyIfShowingPc, k_ShutdownBudgetMs);
}

void PowerPolicy::updateRules(const TvRecord& tv)
{
    m_Rules = tv;
    m_Tv.setInput(tv.input);
    ServiceLog::write(QStringLiteral("policy: rules now %1").arg(describe(tv)));
}

void PowerPolicy::restartAnnounced()
{
    {
        std::lock_guard<std::mutex> lock(m_ShutdownLock);
        m_ShutdownHandledAt = QDateTime::currentMSecsSinceEpoch();
    }
    ServiceLog::write(QStringLiteral("policy: StreamLight announced a restart, TV left on"));
}

void PowerPolicy::booted(int bootType)
{
    bool alreadyOn = false;
    {
        std::lock_guard<std::mutex> lock(m_ShutdownLock);
        m_ShutdownHandledAt = 0;
        alreadyOn = m_OnForThisBoot;
        m_OnForThisBoot = false;
    }
    // 1 = a Fast Startup boot: for the service it is a resume, with no start, so this is where the
    // TV comes on — unless the resume already recognised it (8.5.0). 2 = back from hibernation: a
    // wake, left alone like any other.
    if (bootType == 1 && m_Rules.onAtStart) {
        if (alreadyOn)
            ServiceLog::write(QStringLiteral("policy: Fast Startup boot — the TV was already turned on at the resume"));
        else
            m_Tv.turnOn(QStringLiteral("Fast Startup boot"));
    }
}
