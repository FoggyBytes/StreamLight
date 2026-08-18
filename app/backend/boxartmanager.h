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

    QString
    getFilePathForBoxArt(NvComputer* computer, int appId);

    bool
    needsRefresh(const QString& path);

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
    QSet<QString> m_Checked;
    QMutex m_CheckLock;
};
