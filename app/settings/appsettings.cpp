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
    if (s.contains("matchlink"))   { ov.hasMatchLink = true;   ov.matchLinkSpeed = s.value("matchlink").toBool(); }
    if (s.contains("waitgame"))      { ov.hasWaitForGame = true; ov.waitForGame = s.value("waitgame").toBool(); }
    if (s.contains("displaymode"))   { ov.hasDisplayMode = true; ov.windowMode = s.value("displaymode").toInt(); }
    if (s.contains("vsync"))         { ov.hasVsync = true;       ov.enableVsync = s.value("vsync").toBool(); }
    if (s.contains("fractionalvsync")) { ov.hasFractionalVsync = true; ov.fractionalVsync = s.value("fractionalvsync").toBool(); }
    if (s.contains("vrr"))          { ov.hasVrr = true;         ov.enableVrr = s.value("vrr").toBool(); }
    if (s.contains("yuv444"))       { ov.hasYuv444 = true;      ov.enableYuv444 = s.value("yuv444").toBool(); }
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
    if (ov.hasMatchLink)   s.setValue("matchlink", ov.matchLinkSpeed);
    if (ov.hasWaitForGame) s.setValue("waitgame", ov.waitForGame);
    if (ov.hasDisplayMode) s.setValue("displaymode", ov.windowMode);
    if (ov.hasVsync)       s.setValue("vsync", ov.enableVsync);
    if (ov.hasFractionalVsync) s.setValue("fractionalvsync", ov.fractionalVsync);
    if (ov.hasVrr)         s.setValue("vrr", ov.enableVrr);
    if (ov.hasYuv444)      s.setValue("yuv444", ov.enableYuv444);
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
    if (ov.hasMatchLink)   m["matchlink"] = ov.matchLinkSpeed;
    if (ov.hasWaitForGame) m["waitgame"] = ov.waitForGame;
    if (ov.hasDisplayMode) m["displaymode"] = ov.windowMode;
    if (ov.hasVsync)       m["vsync"] = ov.enableVsync;
    if (ov.hasFractionalVsync) m["fractionalvsync"] = ov.fractionalVsync;
    if (ov.hasVrr)         m["vrr"] = ov.enableVrr;
    if (ov.hasYuv444)      m["yuv444"] = ov.enableYuv444;
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
    if (m.contains("matchlink"))   { ov.hasMatchLink = true;   ov.matchLinkSpeed = m.value("matchlink").toBool(); }
    if (m.contains("waitgame"))      { ov.hasWaitForGame = true; ov.waitForGame = m.value("waitgame").toBool(); }
    if (m.contains("displaymode"))   { ov.hasDisplayMode = true; ov.windowMode = m.value("displaymode").toInt(); }
    if (m.contains("vsync"))         { ov.hasVsync = true;       ov.enableVsync = m.value("vsync").toBool(); }
    if (m.contains("fractionalvsync")) { ov.hasFractionalVsync = true; ov.fractionalVsync = m.value("fractionalvsync").toBool(); }
    if (m.contains("vrr"))          { ov.hasVrr = true;         ov.enableVrr = m.value("vrr").toBool(); }
    if (m.contains("yuv444"))       { ov.hasYuv444 = true;      ov.enableYuv444 = m.value("yuv444").toBool(); }
    return ov;
}

QVariantMap inheritedValues(const StreamingPreferences* p)
{
    QVariantMap m;
    if (p == nullptr) {
        return m;
    }
    m.insert(QStringLiteral("width"), p->width);
    m.insert(QStringLiteral("height"), p->height);
    m.insert(QStringLiteral("fps"), p->fps);
    m.insert(QStringLiteral("bitrate"), p->bitrateKbps);
    m.insert(QStringLiteral("hdr"), p->enableHdr);
    m.insert(QStringLiteral("yuv444"), p->enableYUV444);
    // The deprecated forced-HEVC-HDR value old settings can still hold runs as Auto.
    m.insert(QStringLiteral("codec"), p->videoCodecConfig == StreamingPreferences::VCC_FORCE_HEVC_HDR_DEPRECATED
                                      ? (int)StreamingPreferences::VCC_AUTO : (int)p->videoCodecConfig);
    m.insert(QStringLiteral("framepacing"), (int)p->framePacingMode);
    m.insert(QStringLiteral("audio"), (int)p->audioConfig);
    m.insert(QStringLiteral("matchlink"), p->matchHostLinkSpeed);
    m.insert(QStringLiteral("waitgame"), p->waitForGameOnScreen);
    m.insert(QStringLiteral("displaymode"), (int)p->windowMode);
    m.insert(QStringLiteral("vsync"), p->enableVsync);
    m.insert(QStringLiteral("fractionalvsync"), p->fractionalVsync);
    m.insert(QStringLiteral("vrr"), p->enableVrr);
    return m;
}

void applyAppOverride(StreamingPreferences* p, const AppOverride& ov)
{
    if (ov.hasResolution)  { p->width = ov.width; p->height = ov.height; }
    if (ov.hasFps)         p->fps = ov.fps;
    if (ov.hasBitrate)     p->bitrateKbps = ov.bitrateKbps;
    if (ov.hasHdr)         p->enableHdr = ov.enableHdr;
    // ⚠️ The bitrate is left alone on purpose. Settings re-derives it when the global
    // switch flips (and only under autoAdjustBitrate); a profile has its own Bitrate row
    // for that, and Session already falls back to the 4:2:0 default when 4:4:4 cannot be
    // negotiated and the bitrate was never touched.
    if (ov.hasYuv444)      p->enableYUV444 = ov.enableYuv444;
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
    if (ov.hasMatchLink)   p->matchHostLinkSpeed = ov.matchLinkSpeed;
    if (ov.hasWaitForGame) p->waitForGameOnScreen = ov.waitForGame;
    // No migration to do here, unlike frame pacing above: this key is new in 5.2.1 and
    // deliberately does not read the `refreshrate` one that 5.1.0 - 5.1.3 profiles could
    // hold. That was a four-value enum for a setting that no longer exists, and Session
    // decides on its own whether exclusive fullscreen makes this actionable at all.
    if (ov.hasDisplayMode) p->windowMode = (StreamingPreferences::WindowMode)ov.windowMode;
    if (ov.hasVsync)       p->enableVsync = ov.enableVsync;
    // No condition applied here on purpose, in step with frame pacing above: the profile
    // keeps the choice it was given, and the renderer is the one place that decides whether
    // it is actionable — it re-gates on V-Sync, frame pacing, tearing and the refresh ratio
    // before it will use a sync interval at all. Collapsing it here would only mean two
    // places had to agree.
    if (ov.hasFractionalVsync) p->fractionalVsync = ov.fractionalVsync;
    if (ov.hasVrr) p->enableVrr = ov.enableVrr;
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
    return std::max(0, c);
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
    // 8.8.0 (#53): in the order the host's sort mode gives, the one Profiles and Home's list show.
    QVector<int> ring;
    ring.append(-1);
    ring.append(order(uuid));
    const int n = (int)ring.size();
    int pos = (int)ring.indexOf(active(uuid));
    if (pos < 0) pos = 0;
    pos = ((pos + dir) % n + n) % n;
    setActive(uuid, ring[pos]);
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
    // Preserve the name and the creation number (8.8.0); rewrite only the override fields.
    QString nm = name(uuid, slot);
    const int cr = created(uuid, slot);
    QSettings s;
    s.remove(pgrp(uuid, slot));
    s.beginGroup(pgrp(uuid, slot));
    s.setValue("name", nm);
    s.setValue("created", cr);
    writeOverrideGroup(s, ov);
    s.endGroup();
}

int HostProfileManager::created(const QString& uuid, int slot) const
{
    QSettings s;
    return s.value(pgrp(uuid, slot) + "/created", slot).toInt();
}

int HostProfileManager::sortMode(const QString& uuid) const
{
    QSettings s;
    const int m = s.value(grp(uuid) + "/sort", SortCreated).toInt();
    return (m >= SortAlphabetical && m <= SortCustom) ? m : SortCreated;
}

void HostProfileManager::setSortMode(const QString& uuid, int mode)
{
    if (mode < SortAlphabetical || mode > SortCustom) return;
    QSettings s;
    s.setValue(grp(uuid) + "/sort", mode);
}

QVector<int> HostProfileManager::order(const QString& uuid) const
{
    const int c = count(uuid);
    QVector<int> out;
    for (int i = 0; i < c; i++) out.append(i);
    const int mode = sortMode(uuid);
    if (mode == SortAlphabetical) {
        QVector<QString> names;
        for (int i = 0; i < c; i++) names.append(name(uuid, i));
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
            return QString::localeAwareCompare(names[a], names[b]) < 0;
        });
    }
    else if (mode == SortCreated) {
        QVector<int> cr;
        for (int i = 0; i < c; i++) cr.append(created(uuid, i));
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return cr[a] < cr[b]; });
    }
    return out;
}

int HostProfileManager::add(const QString& uuid)
{
    // No ceiling since 8.0.0 (#42): a handheld docked in three places, with glasses and two
    // Internet variants, needed more than the three there used to be.
    int c = count(uuid);
    int slot = c;
    // 8.8.0: the creation number comes after every existing one (whatever their slots are now).
    int next = 0;
    for (int i = 0; i < c; i++) next = std::max(next, created(uuid, i) + 1);
    QSettings settings;
    settings.setValue(grp(uuid) + "/count", c + 1);
    settings.setValue(pgrp(uuid, slot) + "/name",
                      QStringLiteral("Profile ") + QString::number(slot + 1));
    settings.setValue(pgrp(uuid, slot) + "/created", next);
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

    // Snapshot remaining profiles, then renumber from scratch. 8.8.0: with their creation
    // numbers, and the host's sort mode, which the wipe below would take with it.
    struct P { QString name; AppOverride ov; int created; };
    QVector<P> remaining;
    for (int i = 0; i < c; i++) {
        if (i == slot) continue;
        remaining.append({ name(uuid, i), settings(uuid, i), created(uuid, i) });
    }
    const int sort = sortMode(uuid);

    int oldActive = active(uuid);   // -1 == OFF (preserved below)
    int newActive = oldActive;
    if (oldActive == slot)      newActive = 0;
    else if (oldActive > slot)  newActive = oldActive - 1;

    QSettings s;
    s.remove(grp(uuid));   // wipe the whole host group, then rewrite

    const int remCount = (int)remaining.size();
    s.setValue(grp(uuid) + "/count", remCount);
    s.setValue(grp(uuid) + "/sort", sort);
    if (remCount > 0) {
        s.setValue(grp(uuid) + "/active",
                   newActive < 0 ? -1 : std::max(0, std::min(newActive, remCount - 1)));
    }
    for (int i = 0; i < remCount; i++) {
        s.beginGroup(pgrp(uuid, i));
        s.setValue("name", remaining[i].name);
        s.setValue("created", remaining[i].created);
        writeOverrideGroup(s, remaining[i].ov);
        s.endGroup();
    }
}

void HostProfileManager::move(const QString& uuid, int from, int to)
{
    const int c = count(uuid);
    if (from < 0 || from >= c || to < 0 || to >= c || from == to) {
        return;
    }

    // Snapshot, reorder, rewrite — the same way remove() renumbers. Nothing outside this group
    // keeps a slot number (per-game overrides and the PyroWave Test go through active()), so
    // rewriting the slots is all a move needs.
    struct P { QString name; AppOverride ov; int created; };
    QVector<P> all;
    for (int i = 0; i < c; i++) {
        all.append({ name(uuid, i), settings(uuid, i), created(uuid, i) });
    }
    const int sort = sortMode(uuid);

    // The active profile follows itself, not its old slot number.
    const int oldActive = active(uuid);   // -1 == Global, kept as is
    QVector<int> perm;
    for (int i = 0; i < c; i++) perm.append(i);
    perm.move(from, to);
    const int newActive = oldActive < 0 ? -1 : (int)perm.indexOf(oldActive);

    QSettings s;
    s.remove(grp(uuid));
    s.setValue(grp(uuid) + "/count", c);
    s.setValue(grp(uuid) + "/sort", sort);
    s.setValue(grp(uuid) + "/active", newActive);
    for (int i = 0; i < c; i++) {
        s.beginGroup(pgrp(uuid, i));
        s.setValue("name", all[perm[i]].name);
        s.setValue("created", all[perm[i]].created);
        writeOverrideGroup(s, all[perm[i]].ov);
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
