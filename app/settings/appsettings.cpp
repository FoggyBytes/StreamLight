#include "appsettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QVector>
#include <algorithm>

#define GAME_PREFIX "appoverrides/"
#define HOST_PREFIX "hostprofiles/"

// ── shared override (de)serialization (caller has positioned the group) ──────
static AppOverride readOverrideGroup(const QSettings& s)
{
    AppOverride ov;
    if (s.contains("width") && s.contains("height")) {
        ov.hasResolution = true;
        ov.width = s.value("width").toInt();
        ov.height = s.value("height").toInt();
    }
    if (s.contains("fps"))         { ov.hasFps = true;         ov.fps = s.value("fps").toInt(); }
    if (s.contains("bitrate"))     { ov.hasBitrate = true;     ov.bitrateKbps = s.value("bitrate").toInt(); }
    if (s.contains("hdr"))         { ov.hasHdr = true;         ov.enableHdr = s.value("hdr").toBool(); }
    if (s.contains("codec"))       { ov.hasCodec = true;       ov.videoCodecConfig = s.value("codec").toInt(); }
    if (s.contains("framepacing")) { ov.hasFramePacing = true; ov.framePacingMode = s.value("framepacing").toInt(); }
    if (s.contains("audio"))       { ov.hasAudio = true;       ov.audioConfig = s.value("audio").toInt(); }
    if (s.contains("hue"))         { ov.hasHue = true;         ov.hueSync = s.value("hue").toBool(); }
    if (s.contains("matchlink"))   { ov.hasMatchLink = true;   ov.matchLinkSpeed = s.value("matchlink").toBool(); }
    if (s.contains("waitgame"))      { ov.hasWaitForGame = true; ov.waitForGame = s.value("waitgame").toBool(); }
    if (s.contains("matchrefresh"))  { ov.hasMatchRefreshRate = true; ov.matchRefreshRate = s.value("matchrefresh").toBool(); }
    if (s.contains("displaymode"))   { ov.hasDisplayMode = true; ov.windowMode = s.value("displaymode").toInt(); }
    if (s.contains("vsync"))         { ov.hasVsync = true;       ov.enableVsync = s.value("vsync").toBool(); }
    return ov;
}

static void writeOverrideGroup(QSettings& s, const AppOverride& ov)
{
    if (ov.hasResolution)  { s.setValue("width", ov.width); s.setValue("height", ov.height); }
    if (ov.hasFps)         s.setValue("fps", ov.fps);
    if (ov.hasBitrate)     s.setValue("bitrate", ov.bitrateKbps);
    if (ov.hasHdr)         s.setValue("hdr", ov.enableHdr);
    if (ov.hasCodec)       s.setValue("codec", ov.videoCodecConfig);
    if (ov.hasFramePacing) s.setValue("framepacing", ov.framePacingMode);
    if (ov.hasAudio)       s.setValue("audio", ov.audioConfig);
    if (ov.hasHue)         s.setValue("hue", ov.hueSync);
    if (ov.hasMatchLink)   s.setValue("matchlink", ov.matchLinkSpeed);
    if (ov.hasWaitForGame) s.setValue("waitgame", ov.waitForGame);
    if (ov.hasMatchRefreshRate) s.setValue("matchrefresh", ov.matchRefreshRate);
    if (ov.hasDisplayMode) s.setValue("displaymode", ov.windowMode);
    if (ov.hasVsync)       s.setValue("vsync", ov.enableVsync);
}

QVariantMap appOverrideToMap(const AppOverride& ov)
{
    QVariantMap m;
    if (ov.hasResolution)  { m["width"] = ov.width; m["height"] = ov.height; }
    if (ov.hasFps)         m["fps"] = ov.fps;
    if (ov.hasBitrate)     m["bitrate"] = ov.bitrateKbps;
    if (ov.hasHdr)         m["hdr"] = ov.enableHdr;
    if (ov.hasCodec)       m["codec"] = ov.videoCodecConfig;
    if (ov.hasFramePacing) m["framepacing"] = ov.framePacingMode;
    if (ov.hasAudio)       m["audio"] = ov.audioConfig;
    if (ov.hasHue)         m["hue"] = ov.hueSync;
    if (ov.hasMatchLink)   m["matchlink"] = ov.matchLinkSpeed;
    if (ov.hasWaitForGame) m["waitgame"] = ov.waitForGame;
    if (ov.hasMatchRefreshRate) m["matchrefresh"] = ov.matchRefreshRate;
    if (ov.hasDisplayMode) m["displaymode"] = ov.windowMode;
    if (ov.hasVsync)       m["vsync"] = ov.enableVsync;
    return m;
}

AppOverride appOverrideFromMap(const QVariantMap& m)
{
    AppOverride ov;
    if (m.contains("width") && m.contains("height")) {
        ov.hasResolution = true;
        ov.width = m.value("width").toInt();
        ov.height = m.value("height").toInt();
    }
    if (m.contains("fps"))         { ov.hasFps = true;         ov.fps = m.value("fps").toInt(); }
    if (m.contains("bitrate"))     { ov.hasBitrate = true;     ov.bitrateKbps = m.value("bitrate").toInt(); }
    if (m.contains("hdr"))         { ov.hasHdr = true;         ov.enableHdr = m.value("hdr").toBool(); }
    if (m.contains("codec"))       { ov.hasCodec = true;       ov.videoCodecConfig = m.value("codec").toInt(); }
    if (m.contains("framepacing")) { ov.hasFramePacing = true; ov.framePacingMode = m.value("framepacing").toInt(); }
    if (m.contains("audio"))       { ov.hasAudio = true;       ov.audioConfig = m.value("audio").toInt(); }
    if (m.contains("hue"))         { ov.hasHue = true;         ov.hueSync = m.value("hue").toBool(); }
    if (m.contains("matchlink"))   { ov.hasMatchLink = true;   ov.matchLinkSpeed = m.value("matchlink").toBool(); }
    if (m.contains("waitgame"))      { ov.hasWaitForGame = true; ov.waitForGame = m.value("waitgame").toBool(); }
    if (m.contains("matchrefresh"))  { ov.hasMatchRefreshRate = true; ov.matchRefreshRate = m.value("matchrefresh").toBool(); }
    if (m.contains("displaymode"))   { ov.hasDisplayMode = true; ov.windowMode = m.value("displaymode").toInt(); }
    if (m.contains("vsync"))         { ov.hasVsync = true;       ov.enableVsync = m.value("vsync").toBool(); }
    return ov;
}

QVariantMap inheritedValueLabels(const StreamingPreferences* p)
{
    QVariantMap m;
    if (p == nullptr) {
        return m;
    }

    // Resolution reads back as the preset name when it is one, because that is what the
    // pill next to it says. Anything else — a custom resolution, or a preset we do not
    // offer — is printed as itself rather than rounded to the nearest label.
    QString res;
    if      (p->width == 1280 && p->height == 720)  res = QStringLiteral("720p");
    else if (p->width == 1920 && p->height == 1080) res = QStringLiteral("1080p");
    else if (p->width == 2560 && p->height == 1440) res = QStringLiteral("1440p");
    else if (p->width == 3840 && p->height == 2160) res = QStringLiteral("4K");
    else res = QString::number(p->width) + QChar(0x00D7) + QString::number(p->height);
    m.insert(QStringLiteral("resolution"), res);

    m.insert(QStringLiteral("fps"), QString::number(p->fps));

    // Bare number: the row it sits on is already labelled "Bitrate (Mbps)", and the unit
    // repeated inside the pill is the widest thing on the widest row for no information.
    m.insert(QStringLiteral("bitrate"), QString::number(qRound(p->bitrateKbps / 1000.0)));

    const QString on  = QCoreApplication::translate("AppSettings", "On");
    const QString off = QCoreApplication::translate("AppSettings", "Off");

    m.insert(QStringLiteral("hdr"),          p->enableHdr ? on : off);
    m.insert(QStringLiteral("hue"),          p->hueSyncIntegration ? on : off);
    m.insert(QStringLiteral("matchlink"),    p->matchHostLinkSpeed ? on : off);
    m.insert(QStringLiteral("waitgame"),     p->waitForGameOnScreen ? on : off);
    m.insert(QStringLiteral("matchrefresh"), p->matchRefreshRate ? on : off);
    m.insert(QStringLiteral("vsync"),        p->enableVsync ? on : off);
    m.insert(QStringLiteral("framepacing"),
             p->framePacingMode == StreamingPreferences::FP_ON ? on : off);

    QString codec;
    switch (p->videoCodecConfig) {
    case StreamingPreferences::VCC_FORCE_H264: codec = QStringLiteral("H.264"); break;
    case StreamingPreferences::VCC_FORCE_HEVC: codec = QStringLiteral("HEVC");  break;
    case StreamingPreferences::VCC_FORCE_AV1:  codec = QStringLiteral("AV1");   break;
    // VCC_AUTO, and the deprecated forced-HEVC-HDR value that old settings can still hold.
    default: codec = QCoreApplication::translate("AppSettings", "Auto"); break;
    }
    m.insert(QStringLiteral("codec"), codec);

    QString audio;
    switch (p->audioConfig) {
    case StreamingPreferences::AC_51_SURROUND: audio = QStringLiteral("5.1"); break;
    case StreamingPreferences::AC_71_SURROUND: audio = QStringLiteral("7.1"); break;
    default: audio = QCoreApplication::translate("AppSettings", "Stereo"); break;
    }
    m.insert(QStringLiteral("audio"), audio);

    QString dm;
    switch (p->windowMode) {
    case StreamingPreferences::WM_FULLSCREEN_DESKTOP:
        dm = QCoreApplication::translate("AppSettings", "Borderless"); break;
    case StreamingPreferences::WM_WINDOWED:
        dm = QCoreApplication::translate("AppSettings", "Windowed"); break;
    default:
        dm = QCoreApplication::translate("AppSettings", "Fullscreen"); break;
    }
    m.insert(QStringLiteral("displaymode"), dm);

    return m;
}

void applyAppOverride(StreamingPreferences* p, const AppOverride& ov)
{
    if (ov.hasResolution)  { p->width = ov.width; p->height = ov.height; }
    if (ov.hasFps)         p->fps = ov.fps;
    if (ov.hasBitrate)     p->bitrateKbps = ov.bitrateKbps;
    if (ov.hasHdr)         p->enableHdr = ov.enableHdr;
    if (ov.hasCodec)       p->videoCodecConfig = (StreamingPreferences::VideoCodecConfig)ov.videoCodecConfig;
    // ⚠️ Overrides live in their own store and never pass through reload(), so the
    // frame-pacing values 3.4.0 - 5.1.3 could write have to be collapsed here too.
    // Legacy 2 was Matched and 3 was Multiple; both are gone. Same rule reload() uses:
    // Matched becomes On, Multiple becomes On only where V-Sync gives the software
    // Pacer something to pace against. p->enableVsync is read rather than the global
    // because a profile can override V-Sync itself, and that is applied below — so
    // this deliberately reads the value the session will actually run with only when
    // the override does not touch it; when it does, the two land in the same profile
    // and the user chose both.
    if (ov.hasFramePacing) {
        const int LEGACY_MATCHED = 2, LEGACY_MULTIPLE = 3;
        int stored = ov.framePacingMode;
        bool vsync = ov.hasVsync ? ov.enableVsync : p->enableVsync;
        if (stored == LEGACY_MATCHED) {
            stored = StreamingPreferences::FP_ON;
        }
        else if (stored == LEGACY_MULTIPLE) {
            stored = vsync ? StreamingPreferences::FP_ON : StreamingPreferences::FP_OFF;
        }
        else if (stored < StreamingPreferences::FP_OFF || stored > StreamingPreferences::FP_ON) {
            stored = StreamingPreferences::FP_OFF;
        }
        p->framePacingMode = (StreamingPreferences::FramePacingMode)stored;
    }
    if (ov.hasAudio)       p->audioConfig = (StreamingPreferences::AudioConfig)ov.audioConfig;
    if (ov.hasHue)         p->hueSyncIntegration = ov.hueSync;
    if (ov.hasMatchLink)   p->matchHostLinkSpeed = ov.matchLinkSpeed;
    if (ov.hasWaitForGame) p->waitForGameOnScreen = ov.waitForGame;
    // No migration to do here, unlike frame pacing above: this key is new in 5.2.1 and
    // deliberately does not read the `refreshrate` one that 5.1.0 - 5.1.3 profiles could
    // hold. That was a four-value enum for a setting that no longer exists, and Session
    // decides on its own whether exclusive fullscreen makes this actionable at all.
    if (ov.hasMatchRefreshRate) p->matchRefreshRate = ov.matchRefreshRate;
    if (ov.hasDisplayMode) p->windowMode = (StreamingPreferences::WindowMode)ov.windowMode;
    if (ov.hasVsync)       p->enableVsync = ov.enableVsync;
}

// ── AppSettingsManager (per-game) ────────────────────────────────────────────
AppSettingsManager* AppSettingsManager::get()
{
    static AppSettingsManager instance;
    return &instance;
}

QString AppSettingsManager::keyFor(const QString& hostUuid, int appId)
{
    return QStringLiteral(GAME_PREFIX) + hostUuid + QStringLiteral("_") + QString::number(appId);
}

AppOverride AppSettingsManager::getOverride(const QString& hostUuid, int appId) const
{
    QSettings settings;
    settings.beginGroup(keyFor(hostUuid, appId));
    AppOverride ov = readOverrideGroup(settings);
    settings.endGroup();
    return ov;
}

void AppSettingsManager::setOverride(const QString& hostUuid, int appId, const AppOverride& ov)
{
    QSettings settings;
    QString group = keyFor(hostUuid, appId);
    settings.remove(group);
    settings.beginGroup(group);
    writeOverrideGroup(settings, ov);
    settings.endGroup();
}

bool AppSettingsManager::hasOverride(const QString& hostUuid, int appId) const
{
    QSettings settings;
    settings.beginGroup(keyFor(hostUuid, appId));
    bool has = !settings.childKeys().isEmpty();
    settings.endGroup();
    return has;
}

void AppSettingsManager::clearOverride(const QString& hostUuid, int appId)
{
    QSettings settings;
    settings.remove(keyFor(hostUuid, appId));
}

StreamingPreferences* AppSettingsManager::buildPrefs(StreamingPreferences* base,
                                                     const QString& hostUuid, int appId,
                                                     QObject* parent) const
{
    StreamingPreferences* p = base->clone(parent);
    // Cascade: global ← host active profile ← per-game override.
    applyAppOverride(p, HostProfileManager::get()->activeOverride(hostUuid));
    AppOverride game = getOverride(hostUuid, appId);
    if (!game.isEmpty()) {
        applyAppOverride(p, game);
    }
    return p;
}

// ── HostProfileManager (per-host profiles) ───────────────────────────────────
HostProfileManager* HostProfileManager::get()
{
    static HostProfileManager instance;
    return &instance;
}

void AppSettingsManager::forgetHost(const QString& hostUuid)
{
    if (hostUuid.isEmpty()) {
        return;
    }

    // Per-game overrides are flat keys, not a group, so they have to be found by prefix.
    // Collected first and removed afterwards: removing while iterating the same QSettings
    // is not something to rely on.
    QSettings settings;
    const QString prefix = QStringLiteral(GAME_PREFIX) + hostUuid + QStringLiteral("_");
    QStringList doomed;
    for (const QString& key : settings.allKeys()) {
        if (key.startsWith(prefix)) {
            doomed.append(key);
        }
    }
    for (const QString& key : doomed) {
        settings.remove(key);
    }
}

void HostProfileManager::forgetHost(const QString& uuid)
{
    if (uuid.isEmpty()) {
        return;
    }

    // Profiles DO live under a group of their own, so one remove takes the lot.
    QSettings settings;
    settings.remove(grp(uuid));
}

QString HostProfileManager::grp(const QString& uuid)
{
    return QStringLiteral(HOST_PREFIX) + uuid;
}

QString HostProfileManager::pgrp(const QString& uuid, int slot)
{
    return grp(uuid) + QStringLiteral("/p") + QString::number(slot);
}

int HostProfileManager::count(const QString& uuid) const
{
    QSettings settings;
    int c = settings.value(grp(uuid) + "/count", 0).toInt();
    return std::max(0, std::min(c, kMaxProfiles));
}

int HostProfileManager::active(const QString& uuid) const
{
    int c = count(uuid);
    if (c <= 0) {
        return -1;
    }
    QSettings settings;
    int a = settings.value(grp(uuid) + "/active", 0).toInt();
    if (a < 0) {
        return -1;   // explicitly OFF: no active profile, host uses global
    }
    return std::max(0, std::min(a, c - 1));
}

void HostProfileManager::setActive(const QString& uuid, int slot)
{
    int c = count(uuid);
    if (c <= 0) {
        return;
    }
    QSettings settings;
    if (slot < 0) {
        settings.setValue(grp(uuid) + "/active", -1);   // OFF
        return;
    }
    settings.setValue(grp(uuid) + "/active", std::max(0, std::min(slot, c - 1)));
}

void HostProfileManager::cycle(const QString& uuid, int dir)
{
    // Global is one of the stops, not the state you leave by configuring a profile. Without it
    // there was no way back to the unmodified settings from the pad, and a host with exactly one
    // profile could not cycle at all — the shoulders did nothing at all in the commonest setup.
    //
    // Positions run -1 (Global), 0 … c-1, so a host with one profile has two stops.
    int c = count(uuid);
    if (c <= 0) {
        return;   // nothing configured: Global is the only state there is
    }
    int pos = active(uuid) + 1;                       // -1 → 0
    pos = ((pos + dir) % (c + 1) + (c + 1)) % (c + 1);
    setActive(uuid, pos - 1);
}

QString HostProfileManager::name(const QString& uuid, int slot) const
{
    QSettings settings;
    return settings.value(pgrp(uuid, slot) + "/name",
                          QStringLiteral("Profile ") + QString::number(slot + 1)).toString();
}

void HostProfileManager::setName(const QString& uuid, int slot, const QString& name)
{
    QString trimmed = name.trimmed().left(kMaxNameLen);
    if (trimmed.isEmpty()) {
        trimmed = QStringLiteral("Profile ") + QString::number(slot + 1);
    }
    QSettings settings;
    settings.setValue(pgrp(uuid, slot) + "/name", trimmed);
}

AppOverride HostProfileManager::settings(const QString& uuid, int slot) const
{
    QSettings s;
    s.beginGroup(pgrp(uuid, slot));
    AppOverride ov = readOverrideGroup(s);
    s.endGroup();
    return ov;
}

void HostProfileManager::setSettings(const QString& uuid, int slot, const AppOverride& ov)
{
    // Preserve the name; rewrite only the override fields.
    QString nm = name(uuid, slot);
    QSettings s;
    s.remove(pgrp(uuid, slot));
    s.beginGroup(pgrp(uuid, slot));
    s.setValue("name", nm);
    writeOverrideGroup(s, ov);
    s.endGroup();
}

int HostProfileManager::add(const QString& uuid)
{
    int c = count(uuid);
    if (c >= kMaxProfiles) {
        return -1;
    }
    int slot = c;
    QSettings settings;
    settings.setValue(grp(uuid) + "/count", c + 1);
    settings.setValue(pgrp(uuid, slot) + "/name",
                      QStringLiteral("Profile ") + QString::number(slot + 1));
    if (c == 0) {
        settings.setValue(grp(uuid) + "/active", 0);
    }
    return slot;
}

void HostProfileManager::remove(const QString& uuid, int slot)
{
    int c = count(uuid);
    if (slot < 0 || slot >= c) {
        return;
    }

    // Snapshot remaining profiles, then renumber from scratch.
    struct P { QString name; AppOverride ov; };
    QVector<P> remaining;
    for (int i = 0; i < c; i++) {
        if (i == slot) continue;
        remaining.append({ name(uuid, i), settings(uuid, i) });
    }

    int oldActive = active(uuid);   // -1 == OFF (preserved below)
    int newActive = oldActive;
    if (oldActive == slot)      newActive = 0;
    else if (oldActive > slot)  newActive = oldActive - 1;

    QSettings s;
    s.remove(grp(uuid));   // wipe the whole host group, then rewrite

    const int remCount = (int)remaining.size();
    s.setValue(grp(uuid) + "/count", remCount);
    if (remCount > 0) {
        s.setValue(grp(uuid) + "/active",
                   newActive < 0 ? -1 : std::max(0, std::min(newActive, remCount - 1)));
    }
    for (int i = 0; i < remCount; i++) {
        s.beginGroup(pgrp(uuid, i));
        s.setValue("name", remaining[i].name);
        writeOverrideGroup(s, remaining[i].ov);
        s.endGroup();
    }
}

AppOverride HostProfileManager::activeOverride(const QString& uuid) const
{
    int a = active(uuid);
    if (a < 0) {
        return AppOverride();
    }
    return settings(uuid, a);
}
