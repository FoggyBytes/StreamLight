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
        IsTailscaleCloneRole
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

    Q_INVOKABLE void requestStreamTweakStatus(int computerIndex);

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
     * Persists a "don't ask again" preference per host UUID so the Tailscale
     * suggestion popup never reappears for that host. Stored in QSettings.
     */
    Q_INVOKABLE void dismissTailscaleSuggestion(QString parentUuid);

    /**
     * Creates a Tailscale-pinned clone tile for the host with the given UUID.
     * Returns true if the operation was queued.
     */
    Q_INVOKABLE bool addTailscaleClone(QString parentUuid, QString tailscaleIp);

signals:
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result, QString blockedPorts);
    void streamTweakStatusReceived(int computerIndex, QString status);
    void appStoresReceived(int computerIndex, QVariantMap stores);

    /**
     * Fired after a successful pairing when the bridge confirms Tailscale is
     * installed on the host. QML displays the TailscaleSuggestDialog.
     * Suppressed if the user previously dismissed the suggestion for this UUID
     * or if a Tailscale clone for this UUID already exists.
     */
    void tailscaleSuggestion(QString parentUuid, QString parentName, QString tailscaleIp);

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handlePairingCompleted(NvComputer* computer, QString error);

private:
    QVector<NvComputer*> m_Computers;
    ComputerManager* m_ComputerManager;
    StreamTweakBridge m_streamTweakBridge;
    QHash<int, QVariantMap> m_appStoresCache;
};
