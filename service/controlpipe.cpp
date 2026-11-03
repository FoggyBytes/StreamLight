#include "controlpipe.h"

#include "servicelog.h"

#include <QJsonDocument>

#include <sddl.h>

namespace
{

// SYSTEM, administrators and LOCAL SERVICE (the service itself): everything; interactive users:
// read and write, enough to connect as a client.
const wchar_t k_Sddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)(A;;GRGW;;;IU)";

// A request line longer than this is not one of ours.
constexpr int k_MaxLine = 16 * 1024;

}

ControlPipe::ControlPipe(Handler handler)
    : m_Handler(std::move(handler))
{
    m_LastActivity = GetTickCount64();
}

ControlPipe::~ControlPipe()
{
    stop();
}

bool ControlPipe::start()
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(k_Sddl, SDDL_REVISION_1, &descriptor, nullptr)) {
        ServiceLog::write(QStringLiteral("pipe: bad security descriptor (%1)").arg(GetLastError()));
        return false;
    }
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), descriptor, FALSE };
    m_Pipe = CreateNamedPipeW(Name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                              1, 64 * 1024, 64 * 1024, 0, &attributes);
    const DWORD error = GetLastError();
    LocalFree(descriptor);
    if (m_Pipe == INVALID_HANDLE_VALUE) {
        // FILE_FLAG_FIRST_PIPE_INSTANCE: someone else already holds the name.
        ServiceLog::write(QStringLiteral("pipe: cannot create %1 (%2)").arg(QString::fromWCharArray(Name)).arg(error));
        return false;
    }
    m_Thread = std::thread(&ControlPipe::run, this);
    return true;
}

void ControlPipe::stop()
{
    if (m_Stop.exchange(true))
        return;
    if (m_Pipe != INVALID_HANDLE_VALUE) {
        // Wake ConnectNamedPipe (or a blocked read) so the thread can see m_Stop.
        CancelSynchronousIo(m_Thread.native_handle());
        HANDLE self = CreateFileW(Name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (self != INVALID_HANDLE_VALUE)
            CloseHandle(self);
    }
    if (m_Thread.joinable())
        m_Thread.join();
    if (m_Pipe != INVALID_HANDLE_VALUE)
        CloseHandle(m_Pipe);
    m_Pipe = INVALID_HANDLE_VALUE;
}

bool ControlPipe::clientGone() const
{
    DWORD available = 0;
    return !PeekNamedPipe(m_Pipe, nullptr, 0, nullptr, &available, nullptr);
}

bool ControlPipe::writeLine(const QJsonObject& message)
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    DWORD written = 0;
    return WriteFile(m_Pipe, line.constData(), static_cast<DWORD>(line.size()), &written, nullptr)
           && written == static_cast<DWORD>(line.size());
}

void ControlPipe::run()
{
    while (!m_Stop) {
        if (!ConnectNamedPipe(m_Pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            if (m_Stop)
                break;
            Sleep(200);
            continue;
        }
        QByteArray buffer;
        char chunk[4096];
        bool alive = true;
        while (alive && !m_Stop) {
            DWORD read = 0;
            if (!ReadFile(m_Pipe, chunk, sizeof(chunk), &read, nullptr) || read == 0)
                break;
            buffer.append(chunk, static_cast<int>(read));
            if (buffer.size() > k_MaxLine && !buffer.contains('\n')) {
                ServiceLog::write(QStringLiteral("pipe: oversized request dropped"));
                break;
            }
            int newline;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                const QByteArray line = buffer.left(newline).trimmed();
                buffer.remove(0, newline + 1);
                if (line.isEmpty())
                    continue;
                m_LastActivity = GetTickCount64();
                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
                QJsonObject reply;
                if (!document.isObject()) {
                    reply = { { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("not JSON") } };
                }
                else {
                    reply = m_Handler(document.object(), [this](const QJsonObject& event) { writeLine(event); });
                    if (!reply.contains(QStringLiteral("ok")))
                        reply.insert(QStringLiteral("ok"), true);
                }
                if (!writeLine(reply)) {
                    alive = false;
                    break;
                }
            }
        }
        FlushFileBuffers(m_Pipe);
        DisconnectNamedPipe(m_Pipe);
    }
}
