#include "linkmatcher.h"
#include "linkspeed.h"
#include "nvcomputer.h"
#include "../settings/streamingpreferences.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
    constexpr int PollIntervalMs = 500;

    // Absolute ceiling, not the expected cost: a measured switch takes 6-8 s and normally
    // ends the wait early. It has to clear the *host's* own settle timeout (15 s) with room
    // to spare — the first field test failed precisely because this was 10 s, so the client
    // gave up while the host was still working and then launched into a link that was down.
    // Only reached if the host never becomes reachable again, in which case the launch was
    // doomed anyway.
    constexpr int MaxWaitMs = 40000;
}

LinkMatcher::LinkMatcher(QObject* parent)
    : QObject(parent)
{
    m_PollTimer.setInterval(PollIntervalMs);
    m_PollTimer.setSingleShot(false);
    connect(&m_PollTimer, &QTimer::timeout, this, &LinkMatcher::poll);
}

QString LinkMatcher::formatMbps(quint64 mbps)
{
    if (mbps == 0) return QStringLiteral("—");
    if (mbps >= 1000) {
        double g = mbps / 1000.0;
        return QString::number(g, 'g', 3) + QStringLiteral(" Gbps");
    }
    return QString::number(mbps) + QStringLiteral(" Mbps");
}

void LinkMatcher::done(bool changed, const QString& warning)
{
    if (m_Finished) return;
    m_Finished = true;
    m_PollTimer.stop();
    emit finished(changed, warning);
}

void LinkMatcher::start(NvComputer* computer, StreamingPreferences* prefs)
{
    if (computer == nullptr || prefs == nullptr) { done(false); return; }

    // Deliberately the session's prefs, not StreamingPreferences::get(): the global value
    // is only the bottom of the cascade, and a host profile may override it.
    if (!prefs->matchHostLinkSpeed) {
        done(false);
        return;
    }

    QString address;
    {
        QReadLocker lock(&computer->lock);
        address = computer->activeAddress.address();
    }
    if (address.isEmpty()) { done(false); return; }
    m_HostAddress = address;

    // What can this device actually do on the path to *this* host? A docked handheld can
    // have its cable up while routing over Wi-Fi or Tailscale, and matching the host to a
    // link that carries no traffic would be worse than not matching at all.
    LinkSpeed::Info local = LinkSpeed::probeForHost(QHostAddress(address));
    if (!local.usable()) {
        qInfo() << "Link match skipped:" << (local.reason.isEmpty()
                                             ? QStringLiteral("local link unknown") : local.reason);
        done(false);
        return;
    }

    const quint64 localMbps = local.mbps;

    m_Bridge.requestNetInfo(address, [this, localMbps](const QString& reply) {
        // Empty or ERR means the host predates 8.1.0 and has no NETINFO. Nothing to do,
        // and nothing to tell the user: they simply keep the behaviour they had.
        if (reply.isEmpty() || reply.startsWith(QStringLiteral("ERR"))) {
            qInfo() << "Link match skipped: host does not support NETINFO";
            done(false);
            return;
        }

        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "Link match: malformed NETINFO:" << err.errorString();
            done(false);
            return;
        }

        QJsonObject o = doc.object();
        if (!o.value(QStringLiteral("allow_client_control")).toBool()) {
            qInfo() << "Link match skipped: host does not allow clients to change its link speed";
            done(false);
            return;
        }
        if (o.value(QStringLiteral("session_active")).toBool()) {
            qInfo() << "Link match skipped: a session is already running on the host";
            done(false);
            return;
        }

        const quint64 hostMbps = static_cast<quint64>(o.value(QStringLiteral("current_mbps")).toDouble());
        if (hostMbps == 0) { done(false); return; }

        // Only ever downward: raising the host's link is the restore's job, not ours.
        const quint64 want = qMin(localMbps, hostMbps);

        if (want == hostMbps) {
            // Already at the right speed — but say so anyway, because the host may have a
            // restore pending from a session that just ended (it waits ~60 s before putting
            // the link back, so a quick relaunch doesn't pay for a second renegotiation).
            // Asking for the speed it is already at cancels that timer; the host treats it
            // as a no-op and replies immediately. Without this call the restore could fire
            // mid-launch and drop the link during the handshake.
            // Fire-and-forget: nothing to wait for, nothing to show.
            m_Bridge.sendSetSpeed(m_HostAddress, want, [](const QString&) {});
            done(false);
            return;
        }

        // The host must actually support the value; speeds are matched by number, never
        // by the driver's display string, which varies by vendor and can be localized.
        bool supported = false;
        for (const QJsonValue& v : o.value(QStringLiteral("supported")).toArray()) {
            if (static_cast<quint64>(v.toObject().value(QStringLiteral("mbps")).toDouble()) == want) {
                supported = true;
                break;
            }
        }
        if (!supported) {
            qInfo() << "Link match skipped: host does not support" << want << "Mbps";
            done(false);
            return;
        }

        m_TargetMbps = want;
        m_FromMbps   = hostMbps;
        emit stage(QStringLiteral("%1 → %2").arg(formatMbps(hostMbps), formatMbps(want)));

        qInfo() << "Link match: asking host to go from" << hostMbps << "to" << want << "Mbps";

        m_Bridge.sendSetSpeed(m_HostAddress, want, [this](const QString& ack) {
            if (ack != QStringLiteral("OK")) {
                qWarning() << "Link match refused by host:" << (ack.isEmpty() ? "no reply" : ack);
                done(false, tr("Couldn't change the host link speed — starting anyway"));
                return;
            }
            m_PollsLeft = MaxWaitMs / PollIntervalMs;
            m_PollTimer.start();
        });
    });
}

void LinkMatcher::poll()
{
    if (m_Finished) return;

    if (--m_PollsLeft < 0) {
        // Only reachable when the host never came back at all — every other outcome exits
        // through the callback below, with the link up.
        qWarning() << "Link match timed out: host never became reachable again";
        done(false, tr("The host didn't come back in time — starting anyway"));
        return;
    }

    m_Bridge.requestNetInfo(m_HostAddress, [this](const QString& reply) {
        // No reply: the adapter is down mid-renegotiation, so the host is simply
        // unreachable. That is the feature working, not failing — keep waiting.
        if (m_Finished || reply.isEmpty()) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
        if (!doc.isObject()) return;
        QJsonObject o = doc.object();

        const QString state = o.value(QStringLiteral("state")).toString();

        // "changing" means the host is still working. Never give up here: the whole point
        // of waiting is to avoid launching while the link is down, and the host's own
        // settle timeout will resolve this into idle or error soon enough.
        if (state == QStringLiteral("changing")) return;

        if (state == QStringLiteral("error")) {
            // The link is up again, just not where we asked — safe to launch.
            done(false, tr("The host couldn't change its link speed — starting anyway"));
            return;
        }
        if (state != QStringLiteral("idle")) return;

        const quint64 now = static_cast<quint64>(o.value(QStringLiteral("current_mbps")).toDouble());
        if (now == m_TargetMbps) {
            qInfo() << "Link match complete: host now at" << now << "Mbps";
            done(true);
            return;
        }

        // Idle at the wrong speed: the change finished without achieving the target. The
        // adapter is up, so launching now is safe — and waiting longer would achieve nothing.
        if (now > 0) {
            qWarning() << "Link match: host settled at" << now << "Mbps, wanted" << m_TargetMbps;
            done(false, tr("The host link stayed at %1 — starting anyway").arg(formatMbps(now)));
        }
    });
}
