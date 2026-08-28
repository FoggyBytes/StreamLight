#include "computermodel.h"
#include "backend/boxartmanager.h"
#include "backend/linkspeed.h"
#include "backend/linkmatcher.h"
#include "../TailscaleManager.h"
#include "settings/appsettings.h"
#include "settings/streamingpreferences.h"

#include <QJsonArray>
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
    case ProfileCountRole:
        return HostProfileManager::get()->count(computer->uuid);
    case ActiveProfileSlotRole:
        return HostProfileManager::get()->active(computer->uuid);
    case ActiveProfileNameRole: {
        int a = HostProfileManager::get()->active(computer->uuid);
        return a >= 0 ? HostProfileManager::get()->name(computer->uuid, a) : QString();
    }
    case StageColorFromRole:
        return computer->stageColorFrom;
    case StageColorToRole:
        return computer->stageColorTo;
    case StageImageRole:
        return computer->stageImagePath;
    case StageSeedRole:
        return computer->stageSeedColor;
    case StreamTweakEnabledRole:
        return computer->streamTweakEnabled;
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
    names[ProfileCountRole] = "profileCount";
    names[ActiveProfileSlotRole] = "activeProfileSlot";
    names[ActiveProfileNameRole] = "activeProfileName";
    names[StageColorFromRole] = "stageColorFrom";
    names[StageColorToRole] = "stageColorTo";
    names[StageImageRole] = "stageImage";
    names[StageSeedRole] = "stageSeed";
    names[StreamTweakEnabledRole] = "streamTweakEnabled";

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
            StreamingPreferences* prefs = AppSettingsManager::get()->buildPrefs(
                StreamingPreferences::get(), computer->uuid, app.id);
            Session* session = new Session(computer, app, prefs);
            prefs->setParent(session);
            return session;
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

QVariantMap ComputerModel::probeLocalLink(int computerIndex)
{
    QVariantMap out;
    out[QStringLiteral("usable")] = false;
    out[QStringLiteral("mbps")]   = 0;

    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return out;

    NvComputer* computer = m_Computers[computerIndex];
    QString name, address;
    {
        QReadLocker lock(&computer->lock);
        name    = computer->name;
        address = computer->activeAddress.address();
    }
    if (address.isEmpty()) return out;

    LinkSpeed::Info info = LinkSpeed::probeForHost(QHostAddress(address));

    static const char* const kStatus[] = { "wired", "wireless", "virtual", "nolinkspeed", "unavailable" };
    out[QStringLiteral("status")]  = QString::fromLatin1(kStatus[static_cast<int>(info.status)]);
    out[QStringLiteral("mbps")]    = static_cast<qulonglong>(info.mbps);
    out[QStringLiteral("adapter")] = info.adapterName;
    out[QStringLiteral("reason")]  = info.reason;
    out[QStringLiteral("usable")]  = info.usable();

    // Logged on every probe: this is the number the whole host-matching feature rests on,
    // and it changes with the dock, the cable and the network the handheld is on.
    qInfo() << "Local link to" << name << "via" << address << "→"
            << (info.usable() ? QString::number(info.mbps) + " Mbps" : QStringLiteral("unusable"))
            << "[" << out[QStringLiteral("status")].toString() << "]"
            << "adapter:" << (info.adapterName.isEmpty() ? QStringLiteral("(none)") : info.adapterName)
            << (info.reason.isEmpty() ? QString() : "— " + info.reason);

    return out;
}

void ComputerModel::requestHostNetInfo(int computerIndex)
{
    // ⚠️ Every bridge call in this class carries this guard, and it is the safety net rather
    // than the primary gate: the probe timers and the feature UI are already bound to the
    // role, so nothing should reach here with the switch off. It exists because "should" is
    // not "cannot" — a path added later would otherwise talk to a host the user has opted
    // out of, silently. streamTweakEnabled() returns false for an out-of-range index too,
    // so the failure direction is always "off".
    //
    // probeStreamTweakPresence() is the single deliberate exception; see its declaration.
    //
    // Where a caller is waiting on a signal, the guard emits the same "nothing" answer the
    // empty-address path emits. Returning silently would hang the waiter.
    if (!streamTweakEnabled(computerIndex)) return;

    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;

    NvComputer* computer = m_Computers[computerIndex];
    QString address;
    {
        QReadLocker lock(&computer->lock);
        address = computer->activeAddress.address();
    }
    if (address.isEmpty()) return;

    m_streamTweakBridge.requestNetInfo(address, [this, computerIndex](const QString& reply) {
        QVariantMap out;
        if (!reply.isEmpty() && !reply.startsWith(QStringLiteral("ERR"))) {
            QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
            if (doc.isObject()) {
                QJsonObject o = doc.object();
                out[QStringLiteral("allowsLinkControl")] =
                    o.value(QStringLiteral("allow_client_control")).toBool();
                out[QStringLiteral("currentMbps")] =
                    static_cast<qulonglong>(o.value(QStringLiteral("current_mbps")).toDouble());
                // Needed to follow a restore to completion: "switched" is true for as long as
                // the link sits on a speed a client asked for, and drops to false only once the
                // adapter is back on its own setting and settled.
                out[QStringLiteral("state")]    = o.value(QStringLiteral("state")).toString();
                out[QStringLiteral("switched")] = o.value(QStringLiteral("switched")).toBool();
                // The host's own view of whether something is streaming. Used to decide
                // whether it is even sensible to ask about putting the link back: a session
                // that is still running — or merely paused and resumable — is not finished.
                out[QStringLiteral("sessionActive")] =
                    o.value(QStringLiteral("session_active")).toBool();
            }
        }
        emit hostNetInfoReceived(computerIndex, out);
    });
}

void ComputerModel::requestLastSession(int computerIndex)
{
    if (!streamTweakEnabled(computerIndex)) return;   // see requestHostNetInfo()
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;

    NvComputer* computer = m_Computers[computerIndex];
    QString address;
    {
        QReadLocker lock(&computer->lock);
        address = computer->activeAddress.address();
    }
    if (address.isEmpty()) return;

    m_streamTweakBridge.requestLastSession(address, [this, computerIndex](const QString& reply) {
        QVariantMap out;
        out[QStringLiteral("has")] = false;

        if (!reply.isEmpty() && !reply.startsWith(QStringLiteral("ERR"))) {
            QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
            if (doc.isObject()) {
                QJsonObject o = doc.object();
                if (o.value(QStringLiteral("has")).toBool()) {
                    out[QStringLiteral("has")]        = true;
                    out[QStringLiteral("ago")]        = o.value(QStringLiteral("ago")).toString();
                    out[QStringLiteral("duration")]   = o.value(QStringLiteral("duration")).toString();
                    out[QStringLiteral("hasGrade")]   = o.value(QStringLiteral("has_grade")).toBool();
                    out[QStringLiteral("grade")]      = o.value(QStringLiteral("grade")).toString();
                    out[QStringLiteral("gradeColor")] = o.value(QStringLiteral("grade_color")).toString();

                    // -1 means the host never measured it. Kept as -1 rather than folded to 0
                    // so the QML can tell "no data" from "a genuinely excellent zero".
                    out[QStringLiteral("rttMs")]      = o.value(QStringLiteral("rtt_ms")).toDouble(-1);
                    out[QStringLiteral("rttPeakMs")]  = o.value(QStringLiteral("rtt_peak_ms")).toDouble(-1);
                    out[QStringLiteral("hostLatMs")]  = o.value(QStringLiteral("host_latency_ms")).toDouble(-1);
                    out[QStringLiteral("dropsPct")]   = o.value(QStringLiteral("drops_pct")).toDouble(-1);

                    // Not games.length: the host caps the list it sends, so this is the only
                    // way the client can know a session credited more than it can draw.
                    out[QStringLiteral("gamesTotal")] =
                        o.value(QStringLiteral("games_total")).toInt(0);

                    /*
                     * The covers are resolved against this host's own app list first, and
                     * only fall back to what the reply carries.
                     *
                     * The client already holds the real artwork: the host page fetches it
                     * from the streaming server (/appasset, keyed by app id) at full size
                     * and caches it per host. The inline thumbnail in this reply exists
                     * because the panel had no way to reach that cache — LASTSESSION names
                     * the games but carries no app id, so there was nothing to look them up
                     * by. Matching on the name closes that: both sides get it from the same
                     * apps.json entry, so the strings agree by construction.
                     *
                     * Worth the lookup because the thumbnail is the weaker picture by some
                     * way — it is sized for the panel's design size and is stretched, not
                     * fitted, so a cover that is not 2:3 arrives distorted. The cached one
                     * is the same file the library is showing.
                     *
                     * ⚠️ Deliberately does NOT fetch anything it does not already have.
                     * This is a summary on the home screen, not a library being browsed:
                     * kicking off downloads for games the user has not opened would be
                     * doing work nobody asked for, and BoxArtManager::loadBoxArt() would
                     * hand back its placeholder in the meantime — which is worse than the
                     * thumbnail we already have in hand.
                     */
                    QHash<QString, int> appIdByName;
                    NvComputer* computer = nullptr;
                    if (computerIndex >= 0 && computerIndex < m_Computers.count()) {
                        computer = m_Computers[computerIndex];
                        QReadLocker lock(&computer->lock);
                        for (const NvApp& app : computer->appList) {
                            appIdByName.insert(app.name.toLower(), app.id);
                        }
                    }

                    QVariantList games;
                    for (const QJsonValue& v : o.value(QStringLiteral("games")).toArray()) {
                        QJsonObject g = v.toObject();
                        QVariantMap game;
                        const QString gameName = g.value(QStringLiteral("name")).toString();
                        game[QStringLiteral("name")] = gameName;

                        QUrl localCover;
                        if (computer != nullptr) {
                            const auto it = appIdByName.constFind(gameName.toLower());
                            if (it != appIdByName.constEnd()) {
                                localCover = BoxArtManager::cachedBoxArt(computer, it.value());
                            }
                        }

                        if (!localCover.isEmpty()) {
                            game[QStringLiteral("cover")] = localCover.toString();
                        }
                        else {
                            // Nothing cached for it — a game that has been removed from the
                            // host's library, or one this client has simply never opened. The
                            // host's inline thumbnail is the fallback, handed to QML as a
                            // data: URI so an Image can take it with no image provider and
                            // nothing written to disk.
                            //
                            // ⚠️ The MIME is read off the payload rather than assumed. A
                            // data: URI has to declare a type, and hardcoding one ties this
                            // line to whatever the host happens to encode. Base64 is 6 bits
                            // per character, so the first bytes always land on the same
                            // leading characters: "iVBORw0KGgo" is the PNG signature, "/9j/"
                            // is FF D8 FF (JPEG).
                            const QString cover = g.value(QStringLiteral("cover")).toString();
                            if (!cover.isEmpty()) {
                                const QLatin1String mime =
                                    cover.startsWith(QLatin1String("iVBORw0KGgo"))
                                        ? QLatin1String("image/png")
                                        : QLatin1String("image/jpeg");
                                game[QStringLiteral("cover")] =
                                    QStringLiteral("data:") + mime + QStringLiteral(";base64,") + cover;
                            }
                            else {
                                game[QStringLiteral("cover")] = QString();
                            }
                        }

                        games.append(game);
                    }
                    out[QStringLiteral("games")] = games;
                }
            }
        }
        emit lastSessionReceived(computerIndex, out);
    });
}

void ComputerModel::restoreHostLink(int computerIndex)
{
    if (!streamTweakEnabled(computerIndex)) return;   // see requestHostNetInfo()
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;

    NvComputer* computer = m_Computers[computerIndex];
    QString address;
    {
        QReadLocker lock(&computer->lock);
        address = computer->activeAddress.address();
    }
    if (address.isEmpty()) return;

    // Fire-and-forget: a host that predates 8.1.0, or is already back on its own speed,
    // simply has nothing to do. The UI follows the outcome through NETINFO regardless.
    m_streamTweakBridge.sendRestore(address, [](const QString&) {});
}

void ComputerModel::setHostStageBackground(int computerIndex, const QString& imagePath,
                                           const QString& seedColor)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;

    NvComputer* computer = m_Computers[computerIndex];
    QString uuid;
    {
        QReadLocker lock(&computer->lock);
        uuid = computer->uuid;
    }
    if (uuid.isEmpty()) return;

    m_ComputerManager->setStageBackground(uuid, imagePath, seedColor);

    // computerStateChanged only reaches the model on the next poll tick, and the user has
    // just picked a colour — repaint now so the stage answers the click.
    QModelIndex idx = createIndex(computerIndex, 0);
    emit dataChanged(idx, idx, { StageColorFromRole, StageColorToRole, StageImageRole, StageSeedRole });
}

bool ComputerModel::streamTweakEnabled(int computerIndex) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return false;
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return computer->streamTweakEnabled;
}

void ComputerModel::setStreamTweakEnabled(int computerIndex, bool enabled)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;

    NvComputer* computer = m_Computers[computerIndex];
    QString uuid;
    {
        QReadLocker lock(&computer->lock);
        uuid = computer->uuid;
    }
    if (uuid.isEmpty()) return;

    m_ComputerManager->setStreamTweakEnabled(uuid, enabled);

    // Repaint now, not on the next poll tick. Every probe timer and every feature gate is a
    // binding on this role, so switching off has to take the features away in the same frame
    // as the click — the alternative is a switch that appears to do nothing until you leave
    // the screen and come back.
    QModelIndex idx = createIndex(computerIndex, 0);
    emit dataChanged(idx, idx, { StreamTweakEnabledRole });
}

void ComputerModel::probeStreamTweakPresence(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;

    NvComputer* computer = m_Computers[computerIndex];
    QString address;
    {
        QReadLocker lock(&computer->lock);
        address = computer->activeAddress.address();
    }
    if (address.isEmpty()) {
        // Offline, or no address resolved yet — nothing to ask. The tab distinguishes this
        // from "asked and got nothing" using the host's own online state.
        emit streamTweakPresenceReceived(computerIndex, false);
        return;
    }

    // CAPS and not STATUS: it is the one verb that needs no authentication, so it answers on
    // a host that has never approved this client — which is precisely the host the user is
    // in the tab to ask about. Anything that isn't a CAPS1 line (empty on timeout, "ERR"
    // from a pre-7.1 host, a plain Sunshine box refusing the port) means not found.
    m_streamTweakBridge.requestCaps(address,
        [this, computerIndex](const QString& caps) {
            emit streamTweakPresenceReceived(computerIndex,
                                             caps.startsWith(QLatin1String("CAPS1")));
        });
}

void ComputerModel::refreshTailscale(int computerIndex)
{
    // Note this only drops the endpoint we would have LEARNED from the bridge. Tailscale
    // itself keeps working: the range classification in NvComputer is independent of us.
    if (!streamTweakEnabled(computerIndex)) return;   // see requestHostNetInfo()
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
            // NOTE: we intentionally do NOT optimistically set activeAddress = localAddress
            // here. The poller's LAN-preferred re-probe (PcMonitorThread::run) already moves
            // the active connection back to the LAN within a poll cycle whenever the LAN is
            // actually reachable. Forcing it here caused a visible flap when away (LAN down):
            // returning Home set activeAddress to the unreachable LAN IP, the next poll
            // reverted it to Tailscale, the badge bounced AVAILABLE<->TAILSCALE on every Home
            // return, and a launch right after Home stalled probing the dead LAN IP first.
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

void ComputerModel::shutdownHost(int computerIndex, bool installUpdates)
{
    // Powering off the CLIENT is not a StreamTweak feature and is not affected — that lives
    // in SystemProperties. Only the host half goes away.
    if (!streamTweakEnabled(computerIndex)) return;   // see requestHostNetInfo()

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
    if (!streamTweakEnabled(computerIndex)) {         // see requestHostNetInfo()
        emit updateStateReceived(computerIndex, false);
        return;
    }

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

// ── Remote PIN unlock ────────────────────────────────────────────────────────

void ComputerModel::requestLockState(int computerIndex)
{
    // supported=false, which every caller already reads as "this host cannot tell us" — the
    // same conclusion, arrived at without a round trip.
    if (!streamTweakEnabled(computerIndex)) {         // see requestHostNetInfo()
        emit lockStateReceived(computerIndex, false, false);
        return;
    }

    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;

    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);

    QString address = computer->activeAddress.address();
    if (address.isEmpty()) {
        emit lockStateReceived(computerIndex, false, false);
        return;
    }

    m_streamTweakBridge.requestLockState(address,
        [this, computerIndex](const QString& response) {
            // An empty reply or "ERR" is a host that does not know the command. That is a
            // different answer from "not locked", and collapsing the two would march us
            // past the PIN pad and into a session with a logon screen behind it.
            bool supported = response.contains(QLatin1String("\"locked\""));
            bool locked    = response.contains(QLatin1String("\"locked\":true"));
            emit lockStateReceived(computerIndex, supported, locked);
        });
}

void ComputerModel::matchHostLinkSpeed(int computerIndex)
{
    // ⚠️ Must emit, not just return: the wake flow's last step waits for
    // linkMatchProgress(running=false) to finish, so a silent return would hang it on a host
    // whose integration is off.
    if (!streamTweakEnabled(computerIndex)) {         // see requestHostNetInfo()
        emit linkMatchProgress(computerIndex, false, QString());
        return;
    }

    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;

    NvComputer* computer = m_Computers[computerIndex];

    // appId -1: there is no game here, so the cascade stops at the host profile. Passing a
    // real game's id would apply that game's overrides to a decision that is not about it.
    StreamingPreferences* prefs = AppSettingsManager::get()->buildPrefs(
        StreamingPreferences::get(), computer->uuid, -1, this);

    LinkMatcher* matcher = new LinkMatcher(this);
    connect(matcher, &LinkMatcher::stage, this,
            [this, computerIndex](const QString& detail) {
                emit linkMatchProgress(computerIndex, true, detail);
            });
    connect(matcher, &LinkMatcher::finished, this,
            [this, computerIndex, matcher, prefs](bool, const QString&) {
                emit linkMatchProgress(computerIndex, false, QString());
                matcher->deleteLater();
                prefs->deleteLater();
            });

    matcher->start(computer, prefs);
}

void ComputerModel::markUnlockSession(int computerIndex, bool begin)
{
    if (!streamTweakEnabled(computerIndex)) return;   // see requestHostNetInfo()
    if (computerIndex < 0 || computerIndex >= m_Computers.count())
        return;
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    QString address = computer->activeAddress.address();
    if (address.isEmpty())
        return;

    if (begin)
        m_streamTweakBridge.sendUnlockBegin(address);
    else
        m_streamTweakBridge.sendUnlockEnd(address);
}

// ── Remote "Update host" (Windows Update Agent on the host) ──────────────────

void ComputerModel::startUpdateCheck(int computerIndex)
{
    if (!streamTweakEnabled(computerIndex)) return;   // see requestHostNetInfo()
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
    if (!streamTweakEnabled(computerIndex)) return;   // see requestHostNetInfo()
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
    if (!streamTweakEnabled(computerIndex)) {         // see requestHostNetInfo()
        emit updateProgressReceived(computerIndex, QVariantMap{{ "phase", "IDLE" }});
        return;
    }

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
    if (!streamTweakEnabled(computerIndex)) {         // see requestHostNetInfo()
        emit streamTweakStatusReceived(computerIndex, QString());
        return;
    }

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
    // "none" is what a host that doesn't run StreamTweak reports, and it is what hides the
    // access chip and every Options tile gated on "authorized" — so switching the
    // integration off takes the whole UI surface with it for free, with no separate gates.
    if (!streamTweakEnabled(computerIndex)) {         // see requestHostNetInfo()
        emit streamTweakAuthReceived(computerIndex, QStringLiteral("none"), QString());
        return;
    }

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
            // ⚠️ Here, not at the outcomes below. The key means "this host runs StreamTweak",
            // and a CAPS1 reply is the proof of exactly that; whether the host then approves
            // us, holds us pending or denies us is a separate question about permission.
            // Recording it only on open/authorized left a real trap: a client sitting at
            // "pending" when the user upgraded would be seeded OFF, and with the integration
            // off it stops enrolling — so the approval could never arrive and the only way
            // out was finding the switch by hand.
            rememberStreamTweakSeen(uuid);

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
                    // (Nothing to record here: the CAPS1 reply that got us this far already
                    //  did it.)
                    emit streamTweakAuthReceived(computerIndex, state,
                                                 state == QLatin1String("pending") ? pin : QString());
                });
        });
}

// ── "This host runs StreamTweak" ─────────────────────────────────────────────
//
// ⚠️ This key outlived its original reader and now has exactly one job: seeding the per-host
// StreamTweak switch for hosts stored by a build that predates it. Without that seed, a
// default of off would be a regression shipped in a release — everyone already using
// StreamTweak would upgrade and silently lose the integration. NvComputer's QSettings
// constructor is the reader; see the note there.
//
// (It used to decide how long the wake flow waited for StreamTweak to come up. The switch
// answers that outright now, so the wake takes one fixed cap and no longer guesses.)

static const QString kSeenGroup = QStringLiteral("streamtweakSeen");

void ComputerModel::rememberStreamTweakSeen(const QString& uuid)
{
    if (uuid.isEmpty()) return;
    QSettings settings;
    settings.setValue(kSeenGroup + QLatin1Char('/') + uuid, true);
}

void ComputerModel::requestAppStores(int computerIndex)
{
    if (!streamTweakEnabled(computerIndex)) {         // see requestHostNetInfo()
        emit appStoresReceived(computerIndex, QVariantMap());
        return;
    }

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

// ── Per-host streaming profiles (StreamLight 4.0.0) ──────────────────────────
int ComputerModel::hostProfileCount(int computerIndex) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return 0;
    return HostProfileManager::get()->count(m_Computers[computerIndex]->uuid);
}

int ComputerModel::hostActiveProfile(int computerIndex) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return -1;
    return HostProfileManager::get()->active(m_Computers[computerIndex]->uuid);
}

QString ComputerModel::hostActiveProfileName(int computerIndex) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return QString();
    auto* m = HostProfileManager::get();
    const QString uuid = m_Computers[computerIndex]->uuid;
    int a = m->active(uuid);
    return a >= 0 ? m->name(uuid, a) : QString();
}

void ComputerModel::setHostActiveProfile(int computerIndex, int slot)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;
    HostProfileManager::get()->setActive(m_Computers[computerIndex]->uuid, slot);
    QModelIndex idx = index(computerIndex, 0);
    emit dataChanged(idx, idx, { ProfileCountRole, ActiveProfileSlotRole, ActiveProfileNameRole });
}

void ComputerModel::cycleHostProfile(int computerIndex, int dir)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;
    HostProfileManager::get()->cycle(m_Computers[computerIndex]->uuid, dir);
    QModelIndex idx = index(computerIndex, 0);
    emit dataChanged(idx, idx, { ProfileCountRole, ActiveProfileSlotRole, ActiveProfileNameRole });
}

QString ComputerModel::hostProfileName(int computerIndex, int slot) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return QString();
    return HostProfileManager::get()->name(m_Computers[computerIndex]->uuid, slot);
}

void ComputerModel::setHostProfileName(int computerIndex, int slot, const QString& name)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;
    HostProfileManager::get()->setName(m_Computers[computerIndex]->uuid, slot, name);
    QModelIndex idx = index(computerIndex, 0);
    emit dataChanged(idx, idx, { ProfileCountRole, ActiveProfileSlotRole, ActiveProfileNameRole });
}

QVariantMap ComputerModel::hostProfileSettings(int computerIndex, int slot) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return QVariantMap();
    return appOverrideToMap(HostProfileManager::get()->settings(m_Computers[computerIndex]->uuid, slot));
}

void ComputerModel::setHostProfileSettings(int computerIndex, int slot, const QVariantMap& ov)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;
    HostProfileManager::get()->setSettings(m_Computers[computerIndex]->uuid, slot, appOverrideFromMap(ov));

    // ⚠️ This was the one profile mutator that told nobody. Its six siblings — setActive,
    // cycle, setName, add, remove — all emit; changing a setting *inside* the active profile
    // did not, so the card went on promising a link switch that the profile had just turned
    // off, until something unrelated (leaving the page and coming back) happened to
    // re-evaluate the binding. The slot and the name are unchanged here, but they are what
    // the QML re-reads the override on, so they are the roles to announce.
    QModelIndex idx = index(computerIndex, 0);
    emit dataChanged(idx, idx, { ProfileCountRole, ActiveProfileSlotRole, ActiveProfileNameRole });
}

int ComputerModel::addHostProfile(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return -1;
    int slot = HostProfileManager::get()->add(m_Computers[computerIndex]->uuid);
    QModelIndex idx = index(computerIndex, 0);
    emit dataChanged(idx, idx, { ProfileCountRole, ActiveProfileSlotRole, ActiveProfileNameRole });
    return slot;
}

void ComputerModel::removeHostProfile(int computerIndex, int slot)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return;
    HostProfileManager::get()->remove(m_Computers[computerIndex]->uuid, slot);
    QModelIndex idx = index(computerIndex, 0);
    emit dataChanged(idx, idx, { ProfileCountRole, ActiveProfileSlotRole, ActiveProfileNameRole });
}

QVariantMap ComputerModel::hostActiveOverride(int computerIndex) const
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) return QVariantMap();
    return appOverrideToMap(HostProfileManager::get()->activeOverride(m_Computers[computerIndex]->uuid));
}

#include "computermodel.moc"
