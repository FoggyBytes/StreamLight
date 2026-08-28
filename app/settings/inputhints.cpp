#include "inputhints.h"

#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
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

void InputHints::setPointerHidden(bool hidden)
{
    if (m_PointerHidden == hidden) {
        return;
    }

    m_PointerHidden = hidden;

    // The equality guard above is what keeps the override-cursor stack balanced: one push per
    // hide, one pop per reveal, never two of either.
    if (hidden) {
        m_HiddenAt = QCursor::pos();
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
    }
    else {
        QGuiApplication::restoreOverrideCursor();
    }

    emit pointerHiddenChanged();
}

void InputHints::notePadInput()
{
    setPadActive(true);
    setPointerHidden(true);
}

void InputHints::seedFromConnectedPads(bool anyConnected)
{
    // Prompts only. A pad merely being plugged in is enough to draw controller glyphs on the
    // first frame, but not enough to take the pointer away from someone who has not touched
    // it yet — on a desktop with a pad in a drawer that would blank the cursor at launch.
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
            setPointerHidden(false);
        }
        break;

    case QEvent::MouseButtonPress:
        // A click counts as keyboard-and-mouse: someone reaching for the mouse wants the
        // prompts that name keys. Movement deliberately does not — a mouse knocked on a desk
        // must not repaint the whole interface.
        if (event->spontaneous()) {
            setPadActive(false);
            setPointerHidden(false);
        }
        break;

    case QEvent::MouseMove:
    case QEvent::HoverMove:
        // The pointer, and ONLY the pointer, comes back on movement — see the note on
        // pointerHidden for why this rule cannot be the same as padActive's.
        //
        // ⚠️ Both event types are needed and neither is enough on its own: a Qt Quick window
        // turns platform mouse moves into hover events for its items, and which of the two an
        // app-level filter sees depends on what is under the cursor. And the position is
        // compared rather than trusting the event, because hover events are also manufactured
        // when items move under a cursor that has not budged — a list scrolling under the pad
        // would otherwise hand the pointer straight back.
        if (m_PointerHidden && QCursor::pos() != m_HiddenAt) {
            setPointerHidden(false);
        }
        break;

    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}
