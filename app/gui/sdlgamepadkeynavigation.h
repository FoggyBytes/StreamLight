#pragma once

#include <QTimer>
#include <QEvent>
#include <QPoint>
#include <QElapsedTimer>
#include <QHash>

class QKeyEvent;

#include "SDL_compat.h"

#include "settings/streamingpreferences.h"

class SdlGamepadKeyNavigation : public QObject
{
    Q_OBJECT

    // Exposes the detected controller family so QML can render the correct
    // button glyphs (Xbox vs PlayStation vs generic).
    // Values: "xbox", "ps", "switch", "steam", "generic", "none".
    Q_PROPERTY(QString controllerType READ controllerType NOTIFY controllerTypeChanged)

    // Tracks the last-used input device so QML can suppress mouse hover when
    // navigating with gamepad/keyboard and vice versa.
    // Values: "pointer" (mouse), "key" (gamepad or keyboard). With Button prompts = Controller
    // it stays "key": the mouse still points and clicks, but never switches the mode (#24).
    Q_PROPERTY(QString inputMode READ inputMode NOTIFY inputModeChanged)

public:
    SdlGamepadKeyNavigation(StreamingPreferences* prefs);

    ~SdlGamepadKeyNavigation();

    Q_INVOKABLE void enable();

    Q_INVOKABLE void disable();

    Q_INVOKABLE void notifyWindowFocus(bool hasFocus);

    Q_INVOKABLE void setUiNavMode(bool settingsMode);

    Q_INVOKABLE int getConnectedGamepads();

    // Simulate a key press+release pair on the focused window. Used by the
    // clickable status-bar prompts so a mouse click on (e.g.) "Settings" or
    // "Back" produces the same effect as the matching gamepad button.
    Q_INVOKABLE void simulateKey(int qtKey);

    // Re-emit controllerTypeChanged so the UI re-resolves glyphs after the user
    // changes the glyph-set preference in Settings.
    Q_INVOKABLE void refreshGlyphPreference() { emit controllerTypeChanged(); }

    QString controllerType() const;

    QString inputMode() const { return m_InputMode; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void controllerTypeChanged();
    void inputModeChanged();
    // 6.5.1 (#24): Steam's Desktop Layout is sending the pad as keyboard too — seen from the
    // echoes below, once per launch. menuHoldSwitches: the pad is the Steam Controller
    // (28de:1304), whose Desktop Layout switches to Gamepad on a long press of Menu.
    void steamDesktopLayoutDetected(bool menuHoldSwitches);

private:
    void sendKey(QEvent::Type type, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    void updateTimerState();

    void updateControllerType();

    void setInputMode(const QString& mode);

    // Button prompts = Controller: mouse movement and clicks leave inputMode on "key" (#24).
    bool padNavigationPinned() const;

    // Steam Input echoes (#24). With Steam open on the client, its Desktop Layout sends a
    // controller's D-pad and face buttons as keyboard keys too, while SDL reads the same
    // controller directly — so every press arrived twice and the focus moved by two. A pad
    // press and a keyboard key meaning the same thing within a short window are one press:
    // whichever arrives first acts, the other is dropped (and its release with it).
    enum NavButton { NB_UP, NB_DOWN, NB_LEFT, NB_RIGHT, NB_ACCEPT, NB_BACK, NB_COUNT };
    static int navButtonForKey(int qtKey);
    static int navButtonForPad(int sdlButton);
    // Pad side: true = this press echoes a key already delivered, drop it.
    bool padPressIsEcho(int nav);
    // Keyboard side, from the event filter: true = swallow the event.
    bool keyEventIsEcho(QKeyEvent* ke);
    // Counts one matched pair; the third one reports the Desktop Layout.
    void noteInputEcho();
    bool steamControllerConnected() const;

private slots:
    void onPollingTimerFired();

private:
    StreamingPreferences* m_Prefs;
    QTimer* m_PollingTimer;
    QList<SDL_GameController*> m_Gamepads;
    bool m_Enabled;
    bool m_UiNavMode;
    bool m_FirstPoll;
    bool m_HasFocus;
    Uint32 m_LastAxisNavigationEventTime;
    // Triggers are edge-detected, not fed through the stick repeat timer above:
    // pulling LT/RT is one discrete "previous/next host", not a direction you hold.
    bool m_LeftTriggerDown;
    bool m_RightTriggerDown;
    QString m_ControllerType;
    QString m_InputMode;
    QPoint m_LastMousePos;
    bool m_HasLastMousePos;

    // Echo matching (#24). Times on m_NavClock, -1 = nothing waiting. Each press cancels at
    // most one press from the other side, so fast tapping with both sources stays 1:1.
    QElapsedTimer m_NavClock;
    qint64 m_PadNavAt[NB_COUNT];
    qint64 m_KeyNavAt[NB_COUNT];
    bool m_PadNavDropped[NB_COUNT];
    // The decision taken for the current press of each keyboard key. A real key reaches the
    // filter first as ShortcutOverride and then as KeyPress (same timestamp): both must get
    // the same answer, and its auto-repeats and release follow it.
    struct KeyDecision {
        quint64 timestamp;
        bool echo;
    };
    QHash<int, KeyDecision> m_KeyDecisions;
    int m_EchoCount;
    bool m_DesktopLayoutReported;
};
