#pragma once

#include <QString>

#include <windows.h>
#include <winevt.h>

#include <functional>

// Two entries of the System log that the service needs and that no service control carries:
//
//  • User32 1074 — a shutdown or restart was requested (Start menu, shutdown.exe, Windows Update).
//    Logged before anything closes. With Fast Startup a "shut down" stops no service, so
//    SERVICE_CONTROL_PRESHUTDOWN never comes: this event is then the only notice there is.
//  • Kernel-Boot 27 — the boot type, logged on every boot, Fast Startup boots included (for the
//    service those look like a resume, not a start).
class EventWatch
{
public:
    EventWatch(std::function<void(bool restart, const QString& type)> onShutdownRequested,
               std::function<void(int bootType)> onBoot);
    ~EventWatch();
    EventWatch(const EventWatch&) = delete;
    EventWatch& operator=(const EventWatch&) = delete;

    // Whether the latest 1074 of the last few minutes asks for a restart. For
    // SERVICE_CONTROL_PRESHUTDOWN, which does not say. *type gets the event's own word for it.
    static bool latestShutdownIsRestart(QString* type);

    // 1074 carries the type in the language of Windows ("Riavvia", "Spegni"…). The same words are
    // string 708 ("restart") of user32.dll, read in the same language — measured on it-IT, 30/10/2026:
    // 706 "Arresta il sistema", 707 "Spegni", 708 "Riavvia".
    static bool isRestartType(const QString& type);

private:
    static DWORD WINAPI onShutdownEvent(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID context, EVT_HANDLE event);
    static DWORD WINAPI onBootEvent(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID context, EVT_HANDLE event);

    std::function<void(bool, const QString&)> m_OnShutdown;
    std::function<void(int)> m_OnBoot;
    EVT_HANDLE m_ShutdownSubscription = nullptr;
    EVT_HANDLE m_BootSubscription = nullptr;
};
