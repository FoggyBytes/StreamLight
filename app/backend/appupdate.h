#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

/**
 * Release lookup and self-update, for the About tab and the startup prompt.
 *
 * <p><b>Derived from ArtMoon's appupdate</b> (app/backend/appupdate.h / .cpp) by Onaiaku —
 * https://github.com/onaiaku/ArtMoon, commits 9ebfc60, 1418d3f and 6ed4df2. Both projects are
 * GPLv3. What was kept: the shape of the thing — look up releases/latest, pick the installer
 * out of the same response's assets, download it to the temp folder, run it and quit.
 * What changed, and why:</p>
 *
 * <ul>
 * <li>Windows only. ArtMoon's Linux branch (a terminal running install.sh) is gone.</li>
 * <li>The asset is matched by pattern, not by a literal name: ours carries the version
 *     (StreamLight_5.8.0_Installer.exe, OutputBaseFilename in StreamLight.iss), and the version in
 *     the name must be the tag's own.</li>
 * <li>The download is checked against the SHA-256 GitHub publishes for the asset, and refused
 *     when GitHub publishes none — then checked again just before it runs. The file runs
 *     elevated; it had better be the one uploaded.</li>
 * <li><b>Two presses, not one.</b> Update now downloads and verifies; Install now runs it.
 *     ArtMoon installed the moment the download ended, and so did the first version of this,
 *     which is how it came to quit the app into a stream, into a wake in progress, and past the
 *     question that puts the host's link back — the host holds the streaming speed until asked,
 *     so an app that quits before asking leaves it there. Every one of those came from the app
 *     choosing the moment; the user pressing a button in Settings is the only moment used now,
 *     and HomeScreen can still hold it (installBlocked).</li>
 * <li><b>The installer is run by StreamLightUpdater.exe (8.6.0)</b>, not by the app. From 5.8.0
 *     to 8.5.x Install now ran Setup visibly, and its wizard and the Windows prompt before it
 *     wanted a mouse and keyboard, the one place a controller could not go. Now the app starts
 *     the scheduled task "StreamLight Update", which the installer registered with the user's
 *     highest rights — or, with no usable task, the same updater through the ordinary prompt,
 *     said beforehand on screen (PhaseAsk). The updater checks everything again itself, takes
 *     over the screen (it redraws this app's last frame from look.json, see writeLook), closes
 *     nothing it did not have to, runs Setup /VERYSILENT and reopens the app as the user. The
 *     design and its checks are in SECURITY.md; the updater is updater/ in this repository.
 *     ⚠️ This replaces the decision of 14/09/2026 (installer visible, §71.4): Marcello asked
 *     for the same screen with the pad and with mouse and keyboard (07/11/2026).</li>
 * <li>A failure stays in the app and says what went wrong. ArtMoon opened the releases page and
 *     quit on every error, which from the user's side is the app disappearing.</li>
 * <li>Streamed to disk with progress, rather than read whole into memory, with a stall watchdog
 *     of our own (see the note on m_StallWatch).</li>
 * <li>The StreamTweak tag, which the About and StreamTweak tabs also show, is looked up here too:
 *     the QML used to do both lookups with its own XMLHttpRequest, and two copies of the same
 *     request would have been the result of leaving it there.</li>
 * <li>A startup prompt (AppShell, UpdatePromptDialog) offers a newer release once per launch,
 *     unless the user asked not to be reminded of that version — shouldPrompt() and
 *     skipLatestVersion().</li>
 * </ul>
 */
class AppUpdate : public QObject
{
    Q_OBJECT

public:
    enum State {
        Idle,           // nothing running; the button (if shown) offers the update
        Checking,       // releases/latest for StreamLight is in flight
        Downloading,
        Ready,          // installer downloaded and verified; the button offers to install it
        Launching,      // installer started, the app is on its way out
        Failed          // updateNow() gave up; failureReason says why
    };
    Q_ENUM(State)

    /**
     * Where the update screen is (8.6.0, UpdateScreen.qml), from Install now until the app
     * closes, or until the user goes back to Settings:
     *   PhaseCheck      the updater is checking the release and its copy of the installer;
     *   PhaseAsk        there is no usable task: Windows will ask, and a pad cannot answer it;
     *   PhaseClosing    the updater is ready — the screen freezes and hands over (writeLook);
     *   PhaseFailed     stopped before anything was installed; installMessage says why;
     *   PhaseCancelled  the user went back.
     */
    enum InstallPhase { PhaseNone, PhaseCheck, PhaseAsk, PhaseClosing, PhaseFailed, PhaseCancelled };
    Q_ENUM(InstallPhase)

    Q_PROPERTY(InstallPhase installPhase READ installPhase NOTIFY installPhaseChanged)
    Q_PROPERTY(QString installMessage READ installMessage NOTIFY installPhaseChanged)
    /** "8.5.1" and "8.6.0", for the screen. */
    Q_PROPERTY(QString installFrom READ installFrom CONSTANT)
    Q_PROPERTY(QString installTo READ installTo NOTIFY latestChanged)

    /**
     * After the updater reopened the app: "Updated to StreamLight 8.6.0", or why it did not
     * install. Empty otherwise, and once acknowledgeAfterUpdate() has been called.
     */
    Q_PROPERTY(QString afterUpdateNotice READ afterUpdateNotice NOTIFY afterUpdateNoticeChanged)

    /** Latest release tags as GitHub reports them ("v5.8.0"); empty until known. */
    Q_PROPERTY(QString latestStreamLight READ latestStreamLight NOTIFY latestChanged)
    Q_PROPERTY(QString latestStreamTweak READ latestStreamTweak NOTIFY latestChanged)

    /** The latest StreamLight release is newer than this build. False while unknown. */
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY latestChanged)

    Q_PROPERTY(State state READ state NOTIFY stateChanged)

    /** Download progress, 0–100; -1 when the size is not known. */
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)

    /** Short, user-facing; only meaningful in the Failed state. */
    Q_PROPERTY(QString failureReason READ failureReason NOTIFY stateChanged)

    /**
     * Something on Home would be stranded if the app quit now: a wake in progress (its last step
     * is a link match), a host link being changed or put back, or the "put the link back?"
     * question still to be asked after a stream — the host holds the streaming speed until
     * asked, so quitting past that question leaves it there. Written by HomeScreen, which owns
     * all of that state; Install now waits while it is true.
     */
    Q_PROPERTY(bool installBlocked READ installBlocked WRITE setInstallBlocked NOTIFY installBlockedChanged)

    explicit AppUpdate(QObject* parent = nullptr);
    ~AppUpdate() override;

    QString latestStreamLight() const { return m_LatestStreamLight; }
    QString latestStreamTweak() const { return m_LatestStreamTweak; }
    bool updateAvailable() const;
    State state() const { return m_State; }
    int progress() const { return m_Progress; }
    QString failureReason() const { return m_FailureReason; }
    bool installBlocked() const { return m_InstallBlocked; }
    void setInstallBlocked(bool blocked);
    InstallPhase installPhase() const { return m_Phase; }
    QString installMessage() const { return m_PhaseMessage; }
    QString installFrom() const;
    QString installTo() const;
    QString afterUpdateNotice() const { return m_AfterUpdateNotice; }

    /** PhaseAsk, A: start the updater through the Windows prompt. */
    Q_INVOKABLE void confirmPermission();
    /** PhaseAsk or PhaseCheck, B: back to Settings, nothing installed. */
    Q_INVOKABLE void cancelInstall();
    /** PhaseFailed or PhaseCancelled, A: the screen goes, Settings shows the outcome. */
    Q_INVOKABLE void closeInstallScreen();
    /**
     * PhaseClosing: the screen as it stands, for the updater to redraw (look.json in the user's
     * update folder). `look` is in the window's logical pixels; this turns it into physical
     * pixels and adds where the window is. Only numbers and colours: see updater/screen.h.
     */
    Q_INVOKABLE void writeLook(QObject* window, const QVariantMap& look);
    Q_INVOKABLE void acknowledgeAfterUpdate();

    /** Looks up the latest StreamLight and StreamTweak releases. Ignored mid-update. */
    Q_INVOKABLE void checkLatest();

    /**
     * The button's one action, by state: downloads and verifies the installer (Idle, Checking,
     * Failed), or runs the one already verified and quits (Ready).
     */
    Q_INVOKABLE void updateNow();

    /**
     * Whether the startup prompt should offer the latest release: it is newer than this build,
     * it has an installer to update with, and the user has not asked not to be reminded of it
     * (or of anything newer).
     */
    Q_INVOKABLE bool shouldPrompt() const;

    /** "Don't remind me" for the latest release; a newer one than that is offered again. */
    Q_INVOKABLE void skipLatestVersion();

    /**
     * "5.7.1" style compare: negative if a < b, 0 if equal, positive if a > b. Tolerates a
     * leading "v", surrounding whitespace and a UTF-8 BOM (app/version.txt has carried one).
     * An unreadable version compares as 0 — never as "older", which would offer an update.
     */
    Q_INVOKABLE static int compareVersions(const QString& a, const QString& b);

signals:
    void latestChanged();
    void stateChanged();
    void progressChanged();
    void installBlockedChanged();
    void installPhaseChanged();
    void afterUpdateNoticeChanged();

private:
    bool m_InstallBlocked = false;

    // ---- the updater (8.6.0) ----
    void setPhase(InstallPhase phase, const QString& message = QString());
    void startUpdater();
    void pollStatus();
    void readAfterUpdate();
    static QString statusFile();
    static QString messageFor(const QString& reason);

    InstallPhase m_Phase = PhaseNone;
    QString m_PhaseMessage;
    QString m_AfterUpdateNotice;
    qint64 m_AfterUpdateStarted = 0;
    QTimer m_StatusPoll;
    qint64 m_TriggeredAt = 0;       // ms since the epoch, when the updater was asked to start
    qint64 m_PhaseSince = 0;

    struct Asset {
        QString url;
        QString name;
        QByteArray sha256;      // lower-case hex; empty when GitHub published no digest
    };

    static QList<int> parseVersion(const QString& version);
    static QString installedVersion();
    static QString downloadDir();
    static void clearDownloads();

    // Holding a download, finished or not: the release and the file must stay as they are.
    bool holdsDownload() const { return m_State == Downloading || m_State == Ready || m_State == Launching; }

    QNetworkReply* getJson(const QString& repo);
    void handleStreamLight(QNetworkReply* reply);
    void handleStreamTweak(QNetworkReply* reply);
    void startDownload();
    void handleDownloadData();
    void handleDownloadFinished();
    void checkStall();
    void launchInstaller();
    void setState(State state, const QString& failureReason = QString());
    void fail(const QString& reason);

    QNetworkAccessManager* m_Nam;
    QString m_LatestStreamLight;
    QString m_LatestStreamTweak;
    Asset m_Asset;
    bool m_HasAsset = false;

    State m_State = Idle;
    int m_Progress = -1;
    QString m_FailureReason;

    QNetworkReply* m_Download = nullptr;
    QSaveFile* m_File = nullptr;
    bool m_WriteFailed = false;
    bool m_Stalled = false;
    QCryptographicHash m_Hash{QCryptographicHash::Sha256};
    QString m_InstallerPath;

    /*
     * Why not QNetworkRequest::setTransferTimeout. Its timer lives on the main thread's event
     * loop, and so does every progress event that would reset it — and during a stream that loop
     * is not running: Session::exec() sits in its own SDL loop. A download left going under a
     * game resumed with an overdue timer and a queue of data behind it, and could be aborted as
     * stalled in the instant the stream ended.
     *
     * So the watchdog tells the two apart: a tick that arrives far later than it was due means
     * the main thread was held, not that the network stopped, and the data clock starts over.
     */
    QTimer m_StallWatch;
    QElapsedTimer m_SinceData;
    QElapsedTimer m_SinceTick;
};
