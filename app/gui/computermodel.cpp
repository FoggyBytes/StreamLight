#include "computermodel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QThreadPool>

ComputerModel::ComputerModel(QObject* object)
    : QAbstractListModel(object) {}

void ComputerModel::initialize(ComputerManager* computerManager)
{
    m_ComputerManager = computerManager;
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &ComputerModel::handleComputerStateChanged);
    connect(m_ComputerManager, &ComputerManager::pairingCompleted,
            this, &ComputerModel::handlePairingCompleted);

    m_Computers = m_ComputerManager->getComputers();
}

QVariant ComputerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    Q_ASSERT(index.row() < m_Computers.count());

    NvComputer* computer = m_Computers[index.row()];
    QReadLocker lock(&computer->lock);

    switch (role) {
    case NameRole:
        return computer->name;
    case OnlineRole:
        return computer->state == NvComputer::CS_ONLINE;
    case PairedRole:
        return computer->pairState == NvComputer::PS_PAIRED;
    case BusyRole:
        return computer->currentGameId != 0;
    case WakeableRole:
        return !computer->macAddress.isEmpty();
    case StatusUnknownRole:
        return computer->state == NvComputer::CS_UNKNOWN;
    case ServerSupportedRole:
        return computer->isSupportedServerVersion;
    case AddressRole: {
        // Prefer the currently active address (selected at runtime), then fall
        // back through the known addresses. Return an empty string if nothing
        // is known yet — QML displays "N/A" in that case.
        QString addr = computer->activeAddress.address();
        if (!addr.isEmpty()) return addr;
        addr = computer->localAddress.address();
        if (!addr.isEmpty()) return addr;
        addr = computer->remoteAddress.address();
        if (!addr.isEmpty()) return addr;
        addr = computer->manualAddress.address();
        if (!addr.isEmpty()) return addr;
        addr = computer->ipv6Address.address();
        return addr;
    }
    case GpuModelRole:
        return computer->gpuModel;
    case IsTailscaleCloneRole:
        return computer->aliasSuffix == QStringLiteral("tailscale");
    case DetailsRole: {
        QString state, pairState;

        switch (computer->state) {
        case NvComputer::CS_ONLINE:
            state = tr("Online");
            break;
        case NvComputer::CS_OFFLINE:
            state = tr("Offline");
            break;
        default:
            state = tr("Unknown");
            break;
        }

        switch (computer->pairState) {
        case NvComputer::PS_PAIRED:
            pairState = tr("Paired");
            break;
        case NvComputer::PS_NOT_PAIRED:
            pairState = tr("Unpaired");
            break;
        default:
            pairState = tr("Unknown");
            break;
        }

        return tr("Name: %1").arg(computer->name) + '\n' +
               tr("Status: %1").arg(state) + '\n' +
               tr("Active Address: %1").arg(computer->activeAddress.toString()) + '\n' +
               tr("UUID: %1").arg(computer->uuid) + '\n' +
               tr("Local Address: %1").arg(computer->localAddress.toString()) + '\n' +
               tr("Remote Address: %1").arg(computer->remoteAddress.toString()) + '\n' +
               tr("IPv6 Address: %1").arg(computer->ipv6Address.toString()) + '\n' +
               tr("Manual Address: %1").arg(computer->manualAddress.toString()) + '\n' +
               tr("MAC Address: %1").arg(computer->macAddress.isEmpty() ? tr("Unknown") : QString(computer->macAddress.toHex(':'))) + '\n' +
               tr("Pair State: %1").arg(pairState) + '\n' +
               tr("Running Game ID: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->currentGameId) : tr("Unknown")) + '\n' +
               tr("HTTPS Port: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->activeHttpsPort) : tr("Unknown"));
    }
    default:
        return QVariant();
    }
}

int ComputerModel::rowCount(const QModelIndex& parent) const
{
    // We should not return a count for valid index values,
    // only the parent (which will not have a "valid" index).
    if (parent.isValid()) {
        return 0;
    }

    return m_Computers.count();
}

QHash<int, QByteArray> ComputerModel::roleNames() const
{
    QHash<int, QByteArray> names;

    names[NameRole] = "name";
    names[OnlineRole] = "online";
    names[PairedRole] = "paired";
    names[BusyRole] = "busy";
    names[WakeableRole] = "wakeable";
    names[StatusUnknownRole] = "statusUnknown";
    names[ServerSupportedRole] = "serverSupported";
    names[DetailsRole] = "details";
    names[AddressRole] = "address";
    names[GpuModelRole] = "gpuModel";
    names[IsTailscaleCloneRole] = "isTailscaleClone";

    return names;
}

Session* ComputerModel::createSessionForCurrentGame(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    NvComputer* computer = m_Computers[computerIndex];

    // We must currently be streaming a game to use this function
    Q_ASSERT(computer->currentGameId != 0);

    for (NvApp& app : computer->appList) {
        if (app.id == computer->currentGameId) {
            return new Session(computer, app);
        }
    }

    // We have a current running app but it's not in our app list
    Q_ASSERT(false);
    return nullptr;
}

void ComputerModel::deleteComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    beginRemoveRows(QModelIndex(), computerIndex, computerIndex);

    // m_Computer[computerIndex] will be deleted by this call
    m_ComputerManager->deleteHost(m_Computers[computerIndex]);

    // Remove the now invalid item
    m_Computers.removeAt(computerIndex);

    endRemoveRows();
}

class DeferredWakeHostTask : public QRunnable
{
public:
    DeferredWakeHostTask(NvComputer* computer)
        : m_Computer(computer) {}

    void run()
    {
        m_Computer->wake();
    }

private:
    NvComputer* m_Computer;
};

void ComputerModel::wakeComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    DeferredWakeHostTask* wakeTask = new DeferredWakeHostTask(m_Computers[computerIndex]);
    QThreadPool::globalInstance()->start(wakeTask);
}

void ComputerModel::renameComputer(int computerIndex, QString name)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->renameHost(m_Computers[computerIndex], name);
}

QString ComputerModel::generatePinString()
{
    return m_ComputerManager->generatePinString();
}

class DeferredTestConnectionTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    void run()
    {
        unsigned int portTestResult = LiTestClientConnectivity("qt.conntest.moonlight-stream.org", 443, ML_PORT_FLAG_ALL);
        if (portTestResult == ML_TEST_RESULT_INCONCLUSIVE) {
            emit connectionTestCompleted(-1, QString());
        }
        else {
            char blockedPorts[512];
            LiStringifyPortFlags(portTestResult, "\n", blockedPorts, sizeof(blockedPorts));
            emit connectionTestCompleted(portTestResult, QString(blockedPorts));
        }
    }

signals:
    void connectionTestCompleted(int result, QString blockedPorts);
};

void ComputerModel::testConnectionForComputer(int)
{
    DeferredTestConnectionTask* testConnectionTask = new DeferredTestConnectionTask();
    QObject::connect(testConnectionTask, &DeferredTestConnectionTask::connectionTestCompleted,
                     this, &ComputerModel::connectionTestCompleted);
    QThreadPool::globalInstance()->start(testConnectionTask);
}

void ComputerModel::pairComputer(int computerIndex, QString pin)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->pairHost(m_Computers[computerIndex], pin);
}

void ComputerModel::handlePairingCompleted(NvComputer* computer, QString error)
{
    emit pairingCompleted(error.isEmpty() ? QVariant() : error);

    if (!error.isEmpty() || computer == nullptr) {
        return;
    }

    // Only probe for Tailscale on primary tiles (not on already-cloned ones).
    QString parentUuid, parentName, probeAddress;
    {
        QReadLocker lock(&computer->lock);
        if (!computer->aliasSuffix.isEmpty()) {
            return;
        }
        parentUuid = computer->uuid;
        parentName = computer->name;
        probeAddress = computer->activeAddress.address();
    }
    if (probeAddress.isEmpty() || parentUuid.isEmpty()) {
        return;
    }

    // Skip if user already dismissed this UUID, or a clone already exists.
    QSettings settings;
    if (settings.value(QStringLiteral("tailscaleDismissed/") + parentUuid, false).toBool()) {
        return;
    }
    for (NvComputer* c : std::as_const(m_Computers)) {
        QReadLocker cLock(&c->lock);
        if (c->uuid == parentUuid && c->aliasSuffix == QStringLiteral("tailscale")) {
            return;
        }
    }

    // Single-shot connect: the next tailscaleReceived signal carries the answer.
    QMetaObject::Connection* conn = new QMetaObject::Connection();
    *conn = connect(&m_streamTweakBridge, &StreamTweakBridge::tailscaleReceived,
        [this, conn, parentUuid, parentName](const QString& tailscaleIp) {
            disconnect(*conn);
            delete conn;
            if (tailscaleIp.isEmpty() || tailscaleIp == QStringLiteral("NOT_DETECTED")) {
                return;
            }
            emit tailscaleSuggestion(parentUuid, parentName, tailscaleIp);
        });

    m_streamTweakBridge.requestTailscale(probeAddress);
}

void ComputerModel::dismissTailscaleSuggestion(QString parentUuid)
{
    if (parentUuid.isEmpty()) return;
    QSettings settings;
    settings.setValue(QStringLiteral("tailscaleDismissed/") + parentUuid, true);
}

bool ComputerModel::addTailscaleClone(QString parentUuid, QString tailscaleIp)
{
    if (m_ComputerManager == nullptr) return false;
    return m_ComputerManager->addTailscaleClone(parentUuid, tailscaleIp);
}

void ComputerModel::handleComputerStateChanged(NvComputer* computer)
{
    QVector<NvComputer*> newComputerList = m_ComputerManager->getComputers();

    // Reset the model if the structural layout of the list has changed
    if (m_Computers != newComputerList) {
        beginResetModel();
        m_Computers = newComputerList;
        endResetModel();
    }
    else {
        // Let the view know that this specific computer changed
        int index = m_Computers.indexOf(computer);
        emit dataChanged(createIndex(index, 0), createIndex(index, 0));
    }
}

void ComputerModel::prepareStreamTweak(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;

    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);

    QString address = computer->activeAddress.address();
    if (address.isEmpty())
        return;

    m_streamTweakBridge.sendPrepare(address);
}

void ComputerModel::requestStreamTweakStatus(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;

    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);

    QString address = computer->activeAddress.address();
    if (address.isEmpty()) {
        emit streamTweakStatusReceived(computerIndex, QString());
        return;
    }

    // Single-shot connection: disconnect after first response
    QMetaObject::Connection* conn = new QMetaObject::Connection();
    *conn = connect(&m_streamTweakBridge, &StreamTweakBridge::statusReceived,
        [this, computerIndex, conn](const QString& status) {
            disconnect(*conn);
            delete conn;
            emit streamTweakStatusReceived(computerIndex, status);
        });

    m_streamTweakBridge.requestStatus(address);
}

void ComputerModel::requestAppStores(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;

    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);

    QString address = computer->activeAddress.address();
    if (address.isEmpty()) {
        emit appStoresReceived(computerIndex, QVariantMap());
        return;
    }

    QMetaObject::Connection* conn = new QMetaObject::Connection();
    *conn = connect(&m_streamTweakBridge, &StreamTweakBridge::appStoresReceived,
        [this, computerIndex, conn](const QString& json) {
            disconnect(*conn);
            delete conn;

            QVariantMap stores;
            if (!json.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    for (auto it = obj.begin(); it != obj.end(); ++it) {
                        stores[it.key()] = it.value().toString();
                    }
                }
            }

            m_appStoresCache[computerIndex] = stores;
            emit appStoresReceived(computerIndex, stores);
        });

    m_streamTweakBridge.requestAppStores(address);
}

QVariantMap ComputerModel::getCachedAppStores(int computerIndex) const
{
    return m_appStoresCache.value(computerIndex, QVariantMap());
}

#include "computermodel.moc"
