#include "HostMetricsPoller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>

HostMetricsPoller::HostMetricsPoller(const QString& hostAddress, QObject* parent)
    : QObject(parent)
    , m_hostAddress(hostAddress)
    , m_bridge(this)
{
    connect(&m_bridge, &StreamTweakBridge::statsReceived,
            this,      &HostMetricsPoller::onStatsReceived);

    connect(&m_timer, &QTimer::timeout,
            this,     &HostMetricsPoller::poll);

    m_timer.setInterval(1000);
}

void HostMetricsPoller::start()
{
    m_timer.start();
}

void HostMetricsPoller::stop()
{
    m_timer.stop();
}

HostMetrics HostMetricsPoller::latestMetrics() const
{
    QMutexLocker locker(&m_mutex);
    return m_metrics;
}

void HostMetricsPoller::poll()
{
    // Skip if the previous request is still in flight to avoid piling up
    // socket objects when StreamTweak is slow or temporarily unreachable.
    if (m_pendingRequest)
        return;

    m_pendingRequest = true;
    m_bridge.requestStats(m_hostAddress);
}

void HostMetricsPoller::onStatsReceived(const QString& statsJson)
{
    m_pendingRequest = false;

    // Empty string means the connection failed — keep the last known values
    // rather than resetting to -1, so the overlay doesn't flicker on transient
    // network hiccups.
    if (statsJson.isEmpty() || statsJson == QStringLiteral("STATS_UNAVAILABLE"))
        return;

    HostMetrics parsed = parseJson(statsJson);

    // StreamTweak injects "stop":1 into the STATS response as a one-shot
    // signal to terminate the active streaming session from the host side.
    if (statsJson.contains(QLatin1String("\"stop\":1")))
        emit stopRequested();

    QMutexLocker locker(&m_mutex);
    m_metrics = parsed;
}

HostMetrics HostMetricsPoller::parseJson(const QString& json)
{
    HostMetrics m;

    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return m;

    QJsonObject obj = doc.object();

    auto getInt = [&](const char* key) -> int {
        QJsonValue v = obj.value(QLatin1String(key));
        return v.isDouble() ? static_cast<int>(v.toDouble()) : -1;
    };

    m.gpu       = getInt("gpu");
    m.gpuEnc    = getInt("gpu_enc");
    m.gpuTemp   = getInt("gpu_temp");
    m.vramUsed  = getInt("vram_used");
    m.vramTotal = getInt("vram_total");
    m.cpu       = getInt("cpu");
    m.netTx     = getInt("net_tx");

    return m;
}
