#include "singleinstance.h"

#include <QLocalServer>
#include <QLocalSocket>

SingleInstance::SingleInstance(const QString& serverName, QObject* parent)
    : QObject(parent), m_ServerName(serverName)
{
}

SingleInstance::~SingleInstance()
{
    if (m_Server) {
        m_Server->close();
    }
}

bool SingleInstance::attach()
{
    // Try to connect to an existing primary instance.
    QLocalSocket probe;
    probe.connectToServer(m_ServerName);
    if (probe.waitForConnected(300)) {
        // Primary instance is alive — ask it to raise its window, then exit.
        probe.write("RAISE\n");
        probe.flush();
        probe.waitForBytesWritten(300);
        probe.disconnectFromServer();
        return false;
    }

    // No primary instance answered. Clean up any stale socket file (Unix) or
    // pipe name (Windows) left by a previous crashed instance, then listen.
    QLocalServer::removeServer(m_ServerName);

    m_Server = new QLocalServer(this);
    // Loopback / per-user access only — never expose to other accounts.
    m_Server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_Server->listen(m_ServerName)) {
        // If listen fails we still let the app run; we just lose the
        // single-instance guarantee for this session.
        delete m_Server;
        m_Server = nullptr;
        return true;
    }

    connect(m_Server, &QLocalServer::newConnection,
            this, &SingleInstance::onNewConnection);
    return true;
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket* client = m_Server->nextPendingConnection()) {
        connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
        if (client->waitForReadyRead(200)) {
            const QByteArray msg = client->readAll();
            if (msg.startsWith("RAISE")) {
                emit raiseRequested();
            }
        }
        client->disconnectFromServer();
    }
}
