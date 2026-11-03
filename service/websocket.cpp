#include "websocket.h"

#include <vector>

namespace
{

QString winHttpError(const char* what, DWORD code)
{
    return QStringLiteral("%1 failed (%2)").arg(QLatin1String(what)).arg(code);
}

}

WebSocket::~WebSocket()
{
    close();
}

bool WebSocket::open(const QString& host, quint16 port, bool secure, int timeoutMs, QString* error,
                     const QString& path)
{
    close();
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        m_Inbox.clear();
        m_CloseReason.clear();
    }

    m_Session = WinHttpOpen(L"StreamLight", WINHTTP_ACCESS_TYPE_NO_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!m_Session) {
        *error = winHttpError("WinHttpOpen", GetLastError());
        return false;
    }
    WinHttpSetTimeouts(m_Session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    const std::wstring wideHost = host.toStdWString();
    m_Connection = WinHttpConnect(m_Session, wideHost.c_str(), port, 0);
    if (!m_Connection) {
        *error = winHttpError("WinHttpConnect", GetLastError());
        close();
        return false;
    }

    const std::wstring widePath = path.toStdWString();
    HINTERNET request = WinHttpOpenRequest(m_Connection, L"GET", widePath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        *error = winHttpError("WinHttpOpenRequest", GetLastError());
        close();
        return false;
    }
    if (secure) {
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
                      | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    }
    WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            || !WinHttpReceiveResponse(request, nullptr)) {
        *error = winHttpError("websocket handshake", GetLastError());
        WinHttpCloseHandle(request);
        close();
        return false;
    }
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status != HTTP_STATUS_SWITCH_PROTOCOLS) {
        *error = QStringLiteral("websocket handshake answered HTTP %1").arg(status);
        WinHttpCloseHandle(request);
        close();
        return false;
    }

    m_Socket = WinHttpWebSocketCompleteUpgrade(request, 0);
    const DWORD upgradeError = GetLastError();
    WinHttpCloseHandle(request);
    if (!m_Socket) {
        *error = winHttpError("WinHttpWebSocketCompleteUpgrade", upgradeError);
        close();
        return false;
    }

    // The handshake timeout must not become an idle limit on the open connection: a TV that has
    // nothing to say for a minute is not gone. readLoop() also tolerates a timeout, in case this
    // option is not honoured on the websocket handle.
    DWORD noLimit = 0;
    WinHttpSetOption(m_Socket, WINHTTP_OPTION_RECEIVE_TIMEOUT, &noLimit, sizeof(noLimit));

    m_Open = true;
    m_Reader = std::thread(&WebSocket::readLoop, this);
    return true;
}

bool WebSocket::send(const QByteArray& text, QString* error)
{
    if (!m_Open) {
        *error = QStringLiteral("not connected");
        return false;
    }
    const DWORD result = WinHttpWebSocketSend(m_Socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                              const_cast<char*>(text.constData()), static_cast<DWORD>(text.size()));
    if (result != NO_ERROR) {
        *error = winHttpError("WinHttpWebSocketSend", result);
        return false;
    }
    return true;
}

std::optional<QByteArray> WebSocket::receive(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(m_Lock);
    m_Arrived.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                       [this] { return !m_Inbox.empty() || !m_Open; });
    if (m_Inbox.empty())
        return std::nullopt;
    QByteArray message = m_Inbox.front();
    m_Inbox.pop_front();
    return message;
}

QString WebSocket::closeReason() const
{
    std::lock_guard<std::mutex> lock(m_Lock);
    return m_CloseReason;
}

void WebSocket::readLoop()
{
    std::vector<char> buffer(64 * 1024);
    QByteArray message;
    QString reason;
    for (;;) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        const DWORD result = WinHttpWebSocketReceive(m_Socket, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                     &read, &type);
        if (result == ERROR_WINHTTP_TIMEOUT && m_Open)
            continue;
        if (result != NO_ERROR) {
            reason = m_Open ? winHttpError("receive", result) : QStringLiteral("closed here");
            break;
        }
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            reason = QStringLiteral("closed by the TV");
            break;
        }
        message.append(buffer.data(), static_cast<int>(read));
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            std::lock_guard<std::mutex> lock(m_Lock);
            m_Inbox.push_back(message);
            message.clear();
            m_Arrived.notify_all();
        }
    }
    std::lock_guard<std::mutex> lock(m_Lock);
    m_Open = false;
    m_CloseReason = reason;
    m_Arrived.notify_all();
}

void WebSocket::close()
{
    const bool wasOpen = m_Open.exchange(false);
    if (m_Socket) {
        if (wasOpen)
            WinHttpWebSocketShutdown(m_Socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        // Closing the handle is also what wakes the reader out of a blocked receive.
        WinHttpCloseHandle(m_Socket);
    }
    if (m_Reader.joinable())
        m_Reader.join();
    m_Socket = nullptr;
    if (m_Connection)
        WinHttpCloseHandle(m_Connection);
    m_Connection = nullptr;
    if (m_Session)
        WinHttpCloseHandle(m_Session);
    m_Session = nullptr;
}
