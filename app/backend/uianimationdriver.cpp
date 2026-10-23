#include "uianimationdriver.h"

#include "streaming/session.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QtMath>

namespace {
UiAnimationDriver* s_Driver = nullptr;
bool s_Installed = false;
}

UiAnimationDriver::UiAnimationDriver(QObject* parent)
    : QAnimationDriver(parent)
{
    // Precise: a coarse timer on Windows rounds to the 15.6 ms system tick, which is the very
    // 60-ish rate this exists to get away from.
    m_Timer.setTimerType(Qt::PreciseTimer);
    connect(&m_Timer, &QTimer::timeout, this, [this]() {
        retime();
        advance();
    });
}

void UiAnimationDriver::apply(bool unlocked)
{
    if (unlocked == s_Installed) {
        return;
    }
    if (s_Driver == nullptr) {
        s_Driver = new UiAnimationDriver(qApp);
    }
    if (unlocked) {
        s_Driver->install();
    }
    else {
        s_Driver->uninstall();
    }
    s_Installed = unlocked;
    qInfo() << "[ui] animation frame rate:" << (unlocked ? "display refresh rate" : "Qt default (60)");
}

void UiAnimationDriver::start()
{
    QAnimationDriver::start();
    retime();
    m_Timer.start();
}

void UiAnimationDriver::stop()
{
    m_Timer.stop();
    QAnimationDriver::stop();
}

int UiAnimationDriver::screenHz()
{
    QScreen* screen = nullptr;
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow* w : windows) {
        if (w->isVisible() && w->screen() != nullptr) {
            screen = w->screen();
            break;
        }
    }
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    return (screen != nullptr && screen->refreshRate() > 1.0)
            ? qBound(30, qRound(screen->refreshRate()), 500) : 0;
}

// The refresh rate of the screen the window is on, or 60 while a stream runs. Read on every tick:
// it costs a pointer walk, and the window can move to another monitor, or a stream start, at any time.
void UiAnimationDriver::retime()
{
    int hz = 60;
    if (Session::get() == nullptr) {
        const int screen = screenHz();
        if (screen > 0) {
            hz = screen;
        }
    }

    // Whole milliseconds: 120 Hz ticks every 8 ms (125 a second) and the frame is still presented
    // on vsync, so the screen shows 120; 144 Hz → 7 ms; 240 Hz → 4 ms.
    const int interval = qMax(1, qFloor(1000.0 / hz));
    if (m_Timer.interval() != interval) {
        m_Timer.setInterval(interval);
        // Once per change, so the log says what the switch actually does on this machine.
        qInfo() << "[ui] animation tick:" << interval << "ms (" << hz << "Hz )";
    }
}
