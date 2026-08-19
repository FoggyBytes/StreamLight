#include "appmodel.h"

#include "settings/appsettings.h"
#include "settings/streamingpreferences.h"

AppModel::AppModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_BoxArtManager, &BoxArtManager::boxArtLoadComplete,
            this, &AppModel::handleBoxArtLoaded);
}

void AppModel::initialize(ComputerManager* computerManager, int computerIndex, bool showHiddenGames)
{
    // Re-initialising is legitimate (the unlock flow reuses one model across wakes) and used
    // to stack a second connection on top of the first, so every state change was handled
    // twice. AppsScreen never hit it because it builds a fresh model each time.
    if (m_ComputerManager != nullptr) {
        disconnect(m_ComputerManager, &ComputerManager::computerStateChanged,
                   this, &AppModel::handleComputerStateChanged);
    }

    m_ComputerManager = computerManager;
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &AppModel::handleComputerStateChanged);

    Q_ASSERT(computerIndex < m_ComputerManager->getComputers().count());
    m_Computer = m_ComputerManager->getComputers().at(computerIndex);
    m_CurrentGameId = m_Computer->currentGameId;
    m_ShowHiddenGames = showHiddenGames;

    updateAppList(m_Computer->appList);
}

int AppModel::getRunningAppId()
{
    return m_CurrentGameId;
}

QString AppModel::getRunningAppName()
{
    if (m_CurrentGameId != 0) {
        for (int i = 0; i < m_AllApps.count(); i++) {
            if (m_AllApps[i].id == m_CurrentGameId) {
                return m_AllApps[i].name;
            }
        }
    }

    return nullptr;
}

// The artwork for the app currently running on the host, so the screens that close it can
// stand on the same picture the rest of the app uses. Looks in the full list rather than
// the visible one for the same reason getRunningAppName() does: a hidden game is still the
// one that is running.
QUrl AppModel::getRunningAppBoxArt()
{
    if (m_CurrentGameId != 0) {
        for (int i = 0; i < m_AllApps.count(); i++) {
            if (m_AllApps[i].id == m_CurrentGameId) {
                return m_BoxArtManager.loadBoxArt(m_Computer, m_AllApps[i]);
            }
        }
    }

    return QUrl();
}

int AppModel::indexOfAppNamed(const QString& name) const
{
    for (int i = 0; i < m_VisibleApps.count(); i++) {
        if (m_VisibleApps.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

Session* AppModel::createSessionForApp(int appIndex)
{
    Q_ASSERT(appIndex < m_VisibleApps.count());
    NvApp app = m_VisibleApps.at(appIndex);

    // Apply this game's per-app overrides on top of a clone of the global
    // preferences (the global object is never mutated). The clone is owned by
    // the Session.
    StreamingPreferences* prefs = AppSettingsManager::get()->buildPrefs(
        StreamingPreferences::get(), m_Computer->uuid, app.id);
    Session* session = new Session(m_Computer, app, prefs);
    prefs->setParent(session);
    return session;
}

QVariantMap AppModel::getAppOverride(int appIndex)
{
    QVariantMap m;
    if (appIndex < 0 || appIndex >= m_VisibleApps.count()) {
        return m;
    }
    // ⚠️ Through the shared helpers, never a copy of them. These two functions used to
    // convert by hand and had fallen a release behind: "hue" was missing from both, so the
    // dialog's Philips Hue row wrote a key nobody read and always came back reading Global.
    // A per-game override is stored, cascaded and displayed by three different call sites,
    // and a fourth private idea of which keys exist is how one of them quietly stops working.
    return appOverrideToMap(
        AppSettingsManager::get()->getOverride(m_Computer->uuid, m_VisibleApps.at(appIndex).id));
}

void AppModel::setAppOverride(int appIndex, const QVariantMap& src)
{
    if (appIndex < 0 || appIndex >= m_VisibleApps.count()) {
        return;
    }
    // Same shared helpers as the read above. The caller rebuilds the whole map from its
    // controls on every change, so a key it does not offer is an override being dropped —
    // which is correct here, because the per-game dialog is the only thing that writes this.
    AppSettingsManager::get()->setOverride(m_Computer->uuid, m_VisibleApps.at(appIndex).id,
                                           appOverrideFromMap(src));
    QModelIndex idx = index(appIndex, 0);
    emit dataChanged(idx, idx, { OverriddenRole });
}

bool AppModel::appHasOverride(int appIndex)
{
    if (appIndex < 0 || appIndex >= m_VisibleApps.count()) {
        return false;
    }
    return AppSettingsManager::get()->hasOverride(m_Computer->uuid, m_VisibleApps.at(appIndex).id);
}

void AppModel::clearAppOverride(int appIndex)
{
    if (appIndex < 0 || appIndex >= m_VisibleApps.count()) {
        return;
    }
    AppSettingsManager::get()->clearOverride(m_Computer->uuid, m_VisibleApps.at(appIndex).id);
    QModelIndex idx = index(appIndex, 0);
    emit dataChanged(idx, idx, { OverriddenRole });
}

int AppModel::getDirectLaunchAppIndex()
{
    for (int i = 0; i < m_VisibleApps.count(); i++) {
        if (m_VisibleApps[i].directLaunch) {
            return i;
        }
    }

    return -1;
}

int AppModel::rowCount(const QModelIndex &parent) const
{
    // For list models only the root node (an invalid parent) should return the list's size. For all
    // other (valid) parents, rowCount() should return 0 so that it does not become a tree model.
    if (parent.isValid())
        return 0;

    return m_VisibleApps.count();
}

QVariant AppModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    Q_ASSERT(index.row() < m_VisibleApps.count());
    NvApp app = m_VisibleApps.at(index.row());

    switch (role)
    {
    case NameRole:
        return app.name;
    case RunningRole:
        return m_Computer->currentGameId == app.id;
    case BoxArtRole:
        // FIXME: const-correctness
        return const_cast<BoxArtManager&>(m_BoxArtManager).loadBoxArt(m_Computer, app);
    case HiddenRole:
        return app.hidden;
    case AppIdRole:
        return app.id;
    case DirectLaunchRole:
        return app.directLaunch;
    case AppCollectorGameRole:
        return app.isAppCollectorGame;
    case OverriddenRole:
        return AppSettingsManager::get()->hasOverride(m_Computer->uuid, app.id);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> AppModel::roleNames() const
{
    QHash<int, QByteArray> names;

    names[NameRole] = "name";
    names[RunningRole] = "running";
    names[BoxArtRole] = "boxart";
    names[HiddenRole] = "hidden";
    names[AppIdRole] = "appid";
    names[DirectLaunchRole] = "directLaunch";
    names[AppCollectorGameRole] = "appCollectorGame";
    names[OverriddenRole] = "overridden";

    return names;
}

void AppModel::quitRunningApp()
{
    m_ComputerManager->quitRunningApp(m_Computer);
}

bool AppModel::isAppCurrentlyVisible(const NvApp& app)
{
    for (const NvApp& visibleApp : std::as_const(m_VisibleApps)) {
        if (app.id == visibleApp.id) {
            return true;
        }
    }

    return false;
}

QVector<NvApp> AppModel::getVisibleApps(const QVector<NvApp>& appList)
{
    QVector<NvApp> visibleApps;

    for (const NvApp& app : appList) {
        // Don't immediately hide games that were previously visible. This
        // allows users to easily uncheck the "Hide App" checkbox if they
        // check it by mistake.
        if (m_ShowHiddenGames || !app.hidden || isAppCurrentlyVisible(app)) {
            visibleApps.append(app);
        }
    }

    return visibleApps;
}

void AppModel::updateAppList(QVector<NvApp> newList)
{
    m_AllApps = newList;

    QVector<NvApp> newVisibleList = getVisibleApps(newList);

    // Process removals and updates first
    for (int i = 0; i < m_VisibleApps.count(); i++) {
        const NvApp& existingApp = m_VisibleApps.at(i);

        bool found = false;
        for (const NvApp& newApp : std::as_const(newVisibleList)) {
            if (existingApp.id == newApp.id) {
                // If the data changed, update it in our list
                if (existingApp != newApp) {
                    m_VisibleApps.replace(i, newApp);
                    emit dataChanged(createIndex(i, 0), createIndex(i, 0));
                }

                found = true;
                break;
            }
        }

        if (!found) {
            beginRemoveRows(QModelIndex(), i, i);
            m_VisibleApps.removeAt(i);
            endRemoveRows();
            i--;
        }
    }

    auto appOrder = [](const QString& name) -> int {
        if (name.compare("Desktop", Qt::CaseInsensitive) == 0) return 0;
        if (name.compare("Steam Big Picture", Qt::CaseInsensitive) == 0) return 1;
        return 2;
    };

    // Process additions now
    for (const NvApp& newApp : std::as_const(newVisibleList)) {
        int insertionIndex = m_VisibleApps.size();
        bool found = false;
        int ob = appOrder(newApp.name);

        for (int i = 0; i < m_VisibleApps.count(); i++) {
            const NvApp& existingApp = m_VisibleApps.at(i);

            if (existingApp.id == newApp.id) {
                found = true;
                break;
            }
            else {
                int oa = appOrder(existingApp.name);
                if (oa != ob ? ob < oa : existingApp.name.toLower() > newApp.name.toLower()) {
                    insertionIndex = i;
                    break;
                }
            }
        }

        if (!found) {
            beginInsertRows(QModelIndex(), insertionIndex, insertionIndex);
            m_VisibleApps.insert(insertionIndex, newApp);
            endInsertRows();
        }
    }

    Q_ASSERT(newVisibleList == m_VisibleApps);
}

void AppModel::setAppHidden(int appIndex, bool hidden)
{
    Q_ASSERT(appIndex < m_VisibleApps.count());
    int appId = m_VisibleApps.at(appIndex).id;

    {
        QWriteLocker lock(&m_Computer->lock);

        for (NvApp& app : m_Computer->appList) {
            if (app.id == appId) {
                app.hidden = hidden;
                break;
            }
        }
    }

    m_ComputerManager->clientSideAttributeUpdated(m_Computer);
}

void AppModel::setAppDirectLaunch(int appIndex, bool directLaunch)
{
    Q_ASSERT(appIndex < m_VisibleApps.count());
    int appId = m_VisibleApps.at(appIndex).id;

    {
        QWriteLocker lock(&m_Computer->lock);

        for (NvApp& app : m_Computer->appList) {
            if (directLaunch) {
                // We must clear direct launch from all other apps
                // to set it on the new app.
                app.directLaunch = app.id == appId;
            }
            else if (app.id == appId) {
                // If we're clearing direct launch, we're done once we
                // find our matching app ID.
                app.directLaunch = false;
                break;
            }
        }
    }

    m_ComputerManager->clientSideAttributeUpdated(m_Computer);
}

void AppModel::handleComputerStateChanged(NvComputer* computer)
{
    // Ignore updates for computers that aren't ours
    if (computer != m_Computer) {
        return;
    }

    // If the computer has gone offline or we've been unpaired,
    // signal the UI so we can go back to the PC view.
    if (m_Computer->state == NvComputer::CS_OFFLINE ||
            m_Computer->pairState == NvComputer::PS_NOT_PAIRED) {
        emit computerLost();
        return;
    }

    // First, process additions/removals from the app list. This
    // is required because the new game may now be running, so
    // we can't check that first.
    if (computer->appList != m_AllApps) {
        updateAppList(computer->appList);
    }

    // Finally, process changes to the active app
    if (computer->currentGameId != m_CurrentGameId) {
        // First, invalidate the running state of newly running game
        for (int i = 0; i < m_VisibleApps.count(); i++) {
            if (m_VisibleApps[i].id == computer->currentGameId) {
                emit dataChanged(createIndex(i, 0),
                                 createIndex(i, 0),
                                 QVector<int>() << RunningRole);
                break;
            }
        }

        // Next, invalidate the running state of the old game (if it exists)
        if (m_CurrentGameId != 0) {
            for (int i = 0; i < m_VisibleApps.count(); i++) {
                if (m_VisibleApps[i].id == m_CurrentGameId) {
                    emit dataChanged(createIndex(i, 0),
                                     createIndex(i, 0),
                                     QVector<int>() << RunningRole);
                    break;
                }
            }
        }

        // Now update our internal state
        m_CurrentGameId = m_Computer->currentGameId;
    }
}

void AppModel::handleBoxArtLoaded(NvComputer* computer, NvApp app, QUrl /* image */)
{
    Q_ASSERT(computer == m_Computer);

    int index = m_VisibleApps.indexOf(app);

    // Make sure we're not delivering a callback to an app that's already been removed
    if (index >= 0) {
        // Let our view know the box art data has changed for this app
        emit dataChanged(createIndex(index, 0),
                         createIndex(index, 0),
                         QVector<int>() << BoxArtRole);
    }
    else {
        qWarning() << "App not found for box art callback:" << app.name;
    }
}
