#include "settings/playtime.h"
#include "backend/nvapp.h"

#include <QSettings>
#include <QStringList>

#define PLAYTIME_PREFIX "playtime/"

// Values inside a game's group.
#define SER_NAME        "name"
#define SER_APPID       "appid"
#define SER_TOTAL       "totalSeconds"
#define SER_COUNT       "sessionCount"
#define SER_LASTPLAYED  "lastPlayed"
#define SER_LASTSECONDS "lastSessionSeconds"
#define SER_FPS         "lastFpsAvg"
#define SER_TARGETFPS   "lastTargetFps"
#define SER_DROPS       "lastDropsPct"
#define SER_JITTERDROPS "lastJitterDropsPct"
#define SER_RTT         "lastRttMs"
#define SER_HOSTLAT     "lastHostLatencyMs"
#define SER_DECODE      "lastDecodeMs"
#define SER_BITRATE     "lastBitrateMbps"

// Which game this host was last played on, stored beside the game groups rather than found
// by walking them: with the pointer written once per session, "what do I resume here" is one
// read instead of loading every record and sorting it by date on every repaint of the card.
#define SER_LASTGAME    "lastGame"

PlaytimeManager* PlaytimeManager::get()
{
    static PlaytimeManager instance;
    return &instance;
}

bool PlaytimeManager::isTracked(const QString& appName)
{
    return !appName.isEmpty() && !isSystemApp(appName);
}

QString PlaytimeManager::normalise(const QString& appName)
{
    // simplified() rather than trimmed(): it also collapses runs of inner whitespace, so
    // "Hollow  Knight" and "Hollow Knight" are one game. Case folding on top of that, since
    // the streaming server's entry and the library's copy of a title do not always agree on
    // capitalisation.
    return appName.simplified().toLower();
}

QString PlaytimeManager::digest(const QString& normalisedName)
{
    // FNV-1a, 64-bit, over the UTF-8 of the normalised name.
    //
    // A hash rather than the name itself because the name cannot be a settings key at all on
    // Windows — see the header. 64 bits is far more than a library of a few hundred titles
    // needs, and the readable name is stored inside the group anyway, so a collision would
    // be visible rather than silent.
    const QByteArray utf8 = normalisedName.toUtf8();
    quint64 hash = 14695981039346656037ULL;
    for (char c : utf8) {
        hash ^= static_cast<quint8>(c);
        hash *= 1099511628211ULL;
    }
    return QString::number(hash, 16).rightJustified(16, QLatin1Char('0'));
}

QString PlaytimeManager::hostGroup(const QString& hostUuid)
{
    return QStringLiteral(PLAYTIME_PREFIX) + hostUuid;
}

QString PlaytimeManager::gameGroup(const QString& hostUuid, const QString& appName)
{
    // The 'g' prefix keeps a game group from ever colliding with a plain value written at
    // the host level — SER_LASTGAME lives in the same node.
    return hostGroup(hostUuid) + QStringLiteral("/g") + digest(normalise(appName));
}

QString PlaytimeManager::formatDuration(qint64 seconds)
{
    if (seconds < 0) seconds = 0;

    const qint64 hours   = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;

    // Under an hour there is no point printing "0 h": the minutes are the whole answer.
    if (hours == 0)
        return QStringLiteral("%1 m").arg(minutes);

    // Two digits on the minutes past the first hour so the numbers line up in a column of
    // rows — the host page prints one of these per game.
    return QStringLiteral("%1 h %2 m")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'));
}

void PlaytimeManager::addSeconds(const QString& hostUuid, const QString& appName,
                                 int appId, qint64 seconds)
{
    if (seconds <= 0 || hostUuid.isEmpty() || !isTracked(appName))
        return;

    QSettings settings;
    settings.beginGroup(gameGroup(hostUuid, appName));

    const qint64 total = settings.value(QStringLiteral(SER_TOTAL), 0).toLongLong() + seconds;
    settings.setValue(QStringLiteral(SER_TOTAL), total);

    // Rewritten every time rather than only on creation: the display name follows the
    // library when a title is corrected (the normalised key is what has to stay put, and a
    // rename that changes the key starts a new record by design), and the id follows the
    // host when apps.json is rebuilt.
    settings.setValue(QStringLiteral(SER_NAME), appName);
    settings.setValue(QStringLiteral(SER_APPID), appId);

    settings.endGroup();
}

void PlaytimeManager::endSession(const QString& hostUuid, const QString& appName, int appId,
                                 qint64 unbankedSeconds, qint64 sessionSeconds,
                                 const PlaytimeSessionStats& stats)
{
    if (hostUuid.isEmpty() || !isTracked(appName))
        return;

    // Only what the flushes have not already taken — see the header.
    addSeconds(hostUuid, appName, appId, unbankedSeconds);

    if (sessionSeconds < kMinSessionSeconds)
        return;

    QSettings settings;
    settings.beginGroup(gameGroup(hostUuid, appName));

    // Counted past the same threshold that decides "last played", and for the same reason:
    // a launch corrected ten seconds later was not a session you had.
    settings.setValue(QStringLiteral(SER_COUNT),
                      settings.value(QStringLiteral(SER_COUNT), 0).toInt() + 1);

    settings.setValue(QStringLiteral(SER_LASTPLAYED),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    settings.setValue(QStringLiteral(SER_LASTSECONDS), sessionSeconds);

    settings.setValue(QStringLiteral(SER_FPS),         stats.fpsAvg);
    settings.setValue(QStringLiteral(SER_TARGETFPS),   stats.targetFps);
    settings.setValue(QStringLiteral(SER_DROPS),       stats.dropsPct);
    settings.setValue(QStringLiteral(SER_JITTERDROPS), stats.jitterDropsPct);
    settings.setValue(QStringLiteral(SER_RTT),         stats.rttMs);
    settings.setValue(QStringLiteral(SER_HOSTLAT),     stats.hostLatencyMs);
    settings.setValue(QStringLiteral(SER_DECODE),      stats.decodeMs);
    settings.setValue(QStringLiteral(SER_BITRATE),     stats.bitrateMbps);

    settings.endGroup();

    settings.beginGroup(hostGroup(hostUuid));
    settings.setValue(QStringLiteral(SER_LASTGAME), appName);
    settings.endGroup();
}

PlaytimeRecord PlaytimeManager::recordFor(const QString& hostUuid, const QString& appName) const
{
    PlaytimeRecord r;
    if (hostUuid.isEmpty() || appName.isEmpty())
        return r;

    QSettings settings;
    settings.beginGroup(gameGroup(hostUuid, appName));

    // No total means no record: every write path sets it, so its absence is the one
    // reliable "nothing here" test. childKeys() would also answer, at the cost of building
    // a string list for a question a single value already settles.
    if (!settings.contains(QStringLiteral(SER_TOTAL))) {
        settings.endGroup();
        return r;
    }

    r.valid              = true;
    r.name               = settings.value(QStringLiteral(SER_NAME), appName).toString();
    r.appId              = settings.value(QStringLiteral(SER_APPID), 0).toInt();
    r.totalSeconds       = settings.value(QStringLiteral(SER_TOTAL), 0).toLongLong();
    r.sessionCount       = settings.value(QStringLiteral(SER_COUNT), 0).toInt();
    r.lastSessionSeconds = settings.value(QStringLiteral(SER_LASTSECONDS), 0).toLongLong();

    const QString stamp = settings.value(QStringLiteral(SER_LASTPLAYED)).toString();
    if (!stamp.isEmpty()) {
        r.lastPlayed = QDateTime::fromString(stamp, Qt::ISODate);
        r.lastPlayed.setTimeSpec(Qt::UTC);
    }

    r.lastFpsAvg          = settings.value(QStringLiteral(SER_FPS),         -1.0f).toFloat();
    r.lastTargetFps       = settings.value(QStringLiteral(SER_TARGETFPS),   0).toInt();
    r.lastDropsPct        = settings.value(QStringLiteral(SER_DROPS),       -1.0f).toFloat();
    r.lastJitterDropsPct  = settings.value(QStringLiteral(SER_JITTERDROPS), -1.0f).toFloat();
    r.lastRttMs           = settings.value(QStringLiteral(SER_RTT),         -1.0f).toFloat();
    r.lastHostLatencyMs   = settings.value(QStringLiteral(SER_HOSTLAT),     -1.0f).toFloat();
    r.lastDecodeMs        = settings.value(QStringLiteral(SER_DECODE),      -1.0f).toFloat();
    r.lastBitrateMbps     = settings.value(QStringLiteral(SER_BITRATE),     -1.0f).toFloat();

    settings.endGroup();
    return r;
}

PlaytimeRecord PlaytimeManager::lastPlayedOn(const QString& hostUuid) const
{
    if (hostUuid.isEmpty())
        return PlaytimeRecord();

    QSettings settings;
    settings.beginGroup(hostGroup(hostUuid));
    const QString name = settings.value(QStringLiteral(SER_LASTGAME)).toString();
    settings.endGroup();

    if (name.isEmpty())
        return PlaytimeRecord();

    // The pointer can outlive the record it names — reset() clears a game without knowing
    // whether it was the last one played. recordFor() returning invalid is the answer in
    // that case, and the caller draws nothing.
    return recordFor(hostUuid, name);
}

void PlaytimeManager::reset(const QString& hostUuid, const QString& appName)
{
    if (hostUuid.isEmpty() || appName.isEmpty())
        return;

    QSettings settings;
    settings.remove(gameGroup(hostUuid, appName));

    // Take the "last played here" pointer with it when it named this game, or the card
    // would go on offering a game with no record behind it.
    settings.beginGroup(hostGroup(hostUuid));
    if (normalise(settings.value(QStringLiteral(SER_LASTGAME)).toString()) == normalise(appName))
        settings.remove(QStringLiteral(SER_LASTGAME));
    settings.endGroup();
}

void PlaytimeManager::forgetHost(const QString& hostUuid)
{
    if (hostUuid.isEmpty())
        return;

    QSettings settings;
    settings.remove(hostGroup(hostUuid));
}
