#include "appmodel.h"

#include "settings/appsettings.h"
#include "settings/playtime.h"
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

QVariantMap AppModel::inheritedLabels() const
{
    if (m_Computer == nullptr) {
        return QVariantMap();
    }

    // Same two steps buildPrefs() takes, minus the third: global, then the host's active
    // profile. Reusing buildPrefs() itself would fold the game's own overrides in and the
    // dialog would end up quoting the user their own answer back as what they inherit.
    QScopedPointer<StreamingPreferences> p(StreamingPreferences::get()->clone());
    applyAppOverride(p.data(), HostProfileManager::get()->activeOverride(m_Computer->uuid));
    return inheritedValueLabels(p.data());
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
    case PlaytimeRole: {
        // Empty string, not "0 m", when there is nothing to say: the row's subtitle appends
        // this after the store name, and a game never streamed should read "Steam", not
        // "Steam · 0 m". Desktop and Steam Big Picture never get one at all.
        auto it = m_PlaytimeLabels.constFind(app.id);
        if (it != m_PlaytimeLabels.constEnd())
            return *it;

        QString label;
        if (PlaytimeManager::isTracked(app.name)) {
            PlaytimeRecord rec = PlaytimeManager::get()->recordFor(m_Computer->uuid, app.name);
            if (rec.valid && rec.totalSeconds > 0)
                label = PlaytimeManager::formatDuration(rec.totalSeconds);
        }
        m_PlaytimeLabels.insert(app.id, label);
        return label;
    }
    case SectionRole: {
        // What the ListView groups on. Two values only, and the sort order guarantees the
        // "continue" one is a single row at the top — so the section header is the caption
        // above it rather than a heading that could repeat further down.
        //
        // Only row 0 can be it, which is what keeps this cheap: every other row answers
        // without reading anything, where asking lastPlayedIndex() per row would be a
        // settings read and a list scan on every repaint.
        if (index.row() != 0 || m_Computer == nullptr)
            return QStringLiteral("all");

        const QString lastPlayed = PlaytimeManager::get()->lastPlayedOn(m_Computer->uuid).name;
        return (!lastPlayed.isEmpty() && app.name.compare(lastPlayed, Qt::CaseInsensitive) == 0)
               ? QStringLiteral("continue") : QStringLiteral("all");
    }
    default:
        return QVariant();
    }
}

QVariantMap AppModel::playtimeFor(int appIndex) const
{
    QVariantMap out;
    if (appIndex < 0 || appIndex >= m_VisibleApps.count())
        return out;

    const NvApp& app = m_VisibleApps.at(appIndex);
    if (!PlaytimeManager::isTracked(app.name))
        return out;

    PlaytimeRecord rec = PlaytimeManager::get()->recordFor(m_Computer->uuid, app.name);
    if (!rec.valid)
        return out;

    out[QStringLiteral("totalSeconds")]  = (qint64)rec.totalSeconds;
    out[QStringLiteral("total")]         = PlaytimeManager::formatDuration(rec.totalSeconds);
    out[QStringLiteral("sessions")]      = rec.sessionCount;
    out[QStringLiteral("lastSeconds")]   = (qint64)rec.lastSessionSeconds;
    out[QStringLiteral("lastSession")]   = rec.lastSessionSeconds > 0
                                           ? PlaytimeManager::formatDuration(rec.lastSessionSeconds)
                                           : QString();
    // Handed over as a local-time ISO string: QML formats it, because the date format is a
    // user setting and this file has no business knowing which one is picked.
    out[QStringLiteral("lastPlayed")]    = rec.lastPlayed.isValid()
                                           ? rec.lastPlayed.toLocalTime().toString(Qt::ISODate)
                                           : QString();

    // Metrics stay raw, -1 included: QML draws a dash for those, and rounding a "never
    // measured" into a number here would throw the distinction away at the only point that
    // still has it.
    out[QStringLiteral("fpsAvg")]        = rec.lastFpsAvg;
    out[QStringLiteral("targetFps")]     = rec.lastTargetFps;
    out[QStringLiteral("dropsPct")]      = rec.lastDropsPct;
    out[QStringLiteral("jitterDropsPct")]= rec.lastJitterDropsPct;
    out[QStringLiteral("rttMs")]         = rec.lastRttMs;
    out[QStringLiteral("hostLatencyMs")] = rec.lastHostLatencyMs;
    out[QStringLiteral("decodeMs")]      = rec.lastDecodeMs;
    out[QStringLiteral("bitrateMbps")]   = rec.lastBitrateMbps;

    return out;
}

void AppModel::resetPlaytime(int appIndex)
{
    if (appIndex < 0 || appIndex >= m_VisibleApps.count())
        return;

    const NvApp& app = m_VisibleApps.at(appIndex);
    PlaytimeManager::get()->reset(m_Computer->uuid, app.name);

    /*
     * ⚠️ Clears the labels but does NOT re-sort, and that is deliberate.
     *
     * Clearing a game can also clear the host's "last played" pointer, which changes the
     * order — and the only caller is the per-game panel, which is open on THIS appIndex while
     * this runs. Re-sorting under it would leave the dialog holding an index that now points
     * at a different game, and every setting it wrote afterwards would land on the wrong one.
     *
     * The order is put right when the panel closes, which is the first moment the index stops
     * mattering: AppsScreen calls refreshPlaytime() from onClosedByUser.
     */
    m_PlaytimeLabels.clear();
    if (!m_VisibleApps.isEmpty()) {
        emit dataChanged(createIndex(0, 0), createIndex(m_VisibleApps.count() - 1, 0),
                         { PlaytimeRole, SectionRole });
    }
}

void AppModel::refreshPlaytime()
{
    m_PlaytimeLabels.clear();
    if (m_VisibleApps.isEmpty() || m_Computer == nullptr)
        return;

    /*
     * ⚠️ A full reset, not a dataChanged() over the play-time role, because the ORDER can
     * have moved: the game that just ended is now the one at the top, under Continue.
     *
     * This is the moment that needs it. A session does not rebuild the host page — it stays
     * alive behind the stream and comes back with the same model — so nothing else would ever
     * re-sort, and the row would go on sitting wherever it was alphabetically while the
     * Continue section named it.
     */
    if (sortVisibleApps())
        return;   // the reset already redrew every row, labels included

    emit dataChanged(createIndex(0, 0), createIndex(m_VisibleApps.count() - 1, 0),
                     { PlaytimeRole, SectionRole });
}

int AppModel::lastPlayedIndex() const
{
    if (m_Computer == nullptr)
        return -1;

    PlaytimeRecord rec = PlaytimeManager::get()->lastPlayedOn(m_Computer->uuid);
    if (!rec.valid)
        return -1;

    // Matched by name, not by the id stored with the record: the id is a hint that goes
    // stale when the host rebuilds apps.json, and the name is the key everything else here
    // is built on. A game since removed from the host simply does not match, which is
    // exactly the "is it still launchable" gate — no separate check needed.
    for (int i = 0; i < m_VisibleApps.count(); i++) {
        if (m_VisibleApps.at(i).name.compare(rec.name, Qt::CaseInsensitive) == 0)
            return i;
    }

    return -1;
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
    names[PlaytimeRole] = "playtime";
    names[SectionRole] = "section";

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

    // Read once for the whole pass, exactly as sortAppList() does — same value, same reason.
    const QString lastPlayed = m_Computer != nullptr
                               ? PlaytimeManager::get()->lastPlayedOn(m_Computer->uuid).name
                               : QString();

    // Process additions now
    for (const NvApp& newApp : std::as_const(newVisibleList)) {
        int insertionIndex = m_VisibleApps.size();
        bool found = false;
        // ⚠️ Shared with NvComputer::sortAppList() — see nvapp.h. The two must produce the
        // same order or the assert at the end of this function fires in a debug build.
        int ob = appSortOrder(newApp.name, lastPlayed);

        for (int i = 0; i < m_VisibleApps.count(); i++) {
            const NvApp& existingApp = m_VisibleApps.at(i);

            if (existingApp.id == newApp.id) {
                found = true;
                break;
            }
            else {
                int oa = appSortOrder(existingApp.name, lastPlayed);
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

    /*
     * ⚠️ This replaces a `Q_ASSERT(newVisibleList == m_VisibleApps)`, and the change is
     * deliberate rather than a way of silencing it.
     *
     * The assert held because the incoming list was already sorted the same way this loop
     * inserts. That stopped being guaranteed the moment the order started depending on which
     * game was played last: NvComputer::sortAppList() only runs when the app list itself
     * changes, so after a session ends the model has re-sorted (refreshPlaytime) while the
     * host's copy still carries the old order — and the next poll would hand it back that way.
     *
     * Reconciling is strictly better than asserting: it fixes the order instead of complaining
     * about it in debug builds and doing nothing in release ones. And it costs nothing in the
     * ordinary case, where the list is already in order and sortVisibleApps() returns false
     * without touching the model.
     */
    sortVisibleApps();
}

bool AppModel::sortVisibleApps()
{
    if (m_VisibleApps.isEmpty() || m_Computer == nullptr)
        return false;

    const QString lastPlayed = PlaytimeManager::get()->lastPlayedOn(m_Computer->uuid).name;

    QVector<NvApp> sorted = m_VisibleApps;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&lastPlayed](const NvApp& a, const NvApp& b) {
        int oa = appSortOrder(a.name, lastPlayed), ob = appSortOrder(b.name, lastPlayed);
        if (oa != ob) return oa < ob;
        return a.name.toLower() < b.name.toLower();
    });

    if (sorted == m_VisibleApps)
        return false;

    // A reset rather than moveRows: the only time this fires is a session ending, which is
    // also the moment the page is being rebuilt around the user anyway.
    beginResetModel();
    m_VisibleApps = sorted;
    endResetModel();
    return true;
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
