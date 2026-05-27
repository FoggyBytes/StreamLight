#include "XboxTileArtwork.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

namespace {

// Relative to %LocalAppData%. The 8wekyb3d8bbwe family token belongs to
// Microsoft and is stable across Windows 11 versions of the Xbox app.
constexpr const char* kManifestRelative =
    "Packages/Microsoft.GamingApp_8wekyb3d8bbwe/LocalState/"
    "CustomLibraryManagement";

constexpr const char* kManifestFile = "CustomLibraryManagement.manifest";

// Embedded gradient tile (1024x1024 PNG, RGB), shipped via resources.qrc.
constexpr const char* kEmbeddedTile = ":/streamlightxbox.png";

// Static identifiers for proactive pre-population. The UUID is arbitrary —
// it just has to be unique to "StreamLight by FoggyBytes" and stable across
// installs / updates so we always find/update the same entry. Format must
// match the UUID-v4-style canonical pattern Xbox uses elsewhere.
constexpr const char* kStreamLightUuid     = "8b3f5d1e-9c2a-4e6f-a847-7b9d2c1e8a5f";
constexpr const char* kStreamLightImageId  = "9220176001457765510"; // numeric PNG basename
constexpr const char* kStreamLightTitle    = "StreamLight";

QString normalizeDir(const QString& path)
{
    QString p = QDir::toNativeSeparators(path);
    if (!p.endsWith(QLatin1Char('\\'))) {
        p.append(QLatin1Char('\\'));
    }
    return p;
}

// Diagnostic log: written under %TEMP%\streamlight-xbox-tile.log so users can
// share a single file if pre-population doesn't behave as expected. Append-only,
// timestamped, ~one line per major step. Best-effort; failures are swallowed.
void diagLog(const QString& line)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) return;
    QFile f(QDir(dir).filePath("streamlight-xbox-tile.log"));
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
       << "  " << line << '\n';
    f.close();
}

QByteArray readEmbeddedTile()
{
    QFile rc{QString::fromLatin1(kEmbeddedTile)};
    if (!rc.open(QIODevice::ReadOnly)) {
        return {};
    }
    return rc.readAll();
}

bool writeAtomic(const QString& path, const QByteArray& bytes)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[XboxTileArtwork] cannot open for write:" << path;
        return false;
    }
    if (f.write(bytes) != bytes.size()) {
        qWarning() << "[XboxTileArtwork] short write to:" << path;
        return false;
    }
    if (!f.commit()) {
        qWarning() << "[XboxTileArtwork] atomic commit failed:" << path;
        return false;
    }
    return true;
}

} // namespace

QString XboxTileArtwork::manifestPath()
{
#ifndef Q_OS_WIN
    return {};
#else
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty()) {
        return {};
    }
    return QDir(localAppData).filePath(
        QString::fromLatin1(kManifestRelative) +
        QLatin1Char('/') + QString::fromLatin1(kManifestFile));
#endif
}

QString XboxTileArtwork::imagesDir()
{
#ifndef Q_OS_WIN
    return {};
#else
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty()) {
        return {};
    }
    return QDir(localAppData).filePath(
        QString::fromLatin1(kManifestRelative) + QLatin1String("/Images"));
#endif
}

// =================================================================
// Layer 1 — Boot patch
// =================================================================

void XboxTileArtwork::applyIfRegistered()
{
#ifndef Q_OS_WIN
    return;
#else
    const QString mPath = manifestPath();
    if (mPath.isEmpty()) return;

    QFile mf(mPath);
    if (!mf.exists() || !mf.open(QIODevice::ReadOnly)) {
        // Xbox app not installed, or user never opened "My Apps".
        return;
    }
    const QByteArray manifestBytes = mf.readAll();
    mf.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject gameCache = doc.object().value(QLatin1String("gameCache")).toObject();
    if (gameCache.isEmpty()) {
        return;
    }

    const QFileInfo myFi(QCoreApplication::applicationFilePath());
    const QString myDir  = normalizeDir(myFi.absolutePath());
    const QString myExe  = myFi.fileName();

    QString imagePath;
    for (auto it = gameCache.constBegin(); it != gameCache.constEnd(); ++it) {
        if (it.key() == QLatin1String("version")) continue;
        const QJsonObject e = it.value().toObject();
        const QString installLoc = normalizeDir(e.value(QLatin1String("installLocation")).toString());
        const QString exeName    = e.value(QLatin1String("executableName")).toString();
        const QString candidate  = e.value(QLatin1String("imagePath")).toString();

        if (installLoc.compare(myDir, Qt::CaseInsensitive) == 0 &&
            exeName.compare(myExe, Qt::CaseInsensitive) == 0 &&
            !candidate.isEmpty()) {
            imagePath = candidate;
            break;
        }
    }

    if (imagePath.isEmpty()) {
        return;
    }

    const QByteArray newBytes = readEmbeddedTile();
    if (newBytes.isEmpty()) {
        qWarning() << "[XboxTileArtwork] embedded tile resource missing";
        return;
    }

    QFile existing(imagePath);
    if (existing.exists() && existing.open(QIODevice::ReadOnly)) {
        const QByteArray cur = existing.readAll();
        existing.close();
        if (cur == newBytes) {
            return; // already up to date
        }
    }

    if (writeAtomic(imagePath, newBytes)) {
        qInfo().nospace() << "[XboxTileArtwork] patched tile artwork at "
                          << imagePath << " (" << newBytes.size() << " bytes)";
    }
#endif
}

// =================================================================
// Layer 2 — Proactive pre-population (installer hook)
// =================================================================

void XboxTileArtwork::registerEntry()
{
#ifndef Q_OS_WIN
    return;
#else
    diagLog(QStringLiteral("registerEntry: enter exe=%1 user=%2 LOCALAPPDATA=%3")
            .arg(QCoreApplication::applicationFilePath(),
                 qEnvironmentVariable("USERNAME"),
                 qEnvironmentVariable("LOCALAPPDATA")));

    const QByteArray tileBytes = readEmbeddedTile();
    if (tileBytes.isEmpty()) {
        diagLog("registerEntry: ABORT — embedded tile resource missing");
        qWarning() << "[XboxTileArtwork] register: embedded tile missing";
        return;
    }

    const QString mPath = manifestPath();
    if (mPath.isEmpty()) {
        diagLog("registerEntry: ABORT — LOCALAPPDATA not set");
        qWarning() << "[XboxTileArtwork] register: no LOCALAPPDATA";
        return;
    }
    const QString imgDir = imagesDir();
    diagLog(QStringLiteral("registerEntry: manifest=%1 imagesDir=%2").arg(mPath, imgDir));

    // Ensure the directory tree exists (Xbox app creates it on first use,
    // but we might run before the user has ever opened the Xbox app).
    QDir().mkpath(QFileInfo(mPath).absolutePath());
    QDir().mkpath(imgDir);

    const QFileInfo myFi(QCoreApplication::applicationFilePath());
    const QString myDir = normalizeDir(myFi.absolutePath());
    const QString myExe = myFi.fileName();

    // Read existing manifest if any.
    QJsonObject manifestObj;
    QJsonObject gameCache;
    QJsonObject provider;
    {
        QFile mf(mPath);
        if (mf.exists() && mf.open(QIODevice::ReadOnly)) {
            const QByteArray raw = mf.readAll();
            mf.close();
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                manifestObj = doc.object();
                gameCache   = manifestObj.value(QLatin1String("gameCache")).toObject();
                provider    = manifestObj.value(QLatin1String("provider")).toObject();
            }
        }
    }
    if (!manifestObj.contains(QLatin1String("version"))) {
        manifestObj[QLatin1String("version")] = 1;
    }
    if (provider.isEmpty()) {
        provider[QLatin1String("version")] = 1;
        provider[QLatin1String("enabled")] = true;
    }
    manifestObj[QLatin1String("provider")] = provider;
    if (gameCache.isEmpty() || !gameCache.contains(QLatin1String("version"))) {
        gameCache[QLatin1String("version")] = 1;
    }

    // Try to find an existing entry that matches the current exe path
    // (installLocation + executableName). Preserve its id/imagePath if so;
    // otherwise use our static StreamLight identifiers.
    QString matchedKey;
    QJsonObject existingEntry;
    for (auto it = gameCache.constBegin(); it != gameCache.constEnd(); ++it) {
        if (it.key() == QLatin1String("version")) continue;
        const QJsonObject e = it.value().toObject();
        const QString iLoc = normalizeDir(e.value(QLatin1String("installLocation")).toString());
        const QString eName = e.value(QLatin1String("executableName")).toString();
        if (iLoc.compare(myDir, Qt::CaseInsensitive) == 0 &&
            eName.compare(myExe, Qt::CaseInsensitive) == 0) {
            matchedKey = it.key();
            existingEntry = e;
            break;
        }
    }

    const QString uuid = !matchedKey.isEmpty()
        ? matchedKey
        : QString::fromLatin1(kStreamLightUuid);

    QString imagePath = existingEntry.value(QLatin1String("imagePath")).toString();
    if (imagePath.isEmpty()) {
        imagePath = QDir(imgDir).filePath(
            QString::fromLatin1(kStreamLightImageId) + QLatin1String(".png"));
        imagePath = QDir::toNativeSeparators(imagePath);
    }

    QJsonObject entry;
    entry[QLatin1String("id")] = uuid;
    // Preserve original addedDate if we already had an entry, otherwise now.
    if (existingEntry.contains(QLatin1String("addedDate"))) {
        entry[QLatin1String("addedDate")] = existingEntry[QLatin1String("addedDate")];
    } else {
        entry[QLatin1String("addedDate")] =
            QString::number(QDateTime::currentMSecsSinceEpoch());
    }
    entry[QLatin1String("imagePath")]            = imagePath;
    entry[QLatin1String("title")]                = QString::fromLatin1(kStreamLightTitle);
    entry[QLatin1String("installLocation")]      = QDir::toNativeSeparators(myDir);
    entry[QLatin1String("executableName")]       = myExe;
    entry[QLatin1String("executableCommandArgs")] = QString();

    gameCache[uuid] = entry;
    manifestObj[QLatin1String("gameCache")] = gameCache;

    // Write the PNG first, then the manifest. If either fails the other is
    // harmless (PNG without manifest entry is unreferenced; manifest without
    // PNG would fall back to grey, same as today).
    if (!writeAtomic(imagePath, tileBytes)) {
        diagLog(QStringLiteral("registerEntry: ABORT — PNG write failed at %1").arg(imagePath));
        return;
    }

    const QJsonDocument outDoc(manifestObj);
    if (!writeAtomic(mPath, outDoc.toJson(QJsonDocument::Compact))) {
        diagLog(QStringLiteral("registerEntry: ABORT — manifest write failed at %1").arg(mPath));
        return;
    }

    diagLog(QStringLiteral("registerEntry: OK — uuid=%1 image=%2 (%3 bytes)")
            .arg(uuid).arg(imagePath).arg(tileBytes.size()));
    qInfo().nospace() << "[XboxTileArtwork] registered Xbox tile entry "
                      << "(uuid=" << uuid << ", image=" << imagePath << ")";
#endif
}

// =================================================================
// Layer 3 — Runtime watcher
// =================================================================

XboxTileArtwork* XboxTileArtwork::instance()
{
    static XboxTileArtwork* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new XboxTileArtwork(QCoreApplication::instance());
    }
    return s_instance;
}

XboxTileArtwork::XboxTileArtwork(QObject* parent)
    : QObject(parent)
{
}

void XboxTileArtwork::startWatching()
{
#ifndef Q_OS_WIN
    return;
#else
    if (m_watcher) {
        return;
    }
    const QString mPath = manifestPath();
    if (mPath.isEmpty()) {
        return;
    }
    const QString mDir = QFileInfo(mPath).absolutePath();
    if (!QDir(mDir).exists()) {
        // The Xbox app has never been opened. Nothing to watch yet — we
        // could poll, but that's not worth the effort. The boot patch on
        // the *next* StreamLight launch will catch up.
        return;
    }

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(250);
    connect(m_debounce, &QTimer::timeout,
            this, &XboxTileArtwork::onDebouncedReapply);

    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(mDir);
    if (QFileInfo::exists(mPath)) {
        m_watcher->addPath(mPath);
    }
    // Also watch Images/ so we re-patch if Xbox app writes a new PNG after
    // we've already patched it (race condition during manual "+" add).
    const QString imgDir = imagesDir();
    if (QDir(imgDir).exists()) {
        m_watcher->addPath(imgDir);
    }
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &XboxTileArtwork::onManifestChanged);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &XboxTileArtwork::onManifestChanged);

    qInfo() << "[XboxTileArtwork] watching" << mDir << "for tile changes";
#endif
}

void XboxTileArtwork::onManifestChanged(const QString& /*path*/)
{
    if (m_debounce) {
        m_debounce->start(); // restart debounce on every event
    }
}

void XboxTileArtwork::onDebouncedReapply()
{
    // Re-add the manifest file: atomic replace (QSaveFile) may have detached
    // the watcher from the old inode.
    if (m_watcher) {
        const QString mPath = manifestPath();
        if (QFileInfo::exists(mPath) && !m_watcher->files().contains(mPath)) {
            m_watcher->addPath(mPath);
        }
    }
    applyIfRegistered();
}
