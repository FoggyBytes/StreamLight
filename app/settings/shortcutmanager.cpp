#include "shortcutmanager.h"

#include "SDL_compat.h"
#include <Limelight.h>

#include <QSettings>
#include <QReadWriteLock>
#include <QChar>

#define SHORTCUTS_GROUP "shortcuts"

static ShortcutManager* s_GlobalShortcuts;
Q_GLOBAL_STATIC(QReadWriteLock, s_GlobalShortcutsLock)

// Default keyboard bindings — these mirror the historical hardcoded combos
// (Ctrl+Alt+Shift+<key>). Order MUST match ShortcutManager::KbAction and
// SdlInputHandler::KeyCombo.
struct KbDefault {
    const char* name;
    int modifiers;
    SDL_Keycode keyCode;
    SDL_Scancode scanCode;
    const char* label;
};

static const int DEF_MODS =
        ShortcutManager::SMOD_CTRL | ShortcutManager::SMOD_ALT | ShortcutManager::SMOD_SHIFT;

static const KbDefault s_KbDefaults[ShortcutManager::KB_COUNT] = {
    { "Quit session",                     DEF_MODS, SDLK_q, SDL_SCANCODE_Q, "Q" },
    { "Ungrab keyboard / mouse",          DEF_MODS, SDLK_z, SDL_SCANCODE_Z, "Z" },
    { "Toggle fullscreen",                DEF_MODS, SDLK_x, SDL_SCANCODE_X, "X" },
    { "Toggle performance overlay",       DEF_MODS, SDLK_s, SDL_SCANCODE_S, "S" },
    { "Toggle mouse mode",                DEF_MODS, SDLK_m, SDL_SCANCODE_M, "M" },
    { "Show / hide cursor",               DEF_MODS, SDLK_c, SDL_SCANCODE_C, "C" },
    { "Minimize window",                  DEF_MODS, SDLK_d, SDL_SCANCODE_D, "D" },
    { "Paste clipboard text to host",     DEF_MODS, SDLK_v, SDL_SCANCODE_V, "V" },
    { "Lock pointer to window region",    DEF_MODS, SDLK_l, SDL_SCANCODE_L, "L" },
    { "Open stream settings",             DEF_MODS, SDLK_o, SDL_SCANCODE_O, "O" },
};

struct PadDefault {
    const char* name;
    int mask;
};

static const PadDefault s_PadDefaults[ShortcutManager::PAD_COUNT] = {
    { "Quit session",              PLAY_FLAG | BACK_FLAG | LB_FLAG | RB_FLAG },
    { "Cycle performance overlay", BACK_FLAG | LB_FLAG | RB_FLAG | X_FLAG },
    { "Open stream settings",      BACK_FLAG | LB_FLAG | RB_FLAG | B_FLAG },
};

// Digital buttons that can take part in a pad combo (analog triggers L2/R2 are
// excluded — they are not part of the Limelight button mask). "key" drives
// glyph resolution in QML; "label" is the text fallback.
struct PadButton {
    int flag;
    const char* key;
    const char* label;
    bool system; // counts toward the "must hold a system button" safety rule
};

// Essential combo buttons only — every one has a proper vendor glyph. The
// Guide/PS/Xbox button, touchpad, Share/Capture, D-pad and stick clicks were
// intentionally excluded to keep the picker clean (and Guide is often grabbed
// by the OS/Steam anyway).
static const PadButton s_PadButtons[] = {
    { PLAY_FLAG, "START",  "Start",  true  },
    { BACK_FLAG, "SELECT", "Select", true  },
    { LB_FLAG,   "LB",     "LB",     true  },
    { RB_FLAG,   "RB",     "RB",     true  },
    { A_FLAG,    "A",      "A",      false },
    { B_FLAG,    "B",      "B",      false },
    { X_FLAG,    "X",      "X",      false },
    { Y_FLAG,    "Y",      "Y",      false },
};
static const int s_PadButtonCount = (int)(sizeof(s_PadButtons) / sizeof(s_PadButtons[0]));

ShortcutManager::ShortcutManager(QQmlEngine* qmlEngine)
    : m_QmlEngine(qmlEngine)
{
}

ShortcutManager* ShortcutManager::get(QQmlEngine* qmlEngine)
{
    {
        QReadLocker readGuard(s_GlobalShortcutsLock);
        if (s_GlobalShortcuts && (s_GlobalShortcuts->m_QmlEngine || !qmlEngine)) {
            return s_GlobalShortcuts;
        }
    }

    {
        QWriteLocker writeGuard(s_GlobalShortcutsLock);
        if (s_GlobalShortcuts) {
            if (!s_GlobalShortcuts->m_QmlEngine) {
                s_GlobalShortcuts->m_QmlEngine = qmlEngine;
            }
        }
        else {
            s_GlobalShortcuts = new ShortcutManager(qmlEngine);
        }
        return s_GlobalShortcuts;
    }
}

ShortcutManager::KbBinding ShortcutManager::loadKb(int action)
{
    KbBinding b{};
    if (action < 0 || action >= KB_COUNT) {
        return b;
    }

    const KbDefault& d = s_KbDefaults[action];
    QSettings settings;
    settings.beginGroup(SHORTCUTS_GROUP);
    const QString p = QStringLiteral("kb%1_").arg(action);
    b.modifiers   = settings.value(p + "mods", d.modifiers).toInt();
    b.sdlKeycode  = settings.value(p + "key", (int)d.keyCode).toInt();
    b.sdlScancode = settings.value(p + "scan", (int)d.scanCode).toInt();
    b.enabled     = settings.value(p + "enabled", true).toBool();
    settings.endGroup();
    return b;
}

ShortcutManager::PadBinding ShortcutManager::loadPad(int action)
{
    PadBinding b{};
    if (action < 0 || action >= PAD_COUNT) {
        return b;
    }

    QSettings settings;
    settings.beginGroup(SHORTCUTS_GROUP);
    b.buttonMask = settings.value(QStringLiteral("pad%1_mask").arg(action),
                                  s_PadDefaults[action].mask).toInt();
    settings.endGroup();
    return b;
}

QString ShortcutManager::modifierLabel(int modifiers)
{
    QStringList parts;
    if (modifiers & SMOD_CTRL)  parts << QStringLiteral("Ctrl");
    if (modifiers & SMOD_ALT)   parts << QStringLiteral("Alt");
    if (modifiers & SMOD_SHIFT) parts << QStringLiteral("Shift");
    if (modifiers & SMOD_GUI)   parts << QStringLiteral("Win");
    return parts.join(QStringLiteral(" + "));
}

QVariantList ShortcutManager::keyboardModel()
{
    QVariantList list;
    for (int i = 0; i < KB_COUNT; i++) {
        KbBinding b = loadKb(i);
        // Recover a display label for the key. Stored bindings keep the label;
        // fall back to the default's label if absent (older builds).
        QSettings settings;
        settings.beginGroup(SHORTCUTS_GROUP);
        QString label = settings.value(QStringLiteral("kb%1_label").arg(i),
                                       QString::fromLatin1(s_KbDefaults[i].label)).toString();
        settings.endGroup();

        QVariantMap row;
        row["action"]    = i;
        row["name"]      = QString::fromLatin1(s_KbDefaults[i].name);
        row["modifiers"] = b.modifiers;
        row["key"]       = b.sdlKeycode;
        row["scan"]      = b.sdlScancode;
        row["label"]     = label;
        row["enabled"]   = b.enabled;
        list.append(row);
    }
    return list;
}

QVariantList ShortcutManager::gamepadModel()
{
    QVariantList list;
    for (int i = 0; i < PAD_COUNT; i++) {
        PadBinding b = loadPad(i);
        QVariantMap row;
        row["action"]  = i;
        row["name"]    = QString::fromLatin1(s_PadDefaults[i].name);
        row["mask"]    = b.buttonMask;
        row["buttons"] = describeGamepadMask(b.buttonMask);
        list.append(row);
    }
    return list;
}

QVariantList ShortcutManager::describeGamepadMask(int mask)
{
    QVariantList buttons;
    for (int i = 0; i < s_PadButtonCount; i++) {
        if (mask & s_PadButtons[i].flag) {
            QVariantMap btn;
            btn["key"]   = QString::fromLatin1(s_PadButtons[i].key);
            btn["label"] = QString::fromLatin1(s_PadButtons[i].label);
            buttons.append(btn);
        }
    }
    return buttons;
}

QVariantList ShortcutManager::gamepadButtonCatalog()
{
    QVariantList list;
    for (int i = 0; i < s_PadButtonCount; i++) {
        QVariantMap btn;
        btn["flag"]   = s_PadButtons[i].flag;
        btn["key"]    = QString::fromLatin1(s_PadButtons[i].key);
        btn["label"]  = QString::fromLatin1(s_PadButtons[i].label);
        btn["system"] = s_PadButtons[i].system;
        list.append(btn);
    }
    return list;
}

bool ShortcutManager::gamepadMaskIsSafe(int mask)
{
    // A combo must hold at least 3 buttons, with at least one "system" button
    // (Start/Select/LB/RB). Fewer buttons risk being pressed during normal play.
    int count = 0;
    bool hasSystem = false;
    for (int i = 0; i < s_PadButtonCount; i++) {
        if (mask & s_PadButtons[i].flag) {
            count++;
            if (s_PadButtons[i].system) {
                hasSystem = true;
            }
        }
    }
    return count >= 3 && hasSystem;
}

QVariantMap ShortcutManager::translateQtKey(int qtKey, int nativeScanCode)
{
    QVariantMap r;
    r["ok"] = false;

    int sdlKey = 0;
    SDL_Scancode scan = SDL_SCANCODE_UNKNOWN;
    QString label;

    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        int off = qtKey - Qt::Key_A;
        sdlKey = SDLK_a + off;
        scan = (SDL_Scancode)(SDL_SCANCODE_A + off);
        label = QChar('A' + off);
    }
    else if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        int d = qtKey - Qt::Key_0;
        sdlKey = SDLK_0 + d;
        scan = (d == 0) ? SDL_SCANCODE_0 : (SDL_Scancode)(SDL_SCANCODE_1 + (d - 1));
        label = QChar('0' + d);
    }
    else if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12) {
        int n = qtKey - Qt::Key_F1; // 0-based
        scan = (SDL_Scancode)(SDL_SCANCODE_F1 + n);
        sdlKey = SDL_SCANCODE_TO_KEYCODE(scan);
        label = QStringLiteral("F%1").arg(n + 1);
    }
    else if (qtKey >= Qt::Key_F13 && qtKey <= Qt::Key_F24) {
        int n = qtKey - Qt::Key_F13;
        scan = (SDL_Scancode)(SDL_SCANCODE_F13 + n);
        sdlKey = SDL_SCANCODE_TO_KEYCODE(scan);
        label = QStringLiteral("F%1").arg(n + 13);
    }
    else {
        switch (qtKey) {
        case Qt::Key_Space:  sdlKey = SDLK_SPACE;  scan = SDL_SCANCODE_SPACE;  label = QStringLiteral("Space"); break;
        case Qt::Key_Home:   sdlKey = SDLK_HOME;   scan = SDL_SCANCODE_HOME;   label = QStringLiteral("Home"); break;
        case Qt::Key_End:    sdlKey = SDLK_END;    scan = SDL_SCANCODE_END;    label = QStringLiteral("End"); break;
        case Qt::Key_Insert: sdlKey = SDLK_INSERT; scan = SDL_SCANCODE_INSERT; label = QStringLiteral("Insert"); break;
        case Qt::Key_Delete: sdlKey = SDLK_DELETE; scan = SDL_SCANCODE_DELETE; label = QStringLiteral("Delete"); break;
        case Qt::Key_PageUp:   sdlKey = SDLK_PAGEUP;   scan = SDL_SCANCODE_PAGEUP;   label = QStringLiteral("PgUp"); break;
        case Qt::Key_PageDown: sdlKey = SDLK_PAGEDOWN; scan = SDL_SCANCODE_PAGEDOWN; label = QStringLiteral("PgDn"); break;
        case Qt::Key_Left:  sdlKey = SDLK_LEFT;  scan = SDL_SCANCODE_LEFT;  label = QStringLiteral("←"); break;
        case Qt::Key_Right: sdlKey = SDLK_RIGHT; scan = SDL_SCANCODE_RIGHT; label = QStringLiteral("→"); break;
        case Qt::Key_Up:    sdlKey = SDLK_UP;    scan = SDL_SCANCODE_UP;    label = QStringLiteral("↑"); break;
        case Qt::Key_Down:  sdlKey = SDLK_DOWN;  scan = SDL_SCANCODE_DOWN;  label = QStringLiteral("↓"); break;
        // Punctuation — bound by physical key (scancode). Shifted variants map to
        // the same physical key so e.g. "~" records as the grave/backtick key.
        case Qt::Key_Minus:        sdlKey = SDLK_MINUS;        scan = SDL_SCANCODE_MINUS;        label = QStringLiteral("-"); break;
        case Qt::Key_Equal:        sdlKey = SDLK_EQUALS;       scan = SDL_SCANCODE_EQUALS;       label = QStringLiteral("="); break;
        case Qt::Key_BracketLeft:  sdlKey = SDLK_LEFTBRACKET;  scan = SDL_SCANCODE_LEFTBRACKET;  label = QStringLiteral("["); break;
        case Qt::Key_BracketRight: sdlKey = SDLK_RIGHTBRACKET; scan = SDL_SCANCODE_RIGHTBRACKET; label = QStringLiteral("]"); break;
        case Qt::Key_Backslash:    sdlKey = SDLK_BACKSLASH;    scan = SDL_SCANCODE_BACKSLASH;    label = QStringLiteral("\\"); break;
        case Qt::Key_Semicolon:    sdlKey = SDLK_SEMICOLON;    scan = SDL_SCANCODE_SEMICOLON;    label = QStringLiteral(";"); break;
        case Qt::Key_Apostrophe:   sdlKey = SDLK_QUOTE;        scan = SDL_SCANCODE_APOSTROPHE;   label = QStringLiteral("'"); break;
        case Qt::Key_Comma:        sdlKey = SDLK_COMMA;        scan = SDL_SCANCODE_COMMA;        label = QStringLiteral(","); break;
        case Qt::Key_Period:       sdlKey = SDLK_PERIOD;       scan = SDL_SCANCODE_PERIOD;       label = QStringLiteral("."); break;
        case Qt::Key_Slash:        sdlKey = SDLK_SLASH;        scan = SDL_SCANCODE_SLASH;        label = QStringLiteral("/"); break;
        case Qt::Key_QuoteLeft:
        case Qt::Key_AsciiTilde:   sdlKey = SDLK_BACKQUOTE;    scan = SDL_SCANCODE_GRAVE;        label = QStringLiteral("`"); break;
        default:
            // Holding Shift turns digits/punctuation into symbol Qt keys
            // (Shift+1 -> Key_Exclam, Shift+, -> Key_Less, …) and that symbol↔key
            // mapping is keyboard-layout dependent, so Qt::Key can't resolve them.
            // Fall back to the Windows hardware scan code, which is the same
            // regardless of Shift or layout. The runtime combo match compares
            // scancodes too, so this binds correctly on any layout (the printed
            // label uses the US legend for punctuation, but the key is right).
            switch (nativeScanCode & 0xFF) {
            case 0x02: sdlKey = SDLK_1; scan = SDL_SCANCODE_1; label = QStringLiteral("1"); break;
            case 0x03: sdlKey = SDLK_2; scan = SDL_SCANCODE_2; label = QStringLiteral("2"); break;
            case 0x04: sdlKey = SDLK_3; scan = SDL_SCANCODE_3; label = QStringLiteral("3"); break;
            case 0x05: sdlKey = SDLK_4; scan = SDL_SCANCODE_4; label = QStringLiteral("4"); break;
            case 0x06: sdlKey = SDLK_5; scan = SDL_SCANCODE_5; label = QStringLiteral("5"); break;
            case 0x07: sdlKey = SDLK_6; scan = SDL_SCANCODE_6; label = QStringLiteral("6"); break;
            case 0x08: sdlKey = SDLK_7; scan = SDL_SCANCODE_7; label = QStringLiteral("7"); break;
            case 0x09: sdlKey = SDLK_8; scan = SDL_SCANCODE_8; label = QStringLiteral("8"); break;
            case 0x0A: sdlKey = SDLK_9; scan = SDL_SCANCODE_9; label = QStringLiteral("9"); break;
            case 0x0B: sdlKey = SDLK_0; scan = SDL_SCANCODE_0; label = QStringLiteral("0"); break;
            case 0x0C: sdlKey = SDLK_MINUS;        scan = SDL_SCANCODE_MINUS;        label = QStringLiteral("-"); break;
            case 0x0D: sdlKey = SDLK_EQUALS;       scan = SDL_SCANCODE_EQUALS;       label = QStringLiteral("="); break;
            case 0x1A: sdlKey = SDLK_LEFTBRACKET;  scan = SDL_SCANCODE_LEFTBRACKET;  label = QStringLiteral("["); break;
            case 0x1B: sdlKey = SDLK_RIGHTBRACKET; scan = SDL_SCANCODE_RIGHTBRACKET; label = QStringLiteral("]"); break;
            case 0x27: sdlKey = SDLK_SEMICOLON;    scan = SDL_SCANCODE_SEMICOLON;    label = QStringLiteral(";"); break;
            case 0x28: sdlKey = SDLK_QUOTE;        scan = SDL_SCANCODE_APOSTROPHE;   label = QStringLiteral("'"); break;
            case 0x29: sdlKey = SDLK_BACKQUOTE;    scan = SDL_SCANCODE_GRAVE;        label = QStringLiteral("`"); break;
            case 0x2B: sdlKey = SDLK_BACKSLASH;    scan = SDL_SCANCODE_BACKSLASH;    label = QStringLiteral("\\"); break;
            case 0x33: sdlKey = SDLK_COMMA;        scan = SDL_SCANCODE_COMMA;        label = QStringLiteral(","); break;
            case 0x34: sdlKey = SDLK_PERIOD;       scan = SDL_SCANCODE_PERIOD;       label = QStringLiteral("."); break;
            case 0x35: sdlKey = SDLK_SLASH;        scan = SDL_SCANCODE_SLASH;        label = QStringLiteral("/"); break;
            default:
                return r; // genuinely unsupported
            }
            break;
        }
    }

    r["ok"] = true;
    r["sdlKey"] = sdlKey;
    r["sdlScan"] = (int)scan;
    r["label"] = label;
    return r;
}

int ShortcutManager::keyboardConflict(int action, int modifiers, int sdlKey, int sdlScan)
{
    for (int i = 0; i < KB_COUNT; i++) {
        if (i == action) {
            continue;
        }
        KbBinding b = loadKb(i);
        if (!b.enabled) {
            continue;
        }
        if (b.modifiers == modifiers &&
                (b.sdlKeycode == sdlKey || b.sdlScancode == sdlScan)) {
            return i;
        }
    }
    return -1;
}

int ShortcutManager::gamepadConflict(int action, int buttonMask)
{
    for (int i = 0; i < PAD_COUNT; i++) {
        if (i == action) {
            continue;
        }
        if (loadPad(i).buttonMask == buttonMask) {
            return i;
        }
    }
    return -1;
}

void ShortcutManager::setKeyboardBinding(int action, int modifiers, int sdlKey, int sdlScan, const QString& label)
{
    if (action < 0 || action >= KB_COUNT) {
        return;
    }
    QSettings settings;
    settings.beginGroup(SHORTCUTS_GROUP);
    const QString p = QStringLiteral("kb%1_").arg(action);
    settings.setValue(p + "mods", modifiers);
    settings.setValue(p + "key", sdlKey);
    settings.setValue(p + "scan", sdlScan);
    settings.setValue(p + "label", label);
    settings.setValue(p + "enabled", true);
    settings.endGroup();
    emit shortcutsChanged();
}

void ShortcutManager::setKeyboardEnabled(int action, bool enabled)
{
    if (action < 0 || action >= KB_COUNT) {
        return;
    }
    QSettings settings;
    settings.beginGroup(SHORTCUTS_GROUP);
    settings.setValue(QStringLiteral("kb%1_enabled").arg(action), enabled);
    settings.endGroup();
    emit shortcutsChanged();
}

void ShortcutManager::resetKeyboard(int action)
{
    if (action < 0 || action >= KB_COUNT) {
        return;
    }
    const KbDefault& d = s_KbDefaults[action];
    QSettings settings;
    settings.beginGroup(SHORTCUTS_GROUP);
    const QString p = QStringLiteral("kb%1_").arg(action);
    settings.setValue(p + "mods", d.modifiers);
    settings.setValue(p + "key", (int)d.keyCode);
    settings.setValue(p + "scan", (int)d.scanCode);
    settings.setValue(p + "label", QString::fromLatin1(d.label));
    settings.setValue(p + "enabled", true);
    settings.endGroup();
    emit shortcutsChanged();
}

void ShortcutManager::setGamepadBinding(int action, int buttonMask)
{
    if (action < 0 || action >= PAD_COUNT) {
        return;
    }
    QSettings settings;
    settings.beginGroup(SHORTCUTS_GROUP);
    settings.setValue(QStringLiteral("pad%1_mask").arg(action), buttonMask);
    settings.endGroup();
    emit shortcutsChanged();
}

void ShortcutManager::resetGamepad(int action)
{
    if (action < 0 || action >= PAD_COUNT) {
        return;
    }
    QSettings settings;
    settings.beginGroup(SHORTCUTS_GROUP);
    settings.setValue(QStringLiteral("pad%1_mask").arg(action), s_PadDefaults[action].mask);
    settings.endGroup();
    emit shortcutsChanged();
}

void ShortcutManager::resetAll()
{
    for (int i = 0; i < KB_COUNT; i++) {
        resetKeyboard(i);
    }
    for (int i = 0; i < PAD_COUNT; i++) {
        resetGamepad(i);
    }
}
