#include "servicehost.h"

#include "eventwatch.h"
#include "servicecore.h"
#include "servicelog.h"

#include <windows.h>

#include <cstdio>
#include <mutex>

namespace
{

// With no TV paired the service has nothing to do: it stops after this long without a request
// from StreamLight, which starts it again when it needs it.
constexpr ULONGLONG k_IdleWithoutTvMs = 10 * 60 * 1000;

// The core the power notices go to. The lock keeps a notice from running into its teardown.
std::mutex s_CoreLock;
ServiceCore* s_Core = nullptr;

// Sleep and wake leave the TV as it is (03/11/2026): they only matter to the restart/shutdown
// bookkeeping. See PowerPolicy for why the TV no longer follows the screen.
void handlePowerEvent(DWORD type)
{
    std::lock_guard<std::mutex> lock(s_CoreLock);
    if (!s_Core)
        return;
    if (type == PBT_APMSUSPEND)
        s_Core->suspending();
    else if (type == PBT_APMRESUMEAUTOMATIC)
        s_Core->resumed();
}

void handleShutdown(const QString& source)
{
    QString type;
    const bool restart = EventWatch::latestShutdownIsRestart(&type);
    std::lock_guard<std::mutex> lock(s_CoreLock);
    if (s_Core)
        s_Core->shutdownRequested(source, restart, type);
}

void attach(ServiceCore* core)
{
    std::lock_guard<std::mutex> lock(s_CoreLock);
    s_Core = core;
}

// ── As a service ─────────────────────────────────────────────────────────────

SERVICE_STATUS_HANDLE s_StatusHandle = nullptr;
SERVICE_STATUS s_Status = {};
HANDLE s_StopEvent = nullptr;
DWORD s_CheckPoint = 1;

void report(DWORD state, DWORD waitHint = 0)
{
    s_Status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s_Status.dwCurrentState = state;
    s_Status.dwControlsAccepted = state == SERVICE_RUNNING
            ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN | SERVICE_ACCEPT_POWEREVENT
            : 0;
    s_Status.dwWin32ExitCode = NO_ERROR;
    s_Status.dwWaitHint = waitHint;
    s_Status.dwCheckPoint = state == SERVICE_RUNNING || state == SERVICE_STOPPED ? 0 : s_CheckPoint++;
    SetServiceStatus(s_StatusHandle, &s_Status);
}

DWORD WINAPI serviceHandler(DWORD control, DWORD eventType, LPVOID, LPVOID)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
        report(SERVICE_STOP_PENDING, 3000);
        SetEvent(s_StopEvent);
        return NO_ERROR;
    case SERVICE_CONTROL_PRESHUTDOWN:
        // The last notice with the network still up; the TV gets its POWER before we stop.
        report(SERVICE_STOP_PENDING, 8000);
        handleShutdown(QStringLiteral("preshutdown"));
        SetEvent(s_StopEvent);
        return NO_ERROR;
    case SERVICE_CONTROL_POWEREVENT:
        handlePowerEvent(eventType);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI serviceMain(DWORD, LPWSTR*)
{
    s_StatusHandle = RegisterServiceCtrlHandlerExW(ServiceHost::ServiceName, serviceHandler, nullptr);
    if (!s_StatusHandle)
        return;
    report(SERVICE_START_PENDING, 5000);
    s_StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    ServiceCore core;
    if (!core.start(false)) {
        report(SERVICE_STOPPED);
        return;
    }
    attach(&core);
    report(SERVICE_RUNNING);
    for (;;) {
        if (WaitForSingleObject(s_StopEvent, 30000) == WAIT_OBJECT_0)
            break;
        if (!core.hasTv() && GetTickCount64() - core.lastActivity() > k_IdleWithoutTvMs) {
            ServiceLog::write(QStringLiteral("service: no TV paired and nothing asked for 10 minutes, stopping"));
            break;
        }
    }

    attach(nullptr);
    core.stop();
    CloseHandle(s_StopEvent);
    report(SERVICE_STOPPED);
}

// ── In the foreground ────────────────────────────────────────────────────────

DWORD s_MainThread = 0;

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_POWERBROADCAST:
        handlePowerEvent(static_cast<DWORD>(wParam));
        return TRUE;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wParam)
            handleShutdown(QStringLiteral("end of session"));
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

BOOL WINAPI consoleHandler(DWORD)
{
    PostThreadMessageW(s_MainThread, WM_QUIT, 0, 0);
    return TRUE;
}

}

namespace ServiceHost
{

int runAsService()
{
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(ServiceName), serviceMain },
        { nullptr, nullptr },
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        std::fprintf(stderr, "Not started by the service manager (error %lu). Use --run or --console.\n", GetLastError());
        return 1;
    }
    return 0;
}

int runInForeground()
{
    ServiceCore core;
    if (!core.start(true))
        return 1;
    attach(&core);

    // A real top-level window, never shown: a message-only window gets no broadcasts, and
    // WM_POWERBROADCAST is one.
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"StreamLightServiceWindow";
    RegisterClassW(&windowClass);
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"StreamLight service", WS_OVERLAPPED,
                                  0, 0, 0, 0, nullptr, nullptr, windowClass.hInstance, nullptr);

    s_MainThread = GetCurrentThreadId();
    SetConsoleCtrlHandler(consoleHandler, TRUE);
    ServiceLog::write(QStringLiteral("service: running, Ctrl+C to stop"));
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DestroyWindow(window);
    attach(nullptr);
    core.stop();
    return 0;
}

}
