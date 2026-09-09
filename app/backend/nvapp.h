#pragma once

#include <QSettings>
#include <QString>

/**
 * The two entries in a host's app list that are not games: the host's own desktop, and
 * Steam's living-room shell.
 *
 * ⚠️ These two names were spelled out in two separate sort orders — NvComputer::sortAppList()
 * and the insertion order in AppModel::updateAppList() — which have to agree or the assert at
 * the end of the latter fires. Play time added a third caller (hours are never counted for
 * either) and "last played" a fourth, so the strings live here now, next to the type they
 * describe, and each caller asks instead of repeating them.
 */
inline bool isSystemApp(const QString& name)
{
    return name.compare(QStringLiteral("Desktop"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("Steam Big Picture"), Qt::CaseInsensitive) == 0;
}

/**
 * Where an app sits in the list: the game you last played first, then the desktop, then
 * Steam's shell, then everything else alphabetically.
 *
 * ⚠️ Shared so the two sort sites cannot drift — NvComputer::sortAppList() orders the list
 * and AppModel::updateAppList() inserts against that order, then asserts the two agree.
 *
 * `lastPlayedName` is empty on every host where nothing has been streamed yet, and then this
 * is exactly the order it always was.
 */
inline int appSortOrder(const QString& name, const QString& lastPlayedName = QString())
{
    if (!lastPlayedName.isEmpty()
        && name.compare(lastPlayedName, Qt::CaseInsensitive) == 0) return 0;
    if (name.compare(QStringLiteral("Desktop"), Qt::CaseInsensitive) == 0) return 1;
    if (name.compare(QStringLiteral("Steam Big Picture"), Qt::CaseInsensitive) == 0) return 2;
    return 3;
}

class NvApp
{
public:
    NvApp() {}
    explicit NvApp(QSettings& settings);

    bool operator==(const NvApp& other) const
    {
        return id == other.id &&
                name == other.name &&
                hdrSupported == other.hdrSupported &&
                isAppCollectorGame == other.isAppCollectorGame &&
                hidden == other.hidden &&
                directLaunch == other.directLaunch;
    }

    bool operator!=(const NvApp& other) const
    {
        return !operator==(other);
    }

    bool isInitialized()
    {
        return id != 0 && !name.isEmpty();
    }

    void
    serialize(QSettings& settings) const;

    int id = 0;
    QString name;
    bool hdrSupported = false;
    bool isAppCollectorGame = false;
    bool hidden = false;
    bool directLaunch = false;
};

Q_DECLARE_METATYPE(NvApp)
