#include "signinlaunch.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <SDL.h>

#ifdef Q_OS_WIN32
#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#include <taskschd.h>
#include <wtsapi32.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
#endif

namespace {

bool s_LaunchedAtSignIn = false;

#ifdef Q_OS_WIN32

// At the root folder, like StreamTweak's "StreamTweak Startup". The uninstaller deletes this
// exact name (StreamLight.iss, [UninstallRun]).
const wchar_t* const k_TaskName = L"StreamLight Startup";

// A BSTR for the length of one call.
class Bstr
{
public:
    explicit Bstr(const QString& s)
        : m_B(SysAllocString(reinterpret_cast<const OLECHAR*>(s.utf16()))) {}
    explicit Bstr(const wchar_t* s) : m_B(SysAllocString(s)) {}
    ~Bstr() { SysFreeString(m_B); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return m_B; }

private:
    BSTR m_B;
};

// COM on this thread for the length of one call. The GUI thread already has an STA (Qt's
// OLE initialisation), so this is normally S_FALSE and only balances itself.
class ComScope
{
public:
    ComScope() : m_Hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() { if (SUCCEEDED(m_Hr)) CoUninitialize(); }

private:
    HRESULT m_Hr;
};

QString hrText(const char* what, HRESULT hr)
{
    return QStringLiteral("%1 failed (0x%2)").arg(QLatin1String(what))
            .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

bool connectService(ComPtr<ITaskService>& svc, QString* error)
{
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&svc));
    if (FAILED(hr)) {
        if (error) *error = hrText("Task Scheduler", hr);
        return false;
    }
    VARIANT empty;
    VariantInit(&empty);
    hr = svc->Connect(empty, empty, empty, empty);
    if (FAILED(hr)) {
        if (error) *error = hrText("Task Scheduler connection", hr);
        return false;
    }
    return true;
}

bool rootFolder(ITaskService* svc, ComPtr<ITaskFolder>& folder, QString* error)
{
    HRESULT hr = svc->GetFolder(Bstr(L"\\"), &folder);
    if (FAILED(hr)) {
        if (error) *error = hrText("Task Scheduler folder", hr);
        return false;
    }
    return true;
}

// DOMAIN\user of this session, as the task's principal and logon trigger want it. Only this
// user's sign-in starts the app, never everyone's.
QString currentUser()
{
    wchar_t name[512];
    ULONG len = ARRAYSIZE(name);
    if (GetUserNameExW(NameSamCompatible, name, &len)) {
        return QString::fromWCharArray(name, static_cast<int>(len));
    }
    return qEnvironmentVariable("USERDOMAIN") + QLatin1Char('\\') + qEnvironmentVariable("USERNAME");
}

QString thisExe()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

/**
 * The exe the registered task starts, or an empty string when there is no ENABLED task.
 * A disabled task counts as absent: it would not run, and showing On for it would leave the
 * user with no start at sign-in and a switch saying otherwise.
 */
QString readTaskExe()
{
    ComScope com;
    ComPtr<ITaskService> svc;
    ComPtr<ITaskFolder> folder;
    if (!connectService(svc, nullptr) || !rootFolder(svc.Get(), folder, nullptr)) return QString();

    ComPtr<IRegisteredTask> task;
    if (FAILED(folder->GetTask(Bstr(k_TaskName), &task))) return QString();    // missing: the common case

    VARIANT_BOOL on = VARIANT_FALSE;
    if (FAILED(task->get_Enabled(&on)) || on != VARIANT_TRUE) return QString();

    ComPtr<ITaskDefinition> def;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    LONG count = 0;
    if (FAILED(task->get_Definition(&def)) || FAILED(def->get_Actions(&actions))
            || FAILED(actions->get_Count(&count)) || count < 1
            || FAILED(actions->get_Item(1, &action))           // 1-based
            || FAILED(action.As(&exec))) {
        return QString();
    }
    BSTR path = nullptr;
    if (FAILED(exec->get_Path(&path)) || path == nullptr) return QString();
    QString result = QString::fromWCharArray(path);
    SysFreeString(path);
    return result;
}

/**
 * Creates or replaces the task. ⚠️ Every setting assigned below is there because Task
 * Scheduler's default would break the feature, mostly in ways that show up days later or on
 * someone else's machine — the same list StreamTweak's priority start was built on (§66):
 *
 *   · Priority defaults to 7, which is BELOW_NORMAL: worse than the Run key, during the very
 *     boot storm this is meant to beat. 5 is NORMAL;
 *   · ExecutionTimeLimit defaults to 3 days, after which Task Scheduler TERMINATES the app;
 *   · DisallowStartIfOnBatteries / StopIfGoingOnBatteries default to true: on a handheld on
 *     battery — the ROG Ally this is for — the task would never start.
 */
bool registerTask(const QString& exe, QString* error)
{
    ComScope com;
    ComPtr<ITaskService> svc;
    ComPtr<ITaskFolder> folder;
    if (!connectService(svc, error) || !rootFolder(svc.Get(), folder, error)) return false;

    const QString user = currentUser();
    HRESULT hr;

    ComPtr<ITaskDefinition> td;
    if (FAILED(hr = svc->NewTask(0, &td))) { if (error) *error = hrText("NewTask", hr); return false; }

    ComPtr<IRegistrationInfo> info;
    if (SUCCEEDED(td->get_RegistrationInfo(&info))) {
        info->put_Author(Bstr(L"FoggyBytes"));
        info->put_Description(Bstr(L"Starts StreamLight when you sign in to Windows and asks whether to open "
                                   L"it or go to the desktop. Managed from StreamLight's Settings."));
    }

    ComPtr<IPrincipal> principal;
    if (FAILED(hr = td->get_Principal(&principal))) { if (error) *error = hrText("Principal", hr); return false; }
    principal->put_UserId(Bstr(user));
    principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    // Never "highest privileges": an elevated StreamLight started without a UAC prompt.
    principal->put_RunLevel(TASK_RUNLEVEL_LUA);

    ComPtr<ITaskSettings> settings;
    if (FAILED(hr = td->get_Settings(&settings))) { if (error) *error = hrText("Settings", hr); return false; }
    settings->put_Enabled(VARIANT_TRUE);
    settings->put_Hidden(VARIANT_FALSE);
    settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    settings->put_ExecutionTimeLimit(Bstr(L"PT0S"));
    settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    settings->put_RunOnlyIfIdle(VARIANT_FALSE);
    settings->put_RunOnlyIfNetworkAvailable(VARIANT_FALSE);
    settings->put_StartWhenAvailable(VARIANT_FALSE);
    settings->put_WakeToRun(VARIANT_FALSE);
    settings->put_Priority(5);
    ComPtr<IIdleSettings> idle;
    if (SUCCEEDED(settings->get_IdleSettings(&idle))) {
        idle->put_StopOnIdleEnd(VARIANT_FALSE);
    }

    ComPtr<ITriggerCollection> triggers;
    ComPtr<ITrigger> trigger;
    ComPtr<ILogonTrigger> logon;
    if (FAILED(hr = td->get_Triggers(&triggers)) || FAILED(hr = triggers->Create(TASK_TRIGGER_LOGON, &trigger))
            || FAILED(hr = trigger.As(&logon))) {
        if (error) *error = hrText("Logon trigger", hr);
        return false;
    }
    logon->put_Id(Bstr(L"AtLogon"));
    logon->put_Enabled(VARIANT_TRUE);
    logon->put_UserId(Bstr(user));
    logon->put_Delay(Bstr(L"PT0S"));

    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    if (FAILED(hr = td->get_Actions(&actions)) || FAILED(hr = actions->Create(TASK_ACTION_EXEC, &action))
            || FAILED(hr = action.As(&exec))) {
        if (error) *error = hrText("Action", hr);
        return false;
    }
    exec->put_Path(Bstr(exe));
    exec->put_Arguments(Bstr(QString::fromLatin1(SignInLaunch::argument())));
    exec->put_WorkingDirectory(Bstr(QDir::toNativeSeparators(QFileInfo(exe).absolutePath())));

    VARIANT empty;
    VariantInit(&empty);
    ComPtr<IRegisteredTask> registered;
    hr = folder->RegisterTaskDefinition(Bstr(k_TaskName), td.Get(), TASK_CREATE_OR_UPDATE,
                                        empty, empty,                   // user and password: the principal's
                                        TASK_LOGON_INTERACTIVE_TOKEN,
                                        empty,                          // sddl: inherited
                                        &registered);
    if (FAILED(hr)) {
        if (error) *error = hrText("RegisterTaskDefinition", hr);
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[signin] registered logon task for %s",
                qPrintable(exe));
    return true;
}

bool deleteTask(QString* error)
{
    ComScope com;
    ComPtr<ITaskService> svc;
    ComPtr<ITaskFolder> folder;
    if (!connectService(svc, error) || !rootFolder(svc.Get(), folder, error)) return false;

    HRESULT hr = folder->DeleteTask(Bstr(k_TaskName), 0);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        if (error) *error = hrText("DeleteTask", hr);
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[signin] deleted logon task");
    return true;
}

#endif

/**
 * The session this process runs in is behind the lock screen. WTSSessionInfoEx's flags, read
 * for our own session: WTS_SESSIONSTATE_LOCK while the lock screen covers it — also right after
 * an automatic sign-in that locks at once. (The flag is swapped on Windows 7 / Server 2008 R2
 * only; Qt 6 needs Windows 10.) False when it cannot be read: better a choice nobody saw than
 * one that never comes.
 */
bool sessionLocked()
{
#ifdef Q_OS_WIN32
    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) return false;

    LPWSTR buffer = nullptr;
    DWORD bytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSSessionInfoEx, &buffer, &bytes)
            || buffer == nullptr) {
        return false;
    }
    const auto* info = reinterpret_cast<const WTSINFOEXW*>(buffer);
    const bool locked = info->Level == 1 && info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK;
    WTSFreeMemory(buffer);
    return locked;
#else
    return false;
#endif
}

} // namespace

SignInLaunch* SignInLaunch::get(QQmlEngine*)
{
    static SignInLaunch* instance = new SignInLaunch();
    return instance;
}

void SignInLaunch::setLaunchedAtSignIn(bool atSignIn)
{
    s_LaunchedAtSignIn = atSignIn;
}

const char* SignInLaunch::argument()
{
    return "--sign-in";
}

bool SignInLaunch::launchedAtSignIn() const
{
    return s_LaunchedAtSignIn;
}

SignInLaunch::SignInLaunch(QObject* parent)
    : QObject(parent),
      m_Holding(s_LaunchedAtSignIn)
{
#ifdef Q_OS_WIN32
    const QString registered = readTaskExe();
    m_Enabled = !registered.isEmpty();

    // The task stores an absolute path: StreamLight installed somewhere else would leave it
    // starting an exe that is gone, silently, at every sign-in. Point it at this one.
    if (m_Enabled && registered.compare(thisExe(), Qt::CaseInsensitive) != 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[signin] task starts %s, moving it to %s",
                    qPrintable(registered), qPrintable(thisExe()));
        QString error;
        if (!registerTask(thisExe(), &error)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[signin] %s", qPrintable(error));
        }
    }
#endif
    if (s_LaunchedAtSignIn) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[signin] started at sign-in: hosts held until the choice");

        // Behind the lock screen (automatic sign-in, an update restart): wait for the unlock.
        if (sessionLocked()) {
            m_WaitingForUnlock = true;
            m_LockedSince.start();
            m_UnlockPoll = new QTimer(this);
            m_UnlockPoll->setInterval(500);
            connect(m_UnlockPoll, &QTimer::timeout, this, &SignInLaunch::checkUnlocked);
            m_UnlockPoll->start();
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[signin] the session is locked: waiting for the unlock");
        }
    }
}

void SignInLaunch::checkUnlocked()
{
    if (sessionLocked()) return;
    m_UnlockPoll->stop();
    m_WaitingForUnlock = false;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[signin] unlocked after %lld s: the choice starts",
                static_cast<long long>(m_LockedSince.elapsed() / 1000));
    emit waitingForUnlockChanged();
}

bool SignInLaunch::setEnabled(bool enabled)
{
#ifdef Q_OS_WIN32
    QString error;
    const bool ok = enabled ? registerTask(thisExe(), &error) : deleteTask(&error);
    if (!ok) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[signin] %s", qPrintable(error));
    }
    m_LastError = ok ? QString() : error;
    // The truth is Task Scheduler's, read back rather than assumed from what was asked.
    m_Enabled = !readTaskExe().isEmpty();
    emit enabledChanged();
    return ok;
#else
    Q_UNUSED(enabled)
    return false;
#endif
}

void SignInLaunch::release()
{
    if (!m_Holding) return;
    m_Holding = false;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[signin] StreamLight chosen: hosts released");
    emit holdingChanged();
}
