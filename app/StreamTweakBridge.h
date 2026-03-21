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
 *   STATS    — requests real-time host metrics (GPU %, encoder %, temperature,
 *              VRAM used, CPU %, network TX). StreamTweak replies with a JSON
 *              object, e.g. {"gpu":45,"gpu_enc":80,"gpu_temp":72,"vram_used":4200,
 *              "cpu":30,"net_tx":18}, or "STATS_UNAVAILABLE".
 *
 * PREPARE/RESTORE are fire-and-forget. STATUS emits statusReceived() and STATS
 * emits statsReceived() with the response string, or an empty string on error.
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

    /**
     * Asynchronously requests real-time host metrics from StreamTweak.
     * Emits statsReceived(QString) with a JSON payload on success,
     * or statsReceived("") on connection error.
     */
    void requestStats(const QString& hostAddress);

    /**
     * Asynchronously requests the store map for all managed apps from StreamTweak.
     * Emits appStoresReceived(QString) with a JSON object mapping app names to
     * store names, e.g. {"Cyberpunk 2077":"Steam","Fortnite":"Epic Games"}.
     * Emits appStoresReceived("") on connection error or if StreamTweak is
     * unreachable.
     */
    void requestAppStores(const QString& hostAddress);

    static constexpr quint16 BridgePort = 47998;

signals:
    void statusReceived(const QString& status);
    void statsReceived(const QString& statsJson);
    void appStoresReceived(const QString& storesJson);

private:
    void sendCommand(const QString& hostAddress, const QString& command);
};
