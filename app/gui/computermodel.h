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
        TailscaleActiveRole
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

    Q_INVOKABLE void prepareStreamTweak(int computerIndex);

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

signals:
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result, QString blockedPorts);
    void streamTweakStatusReceived(int computerIndex, QString status);
    void streamTweakAuthReceived(int computerIndex, QString state, QString pin);
    void appStoresReceived(int computerIndex, QVariantMap stores);
    void updateStateReceived(int computerIndex, bool pending);
    void updateProgressReceived(int computerIndex, QVariantMap state);

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handlePairingCompleted(NvComputer* computer, QString error);

private:
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
