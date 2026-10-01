// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHostAddress>

// PyroWave frames arrive as hundreds of packets at line rate. Two places can
// drop them before Moonlight reads the socket:
// - Linux caps the video socket's receive buffer at net.core.rmem_max.
// - On Windows, a network adapter whose receive ring is configured with only
//   a few descriptors (a Realtek USB 2.5GbE ships with 16) overflows at the
//   tail of every burst, inside the adapter, where Windows never counts it.
// This checks for either and fixes it with the user's authorization.
class NetworkBuffers : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool needsFix READ needsFix NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int currentKb READ currentKb NOTIFY changed)
    Q_PROPERTY(int recommendedMb READ recommendedMb CONSTANT)
    Q_PROPERTY(QString problemText READ problemText NOTIFY changed)
    Q_PROPERTY(QString fixDescription READ fixDescription CONSTANT)
    Q_PROPERTY(QString manualHint READ manualHint CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString manualCommand READ manualCommand NOTIFY changed)

public:
    explicit NetworkBuffers(QObject* parent = nullptr);

    // Whether this PC may drop PyroWave packets before Moonlight sees them.
    // Returns false where the limits cannot be read or do not apply.
    static bool receiveBufferTooSmall();
    // The launch warning for that case, or empty.
    static QString launchWarning();

    // The speed of the fastest connected wired link in Mbps, or 0 when none is
    // connected or its speed is unknown. wirelessConnected, when given, reports
    // whether a wireless adapter is connected.
    static int wiredLinkMbps(bool* wirelessConnected = nullptr);
    // Speed of the wired interface actually routed to the selected host.
    static int routedWiredLinkMbps(const QHostAddress& host);

    bool needsFix() const { return m_NeedsFix; }
    bool canApply() const;
    bool busy() const { return m_Busy; }
    int currentKb() const { return int(m_CurrentBytes / 1024); }
    int recommendedMb() const;
    QString problemText() const { return m_ProblemText; }
    QString fixDescription() const;
    QString manualHint() const;
    QString message() const { return m_Message; }
    QString manualCommand() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void copyCommand();

signals:
    void changed();

private:
    void finishApply(bool started, bool cancelled, unsigned long exitCode);

    bool m_NeedsFix = false;
    bool m_Busy = false;
    qint64 m_CurrentBytes = 0;
    QString m_ProblemText;
    QString m_Message;
    QStringList m_ApplyCommand;
    QString m_WindowsScript;
};
