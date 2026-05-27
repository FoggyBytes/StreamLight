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

signals:
    void raiseRequested();

private slots:
    void onNewConnection();

private:
    QString m_ServerName;
    QLocalServer* m_Server = nullptr;
};
