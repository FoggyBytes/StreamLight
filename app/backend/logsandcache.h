#pragma once

#include <QObject>
#include <QString>

class QQmlEngine;

/**
 * Settings → About → "Logs & crash dumps" and "Cover cache" (6.5.0).
 *
 * Where StreamLight writes its logs and crash dumps (one folder for both: the crash handler
 * reads Path::getLogDir() too), and where it keeps the covers it downloaded. Each can be
 * opened in File Explorer and cleared; the log folder can also be moved, and the move applies
 * at once — the running log continues in the new folder (LogFile::continueIn).
 *
 * The figures are read when asked (refresh), not watched: QML refreshes when the About tab
 * opens and after every action here.
 */
class LogsAndCache : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString logDir READ logDir NOTIFY changed)
    Q_PROPERTY(int logDirChoice READ logDirChoice NOTIFY changed)
    Q_PROPERTY(QString logSummary READ logSummary NOTIFY changed)
    Q_PROPERTY(bool hasOldLogs READ hasOldLogs NOTIFY changed)
    Q_PROPERTY(QString coverDir READ coverDir NOTIFY changed)
    Q_PROPERTY(QString coverSummary READ coverSummary NOTIFY changed)
    Q_PROPERTY(bool hasCovers READ hasCovers NOTIFY changed)

public:
    static LogsAndCache* get(QQmlEngine* engine = nullptr);

    QString logDir() const;
    int logDirChoice() const { return m_LogDirChoice; }
    QString logSummary() const { return m_LogSummary; }
    bool hasOldLogs() const { return m_OldLogs + m_Dumps > 0; }
    QString coverDir() const;
    QString coverSummary() const { return m_CoverSummary; }
    bool hasCovers() const { return m_Covers > 0; }

    // The folder a choice means (Path::LogDirChoice), for the list the user picks from.
    // For "custom" it is the folder picked last time, or empty if there was never one.
    Q_INVOKABLE QString choicePath(int choice) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE void openCoverFolder();

    // Each returns the sentence to show under its button: what was done, or why not.
    Q_INVOKABLE QString clearLogs();
    Q_INVOKABLE QString clearCoverCache();
    // customDir: a local path or a file:// URL straight from FolderDialog; used only for
    // the custom choice.
    Q_INVOKABLE QString setLogDirChoice(int choice, const QString& customDir = QString());

signals:
    void changed();

private:
    explicit LogsAndCache(QObject* parent = nullptr);

    int m_LogDirChoice = 0;
    QString m_CustomDir;
    int m_OldLogs = 0;
    int m_Dumps = 0;
    QString m_LogSummary;
    int m_Covers = 0;
    QString m_CoverSummary;
};
