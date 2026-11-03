#pragma once

#include <QByteArray>
#include <QString>

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

// One client websocket over WinHTTP (Schannel for wss://). A reader thread collects whole text
// messages and receive() hands them out in order.
//
// ⚠️ Certificate errors are ignored on purpose: a webOS TV presents a self-signed certificate. The
// TV is identified by its MAC (TvNetwork::arpResolve) before anything is sent, never by its
// certificate.
class WebSocket
{
public:
    WebSocket() = default;
    ~WebSocket();
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    bool open(const QString& host, quint16 port, bool secure, int timeoutMs, QString* error,
              const QString& path = QStringLiteral("/"));
    bool send(const QByteArray& text, QString* error);

    // The next whole message, or nothing after timeoutMs or once the connection has closed.
    std::optional<QByteArray> receive(int timeoutMs);

    bool isOpen() const { return m_Open; }
    QString closeReason() const;
    void close();

private:
    void readLoop();

    HINTERNET m_Session = nullptr;
    HINTERNET m_Connection = nullptr;
    HINTERNET m_Socket = nullptr;
    std::thread m_Reader;
    mutable std::mutex m_Lock;
    std::condition_variable m_Arrived;
    std::deque<QByteArray> m_Inbox;
    std::atomic<bool> m_Open { false };
    QString m_CloseReason;
};
