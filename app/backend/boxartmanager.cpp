#include "boxartmanager.h"
#include "../path.h"

#include <QImageReader>
#include <QImageWriter>

BoxArtManager::BoxArtManager(QObject *parent) :
    QObject(parent),
    m_BoxArtDir(Path::getBoxArtCacheDir()),
    m_ThreadPool(this)
{
    // 4 is a good balance between fast loading for large
    // app grids and not crushing GFE with tons of requests
    // and causing UI jank from constantly stalling to decode
    // new images.
    m_ThreadPool.setMaxThreadCount(4);
    if (!m_BoxArtDir.exists()) {
        m_BoxArtDir.mkpath(".");
    }
}

namespace {

// Where a cover lives, and the only place that answers it. Composition only: it creates
// nothing, so a caller that is merely asking whether we already have one does not leave a
// trail of empty directories behind.
QString boxArtFilePath(NvComputer* computer, int appId)
{
    return QDir(Path::getBoxArtCacheDir())
            .filePath(computer->uuid + QLatin1Char('/') + QString::number(appId) + ".png");
}

} // namespace

QString
BoxArtManager::getFilePathForBoxArt(NvComputer* computer, int appId)
{
    QDir dir(Path::getBoxArtCacheDir());

    // Create the cache directory if it did not already exist
    if (!dir.exists(computer->uuid)) {
        dir.mkpath(computer->uuid);
    }

    return boxArtFilePath(computer, appId);
}

QUrl
BoxArtManager::cachedBoxArt(NvComputer* computer, int appId)
{
    QFile file(boxArtFilePath(computer, appId));
    if (!file.exists() || file.size() == 0) {
        return QUrl();
    }

    return QUrl::fromLocalFile(file.fileName());
}

class NetworkBoxArtLoadTask : public QObject, public QRunnable
{
    Q_OBJECT

public:
    NetworkBoxArtLoadTask(BoxArtManager* boxArtManager, NvComputer* computer, NvApp& app)
        : m_Bam(boxArtManager),
          m_Computer(computer),
          m_App(app)
    {
        connect(this, &NetworkBoxArtLoadTask::boxArtFetchCompleted,
                boxArtManager, &BoxArtManager::handleBoxArtLoadComplete);
    }

signals:
    void boxArtFetchCompleted(NvComputer* computer, NvApp app, QUrl image);

private:
    void run()
    {
        QUrl image = m_Bam->loadBoxArtFromNetwork(m_Computer, m_App.id);
        if (image.isEmpty()) {
            // Give it another shot if it fails once
            image = m_Bam->loadBoxArtFromNetwork(m_Computer, m_App.id);
        }
        emit boxArtFetchCompleted(m_Computer, m_App, image);
    }

    BoxArtManager* m_Bam;
    NvComputer* m_Computer;
    NvApp m_App;
};

/*
 * Whether the cover already on disk should be fetched again.
 *
 * Nothing in this cache ever expired: loadBoxArt() returned the file if it existed, so a
 * cover corrected on the host never reached a client that had already stored the old one —
 * the only thing that ever cleared it was deleting the host. That left covers from before
 * the host learned to look for portrait artwork on display indefinitely.
 *
 * The size is read from the image header, not by decoding it, and the answer is remembered
 * for the rest of the run either way, so this costs one small read per game per launch and
 * nothing afterwards.
 */
bool BoxArtManager::needsRefresh(const QString& path)
{
    QMutexLocker lock(&m_CheckLock);

    if (m_Checked.contains(path)) {
        return false;
    }
    m_Checked.insert(path);

    QImageReader reader(path);
    QSize size = reader.size();

    // An unreadable header is not something a re-fetch is likely to fix, and re-fetching on
    // it would turn a corrupt file into a download on every launch.
    return size.isValid() && size.height() < MinCoverHeight;
}

QUrl BoxArtManager::loadBoxArt(NvComputer* computer, NvApp& app)
{
    // Try to open the cached file if it exists and contains data
    QFile cacheFile(getFilePathForBoxArt(computer, app.id));
    if (cacheFile.exists() && cacheFile.size() > 0) {
        if (needsRefresh(cacheFile.fileName())) {
            m_ThreadPool.start(new NetworkBoxArtLoadTask(this, computer, app));

            // Keep showing what we have while the better one downloads — a small cover
            // beats the placeholder — but hand back a URL that differs from the one the
            // next call will produce.
            //
            // ⚠️ That difference is the whole mechanism. QML caches a pixmap against its
            // full URL, so a file replaced under the same name is never re-read and the
            // refreshed cover would only appear on the next launch. When the download
            // lands, boxArtLoadComplete makes the view ask again; needsRefresh() has
            // already recorded this path, so that call returns the bare file URL — a
            // different URL, hence a reload. The query itself is dropped when the URL is
            // turned back into a path, so the same file is opened either way.
            //
            // Deliberately confined to this branch: a cover that does not need refreshing
            // is served exactly as before, so if this ever stops working it can only
            // affect covers that were already wrong.
            QUrl url = QUrl::fromLocalFile(cacheFile.fileName());
            url.setQuery(QStringLiteral("refresh=1"));
            return url;
        }
        return QUrl::fromLocalFile(cacheFile.fileName());
    }

    // If we get here, we need to fetch asynchronously.
    // Kick off a worker on our thread pool to do just that.
    NetworkBoxArtLoadTask* netLoadTask = new NetworkBoxArtLoadTask(this, computer, app);
    m_ThreadPool.start(netLoadTask);

    // Return the placeholder then we can notify the caller
    // later when the real image is ready.
    return QUrl("qrc:/res/no_app_image.png");
}

void BoxArtManager::deleteBoxArt(NvComputer* computer)
{
    QDir dir(Path::getBoxArtCacheDir());

    // Delete everything in this computer's box art directory
    if (dir.cd(computer->uuid)) {
        dir.removeRecursively();
    }
}

void BoxArtManager::handleBoxArtLoadComplete(NvComputer* computer, NvApp app, QUrl image)
{
    if (!image.isEmpty()) {
        emit boxArtLoadComplete(computer, app, image);
    }
}

QUrl BoxArtManager::loadBoxArtFromNetwork(NvComputer* computer, int appId)
{
    NvHTTP http(computer);

    QString cachePath = getFilePathForBoxArt(computer, appId);
    QImage image;
    try {
        image = http.getBoxArt(appId);
    } catch (...) {}

    // Cache the box art on disk if it loaded
    if (!image.isNull()) {
        if (image.save(cachePath)) {
            return QUrl::fromLocalFile(cachePath);
        }
        else {
            // A failed save() may leave a zero byte file. Make sure that's removed.
            QFile(cachePath).remove();
        }
    }

    return QUrl();
}

#include "boxartmanager.moc"
