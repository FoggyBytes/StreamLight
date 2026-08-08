#include "backend/computermanager.h"
#include "streaming/session.h"
#include "../StreamTweakBridge.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>

class ComputerModel : public QAbstractListModel
{
    Q_OBJECT

    enum Roles
    {
        NameRole = Qt::UserRole,
        OnlineRole,
        PairedRole,
        BusyRole,
        WakeableRole,
        StatusUnknownRole,
        ServerSupportedRole,
        DetailsRole,
        AddressRole,
        GpuModelRole,
        IsTailscaleCloneRole,
        PhysicalAddressRole,
        TailscaleAddressRole,
        HasTailscaleRole,
        TailscaleActiveRole,
        ProfileCountRole,
        ActiveProfileSlotRole,
        ActiveProfileNameRole,
        StageColorFromRole,
        StageColorToRole,
        StageImageRole,
        StageSeedRole
    };

public:
    explicit ComputerModel(QObject* object = nullptr);

    // Must be called before any QAbstractListModel functions
    Q_INVOKABLE void initialize(ComputerManager* computerManager);

    QVariant data(const QModelIndex &index, int role) const override;

    int rowCount(const QModelIndex &parent) const override;

    virtual QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void deleteComputer(int computerIndex);

    Q_INVOKABLE QString generatePinString();

    Q_INVOKABLE void pairComputer(int computerIndex, QString pin);

    Q_INVOKABLE void testConnectionForComputer(int computerIndex);

    Q_INVOKABLE void wakeComputer(int computerIndex);

    Q_INVOKABLE void renameComputer(int computerIndex, QString name);

    Q_INVOKABLE Session* createSessionForCurrentGame(int computerIndex);


    // Asks the host (via StreamTweak) to power off. Requires the host to have
    // approved this client; fire-and-forget over the authenticated bridge.
    // installUpdates: install pending Windows updates before powering off.
    Q_INVOKABLE void shutdownHost(int computerIndex, bool installUpdates = false);

    Q_INVOKABLE void requestStreamTweakStatus(int computerIndex);

    /**
     * Asks the host whether it has Windows updates waiting for a reboot.
     * Emits updateStateReceived(computerIndex, pending) when the response arrives
     * (pending=false on legacy/unreachable hosts). Drives the Power dialog hint.
     */
    Q_INVOKABLE void requestUpdateState(int computerIndex);

    /**
     * Remote PIN unlock.
     *
     * requestLockState asks whether a lock or logon screen is up; the answer arrives as
     * lockStateReceived(index, supported, locked). `supported` is separate on purpose: a
     * host that predates the command answers nothing, and reading that as "not locked"
     * would send us straight past the PIN pad into a session nobody can drive.
     *
     * markUnlockSession declares the session we are about to open as unlock plumbing, so
     * the host keeps it out of its history and skips its session-start side effects.
     */
    Q_INVOKABLE void requestLockState(int computerIndex);
    Q_INVOKABLE void markUnlockSession(int computerIndex, bool begin);

    /**
     * Runs the link-speed match on its own, with no session behind it. Used right after a
     * remote unlock: the speed cannot be matched before the host has logged in — the adapter
     * would go down for a session lasting as long as typing a PIN — so it is done here, and
     * the host card only calls itself ready once this finishes.
     *
     * Emits linkMatchProgress(index, true, detail) while it runs and (index, false, "") when
     * it ends, whatever the outcome: like every other path through LinkMatcher, failure is
     * reported and then ignored.
     */
    Q_INVOKABLE void matchHostLinkSpeed(int computerIndex);

    /**
     * Whether this host has ever answered as a StreamTweak host, remembered across runs.
     * The wake flow uses it to decide how long to wait for StreamTweak to come up — and it
     * must be persisted, because an in-memory flag reads "no" on every fresh start, which is
     * "I have not asked yet", not "this host has none".
     */
    Q_INVOKABLE bool hostEverHadStreamTweak(int computerIndex);

    /**
     * Remote "Update host" feature. startUpdateCheck kicks off an async scan;
     * startUpdateInstall installs the scanned set for a scope ("SEC"/"ALL") and reboots
     * if required; requestUpdateProgress polls the job state and emits
     * updateProgressReceived(computerIndex, state) where state is the parsed JSON
     * (phase/percent/message/updates/counts), or {"phase":"IDLE"} when unreachable.
     */
    Q_INVOKABLE void startUpdateCheck(int computerIndex);
    Q_INVOKABLE void startUpdateInstall(int computerIndex, const QString& scope);
    Q_INVOKABLE void requestUpdateProgress(int computerIndex);

    /**
     * Enrolls this client with the host's StreamTweak and reports the access state.
     * Emits streamTweakAuthReceived(computerIndex, state) where state is one of
     * "authorized" / "pending" / "denied" / "none" ("none" = StreamTweak absent,
     * legacy, or unreachable). Triggers the approval prompt on the host on first
     * contact. Used to drive the per-host access badge and the "Request access"
     * option.
     */
    Q_INVOKABLE void requestStreamTweakAuth(int computerIndex);

    /**
     * Requests the store map for all StreamTweak-managed apps from the host.
     * Emits appStoresReceived(computerIndex, storesMap) when the response arrives.
     * If StreamTweak is unreachable the map will be empty.
     * Returns the cached map immediately if already fetched for this host.
     */
    Q_INVOKABLE void requestAppStores(int computerIndex);

    /**
     * Returns the last successfully fetched store map for this computer,
     * or an empty QVariantMap if no fetch has succeeded yet.
     */
    Q_INVOKABLE QVariantMap getCachedAppStores(int computerIndex) const;

    /**
     * Probes the host's StreamTweak for its Tailscale (100.x) endpoint and, if found,
     * records it on the host (unified tile). Safe to call repeatedly; no-op if the host
     * is unreachable or has no Tailscale.
     */
    Q_INVOKABLE void refreshTailscale(int computerIndex);

    /**
     * Sets this host's stage backdrop. Pass a picture path, OR a "#rrggbb" seed, OR neither
     * to clear it and fall back to the colours derived from the host name.
     *
     * The gradient pair is worked out once, here, and stored on the host — not recomputed
     * when the stage is drawn, which happens every time the user changes tab.
     */
    Q_INVOKABLE void setHostStageBackground(int computerIndex, const QString& imagePath,
                                            const QString& seedColor);

    // Wired link speed of the interface that actually routes to this host — the value the
    // host is asked to match. Returns {status, mbps, adapter, reason, usable}; `usable` is
    // false whenever the path is Wi-Fi, a tunnel, or the adapter reports no rate, and
    // `reason` is written for the UI so the feature is never inert without saying why.
    Q_INVOKABLE QVariantMap probeLocalLink(int computerIndex);

    // One-shot NETINFO so the host card can say whether a switch is coming. Cached by the
    // caller: the permission rarely changes, and the current speed is already kept live by
    // the 2 s STATUS poll. Emits hostNetInfoReceived; silently does nothing on hosts older
    // than StreamTweak 8.1.0.
    Q_INVOKABLE void requestHostNetInfo(int computerIndex);

    // The host's last finished session, for the panel on the host card. One-shot, fired when
    // a host becomes authorized: it describes something that already happened, so polling it
    // would ask the same question over and over. Emits lastSessionReceived with {has:false}
    // on hosts older than StreamTweak 8.1.0.
    Q_INVOKABLE void requestLastSession(int computerIndex);

    /// Asks the host to put its link speed back. The host never decides this for itself — it
    /// holds the streaming speed until told — so this is the only thing that ends a switch,
    /// sent when the user answers the prompt on returning to the host list, or picks the
    /// Options tile.
    Q_INVOKABLE void restoreHostLink(int computerIndex);

    /** True if Tailscale is installed on THIS (client) PC (so the host's Tailscale
     *  endpoint is actually usable from here). Drives the greyed "Tailscale" option. */
    Q_INVOKABLE bool clientHasTailscale() const;

    /**
     * Pins the host's active connection to its Tailscale endpoint for the next session
     * (used by the host's "Tailscale" option). Returns false if no Tailscale endpoint is
     * known. Call clearTailscalePreferences() when returning to the host list.
     */
    Q_INVOKABLE bool prepareTailscaleSession(int computerIndex);

    /** Clears any Tailscale session pin on all hosts (poller reverts to LAN-first). */
    Q_INVOKABLE void clearTailscalePreferences();

    // ── Per-host streaming profiles (StreamLight 4.0.0) ──────────────────────
    Q_INVOKABLE int hostProfileCount(int computerIndex) const;
    Q_INVOKABLE int hostActiveProfile(int computerIndex) const;       // slot, or -1
    Q_INVOKABLE QString hostActiveProfileName(int computerIndex) const;
    Q_INVOKABLE void setHostActiveProfile(int computerIndex, int slot);
    Q_INVOKABLE void cycleHostProfile(int computerIndex, int dir);    // -1 prev / +1 next
    Q_INVOKABLE QString hostProfileName(int computerIndex, int slot) const;
    Q_INVOKABLE void setHostProfileName(int computerIndex, int slot, const QString& name);
    Q_INVOKABLE QVariantMap hostProfileSettings(int computerIndex, int slot) const;
    Q_INVOKABLE void setHostProfileSettings(int computerIndex, int slot, const QVariantMap& ov);
    Q_INVOKABLE int addHostProfile(int computerIndex);                // new slot, or -1
    Q_INVOKABLE void removeHostProfile(int computerIndex, int slot);

    // The active profile's override as a QVariantMap (keys: width/height/fps/
    // bitrate/hdr/codec/framepacing/audio/hue). Empty when no profile is active.
    Q_INVOKABLE QVariantMap hostActiveOverride(int computerIndex) const;

signals:
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result, QString blockedPorts);
    void streamTweakStatusReceived(int computerIndex, QString status);
    void streamTweakAuthReceived(int computerIndex, QString state, QString pin);

    /** @param info {allowsLinkControl, currentMbps} — empty map on hosts without NETINFO. */
    void hostNetInfoReceived(int computerIndex, QVariantMap info);

    /** @param s {has, ago, duration, grade, gradeColor, rttMs, rttPeakMs, hostLatMs,
     *            dropsPct, games:[{name, cover}]} — {has:false} when there is nothing to
     *            show or the host does not know the command. */
    void lastSessionReceived(int computerIndex, QVariantMap s);
    void appStoresReceived(int computerIndex, QVariantMap stores);
    void updateStateReceived(int computerIndex, bool pending);
    /** supported=false means the host does not know LOCKSTATE — not that it is unlocked. */
    void lockStateReceived(int computerIndex, bool supported, bool locked);
    /** detail is the change being made ("2.5 Gbps → 1 Gbps"), empty once finished. */
    void linkMatchProgress(int computerIndex, bool running, QString detail);
    void updateProgressReceived(int computerIndex, QVariantMap state);

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handlePairingCompleted(NvComputer* computer, QString error);

private:
    static void rememberStreamTweakSeen(const QString& uuid);

    QVector<NvComputer*> m_Computers;
    ComputerManager* m_ComputerManager;
    StreamTweakBridge m_streamTweakBridge;
    // Keyed by host UUID, not list index: the index shifts whenever the model is
    // reset (host added/removed, Tailscale clone inserted), which would otherwise
    // associate a cached store map with the wrong host.
    QHash<QString, QVariantMap> m_appStoresCache;
    // Per-host (UUID) 4-digit confirmation PIN, generated on first enrollment and
    // reused across re-polls while the host approval is pending; cleared once the
    // host approves/denies so a fresh attempt gets a new PIN.
    QHash<QString, QString> m_streamTweakPins;
};
