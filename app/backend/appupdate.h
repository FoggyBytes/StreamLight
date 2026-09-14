#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

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
 * <li><b>The installer runs visibly</b>, with no switches. ArtMoon ran it /VERYSILENT, and so did
 *     this until the first runtime test (14/09/2026): silent, the user saw none of the steps, and
 *     the app could only come back through a [Run] entry of our own. Visible, Setup's own
 *     "Launch StreamLight" box on its last page reopens it — the entry that already runs as the
 *     original user after a manual install. The silent relaunch entry and its /SELFUPDATE
 *     switch were removed from StreamLight.iss with it.</li>
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

private:
    bool m_InstallBlocked = false;

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
