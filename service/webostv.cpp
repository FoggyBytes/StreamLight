#include "webostv.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>

namespace Ssap
{

QString hdmiAppId(int n)
{
    return QStringLiteral("com.webos.app.hdmi%1").arg(n);
}

QString hdmiInputId(int n)
{
    return QStringLiteral("HDMI_%1").arg(n);
}

int hdmiFromAppId(const QString& appId)
{
    static const QRegularExpression pattern(QStringLiteral("^com\\.webos\\.app\\.hdmi(\\d)$"));
    const QRegularExpressionMatch match = pattern.match(appId);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

}

QStringList WebOsTv::permissions()
{
    return {
        QStringLiteral("LAUNCH"),
        QStringLiteral("CONTROL_POWER"),
        QStringLiteral("CONTROL_TV_SCREEN"),
        QStringLiteral("READ_POWER_STATE"),
        QStringLiteral("CONTROL_INPUT_TV"),
        QStringLiteral("CONTROL_INPUT_JOYSTICK"),
        QStringLiteral("CONTROL_MOUSE_AND_KEYBOARD"),
        QStringLiteral("READ_INPUT_DEVICE_LIST"),
        QStringLiteral("READ_RUNNING_APPS"),
        QStringLiteral("READ_CURRENT_CHANNEL"),
    };
}

bool WebOsTv::connect(const QString& ip, int timeoutMs, QString* error)
{
    m_Subscriptions.clear();
    QString secureError;
    if (m_Ws.open(ip, 3001, true, timeoutMs, &secureError)) {
        m_Endpoint = QStringLiteral("wss://%1:3001").arg(ip);
        return true;
    }
    QString plainError;
    if (m_Ws.open(ip, 3000, false, timeoutMs, &plainError)) {
        m_Endpoint = QStringLiteral("ws://%1:3000").arg(ip);
        return true;
    }
    m_Endpoint.clear();
    *error = QStringLiteral("wss:3001 %1; ws:3000 %2").arg(secureError, plainError);
    return false;
}

QString WebOsTv::nextId(const char* prefix)
{
    return QStringLiteral("%1%2").arg(QLatin1String(prefix)).arg(++m_Counter);
}

QString WebOsTv::lostReason() const
{
    const QString reason = m_Ws.closeReason();
    return reason.isEmpty() ? QStringLiteral("connection closed") : reason;
}

bool WebOsTv::sendMessage(const QJsonObject& message, QString* error)
{
    return m_Ws.send(QJsonDocument(message).toJson(QJsonDocument::Compact), error);
}

std::optional<QJsonObject> WebOsTv::waitFor(const QString& id, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        const std::optional<QByteArray> text = m_Ws.receive(timeoutMs - static_cast<int>(clock.elapsed()));
        if (!text)
            return std::nullopt;
        const QJsonObject message = QJsonDocument::fromJson(*text).object();
        if (message.value(QStringLiteral("id")).toString() == id)
            return message;
        dispatch(message);
    }
    return std::nullopt;
}

void WebOsTv::dispatch(const QJsonObject& message)
{
    const auto subscription = m_Subscriptions.find(message.value(QStringLiteral("id")).toString());
    if (subscription != m_Subscriptions.end() && message.value(QStringLiteral("type")).toString() == QLatin1String("response"))
        subscription->second(message.value(QStringLiteral("payload")).toObject());
}

bool WebOsTv::subscribe(const QString& uri, const std::function<void(const QJsonObject&)>& onUpdate, QString* error)
{
    const QString id = nextId("subscribe_");
    if (!sendMessage({ { QStringLiteral("type"), QStringLiteral("subscribe") },
                       { QStringLiteral("id"), id },
                       { QStringLiteral("uri"), uri },
                       { QStringLiteral("payload"), QJsonObject() } }, error))
        return false;
    const std::optional<QJsonObject> first = waitFor(id, 4000);
    if (!first) {
        *error = m_Ws.isOpen() ? QStringLiteral("timeout") : lostReason();
        return false;
    }
    if (first->value(QStringLiteral("type")).toString() == QLatin1String("error")) {
        *error = first->value(QStringLiteral("error")).toString();
        return false;
    }
    m_Subscriptions[id] = onUpdate;
    onUpdate(first->value(QStringLiteral("payload")).toObject());
    return true;
}

bool WebOsTv::pump(int timeoutMs)
{
    std::optional<QByteArray> text = m_Ws.receive(timeoutMs);
    while (text) {
        dispatch(QJsonDocument::fromJson(*text).object());
        text = m_Ws.receive(0);
    }
    return m_Ws.isOpen();
}

bool WebOsTv::registerClient(QString* key, int timeoutMs, const std::function<void()>& onPrompt, QString* error,
                             const std::function<bool()>& stopWaiting)
{
    QJsonObject manifest {
        { QStringLiteral("manifestVersion"), 1 },
        { QStringLiteral("appVersion"), QStringLiteral("1.0") },
        { QStringLiteral("permissions"), QJsonArray::fromStringList(permissions()) },
    };
    QJsonObject payload {
        { QStringLiteral("forcePairing"), false },
        { QStringLiteral("pairingType"), QStringLiteral("PROMPT") },
        { QStringLiteral("manifest"), manifest },
    };
    if (!key->isEmpty())
        payload.insert(QStringLiteral("client-key"), *key);

    const QString id = nextId("register_");
    if (!sendMessage({ { QStringLiteral("type"), QStringLiteral("register") },
                       { QStringLiteral("id"), id },
                       { QStringLiteral("payload"), payload } }, error))
        return false;

    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        if (stopWaiting && stopWaiting()) {
            *error = QStringLiteral("cancelled");
            return false;
        }
        // In slices of a second, so a cancel is seen while the TV is still asking.
        const int slice = qMin(1000, timeoutMs - static_cast<int>(clock.elapsed()));
        const std::optional<QJsonObject> message = waitFor(id, slice);
        if (!message) {
            if (!m_Ws.isOpen())
                break;
            continue;
        }
        const QString type = message->value(QStringLiteral("type")).toString();
        const QJsonObject answer = message->value(QStringLiteral("payload")).toObject();
        if (type == QLatin1String("registered")) {
            const QString granted = answer.value(QStringLiteral("client-key")).toString();
            if (!granted.isEmpty())
                *key = granted;
            return true;
        }
        if (type == QLatin1String("error")) {
            *error = message->value(QStringLiteral("error")).toString();
            return false;
        }
        // The TV is showing its prompt and will answer again once someone presses a button.
        if (answer.value(QStringLiteral("pairingType")).toString() == QLatin1String("PROMPT") && onPrompt)
            onPrompt();
    }
    *error = m_Ws.isOpen() ? QStringLiteral("no answer within %1 s").arg(timeoutMs / 1000) : lostReason();
    return false;
}

WebOsTv::Reply WebOsTv::request(const QString& uri, const QJsonObject& payload, int timeoutMs)
{
    Reply reply;
    const QString id = nextId("request_");
    if (!sendMessage({ { QStringLiteral("type"), QStringLiteral("request") },
                       { QStringLiteral("id"), id },
                       { QStringLiteral("uri"), uri },
                       { QStringLiteral("payload"), payload } }, &reply.error))
        return reply;

    const std::optional<QJsonObject> message = waitFor(id, timeoutMs);
    if (!message) {
        reply.error = m_Ws.isOpen() ? QStringLiteral("timeout") : lostReason();
        return reply;
    }
    reply.payload = message->value(QStringLiteral("payload")).toObject();
    if (message->value(QStringLiteral("type")).toString() == QLatin1String("error")) {
        reply.error = message->value(QStringLiteral("error")).toString();
        return reply;
    }
    if (reply.payload.value(QStringLiteral("returnValue")) == QJsonValue(false)) {
        reply.error = reply.payload.value(QStringLiteral("errorText")).toString(
                QStringLiteral("returnValue false"));
        return reply;
    }
    reply.ok = true;
    return reply;
}

bool WebOsTv::pressButton(const QString& name, QString* error)
{
    const Reply socket = request(QString::fromLatin1(Ssap::PointerSocket));
    if (!socket.ok) {
        *error = QStringLiteral("pointer channel: %1").arg(socket.error);
        return false;
    }
    const QUrl url(socket.payload.value(QStringLiteral("socketPath")).toString());
    if (!url.isValid() || url.host().isEmpty()) {
        *error = QStringLiteral("pointer channel: no socket path");
        return false;
    }
    const bool secure = url.scheme() == QLatin1String("wss");
    WebSocket pointer;
    if (!pointer.open(url.host(), static_cast<quint16>(url.port(secure ? 3001 : 3000)), secure, 4000, error,
                      url.path(QUrl::FullyEncoded)))
        return false;
    const bool sent = pointer.send(QStringLiteral("type:button\nname:%1\n\n").arg(name).toUtf8(), error);
    // Give the TV a moment to take the press before the channel closes under it.
    QThread::msleep(300);
    pointer.close();
    return sent;
}
