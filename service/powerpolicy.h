#pragma once

#include "tvcontroller.h"

#include <windows.h>

#include <mutex>

// When the TV goes on and off: on when the PC starts (a Fast Startup boot too), off when it shuts
// down, left on by a restart. Nothing else.
//
// ⚠️ Sleep, wake and the screen are left alone on purpose (03/11/2026, Marcello's call after the
// SER9's measurements). Up to 8.4.0's development the TV followed the PC's screen, and on Modern
// Standby that cannot be made reliable: a Logitech Unifying receiver sends an empty mouse report
// (no movement, no button) 6-32 s after the keyboard was last used — or switched off — and
// Windows takes it as input and wakes, from the Start menu's Sleep as from StreamLight's. Windows
// even marks the user present for it, so nothing the service can read tells it from a hand. A PC
// that sleeps now leaves the TV on its no-signal screen, and the TV's own settings take it from
// there. docs/piano-streamlight-8.4.0-tv.md has the measurements.
class PowerPolicy
{
public:
    // startedAtBoot: the service started with Windows, so the TV is turned on now. Otherwise
    // (installed, updated, restarted by hand, rebuilt for a new pairing or new rules) it is left
    // alone: the TV may well be showing something else.
    PowerPolicy(const TvRecord& tv, bool startedAtBoot);

    TvController& tv() { return m_Tv; }

    // New rules or input for the same TV, from StreamLight. Unlike a new policy, this does not stop
    // a turn-on or turn-off under way.
    void updateRules(const TvRecord& tv);

    // StreamLight is about to restart this PC (its Power popup): the TV stays on.
    void restartAnnounced();

    // A sleep begins / ends. Nothing is done to the TV; they only close a shutdown that did not
    // happen (a Fast Startup "shut down" is a suspend for the service, and it ends with a boot).
    void suspending();
    void resumed();
    // A shutdown or restart is on its way. Several notices arrive for one (event 1074, then
    // preshutdown, or the end of the session): the first acts, the rest are logged.
    void shutdownRequested(const QString& source, bool restart, const QString& type);
    void booted(int bootType);

private:
    TvRecord m_Rules;
    TvController m_Tv;
    std::mutex m_ShutdownLock;
    qint64 m_ShutdownHandledAt = 0;
};
