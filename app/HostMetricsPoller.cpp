#include "HostMetricsPoller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>

HostMetricsPoller::HostMetricsPoller(const QString& hostAddress, QObject* parent)
    : QObject(parent)
    , m_hostAddress(hostAddress)
    , m_bridge(this)
{
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
    // The bridge always invokes this callback exactly once (success, error, or
    // timeout), so m_pendingRequest can never get stuck true and stall polling.
    m_bridge.requestStats(m_hostAddress,
                          [this](const QString& json) { onStatsReceived(json); });
}

void HostMetricsPoller::onStatsReceived(const QString& statsJson)
{
    m_pendingRequest = false;

    // Empty string means the connection failed — keep the last known values
    // rather than resetting to -1, so the overlay doesn't flicker on transient
    // network hiccups.
    if (statsJson.isEmpty() || statsJson == QStringLiteral("STATS_UNAVAILABLE"))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(statsJson.toUtf8());
    if (!doc.isObject())
        return;

    QJsonObject obj = doc.object();

    // StreamTweak sets "stop":1 in the STATS response as a one-shot signal to
    // terminate the active streaming session from the host side. Read it from
    // the parsed object (not a raw substring match), so whitespace or field
    // ordering in the JSON can never make us miss — or falsely trigger — a stop.
    if (obj.value(QStringLiteral("stop")).toInt() == 1)
        emit stopRequested();

    // The host's clipboard sequence number (StreamTweak 8.7.0, only while it shares its
    // clipboard). The host never pushes: this is how the client learns there is something new
    // to ask for with CLIPGET. Absent means nothing to report — not a change.
    //
    // ⚠️ Reported on every reply, not only when it moves: ClipboardSync is created after the
    // poller, and the first number it hears is where it starts from. Filtering here would hand
    // it the first CHANGE as that starting point, and the copy behind it would be lost.
    QJsonValue clip = obj.value(QStringLiteral("clip"));
    if (clip.isDouble())
        emit hostClipboardSeq(static_cast<qint64>(clip.toDouble()));

    HostMetrics parsed = parseObject(obj);

    QMutexLocker locker(&m_mutex);
    m_metrics = parsed;
}

HostMetrics HostMetricsPoller::parseObject(const QJsonObject& obj)
{
    HostMetrics m;

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
