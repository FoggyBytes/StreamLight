#pragma once

#include "backend/boxartmanager.h"
#include "backend/computermanager.h"
#include "streaming/session.h"

#include <QAbstractListModel>
#include <QVariant>

class AppModel : public QAbstractListModel
{
    Q_OBJECT

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
    };

public:
    explicit AppModel(QObject *parent = nullptr);

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

    QVariant data(const QModelIndex &index, int role) const override;

    int rowCount(const QModelIndex &parent) const override;

    virtual QHash<int, QByteArray> roleNames() const override;

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handleBoxArtLoaded(NvComputer* computer, NvApp app, QUrl image);

signals:
    void computerLost();

private:
    void updateAppList(QVector<NvApp> newList);

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
};
