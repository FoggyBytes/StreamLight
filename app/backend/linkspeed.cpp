#include "linkspeed.h"

#include <QtGlobal>

#ifdef Q_OS_WIN32
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#endif

namespace LinkSpeed
{

#ifdef Q_OS_WIN32

// Some virtual adapters report a nonsense rate (0, or 0xFFFF...). Anything outside a
// plausible Ethernet range is treated as "no usable rate" rather than propagated.
static bool plausibleMbps(quint64 mbps)
{
    return mbps > 0 && mbps <= 400000;   // 400 Gbps ceiling: generous, still catches garbage
}

Info probeForHost(const QHostAddress& host)
{
    Info info;

    if (host.isNull()) {
        info.reason = QObject::tr("no address for this host yet");
        return info;
    }

    // Ask the routing table which interface would carry traffic to this host. Same approach
    // StreamTweak uses on the host side, and it costs no packets.
    DWORD ifIndex = 0;
    DWORD ret = 0;

    if (host.protocol() == QAbstractSocket::IPv4Protocol) {
        sockaddr_in sa;
        ZeroMemory(&sa, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_addr.S_un.S_addr = htonl(host.toIPv4Address());
        ret = GetBestInterfaceEx(reinterpret_cast<sockaddr*>(&sa), &ifIndex);
    }
    else {
        sockaddr_in6 sa;
        ZeroMemory(&sa, sizeof(sa));
        sa.sin6_family = AF_INET6;
        Q_IPV6ADDR a = host.toIPv6Address();
        memcpy(&sa.sin6_addr, &a, sizeof(a));
        ret = GetBestInterfaceEx(reinterpret_cast<sockaddr*>(&sa), &ifIndex);
    }

    if (ret != NO_ERROR || ifIndex == 0) {
        info.reason = QObject::tr("the route to this host could not be determined");
        return info;
    }

    MIB_IF_ROW2 row;
    ZeroMemory(&row, sizeof(row));
    row.InterfaceIndex = static_cast<NET_IFINDEX>(ifIndex);
    if (GetIfEntry2(&row) != NO_ERROR) {
        info.reason = QObject::tr("the network adapter could not be inspected");
        return info;
    }

    info.adapterName = QString::fromWCharArray(row.Alias);

    // HardwareInterface is the discriminator that matters: Tailscale, WireGuard and Hyper-V
    // switches can present as Ethernet-typed, but none of them are hardware.
    if (!row.InterfaceAndOperStatusFlags.HardwareInterface) {
        info.status = Status::Virtual;
        info.reason = QObject::tr("this device reaches the host through a VPN or virtual adapter");
        return info;
    }

    if (row.Type == IF_TYPE_IEEE80211) {
        info.status = Status::Wireless;
        info.reason = QObject::tr("this device reaches the host over Wi-Fi");
        return info;
    }

    if (row.Type != IF_TYPE_ETHERNET_CSMACD) {
        info.status = Status::Virtual;
        info.reason = QObject::tr("this device reaches the host over a non-Ethernet adapter");
        return info;
    }

    quint64 mbps = static_cast<quint64>(row.TransmitLinkSpeed) / 1000000ULL;
    if (!plausibleMbps(mbps)) {
        info.status = Status::NoLinkSpeed;
        info.reason = QObject::tr("this adapter does not report a link speed");
        return info;
    }

    info.status = Status::Wired;
    info.mbps   = mbps;
    return info;
}

Info probeDefaultRoute()
{
    // Any off-link address resolves to the default-route interface; nothing is sent.
    return probeForHost(QHostAddress(QStringLiteral("8.8.8.8")));
}

#else

Info probeForHost(const QHostAddress&)
{
    Info info;
    info.reason = QObject::tr("matching the host link speed is only supported on Windows");
    return info;
}

Info probeDefaultRoute()
{
    return probeForHost(QHostAddress());
}

#endif

}
