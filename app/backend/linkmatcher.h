#pragma once

#include "../StreamTweakBridge.h"

#include <QObject>
#include <QString>
#include <QTimer>

class NvComputer;
class StreamingPreferences;

/**
 * Asks the host to run its wired link at this device's speed, before the stream starts.
 *
 * The whole point is the ordering. StreamTweak used to switch when its streaming server
 * logged a client connection — which happens *after* the session exists, so renegotiating
 * the adapter dropped the link for several seconds and killed the very stream it was meant
 * to help. The host can never learn early enough (it sees the session ~1 s before the
 * client connects, and a renegotiation takes longer than that); only the client knows in
 * advance. So the client asks, waits for confirmation, and only then launches.
 *
 * Two rules are non-negotiable:
 *   • never raise the host's speed — the target is min(this device, host current), and the
 *     host's own restore puts it back afterwards;
 *   • never block a launch. Every failure path still emits finished() and the stream starts
 *     anyway: a tuning feature that prevents streaming is worse than no tuning feature.
 *
 * Emits finished() exactly once, always.
 */
class LinkMatcher : public QObject
{
    Q_OBJECT

public:
    explicit LinkMatcher(QObject* parent = nullptr);

    /**
     * Runs the handshake. Safe to call with a null computer or null prefs (finishes at once).
     *
     * `prefs` must be the *session's* preferences, not the global singleton: they already
     * carry the global ← host-profile cascade built by AppSettingsManager::buildPrefs, which
     * is what makes a per-profile override of matchHostLinkSpeed take effect.
     */
    void start(NvComputer* computer, StreamingPreferences* prefs);

signals:
    /**
     * The change being made, formatted for the launch screen: "2.5 Gbps → 1 Gbps".
     * Only the numbers — the surrounding wording lives in QML, where it can be
     * translated in context.
     */
    void stage(QString detail);

    /**
     * @param changed  true when the host's link was actually switched
     * @param warning  non-empty when the attempt failed; the caller shows it and
     *                 proceeds regardless
     */
    void finished(bool changed, QString warning);

private:
    void poll();
    void done(bool changed, const QString& warning = QString());

    static QString formatMbps(quint64 mbps);

    StreamTweakBridge m_Bridge;
    QTimer            m_PollTimer;
    QString           m_HostAddress;
    quint64           m_TargetMbps  = 0;
    quint64           m_FromMbps    = 0;
    int               m_PollsLeft   = 0;
    bool              m_Finished    = false;
};
