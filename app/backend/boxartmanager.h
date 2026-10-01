#pragma once

#include "computermanager.h"
#include <QDir>
#include <QImage>
#include <QThreadPool>
#include <QRunnable>
#include <QMutex>
#include <QSet>
#include <QString>

class BoxArtManager : public QObject
{
    Q_OBJECT

    friend class NetworkBoxArtLoadTask;

public:
    explicit BoxArtManager(QObject *parent = nullptr);

    QUrl
    loadBoxArt(NvComputer* computer, NvApp& app);

    /**
     * The cover already on disk for this app, or an empty QUrl when there is none.
     *
     * Unlike loadBoxArt() this never fetches and never hands back the placeholder: it
     * answers "do we already have this one" and nothing else. That is what a caller
     * needs when it has something of its own to fall back on and would rather show
     * that than a grey rectangle or a download it did not ask for.
     *
     * Static because the caller — the last-session panel on the home screen — has a
     * computer and an app id but no reason to own a BoxArtManager: it is not browsing
     * a library, it is decorating a summary.
     */
    static
    QUrl
    cachedBoxArt(NvComputer* computer, int appId);

    static
    void
    deleteBoxArt(NvComputer* computer);

signals:
    void
    boxArtLoadComplete(NvComputer* computer, NvApp app, QUrl image);

public slots:

private slots:
    void
    handleBoxArtLoadComplete(NvComputer* computer, NvApp app, QUrl image);

private:
    QUrl
    loadBoxArtFromNetwork(NvComputer* computer, int appId);

    // Static so cachedBoxArt() can reach it without an instance. It derives the whole
    // path from Path::getBoxArtCacheDir(), exactly as it always did — the member QDir it
    // used to copy was that same directory. Keeping this the single definition of where a
    // cover lives is the point: a second copy of the convention elsewhere is how the two
    // drift apart.
    static
    QString
    getFilePathForBoxArt(NvComputer* computer, int appId);

    bool
    needsRefresh(const QString& path);

    void
    startFetch(NvComputer* computer, NvApp& app);

    // A cover shorter than this is treated as a thumbnail rather than the real artwork.
    // The host's own sources bottom out at 600 (Steam's library capsule is 600x900), so
    // this accepts everything they produce and catches what predates them: a cache filled
    // before StreamTweak 8.1.1 holds square launcher logos and landscape keyart, and
    // nothing here ever expired, so those covers would have been shown for ever.
    static constexpr int MinCoverHeight = 600;

    QDir m_BoxArtDir;
    QThreadPool m_ThreadPool;

    // Covers this run has already looked at. Without it a game whose cover really is
    // small at every source would be re-downloaded on every visit to the host page.
    // ⚠️ Static (6.4.0): every visit to the host page builds a new AppModel, and with it a
    // new BoxArtManager, so a member set was empty on each visit and did the opposite of
    // what this comment promised — the small cover was fetched again every time.
    static QSet<QString> s_Checked;
    static QMutex s_CheckLock;

    // Covers this manager is downloading right now. The list and the running-game lookup
    // ask for the same app within the same frame, and two downloads of one cover used to
    // write the same file while the view was reading it. GUI thread only: loadBoxArt() runs
    // from the model and the completion slot is queued back to it.
    QSet<QString> m_InFlight;
};
