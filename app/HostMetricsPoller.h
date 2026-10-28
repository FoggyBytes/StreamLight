#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
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
 * simply fails silently. latestMetrics() keeps returning the last known
 * values through a transient hiccup, but only for kStaleAfterMs: after that
 * it returns all-(-1), as before the first successful response.
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

    /** The host's clipboard sequence number, from every STATS reply that has one (6.3.0, §79).
     *  Main thread. ClipboardSync decides what counts as a change. */
    void hostClipboardSeq(qint64 seq);

private slots:
    void poll();
    void onStatsReceived(const QString& statsJson);

private:
    static HostMetrics parseObject(const QJsonObject& obj);

    // How long a reply stays valid without a newer one. Long enough to ride out a few
    // missed polls (one a second), short enough that a host that stopped answering —
    // StreamTweak closed, this client revoked — no longer passes frozen figures for live ones.
    static constexpr qint64 kStaleAfterMs = 5000;

    QString            m_hostAddress;
    QTimer             m_timer;
    StreamTweakBridge  m_bridge;
    QElapsedTimer      m_clock;
    mutable QMutex     m_mutex;
    HostMetrics        m_metrics;
    qint64             m_receivedAtMs = -1;   // m_clock time of m_metrics, -1 = never
    bool               m_pendingRequest = false;
};
