#pragma once

#include <QHostAddress>

#define DEFAULT_HTTP_PORT 47989
#define DEFAULT_HTTPS_PORT 47984

class NvAddress
{
public:
    NvAddress();
    explicit NvAddress(QString addr, uint16_t port);
    explicit NvAddress(QHostAddress addr, uint16_t port);

    uint16_t port() const;
    void setPort(uint16_t port);

    QString address() const;
    void setAddress(QString addr);
    void setAddress(QHostAddress addr);

    bool isNull() const;

    // True if this address falls in a range Tailscale assigns to its tailnet
    // (IPv4 CGNAT 100.64.0.0/10 or IPv6 ULA fd7a:115c:a1e0::/48). Used to keep
    // a Tailscale endpoint out of the LAN/remote address slots so route
    // selection prefers the real LAN when both are reachable.
    bool isTailscaleRange() const;

    /**
     * Loopback — 127.0.0.0/8 or ::1.
     *
     * An address that names whoever is asking, not the host. It reaches a record when
     * StreamLight runs on the machine that is also the host: the server answers over
     * loopback and reports the interface it was reached on, exactly as it does with the
     * Tailscale interface. Unlike a Tailscale address this one is not reclaimed anywhere —
     * it identifies no host and is useful to nobody.
     */
    bool isLoopback() const;

    QString toString() const;

    bool operator==(const NvAddress& other) const
    {
        return m_Address == other.m_Address &&
                m_Port == other.m_Port;
    }

    bool operator!=(const NvAddress& other) const
    {
        return !operator==(other);
    }

private:
    QString m_Address;
    uint16_t m_Port;
};
