#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <QVariantMap>

#include <atomic>
#include <memory>

class ComputerManager;
class QQmlEngine;

/**
 * PyroWave Test (7.0.0) — the host's Options → "PyroWave Test".
 *
 * Measures, for one host, what a PyroWave stream can carry, in two steps:
 *   1. Network: the host sends UDP to this device over Vibeshine/Vibepollo 2.0.1's paired
 *      /pyrowave-udp-probe, first spread evenly (the capacity without loss, the "budget"), then
 *      as whole video frames at the stream's frame rate (the fastest clean "pace").
 *   2. Decoder: synthetic frames at the host's effective resolution, frame rate, 4:4:4 and HDR
 *      are encoded here, decoded through the same D3D11 surfaces a stream uses, and timed; the
 *      bitrate is lowered only if this device can't keep up.
 * The result is a bitrate for the chosen target (Minimum, Recommended, Moderate, Maximum), which
 * the popup can apply, and the pace, which every later PyroWave stream to that host announces
 * (x-ss-video[0].pyrowavePaceMbps) so the host sends its frames at a rate the path delivers.
 *
 * Derived from Nonary's PyroWaveCalibrator (Nonary/moonlight-qt master at f96b0524, GPLv3): the
 * network searches, the D3D11 test renderer, the synthetic source, the record framing and the
 * per-format device search are his. Ours: one format instead of his sixteen (the one the host
 * will stream, from Settings plus its active profile), the target chosen before the test as his
 * own flow does (the pace depends on the frame size the target implies), both steps in one go,
 * and the result kept PER HOST instead of in one global preference.
 *
 * ⚠️ Nothing changes by itself: the bitrate moves only when the user presses "Use", and the pace
 * is used only while this device's wired link is the one it was measured on, and only on the LAN.
 */
class PyroWaveTest : public QObject
{
    Q_OBJECT

    // 0 idle · 1 network · 2 decoder · 3 done · 4 failed
    Q_PROPERTY(int phase READ phase NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString hostUuid READ hostUuid NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)

public:
    enum Target { Minimum, Recommended, Moderate, Maximum };
    Q_ENUM(Target)

    static PyroWaveTest* get(QQmlEngine* engine = nullptr);

    int phase() const { return m_Phase; }
    double progress() const { return m_Progress; }
    QString status() const { return m_Status; }
    QString error() const { return m_Error; }
    QString hostUuid() const { return m_HostUuid; }
    QVariantMap result() const { return m_Result; }

    // What the test would measure for this host: the effective format (Settings plus the active
    // profile), whether that codec is PyroWave, the bitrate now and where it lives.
    Q_INVOKABLE QVariantMap settingsFor(const QString& hostUuid) const;
    // The last result kept for this host, or an empty map.
    Q_INVOKABLE QVariantMap savedResult(const QString& hostUuid) const;

    // Shows the saved result for this host (phase 3), or idle (phase 0) when there is none.
    Q_INVOKABLE void show(const QString& hostUuid);
    Q_INVOKABLE void start(ComputerManager* manager, const QString& hostUuid, int target);
    Q_INVOKABLE void cancel();
    // Sets the bitrate where it lives for this host: its active profile when that profile sets
    // one, otherwise Settings. Returns where it went, for the toast.
    Q_INVOKABLE QString applyBitrate(const QString& hostUuid, int kbps);

    // For Session: the pace to announce for this host, or 0. Only while this device's wired
    // link is still the one the test ran on.
    static int paceMbpsFor(const QString& hostUuid, int clientLinkMbps);

signals:
    void changed();

private:
    explicit PyroWaveTest(QObject* parent = nullptr);
    ~PyroWaveTest() override;

    void setStatus(const QString& status, double progress);
    void fail(const QString& error);
    void finish(const QVariantMap& result);

    int m_Phase = 0;
    double m_Progress = 0;
    QString m_Status;
    QString m_Error;
    QString m_HostUuid;
    QVariantMap m_Result;
    QPointer<QThread> m_Worker;
    std::shared_ptr<std::atomic<bool>> m_Cancel;
};
