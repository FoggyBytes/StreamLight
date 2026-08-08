#pragma once

#include "../StreamTweakBridge.h"

#include <QObject>
#include <QString>
#include <QTimer>

/**
 * How far along the host says the launch is. Mirrors StreamTweak's LaunchWatcher — the
 * wire values are the strings in GAMESTATE's "phase" field.
 */
enum class LaunchPhase
{
    Unknown,        ///< No answer, or a host that doesn't know the command. Show no curtain.
    Idle,           ///< The host isn't watching a launch: nothing to wait for, so reveal.
    Launching,      ///< The command ran; no window of the game yet.
    GameWindow,     ///< A window exists but isn't filling the screen (splash, launcher, intro).
    Ready,          ///< The game owns the screen. Reveal.
    NeedsAttention, ///< The store's own client is holding the screen — a login, an update, a dialog.
    Timeout,        ///< The host gave up waiting. Reveal anyway.
    NotApplicable   ///< Nothing to wait for: the Desktop entry has no command to run.
};

/**
 * Asks the host how the launch is going, so the client can hold its curtain up until the
 * game is actually on screen instead of dropping the user into a desktop that is still
 * reconfiguring itself.
 *
 * <p>Only the host can answer this. It watches for the launched game's window; the client
 * has no visibility into what is happening over there, and the time involved is not
 * guessable — the same game measured 9 s warm and 27 s cold on the same machine minutes
 * apart, which is exactly why this is a state machine and not a countdown.</p>
 *
 * <p>Every exit is a reveal. Ready, Timeout, NeedsAttention, NotApplicable, an unreachable
 * host, a host too old to know the command: all of them end with finished(), because a
 * curtain that outlives its reason is worse than the desktop it was hiding. The gate never
 * decides to keep waiting forever.</p>
 *
 * Lives on the main thread (it owns a QTimer); start() must be called from there.
 */
class LaunchGate : public QObject
{
    Q_OBJECT

public:
    explicit LaunchGate(QObject* parent = nullptr);

    /** Begins polling. Safe to call when the host address is empty (finishes at once). */
    void start(const QString& hostAddress);

    /** Stops polling without emitting finished(). Used when the session ends first. */
    void stop();

    bool isActive() const { return m_PollTimer.isActive(); }

    LaunchPhase phase() const { return m_Phase; }

signals:
    /**
     * The host moved to a new phase. `foreground` names the process holding the screen and
     * is only meaningful for NeedsAttention; `elapsedMs` is measured by the host from the
     * moment it saw the launch, which is a beat earlier than anything the client can time.
     */
    void phaseChanged(LaunchPhase phase, QString foreground, qint64 elapsedMs);

    /** The curtain must come down now, whatever the reason. Emitted exactly once. */
    void finished(LaunchPhase finalPhase);

private slots:
    void poll();

private:
    void finish(LaunchPhase phase, const char* reason);

    StreamTweakBridge m_Bridge;
    QTimer            m_PollTimer;
    QString           m_HostAddress;
    LaunchPhase       m_Phase        = LaunchPhase::Unknown;
    qint64            m_LimitMs      = 0;
    int               m_Misses       = 0;
    int               m_PollsLeft    = 0;
    bool              m_EverAnswered = false;
    bool              m_Finished     = false;
    bool              m_Pending      = false;
};
