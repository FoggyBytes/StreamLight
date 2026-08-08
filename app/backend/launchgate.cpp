#include "launchgate.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace
{
    // Four polls a second would be pointless: the host samples the desktop every 250 ms and
    // nothing downstream reacts faster than a person can see. Two and a half a second keeps
    // the reveal tight while leaving the bridge alone.
    constexpr int PollIntervalMs = 400;

    // Hard ceiling of our own, on top of the host's. The host reports its limit (90 s) and
    // resolves the wait itself in every path we know of, but "the host stopped answering
    // halfway" is not a state it can report — so the client keeps its own clock. Generous
    // enough never to pre-empt the host's own timeout.
    constexpr int MaxWaitMs = 120000;

    // How many unanswered polls before we conclude nobody is listening. A couple of misses
    // right after the stream starts are normal — the host is at its busiest — but a host
    // without StreamTweak, or one too old to know GAMESTATE, never answers at all and the
    // curtain must not wait on it.
    constexpr int MaxMisses = 5;

    LaunchPhase parsePhase(const QString& s)
    {
        if (s == QStringLiteral("idle"))            return LaunchPhase::Idle;
        if (s == QStringLiteral("launching"))       return LaunchPhase::Launching;
        if (s == QStringLiteral("game_window"))     return LaunchPhase::GameWindow;
        if (s == QStringLiteral("ready"))           return LaunchPhase::Ready;
        if (s == QStringLiteral("needs_attention")) return LaunchPhase::NeedsAttention;
        if (s == QStringLiteral("timeout"))         return LaunchPhase::Timeout;
        if (s == QStringLiteral("not_applicable"))  return LaunchPhase::NotApplicable;
        return LaunchPhase::Unknown;
    }

    const char* phaseName(LaunchPhase p)
    {
        switch (p) {
        case LaunchPhase::Idle:           return "idle";
        case LaunchPhase::Launching:      return "launching";
        case LaunchPhase::GameWindow:     return "game_window";
        case LaunchPhase::Ready:          return "ready";
        case LaunchPhase::NeedsAttention: return "needs_attention";
        case LaunchPhase::Timeout:        return "timeout";
        case LaunchPhase::NotApplicable:  return "not_applicable";
        default:                          return "unknown";
        }
    }
}

LaunchGate::LaunchGate(QObject* parent)
    : QObject(parent)
{
    m_PollTimer.setInterval(PollIntervalMs);
    m_PollTimer.setSingleShot(false);
    connect(&m_PollTimer, &QTimer::timeout, this, &LaunchGate::poll);
}

void LaunchGate::start(const QString& hostAddress)
{
    if (m_Finished || m_PollTimer.isActive()) return;

    m_HostAddress = hostAddress;
    if (m_HostAddress.isEmpty()) {
        finish(LaunchPhase::Unknown, "no host address");
        return;
    }

    m_PollsLeft = MaxWaitMs / PollIntervalMs;
    m_PollTimer.start();
    poll();   // don't wait a whole interval to ask the first time
}

void LaunchGate::stop()
{
    m_PollTimer.stop();
    m_Finished = true;
}

void LaunchGate::finish(LaunchPhase phase, const char* reason)
{
    if (m_Finished) return;
    m_Finished = true;
    m_PollTimer.stop();

    qInfo() << "Launch gate: revealing —" << reason << "(phase" << phaseName(phase) << ")";
    emit finished(phase);
}

void LaunchGate::poll()
{
    if (m_Finished) return;

    if (--m_PollsLeft < 0) {
        // Only reachable if the host answers but never reaches a terminal phase, which would
        // mean its own 90 s timeout didn't fire either. Nothing left to wait for.
        finish(LaunchPhase::Timeout, "client-side ceiling reached");
        return;
    }

    // One request in flight at a time: on a busy host the reply can take longer than the
    // interval, and queueing them up would only make it worse.
    if (m_Pending) return;
    m_Pending = true;

    m_Bridge.requestGameState(m_HostAddress, [this](const QString& reply) {
        m_Pending = false;
        if (m_Finished) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
        if (reply.isEmpty() || !doc.isObject()) {
            // No answer, "ERR" from a host that doesn't know the command, or something we
            // can't read. Tolerate a few — the host is at its busiest right when the stream
            // starts — then stop waiting on a conversation that isn't happening.
            if (++m_Misses >= MaxMisses && !m_EverAnswered) {
                finish(LaunchPhase::Unknown, "host never answered GAMESTATE");
            }
            return;
        }

        m_EverAnswered = true;
        m_Misses = 0;

        const QJsonObject o = doc.object();
        const LaunchPhase phase = parsePhase(o.value(QStringLiteral("phase")).toString());
        const QString foreground = o.value(QStringLiteral("foreground")).toString();
        const qint64 elapsedMs = static_cast<qint64>(o.value(QStringLiteral("elapsed_ms")).toDouble());
        m_LimitMs = static_cast<qint64>(o.value(QStringLiteral("limit_ms")).toDouble());

        if (phase != m_Phase) {
            m_Phase = phase;
            qInfo().nospace() << "Launch gate: " << phaseName(phase) << " at "
                              << (elapsedMs / 1000.0) << "s"
                              << (foreground.isEmpty() ? QString() : QStringLiteral(" (foreground: %1)").arg(foreground));
            emit phaseChanged(phase, foreground, elapsedMs);
        }

        switch (phase) {
        case LaunchPhase::Ready:
            finish(phase, "the game is on screen");
            break;
        case LaunchPhase::NeedsAttention:
            // The host is asking for a click we would otherwise be covering up. The host keeps
            // watching and may reach Ready afterwards, but by then the user is already looking
            // at the screen, so there is nothing left for the curtain to do.
            finish(phase, "the host needs the user");
            break;
        case LaunchPhase::Timeout:
            finish(phase, "the host gave up waiting");
            break;
        case LaunchPhase::NotApplicable:
            // The Desktop entry: the desktop *is* the content. Waiting for a game window here
            // would hide the very thing the user asked for until the ceiling ran out.
            finish(phase, "nothing to wait for");
            break;
        case LaunchPhase::Idle:
            // The host isn't watching anything. Either it never saw a launch, or its session
            // bookkeeping cleared the one we were following — which is a host-side fault, not
            // something to wait out. Named separately from Unknown so the log says which.
            finish(phase, "the host is not watching a launch");
            break;
        case LaunchPhase::Unknown:
            // A phase name we don't recognise — a newer host, most likely. Better to show the
            // stream than to sit behind a curtain we can no longer reason about.
            finish(phase, "unrecognised phase from host");
            break;
        default:
            break;   // launching / game_window: keep waiting
        }
    });
}
