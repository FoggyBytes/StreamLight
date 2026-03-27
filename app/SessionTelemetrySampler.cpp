#include "SessionTelemetrySampler.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "streaming/session.h"
#include "streaming/video/ffmpeg.h"

SessionTelemetrySampler::SessionTelemetrySampler(QObject* parent)
    : QObject(parent)
{
    m_SampleTimer.setInterval(1000);
    m_SampleTimer.setSingleShot(false);
    connect(&m_SampleTimer, &QTimer::timeout, this, &SessionTelemetrySampler::onSampleTimer);

    m_BatchTimer.setInterval(10000);
    m_BatchTimer.setSingleShot(false);
    connect(&m_BatchTimer, &QTimer::timeout, this, &SessionTelemetrySampler::onBatchTimer);
}

void SessionTelemetrySampler::start(const QString& hostAddress, int targetFps)
{
    m_HostAddress = hostAddress;
    m_TargetFps   = targetFps;

    // Start sampling immediately — no session ID negotiation needed.
    // StreamTweak accepts batches whenever a session is active, regardless
    // of NIC throttle mode. Telemetry is fully independent of streaming settings.
    m_SampleTimer.start();
    m_BatchTimer.start();
}

void SessionTelemetrySampler::onSampleTimer()
{
    Session* session = Session::get();
    if (!session) return;

    // Retrieve last-window stats via the thread-safe accessor
    SDL_LockMutex(session->decoderLock());
    IVideoDecoder* dec = session->videoDecoder();
    TelemetryWindowStats ws = {};
    if (dec) {
        auto* ffDec = dynamic_cast<FFmpegVideoDecoder*>(dec);
        if (ffDec)
            ws = ffDec->getLastWindowStats();
    }
    SDL_UnlockMutex(session->decoderLock());

    // Skip the sample if the decoder window hasn't been filled yet (first second).
    // fpsAvg == 0 means no frames have been rendered; including it would corrupt
    // the batch average and force fps_min to 0 for the entire batch.
    if (ws.fpsAvg == 0.0) return;

    TelemetrySample s;
    s.fpsAvg      = (float)ws.fpsAvg;
    s.fpsMin      = s.fpsAvg;       // will be refined across batch as running min
    s.drops       = ws.drops;
    s.rttAvg      = (float)ws.rttAvgMs;
    s.rttMax      = s.rttAvg;       // will be refined across batch as running max
    s.jitterAvg   = (float)ws.jitterMs;
    s.jitterMax   = s.jitterAvg;    // will be refined across batch as running max
    s.decodeMs    = ws.decodeAvgMs;
    s.bitrateMbps = ws.bitrateMbps;

    // Track running min/max within the current batch
    if (s.fpsAvg    < m_BatchFpsMin)    m_BatchFpsMin    = s.fpsAvg;
    if (s.rttAvg    > m_BatchRttMax)    m_BatchRttMax    = s.rttAvg;
    if (s.jitterAvg > m_BatchJitterMax) m_BatchJitterMax = s.jitterAvg;

    m_Samples.append(s);
}

void SessionTelemetrySampler::onBatchTimer()
{
    sendBatch();
}

void SessionTelemetrySampler::flushAndStop()
{
    m_SampleTimer.stop();
    m_BatchTimer.stop();

    if (m_Samples.isEmpty()) return;

    // Apply batch-level min/max before the final send
    for (auto& s : m_Samples) {
        s.fpsMin    = m_BatchFpsMin;
        s.rttMax    = m_BatchRttMax;
        s.jitterMax = m_BatchJitterMax;
    }

    // Use synchronous send: exec() has returned and the Qt event loop is no
    // longer pumping, so the async socket in sendSessionData() would never
    // complete. sendSessionDataSync() blocks until the data is written.
    m_Bridge.sendSessionDataSync(m_HostAddress, buildBatchJson());
    m_Samples.clear();
}

void SessionTelemetrySampler::sendBatch()
{
    if (m_Samples.isEmpty()) return;

    // Apply batch-level min/max to each sample's fpsMin, rttMax, jitterMax fields
    for (auto& s : m_Samples) {
        s.fpsMin    = m_BatchFpsMin;
        s.rttMax    = m_BatchRttMax;
        s.jitterMax = m_BatchJitterMax;
    }

    QString json = buildBatchJson();
    m_Bridge.sendSessionData(m_HostAddress, json);

    m_Samples.clear();
    m_BatchFpsMin    =  9999.0f;
    m_BatchRttMax    = -1.0f;
    m_BatchJitterMax = -1.0f;
}

QString SessionTelemetrySampler::buildBatchJson() const
{
    QJsonArray samplesArray;
    for (const auto& s : m_Samples) {
        QJsonObject obj;
        obj[QStringLiteral("fps_avg")]      = qRound(s.fpsAvg * 10) / 10.0;
        obj[QStringLiteral("fps_min")]      = (int)s.fpsMin;
        obj[QStringLiteral("drops")]        = s.drops;
        obj[QStringLiteral("rtt_avg")]      = qRound(s.rttAvg * 10) / 10.0;
        obj[QStringLiteral("rtt_max")]      = qRound(s.rttMax * 10) / 10.0;
        obj[QStringLiteral("jitter_avg")]   = qRound(s.jitterAvg * 10) / 10.0;
        obj[QStringLiteral("jitter_max")]   = qRound(s.jitterMax * 10) / 10.0;
        obj[QStringLiteral("decode_ms")]    = qRound(s.decodeMs * 10) / 10.0;
        obj[QStringLiteral("bitrate_mbps")] = qRound(s.bitrateMbps * 10) / 10.0;
        samplesArray.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("target_fps")] = m_TargetFps;
    root[QStringLiteral("samples")]    = samplesArray;

    // Compact serialization — no embedded newlines (required by bridge protocol)
    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
}
