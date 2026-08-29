#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(const QString& serverName, QObject* parent = nullptr);
    ~SingleInstance() override;

    // Returns true if this is the primary instance (server now listening),
    // false if another instance already exists (a raise message was sent to it).
    bool attach();

    // Stops listening, so a process started from here does NOT find us and bow out.
    // ⚠️ Only for the restart path (SystemProperties::restartApplication): without it
    // the replacement probes a parent that has not finished quitting, is told there is
    // already an instance, and exits — leaving nothing running at all. See the comment
    // there. Safe to call more than once.
    void release();

    // The primary instance's object, or nullptr when single-instance enforcement is
    // not in play (CLI subcommands, or listen() having failed).
    static SingleInstance* primary();

signals:
    void raiseRequested();

private slots:
    void onNewConnection();

private:
    static SingleInstance* s_Primary;

    QString m_ServerName;
    QLocalServer* m_Server = nullptr;
};
