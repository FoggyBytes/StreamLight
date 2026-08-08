#pragma once

#include "nvhttp.h"
#include "nvaddress.h"

#include <QThread>
#include <QReadWriteLock>
#include <QSettings>
#include <QRunnable>

class CopySafeReadWriteLock : public QReadWriteLock
{
public:
    CopySafeReadWriteLock() = default;

    // Don't actually copy the QReadWriteLock
    CopySafeReadWriteLock(const CopySafeReadWriteLock&) : QReadWriteLock() {}
    CopySafeReadWriteLock& operator=(const CopySafeReadWriteLock &) { return *this; }
};

class NvComputer
{
    friend class PcMonitorThread;
    friend class ComputerManager;
    friend class PendingQuitTask;

private:
    void sortAppList();

    bool updateAppList(QVector<NvApp> newAppList);

    bool pendingQuit;

public:
    NvComputer() = default;

    // Caller is responsible for synchronizing read access to the other host
    NvComputer(const NvComputer&) = default;

    // Caller is responsible for synchronizing read access to the other host
    NvComputer& operator=(const NvComputer &) = default;

    explicit NvComputer(NvHTTP& http, QString serverInfo);

    explicit NvComputer(QSettings& settings);

    void
    setRemoteAddress(QHostAddress);

    bool
    update(const NvComputer& that);

    bool
    wake() const;

    enum ReachabilityType
    {
        RI_UNKNOWN,
        RI_LAN,
        RI_VPN,
    };

    ReachabilityType
    getActiveAddressReachability() const;

    QVector<NvAddress>
    uniqueAddresses() const;

    /**
     * Storage key used by ComputerManager::m_KnownHosts.
     *
     * Always returns the real uuid for normal hosts (aliasSuffix empty).
     * For locally-cloned hosts (e.g. Tailscale dual-tile), returns
     * "<uuid>#<aliasSuffix>" so multiple tiles for the same physical PC
     * (same Moonlight uuid) can coexist in the map. The uuid field itself
     * is never modified — Moonlight protocol identity stays intact.
     */
    QString
    storageKey() const;

    void
    serialize(QSettings& settings, bool serializeApps) const;

    // Caller is responsible for synchronizing read access to both hosts
    bool
    isEqualSerialized(const NvComputer& that) const;

    enum PairState
    {
        PS_UNKNOWN,
        PS_PAIRED,
        PS_NOT_PAIRED
    };

    enum ComputerState
    {
        CS_UNKNOWN,
        CS_ONLINE,
        CS_OFFLINE
    };

    // Ephemeral traits
    ComputerState state;
    PairState pairState;
    NvAddress activeAddress;
    uint16_t activeHttpsPort;
    int currentGameId;
    QString gfeVersion;
    QString appVersion;
    QVector<NvDisplayMode> displayModes;
    int maxLumaPixelsHEVC;
    int serverCodecModeSupport;
    QString gpuModel;
    bool isSupportedServerVersion;
    // Transient: when true, uniqueAddresses() returns tailscaleAddress first so the
    // poller pins the active connection to Tailscale even on the LAN (used by the
    // host's "Tailscale" option to force the 100.x path). Never persisted.
    bool preferTailscaleAddress = false;

    // Persisted traits
    NvAddress localAddress;
    NvAddress remoteAddress;
    NvAddress ipv6Address;
    NvAddress manualAddress;
    // The host's Tailscale (100.x) endpoint, learned from StreamTweak's TAILSCALE
    // bridge command and stored on the host itself (no separate clone tile). Probed
    // by the poller as a fallback, so the active address becomes Tailscale when the
    // LAN path is unreachable (remote). Persisted; not overwritten by update().
    NvAddress tailscaleAddress;
    QByteArray macAddress;
    QString name;
    bool hasCustomName;
    QString uuid;
    QSslCertificate serverCert;
    QVector<NvApp> appList;
    bool isNvidiaServerSoftware;
    // Local-only tag that disambiguates multiple tiles for the same uuid.
    // Empty for the primary tile. "tailscale" for the Tailscale clone.
    QString aliasSuffix;
    // When true, NvComputer::update() will not overwrite local/remote/ipv6
    // addresses (used by Tailscale clones so the poller cannot collapse them
    // back onto the parent's LAN endpoint). Persisted to QSettings.
    bool isAddressPinned = false;
    // The stage backdrop for this host, in the order it is resolved: a picture the user
    // picked, else a colour they picked, else nothing (and the Home screen falls back to a
    // hash of the host name). The derived pair is stored alongside the source so the
    // gradient does not have to be re-extracted from a JPEG on every launch — and so it
    // survives the picture being moved or deleted.
    //
    // It lives on the host and not on a profile deliberately: "docked" and "handheld" are
    // two ways of using the SAME host and must look the same. Same reasoning, and the same
    // storage, as tailscaleAddress above.
    QString stageImagePath;
    QString stageSeedColor;    // "#rrggbb", empty when the source is a picture or unset
    QString stageColorFrom;    // derived, "#rrggbb"
    QString stageColorTo;      // derived, "#rrggbb"
    // Remember to update isEqualSerialized() when adding fields here!

    // Set when the QSettings constructor had to repair persisted addresses. Deliberately
    // NOT serialized and NOT part of isEqualSerialized(): it is not a trait of the host, it
    // is a message to the loader saying "the copy on disk is stale, write this one out".
    bool migratedOnLoad = false;

    // Synchronization
    mutable CopySafeReadWriteLock lock;

private:
    uint16_t externalPort;
};
