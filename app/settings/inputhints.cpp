#include "inputhints.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>

InputHints* InputHints::get(QQmlEngine*)
{
    static InputHints* instance = new InputHints();
    return instance;
}

InputHints::InputHints(QObject* parent)
    : QObject(parent)
{
    // Application-wide, so no screen has to remember to report input. Installed on the
    // application object rather than a window: dialogs and popups are windows of their own.
    QCoreApplication::instance()->installEventFilter(this);
}

void InputHints::setPadActive(bool padActive)
{
    if (m_PadActive == padActive) {
        return;
    }

    m_PadActive = padActive;
    emit padActiveChanged();
}

void InputHints::notePadInput()
{
    setPadActive(true);
}

void InputHints::seedFromConnectedPads(bool anyConnected)
{
    setPadActive(anyConnected);
}

bool InputHints::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
        // ⚠️ spontaneous() is doing the load-bearing work. Pad buttons arrive here as Qt key
        // events too, posted by SdlGamepadKeyNavigation::simulateKey — B as Escape, the D-pad
        // as arrows. Those are not spontaneous; only input the window system delivered is.
        // Without this check every controller press would be read as typing and the prompts
        // would flip to keyboard on the very button they are describing.
        //
        // Auto-repeat is ignored as well: holding a key is one intent, not fifty.
        if (event->spontaneous() && !static_cast<QKeyEvent*>(event)->isAutoRepeat()) {
            setPadActive(false);
        }
        break;

    case QEvent::MouseButtonPress:
        // A click counts as keyboard-and-mouse: someone reaching for the mouse wants the
        // prompts that name keys. Movement deliberately does not — a mouse knocked on a desk
        // must not repaint the whole interface.
        if (event->spontaneous()) {
            setPadActive(false);
        }
        break;

    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}
