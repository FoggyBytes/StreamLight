#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

// Central store for user-configurable shortcuts (keyboard hotkeys + gamepad
// button combos). Exposed as a QML singleton for the Settings UI and read by
// SdlInputHandler at session start. Bindings persist in QSettings under the
// "shortcuts/" group so they survive across the build-wiped dev deploy dir
// (registry-backed, same as StreamingPreferences).
//
// The keyboard action ids below MUST stay in the same order/count as
// SdlInputHandler::KeyCombo — the handler indexes its combo table by these.
class ShortcutManager : public QObject
{
    Q_OBJECT

public:
    static ShortcutManager* get(QQmlEngine* qmlEngine = nullptr);

    enum KbAction {
        KB_QUIT,
        KB_UNGRAB,
        KB_FULLSCREEN,
        KB_STATS,
        KB_MOUSEMODE,
        KB_CURSORHIDE,
        KB_MINIMIZE,
        KB_PASTE,
        KB_POINTERLOCK,
        KB_STREAM_SETTINGS,
        KB_COUNT
    };
    Q_ENUM(KbAction)

    // Modifier bitmask used in stored bindings and the QML capture dialog. These
    // are translated to SDL KMOD_* presence checks inside SdlInputHandler.
    // NB: the SMOD_ prefix avoids colliding with winuser.h's MOD_ALT / MOD_SHIFT
    // RegisterHotKey macros.
    enum Modifier {
        SMOD_CTRL  = 0x01,
        SMOD_ALT   = 0x02,
        SMOD_SHIFT = 0x04,
        SMOD_GUI   = 0x08
    };
    Q_ENUM(Modifier)

    enum PadAction {
        PAD_QUIT,
        PAD_STATS,
        PAD_STREAM_SETTINGS,
        PAD_COUNT
    };
    Q_ENUM(PadAction)

    // --- C++ side (read by SdlInputHandler, no singleton needed) ---
    struct KbBinding {
        int modifiers;   // Modifier bitmask
        int sdlKeycode;  // SDLK_*
        int sdlScancode; // SDL_SCANCODE_*
        bool enabled;
    };
    struct PadBinding {
        int buttonMask;  // OR of Limelight *_FLAG values; 0 = disabled
    };
    static KbBinding  loadKb(int action);
    static PadBinding loadPad(int action);

    // --- QML model accessors (Settings UI) ---
    Q_INVOKABLE QVariantList keyboardModel();
    Q_INVOKABLE QVariantList gamepadModel();

    Q_INVOKABLE QString modifierLabel(int modifiers);
    // Decompose a button mask into [{ key, label }] for chip rendering.
    Q_INVOKABLE QVariantList describeGamepadMask(int mask);
    // Full catalogue of bindable digital buttons [{ flag, key, label, system }]
    // for the gamepad combo picker.
    Q_INVOKABLE QVariantList gamepadButtonCatalog();
    // Translate a captured key to SDL codes + a display label. qtKey resolves
    // layout-stable keys (letters/F-keys/arrows/nav); nativeScanCode is the
    // fallback for digits/punctuation, which Shift mutates into layout-dependent
    // symbol Qt keys. Returns { ok, sdlKey, sdlScan, label }.
    Q_INVOKABLE QVariantMap translateQtKey(int qtKey, int nativeScanCode);
    // A pad combo must hold at least one "system" button (Start/Select/LB/RB/
    // Guide) so it never collides with ordinary gameplay input.
    Q_INVOKABLE bool gamepadMaskIsSafe(int mask);

    Q_INVOKABLE void setKeyboardBinding(int action, int modifiers, int sdlKey, int sdlScan, const QString& label);
    Q_INVOKABLE void setKeyboardEnabled(int action, bool enabled);
    Q_INVOKABLE void resetKeyboard(int action);
    // Returns the action id this binding would collide with, or -1 if free.
    Q_INVOKABLE int  keyboardConflict(int action, int modifiers, int sdlKey, int sdlScan);

    Q_INVOKABLE void setGamepadBinding(int action, int buttonMask);
    Q_INVOKABLE void resetGamepad(int action);
    Q_INVOKABLE int  gamepadConflict(int action, int buttonMask);

    Q_INVOKABLE void resetAll();

signals:
    void shortcutsChanged();

private:
    explicit ShortcutManager(QQmlEngine* qmlEngine);

    QQmlEngine* m_QmlEngine;
};
