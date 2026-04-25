#pragma once

#include <QObject>
#include <QTimer>
#include <QList>
#include <QString>

#include "StreamTweakBridge.h"

/**
 * SessionTelemetrySampler
 *
 * Collects per-second client-side streaming metrics and sends each sample
 * to StreamTweak every 1 second via the SESSIONDATA TCP command.
 *
 * Lifecycle: created as a Qt child of Session in the Session constructor.
 * Call start() after the stream is up, flushAndStop() before decoder teardown.
 */
class SessionTelemetrySampler : public QObject
{
    Q_OBJECT

public:
    explicit SessionTelemetrySampler(QObject* parent = nullptr);

    /**
     * Begin sampling. Requests the session ID from StreamTweak, then starts
     * the 1s sample timer and the 10s batch timer.
     */
    void start(const QString& hostAddress, int targetFps);

    /**
     * Send any buffered samples as a final batch, then stop all timers.
     * Must be called before the video decoder is destroyed.
     */
    void flushAndStop();

private slots:
    void onSampleTimer();

private:
    struct TelemetrySample {
        float fpsAvg;
        float fpsMin;
        int   drops;
        float rttAvg;
        float rttMax;
        float jitterAvg;
        float jitterMax;
        float decodeMs;
        float bitrateMbps;
    };

    QString buildBatchJson() const;
    void    sendBatch();

    StreamTweakBridge m_Bridge;
    QTimer            m_SampleTimer;   // fires every 1 s — samples and sends immediately

    QString  m_HostAddress;
    int      m_TargetFps = 0;

    QList<TelemetrySample> m_Samples;

    // Running min/max within the current batch (reset each flush)
    float m_BatchFpsMin    =  9999.0f;
    float m_BatchRttMax    = -1.0f;
    float m_BatchJitterMax = -1.0f;
};
