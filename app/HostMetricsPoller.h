#pragma once

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QString>

#include "StreamTweakBridge.h"

class QJsonObject;

/**
 * Snapshot of host metrics received via the STATS TCP command.
 * All fields are -1 when the metric is unavailable (StreamTweak not running,
 * counter not supported on this GPU vendor, etc.).
 */
struct HostMetrics
{
    int gpu      = -1;  // GPU 3D engine utilization %
    int gpuEnc   = -1;  // GPU VideoEncode engine utilization %
    int gpuTemp  = -1;  // GPU temperature °C  (NVIDIA only; -1 on AMD/Intel)
    int vramUsed  = -1;  // Dedicated GPU memory in use, MB
    int vramTotal = -1;  // Total dedicated GPU memory, MB (NVIDIA only; -1 on AMD/Intel)
    int cpu       = -1;  // CPU utilization %
    int netTx     = -1;  // Network outbound throughput, Mbps
};

/**
 * Polls StreamTweak on the host machine every second for real-time metrics.
 *
 * Must be created and used on a thread that has a running Qt event loop
 * (the main GUI thread). The session decoder thread reads latestMetrics()
 * concurrently; access is protected by an internal QMutex.
 *
 * If the host is unreachable (StreamTweak not running) each poll attempt
 * simply fails silently and latestMetrics() continues returning the last
 * known values (or all-(-1) until the first successful response).
 */
class HostMetricsPoller : public QObject
{
    Q_OBJECT

public:
    explicit HostMetricsPoller(const QString& hostAddress, QObject* parent = nullptr);

    void start();
    void stop();

    /** Thread-safe snapshot of the most recently received metrics. */
    HostMetrics latestMetrics() const;

signals:
    /**
     * Emitted when StreamTweak includes "stop":1 in a STATS response,
     * requesting that the active streaming session be terminated.
     * Connect to Session::interrupt() to act on it.
     */
    void stopRequested();

private slots:
    void poll();
    void onStatsReceived(const QString& statsJson);

private:
    static HostMetrics parseObject(const QJsonObject& obj);

    QString            m_hostAddress;
    QTimer             m_timer;
    StreamTweakBridge  m_bridge;
    mutable QMutex     m_mutex;
    HostMetrics        m_metrics;
    bool               m_pendingRequest = false;
};
