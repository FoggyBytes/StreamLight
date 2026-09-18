#pragma once

#include <QSettings>
#include <QSet>
#include <QString>

/**
 * The entries in a host's app list that are not games, by name: the host's own desktop,
 * Steam's living-room shell, and the entries Vibeshine / Vibepollo 2.0 add for remote
 * sessions (5.9.0 — see hostControlKind() below for the reliable way to spot those).
 *
 * ⚠️ These names were spelled out in two separate sort orders — NvComputer::sortAppList()
 * and the insertion order in AppModel::updateAppList() — which have to agree or the assert at
 * the end of the latter fires. Play time added a third caller (hours are never counted for
 * any of them) and "last played" a fourth, so the strings live here now, next to the type they
 * describe, and each caller asks instead of repeating them.
 *
 * Trimmed before comparing: the 2.0 servers pad some control titles with leading spaces on
 * purpose, so that they sort first in a client that alphabetises the list.
 */
/**
 * The host's desktop, under either of the names a server gives it (6.1.0): "Desktop", from
 * apps.json, and "Desktop (fallback)", the entry Apollo and Vibepollo add on their own when
 * apps.json cannot be read (src/process.cpp, FALLBACK_DESKTOP_UUID
 * EAAC6159-089A-46A9-9E24-6436885F6610). Without the second the fallback sorted as a game.
 *
 * ⚠️ By name and not by that UUID: the callers — the sort, play time, the unlock flow — are
 * handed names only, and the server writes this one as a fixed string, same as the UUID.
 */
inline bool isDesktopName(const QString& name)
{
    const QString n = name.trimmed();
    return n.compare(QLatin1String("Desktop"), Qt::CaseInsensitive) == 0
        || n.compare(QLatin1String("Desktop (fallback)"), Qt::CaseInsensitive) == 0;
}

inline bool isSystemApp(const QString& name)
{
    if (isDesktopName(name))
        return true;
    const QString n = name.trimmed();
    for (const char* known : { "Steam Big Picture", "Virtual Display",
                               "Remote Input", "Remote Monitor", "Terminate", "Resume",
                               "Disconnect Monitor", "Disconnect Input" }) {
        if (n.compare(QLatin1String(known), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

/**
 * The host controls Vibeshine and Vibepollo 2.0 put in the app list (5.9.0).
 *
 * They are not applications: the server synthesises them per caller and per session state
 * (src/remote_session.cpp at tag 2.0.0-beta.3 of either repo). Remote Input streams input only
 * over a black picture; Remote Monitor gives this client a virtual display of its own that
 * survives the stream; Terminate and the two Disconnects are actions that finish with HTTP 410
 * and a message instead of a stream; Resume and RunningGame rejoin a session already running.
 *
 * ⚠️ Recognised by the fixed UUID or id first and by name only as a fallback. The name is
 * the least reliable of the three — it is padded with spaces for sorting, and the running-game
 * copy carries the game's own title — while the ids and UUIDs are the server's stable identity.
 * The Vibepollo UUIDs are Apollo's older entries, which that server still ships beside them.
 */
enum class HostControl
{
    None,
    Resume,
    DisconnectMonitor,
    DisconnectInput,
    Terminate,
    RemoteMonitor,
    RemoteInput,
    RunningGame,
    VirtualDisplay,
};

inline HostControl hostControlKind(int id, const QString& uuid, const QString& name)
{
    // remote_session::synthetic_uuid(): the control's enum value as the last digit, 1..7.
    static const QLatin1String synthPrefix("9a1c5a25-58fe-40e0-b9aa-7d3f0000000");
    if (uuid.size() == synthPrefix.size() + 1 && uuid.startsWith(synthPrefix, Qt::CaseInsensitive)) {
        switch (uuid.at(synthPrefix.size()).unicode()) {
        case '1': return HostControl::Resume;
        case '2': return HostControl::DisconnectMonitor;
        case '3': return HostControl::DisconnectInput;
        case '4': return HostControl::Terminate;
        case '5': return HostControl::RemoteMonitor;
        case '6': return HostControl::RemoteInput;
        case '7': return HostControl::RunningGame;
        default: break;
        }
    }
    if (uuid.compare(QLatin1String("8CB5C136-DA67-4F99-B4A1-F9CD35005CF4"), Qt::CaseInsensitive) == 0)
        return HostControl::RemoteInput;
    if (uuid.compare(QLatin1String("E16CBE1B-295D-4632-9A76-EC4180C857D3"), Qt::CaseInsensitive) == 0)
        return HostControl::Terminate;
    if (uuid.compare(QLatin1String("8902CB19-674A-403D-A587-41B092E900BA"), Qt::CaseInsensitive) == 0)
        return HostControl::VirtualDisplay;

    // remote_session.h: primary ids 2147483501-507, legacy 2147483601-606, and the
    // "secondary client" copies 511/514/515/516 that differ only in their sort padding.
    switch (id) {
    case 2147483501: case 2147483601: case 2147483511: return HostControl::Resume;
    case 2147483502: case 2147483602:                  return HostControl::DisconnectMonitor;
    case 2147483503: case 2147483603:                  return HostControl::DisconnectInput;
    case 2147483504: case 2147483604: case 2147483514: return HostControl::Terminate;
    case 2147483505: case 2147483605: case 2147483515: return HostControl::RemoteMonitor;
    case 2147483506: case 2147483606: case 2147483516: return HostControl::RemoteInput;
    case 2147483507:                                   return HostControl::RunningGame;
    default: break;
    }

    // Older servers that send no UUID. "Resume" is deliberately not matched by name alone.
    const QString n = name.trimmed();
    if (n.compare(QLatin1String("Remote Input"), Qt::CaseInsensitive) == 0)       return HostControl::RemoteInput;
    if (n.compare(QLatin1String("Remote Monitor"), Qt::CaseInsensitive) == 0)     return HostControl::RemoteMonitor;
    if (n.compare(QLatin1String("Terminate"), Qt::CaseInsensitive) == 0)          return HostControl::Terminate;
    if (n.compare(QLatin1String("Virtual Display"), Qt::CaseInsensitive) == 0)    return HostControl::VirtualDisplay;
    if (n.compare(QLatin1String("Disconnect Monitor"), Qt::CaseInsensitive) == 0) return HostControl::DisconnectMonitor;
    if (n.compare(QLatin1String("Disconnect Input"), Qt::CaseInsensitive) == 0)   return HostControl::DisconnectInput;
    return HostControl::None;
}

/// The string QML sees for a control — empty for an ordinary app.
inline QString hostControlName(HostControl c)
{
    switch (c) {
    case HostControl::Resume:            return QStringLiteral("resume");
    case HostControl::DisconnectMonitor: return QStringLiteral("disconnectMonitor");
    case HostControl::DisconnectInput:   return QStringLiteral("disconnectInput");
    case HostControl::Terminate:         return QStringLiteral("terminate");
    case HostControl::RemoteMonitor:     return QStringLiteral("remoteMonitor");
    case HostControl::RemoteInput:       return QStringLiteral("remoteInput");
    case HostControl::RunningGame:       return QStringLiteral("runningGame");
    case HostControl::VirtualDisplay:    return QStringLiteral("virtualDisplay");
    default:                             return QString();
    }
}

/**
 * How a game's name is compared wherever the NAME is the key — play time and pins: runs of
 * whitespace collapsed and case folded, so "Hollow  Knight" and "hollow knight" are one game.
 *
 * ⚠️ One spelling. PlaytimeManager::normalise() delegates here, and appSortOrder() below uses
 * it for pins, so the sort and the store cannot disagree about whether two names match.
 */
inline QString normaliseGameName(const QString& name)
{
    return name.simplified().toLower();
}

/**
 * Where an app sits in the list: the game you last played first, then the games you pinned
 * (6.0.0), then the desktop, the virtual display (6.1.0), Steam's shell, then everything
 * else alphabetically. The three launchers are compared trimmed, like isSystemApp(): the 2.0
 * servers pad some titles with leading spaces.
 *
 * ⚠️ Shared so the two sort sites cannot drift — NvComputer::sortAppList() orders the list
 * and AppModel::updateAppList() inserts against that order, then asserts the two agree.
 *
 * `lastPlayedName` is empty on every host where nothing has been streamed yet, and `pinned`
 * (normalised names, see normaliseGameName) is empty until something is pinned — with both
 * empty this is exactly the order it always was. A pinned game that is also the last played
 * one sorts as last played: it is shown once, at the top.
 */
inline int appSortOrder(const QString& name, const QString& lastPlayedName = QString(),
                        const QSet<QString>& pinned = QSet<QString>())
{
    if (!lastPlayedName.isEmpty()
        && name.compare(lastPlayedName, Qt::CaseInsensitive) == 0) return 0;
    if (!pinned.isEmpty() && pinned.contains(normaliseGameName(name))) return 1;
    const QString n = name.trimmed();
    if (isDesktopName(n)) return 2;
    if (n.compare(QStringLiteral("Virtual Display"), Qt::CaseInsensitive) == 0) return 3;
    if (n.compare(QStringLiteral("Steam Big Picture"), Qt::CaseInsensitive) == 0) return 4;
    return 5;
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
                uuid == other.uuid &&
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
        // We use isNull() instead of isEmpty() here because we want
        // to detect cases where the name is unassigned, not empty.
        return id != 0 && !name.isNull();
    }

    void
    serialize(QSettings& settings) const;

    int id = 0;
    QString name;
    // The server's own identity for the entry (applist <UUID>), empty on hosts that do not
    // send one. Needed to tell the 2.0 host controls apart — see hostControlKind().
    QString uuid;
    bool hdrSupported = false;
    bool isAppCollectorGame = false;
    bool hidden = false;
    bool directLaunch = false;
};

Q_DECLARE_METATYPE(NvApp)

/**
 * GAMES or APPS on the host page (5.9.0): anything that is not a game by name, and every
 * host control. Everything else counts as a game — which is why a host with no configured
 * titles lands on APPS by itself.
 */
inline bool isAppsCategory(const NvApp& app)
{
    return isSystemApp(app.name) || hostControlKind(app.id, app.uuid, app.name) != HostControl::None;
}

/**
 * An entry that is not an application but a control the server synthesises — the Vibeshine /
 * Vibepollo 2.0 ones (Remote Input, Remote Monitor, Resume, Terminate, the Disconnects) and
 * the running-game copy. Virtual Display is NOT one: it launches a stream like Desktop does.
 *
 * These stay on APPS whatever happens (6.1.0): they are never on ALL — the running-game copy
 * would show the game twice — and the user cannot move them to GAMES.
 */
inline bool isHostControlEntry(const NvApp& app)
{
    const HostControl c = hostControlKind(app.id, app.uuid, app.name);
    if (c == HostControl::VirtualDisplay)
        return false;
    if (c != HostControl::None)
        return true;

    // A system name that is none of the three launchers: "Resume" and the like on a server
    // that sends neither a UUID nor a known id.
    if (!isSystemApp(app.name))
        return false;
    const QString n = app.name.trimmed();
    return !isDesktopName(n)
        && n.compare(QLatin1String("Steam Big Picture"), Qt::CaseInsensitive) != 0
        && n.compare(QLatin1String("Virtual Display"), Qt::CaseInsensitive) != 0;
}

/**
 * The ALL tab (6.1.0, issue #22): everything but the host controls — every game, Desktop,
 * Virtual Display, Steam Big Picture, and whatever the user moved to APPS by hand — so a user
 * who starts from the desktop or a virtual display does not have to change tab for it.
 */
inline bool isAllCategory(const NvApp& app)
{
    return !isHostControlEntry(app);
}
