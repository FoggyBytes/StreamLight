#include "StreamTweakBridge.h"
#include "backend/identitymanager.h"

#include <QByteArray>
#include <QDateTime>
#include <QStringList>
#include <QSysInfo>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>

#include <memory>

#include <openssl/evp.h>
#include <openssl/pem.h>

StreamTweakBridge::StreamTweakBridge(QObject* parent)
    : QObject(parent)
{
}

// ── Authentication helpers ──────────────────────────────────────────────────

QByteArray StreamTweakBridge::signPayload(const QByteArray& payload)
{
    QByteArray keyPem = IdentityManager::get()->getPrivateKey();
    if (keyPem.isEmpty())
        return {};

    BIO* bio = BIO_new_mem_buf(keyPem.constData(), keyPem.size());
    if (!bio)
        return {};

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey)
        return {};

    QByteArray sig;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx &&
        EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
        EVP_DigestSignUpdate(ctx, payload.constData(), payload.size()) == 1) {
        size_t len = 0;
        if (EVP_DigestSignFinal(ctx, nullptr, &len) == 1) {
            sig.resize(static_cast<int>(len));
            if (EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char*>(sig.data()), &len) == 1)
                sig.resize(static_cast<int>(len));
            else
                sig.clear();
        }
    }
    if (ctx)
        EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return sig;
}

QString StreamTweakBridge::buildAuthLine(const QString& command)
{
    QString uid = IdentityManager::get()->getUniqueId();
    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    QByteArray payload = (uid + "\n" + QString::number(ts) + "\n" + command).toUtf8();
    QByteArray sig = signPayload(payload);
    if (sig.isEmpty())
        return QString();
    return QStringLiteral("AUTH1 %1 %2 %3")
        .arg(uid)
        .arg(ts)
        .arg(QString::fromLatin1(sig.toBase64()));
}

// ── Fire-and-forget commands ────────────────────────────────────────────────

// sendPrepare() was removed in 4.6.0 together with the PREPARE verb it sent. It meant
// "apply the target you have configured", and StreamTweak 8.1.0 no longer configures one:
// the client names the speed it wants in SETSPEED.
//
// sendRestore() went too — it had no callers at all, in any version. The host has always
// ended sessions from its own streaming-server log, so the client never needed to say so.

void StreamTweakBridge::sendShutdown(const QString& hostAddress, bool installUpdates)
{
    sendCommand(hostAddress, installUpdates ? QStringLiteral("SHUTDOWN_UPDATE")
                                            : QStringLiteral("SHUTDOWN"));
}

void StreamTweakBridge::sendCommand(const QString& hostAddress, const QString& command)
{
    // Authenticated: AUTH1 line then the command. The reply ("OK") is discarded.
    QStringList lines;
    QString auth = buildAuthLine(command);
    if (!auth.isEmpty())
        lines << auth;
    lines << command;
    sendRawRequest(hostAddress, lines, nullptr);
}

// ── Query commands ──────────────────────────────────────────────────────────

void StreamTweakBridge::sendRequest(const QString& hostAddress,
                                    const QString& command,
                                    ResponseCallback onResult)
{
    QStringList lines;
    QString auth = buildAuthLine(command);
    if (!auth.isEmpty())
        lines << auth;
    lines << command;
    sendRawRequest(hostAddress, lines, std::move(onResult));
}

void StreamTweakBridge::sendRawRequest(const QString& hostAddress,
                                       const QStringList& lines,
                                       ResponseCallback onResult)
{
    // Each request owns its socket (parented to the bridge for lifetime safety)
    // and its own callback, so concurrent requests cannot cross-talk.
    QTcpSocket* socket = new QTcpSocket(this);

    auto buffer = std::make_shared<QByteArray>();
    auto done   = std::make_shared<bool>(false);

    // Invokes onResult exactly once, then tears the socket down. Safe to call from
    // any of the socket's slots — the 'done' guard collapses extra calls to no-ops.
    auto finish = [socket, done, onResult](const QString& result) {
        if (*done)
            return;
        *done = true;
        if (onResult)
            onResult(result);
        socket->abort();
        socket->deleteLater();
    };

    // Watchdog: guarantees onResult fires even if the host accepts the connection
    // but never sends a newline-terminated reply (half-open).
    QTimer* watchdog = new QTimer(socket);
    watchdog->setSingleShot(true);
    watchdog->setInterval(ResponseTimeoutMs);
    QObject::connect(watchdog, &QTimer::timeout, socket,
                     [finish]() { finish(QString()); });

    QObject::connect(socket, &QAbstractSocket::errorOccurred, socket,
                     [finish](QAbstractSocket::SocketError) { finish(QString()); });

    QObject::connect(socket, &QTcpSocket::connected, socket, [socket, lines]() {
        QTextStream stream(socket);
        for (const QString& line : lines)
            stream << line << "\n";
        stream.flush();
    });

    // Accumulate until the protocol's '\n' terminator arrives (handles replies
    // split across multiple TCP segments, e.g. large APPSTORES payloads).
    QObject::connect(socket, &QTcpSocket::readyRead, socket,
                     [socket, buffer, finish]() {
        buffer->append(socket->readAll());
        int nl = buffer->indexOf('\n');
        if (nl >= 0)
            finish(QString::fromUtf8(buffer->left(nl)).trimmed());
    });

    // If the peer closes without ever sending a newline, fall back to whatever
    // was buffered rather than reporting an empty response.
    QObject::connect(socket, &QTcpSocket::disconnected, socket,
                     [buffer, finish]() {
        finish(QString::fromUtf8(*buffer).trimmed());
    });

    watchdog->start();
    socket->connectToHost(hostAddress, BridgePort);
}

void StreamTweakBridge::requestStatus(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("STATUS"), std::move(onResult));
}

void StreamTweakBridge::requestStats(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("STATS"), std::move(onResult));
}

void StreamTweakBridge::requestGameState(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("GAMESTATE"), std::move(onResult));
}

// requestSessionId() (SESSIONID) was removed in 4.6.0: like sendRestore(), it had no
// callers in any version. Telemetry carries its own session id in the SESSIONDATA batch,
// so the client never had to ask for one.

void StreamTweakBridge::requestTailscale(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("TAILSCALE"), std::move(onResult));
}

void StreamTweakBridge::requestAppStores(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("APPSTORES"), std::move(onResult));
}

void StreamTweakBridge::requestUpdateState(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("UPDATESTATE"), std::move(onResult));
}

void StreamTweakBridge::requestLockState(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("LOCKSTATE"), std::move(onResult));
}

void StreamTweakBridge::sendUnlockBegin(const QString& hostAddress)
{
    sendCommand(hostAddress, QStringLiteral("UNLOCKBEGIN"));
}

void StreamTweakBridge::sendUnlockEnd(const QString& hostAddress)
{
    sendCommand(hostAddress, QStringLiteral("UNLOCKEND"));
}

void StreamTweakBridge::requestNetInfo(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("NETINFO"), std::move(onResult));
}

void StreamTweakBridge::requestLastSession(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("LASTSESSION"), std::move(onResult));
}

void StreamTweakBridge::sendRestore(const QString& hostAddress, ResponseCallback onResult)
{
    // "I have finished" — sent only when the user deliberately stops the session, never on a
    // plain disconnect, which the host must be free to treat as a resumable pause. It is the
    // only way the host can know a *Desktop* session is over: there is no process to watch.
    sendRequest(hostAddress, QStringLiteral("RESTORE"), std::move(onResult));
}

void StreamTweakBridge::sendSetSpeed(const QString& hostAddress, quint64 mbps, ResponseCallback onResult)
{
    // Speeds travel as plain numbers: the driver's display strings ("1.0 Gbps Full
    // Duplex") vary by vendor and can be localized, so the client never parses them.
    sendRequest(hostAddress,
                QStringLiteral("SETSPEED ") + QString::number(mbps),
                std::move(onResult));
}

void StreamTweakBridge::sendUpdateCheck(const QString& hostAddress)
{
    sendCommand(hostAddress, QStringLiteral("UPDATECHECK"));
}

void StreamTweakBridge::sendUpdateNow(const QString& hostAddress, const QString& scope)
{
    // scope: "SEC" (security+critical+defender) or "ALL" (everything except upgrades).
    sendCommand(hostAddress, QStringLiteral("UPDATE_NOW ") + scope);
}

void StreamTweakBridge::requestUpdateProgress(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("UPDATEPROGRESS"), std::move(onResult));
}

// ── Capability negotiation / enrollment (unauthenticated bootstrap) ─────────

void StreamTweakBridge::requestCaps(const QString& hostAddress, ResponseCallback onResult)
{
    sendRawRequest(hostAddress, QStringList{ QStringLiteral("CAPS") }, std::move(onResult));
}

void StreamTweakBridge::enroll(const QString& hostAddress, const QString& pin, ResponseCallback onResult)
{
    QString    uid     = IdentityManager::get()->getUniqueId();
    QByteArray certB64 = IdentityManager::get()->getCertificate().toBase64();
    // Send our machine hostname (base64, so spaces/non-ASCII can't break the line)
    // so the host shows a readable device name instead of a bare IP.
    QByteArray nameB64 = QSysInfo::machineHostName().toUtf8().toBase64();

    QStringList lines;
    lines << (QStringLiteral("ENROLL ") + uid + QStringLiteral(" ") + pin
              + QStringLiteral(" ") + QString::fromLatin1(nameB64));
    lines << QString::fromLatin1(certB64);
    sendRawRequest(hostAddress, lines, std::move(onResult));
}

// ── Session telemetry ───────────────────────────────────────────────────────

void StreamTweakBridge::sendSessionData(const QString& hostAddress, const QString& jsonPayload)
{
    // Protocol: AUTH1 line, then "SESSIONDATA", then the compact JSON payload.
    QStringList lines;
    QString auth = buildAuthLine(QStringLiteral("SESSIONDATA"));
    if (!auth.isEmpty())
        lines << auth;
    lines << QStringLiteral("SESSIONDATA");
    lines << jsonPayload;
    sendRawRequest(hostAddress, lines, nullptr);
}

void StreamTweakBridge::sendSessionDataSync(const QString& hostAddress, const QString& jsonPayload)
{
    // Synchronous send: used only for the final flush in flushAndStop(), where
    // exec() has already returned and the Qt event loop is not running, so an
    // async socket would never complete its connected/readyRead cycle.
    QTcpSocket socket;
    socket.connectToHost(hostAddress, BridgePort);
    if (!socket.waitForConnected(2000))
        return;

    QTextStream stream(&socket);
    QString auth = buildAuthLine(QStringLiteral("SESSIONDATA"));
    if (!auth.isEmpty())
        stream << auth << "\n";
    stream << "SESSIONDATA\n" << jsonPayload << "\n";
    stream.flush();

    socket.waitForBytesWritten(2000);
    socket.disconnectFromHost();
}
