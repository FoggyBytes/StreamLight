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

void StreamTweakBridge::sendPrepare(const QString& hostAddress)
{
    sendCommand(hostAddress, QStringLiteral("PREPARE"));
}

void StreamTweakBridge::sendRestore(const QString& hostAddress)
{
    sendCommand(hostAddress, QStringLiteral("RESTORE"));
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

void StreamTweakBridge::requestSessionId(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("SESSIONID"), std::move(onResult));
}

void StreamTweakBridge::requestTailscale(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("TAILSCALE"), std::move(onResult));
}

void StreamTweakBridge::requestAppStores(const QString& hostAddress, ResponseCallback onResult)
{
    sendRequest(hostAddress, QStringLiteral("APPSTORES"), std::move(onResult));
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
