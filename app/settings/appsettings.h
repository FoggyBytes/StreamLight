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

    bool isEmpty() const
    {
        return !(hasResolution || hasFps || hasBitrate || hasHdr ||
                 hasCodec || hasFramePacing || hasAudio || hasHue);
    }
};

// Applies a (non-Global) AppOverride onto a StreamingPreferences instance.
void applyAppOverride(StreamingPreferences* p, const AppOverride& ov);

// QVariantMap (QML) ↔ AppOverride. Map keys (subset): width, height, fps,
// bitrate, hdr, codec, framepacing, audio. A missing key = "inherit".
QVariantMap appOverrideToMap(const AppOverride& ov);
AppOverride appOverrideFromMap(const QVariantMap& m);

class AppSettingsManager
{
public:
    static AppSettingsManager* get();

    AppOverride getOverride(const QString& hostUuid, int appId) const;
    void setOverride(const QString& hostUuid, int appId, const AppOverride& ov);
    bool hasOverride(const QString& hostUuid, int appId) const;
    void clearOverride(const QString& hostUuid, int appId);

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

    AppOverride activeOverride(const QString& uuid) const;  // active settings, or empty

private:
    HostProfileManager() = default;
    static QString grp(const QString& uuid);
    static QString pgrp(const QString& uuid, int slot);
};
