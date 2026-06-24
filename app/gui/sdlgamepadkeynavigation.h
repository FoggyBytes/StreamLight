#pragma once

#include <QTimer>
#include <QEvent>
#include <QPoint>

#include "SDL_compat.h"

#include "settings/streamingpreferences.h"

class SdlGamepadKeyNavigation : public QObject
{
    Q_OBJECT

    // Exposes the detected controller family so QML can render the correct
    // button glyphs (Xbox vs PlayStation vs generic).
    // Values: "xbox", "ps", "switch", "generic", "none".
    Q_PROPERTY(QString controllerType READ controllerType NOTIFY controllerTypeChanged)

    // Tracks the last-used input device so QML can suppress mouse hover when
    // navigating with gamepad/keyboard and vice versa.
    // Values: "pointer" (mouse), "key" (gamepad or keyboard).
    Q_PROPERTY(QString inputMode READ inputMode NOTIFY inputModeChanged)

public:
    SdlGamepadKeyNavigation(StreamingPreferences* prefs);

    ~SdlGamepadKeyNavigation();

    Q_INVOKABLE void enable();

    Q_INVOKABLE void disable();

    Q_INVOKABLE void notifyWindowFocus(bool hasFocus);

    Q_INVOKABLE void setUiNavMode(bool settingsMode);

    Q_INVOKABLE int getConnectedGamepads();

    // Debug-only: write a line to the streamlight_pad.log file from QML.
    Q_INVOKABLE void dbgLog(const QString& msg);

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

private:
    void sendKey(QEvent::Type type, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    void updateTimerState();

    void updateControllerType();

    void setInputMode(const QString& mode);

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
    QString m_ControllerType;
    QString m_InputMode;
    QPoint m_LastMousePos;
    bool m_HasLastMousePos;
};
