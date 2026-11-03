#pragma once

#include <QJsonObject>

#include <windows.h>

#include <atomic>
#include <functional>
#include <thread>

// \\.\pipe\StreamLight: how StreamLight talks to the service. One client at a time, one JSON object
// per line each way. A request gets exactly one reply that carries "ok"; a long request (pairing)
// may send event lines before it, each carrying "event".
//
// Who may connect: the service's own account, SYSTEM, administrators and the signed-in (interactive)
// user — never another PC (PIPE_REJECT_REMOTE_CLIENTS). One instance, created once and reused for
// every client, so there is no moment in which another process could take the name.
class ControlPipe
{
public:
    using Send = std::function<void(const QJsonObject&)>;
    using Handler = std::function<QJsonObject(const QJsonObject& request, const Send& event)>;

    static constexpr wchar_t Name[] = L"\\\\.\\pipe\\StreamLight";

    explicit ControlPipe(Handler handler);
    ~ControlPipe();
    ControlPipe(const ControlPipe&) = delete;
    ControlPipe& operator=(const ControlPipe&) = delete;

    bool start();
    void stop();

    // When the last request arrived (GetTickCount64), for the service's idle stop without a TV.
    ULONGLONG lastActivity() const { return m_LastActivity; }

    // The client of the request being handled has hung up — for a long request (pairing) to give up.
    bool clientGone() const;

private:
    void run();
    bool writeLine(const QJsonObject& message);

    Handler m_Handler;
    HANDLE m_Pipe = INVALID_HANDLE_VALUE;
    std::thread m_Thread;
    std::atomic<bool> m_Stop { false };
    std::atomic<ULONGLONG> m_LastActivity { 0 };
};
