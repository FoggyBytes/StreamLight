#pragma once

#include "websocket.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <optional>

// The URIs of LG's "second screen" API (SSAP) that StreamLight uses.
namespace Ssap
{
constexpr char PowerState[] = "ssap://com.webos.service.tvpower/power/getPowerState";
constexpr char TurnOnScreen[] = "ssap://com.webos.service.tvpower/power/turnOnScreen";
constexpr char TurnOffScreen[] = "ssap://com.webos.service.tvpower/power/turnOffScreen";
constexpr char TurnOff[] = "ssap://system/turnOff";
constexpr char Launch[] = "ssap://system.launcher/launch";
constexpr char SwitchInput[] = "ssap://tv/switchInput";
constexpr char InputList[] = "ssap://tv/getExternalInputList";
constexpr char ForegroundApp[] = "ssap://com.webos.applicationManager/getForegroundAppInfo";
// The remote-control channel LG's phone app uses for its buttons: a second websocket.
constexpr char PointerSocket[] = "ssap://com.webos.service.networkinput/getPointerInputSocket";

// HDMI n as the TV names it: the app that shows the input, and the input itself.
QString hdmiAppId(int n);
QString hdmiInputId(int n);
// 2 for "com.webos.app.hdmi2", 0 when the app is not an HDMI input.
int hdmiFromAppId(const QString& appId);
}

// One SSAP session with a webOS TV: connect, pair (or reuse a pairing), then request/response
// matched by id. Blocking calls, one at a time: the service runs them on its own worker thread.
class WebOsTv
{
public:
    struct Reply
    {
        bool ok = false;
        QString error;          // "timeout", the reason the connection closed, or the TV's own text
        QJsonObject payload;
    };

    // Everything StreamLight will ever ask for, requested at pairing so the TV asks only once.
    static QStringList permissions();

    // wss://ip:3001 first, ws://ip:3000 as the fallback for older TVs.
    // ⚠️ 8.5.0: with pinnedCert (the certificate kept for this TV) only wss:// is tried, and the
    // TV must present that same certificate — *certChanged says when it did not. A TV paired over
    // TLS never falls back to ws://, where its key would travel in clear.
    bool connect(const QString& ip, int timeoutMs, QString* error, const QString& pinnedCert = QString(),
                 bool* certChanged = nullptr);
    QString endpoint() const { return m_Endpoint; }
    // The certificate of this connection (see WebSocket::certHash), "" on ws://.
    QString certHash() const { return m_Ws.certHash(); }
    bool isOpen() const { return m_Ws.isOpen(); }

    // Pairs, or reuses the pairing in *key. Without a key (or with one the TV no longer knows) the
    // TV shows its "allow" prompt: onPrompt is called when it does, and the call then waits up to
    // timeoutMs for the answer. The key to keep is left in *key.
    // stopWaiting, checked about once a second while the TV is asking: true gives up (the one who
    // asked has gone away).
    bool registerClient(QString* key, int timeoutMs, const std::function<void()>& onPrompt, QString* error,
                        const std::function<bool()>& stopWaiting = nullptr);

    Reply request(const QString& uri, const QJsonObject& payload = QJsonObject(), int timeoutMs = 5000);

    // Asks the TV to keep sending a URI's state as it changes. The first answer arrives here and
    // goes to onUpdate before this returns; later ones go to onUpdate from request() or pump().
    bool subscribe(const QString& uri, const std::function<void(const QJsonObject&)>& onUpdate, QString* error);

    // Handles whatever has arrived (subscription updates), waiting up to timeoutMs for the first
    // message. False once the connection has closed.
    bool pump(int timeoutMs);

    // One press of a remote-control button ("POWER", "HOME"…) through the pointer channel, the
    // same path as the phone app's remote. Opens the channel, presses, closes.
    bool pressButton(const QString& name, QString* error);

    void close() { m_Ws.close(); }

private:
    bool sendMessage(const QJsonObject& message, QString* error);
    // The next message carrying this id. Subscription updates that arrive meanwhile are handed to
    // their callback; anything else is dropped.
    std::optional<QJsonObject> waitFor(const QString& id, int timeoutMs);
    void dispatch(const QJsonObject& message);
    QString nextId(const char* prefix);
    QString lostReason() const;

    WebSocket m_Ws;
    QString m_Endpoint;
    QString m_Host;     // the TV's address for this session: pressButton() opens its channel only there
    int m_Counter = 0;
    std::map<QString, std::function<void(const QJsonObject&)>> m_Subscriptions;
};
