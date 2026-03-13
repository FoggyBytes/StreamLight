#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QString>

/**
 * StreamTweakBridge
 *
 * Sends one-shot TCP commands to StreamTweak on the host PC (port 47998).
 *
 * Commands:
 *   PREPARE  — sent from the PC context menu before the user launches the stream.
 *              StreamTweak sets the NIC to 1 Gbps pre-emptively so no disconnect
 *              occurs during the session.
 *   RESTORE  — sent after the session ends as an explicit fallback.
 *              StreamTweak's log monitor handles the normal case; this covers
 *              edge cases where the log event is delayed or missed.
 *   STATUS   — queries the current NIC speed from StreamTweak.
 *              StreamTweak replies with the link speed in Mbps (e.g. "1000").
 *
 * PREPARE/RESTORE are fire-and-forget. STATUS emits statusReceived() with the
 * response string, or an empty string on error/timeout.
 */
class StreamTweakBridge : public QObject
{
    Q_OBJECT

public:
    explicit StreamTweakBridge(QObject* parent = nullptr);

    void sendPrepare(const QString& hostAddress);
    void sendRestore(const QString& hostAddress);

    /**
     * Asynchronously queries the NIC speed from StreamTweak.
     * Emits statusReceived(QString) when the response arrives,
     * or statusReceived("") on error.
     */
    void requestStatus(const QString& hostAddress);

    static constexpr quint16 BridgePort = 47998;

signals:
    void statusReceived(const QString& status);

private:
    void sendCommand(const QString& hostAddress, const QString& command);
};
