#include "sdlgamepadkeynavigation.h"
#include "settings/inputhints.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QWindow>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>

#include "settings/mappingmanager.h"

// Append a line to a debug log file next to the executable.
// Lets us diagnose gamepad-nav issues without redirecting stderr
// (which crashes the app under PowerShell `2>`).
static void slDbg(const QString& line)
{
    static QFile* f = nullptr;
    if (!f) {
        f = new QFile(QCoreApplication::applicationDirPath() + "/streamlight_pad.log");
        f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    if (f->isOpen()) {
        QTextStream(f) << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
                       << " " << line << "\n";
        f->flush();
    }
}

#define AXIS_NAVIGATION_REPEAT_DELAY 150

// How far apart a pad press and Steam Input's keyboard copy of it can land and still be one
// press (#24). The pad is polled every 50 ms, so the copy usually comes first by up to that.
#define INPUT_ECHO_WINDOW_MS 150

// Matched pairs before the Desktop Layout notice (6.5.1). In the log of #24 the third came
// within 3 s of the first press; one alone could be a key pressed with the pad by chance.
#define DESKTOP_LAYOUT_ECHOES 3

// Valve. The Steam Controller, the Steam Deck — and Steam's virtual pad, which is an Xbox 360
// to everyone and is kept on the Xbox glyphs by its SDL type.
#define USB_VENDOR_VALVE 0x28de
// The Steam Controller seen in #24, whose Desktop Layout has "Change Action Set (Gamepad)" on
// a long press of Menu. Other Valve pads are not known to, so the notice names it only here.
#define USB_PRODUCT_STEAM_CONTROLLER 0x1304

// #24 diagnostics: every controller the interface opens, in the main StreamLight-*.log. A
// second device here (Steam's virtual pad is VID 28de PID 11ff) is the other way a press can
// arrive twice, and the echo matching below does not cover it.
static void logPadDevice(const char* what, SDL_GameController* gc)
{
    SDL_Joystick* js = SDL_GameControllerGetJoystick(gc);
    const char* name = SDL_GameControllerName(gc);
    const char* path = SDL_GameControllerPath(gc);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[padnav] %s: '%s' instance=%d VID=%04x PID=%04x type=%d path=%s",
                what, name ? name : "(null)", (int)SDL_JoystickInstanceID(js),
                SDL_GameControllerGetVendor(gc), SDL_GameControllerGetProduct(gc),
                (int)SDL_GameControllerGetType(gc), path ? path : "(null)");
}

SdlGamepadKeyNavigation::SdlGamepadKeyNavigation(StreamingPreferences* prefs)
    : m_Prefs(prefs),
      m_Enabled(false),
      m_UiNavMode(false),
      m_FirstPoll(false),
      m_HasFocus(false),
      m_LastAxisNavigationEventTime(0),
      m_LeftTriggerDown(false),
      m_RightTriggerDown(false),
      m_ControllerType(QStringLiteral("none")),
      m_InputMode(QStringLiteral("pointer")),
      m_HasLastMousePos(false),
      m_EchoCount(0),
      m_DesktopLayoutReported(false)
{
    m_PollingTimer = new QTimer(this);
    connect(m_PollingTimer, &QTimer::timeout, this, &SdlGamepadKeyNavigation::onPollingTimerFired);

    m_NavClock.start();
    for (int i = 0; i < NB_COUNT; i++) {
        m_PadNavAt[i] = -1;
        m_KeyNavAt[i] = -1;
        m_PadNavDropped[i] = false;
    }

    // Issue #24: Button prompts = Controller also pins the navigation to the pad, so a Steam
    // Input trackpad can point and click without switching the interface to mouse use. The
    // mode starts as "pointer" above, and that start has to be undone here as well as on a
    // change — otherwise the focus ring stays off until the first pad press.
    if (padNavigationPinned()) {
        m_InputMode = QStringLiteral("key");
    }
    connect(m_Prefs, &StreamingPreferences::inputPromptsChanged, this, [this]() {
        // Applied the moment it is picked. Back on Auto nothing moves: the next mouse
        // movement or key press decides, as it always has.
        if (padNavigationPinned()) {
            setInputMode(QStringLiteral("key"));
        }
    });

    if (qGuiApp != nullptr) {
        qGuiApp->installEventFilter(this);
    }
}

void SdlGamepadKeyNavigation::simulateKey(int qtKey)
{
    Qt::Key key = static_cast<Qt::Key>(qtKey);
    sendKey(QEvent::Type::KeyPress,   key);
    sendKey(QEvent::Type::KeyRelease, key);
}

QString SdlGamepadKeyNavigation::controllerType() const
{
    // A forced glyph-set preference overrides auto-detection so users with
    // generic / unrecognised pads (or a personal preference) can pick the
    // button icons they want. GS_AUTO falls back to the detected family.
    switch (m_Prefs->glyphSet) {
    case StreamingPreferences::GS_XBOX:
        return QStringLiteral("xbox");
    case StreamingPreferences::GS_PLAYSTATION:
        return QStringLiteral("ps");
    case StreamingPreferences::GS_NINTENDO:
        return QStringLiteral("switch");
    case StreamingPreferences::GS_STEAM:
        return QStringLiteral("steam");
    case StreamingPreferences::GS_AUTO:
    default:
        return m_ControllerType;
    }
}

void SdlGamepadKeyNavigation::updateControllerType()
{
    QString newType = QStringLiteral("none");
    if (!m_Gamepads.isEmpty()) {
        SDL_GameController* gc = m_Gamepads.first();
        SDL_GameControllerType t = SDL_GameControllerGetType(gc);
        const char* name = SDL_GameControllerName(gc);
        switch (t) {
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
        case SDL_CONTROLLER_TYPE_PS5:
            newType = QStringLiteral("ps");
            break;
        case SDL_CONTROLLER_TYPE_XBOX360:
        case SDL_CONTROLLER_TYPE_XBOXONE:
            newType = QStringLiteral("xbox");
            break;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            newType = QStringLiteral("switch");
            break;
        default:
            // SDL has no type for Valve's own pads: the Steam Controller of #24 reports 0
            // (unknown). Read by vendor, after the Xbox cases, so Steam's virtual pad — Valve
            // too, but typed as an Xbox 360 — keeps the Xbox glyphs.
            newType = SDL_GameControllerGetVendor(gc) == USB_VENDOR_VALVE
                    ? QStringLiteral("steam") : QStringLiteral("generic");
            break;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Gamepad detected: %s (SDL type=%d, family=%s)",
                    name ? name : "(null)", (int)t, qPrintable(newType));
    }
    if (newType != m_ControllerType) {
        m_ControllerType = newType;
        emit controllerTypeChanged();
    }
}

SdlGamepadKeyNavigation::~SdlGamepadKeyNavigation()
{
    if (qGuiApp != nullptr) {
        qGuiApp->removeEventFilter(this);
    }
    disable();
}

bool SdlGamepadKeyNavigation::padNavigationPinned() const
{
    return m_Prefs->inputPrompts == StreamingPreferences::IP_CONTROLLER;
}

void SdlGamepadKeyNavigation::setInputMode(const QString& mode)
{
    if (m_InputMode == mode) {
        return;
    }
    m_InputMode = mode;
    emit inputModeChanged();
}

bool SdlGamepadKeyNavigation::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint pos = me->globalPosition().toPoint();
        if (!m_HasLastMousePos) {
            m_LastMousePos = pos;
            m_HasLastMousePos = true;
        } else {
            const int dx = qAbs(pos.x() - m_LastMousePos.x());
            const int dy = qAbs(pos.y() - m_LastMousePos.y());
            if (dx + dy >= 4) {
                m_LastMousePos = pos;
                if (!padNavigationPinned()) {
                    setInputMode(QStringLiteral("pointer"));
                }
            }
        }
        break;
    }
    case QEvent::MouseButtonPress:
        // Pinned to the pad, a click still lands on what it hits — it just does not take the
        // focus ring away or hand the selection to the hover.
        if (!padNavigationPinned()) {
            setInputMode(QStringLiteral("pointer"));
        }
        break;
    case QEvent::KeyPress:
        setInputMode(QStringLiteral("key"));
        // Only keys the window system delivered: the pad's own keys are sent by sendKey()
        // and are never spontaneous.
        if (event->spontaneous() && keyEventIsEcho(static_cast<QKeyEvent*>(event))) {
            return true;
        }
        break;
    case QEvent::ShortcutOverride:
    case QEvent::KeyRelease:
        if (event->spontaneous() && keyEventIsEcho(static_cast<QKeyEvent*>(event))) {
            return true;
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

int SdlGamepadKeyNavigation::navButtonForKey(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_Up:     return NB_UP;
    case Qt::Key_Down:   return NB_DOWN;
    case Qt::Key_Left:   return NB_LEFT;
    case Qt::Key_Right:  return NB_RIGHT;
    case Qt::Key_Return:
    case Qt::Key_Enter:  return NB_ACCEPT;
    case Qt::Key_Escape: return NB_BACK;
    default:             return NB_COUNT;
    }
}

int SdlGamepadKeyNavigation::navButtonForPad(int sdlButton)
{
    // After the face-button swap: what the press will act as, not where it sits.
    switch (sdlButton) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    return NB_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return NB_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return NB_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return NB_RIGHT;
    case SDL_CONTROLLER_BUTTON_A:          return NB_ACCEPT;
    case SDL_CONTROLLER_BUTTON_B:          return NB_BACK;
    default:                               return NB_COUNT;
    }
}

bool SdlGamepadKeyNavigation::padPressIsEcho(int nav)
{
    const qint64 now = m_NavClock.elapsed();
    if (m_KeyNavAt[nav] >= 0 && now - m_KeyNavAt[nav] <= INPUT_ECHO_WINDOW_MS) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[padnav] pad press %d dropped: the same key came from the keyboard %d ms earlier",
                    nav, (int)(now - m_KeyNavAt[nav]));
        m_KeyNavAt[nav] = -1;
        noteInputEcho();
        return true;
    }
    m_PadNavAt[nav] = now;
    return false;
}

bool SdlGamepadKeyNavigation::keyEventIsEcho(QKeyEvent* ke)
{
    const int key = ke->key();
    auto it = m_KeyDecisions.find(key);

    if (ke->type() == QEvent::KeyRelease) {
        if (it == m_KeyDecisions.end()) {
            return false;
        }
        const bool echo = it->echo;
        if (!ke->isAutoRepeat()) {
            m_KeyDecisions.erase(it);
        }
        return echo;
    }

    // ShortcutOverride or KeyPress. Auto-repeat, and the KeyPress that follows its own
    // ShortcutOverride, belong to a press already decided.
    const bool samePress = it != m_KeyDecisions.end()
            && (ke->isAutoRepeat() || it->timestamp == ke->timestamp());
    bool echo;
    if (samePress) {
        echo = it->echo;
    }
    else if (ke->isAutoRepeat()) {
        return false;
    }
    else {
        const int nav = navButtonForKey(key);
        if (nav == NB_COUNT || (ke->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier |
                                                   Qt::AltModifier | Qt::MetaModifier))) {
            if (it != m_KeyDecisions.end()) {
                m_KeyDecisions.erase(it);
            }
            return false;
        }

        const qint64 now = m_NavClock.elapsed();
        echo = m_PadNavAt[nav] >= 0 && now - m_PadNavAt[nav] <= INPUT_ECHO_WINDOW_MS;
        if (echo) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[padnav] keyboard key 0x%x dropped: the same press came from the pad %d ms earlier",
                        key, (int)(now - m_PadNavAt[nav]));
            m_PadNavAt[nav] = -1;
            noteInputEcho();
        }
        else {
            m_KeyNavAt[nav] = now;
        }
        // A new press overwrites whatever a lost release left behind.
        m_KeyDecisions.insert(key, KeyDecision { ke->timestamp(), echo });
    }

    if (echo) {
        // It was the pad: undo the keyboard reading InputHints may have taken from it.
        InputHints::get()->notePadInput();
        if (ke->type() == QEvent::ShortcutOverride) {
            // Accepted override = no shortcut fires (a popup's Escape would close it), and the
            // KeyPress that follows comes back here to be swallowed.
            ke->accept();
        }
    }
    return echo;
}

void SdlGamepadKeyNavigation::noteInputEcho()
{
    if (m_DesktopLayoutReported || ++m_EchoCount < DESKTOP_LAYOUT_ECHOES) {
        return;
    }
    m_DesktopLayoutReported = true;
    const bool menuHold = steamControllerConnected();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[padnav] %d echoes: Steam's Desktop Layout is sending the pad as keyboard too (notice shown%s)",
                m_EchoCount, menuHold ? ", Steam Controller: hold Menu" : "");
    emit steamDesktopLayoutDetected(menuHold);
}

bool SdlGamepadKeyNavigation::steamControllerConnected() const
{
    for (SDL_GameController* gc : m_Gamepads) {
        if (SDL_GameControllerGetVendor(gc) == USB_VENDOR_VALVE &&
                SDL_GameControllerGetProduct(gc) == USB_PRODUCT_STEAM_CONTROLLER) {
            return true;
        }
    }
    return false;
}

void SdlGamepadKeyNavigation::enable()
{
    if (m_Enabled) {
        return;
    }

    // We have to initialize and uninitialize this in enable()/disable()
    // because we need to get out of the way of the Session class. If it
    // doesn't get to reinitialize the GC subsystem, it won't get initial
    // arrival events. Additionally, there's a race condition between
    // our QML objects being destroyed and SDL being deinitialized that
    // this solves too.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) failed: %s",
                     SDL_GetError());
        return;
    }

    MappingManager mappingManager;
    mappingManager.applyMappings();

    // Drop all pending gamepad add events. SDL will generate these for us
    // on first init of the GC subsystem. We can't depend on them due to
    // overlapping lifetimes of SdlGamepadKeyNavigation instances, so we
    // will attach ourselves.
    //
    // NB: We use SDL_JoystickUpdate() instead of SDL_PumpEvents() because
    // the latter can do a bit more work that we want (like handling video
    // events that we intentionally do not want to process yet).
    SDL_JoystickUpdate();
    SDL_FlushEvent(SDL_CONTROLLERDEVICEADDED);

    // Open all currently attached game controllers
    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameController* gc = SDL_GameControllerOpen(i);
            if (gc != nullptr) {
                m_Gamepads.append(gc);
                logPadDevice("opened", gc);
            }
        }
    }

    {
        SDL_version linked;
        SDL_GetVersion(&linked);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[padnav] enable(): %d controller(s), %d joystick(s), SDL %d.%d.%d",
                    (int)m_Gamepads.size(), numJoysticks, linked.major, linked.minor, linked.patch);
    }

    updateControllerType();

    // Prompts start on the pad's side when one is connected, instead of waiting for the first
    // press to leave the keyboard ones. InputHints ignores this once real input has been seen.
    InputHints::get()->seedFromConnectedPads(!m_Gamepads.isEmpty());

    m_Enabled = true;

    // Start the polling timer if the window is focused
    updateTimerState();
}

void SdlGamepadKeyNavigation::disable()
{
    if (!m_Enabled) {
        return;
    }

    m_Enabled = false;
    updateTimerState();
    Q_ASSERT(!m_PollingTimer->isActive());

    while (!m_Gamepads.isEmpty()) {
        SDL_GameControllerClose(m_Gamepads[0]);
        m_Gamepads.removeAt(0);
    }

    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void SdlGamepadKeyNavigation::notifyWindowFocus(bool hasFocus)
{
    slDbg(QString("notifyWindowFocus(%1)  enabled=%2").arg(hasFocus).arg(m_Enabled));
    m_HasFocus = hasFocus;
    updateTimerState();
}

void SdlGamepadKeyNavigation::onPollingTimerFired()
{
    SDL_Event event;

    // Update joystick state without pumping other events (see enable() comment)
    SDL_JoystickUpdate();

    // Discard any pending button events on the first poll to avoid picking up
    // stale input data from the stream session (like the quit combo).
    if (m_FirstPoll) {
        SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
        SDL_FlushEvent(SDL_CONTROLLERBUTTONUP);
        m_FirstPoll = false;
    }

    // Peep events rather than polling to avoid calling SDL_PumpEvents()
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 1) {
        switch (event.type) {
        case SDL_QUIT:
            // SDL may send us a quit event since we initialize
            // the video subsystem on startup. If we get one,
            // forward it on for Qt to take care of.
            //
            // Drop any fullscreen top-level window back to windowed first, then
            // quit on the next event-loop tick. Destroying a fullscreen D3D11
            // swapchain in independent-flip/MPO state can hang the GPU/display
            // (see main.qml quitApp()); leaving fullscreen makes teardown safe.
            for (QWindow* w : QGuiApplication::topLevelWindows()) {
                if (w->visibility() == QWindow::FullScreen) {
                    w->setVisibility(QWindow::Windowed);
                }
            }
            QTimer::singleShot(0, QCoreApplication::instance(), []() {
                QCoreApplication::instance()->quit();
            });
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        {
            // Real controller input — as opposed to the Qt key events we are about to post for
            // it, which must never be mistaken for typing. See InputHints.
            InputHints::get()->notePadInput();

            QEvent::Type type =
                    event.type == SDL_CONTROLLERBUTTONDOWN ?
                        QEvent::Type::KeyPress : QEvent::Type::KeyRelease;

            // Swap face buttons if needed
            if (m_Prefs->swapFaceButtons) {
                switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A:
                    event.cbutton.button = SDL_CONTROLLER_BUTTON_B;
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
                    break;
                case SDL_CONTROLLER_BUTTON_X:
                    event.cbutton.button = SDL_CONTROLLER_BUTTON_Y;
                    break;
                case SDL_CONTROLLER_BUTTON_Y:
                    event.cbutton.button = SDL_CONTROLLER_BUTTON_X;
                    break;
                }
            }

            // Steam Input's keyboard copy of this press may already have acted (#24). A
            // dropped press drops its release too.
            const int nav = navButtonForPad(event.cbutton.button);
            if (nav != NB_COUNT) {
                if (type == QEvent::Type::KeyPress) {
                    m_PadNavDropped[nav] = padPressIsEcho(nav);
                    if (m_PadNavDropped[nav]) {
                        break;
                    }
                }
                else if (m_PadNavDropped[nav]) {
                    m_PadNavDropped[nav] = false;
                    break;
                }
            }

            switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (m_UiNavMode) {
                    // Back-tab
                    sendKey(type, Qt::Key_Tab, Qt::ShiftModifier);
                }
                else {
                    sendKey(type, Qt::Key_Up);
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (m_UiNavMode) {
                    sendKey(type, Qt::Key_Tab);
                }
                else {
                    sendKey(type, Qt::Key_Down);
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                sendKey(type, Qt::Key_Left);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                sendKey(type, Qt::Key_Right);
                break;
            case SDL_CONTROLLER_BUTTON_A:
                if (m_UiNavMode) {
                    sendKey(type, Qt::Key_Space);
                }
                else {
                    sendKey(type, Qt::Key_Return);
                }
                break;
            case SDL_CONTROLLER_BUTTON_B:
                sendKey(type, Qt::Key_Escape);
                break;
            case SDL_CONTROLLER_BUTTON_X:
                sendKey(type, Qt::Key_Menu);
                break;
            case SDL_CONTROLLER_BUTTON_Y:
                // HACK: We use this keycode to inform main.qml
                // to show the settings when Key_Menu is handled
                // by the control in focus.
                // NB: Only Y opens Settings — Start is intentionally NOT mapped
                // here (it used to also open Settings, which was unwanted).
                sendKey(type, Qt::Key_Hangup);
                break;
            // The shoulders cycle the host's streaming profile on Home and the tab in
            // Settings. They used to send Key_PageUp/PageDown, which is also the KEYBOARD
            // pair for cycling the HOST (the trigger legend prints "LT · PgUp"), so the
            // shoulders were delivered straight into the host handler and the profile cycle
            // became unreachable from the pad. Own inert keys, exactly like the triggers'
            // F14/F15 and Select's F13: nothing in Qt treats them as activation, so only our
            // explicit handlers consume them and no keyboard binding can collide again.
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                sendKey(type, Qt::Key_F16);
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                sendKey(type, Qt::Key_F17);
                break;
            case SDL_CONTROLLER_BUTTON_BACK:
                // Select / View / Create / − button. Used by the app list to open
                // per-game Customize. NB: NOT Qt::Key_Select — that key is treated
                // by Qt as an activation key (it would launch the focused app).
                // Key_F13 is inert and only our explicit handler consumes it.
                sendKey(type, Qt::Key_F13);
                break;
            case SDL_CONTROLLER_BUTTON_START:
                // Start / Menu / Options / + (6.0.0): pins the selected game on the host page.
                // Inert key for the same reason as Select's F13 — nothing in Qt acts on it, so
                // only the page's own handler does. Start still does NOT open Settings (see Y).
                sendKey(type, Qt::Key_F18);
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
                // Right stick click (6.1.0): moves the selected entry between GAMES and APPS on
                // the host page. Inert key, same reason as F13 and F18.
                sendKey(type, Qt::Key_F19);
                break;
            default:
                break;
            }
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
            SDL_GameController* gc = SDL_GameControllerOpen(event.cdevice.which);
            if (gc != nullptr) {
                // SDL_CONTROLLERDEVICEADDED can be reported multiple times for the same
                // gamepad in rare cases, because SDL doesn't fixup the device index in
                // the SDL_CONTROLLERDEVICEADDED event if an unopened gamepad disappears
                // before we've processed the add event.
                if (!m_Gamepads.contains(gc)) {
                    m_Gamepads.append(gc);
                    logPadDevice("added", gc);
                    updateControllerType();
                }
                else {
                    // We already have this game controller open
                    SDL_GameControllerClose(gc);
                }
            }
            break;
        }
    }

    // Handle analog sticks by polling
    for (auto gc : std::as_const(m_Gamepads)) {
        short leftX = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        short leftY = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);

        // The stick navigates as much as the D-pad does, so it is pad input as much as a
        // button is. Same threshold as the navigation below: resting drift must not count.
        if (qAbs(leftX) > 30000 || qAbs(leftY) > 30000) {
            InputHints::get()->notePadInput();
        }

        // The one direction this poll would step, in the order the branches below test them.
        const int stickNav = leftY < -30000 ? NB_UP
                           : leftY > 30000  ? NB_DOWN
                           : leftX < -30000 ? NB_LEFT
                           : leftX > 30000  ? NB_RIGHT
                           : NB_COUNT;

        if (SDL_GetTicks() - m_LastAxisNavigationEventTime < AXIS_NAVIGATION_REPEAT_DELAY) {
            // Do nothing
        }
        // A layout that sends the stick as arrow keys echoes it like the D-pad (#24). The repeat
        // delay still restarts, so a dropped step is not retried on the next poll.
        else if (stickNav != NB_COUNT && padPressIsEcho(stickNav)) {
            m_LastAxisNavigationEventTime = SDL_GetTicks();
        }
        else if (leftY < -30000) {
            if (m_UiNavMode) {
                // Back-tab
                sendKey(QEvent::Type::KeyPress, Qt::Key_Tab, Qt::ShiftModifier);
                sendKey(QEvent::Type::KeyRelease, Qt::Key_Tab, Qt::ShiftModifier);
            }
            else {
                sendKey(QEvent::Type::KeyPress, Qt::Key_Up);
                sendKey(QEvent::Type::KeyRelease, Qt::Key_Up);
            }

            m_LastAxisNavigationEventTime = SDL_GetTicks();
        }
        else if (leftY > 30000) {
            if (m_UiNavMode) {
                sendKey(QEvent::Type::KeyPress, Qt::Key_Tab);
                sendKey(QEvent::Type::KeyRelease, Qt::Key_Tab);
            }
            else {
                sendKey(QEvent::Type::KeyPress, Qt::Key_Down);
                sendKey(QEvent::Type::KeyRelease, Qt::Key_Down);
            }

            m_LastAxisNavigationEventTime = SDL_GetTicks();
        }
        else if (leftX < -30000) {
            sendKey(QEvent::Type::KeyPress, Qt::Key_Left);
            sendKey(QEvent::Type::KeyRelease, Qt::Key_Left);
            m_LastAxisNavigationEventTime = SDL_GetTicks();
        }
        else if (leftX > 30000) {
            sendKey(QEvent::Type::KeyPress, Qt::Key_Right);
            sendKey(QEvent::Type::KeyRelease, Qt::Key_Right);
            m_LastAxisNavigationEventTime = SDL_GetTicks();
        }

        // Triggers → previous / next host on the Home screen. Deliberately NOT
        // routed through the repeat timer above: a trigger pull is one discrete
        // step, not a direction you hold, so it is edge-detected with hysteresis
        // (press above 20000, release below 8000) — a trigger resting just at the
        // threshold would otherwise flip hosts continuously.
        //
        // Key_F14/F15 are inert like the Key_F13 used for Select: nothing in Qt
        // treats them as activation, so only our explicit handlers consume them.
        short leftTrigger  = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        short rightTrigger = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

        if (!m_LeftTriggerDown && leftTrigger > 20000) {
            m_LeftTriggerDown = true;
            InputHints::get()->notePadInput();
            sendKey(QEvent::Type::KeyPress, Qt::Key_F14);
            sendKey(QEvent::Type::KeyRelease, Qt::Key_F14);
        }
        else if (m_LeftTriggerDown && leftTrigger < 8000) {
            m_LeftTriggerDown = false;
        }

        if (!m_RightTriggerDown && rightTrigger > 20000) {
            m_RightTriggerDown = true;
            InputHints::get()->notePadInput();
            sendKey(QEvent::Type::KeyPress, Qt::Key_F15);
            sendKey(QEvent::Type::KeyRelease, Qt::Key_F15);
        }
        else if (m_RightTriggerDown && rightTrigger < 8000) {
            m_RightTriggerDown = false;
        }
    }
}

void SdlGamepadKeyNavigation::sendKey(QEvent::Type type, Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    QGuiApplication* app = static_cast<QGuiApplication*>(QGuiApplication::instance());
    QWindow* focusWindow = app->focusWindow();
    slDbg(QString("sendKey: key=0x%1 type=%2 uiNav=%3 focusWindow=%4")
              .arg(QString::number((int)key, 16))
              .arg((int)type)
              .arg(m_UiNavMode ? 1 : 0)
              .arg(focusWindow ? "OK" : "NULL"));
    if (focusWindow != nullptr) {
        QKeyEvent keyPressEvent(type, key, modifiers);
        app->sendEvent(focusWindow, &keyPressEvent);
    }
}

void SdlGamepadKeyNavigation::updateTimerState()
{
    if (m_PollingTimer->isActive() && (!m_HasFocus || !m_Enabled)) {
        m_PollingTimer->stop();
        slDbg("polling timer STOPPED");
    }
    else if (!m_PollingTimer->isActive() && m_HasFocus && m_Enabled) {
        // Flush events on the first poll
        m_FirstPoll = true;

        // Poll every 50 ms for a new joystick event
        m_PollingTimer->start(50);
        slDbg("polling timer STARTED (50ms)");
    }
}

void SdlGamepadKeyNavigation::setUiNavMode(bool uiNavMode)
{
    m_UiNavMode = uiNavMode;
}

int SdlGamepadKeyNavigation::getConnectedGamepads()
{
    Q_ASSERT(m_Enabled);

    int count = 0;
    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; i++) {
        if (SDL_IsGameController(i)) {
            count++;
        }
    }

    return count;
}
