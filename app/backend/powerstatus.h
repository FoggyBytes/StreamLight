#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>

/**
 * This device's own battery, for the client UI.
 *
 * <p>Qt has no battery API — QBatteryInfo lived in Qt Mobility and did not survive into Qt 6 —
 * so this is platform code. Only Windows is implemented; everywhere else it reports no battery,
 * which is the same answer a desktop gives and therefore needs no special handling in QML: the
 * UI simply shows nothing.</p>
 *
 * <p>The state is polled rather than pushed. Windows does broadcast WM_POWERBROADCAST on a
 * plug or unplug, but reading it means a native event filter over the whole application to
 * catch an event that changes a percentage in the corner of one screen. GetSystemPowerStatus
 * reads cached values out of the power manager and costs microseconds, so a small interval
 * buys the same responsiveness for none of the machinery. `changed` is emitted only when a
 * value actually moves, so a poll that finds nothing new costs QML nothing at all.</p>
 */
class PowerStatus : public QObject
{
    Q_OBJECT

    /**
     * This device runs on a battery — a laptop or a handheld. False on a desktop, and on any
     * platform where we cannot tell, which is the same thing as far as the UI is concerned:
     * there is nothing to show either way.
     */
    Q_PROPERTY(bool present READ present NOTIFY changed)

    /** Charge remaining, 0–100. -1 when unknown (also whenever `present` is false). */
    Q_PROPERTY(int percent READ percent NOTIFY changed)

    /** Mains power is connected — says nothing about whether the battery is still filling. */
    Q_PROPERTY(bool pluggedIn READ pluggedIn NOTIFY changed)

    /** Actively charging: on mains and not yet full. This is what earns the bolt. */
    Q_PROPERTY(bool charging READ charging NOTIFY changed)

public:
    static PowerStatus* get(QQmlEngine* engine = nullptr);

    bool present()   const { return m_Present; }
    int  percent()   const { return m_Percent; }
    bool pluggedIn() const { return m_PluggedIn; }
    bool charging()  const { return m_Charging; }

    /**
     * Re-reads the state now instead of waiting for the next tick. For a screen that has just
     * become visible again, where the displayed figure may be as old as the interval.
     */
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    explicit PowerStatus(QObject* parent = nullptr);

    QTimer m_Timer;
    bool   m_Present   = false;
    int    m_Percent   = -1;
    bool   m_PluggedIn = false;
    bool   m_Charging  = false;
};
