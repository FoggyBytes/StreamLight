#include "StreamTweakBridge.h"

#include <QTcpSocket>
#include <QTextStream>

StreamTweakBridge::StreamTweakBridge(QObject* parent)
    : QObject(parent)
{
}

void StreamTweakBridge::sendPrepare(const QString& hostAddress)
{
    sendCommand(hostAddress, QStringLiteral("PREPARE"));
}

void StreamTweakBridge::sendRestore(const QString& hostAddress)
{
    sendCommand(hostAddress, QStringLiteral("RESTORE"));
}

void StreamTweakBridge::sendCommand(const QString& hostAddress, const QString& command)
{
    QTcpSocket* socket = new QTcpSocket();

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);
    QObject::connect(socket, &QAbstractSocket::errorOccurred,
                     socket, &QObject::deleteLater);

    QObject::connect(socket, &QTcpSocket::connected, [socket, command]() {
        QTextStream stream(socket);
        stream << command << "\n";
        stream.flush();
    });

    QObject::connect(socket, &QTcpSocket::readyRead, [socket]() {
        socket->readAll();
        socket->disconnectFromHost();
    });

    socket->connectToHost(hostAddress, BridgePort);
}

void StreamTweakBridge::requestStatus(const QString& hostAddress)
{
    QTcpSocket* socket = new QTcpSocket();

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);

    // On error: emit empty status and clean up
    QObject::connect(socket, &QAbstractSocket::errorOccurred,
                     [this, socket](QAbstractSocket::SocketError) {
        emit statusReceived(QString());
        socket->deleteLater();
    });

    QObject::connect(socket, &QTcpSocket::connected, [socket]() {
        QTextStream stream(socket);
        stream << "STATUS\n";
        stream.flush();
    });

    QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        QString response = QString::fromUtf8(socket->readAll()).trimmed();
        emit statusReceived(response);
        socket->disconnectFromHost();
    });

    socket->connectToHost(hostAddress, BridgePort);
}

void StreamTweakBridge::requestStats(const QString& hostAddress)
{
    QTcpSocket* socket = new QTcpSocket();

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);

    QObject::connect(socket, &QAbstractSocket::errorOccurred,
                     [this, socket](QAbstractSocket::SocketError) {
        emit statsReceived(QString());
        socket->deleteLater();
    });

    QObject::connect(socket, &QTcpSocket::connected, [socket]() {
        QTextStream stream(socket);
        stream << "STATS\n";
        stream.flush();
    });

    QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        QString response = QString::fromUtf8(socket->readAll()).trimmed();
        emit statsReceived(response);
        socket->disconnectFromHost();
    });

    socket->connectToHost(hostAddress, BridgePort);
}

void StreamTweakBridge::requestSessionId(const QString& hostAddress)
{
    QTcpSocket* socket = new QTcpSocket();

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);

    QObject::connect(socket, &QAbstractSocket::errorOccurred,
                     [this, socket](QAbstractSocket::SocketError) {
        emit sessionIdReceived(QString());
        socket->deleteLater();
    });

    QObject::connect(socket, &QTcpSocket::connected, [socket]() {
        QTextStream stream(socket);
        stream << "SESSIONID\n";
        stream.flush();
    });

    QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        QString response = QString::fromUtf8(socket->readAll()).trimmed();
        emit sessionIdReceived(response);
        socket->disconnectFromHost();
    });

    socket->connectToHost(hostAddress, BridgePort);
}

void StreamTweakBridge::sendSessionData(const QString& hostAddress, const QString& jsonPayload)
{
    // Protocol: send "SESSIONDATA\n" then "<compact-json>\n" on the same connection.
    // The server reads two lines: command then payload.
    QTcpSocket* socket = new QTcpSocket();

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);
    QObject::connect(socket, &QAbstractSocket::errorOccurred,
                     socket, &QObject::deleteLater);

    QObject::connect(socket, &QTcpSocket::connected, [socket, jsonPayload]() {
        QTextStream stream(socket);
        stream << "SESSIONDATA\n";
        stream << jsonPayload << "\n";
        stream.flush();
    });

    QObject::connect(socket, &QTcpSocket::readyRead, [socket]() {
        socket->readAll(); // discard OK/ERR
        socket->disconnectFromHost();
    });

    socket->connectToHost(hostAddress, BridgePort);
}

void StreamTweakBridge::sendSessionDataSync(const QString& hostAddress, const QString& jsonPayload)
{
    // Synchronous send: used only for the final flush in flushAndStop(), where
    // exec() has already returned and the Qt event loop is not running, so an
    // async socket would never complete its connected/readyRead cycle.
    QTcpSocket socket;
    socket.connectToHost(hostAddress, BridgePort);
    if (!socket.waitForConnected(2000))
        return;

    QTextStream stream(&socket);
    stream << "SESSIONDATA\n" << jsonPayload << "\n";
    stream.flush();

    socket.waitForBytesWritten(2000);
    socket.disconnectFromHost();
}

void StreamTweakBridge::requestTailscale(const QString& hostAddress)
{
    QTcpSocket* socket = new QTcpSocket();

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);

    QObject::connect(socket, &QAbstractSocket::errorOccurred,
                     [this, socket](QAbstractSocket::SocketError) {
        emit tailscaleReceived(QString());
        socket->deleteLater();
    });

    QObject::connect(socket, &QTcpSocket::connected, [socket]() {
        QTextStream stream(socket);
        stream << "TAILSCALE\n";
        stream.flush();
    });

    QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        QString response = QString::fromUtf8(socket->readAll()).trimmed();
        emit tailscaleReceived(response);
        socket->disconnectFromHost();
    });

    socket->connectToHost(hostAddress, BridgePort);
}

void StreamTweakBridge::requestAppStores(const QString& hostAddress)
{
    QTcpSocket* socket = new QTcpSocket();

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);

    QObject::connect(socket, &QAbstractSocket::errorOccurred,
                     [this, socket](QAbstractSocket::SocketError) {
        emit appStoresReceived(QString());
        socket->deleteLater();
    });

    QObject::connect(socket, &QTcpSocket::connected, [socket]() {
        QTextStream stream(socket);
        stream << "APPSTORES\n";
        stream.flush();
    });

    QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        QString response = QString::fromUtf8(socket->readAll()).trimmed();
        emit appStoresReceived(response);
        socket->disconnectFromHost();
    });

    socket->connectToHost(hostAddress, BridgePort);
}
