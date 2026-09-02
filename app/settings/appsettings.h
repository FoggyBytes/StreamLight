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
    bool hasHue = false;        bool hueSync = false;       // StreamingPreferences::hueSyncIntegration
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

    bool isEmpty() const
    {
        return !(hasResolution || hasFps || hasBitrate || hasHdr ||
                 hasCodec || hasFramePacing || hasAudio || hasHue || hasMatchLink ||
                 hasWaitForGame || hasDisplayMode || hasVsync || hasFractionalVsync);
    }
};

// Applies a (non-Global) AppOverride onto a StreamingPreferences instance.
void applyAppOverride(StreamingPreferences* p, const AppOverride& ov);

// QVariantMap (QML) ↔ AppOverride. Map keys (subset): width, height, fps,
// bitrate, hdr, codec, framepacing, audio. A missing key = "inherit".
QVariantMap appOverrideToMap(const AppOverride& ov);
AppOverride appOverrideFromMap(const QVariantMap& m);

/**
 * What "inherit" actually resolves to, one entry per override row, already formatted
 * for display: {"resolution": "1080p", "fps": "60", "bitrate": "40", "hdr": "Off", …}.
 * Keys are the same ones appOverrideFromMap() reads, so a row and its inherited value
 * are looked up by the same string.
 *
 * ⚠️ Formatted here rather than in QML on purpose. The two override editors — the
 * per-game dialog and the host-profiles dialog — already keep mirrored copies of the
 * label tables, and a third and fourth copy of "how a codec enum is spelled" is exactly
 * how one of them ends up a release behind (it has happened: see the note on
 * AppModel::getAppOverride). The wording lives next to the enums it describes.
 */
QVariantMap inheritedValueLabels(const StreamingPreferences* p);

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

// Per-host streaming profiles (StreamLight 4.0.0): up to 3 named profiles per
// host, each an AppOverride; one is active. The active profile is applied as a
// host-level override at launch (below per-game). Stored via QSettings.
class HostProfileManager
{
public:
    static const int kMaxProfiles = 3;
    static const int kMaxNameLen = 14;

    static HostProfileManager* get();

    int count(const QString& uuid) const;          // 0..kMaxProfiles
    int active(const QString& uuid) const;          // slot index, or -1 if none
    void setActive(const QString& uuid, int slot);
    void cycle(const QString& uuid, int dir);       // wraps among existing

    QString name(const QString& uuid, int slot) const;
    void setName(const QString& uuid, int slot, const QString& name);
    AppOverride settings(const QString& uuid, int slot) const;
    void setSettings(const QString& uuid, int slot, const AppOverride& ov);

    int add(const QString& uuid);                   // new slot, or -1 if full
    void remove(const QString& uuid, int slot);

    /// Drops all of a host's profiles. Same reasoning as AppSettingsManager::forgetHost.
    void forgetHost(const QString& uuid);

    AppOverride activeOverride(const QString& uuid) const;  // active settings, or empty

private:
    HostProfileManager() = default;
    static QString grp(const QString& uuid);
    static QString pgrp(const QString& uuid, int slot);
};
