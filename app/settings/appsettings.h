#pragma once

// Per-app (per-game) settings overrides (StreamLight 4.0.0).
//
// Each game can override a subset of the global StreamingPreferences. At launch
// the global preferences are cloned and the game's overrides applied on top, so
// the global settings (and the QML-bound singleton) are never mutated.
//
// Overrides are keyed by "<host uuid>_<app id>" and persisted via QSettings.

#include <QString>
#include <QVariantMap>

#include "settings/streamingpreferences.h"

struct AppOverride
{
    bool hasResolution = false; int width = 0; int height = 0;
    bool hasFps = false;        int fps = 0;
    bool hasBitrate = false;    int bitrateKbps = 0;
    bool hasHdr = false;        bool enableHdr = false;
    bool hasCodec = false;      int videoCodecConfig = 0;   // StreamingPreferences::VideoCodecConfig
    bool hasFramePacing = false;int framePacingMode = 0;    // StreamingPreferences::FramePacingMode
    bool hasAudio = false;      int audioConfig = 0;        // StreamingPreferences::AudioConfig
    // Host link matching (4.6.0). A profile is a *situation*, not just a quality preset —
    // "docked" and "handheld" differ here as much as they do in resolution — so this is
    // worth overriding per profile. Exposed only in the host-profile UI: the value never
    // varies by game, and a per-game knob would just be somewhere for a stale value to hide.
    bool hasMatchLink = false;  bool matchLinkSpeed = false; // StreamingPreferences::matchHostLinkSpeed

    // Whether to hold the launch screen until the host reports the game on screen, rather than
    // showing the stream as soon as there is a picture. Overridable at both levels, for two
    // different reasons: a host profile because it describes a situation (a host across the room
    // can't be walked over to), and a game because a title that opens its own launcher never
    // puts a game window on screen, so there is nothing for the wait to end on. Absent =
    // inherit the level below.
    bool hasWaitForGame = false;  bool waitForGame = false; // StreamingPreferences::waitForGameOnScreen

    // ⚠️ The two below are dependencies, not features: Frame pacing only means anything
    // with V-Sync on, and a window mode decides whether a session is in exclusive
    // fullscreen at all. The dependent was already overridable per profile while these
    // were global-only, so a profile could hold a setting whose condition it had no way
    // to express — and nothing said so. They are host-profile only, like the settings
    // that need them: a window mode and a V-Sync choice describe the device and the
    // situation, never a particular game.
    //
    // ⚠️ Display mode arrived here as Match refresh rate's condition, and that setting is
    // gone as of 5.5.0. It stays because it is a useful override in its own right — but if
    // it is ever reviewed, this is why it exists and V-Sync is the one with a live
    // dependent.
    bool hasDisplayMode = false;  int windowMode = 0;       // StreamingPreferences::WindowMode
    bool hasVsync = false;        bool enableVsync = false; // StreamingPreferences::enableVsync

    // Fractional V-Sync (5.6.0). Host-profile only, and for two reasons that point the
    // same way.
    //
    // ⚠️ It is a *dependent* of the two above — V-Sync, and Frame pacing — and both of
    // those can be expressed at this level. A per-game copy could not express either, so
    // a game could carry "on" under a profile whose V-Sync is off: the same defect the
    // note above describes, built the other way round. It is deliberately absent from the
    // per-game panel and must stay absent.
    //
    // ⚠️ What makes it a profile setting rather than a global one is VRR: a sync interval
    // above 1 turns variable refresh off, so a handheld profile on a VRR panel and a
    // docked profile on a fixed-refresh screen want opposite answers at the same frame
    // rate. Everything else that varies by situation — a panel that is not a whole
    // multiple of the stream, a profile that streams at 120 instead of 60 — the renderer
    // already decides on its own, and needs no knob to do it.
    bool hasFractionalVsync = false; bool fractionalVsync = false; // StreamingPreferences::fractionalVsync

    // VRR (6.0.0). Host-profile only, for the same reason and with the same shape as
    // Fractional V-Sync above: it is a dependent of V-Sync, and the situation it
    // describes is the panel — a handheld profile on a VRR display and a docked one on
    // a fixed-refresh screen want opposite answers at the same frame rate.
    //
    // ⚠️ VRR and Fractional V-Sync are mutually exclusive and VRR wins; the resolution
    // lives in Session::snapshotPresentationSettings(), NOT here. A profile may hold
    // both switched on, and keeps both: the session decides, and says so in the log.
    bool hasVrr = false;          bool enableVrr = false;    // StreamingPreferences::enableVrr

    // YUV 4:4:4 (6.4.0). Both levels, like HDR and the codec it goes with: the answer
    // depends on the codec, and the codec is already overridable at both. PyroWave
    // 4:4:4 and AV1 4:2:0 are two profiles on the same host — and with the switch only
    // global, an AV1 profile under a 4:4:4 Settings asked for a format no NVENC host
    // offers and launched with a warning every time.
    bool hasYuv444 = false;       bool enableYuv444 = false; // StreamingPreferences::enableYUV444

    bool isEmpty() const
    {
        return !(hasResolution || hasFps || hasBitrate || hasHdr ||
                 hasCodec || hasFramePacing || hasAudio || hasMatchLink ||
                 hasWaitForGame || hasDisplayMode || hasVsync || hasFractionalVsync ||
                 hasVrr || hasYuv444);
    }
};

// Applies a (non-Global) AppOverride onto a StreamingPreferences instance.
void applyAppOverride(StreamingPreferences* p, const AppOverride& ov);

// QVariantMap (QML) ↔ AppOverride. Map keys (subset): width, height, fps,
// bitrate, hdr, yuv444, codec, framepacing, audio. A missing key = "inherit".
QVariantMap appOverrideToMap(const AppOverride& ov);
AppOverride appOverrideFromMap(const QVariantMap& m);

// What "inherit" resolves to, one entry per override row (8.8.0), keyed exactly like an override
// map (width, height, fps, bitrate, hdr, yuv444, codec, framepacing, audio, matchlink, waitgame,
// displaymode, vsync, fractionalvsync, vrr): the profile and per-game sheets compare and pick on
// one shape. (Until 8.7.1 the sheets got these values as display labels, inheritedValueLabels.)
QVariantMap inheritedValues(const StreamingPreferences* p);

class AppSettingsManager
{
public:
    static AppSettingsManager* get();

    AppOverride getOverride(const QString& hostUuid, int appId) const;
    void setOverride(const QString& hostUuid, int appId, const AppOverride& ov);
    bool hasOverride(const QString& hostUuid, int appId) const;
    void clearOverride(const QString& hostUuid, int appId);

    /**
     * Drops every per-game override belonging to a host.
     *
     * Called when the host is deleted. These records are keyed by host uuid, so once the
     * host is gone nothing can ever reach them again — they are not "kept in case it comes
     * back", they are unreachable bytes that accumulate for the life of the install.
     */
    void forgetHost(const QString& hostUuid);

    // Returns a per-session StreamingPreferences: a clone of `base` with the
    // host's active profile applied, then this game's overrides on top
    // (cascade: global ← host profile ← per-game). The returned object is owned
    // by the caller (pass a parent, or re-parent it to the Session).
    StreamingPreferences* buildPrefs(StreamingPreferences* base,
                                     const QString& hostUuid, int appId,
                                     QObject* parent = nullptr) const;

private:
    AppSettingsManager() = default;
    static QString keyFor(const QString& hostUuid, int appId);
};

// Per-host streaming profiles (StreamLight 4.0.0): named profiles per host, each an
// AppOverride; one is active. As many as the user wants since 8.0.0 (#42) — it was 3. The active profile is applied as a
// host-level override at launch (below per-game). Stored via QSettings.
class HostProfileManager
{
public:
    // 20 since 8.8.3 (#54), was 14. Also in HostProfilesDialog._maxNameLen and
    // ProfileNameDialog.maxLength.
    static const int kMaxNameLen = 20;

    static HostProfileManager* get();

    int count(const QString& uuid) const;          // 0 or more
    int active(const QString& uuid) const;          // slot index, or -1 if none
    void setActive(const QString& uuid, int slot);
    void cycle(const QString& uuid, int dir);       // wraps among existing

    QString name(const QString& uuid, int slot) const;
    void setName(const QString& uuid, int slot, const QString& name);
    AppOverride settings(const QString& uuid, int slot) const;
    void setSettings(const QString& uuid, int slot, const AppOverride& ov);

    int add(const QString& uuid);                   // the new slot
    void remove(const QString& uuid, int slot);
    /// 8.8.0 (#53): the profile at `from` goes to `to`, the others close up. The slot order is the
    /// CUSTOM order; the active profile stays active wherever it lands.
    void move(const QString& uuid, int from, int to);

    /// 8.8.0 (#53): how a host's profiles are listed — in Profiles' sidebar, Home's profile list
    /// and the LT/RT cycle alike. Custom is the slot order (move() changes it).
    enum SortMode { SortAlphabetical = 0, SortCreated = 1, SortCustom = 2 };
    int sortMode(const QString& uuid) const;
    void setSortMode(const QString& uuid, int mode);
    /// When the profile was created, as a sequence number that moves and removals keep. Profiles
    /// made before 8.8.0 have none stored: their slot stands in, which is their creation order.
    int created(const QString& uuid, int slot) const;
    /// The slots in the order the sort mode gives.
    QVector<int> order(const QString& uuid) const;

    /// Drops all of a host's profiles. Same reasoning as AppSettingsManager::forgetHost.
    void forgetHost(const QString& uuid);

    AppOverride activeOverride(const QString& uuid) const;  // active settings, or empty

private:
    HostProfileManager() = default;
    static QString grp(const QString& uuid);
    static QString pgrp(const QString& uuid, int slot);
};
