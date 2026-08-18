#include "powerstatus.h"

#include <QtGlobal>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

// How often the state is re-read. Small enough that plugging a handheld in shows up while the
// user's hand is still on the cable, cheap enough that it does not matter that it never stops:
// GetSystemPowerStatus reads values the power manager already holds.
static const int PollIntervalMs = 5000;

PowerStatus* PowerStatus::get(QQmlEngine*)
{
    static PowerStatus* instance = new PowerStatus();
    return instance;
}

PowerStatus::PowerStatus(QObject* parent)
    : QObject(parent)
{
    connect(&m_Timer, &QTimer::timeout, this, &PowerStatus::refresh);
    m_Timer.start(PollIntervalMs);

    // Read once now, so the first frame is already right instead of empty for five seconds.
    refresh();
}

void PowerStatus::refresh()
{
    bool present   = false;
    int  percent   = -1;
    bool pluggedIn = false;
    bool charging  = false;

#ifdef Q_OS_WIN32
    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        // BatteryFlag is 255 when Windows cannot tell, and carries 128 for "no system battery".
        const bool flagValid = (status.BatteryFlag != BYTE(255));
        const bool noBattery = flagValid && (status.BatteryFlag & 128);

        // 255 means unknown here too. Requiring a real number before claiming a battery exists
        // keeps a half-drawn gauge off screens we know nothing about.
        if (status.BatteryLifePercent <= 100) {
            percent = status.BatteryLifePercent;
        }

        present   = !noBattery && percent >= 0;
        pluggedIn = (status.ACLineStatus == 1);

        // Bit 8 is the authoritative "currently charging". Trusting it matters on handhelds
        // with a charge limit — an Ally parked at 80% on mains is plugged in and not charging,
        // and inferring from "plugged and below full" would show it filling forever.
        if (flagValid) {
            charging = (status.BatteryFlag & 8) != 0;
        }
        else {
            charging = pluggedIn && percent >= 0 && percent < 100;
        }
    }
#endif

    if (present == m_Present && percent == m_Percent &&
        pluggedIn == m_PluggedIn && charging == m_Charging) {
        return;
    }

    m_Present   = present;
    m_Percent   = percent;
    m_PluggedIn = pluggedIn;
    m_Charging  = charging;
    emit changed();
}
