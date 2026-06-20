#include "appsettings.h"

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
    return ov;
}

void applyAppOverride(StreamingPreferences* p, const AppOverride& ov)
{
    if (ov.hasResolution)  { p->width = ov.width; p->height = ov.height; }
    if (ov.hasFps)         p->fps = ov.fps;
    if (ov.hasBitrate)     p->bitrateKbps = ov.bitrateKbps;
    if (ov.hasHdr)         p->enableHdr = ov.enableHdr;
    if (ov.hasCodec)       p->videoCodecConfig = (StreamingPreferences::VideoCodecConfig)ov.videoCodecConfig;
    if (ov.hasFramePacing) p->framePacingMode = (StreamingPreferences::FramePacingMode)ov.framePacingMode;
    if (ov.hasAudio)       p->audioConfig = (StreamingPreferences::AudioConfig)ov.audioConfig;
    if (ov.hasHue)         p->hueSyncIntegration = ov.hueSync;
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
    int c = count(uuid);
    if (c <= 1) {
        return;
    }
    int a = active(uuid);
    a = ((a + dir) % c + c) % c;
    setActive(uuid, a);
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
