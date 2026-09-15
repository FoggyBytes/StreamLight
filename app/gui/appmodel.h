#pragma once

#include "backend/boxartmanager.h"
#include "backend/computermanager.h"
#include "streaming/session.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVariant>

class AppModel : public QAbstractListModel
{
    Q_OBJECT

    /**
     * Which half of the host's list the rows show (5.9.0): "games", "apps", or "" for all of
     * it. Empty by default, so every model built before the tabs existed — the unlock flow
     * looking for Desktop, the host stage — still sees the whole list.
     */
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    /// How many entries each tab would show, hidden ones filtered exactly as the rows are.
    Q_PROPERTY(int gamesCount READ gamesCount NOTIFY countsChanged)
    Q_PROPERTY(int appsCount READ appsCount NOTIFY countsChanged)
    /**
     * This client holds a Remote Monitor the server kept after its stream ended. The 2.0
     * servers then send only Resume and Disconnect Monitor, which is the whole app list — so
     * the host page opens on APPS, where those two are.
     */
    Q_PROPERTY(bool remoteMonitorActive READ remoteMonitorActive NOTIFY countsChanged)

    enum Roles
    {
        NameRole = Qt::UserRole,
        RunningRole,
        BoxArtRole,
        HiddenRole,
        AppIdRole,
        DirectLaunchRole,
        AppCollectorGameRole,
        OverriddenRole,
        PlaytimeRole,
        SectionRole,
        IsAppRole,
        ControlRole,
    };

public:
    explicit AppModel(QObject *parent = nullptr);

    QString category() const { return m_Category; }
    void setCategory(const QString& category);
    int gamesCount() const { return m_GamesCount; }
    int appsCount() const { return m_AppsCount; }
    bool remoteMonitorActive() const { return m_RemoteMonitorActive; }

    /// Index of a visible app by id, or -1. Used to relaunch the same entry after a host
    /// confirmation — by id, because the running-game copy shares its name with the game.
    Q_INVOKABLE int indexOfAppId(int appId) const;

    // Must be called before any QAbstractListModel functions
    Q_INVOKABLE void initialize(ComputerManager* computerManager, int computerIndex, bool showHiddenGames);

    Q_INVOKABLE Session* createSessionForApp(int appIndex);

    // Index of a visible app by name, or -1. Used by the remote-unlock flow to find the
    // Desktop app, which is the only thing worth launching on a host where nobody has
    // logged in yet.
    Q_INVOKABLE int indexOfAppNamed(const QString& name) const;

    Q_INVOKABLE int getDirectLaunchAppIndex();

    Q_INVOKABLE int getRunningAppId();

    Q_INVOKABLE QString getRunningAppName();

    Q_INVOKABLE QUrl getRunningAppBoxArt();

    Q_INVOKABLE void quitRunningApp();

    Q_INVOKABLE void setAppHidden(int appIndex, bool hidden);

    Q_INVOKABLE void setAppDirectLaunch(int appIndex, bool directLaunch);

    // Per-game settings overrides (see AppSettingsManager). The map keys are a
    // subset of: width, height, fps, bitrate, hdr, codec, framepacing, audio.
    // A missing key means "inherit the global setting".
    Q_INVOKABLE QVariantMap getAppOverride(int appIndex);
    Q_INVOKABLE void setAppOverride(int appIndex, const QVariantMap& ov);
    Q_INVOKABLE bool appHasOverride(int appIndex);
    Q_INVOKABLE void clearAppOverride(int appIndex);

    // What a per-game row set to "inherit" will actually run at: the global settings with
    // this host's active profile applied on top — one level down, not the full cascade,
    // because the level above is the dialog the user is looking at. Values are formatted
    // for display; see inheritedValueLabels() in settings/appsettings.h.
    Q_INVOKABLE QVariantMap inheritedLabels() const;

    // ── Play time (5.7.0) ────────────────────────────────────────────────────────────────
    /**
     * Everything the per-game panel shows about time played: the total, the last session and
     * how it went. Empty map when this app has no record — a game never streamed, or one of
     * the two entries that are never counted.
     */
    Q_INVOKABLE QVariantMap playtimeFor(int appIndex) const;

    /// Clears one game's play time. The panel offers it because a total nobody can correct
    /// is a total that is eventually wrong.
    Q_INVOKABLE void resetPlaytime(int appIndex);

    /**
     * Drops the cached labels so the rows re-read them.
     *
     * ⚠️ Needed because a session does NOT rebuild this model: the host page stays alive
     * behind the stream and gets its rows back with the same model attached, so without this
     * every row would still be showing the total from before the session that just ended.
     */
    Q_INVOKABLE void refreshPlaytime();

    /// The index of the game this host was last played on, or -1. Drives the "Continue"
    /// section — see the sort order in updateAppList().
    Q_INVOKABLE int lastPlayedIndex() const;

    QVariant data(const QModelIndex &index, int role) const override;

    int rowCount(const QModelIndex &parent) const override;

    virtual QHash<int, QByteArray> roleNames() const override;

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handleBoxArtLoaded(NvComputer* computer, NvApp app, QUrl image);

signals:
    void computerLost();
    void categoryChanged();
    void countsChanged();

private:
    void updateAppList(QVector<NvApp> newList);

    /// Does this app belong in the current tab? Always true with no category set.
    bool matchesCategory(const NvApp& app) const;

    /// The last-played name the sort and the Continue section use — empty on the APPS tab,
    /// where the running-game copy carries the game's own title and must not be promoted.
    QString lastPlayedForSort() const;

    /// Recounts both tabs and the retained-monitor flag; emits countsChanged when any moved.
    void updateCounts();

    /// Puts m_VisibleApps into the order appSortOrder() describes. Returns true when the
    /// order actually moved (and the model was reset), false when it was already right.
    bool sortVisibleApps();

    QVector<NvApp> getVisibleApps(const QVector<NvApp>& appList);

    bool isAppCurrentlyVisible(const NvApp& app);

    // Both were uninitialised until 04/08/2026 and read as garbage before initialize() ran.
    // Harmless while nothing looked at them first — and then the re-initialise guard in
    // initialize() did exactly that, and crashed on whatever the pointer happened to be.
    NvComputer* m_Computer = nullptr;
    BoxArtManager m_BoxArtManager;
    ComputerManager* m_ComputerManager = nullptr;
    QVector<NvApp> m_VisibleApps, m_AllApps;
    int m_CurrentGameId;
    bool m_ShowHiddenGames;

    QString m_Category;
    int m_GamesCount = 0;
    int m_AppsCount = 0;
    bool m_RemoteMonitorActive = false;

    // Formatted play time by app id, filled on first read of each row.
    //
    // ⚠️ A cache rather than a lookup per data() call, because a ListView asks for a role
    // many times per repaint and each miss would be a QSettings read. Cleared by
    // refreshPlaytime() and by resetPlaytime(), which are the only two ways the underlying
    // value can move while this model is alive.
    mutable QHash<int, QString> m_PlaytimeLabels;
};
