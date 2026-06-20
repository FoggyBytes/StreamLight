#include "sdlgamepadkeynavigation.h"

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

SdlGamepadKeyNavigation::SdlGamepadKeyNavigation(StreamingPreferences* prefs)
    : m_Prefs(prefs),
      m_Enabled(false),
      m_UiNavMode(false),
      m_FirstPoll(false),
      m_HasFocus(false),
      m_LastAxisNavigationEventTime(0),
      m_ControllerType(QStringLiteral("none")),
      m_InputMode(QStringLiteral("pointer")),
      m_HasLastMousePos(false)
{
    m_PollingTimer = new QTimer(this);
    connect(m_PollingTimer, &QTimer::timeout, this, &SdlGamepadKeyNavigation::onPollingTimerFired);

    if (qGuiApp != nullptr) {
        qGuiApp->installEventFilter(this);
    }
}

void SdlGamepadKeyNavigation::dbgLog(const QString& msg)
{
    slDbg("QML: " + msg);
}

void SdlGamepadKeyNavigation::simulateKey(int qtKey)
{
    Qt::Key key = static_cast<Qt::Key>(qtKey);
    sendKey(QEvent::Type::KeyPress,   key);
    sendKey(QEvent::Type::KeyRelease, key);
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
            newType = QStringLiteral("generic");
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
                setInputMode(QStringLiteral("pointer"));
            }
        }
        break;
    }
    case QEvent::MouseButtonPress:
        setInputMode(QStringLiteral("pointer"));
        break;
    case QEvent::KeyPress:
        setInputMode(QStringLiteral("key"));
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
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
            }
        }
    }

    updateControllerType();

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
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                // Used by SettingsScreen to switch to the previous tab.
                sendKey(type, Qt::Key_PageUp);
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                // Used by SettingsScreen to switch to the next tab.
                sendKey(type, Qt::Key_PageDown);
                break;
            case SDL_CONTROLLER_BUTTON_BACK:
                // Select / View / Create / − button. Used by the app list to open
                // per-game Customize. NB: NOT Qt::Key_Select — that key is treated
                // by Qt as an activation key (it would launch the focused app).
                // Key_F13 is inert and only our explicit handler consumes it.
                sendKey(type, Qt::Key_F13);
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
        if (SDL_GetTicks() - m_LastAxisNavigationEventTime < AXIS_NAVIGATION_REPEAT_DELAY) {
            // Do nothing
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
