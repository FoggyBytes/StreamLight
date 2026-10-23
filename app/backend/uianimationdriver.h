#pragma once

#include <QAnimationDriver>
#include <QTimer>

/**
 * The interface at the display's refresh rate (8.0.0) — Settings → Interface → "Unlock frame rate".
 *
 * StreamLight runs Qt Quick on the BASIC render loop (main.cpp: the stream needs to block the main
 * thread and pump its events by hand, which the threaded loop does not allow — upstream's choice).
 * On that loop Qt advances every animation from QDefaultAnimationDriver, a 16 ms timer: 60 steps a
 * second, whatever the monitor. The frames are still presented on vsync; there are just only 60
 * new ones to present, so on a 120 or 144 Hz screen the waves, the page slides and the focus all
 * move at 60.
 *
 * This driver replaces that timer with one at the refresh rate of the screen the window is on. It
 * is installed only while the switch is on, and it goes back to 16 ms on its own while a stream is
 * running: the interface is behind the stream then, and the main thread belongs to the session.
 *
 * Public Qt API only (QAnimationDriver), so nothing here depends on Qt's private headers.
 */
class UiAnimationDriver : public QAnimationDriver
{
    Q_OBJECT

public:
    // Installs or removes the driver; called at start-up and whenever the switch changes.
    static void apply(bool unlocked);

    // The refresh rate of the screen the window is on, as this driver ticks to it (0 when no
    // screen says). Settings prints it under the switch, so the words and the timer read one
    // source — QML's Screen.refreshRate read undefined there (21/10/2026).
    static int screenHz();

protected:
    void start() override;
    void stop() override;

private:
    explicit UiAnimationDriver(QObject* parent = nullptr);
    void retime();

    QTimer m_Timer;
};
