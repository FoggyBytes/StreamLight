#pragma once

// Per-game play time, kept by the client (StreamLight 5.7.0).
//
// How long you have streamed each game, and how the last session on it went. Everything
// here is measured on this side and stored on this machine: no bridge verb, no StreamTweak,
// nothing fetched. It is what the host card's "Last played" block and the host page's
// "Continue" row are drawn from, and what the per-game panel reports and can reset.
//
// ── The key ──────────────────────────────────────────────────────────────────────────────
// Records are keyed by host uuid + the *name* of the game, not by app id.
//
// ⚠️ The name is what survives a reinstall, which is the whole requirement. App ids do not:
// for the games StreamTweak manages, the id is derived from the lowercased name
// (SunshineSync.StableAppId), so it moves when the name is corrected; for entries added by
// hand in the streaming server's own UI, the server assigns it however it likes, and Apollo
// or Vibepollo may not agree with Sunshine.
//
// ⚠️ The uuid, and NOT NvComputer::storageKey(). That is how per-game overrides, host
// profiles and the box art cache are already keyed — so a Tailscale clone and the LAN tile
// it was cloned from share one set of hours, which is right: it is the same PC reached by
// another road. Keying on the storage key would split them in two and restart the count the
// first time you streamed over Tailscale.
//
// ⚠️ The name cannot BE the settings key. On Windows these live in the registry, where
// QSettings treats '/' as a group separator and rejects '\' outright — so a title like
// "Marvel's Spider-Man: Miles Morales" is not a legal key name. The group is a hex digest of
// the normalised name, and the readable name is stored as a value inside it.

#include <QDateTime>
#include <QString>

/**
 * What one game on one host looks like on disk.
 *
 * -1 in a metric means "never measured", which is not zero: a card that printed 0 % drops
 * for a session that never reported any would be inventing a result. Same convention as the
 * bridge's own STATS and LASTSESSION payloads.
 */
struct PlaytimeRecord
{
    bool      valid = false;

    QString   name;                     // as launched, for display
    int       appId = 0;                // last known id on this host — a hint, never the key

    qint64    totalSeconds = 0;         // every session ever, on this host
    int       sessionCount = 0;         // how many of them there were
    QDateTime lastPlayed;               // UTC; invalid until a session passes the threshold
    qint64    lastSessionSeconds = 0;

    // The last session's numbers. The card shows the first two; the rest is for the
    // per-game panel, which is the only place with room for them.
    float     lastFpsAvg      = -1.0f;
    int       lastTargetFps   = 0;
    float     lastDropsPct    = -1.0f;  // network drops, the common cause
    float     lastJitterDropsPct = -1.0f;
    float     lastRttMs       = -1.0f;
    float     lastHostLatencyMs = -1.0f;
    float     lastDecodeMs    = -1.0f;
    float     lastBitrateMbps = -1.0f;
};

/**
 * What a finished session measured. Filled from the decoder's whole-session totals right
 * before it is torn down — see FFmpegVideoDecoder::getSessionStats().
 */
struct PlaytimeSessionStats
{
    float fpsAvg          = -1.0f;
    int   targetFps       = 0;
    float dropsPct        = -1.0f;
    float jitterDropsPct  = -1.0f;
    float rttMs           = -1.0f;
    float hostLatencyMs   = -1.0f;
    float decodeMs        = -1.0f;
    float bitrateMbps     = -1.0f;
};

class PlaytimeManager
{
public:
    static PlaytimeManager* get();

    /**
     * Adds time to a game's total. Called every 60 s during a session and once at the end,
     * so a session that ends by the process being killed still keeps all but its last
     * minute — no destructor runs in that case, and nothing else would save it.
     *
     * Only the *hours* move here. Nothing about "the last session" is written, because
     * mid-session there is no such thing yet.
     */
    void addSeconds(const QString& hostUuid, const QString& appName, int appId, qint64 seconds);

    /**
     * Closes a session: banks the time not yet flushed, then records it as the last one
     * played.
     *
     * ⚠️ Two durations, and they are not interchangeable. `unbankedSeconds` is what
     * addSeconds() has not already taken during the session — passing the full length here
     * would count every flushed minute twice. `sessionSeconds` is the session's real
     * length, which is what gets stored as "last session" and what the threshold is judged
     * against.
     *
     * ⚠️ A session under kMinSessionSeconds still counts towards the total — those minutes
     * were real — but does NOT become the host's last played game. A launch corrected ten
     * seconds later must not take the Resume button away from what you were actually
     * playing.
     */
    void endSession(const QString& hostUuid, const QString& appName, int appId,
                    qint64 unbankedSeconds, qint64 sessionSeconds,
                    const PlaytimeSessionStats& stats);

    PlaytimeRecord recordFor(const QString& hostUuid, const QString& appName) const;

    /// The game this host was last played on, or an invalid record when there is none.
    /// When it returns invalid the UI draws nothing at all — no empty state, no greyed row.
    PlaytimeRecord lastPlayedOn(const QString& hostUuid) const;

    /// Clears one game's record. Offered in the per-game panel: a total that cannot be
    /// corrected is a total that is eventually wrong (a session left running overnight).
    void reset(const QString& hostUuid, const QString& appName);

    /**
     * Drops every record belonging to a host. Same reasoning as
     * AppSettingsManager::forgetHost: keyed by uuid, so once the host is gone nothing can
     * reach them again.
     */
    void forgetHost(const QString& hostUuid);

    /**
     * Desktop and Steam Big Picture are not games and never accumulate hours.
     *
     * Delegates to isSystemApp() in nvapp.h — the single place those two names are spelled,
     * shared with the two app-list sort orders. A fourth copy here is exactly how one of
     * them ends up spelled differently.
     */
    static bool isTracked(const QString& appName);

    /// "18 h 42 m", "42 m", "124 h 05 m". One spelling for every screen that shows it.
    static QString formatDuration(qint64 seconds);

    /// A session shorter than this does not become the host's last played game.
    static constexpr qint64 kMinSessionSeconds = 60;

private:
    PlaytimeManager() = default;

    static QString normalise(const QString& appName);
    static QString digest(const QString& normalisedName);
    static QString gameGroup(const QString& hostUuid, const QString& appName);
    static QString hostGroup(const QString& hostUuid);
};
