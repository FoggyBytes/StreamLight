#pragma once

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <queue>

class QLocalSocket;
class QTimer;

/**
 * Settings → TV (8.4.0): the LG TV follows this PC.
 *
 * <p>The work is done by StreamLightService.exe (StreamLight/service/), a Windows service that
 * runs from boot, before anyone signs in, so the TV comes on at the sign-in screen too. This is
 * only its remote control: it reads the state and passes on what the user changes, over the
 * service's pipe (\\.\pipe\StreamLight, one JSON object per line each way).</p>
 *
 * <p>The pipe has one instance, so requests go one at a time, in order. A service that is not
 * running is started here (the setup lets the signed-in user start it); with no TV paired it
 * stops by itself after ten minutes without a request.</p>
 */
class TvLink : public QObject
{
    Q_OBJECT

public:
    enum ServiceState { Unknown, Running, Starting, NotInstalled, CannotStart };
    Q_ENUM(ServiceState)

    enum PairState { PairIdle, PairConnecting, PairPrompt, PairDone, PairFailed };
    Q_ENUM(PairState)

private:
    Q_PROPERTY(ServiceState serviceState READ serviceState NOTIFY changed)
    Q_PROPERTY(bool paired READ paired NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString ip READ ip NOTIFY changed)
    // This PC's HDMI input, 1-4; 0 = not known.
    Q_PROPERTY(int input READ input NOTIFY changed)
    // The TV's own names for HDMI 1, 2…
    Q_PROPERTY(QStringList inputs READ inputs NOTIFY changed)
    // The TV comes on when this PC starts / goes off when it shuts down. Sleep, wake and the screen
    // are left alone since 03/11/2026 (service/powerpolicy.h says why).
    Q_PROPERTY(bool onAtStart READ onAtStart NOTIFY changed)
    Q_PROPERTY(bool offAtShutdown READ offAtShutdown NOTIFY changed)
    Q_PROPERTY(bool offOnlyIfShowingPc READ offOnlyIfShowingPc NOTIFY changed)
    // The service has the TV connected, which it can only be while the TV is on.
    Q_PROPERTY(bool tvOn READ tvOn NOTIFY changed)
    // The HDMI input the TV is showing, 0 for anything else (an app, live TV).
    Q_PROPERTY(int showing READ showing NOTIFY changed)

    Q_PROPERTY(bool searching READ searching NOTIFY foundChanged)
    // [{ name, ip, mac }]
    Q_PROPERTY(QVariantList found READ found NOTIFY foundChanged)
    Q_PROPERTY(PairState pairState READ pairState NOTIFY pairStateChanged)
    Q_PROPERTY(QString pairError READ pairError NOTIFY pairStateChanged)

public:
    static TvLink* get(QQmlEngine* engine = nullptr);

    ServiceState serviceState() const { return m_ServiceState; }
    bool paired() const { return m_Status.value(QStringLiteral("paired")).toBool(); }
    QString name() const { return m_Status.value(QStringLiteral("name")).toString(); }
    QString ip() const { return m_Status.value(QStringLiteral("ip")).toString(); }
    int input() const { return m_Status.value(QStringLiteral("input")).toInt(); }
    QStringList inputs() const;
    bool onAtStart() const { return m_Status.value(QStringLiteral("onAtStart")).toBool(true); }
    bool offAtShutdown() const { return m_Status.value(QStringLiteral("offAtShutdown")).toBool(true); }
    bool offOnlyIfShowingPc() const { return m_Status.value(QStringLiteral("offOnlyIfShowingPc")).toBool(true); }
    bool tvOn() const { return m_Status.value(QStringLiteral("on")).toBool(); }
    int showing() const { return m_Status.value(QStringLiteral("showing")).toInt(); }
    bool searching() const { return m_Searching; }
    QVariantList found() const { return m_Found; }
    PairState pairState() const { return m_PairState; }
    QString pairError() const { return m_PairError; }

    // Reads the state again (Settings asks every two seconds while the TV tab shows). Starts the
    // service when it is not running.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void discover();
    Q_INVOKABLE void pair(const QString& ip, const QString& name);
    // Gives up a pairing still waiting for the TV: the service sees the pipe close and stops.
    Q_INVOKABLE void cancelPair();
    Q_INVOKABLE void resetPair();
    Q_INVOKABLE void setInput(int input);
    Q_INVOKABLE void setOnAtStart(bool on);
    Q_INVOKABLE void setOffAtShutdown(bool off);
    Q_INVOKABLE void setOffOnlyIfShowingPc(bool on);
    Q_INVOKABLE void turnOn();
    Q_INVOKABLE void turnOff();
    Q_INVOKABLE void forget();

    // StreamLight is about to restart this PC (Power): the TV stays on. Blocking, a few hundred
    // milliseconds at most, nothing when the service is not there — it runs right before the
    // restart call, with no event loop to come back to.
    static void announceRestart();

signals:
    void changed();
    void foundChanged();
    void pairStateChanged();

private:
    using Reply = std::function<void(const QJsonObject&)>;
    struct Request
    {
        QJsonObject message;
        Reply onReply;
        Reply onEvent;
    };

    TvLink();
    void send(const QJsonObject& message, Reply onReply = nullptr, Reply onEvent = nullptr);
    void sendNext();
    void finishCurrent(const QJsonObject& reply);
    void onConnectFailed();
    bool startService();
    void takeStatus(const QJsonObject& reply);
    void setServiceState(ServiceState state);
    void setPairState(PairState state, const QString& error = QString());

    QLocalSocket* m_Socket = nullptr;
    std::queue<Request> m_Queue;
    bool m_Busy = false;
    QByteArray m_Buffer;
    int m_StartAttempts = 0;
    QTimer* m_RetryTimer = nullptr;

    ServiceState m_ServiceState = Unknown;
    QJsonObject m_Status;
    bool m_Searching = false;
    QVariantList m_Found;
    PairState m_PairState = PairIdle;
    QString m_PairError;
};
