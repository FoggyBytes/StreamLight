#include "tvlink.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>

#include <SDL.h>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

namespace
{

// The service's name in the service manager and its pipe's name (QLocalSocket adds \\.\pipe\).
const wchar_t k_ServiceName[] = L"StreamLight";
const char k_PipeName[] = "StreamLight";

// After starting the service: how often, and how many times, to try the pipe again.
constexpr int k_RetryMs = 700;
constexpr int k_RetryCount = 12;

QJsonObject failure(const QString& error)
{
    return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), error } };
}

}

TvLink* TvLink::get(QQmlEngine*)
{
    static TvLink* instance = nullptr;
    if (!instance) {
        instance = new TvLink();
        QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);
    }
    return instance;
}

TvLink::TvLink()
{
    m_RetryTimer = new QTimer(this);
    m_RetryTimer->setSingleShot(true);
    m_RetryTimer->setInterval(k_RetryMs);
    connect(m_RetryTimer, &QTimer::timeout, this, &TvLink::sendNext);
}

QStringList TvLink::inputs() const
{
    QStringList labels;
    for (const QJsonValue& label : m_Status.value(QStringLiteral("inputs")).toArray())
        labels.append(label.toString());
    // A TV that gave no names still has four inputs.
    for (int n = labels.size() + 1; n <= 4; n++)
        labels.append(QStringLiteral("HDMI %1").arg(n));
    return labels;
}

// ── The queue ─────────────────────────────────────────────────────────────────

void TvLink::send(const QJsonObject& message, Reply onReply, Reply onEvent)
{
    m_Queue.push({ message, std::move(onReply), std::move(onEvent) });
    if (!m_Busy && !m_RetryTimer->isActive())
        sendNext();
}

void TvLink::sendNext()
{
    if (m_Busy || m_Queue.empty())
        return;
    m_Busy = true;
    m_Buffer.clear();

    m_Socket = new QLocalSocket(this);
    connect(m_Socket, &QLocalSocket::connected, this, [this] {
        m_StartAttempts = 0;
        m_Socket->write(QJsonDocument(m_Queue.front().message).toJson(QJsonDocument::Compact) + '\n');
    });
    connect(m_Socket, &QLocalSocket::readyRead, this, [this] {
        m_Buffer.append(m_Socket->readAll());
        int newline;
        while (m_Busy && (newline = m_Buffer.indexOf('\n')) >= 0) {
            const QJsonObject line = QJsonDocument::fromJson(m_Buffer.left(newline)).object();
            m_Buffer.remove(0, newline + 1);
            if (line.contains(QStringLiteral("ok"))) {
                finishCurrent(line);
                return;
            }
            if (line.contains(QStringLiteral("event")) && m_Queue.front().onEvent)
                m_Queue.front().onEvent(line);
        }
    });
    connect(m_Socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        if (!m_Busy)
            return;
        if (m_Socket->state() != QLocalSocket::ConnectedState
                && (error == QLocalSocket::ServerNotFoundError || error == QLocalSocket::ConnectionRefusedError
                    || error == QLocalSocket::SocketTimeoutError))
            onConnectFailed();
        else
            finishCurrent(failure(QStringLiteral("the service closed the connection")));
    });
    m_Socket->connectToServer(QString::fromLatin1(k_PipeName));
}

void TvLink::finishCurrent(const QJsonObject& reply)
{
    if (!m_Busy)
        return;
    Request request = std::move(m_Queue.front());
    m_Queue.pop();
    m_Busy = false;
    if (m_Socket) {
        m_Socket->disconnect(this);
        m_Socket->abort();
        m_Socket->deleteLater();
        m_Socket = nullptr;
    }
    if (request.onReply)
        request.onReply(reply);
    sendNext();
}

void TvLink::onConnectFailed()
{
    m_Socket->disconnect(this);
    m_Socket->deleteLater();
    m_Socket = nullptr;
    m_Busy = false;

    if (m_StartAttempts == 0 && !startService()) {
        // Not installed, or not ours to start: everything waiting fails now.
        while (!m_Queue.empty()) {
            Request request = std::move(m_Queue.front());
            m_Queue.pop();
            if (request.onReply)
                request.onReply(failure(QStringLiteral("service not available")));
        }
        return;
    }
    if (++m_StartAttempts > k_RetryCount) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TvLink: the service did not open its pipe");
        m_StartAttempts = 0;
        setServiceState(CannotStart);
        while (!m_Queue.empty()) {
            Request request = std::move(m_Queue.front());
            m_Queue.pop();
            if (request.onReply)
                request.onReply(failure(QStringLiteral("service not running")));
        }
        return;
    }
    setServiceState(Starting);
    m_RetryTimer->start();
}

bool TvLink::startService()
{
#ifdef Q_OS_WIN32
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        setServiceState(CannotStart);
        return false;
    }
    SC_HANDLE service = OpenServiceW(manager, k_ServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        setServiceState(error == ERROR_SERVICE_DOES_NOT_EXIST ? NotInstalled : CannotStart);
        if (error != ERROR_SERVICE_DOES_NOT_EXIST)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TvLink: cannot open the service (%lu)", error);
        return false;
    }
    SERVICE_STATUS status = {};
    bool ok = true;
    if (QueryServiceStatus(service, &status) && status.dwCurrentState == SERVICE_STOPPED) {
        if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TvLink: cannot start the service (%lu)", GetLastError());
            setServiceState(CannotStart);
            ok = false;
        }
        else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "TvLink: service started");
        }
    }
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok;
#else
    setServiceState(NotInstalled);
    return false;
#endif
}

// ── State ─────────────────────────────────────────────────────────────────────

void TvLink::setServiceState(ServiceState state)
{
    if (state == m_ServiceState)
        return;
    m_ServiceState = state;
    emit changed();
}

void TvLink::takeStatus(const QJsonObject& reply)
{
    if (!reply.value(QStringLiteral("ok")).toBool() || !reply.contains(QStringLiteral("paired")))
        return;
    m_ServiceState = Running;
    m_Status = reply;
    emit changed();
}

void TvLink::setPairState(PairState state, const QString& error)
{
    m_PairState = state;
    m_PairError = error;
    emit pairStateChanged();
}

// ── Requests ──────────────────────────────────────────────────────────────────

void TvLink::refresh()
{
    // Every two seconds from Settings: never pile them up behind a long request (pairing).
    if (m_Queue.size() > 1)
        return;
    send({ { QStringLiteral("cmd"), QStringLiteral("status") } }, [this](const QJsonObject& reply) { takeStatus(reply); });
}

void TvLink::discover()
{
    m_Searching = true;
    m_Found.clear();
    emit foundChanged();
    send({ { QStringLiteral("cmd"), QStringLiteral("discover") } }, [this](const QJsonObject& reply) {
        m_Found = reply.value(QStringLiteral("tvs")).toArray().toVariantList();
        m_Searching = false;
        emit foundChanged();
    });
}

void TvLink::pair(const QString& ip, const QString& name)
{
    setPairState(PairConnecting);
    send({ { QStringLiteral("cmd"), QStringLiteral("pair") }, { QStringLiteral("ip"), ip }, { QStringLiteral("name"), name } },
         [this](const QJsonObject& reply) {
             if (m_PairState == PairIdle)
                 return;    // cancelled meanwhile
             if (reply.value(QStringLiteral("ok")).toBool()) {
                 takeStatus(reply);
                 setPairState(PairDone);
             }
             else {
                 setPairState(PairFailed, reply.value(QStringLiteral("error")).toString());
             }
         },
         [this](const QJsonObject& event) {
             if (event.value(QStringLiteral("event")).toString() == QLatin1String("prompt"))
                 setPairState(PairPrompt);
         });
}

void TvLink::cancelPair()
{
    const bool pairing = m_Busy && m_Queue.front().message.value(QStringLiteral("cmd")).toString() == QLatin1String("pair");
    setPairState(PairIdle);
    // Hanging up is the cancel: the service checks for it while the TV is asking.
    if (pairing)
        finishCurrent(failure(QStringLiteral("cancelled")));
}

void TvLink::resetPair()
{
    if (m_PairState != PairConnecting && m_PairState != PairPrompt)
        setPairState(PairIdle);
}

void TvLink::setInput(int input)
{
    m_Status.insert(QStringLiteral("input"), input);
    emit changed();
    send({ { QStringLiteral("cmd"), QStringLiteral("set") }, { QStringLiteral("input"), input } },
         [this](const QJsonObject& reply) { takeStatus(reply); });
}

void TvLink::setOnAtStart(bool on)
{
    m_Status.insert(QStringLiteral("onAtStart"), on);
    emit changed();
    send({ { QStringLiteral("cmd"), QStringLiteral("set") }, { QStringLiteral("onAtStart"), on } },
         [this](const QJsonObject& reply) { takeStatus(reply); });
}

void TvLink::setOffAtShutdown(bool off)
{
    m_Status.insert(QStringLiteral("offAtShutdown"), off);
    emit changed();
    send({ { QStringLiteral("cmd"), QStringLiteral("set") }, { QStringLiteral("offAtShutdown"), off } },
         [this](const QJsonObject& reply) { takeStatus(reply); });
}

void TvLink::setOffOnlyIfShowingPc(bool on)
{
    m_Status.insert(QStringLiteral("offOnlyIfShowingPc"), on);
    emit changed();
    send({ { QStringLiteral("cmd"), QStringLiteral("set") }, { QStringLiteral("offOnlyIfShowingPc"), on } },
         [this](const QJsonObject& reply) { takeStatus(reply); });
}

void TvLink::turnOn()
{
    send({ { QStringLiteral("cmd"), QStringLiteral("tv_on") } });
}

void TvLink::turnOff()
{
    send({ { QStringLiteral("cmd"), QStringLiteral("tv_off") } });
}

void TvLink::forget()
{
    send({ { QStringLiteral("cmd"), QStringLiteral("forget") } }, [this](const QJsonObject& reply) { takeStatus(reply); });
}

void TvLink::announceRestart()
{
#ifdef Q_OS_WIN32
    const wchar_t* pipeName = L"\\\\.\\pipe\\StreamLight";
    if (!WaitNamedPipeW(pipeName, 300))
        return;
    HANDLE pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return;
    const char request[] = "{\"cmd\":\"restart\"}\n";
    DWORD done = 0;
    if (WriteFile(pipe, request, sizeof(request) - 1, &done, nullptr)) {
        // The answer is immediate; read it so the service is not left writing to nobody.
        char reply[256];
        ReadFile(pipe, reply, sizeof(reply), &done, nullptr);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "TvLink: restart announced to the service");
    }
    CloseHandle(pipe);
#endif
}
