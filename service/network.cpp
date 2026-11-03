#include "network.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <winhttp.h>

#include <vector>

namespace
{

bool parseMac(const QString& text, quint8 out[6])
{
    QString hex;
    for (QChar c : text) {
        if (c.isLetterOrNumber())
            hex.append(c);
    }
    if (hex.size() != 12)
        return false;
    for (int i = 0; i < 6; i++) {
        bool ok = false;
        out[i] = static_cast<quint8>(hex.mid(i * 2, 2).toUInt(&ok, 16));
        if (!ok)
            return false;
    }
    return true;
}

QString macText(const unsigned char* bytes, unsigned long length)
{
    if (length != 6)
        return QString();
    QStringList parts;
    for (unsigned long i = 0; i < length; i++)
        parts.append(QStringLiteral("%1").arg(bytes[i], 2, 16, QLatin1Char('0')).toUpper());
    return parts.join(QLatin1Char('-'));
}

bool toAddress(const QString& ip, in_addr* out)
{
    return inet_pton(AF_INET, ip.toLatin1().constData(), out) == 1;
}

QString fromAddress(const in_addr& address)
{
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &address, text, sizeof(text));
    return QString::fromLatin1(text);
}

// Broadcast, multicast and the all-zero MAC are never a TV.
bool isUnicastNeighbour(const MIB_IPNET_ROW2& row)
{
    if (row.PhysicalAddressLength != 6 || row.State == NlnsUnreachable || row.State == NlnsIncomplete)
        return false;
    const unsigned char* mac = row.PhysicalAddress;
    if (mac[0] & 0x01)
        return false;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0)
            return true;
    }
    return false;
}

struct LocalAddress
{
    in_addr address;
    UINT8 prefix;
};

// The IPv4 addresses of every adapter that is up: not loopback, not link-local (169.254.x.x, an
// adapter with no network behind it). Multicast and broadcast go out through each of them
// explicitly — Windows would otherwise pick the adapter with the lowest metric, and on a PC with
// Hyper-V, Tailscale or a disconnected NIC that is often not the LAN the TV is on.
std::vector<LocalAddress> localAddresses()
{
    std::vector<LocalAddress> found;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> storage(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr,
                                        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        storage.resize(size);
        result = GetAdaptersAddresses(AF_INET, flags, nullptr,
                                      reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &size);
    }
    if (result != NO_ERROR)
        return found;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            const auto* v4 = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
            if (v4->sin_family != AF_INET)
                continue;
            const quint8 first = v4->sin_addr.S_un.S_un_b.s_b1;
            const quint8 second = v4->sin_addr.S_un.S_un_b.s_b2;
            if (first == 127 || (first == 169 && second == 254))
                continue;
            found.push_back({ v4->sin_addr, u->OnLinkPrefixLength });
        }
    }
    return found;
}

QString header(const QString& response, const char* name)
{
    for (const QString& line : response.split(QStringLiteral("\r\n"))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0 && line.left(colon).trimmed().compare(QLatin1String(name), Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return QString();
}

}

namespace TvNetwork
{

bool startup()
{
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

QString normaliseMac(const QString& mac)
{
    quint8 bytes[6];
    return parseMac(mac, bytes) ? macText(bytes, 6) : QString();
}

bool sameMac(const QString& a, const QString& b)
{
    const QString na = normaliseMac(a);
    return !na.isEmpty() && na == normaliseMac(b);
}

bool portOpen(const QString& ip, quint16 port, int timeoutMs)
{
    sockaddr_in target = {};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    if (!toAddress(ip, &target.sin_addr))
        return false;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    bool open = false;
    if (connect(s, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0) {
        open = true;
    }
    else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set writable, failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(s, &writable);
        FD_SET(s, &failed);
        timeval wait = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        if (select(0, nullptr, &writable, &failed, &wait) > 0 && FD_ISSET(s, &writable)) {
            int error = 0;
            int length = sizeof(error);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
            open = error == 0;
        }
    }
    closesocket(s);
    return open;
}

QString arpResolve(const QString& ip)
{
    in_addr address;
    if (!toAddress(ip, &address))
        return QString();
    ULONG mac[2] = {};
    ULONG length = 6;
    if (SendARP(address.S_un.S_addr, 0, mac, &length) != NO_ERROR)
        return QString();
    return macText(reinterpret_cast<unsigned char*>(mac), length);
}

QStringList addressesFor(const QString& mac)
{
    QStringList found;
    PMIB_IPNET_TABLE2 table = nullptr;
    if (GetIpNetTable2(AF_INET, &table) != NO_ERROR)
        return found;
    for (ULONG i = 0; i < table->NumEntries; i++) {
        const MIB_IPNET_ROW2& row = table->Table[i];
        if (isUnicastNeighbour(row) && sameMac(macText(row.PhysicalAddress, row.PhysicalAddressLength), mac))
            found.append(fromAddress(row.Address.Ipv4.sin_addr));
    }
    FreeMibTable(table);
    return found;
}

QList<Candidate> probeNeighbours(int perPortMs)
{
    QList<Candidate> found;
    PMIB_IPNET_TABLE2 table = nullptr;
    if (GetIpNetTable2(AF_INET, &table) != NO_ERROR)
        return found;
    QSet<QString> seen;
    for (ULONG i = 0; i < table->NumEntries; i++) {
        const MIB_IPNET_ROW2& row = table->Table[i];
        if (!isUnicastNeighbour(row))
            continue;
        const QString ip = fromAddress(row.Address.Ipv4.sin_addr);
        if (seen.contains(ip))
            continue;
        seen.insert(ip);
        if (portOpen(ip, 3001, perPortMs) || portOpen(ip, 3000, perPortMs))
            found.append({ ip, macText(row.PhysicalAddress, row.PhysicalAddressLength), QString(), QString() });
    }
    FreeMibTable(table);
    return found;
}

QList<Candidate> ssdpSearch(int waitMs)
{
    QList<Candidate> found;
    sockaddr_in group = {};
    group.sin_family = AF_INET;
    group.sin_port = htons(1900);
    toAddress(QStringLiteral("239.255.255.250"), &group.sin_addr);
    const QByteArray search =
            "M-SEARCH * HTTP/1.1\r\n"
            "HOST: 239.255.255.250:1900\r\n"
            "MAN: \"ssdp:discover\"\r\n"
            "MX: 2\r\n"
            "ST: urn:lge-com:service:webos-second-screen:1\r\n\r\n";

    // One socket per adapter, each sending through its own interface (see localAddresses()).
    std::vector<SOCKET> sockets;
    for (const LocalAddress& local : localAddresses()) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET)
            continue;
        sockaddr_in bound = {};
        bound.sin_family = AF_INET;
        bound.sin_addr = local.address;
        DWORD ttl = 2;
        if (bind(s, reinterpret_cast<sockaddr*>(&bound), sizeof(bound)) != 0
                || setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&local.address),
                              sizeof(local.address)) != 0) {
            closesocket(s);
            continue;
        }
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));
        sockets.push_back(s);
    }
    // UDP: twice, a moment apart, in case the first one is lost.
    for (int i = 0; i < 2; i++) {
        for (SOCKET s : sockets)
            sendto(s, search.constData(), search.size(), 0, reinterpret_cast<sockaddr*>(&group), sizeof(group));
        Sleep(100);
    }

    QSet<QString> seen;
    QElapsedTimer clock;
    clock.start();
    char buffer[2048];
    while (!sockets.empty() && clock.elapsed() < waitMs) {
        fd_set readable;
        FD_ZERO(&readable);
        for (SOCKET s : sockets)
            FD_SET(s, &readable);
        const int left = waitMs - static_cast<int>(clock.elapsed());
        timeval wait = { left / 1000, (left % 1000) * 1000 };
        if (select(0, &readable, nullptr, nullptr, &wait) <= 0)
            break;
        for (SOCKET s : sockets) {
            if (!FD_ISSET(s, &readable))
                continue;
            sockaddr_in from = {};
            int fromLength = sizeof(from);
            const int got = recvfrom(s, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
            if (got <= 0)
                continue;
            const QString ip = fromAddress(from.sin_addr);
            if (seen.contains(ip))
                continue;
            seen.insert(ip);
            const QString response = QString::fromLatin1(buffer, got);
            found.append({ ip, arpResolve(ip), header(response, "SERVER"), header(response, "LOCATION") });
        }
    }
    for (SOCKET s : sockets)
        closesocket(s);
    return found;
}

int sendWakeOnLan(const QString& mac)
{
    quint8 bytes[6];
    if (!parseMac(mac, bytes))
        return 0;
    QByteArray packet(6, char(0xFF));
    for (int i = 0; i < 16; i++)
        packet.append(reinterpret_cast<const char*>(bytes), 6);

    // From each adapter (see localAddresses()): the limited broadcast and that adapter's own subnet
    // broadcast, port 9.
    int sent = 0;
    for (const LocalAddress& local : localAddresses()) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET)
            continue;
        sockaddr_in bound = {};
        bound.sin_family = AF_INET;
        bound.sin_addr = local.address;
        BOOL allow = TRUE;
        if (bind(s, reinterpret_cast<sockaddr*>(&bound), sizeof(bound)) != 0
                || setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&allow), sizeof(allow)) != 0) {
            closesocket(s);
            continue;
        }
        std::vector<in_addr> targets(1);
        targets[0].S_un.S_addr = INADDR_BROADCAST;
        if (local.prefix > 0 && local.prefix < 31) {
            in_addr subnet;
            subnet.S_un.S_addr = local.address.S_un.S_addr | htonl(0xFFFFFFFFu >> local.prefix);
            targets.push_back(subnet);
        }
        for (const in_addr& address : targets) {
            sockaddr_in to = {};
            to.sin_family = AF_INET;
            to.sin_port = htons(9);
            to.sin_addr = address;
            if (sendto(s, packet.constData(), packet.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == packet.size())
                sent++;
        }
        closesocket(s);
    }
    return sent;
}

QString friendlyName(const QString& location, int timeoutMs)
{
    const QUrl url(location);
    if (!url.isValid() || url.scheme() != QLatin1String("http"))
        return QString();
    HINTERNET session = WinHttpOpen(L"StreamLight", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return QString();
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    QByteArray body;
    const std::wstring host = url.host().toStdWString();
    const std::wstring path = url.path(QUrl::FullyEncoded).isEmpty() ? std::wstring(L"/")
                                                                     : url.path(QUrl::FullyEncoded).toStdWString();
    if (HINTERNET connection = WinHttpConnect(session, host.c_str(), static_cast<INTERNET_PORT>(url.port(80)), 0)) {
        if (HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, 0)) {
            if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                    && WinHttpReceiveResponse(request, nullptr)) {
                char chunk[4096];
                DWORD read = 0;
                while (body.size() < 64 * 1024 && WinHttpReadData(request, chunk, sizeof(chunk), &read) && read > 0)
                    body.append(chunk, static_cast<int>(read));
            }
            WinHttpCloseHandle(request);
        }
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);

    static const QRegularExpression pattern(QStringLiteral("<friendlyName>([^<]*)</friendlyName>"));
    QString name = pattern.match(QString::fromUtf8(body)).captured(1).trimmed();
    // "[LG] webOS TV OLED65C56LB" → "LG OLED65C56LB"; a name the owner gave the TV stays as it is.
    static const QRegularExpression lgPrefix(QStringLiteral("^\\[LG\\]\\s*webOS\\s*TV\\s*"));
    if (lgPrefix.match(name).hasMatch())
        name = QStringLiteral("LG ") + name.remove(lgPrefix);
    return name;
}

}
