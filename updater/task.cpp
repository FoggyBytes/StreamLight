#include "task.h"
#include "common.h"

#include <windows.h>
#include <ole2.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace up {

namespace {

class Bstr
{
public:
    explicit Bstr(const QString& s) : m_B(SysAllocString(reinterpret_cast<const OLECHAR*>(s.utf16()))) {}
    explicit Bstr(const wchar_t* s) : m_B(SysAllocString(s)) {}
    ~Bstr() { SysFreeString(m_B); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return m_B; }
private:
    BSTR m_B;
};

QString hrText(const char* what, HRESULT hr)
{
    return QStringLiteral("%1 failed (0x%2)").arg(QLatin1String(what))
            .arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

bool rootFolder(ComPtr<ITaskService>& svc, ComPtr<ITaskFolder>& folder, QString* error)
{
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&svc));
    if (FAILED(hr)) { if (error) *error = hrText("Task Scheduler", hr); return false; }
    VARIANT empty;
    VariantInit(&empty);
    if (FAILED(hr = svc->Connect(empty, empty, empty, empty))) {
        if (error) *error = hrText("Task Scheduler connection", hr);
        return false;
    }
    if (FAILED(hr = svc->GetFolder(Bstr(L"\\"), &folder))) {
        if (error) *error = hrText("Task Scheduler folder", hr);
        return false;
    }
    return true;
}

QString sidOf(const QString& user)
{
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(sid);
    wchar_t domain[256];
    DWORD domainSize = ARRAYSIZE(domain);
    SID_NAME_USE use;
    if (!LookupAccountNameW(nullptr, wide(user).c_str(), sid, &sidSize, domain, &domainSize, &use)) {
        return QString();
    }
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(sid, &text)) return QString();
    const QString result = QString::fromWCharArray(text);
    LocalFree(text);
    return result;
}

} // namespace

bool registerTask(const QString& appDir, const QString& user, QString* error)
{
    const QString sid = sidOf(user);
    if (sid.isEmpty()) { if (error) *error = QStringLiteral("unknown account %1").arg(user); return false; }

    ComPtr<ITaskService> svc;
    ComPtr<ITaskFolder> folder;
    if (!rootFolder(svc, folder, error)) return false;

    HRESULT hr;
    ComPtr<ITaskDefinition> td;
    if (FAILED(hr = svc->NewTask(0, &td))) { if (error) *error = hrText("NewTask", hr); return false; }

    ComPtr<IRegistrationInfo> info;
    if (SUCCEEDED(td->get_RegistrationInfo(&info))) {
        info->put_Author(Bstr(L"FoggyBytes"));
        info->put_Description(Bstr(L"Installs a StreamLight update started from StreamLight's Settings, "
                                   L"after checking it against the release on GitHub. Takes no arguments."));
    }

    ComPtr<IPrincipal> principal;
    if (FAILED(hr = td->get_Principal(&principal))) { if (error) *error = hrText("Principal", hr); return false; }
    principal->put_UserId(Bstr(user));
    principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);

    // ⚠️ As for StreamLight Startup (§66): Task Scheduler's defaults would break this.
    ComPtr<ITaskSettings> settings;
    if (FAILED(hr = td->get_Settings(&settings))) { if (error) *error = hrText("Settings", hr); return false; }
    settings->put_Enabled(VARIANT_TRUE);
    settings->put_AllowDemandStart(VARIANT_TRUE);
    settings->put_Hidden(VARIANT_FALSE);
    settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    settings->put_ExecutionTimeLimit(Bstr(L"PT30M"));
    settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    settings->put_RunOnlyIfIdle(VARIANT_FALSE);
    settings->put_RunOnlyIfNetworkAvailable(VARIANT_FALSE);
    settings->put_StartWhenAvailable(VARIANT_FALSE);
    settings->put_WakeToRun(VARIANT_FALSE);
    settings->put_Priority(5);
    ComPtr<IIdleSettings> idle;
    if (SUCCEEDED(settings->get_IdleSettings(&idle))) idle->put_StopOnIdleEnd(VARIANT_FALSE);

    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    if (FAILED(hr = td->get_Actions(&actions)) || FAILED(hr = actions->Create(TASK_ACTION_EXEC, &action))
            || FAILED(hr = action.As(&exec))) {
        if (error) *error = hrText("Action", hr);
        return false;
    }
    exec->put_Path(Bstr(nativePath(appDir + QStringLiteral("\\StreamLightUpdater.exe"))));
    exec->put_WorkingDirectory(Bstr(nativePath(appDir)));

    // Read and execute for the user: starting it. Nothing for anyone else but SYSTEM and the
    // administrators, who would not need a task to do anything this one does.
    const QString sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;%1)").arg(sid);
    VARIANT vUser, vSddl, empty;
    VariantInit(&vUser);
    VariantInit(&vSddl);
    VariantInit(&empty);
    vUser.vt = VT_BSTR;
    vUser.bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(user.utf16()));
    vSddl.vt = VT_BSTR;
    vSddl.bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(sddl.utf16()));
    ComPtr<IRegisteredTask> registered;
    hr = folder->RegisterTaskDefinition(Bstr(k_TaskName), td.Get(), TASK_CREATE_OR_UPDATE,
                                        vUser, empty, TASK_LOGON_INTERACTIVE_TOKEN, vSddl, &registered);
    VariantClear(&vUser);
    VariantClear(&vSddl);
    if (FAILED(hr)) { if (error) *error = hrText("RegisterTaskDefinition", hr); return false; }
    return true;
}

} // namespace up
