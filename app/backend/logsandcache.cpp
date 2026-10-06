#include "logsandcache.h"

#include "path.h"
#include "logfile.h"
#include "backend/boxartmanager.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QUrl>

#define SER_LOGDIRCHOICE "logs/dirChoice"
#define SER_LOGCUSTOMDIR "logs/customDir"

namespace {

const QStringList k_LogPattern  { QStringLiteral("StreamLight-*.log") };
const QStringList k_DumpPattern { QStringLiteral("StreamLight-*.dmp") };

QString sizeText(qint64 bytes)
{
    return QLocale(QLocale::English).formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

// StreamLight is English-only and loads no translation, so tr()'s %n plurals would print
// "(s)" literally: the two forms are spelled out here instead.
QString count(int n, const char* one, const char* many)
{
    return QString::number(n) + QLatin1Char(' ') + QLatin1String(n == 1 ? one : many);
}

QString sameFile(const QString& path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
}

void openFolder(const QString& dir)
{
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

} // namespace

LogsAndCache* LogsAndCache::get(QQmlEngine*)
{
    static LogsAndCache* instance = new LogsAndCache();
    return instance;
}

LogsAndCache::LogsAndCache(QObject* parent)
    : QObject(parent)
{
    QSettings settings;
    m_LogDirChoice = settings.value(SER_LOGDIRCHOICE, Path::LDC_DEFAULT).toInt();
    m_CustomDir = settings.value(SER_LOGCUSTOMDIR).toString();

    // Path::initialize() falls back to the default folder when the chosen one cannot be
    // written. Say what is really in use rather than what was asked for.
    if (sameFile(Path::logDirForChoice(m_LogDirChoice, m_CustomDir)) != sameFile(Path::getLogDir())) {
        m_LogDirChoice = Path::LDC_DEFAULT;
    }
    refresh();
}

QString LogsAndCache::logDir() const
{
    return QDir::toNativeSeparators(Path::getLogDir());
}

QString LogsAndCache::coverDir() const
{
    return QDir::toNativeSeparators(Path::getBoxArtCacheDir());
}

QString LogsAndCache::choicePath(int choice) const
{
    const QString dir = Path::logDirForChoice(choice, m_CustomDir);
    return dir.isEmpty() ? QString() : QDir::toNativeSeparators(QDir::cleanPath(dir));
}

void LogsAndCache::refresh()
{
    const QDir logs(Path::getLogDir());
    const QFileInfoList logFiles = logs.entryInfoList(k_LogPattern, QDir::Files);
    const QFileInfoList dumpFiles = logs.entryInfoList(k_DumpPattern, QDir::Files);
    qint64 logBytes = 0;
    for (const QFileInfo& f : logFiles)  logBytes += f.size();
    for (const QFileInfo& f : dumpFiles) logBytes += f.size();

    // The running log may sit in another folder only when stderr was redirected (no file),
    // so "old" is every log here except the one being written.
    const QString current = sameFile(LogFile::currentPath());
    m_OldLogs = 0;
    for (const QFileInfo& f : logFiles) {
        if (sameFile(f.filePath()) != current) m_OldLogs++;
    }
    m_Dumps = dumpFiles.size();
    m_LogSummary = count(logFiles.size(), "log", "logs") + QStringLiteral(" · ")
                 + count(m_Dumps, "crash dump", "crash dumps") + QStringLiteral(" · ")
                 + sizeText(logBytes);

    m_Covers = 0;
    qint64 coverBytes = 0;
    QDirIterator it(Path::getBoxArtCacheDir(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        m_Covers++;
        coverBytes += it.fileInfo().size();
    }
    m_CoverSummary = count(m_Covers, "cover", "covers") + QStringLiteral(" · ") + sizeText(coverBytes);

    emit changed();
}

void LogsAndCache::openLogFolder()
{
    openFolder(Path::getLogDir());
}

void LogsAndCache::openCoverFolder()
{
    openFolder(Path::getBoxArtCacheDir());
}

QString LogsAndCache::clearLogs()
{
    const QDir logs(Path::getLogDir());
    const QString current = sameFile(LogFile::currentPath());
    int removedLogs = 0, removedDumps = 0, failed = 0;

    for (const QFileInfo& f : logs.entryInfoList(k_LogPattern, QDir::Files)) {
        if (sameFile(f.filePath()) == current) continue;
        if (QFile::remove(f.filePath())) removedLogs++; else failed++;
    }
    for (const QFileInfo& f : logs.entryInfoList(k_DumpPattern, QDir::Files)) {
        if (QFile::remove(f.filePath())) removedDumps++; else failed++;
    }
    qInfo() << "Cleared logs:" << removedLogs << "logs," << removedDumps << "dumps," << failed << "failed";

    refresh();
    QString text = QStringLiteral("Removed ") + count(removedLogs, "log", "logs") + QStringLiteral(" · ")
                 + count(removedDumps, "crash dump", "crash dumps");
    if (failed > 0) {
        text += QStringLiteral(" · ") + count(failed, "file", "files") + QStringLiteral(" in use not removed");
    }
    return text + QStringLiteral(" · ") + tr("the current log is kept");
}

QString LogsAndCache::clearCoverCache()
{
    const QString root = Path::getBoxArtCacheDir();
    int removed = 0, failed = 0;

    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (QFile::remove(it.next())) removed++; else failed++;
    }
    // The per-host folders are made again on demand (BoxArtManager::getFilePathForBoxArt)
    for (const QFileInfo& d : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir().rmdir(d.filePath());
    }
    BoxArtManager::forgetCheckedCovers();
    qInfo() << "Cleared cover cache:" << removed << "removed," << failed << "failed";

    refresh();
    QString text = QStringLiteral("Removed ") + count(removed, "cover", "covers");
    if (failed > 0) {
        text += QStringLiteral(" · ") + QString::number(failed) + QStringLiteral(" in use not removed");
    }
    return text + QStringLiteral(" · ") + tr("they download again when shown");
}

QString LogsAndCache::setLogDirChoice(int choice, const QString& customDir)
{
    QString custom = m_CustomDir;
    if (choice == Path::LDC_CUSTOM) {
        const QUrl url(customDir);
        custom = url.isLocalFile() ? url.toLocalFile() : customDir;
        if (custom.isEmpty()) {
            return tr("No folder was picked");
        }
    }

    const QString dir = QDir::cleanPath(Path::logDirForChoice(choice, custom));
    if (sameFile(dir) == sameFile(Path::getLogDir())) {
        m_LogDirChoice = choice;
        m_CustomDir = custom;
        QSettings settings;
        settings.setValue(SER_LOGDIRCHOICE, choice);
        settings.setValue(SER_LOGCUSTOMDIR, custom);
        refresh();
        return tr("Logs already go to this folder");
    }

    if (!Path::prepareLogDir(dir)) {
        return tr("Can't write to %1").arg(QDir::toNativeSeparators(dir));
    }

    // With no log file of our own (stderr redirected at launch) there is nothing to move now;
    // the folder is still saved and used from the next launch.
    const bool hadFile = !LogFile::currentPath().isEmpty();
    if (hadFile && !LogFile::continueIn(dir)) {
        return tr("Can't create a log file in %1").arg(QDir::toNativeSeparators(dir));
    }
    Path::setLogDir(dir);

    m_LogDirChoice = choice;
    m_CustomDir = custom;
    QSettings settings;
    settings.setValue(SER_LOGDIRCHOICE, choice);
    settings.setValue(SER_LOGCUSTOMDIR, custom);

    refresh();
    return hadFile ? tr("Logging to this folder from now on · earlier logs stay where they were")
                   : tr("Saved · used from the next launch");
}
