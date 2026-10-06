#pragma once

#include <QString>
#include <QFileInfo>

class Path
{
public:
    static QString getLogDir();
    static QString getBoxArtCacheDir();
    static QString getQmlCacheDir();

    static QByteArray readDataFile(QString fileName);
    static void writeCacheFile(QString fileName, QByteArray data);
    static void deleteCacheFile(QString fileName);
    static QFileInfo getCacheFileInfo(QString fileName);

    // Only safe to use directly for Qt classes
    static QString getDataFilePath(QString fileName);

    static void initialize(bool portable);

    // Where logs and crash dumps go (6.5.0, Settings → About → Logs & crash dumps). The choice is
    // read once in initialize(), before the log file is opened, and changed live by setLogDir().
    enum LogDirChoice {
        LDC_DEFAULT,    // the temporary folder, or the app folder in portable mode
        LDC_DOCUMENTS,  // Documents\StreamLight\Logs
        LDC_APPDATA,    // %LOCALAPPDATA%\FoggyBytes\StreamLight\Logs, beside the cover cache
        LDC_CUSTOM      // a folder the user picked
    };
    static QString logDirForChoice(int choice, const QString& customDir);
    // Creates the folder and proves it writable by writing into it — QFileInfo::isWritable()
    // does not read Windows ACLs.
    static bool prepareLogDir(const QString& dir);
    static void setLogDir(const QString& dir);

private:
    static QString s_CacheDir;
    static QString s_DefaultLogDir;
    static QString s_LogDir;
    static QString s_BoxArtCacheDir;
    static QString s_QmlCacheDir;
};
