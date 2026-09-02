#include "streamsettingsoverlay.h"
#include "streaming/session.h"
#include "streaming/video/overlaymanager.h"
#include "settings/appsettings.h"
#include "settings/videooptions.h"
#include "backend/nvcomputer.h"
#include "backend/nvapp.h"

#include <QByteArray>

#include <algorithm>

// The resolution and frame-rate tables live in settings/videooptions.h since 5.5.0 and are
// read in open(). The Custom entry is not one of them: it is always the slot one past the
// last real resolution — see resCustomIndex().

// Labels match Settings → Video. Two values since 5.2.0 removed the hardware cadence.
static const char* const s_PacingLabels[] = { "Off", "On" };
static const StreamingPreferences::FramePacingMode s_PacingValues[] = {
    StreamingPreferences::FP_OFF, StreamingPreferences::FP_ON
};
static const int s_PacingCount = 2;

static const int kCustomMin = 256, kCustomMax = 7680, kCustomStep = 8;
static const int kBitrateMinKbps = 5000, kBitrateStepKbps = 5000;

StreamSettingsOverlay::StreamSettingsOverlay(StreamingPreferences* prefs)
    : m_Prefs(prefs)
{
}

bool StreamSettingsOverlay::isCustom() const
{
    return m_ResIndex == resCustomIndex();
}

int StreamSettingsOverlay::effectiveWidth() const
{
    return isCustom() ? m_CustomW : m_ResValues.value(m_ResIndex).width();
}

int StreamSettingsOverlay::effectiveHeight() const
{
    return isCustom() ? m_CustomH : m_ResValues.value(m_ResIndex).height();
}

int StreamSettingsOverlay::changeCount() const
{
    int n = 0;
    if (effectiveWidth() != m_BaseW || effectiveHeight() != m_BaseH) n++;
    if (currentFps() != m_BaseFps) n++;
    if (m_BitrateKbps != m_BaseKbps) n++;
    if (m_Hdr != m_BaseHdr) n++;
    if (static_cast<int>(s_PacingValues[m_PacingIndex]) != m_BasePacing) n++;
    return n;
}

void StreamSettingsOverlay::rebuildRows()
{
    m_Rows.clear();
    m_Rows.append(ROW_RES);
    if (isCustom()) {
        m_Rows.append(ROW_CUSTOM_W);
        m_Rows.append(ROW_CUSTOM_H);
    }
    m_Rows.append(ROW_FPS);
    m_Rows.append(ROW_BITRATE);
    m_Rows.append(ROW_HDR);
    m_Rows.append(ROW_PACING);
    if (m_Focus >= m_Rows.size()) m_Focus = m_Rows.size() - 1;
    if (m_Focus < 0) m_Focus = 0;

    // Leaving Custom drops two rows, so the clamp above can land the cursor on
    // the last row — which may be the locked pacing row. Walk back to a usable one.
    while (m_Focus > 0 && isRowLocked(m_Rows[m_Focus])) m_Focus--;
}

void StreamSettingsOverlay::open()
{
    // What this panel offers, read here so it matches what Settings offered before the
    // session started — including this machine's own display values.
    m_ResValues = VideoOptions::resolutions();
    m_FpsValues = VideoOptions::frameRates();

    // Seed the model from the current effective preferences.
    int idx = -1;
    for (int i = 0; i < m_ResValues.count(); i++) {
        if (m_ResValues[i].width() == m_Prefs->width && m_ResValues[i].height() == m_Prefs->height) {
            idx = i;
            break;
        }
    }
    m_CustomW = m_Prefs->width;
    m_CustomH = m_Prefs->height;
    m_ResIndex = (idx >= 0) ? idx : resCustomIndex();

    /*
     * ⚠️ A frame rate that is not on the list used to fall back to the LAST entry, and the
     * baseline was then read from that fallback — so opening the panel at 165 fps showed
     * 120, called it "no changes", and wrote 120 into the profile the moment anything else
     * on the panel was applied. A setting the user never touched, silently downgraded.
     *
     * It could only happen to a value inherited from the store we shared with Moonlight,
     * which is why it went unseen; 5.5.0 hands out such values deliberately, so the fallback
     * had to go. The rate is now appended to the list instead: unknown, but never lost, and
     * the row shows what is actually running.
     */
    m_FpsIndex = m_FpsValues.indexOf(m_Prefs->fps);
    if (m_FpsIndex < 0) {
        m_FpsValues.append(m_Prefs->fps);
        std::sort(m_FpsValues.begin(), m_FpsValues.end());
        m_FpsIndex = m_FpsValues.indexOf(m_Prefs->fps);
    }

    m_BitrateKbps = m_Prefs->bitrateKbps;
    m_Hdr = m_Prefs->enableHdr;

    m_PacingIndex = 0;
    for (int i = 0; i < s_PacingCount; i++) {
        if (s_PacingValues[i] == m_Prefs->framePacingMode) { m_PacingIndex = i; break; }
    }

    // Baseline = the values we'd apply right now (so opening shows "no changes").
    m_BaseW = effectiveWidth();
    m_BaseH = effectiveHeight();
    m_BaseFps = currentFps();
    m_BaseKbps = m_BitrateKbps;
    m_BaseHdr = m_Hdr;
    m_BasePacing = static_cast<int>(s_PacingValues[m_PacingIndex]);

    m_Focus = 0;
    rebuildRows();

    m_Active = true;
    Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayStreamSettings, true);
    render();
}

void StreamSettingsOverlay::close()
{
    if (!m_Active) {
        return;
    }
    m_Active = false;
    Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayStreamSettings, false);
}

bool StreamSettingsOverlay::isRowLocked(int rowId) const
{
    // Frame pacing does nothing without V-Sync — Session passes FP_OFF to the
    // decoder whenever V-Sync is disabled, and the software Pacer is gated on it. V-Sync is a global preference
    // that can't be changed from here, so the row is shown read-only instead of
    // advertising a mode that would be silently ignored.
    return rowId == ROW_PACING && !m_Prefs->enableVsync;
}

void StreamSettingsOverlay::moveFocus(int delta)
{
    const int n = m_Rows.size();
    if (n <= 0) return;

    // Skip locked rows. Bounded by the row count so an all-locked list (which
    // can't happen today) can't spin.
    for (int i = 0; i < n; i++) {
        m_Focus = (m_Focus + delta + n) % n;
        if (!isRowLocked(m_Rows[m_Focus])) break;
    }
    render();
}

void StreamSettingsOverlay::navUp()
{
    if (!m_Active) return;
    moveFocus(-1);
}

void StreamSettingsOverlay::navDown()
{
    if (!m_Active) return;
    moveFocus(1);
}

void StreamSettingsOverlay::navLeft()
{
    if (!m_Active) return;
    changeFocused(-1);
}

void StreamSettingsOverlay::navRight()
{
    if (!m_Active) return;
    changeFocused(1);
}

static int clampEven(int v)
{
    v -= (v % 2);
    if (v < kCustomMin) v = kCustomMin;
    if (v > kCustomMax) v = kCustomMax;
    return v;
}

void StreamSettingsOverlay::changeFocused(int delta)
{
    int row = m_Rows[m_Focus];
    if (isRowLocked(row)) return;

    switch (row) {
    case ROW_RES:
        m_ResIndex = (m_ResIndex + delta + resOptionCount()) % resOptionCount();
        rebuildRows();
        break;
    case ROW_CUSTOM_W:
        m_CustomW = clampEven(m_CustomW + delta * kCustomStep);
        break;
    case ROW_CUSTOM_H:
        m_CustomH = clampEven(m_CustomH + delta * kCustomStep);
        break;
    case ROW_FPS:
        m_FpsIndex = (m_FpsIndex + delta + m_FpsValues.count()) % m_FpsValues.count();
        break;
    case ROW_BITRATE: {
        int maxKbps = m_Prefs->unlockBitrate ? 500000 : 150000;
        m_BitrateKbps += delta * kBitrateStepKbps;
        if (m_BitrateKbps < kBitrateMinKbps) m_BitrateKbps = kBitrateMinKbps;
        if (m_BitrateKbps > maxKbps) m_BitrateKbps = maxKbps;
        break;
    }
    case ROW_HDR:
        m_Hdr = !m_Hdr;
        break;
    case ROW_PACING:
        m_PacingIndex = (m_PacingIndex + delta + s_PacingCount) % s_PacingCount;
        break;
    }
    render();
}

void StreamSettingsOverlay::apply()
{
    if (!m_Active) return;

    if (changeCount() > 0) {
        // Keep the current frame pacing; only res/fps/bitrate/HDR change here.
        //
        // Do NOT "simplify" this to FP_OFF when V-Sync is off. The value passed
        // here lands in the cloned prefs of the resumed session, which is what
        // seeds this overlay next time it opens — and Save (Y) writes whatever
        // is seeded straight into the per-game override / host profile / global.
        // Passing FP_OFF would therefore erase the user's stored mode the first
        // time they save anything else (e.g. a bitrate change), permanently and
        // silently. Round-tripping their own value keeps that Save a no-op.
        // The decoder ignores the mode without V-Sync anyway (chooseDecoder
        // forces FP_OFF), and the locked row already displays "Off", so nothing
        // here is misreported.
        Session::get()->requestReconfigure(effectiveWidth(), effectiveHeight(),
                                           currentFps(), m_BitrateKbps,
                                           m_Hdr, static_cast<int>(s_PacingValues[m_PacingIndex]));
    }
    close();
}

void StreamSettingsOverlay::cancel()
{
    close();
}

StreamSettingsOverlay::SaveTarget StreamSettingsOverlay::activeTarget(int* hostSlot) const
{
    NvComputer* c = Session::get()->getComputer();
    const NvApp& a = Session::get()->getApp();
    if (c != nullptr) {
        if (AppSettingsManager::get()->hasOverride(c->uuid, a.id)) {
            return TARGET_GAME;
        }
        int slot = HostProfileManager::get()->active(c->uuid);
        if (slot >= 0) {
            if (hostSlot) *hostSlot = slot;
            return TARGET_HOST;
        }
    }
    return TARGET_GLOBAL;
}

static QString truncName(const QString& s)
{
    return s.length() > 18 ? (s.left(17) + QStringLiteral("…")) : s;
}

QString StreamSettingsOverlay::targetLabel() const
{
    int slot = -1;
    SaveTarget t = activeTarget(&slot);
    NvComputer* c = Session::get()->getComputer();
    const NvApp& a = Session::get()->getApp();
    switch (t) {
    case TARGET_GAME:
        return QStringLiteral("Game \"%1\"").arg(truncName(a.name));
    case TARGET_HOST:
        return QStringLiteral("Profile \"%1\" (%2)")
            .arg(truncName(HostProfileManager::get()->name(c->uuid, slot)))
            .arg(truncName(c->name));
    default:
        return QStringLiteral("Global");
    }
}

QString StreamSettingsOverlay::saveLabel() const
{
    // North button glyph (Nintendo's north position is labelled X).
    switch (m_Prefs->glyphSet) {
    case StreamingPreferences::GS_PLAYSTATION: return QStringLiteral("Triangle");
    case StreamingPreferences::GS_NINTENDO:    return QStringLiteral("X");
    default:                                   return QStringLiteral("Y");
    }
}

void StreamSettingsOverlay::writeCurrentTo(SaveTarget target, int hostSlot)
{
    const int w = effectiveWidth();
    const int h = effectiveHeight();
    const int fps = currentFps();
    const int kbps = m_BitrateKbps;
    const int pacing = static_cast<int>(s_PacingValues[m_PacingIndex]);
    const bool hdr = m_Hdr;

    if (target == TARGET_GLOBAL) {
        StreamingPreferences* g = StreamingPreferences::get();
        g->width = w;
        g->height = h;
        g->fps = fps;
        g->bitrateKbps = kbps;
        g->enableHdr = hdr;
        g->framePacingMode = static_cast<StreamingPreferences::FramePacingMode>(pacing);
        g->save();
        return;
    }

    NvComputer* c = Session::get()->getComputer();
    const NvApp& a = Session::get()->getApp();
    if (c == nullptr) {
        return;
    }

    // Read-modify-write so codec/audio/Hue overrides aren't clobbered.
    AppOverride ov = (target == TARGET_GAME)
        ? AppSettingsManager::get()->getOverride(c->uuid, a.id)
        : HostProfileManager::get()->settings(c->uuid, hostSlot);

    ov.hasResolution = true;  ov.width = w; ov.height = h;
    ov.hasFps = true;         ov.fps = fps;
    ov.hasBitrate = true;     ov.bitrateKbps = kbps;
    ov.hasHdr = true;         ov.enableHdr = hdr;
    ov.hasFramePacing = true; ov.framePacingMode = pacing;

    if (target == TARGET_GAME) {
        AppSettingsManager::get()->setOverride(c->uuid, a.id, ov);
    } else {
        HostProfileManager::get()->setSettings(c->uuid, hostSlot, ov);
    }
}

void StreamSettingsOverlay::save()
{
    if (!m_Active) return;

    int slot = -1;
    SaveTarget t = activeTarget(&slot);
    writeCurrentTo(t, slot);

    // Persist + apply: apply() reconnects only if values differ from the running
    // session, then closes the overlay.
    apply();
}

QString StreamSettingsOverlay::confirmLabel() const
{
    // The confirm action is the SDL "south" button; the glyph printed on it
    // differs per vendor (Nintendo swaps A/B vs Xbox position).
    switch (m_Prefs->glyphSet) {
    case StreamingPreferences::GS_PLAYSTATION: return QStringLiteral("Cross");
    case StreamingPreferences::GS_NINTENDO:    return QStringLiteral("B");
    default:                                   return QStringLiteral("A");
    }
}

QString StreamSettingsOverlay::cancelLabel() const
{
    // The cancel action is the SDL "east" button.
    switch (m_Prefs->glyphSet) {
    case StreamingPreferences::GS_PLAYSTATION: return QStringLiteral("Circle");
    case StreamingPreferences::GS_NINTENDO:    return QStringLiteral("A");
    default:                                   return QStringLiteral("B");
    }
}

static QString aspectRatio(int w, int h)
{
    if (h <= 0) return QString();
    struct { double r; const char* n; } common[] = {
        { 16.0/9, "16:9" }, { 16.0/10, "16:10" }, { 21.0/9, "21:9" },
        { 4.0/3, "4:3" }, { 3.0/2, "3:2" }, { 1.0, "1:1" }
    };
    double r = (double)w / h;
    for (auto& c : common) {
        if (qAbs(r - c.r) < 0.02) return QString::fromLatin1(c.n);
    }
    int a = w, b = h;
    while (b) { int t = a % b; a = b; b = t; }
    if (a <= 0) a = 1;
    return QStringLiteral("%1:%2").arg(w / a).arg(h / a);
}

// Resource path of the self-contained vendor button glyph for an SDL position
// ('s' = south/confirm, 'e' = east/cancel, 'n' = north/save). GS_AUTO follows
// the Xbox layout, matching the rest of the overlay's existing label defaults.
static QString padIcon(StreamingPreferences::GlyphSet gs, char pos)
{
    switch (gs) {
    case StreamingPreferences::GS_PLAYSTATION:
        return pos == 's' ? QStringLiteral(":/res/pad_ps_cross.svg")
             : pos == 'e' ? QStringLiteral(":/res/pad_ps_circle.svg")
             :              QStringLiteral(":/res/pad_ps_triangle.svg");
    case StreamingPreferences::GS_NINTENDO:
        return pos == 's' ? QStringLiteral(":/res/pad_switch_b.svg")
             : pos == 'e' ? QStringLiteral(":/res/pad_switch_a.svg")
             :              QStringLiteral(":/res/pad_switch_x.svg");
    default: // Xbox / Auto
        return pos == 's' ? QStringLiteral(":/res/pad_xbox_a.svg")
             : pos == 'e' ? QStringLiteral(":/res/pad_xbox_b.svg")
             :              QStringLiteral(":/res/pad_xbox_y.svg");
    }
}

void StreamSettingsOverlay::render()
{
    if (!m_Active) return;

    Overlay::OverlayPanel panel;
    panel.title = QStringLiteral("STREAM SETTINGS");

    // Header chip: the active save target (where a LIVE badge would otherwise go),
    // tinted by layer — game (amber) / host profile (cyan) / global (grey).
    int slot = -1;
    SaveTarget target = activeTarget(&slot);
    panel.badgeText = targetLabel();
    switch (target) {
    case TARGET_GAME: panel.badgeColor = {0xF5, 0x9E, 0x0B, 0xFF}; break;
    case TARGET_HOST: panel.badgeColor = {0x26, 0xC6, 0xDA, 0xFF}; break;
    default:          panel.badgeColor = {0x9A, 0xA0, 0xA6, 0xFF}; break;
    }

    auto addRow = [&](int rowId, const QString& label, const QString& value,
                      const QString& hint = QString(), bool indent = false) {
        const bool locked = isRowLocked(rowId);
        Overlay::OverlayRow r;
        r.label = label;
        r.value = value;
        r.hint = hint;
        // A locked row can't be changed, so it gets no ‹ › affordance and is
        // never drawn as the selection (navigation skips it).
        r.showArrows = !locked;
        r.dimmed = locked;
        r.indent = indent;
        r.selected = !locked && (m_Rows[m_Focus] == rowId);
        panel.rows.append(r);
    };

    for (int rowId : m_Rows) {
        switch (rowId) {
        case ROW_RES: {
            QString v = isCustom()
                ? QStringLiteral("%1x%2").arg(m_CustomW).arg(m_CustomH)
                : QStringLiteral("%1x%2").arg(m_ResValues[m_ResIndex].width()).arg(m_ResValues[m_ResIndex].height());
            // The hint carries what the Settings strip says with its dot: this value is
            // the client display's own. Same information in both places, because the
            // lists are now the same list.
            QString hint = isCustom() ? QStringLiteral("custom")
                         : VideoOptions::isNativeResolution(m_ResValues[m_ResIndex])
                           ? QStringLiteral("this display") : QString();
            addRow(ROW_RES, QStringLiteral("Resolution"), v, hint);
            break;
        }
        case ROW_CUSTOM_W:
            addRow(ROW_CUSTOM_W, QStringLiteral("Width"),
                   QStringLiteral("%1 px").arg(m_CustomW), QString(), true);
            break;
        case ROW_CUSTOM_H:
            addRow(ROW_CUSTOM_H, QStringLiteral("Height"),
                   QStringLiteral("%1 px").arg(m_CustomH),
                   aspectRatio(m_CustomW, m_CustomH), true);
            break;
        case ROW_FPS:
            addRow(ROW_FPS, QStringLiteral("Frame rate"),
                   QStringLiteral("%1 fps").arg(currentFps()),
                   VideoOptions::isNativeFrameRate(currentFps())
                       ? QStringLiteral("this display") : QString());
            break;
        case ROW_BITRATE:
            addRow(ROW_BITRATE, QStringLiteral("Bitrate"),
                   QStringLiteral("%1 Mbps").arg(qRound(m_BitrateKbps / 1000.0)));
            break;
        case ROW_HDR:
            addRow(ROW_HDR, QStringLiteral("HDR"),
                   m_Hdr ? QStringLiteral("On") : QStringLiteral("Off"));
            break;
        case ROW_PACING:
            if (isRowLocked(ROW_PACING)) {
                addRow(ROW_PACING, QStringLiteral("Frame pacing"),
                       QStringLiteral("Off"), QStringLiteral("V-Sync off"));
            }
            else {
                addRow(ROW_PACING, QStringLiteral("Frame pacing"),
                       QString::fromLatin1(s_PacingLabels[m_PacingIndex]));
            }
            break;
        }
    }

    int n = changeCount();
    if (n > 0) {
        panel.statusLine = QStringLiteral("%1 change%2 · Apply briefly reconnects")
            .arg(n).arg(n > 1 ? QStringLiteral("s") : QString());
        panel.statusColor = {0xF5, 0x9E, 0x0B, 0xFF};
    } else {
        panel.statusLine = QStringLiteral("No changes");
        panel.statusColor = {0x80, 0x80, 0x80, 0xFF};
    }

    // Footer: vendor-accurate controller button glyphs.
    Overlay::OverlayBadge apply, cancel, saveBadge;
    apply.iconPath  = padIcon(m_Prefs->glyphSet, 's'); apply.label  = QStringLiteral("Apply");
    cancel.iconPath = padIcon(m_Prefs->glyphSet, 'e'); cancel.label = QStringLiteral("Cancel");
    saveBadge.iconPath = padIcon(m_Prefs->glyphSet, 'n'); saveBadge.label = QStringLiteral("Save");
    panel.footerBadges.append(apply);
    panel.footerBadges.append(cancel);
    panel.footerBadges.append(saveBadge);

    Session::get()->getOverlayManager().updateOverlayPanel(Overlay::OverlayStreamSettings, panel);
}
