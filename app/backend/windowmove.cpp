#include "windowmove.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

WindowMove* WindowMove::get(QQmlEngine*)
{
    static WindowMove* instance = new WindowMove();
    return instance;
}

WindowMove::WindowMove(QObject* parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

bool WindowMove::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
#ifdef Q_OS_WIN32
    if (eventType == "windows_generic_MSG") {
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_ENTERSIZEMOVE) {
            setMoving(true);
        }
        else if (msg->message == WM_EXITSIZEMOVE) {
            setMoving(false);
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif

    // Only watching: the window still gets every message.
    return false;
}

void WindowMove::setMoving(bool moving)
{
    if (m_Moving != moving) {
        m_Moving = moving;
        emit movingChanged();
    }
}
