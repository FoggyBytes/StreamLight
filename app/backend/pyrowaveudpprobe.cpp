// Imported from Nonary/moonlight-qt, branch master at f96b0524 (14/10/2026), by Chase Payne.
// GPLv3, the same licence as StreamLight. The body is verbatim: only this note was added,
// so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "pyrowaveudpprobe.h"

#include <QEventLoop>
#include <QNetworkReply>
#include <QXmlStreamReader>
#include <QHostInfo>
#include <QTimer>
#include <QUdpSocket>
#include <stdexcept>
#include <cstring>
#ifdef Q_OS_DARWIN
#include <mach/mach_time.h>
#include <sys/socket.h>
#include <cerrno>
#endif

namespace {
QHostAddress resolveHost(const QString& hostname, const std::atomic<bool>& cancelled)
{
    if (cancelled.load()) throw std::runtime_error("Calibration stopped.");
    QHostAddress literal(hostname);
    if (!literal.isNull()) return literal;
    if (hostname.isEmpty()) throw std::runtime_error("The PyroWave UDP host is empty");

    QEventLoop loop;
    QHostInfo result;
    bool completed = false;
    const int lookup = QHostInfo::lookupHost(hostname, &loop, [&](const QHostInfo& info) {
        result = info;
        completed = true;
        loop.quit();
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.start(5000);
    QTimer cancellation;
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
        if (cancelled.load()) loop.quit();
    });
    cancellation.start(50);
    loop.exec();
    if (!completed) QHostInfo::abortHostLookup(lookup);
    if (cancelled.load()) throw std::runtime_error("Calibration stopped.");
    if (!completed) {
        throw std::runtime_error(QString("Resolving PyroWave UDP host '%1' timed out")
                                 .arg(hostname).toStdString());
    }
    if (result.error() == QHostInfo::NoError) {
        // Prefer IPv4 for a dual-stack hostname, retaining literal IPv6 and
        // IPv6-only hosts. The HTTPS request is pinned to this chosen address.
        for (auto protocol : {QAbstractSocket::IPv4Protocol, QAbstractSocket::IPv6Protocol}) {
            for (const auto& address : result.addresses()) {
                if (!address.isNull() && address.protocol() == protocol) return address;
            }
        }
    }
    throw std::runtime_error(QString("Could not resolve PyroWave UDP host '%1': %2")
                             .arg(hostname, result.errorString()).toStdString());
}
}

bool PyroWaveUdp::completeCloseDelimitedResponse(QNetworkReply& reply)
{
    const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 || reply.error() != QNetworkReply::NoError ||
        reply.hasRawHeader("Content-Length") || reply.hasRawHeader("Transfer-Encoding") ||
        reply.rawHeader("Connection").trimmed().toLower() != "close" ||
        !reply.hasRawHeader("X-PyroWave-Udp-Port") ||
        reply.bytesAvailable() == 0 || reply.bytesAvailable() > 4096) return false;

    // Peek leaves the body intact for NvHTTP's normal status/count validation.
    // Parsing the entire bounded document rejects partial XML and trailing junk.
    QXmlStreamReader xml(reply.peek(4096));
    if (!xml.readNextStartElement() || xml.name() != QStringLiteral("root")) return false;
    const auto statusCode = xml.attributes().value("status_code").toString();
    const bool success = statusCode == QStringLiteral("200");
    // The host can fail after announcing the UDP port. Its terminal XML error
    // also needs delivery before EOF; NvHTTP::verifyResponseStatus then throws
    // the host's reason rather than converting it to a bandwidth result.
    const bool hostError = statusCode == QStringLiteral("500") &&
        !xml.attributes().value("status_message").trimmed().isEmpty();
    if (!success && !hostError) return false;
    bool expected = false, sent = false, elapsed = false;
    while (xml.readNextStartElement()) {
        expected |= xml.name() == QStringLiteral("expected");
        sent |= xml.name() == QStringLiteral("sent");
        elapsed |= xml.name() == QStringLiteral("elapsedMs");
        xml.skipCurrentElement();
    }
    while (!xml.atEnd()) xml.readNext();
    return !xml.hasError() && (hostError || (expected && sent && elapsed));
}

QHostAddress PyroWaveUdp::bindReceiver(QUdpSocket& socket, const QString& hostname,
                                      const std::atomic<bool>& cancelled)
{
    const QHostAddress remote = resolveHost(hostname, cancelled);
    const QHostAddress local(remote.protocol() == QAbstractSocket::IPv6Protocol ?
                            QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4);
    if (!socket.bind(local, 0)) {
        throw std::runtime_error(QString("Could not open the PyroWave UDP receiver: %1")
                                 .arg(socket.errorString()).toStdString());
    }
    return remote;
}

PyroWaveUdp::ReceiverTiming::ReceiverTiming(QUdpSocket& socket) : m_Socket(socket)
{
    m_Clock.start();
#ifdef Q_OS_DARWIN
    const int enabled = 1;
    if (setsockopt(int(socket.socketDescriptor()), SOL_SOCKET, SO_TIMESTAMP_MONOTONIC,
                   &enabled, sizeof(enabled)) != 0) {
        throw std::runtime_error(QString("Could not enable PyroWave UDP arrival timestamps: %1")
                                 .arg(QString::fromLocal8Bit(std::strerror(errno))).toStdString());
    }
    mach_timebase_info_data_t timebase;
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.denom == 0) {
        throw std::runtime_error("Could not obtain the PyroWave UDP timestamp clock");
    }
    m_TimebaseNumer = timebase.numer;
    m_TimebaseDenom = timebase.denom;
    m_StartTicks = mach_absolute_time();
    m_KernelTimestamps = true;
#endif
}

PyroWaveUdp::ProbeHandshake::ProbeHandshake(QUdpSocket& socket, const QHostAddress& host,
                                           const QByteArray& token, const std::atomic<bool>& cancelled) :
    m_Socket(socket), m_Host(host), m_Token(token), m_Cancelled(cancelled)
{
    m_Retry.setInterval(100);
    QObject::connect(&m_Retry, &QTimer::timeout, &m_Retry, [this] { sendToken(); });
}

bool PyroWaveUdp::ProbeHandshake::start(const QByteArray& portHeader)
{
    if (!m_Error.isEmpty()) return false;
    bool valid = false;
    const auto port = portHeader.toUShort(&valid);
    if (portHeader.isEmpty() || portHeader.size() > 5 || !valid || port < 1024) {
        m_Retry.stop();
        m_Error = QStringLiteral("The host did not announce a valid PyroWave UDP port; update Vibeshine to support the UDP handshake");
        return false;
    }
    for (char digit : portHeader) {
        if (digit < '0' || digit > '9') {
            m_Retry.stop();
            m_Error = QStringLiteral("Invalid PyroWave UDP handshake port");
            return false;
        }
    }
    if (m_Port != 0 && m_Port != port) {
        m_Error = QStringLiteral("The host changed its PyroWave UDP handshake port");
        m_Retry.stop();
        return false;
    }
    if (m_Port == port) return true;
    m_Port = port;
    if (m_Cancelled.load()) return true;
    m_Retry.start();
    sendToken();
    return m_Error.isEmpty();
}

void PyroWaveUdp::ProbeHandshake::sendToken()
{
    if (m_Cancelled.load()) { m_Retry.stop(); return; }
    if (m_Socket.writeDatagram(m_Token, m_Host, m_Port) != m_Token.size()) {
        m_Error = QStringLiteral("Could not send the PyroWave UDP handshake: %1").arg(m_Socket.errorString());
        m_Retry.stop();
    }
    else ++m_SentTokens;
}

QString PyroWaveUdp::ProbeHandshake::timeoutDescription(quint64 receivedPackets) const
{
    const QString phase = !m_Error.isEmpty() ? m_Error :
        m_Port == 0 ? QStringLiteral("No valid HTTPS sender-port announcement arrived") :
        receivedPackets == 0 ? QStringLiteral("No authenticated measured UDP packets arrived after the handshake") :
        QStringLiteral("Measured UDP packets arrived, but the HTTPS result did not complete");
    return QStringLiteral("%1 (host %2, receiver port %3, sender port %4, tokens sent %5, authenticated packets %6)")
        .arg(phase, m_Host.toString()).arg(m_Socket.localPort()).arg(m_Port)
        .arg(m_SentTokens).arg(receivedPackets);
}

bool PyroWaveUdp::ProbeHandshake::acceptsSource(const QHostAddress& source, quint16 port) const
{
    return m_Port != 0 && source == m_Host && port == m_Port;
}

qint64 PyroWaveUdp::ReceiverTiming::readDatagram(char* data, qint64 capacity,
                                               QHostAddress* source, qint64& arrivalUs, quint16* sourcePort)
{
    arrivalUs = -1;
    m_LastReadDelayUs = -1;
#ifdef Q_OS_DARWIN
    // Peek only the first byte plus ancillary metadata, then let Qt consume
    // the packet and maintain its notifier state/address parsing. This socket
    // has one reader, so both operations observe the same queued datagram.
    char byte;
    iovec buffer {&byte, 1};
    alignas(cmsghdr) unsigned char control[256] {};
    msghdr message {};
    message.msg_iov = &buffer;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    if (recvmsg(int(m_Socket.socketDescriptor()), &message, MSG_PEEK | MSG_DONTWAIT) < 0) return -1;
    uint64_t ticks = 0;
    if (!(message.msg_flags & MSG_CTRUNC)) {
        for (auto* entry = CMSG_FIRSTHDR(&message); entry; entry = CMSG_NXTHDR(&message, entry)) {
            if (entry->cmsg_level == SOL_SOCKET && entry->cmsg_type == SCM_TIMESTAMP_MONOTONIC &&
                entry->cmsg_len >= CMSG_LEN(sizeof(ticks))) {
                std::memcpy(&ticks, CMSG_DATA(entry), sizeof(ticks));
                break;
            }
        }
    }
    const auto bytes = m_Socket.readDatagram(data, capacity, source, sourcePort);
    if (bytes < 0) return bytes;
    const uint64_t readTicks = mach_absolute_time();
    if (ticks >= m_StartTicks && ticks <= readTicks) {
        const auto toUs = [&](uint64_t delta) {
            return qint64((static_cast<unsigned __int128>(delta) * m_TimebaseNumer / m_TimebaseDenom) / 1000);
        };
        arrivalUs = toUs(ticks - m_StartTicks);
        m_LastReadDelayUs = toUs(readTicks - ticks);
    }
    // A missing/invalid native timestamp is never substituted with read time.
    // The caller can reject an authenticated packet instead of misgrading it.
    return bytes;
#else
    const auto bytes = m_Socket.readDatagram(data, capacity, source, sourcePort);
    if (bytes >= 0) {
        arrivalUs = m_Clock.nsecsElapsed() / 1000;
        m_LastReadDelayUs = 0;
    }
    return bytes;
#endif
}
