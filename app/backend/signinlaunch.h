#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QQmlEngine>

class QTimer;

/**
 * StreamLight at Windows sign-in, with a choice (8.2.0, 28/10/2026).
 *
 * <p>With the switch on, a scheduled task starts StreamLight when the user signs in, with
 * {@code --sign-in}. The opening animation then stops on two buttons instead of fading into
 * Home: Start StreamLight (A) or Windows desktop (B), and after 10 s untouched, StreamLight.
 * Marcello's idea, "a sort of Xbox mode"; mockup B «Sotto il nome» in docs/mockups/avvio/.</p>
 *
 * <p>⚠️ Not a shell replacement. Explorer is already up underneath, so "Windows desktop" only
 * means that StreamLight closes. Replacing the shell (Winlogon\Shell, Shell Launcher) was
 * ruled out: Enterprise/Education only, and a crash at sign-in would leave a black screen.</p>
 *
 * <p>⚠️⚠️ {@code holding}: until the user picks StreamLight, nothing may reach a host — no
 * polling, no mDNS, no bridge. A NIC with wake on pattern match wakes its PC on the first
 * directed packet (§77), so someone who picked the desktop would find the host switched on.
 * main.qml does not start polling and AppShell does not load Home while this is true.</p>
 *
 * <p>Why a scheduled task and not the Run key: the Run key is started by Explorer, at the end
 * of its startup queue, and the desktop shows for several seconds first; a logon task starts
 * ahead of Explorer. The task's settings that are not left to their defaults are the ones
 * StreamTweak's priority start paid for (§66) — see registerTask().</p>
 *
 * <p>⚠️ {@code waitingForUnlock} (28/10/2026, reported by Marcello on the first build): the
 * logon trigger fires when the session LOGS ON, and a session can log on behind the lock
 * screen — automatic sign-in, or Windows finishing an update restart by itself (ARSO). The
 * animation and the 10 s countdown then ran where nobody could see them, and StreamLight was
 * chosen for the user. So a sign-in launch that finds the session locked waits, polling the
 * session's state twice a second, and the splash starts only once it is unlocked.</p>
 */
class SignInLaunch : public QObject
{
    Q_OBJECT

    // The task is registered and enabled. Read from Task Scheduler itself, never from a
    // setting: the user can delete or disable the task behind our back.
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    // Why the last setEnabled() failed, for Settings. Empty when it did not.
    Q_PROPERTY(QString lastError READ lastError NOTIFY enabledChanged)
    // This process was started by the task.
    Q_PROPERTY(bool launchedAtSignIn READ launchedAtSignIn CONSTANT)
    // The choice is still on screen: no connection to any host yet.
    Q_PROPERTY(bool holding READ holding NOTIFY holdingChanged)
    // Started at sign-in behind the lock screen: nothing starts until the session is unlocked.
    Q_PROPERTY(bool waitingForUnlock READ waitingForUnlock NOTIFY waitingForUnlockChanged)

public:
    static SignInLaunch* get(QQmlEngine* engine = nullptr);

    // Called by main() before the QML engine exists, from the command line.
    static void setLaunchedAtSignIn(bool atSignIn);

    // The argument the task passes.
    static const char* argument();

    bool enabled() const { return m_Enabled; }
    QString lastError() const { return m_LastError; }
    bool launchedAtSignIn() const;
    bool holding() const { return m_Holding; }
    bool waitingForUnlock() const { return m_WaitingForUnlock; }

    // Registers or deletes the task. False, with lastError set, when it could not.
    Q_INVOKABLE bool setEnabled(bool enabled);

    // The user picked StreamLight: hosts may be contacted from now on.
    Q_INVOKABLE void release();

signals:
    void enabledChanged();
    void holdingChanged();
    void waitingForUnlockChanged();

private:
    explicit SignInLaunch(QObject* parent = nullptr);

    void checkUnlocked();

    bool m_Enabled = false;
    bool m_Holding = false;
    bool m_WaitingForUnlock = false;
    QTimer* m_UnlockPoll = nullptr;
    QElapsedTimer m_LockedSince;
    QString m_LastError;
};
