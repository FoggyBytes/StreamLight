#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// The plain network work around the TV: finding it, telling it apart from anything else on the
// LAN, and waking it. IPv4 only — webOS TVs answer on IPv4, and Wake-on-LAN is IPv4 broadcast.
namespace TvNetwork
{

struct Candidate
{
    QString ip;
    QString mac;        // "4C-BA-D7-50-18-4C", empty when ARP gave nothing
    QString server;     // SSDP SERVER header, when found that way
    QString location;   // SSDP LOCATION header, when found that way
};

// Winsock for the whole process. Call once, before anything below.
bool startup();

// SSDP M-SEARCH for LG's second-screen service; whoever answers within waitMs.
QList<Candidate> ssdpSearch(int waitMs);

// Every IPv4 neighbour in the ARP cache with the TV's control port (3001 or 3000) open.
QList<Candidate> probeNeighbours(int perPortMs);

bool portOpen(const QString& ip, quint16 port, int timeoutMs);

// The MAC answering for ip on this subnet, asked on the wire (SendARP). Empty when nothing answers.
QString arpResolve(const QString& ip);

// The addresses the ARP cache holds for mac.
QStringList addressesFor(const QString& mac);

// A magic packet to the limited broadcast and to the subnet broadcast of every adapter that is up.
// Returns how many packets went out.
int sendWakeOnLan(const QString& mac);

// The name a TV gives itself in its UPnP description (the SSDP LOCATION), "[LG] webOS TV OLED65C56LB"
// shown as "LG OLED65C56LB"; empty when the description cannot be read within timeoutMs.
QString friendlyName(const QString& location, int timeoutMs);

// "4c:ba:d7:50:18:4c", "4CBAD750184C"… → "4C-BA-D7-50-18-4C"; empty when it is not a MAC.
QString normaliseMac(const QString& mac);
bool sameMac(const QString& a, const QString& b);

}
