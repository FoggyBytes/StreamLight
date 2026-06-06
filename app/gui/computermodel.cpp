#include "computermodel.h"
#include "../TailscaleManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
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
    case PhysicalAddressRole:
        // The host's LAN endpoint (kept even when currently reached via Tailscale).
        return computer->localAddress.address();
    case TailscaleAddressRole:
        return computer->tailscaleAddress.address();
    case HasTailscaleRole:
        return !computer->tailscaleAddress.isNull();
    case TailscaleActiveRole:
        // True when the host is currently reached only through Tailscale (LAN down).
        return !computer->tailscaleAddress.isNull() &&
               computer->activeAddress == computer->tailscaleAddress;
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
    names[PhysicalAddressRole] = "physicalAddress";
    names[TailscaleAddressRole] = "tailscaleAddress";
    names[HasTailscaleRole] = "hasTailscale";
    names[TailscaleActiveRole] = "tailscaleActive";

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

    // After pairing, learn the host's Tailscale endpoint (if StreamTweak reports one)
    // and record it on the host itself — the unified tile, no separate clone.
    refreshTailscale(m_Computers.indexOf(computer));
}

void ComputerModel::refreshTailscale(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;

    NvComputer* computer = m_Computers[computerIndex];
    QString uuid, probeAddress;
    {
        QReadLocker lock(&computer->lock);
        uuid = computer->uuid;
        probeAddress = computer->activeAddress.address();
    }
    if (uuid.isEmpty() || probeAddress.isEmpty()) return;

    // The callback is bound to this probe, so concurrent requests on the shared
    // bridge can never deliver one host's answer to another.
    m_streamTweakBridge.requestTailscale(probeAddress,
        [this, uuid](const QString& tailscaleIp) {
            if (tailscaleIp.isEmpty() || tailscaleIp == QStringLiteral("NOT_DETECTED")) {
                return;
            }
            if (m_ComputerManager != nullptr) {
                m_ComputerManager->setTailscaleAddress(uuid, tailscaleIp);
            }
        });
}

bool ComputerModel::clientHasTailscale() const
{
    return !TailscaleManager::discoverExecutable().isEmpty();
}

bool ComputerModel::prepareTailscaleSession(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return false;

    NvComputer* computer = m_Computers[computerIndex];
    QWriteLocker lock(&computer->lock);
    if (computer->tailscaleAddress.isNull()) return false;

    // Force the active connection onto Tailscale for this session. preferTailscaleAddress
    // keeps the poller from collapsing back onto the LAN while browsing apps / streaming.
    computer->preferTailscaleAddress = true;
    computer->activeAddress = computer->tailscaleAddress;
    return true;
}

void ComputerModel::clearTailscalePreferences()
{
    for (int i = 0; i < m_Computers.count(); i++) {
        NvComputer* c = m_Computers[i];
        bool changed = false;
        {
            QWriteLocker lock(&c->lock);
            if (c->preferTailscaleAddress) {
                c->preferTailscaleAddress = false;
                changed = true;
            }
            // If the active connection was forced onto Tailscale, revert it to the LAN
            // endpoint so the poller re-prefers LAN (and the badge flips back to
            // "AVAILABLE"). If the LAN is actually down the poller falls back to
            // Tailscale on its own.
            if (!c->localAddress.isNull() && c->activeAddress == c->tailscaleAddress) {
                c->activeAddress = c->localAddress;
                changed = true;
            }
        }
        if (changed) {
            emit dataChanged(index(i, 0), index(i, 0));
        }
    }
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

void ComputerModel::shutdownHost(int computerIndex, bool installUpdates)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;

    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);

    QString address = computer->activeAddress.address();
    if (address.isEmpty())
        return;

    m_streamTweakBridge.sendShutdown(address, installUpdates);
}

void ComputerModel::requestUpdateState(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;

    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);

    QString address = computer->activeAddress.address();
    if (address.isEmpty()) {
        emit updateStateReceived(computerIndex, false);
        return;
    }

    m_streamTweakBridge.requestUpdateState(address,
        [this, computerIndex](const QString& response) {
            // {"pending":true} → true; anything else (incl. "ERR" from a legacy host,
            // "" on timeout, or {"pending":false}) → false.
            bool pending = response.contains(QLatin1String("\"pending\":true"));
            emit updateStateReceived(computerIndex, pending);
        });
}

// ── Remote "Update host" (Windows Update Agent on the host) ──────────────────

void ComputerModel::startUpdateCheck(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    QString address = computer->activeAddress.address();
    if (address.isEmpty())
        return;
    m_streamTweakBridge.sendUpdateCheck(address);
}

void ComputerModel::startUpdateInstall(int computerIndex, const QString& scope)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    QString address = computer->activeAddress.address();
    if (address.isEmpty())
        return;
    m_streamTweakBridge.sendUpdateNow(address, scope);
}

void ComputerModel::requestUpdateProgress(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    QString address = computer->activeAddress.address();
    if (address.isEmpty()) {
        emit updateProgressReceived(computerIndex, QVariantMap{{ "phase", "IDLE" }});
        return;
    }

    m_streamTweakBridge.requestUpdateProgress(address,
        [this, computerIndex](const QString& response) {
            // Empty/"ERR" (host unreachable, e.g. rebooting, or legacy) → IDLE so the UI
            // can resolve the job. Otherwise parse the JSON snapshot into a QVariantMap.
            if (response.isEmpty() || response.startsWith(QLatin1String("ERR"))) {
                emit updateProgressReceived(computerIndex, QVariantMap{{ "phase", "IDLE" }});
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
            if (!doc.isObject()) {
                emit updateProgressReceived(computerIndex, QVariantMap{{ "phase", "IDLE" }});
                return;
            }
            emit updateProgressReceived(computerIndex, doc.object().toVariantMap());
        });
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

    // Per-request callback: the response is delivered only to this caller.
    m_streamTweakBridge.requestStatus(address,
        [this, computerIndex](const QString& status) {
            if (status == QLatin1String("ERR_UNAUTHORIZED")) {
                // Authorization was lost (e.g. revoked on the host while we were
                // authorized). Hide the NIC line and refresh the access state so the
                // badge flips back and the PIN/approval flow resumes automatically —
                // never surface the raw protocol error to the user.
                emit streamTweakStatusReceived(computerIndex, QString());
                requestStreamTweakAuth(computerIndex);
                return;
            }
            emit streamTweakStatusReceived(computerIndex, status);
        });
}

void ComputerModel::requestStreamTweakAuth(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;

    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);

    QString address = computer->activeAddress.address();
    QString uuid    = computer->uuid;
    if (address.isEmpty()) {
        emit streamTweakAuthReceived(computerIndex, QStringLiteral("none"), QString());
        return;
    }

    // First ask whether the host enforces authentication. If it doesn't
    // ("auth=optional", i.e. Require-auth off), the integration works without
    // approval — report "open" and never enroll or prompt for a PIN. Legacy or
    // non-StreamTweak hosts don't understand CAPS → "none" (badge hidden).
    m_streamTweakBridge.requestCaps(address,
        [this, computerIndex, uuid, address](const QString& caps) {
            if (!caps.startsWith(QLatin1String("CAPS1"))) {
                m_streamTweakPins.remove(uuid);
                emit streamTweakAuthReceived(computerIndex, QStringLiteral("none"), QString());
                return;
            }
            if (caps.contains(QLatin1String("auth=optional"))) {
                m_streamTweakPins.remove(uuid);
                emit streamTweakAuthReceived(computerIndex, QStringLiteral("open"), QString());
                return;
            }

            // auth=required → enroll, reusing one stable 4-digit PIN per host while
            // pending so the host keeps showing the same number to compare.
            QString pin = m_streamTweakPins.value(uuid);
            if (pin.isEmpty()) {
                pin = QString::number(QRandomGenerator::global()->bounded(10000)).rightJustified(4, '0');
                m_streamTweakPins.insert(uuid, pin);
            }
            m_streamTweakBridge.enroll(address, pin,
                [this, computerIndex, uuid, pin](const QString& reply) {
                    QString state;
                    if (reply == QLatin1String("ENROLLED"))     state = QStringLiteral("authorized");
                    else if (reply == QLatin1String("PENDING")) state = QStringLiteral("pending");
                    else if (reply == QLatin1String("DENIED"))  state = QStringLiteral("denied");
                    else                                         state = QStringLiteral("none");
                    // The PIN matters only while pending; drop it otherwise so a later
                    // re-request starts a fresh attempt with a new PIN.
                    if (state != QLatin1String("pending"))
                        m_streamTweakPins.remove(uuid);
                    emit streamTweakAuthReceived(computerIndex, state,
                                                 state == QLatin1String("pending") ? pin : QString());
                });
        });
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

    // Capture the UUID (stable across model resets) for cache keying, and bind
    // the response to this caller so concurrent requests never cross-talk.
    QString uuid = computer->uuid;
    m_streamTweakBridge.requestAppStores(address,
        [this, computerIndex, uuid](const QString& json) {
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

            if (!uuid.isEmpty()) {
                m_appStoresCache[uuid] = stores;
            }
            emit appStoresReceived(computerIndex, stores);
        });
}

QVariantMap ComputerModel::getCachedAppStores(int computerIndex) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        return QVariantMap();
    }

    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return m_appStoresCache.value(computer->uuid, QVariantMap());
}

#include "computermodel.moc"
