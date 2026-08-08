#pragma once

#include <QHostAddress>
#include <QString>

// Reads the wired link speed of the interface that actually reaches a given host.
//
// Qt has no API for link speed — QNetworkInterface exposes addresses, flags and type but
// not the negotiated rate — so this is platform code. Only Windows is implemented; every
// other platform reports Unknown, which leaves the host-link-matching feature inert with a
// stated reason rather than silently doing nothing.
//
// It deliberately measures *the interface that routes to this host*, not "the Ethernet
// adapter": a docked handheld can have its cable up while actually reaching the host over
// Wi-Fi or Tailscale, and matching the host to a link that carries no traffic would be
// worse than not matching at all.
namespace LinkSpeed
{
    enum class Status
    {
        Wired,          // real Ethernet hardware — the only case the feature acts on
        Wireless,       // reaches the host over Wi-Fi
        Virtual,        // VPN / tunnel / virtual adapter (Tailscale, WireGuard, Hyper-V)
        NoLinkSpeed,    // hardware found, but it reports no usable rate
        Unavailable     // no route resolved, or unsupported platform
    };

    struct Info
    {
        Status  status = Status::Unavailable;
        quint64 mbps   = 0;             // 0 when unknown
        QString adapterName;            // friendly name, for the UI
        QString reason;                 // user-facing explanation when not Wired

        bool usable() const { return status == Status::Wired && mbps > 0; }
    };

    // Resolves the route to `host` and inspects the interface it lands on.
    // Never throws; returns Unavailable on any failure.
    Info probeForHost(const QHostAddress& host);

    // Same, for the interface carrying the default route. Used by Settings, which has no
    // host in context. It answers "what is this device's wired connection", which is not
    // quite the same question as "how do we reach that host" — a specific host can still
    // be reached over Wi-Fi or Tailscale — so host-specific UI must use probeForHost().
    Info probeDefaultRoute();
}
