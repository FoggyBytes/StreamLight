#include "theme.h"
#include "backend/windowslocation.h"
#include "backend/uianimationdriver.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#include <QVariantMap>

#include <cmath>

namespace
{
    // "Ion" — the cyan, and the default since 5.0.0.
    //
    // It replaced StreamLight's own green (#00e676, still available as the "Signal" preset)
    // for one reason: that green was doing two jobs at once. It meant "you have the focus
    // here" AND it is within a shade of the green that means "this host is online" — and a
    // colour that says two things says neither. The cyan sits beside the online green without
    // being mistaken for it, which is exactly what an accent has to do, since the semantic
    // colours deliberately never follow it.
    const QColor DefaultAccent = QColor(0x00, 0xd3, 0xf2);

    // The Dark theme's accent. The key every version up to 6.5 wrote, kept so that a rollback
    // still finds an accent; the Light theme's has a key of its own (13/10/2026).
    const char* SER_ACCENT = "theme/accent";
    const char* SER_ACCENT_LIGHT = "theme/accentlight";
    const char* SER_AUTOHOW = "theme/autohow";
    // The theme Auto last showed, so the next launch opens on it while Windows looks for the
    // position (13/10/2026). The position itself is never stored.
    const char* SER_LASTLIGHT = "theme/lastlight";
    const char* SER_LIGHTFROM = "theme/lightfrom";
    const char* SER_DARKFROM = "theme/darkfrom";
    const char* SER_FORCED = "theme/forced";
    const char* SER_FORCEDUNTIL = "theme/forceduntil";
    const char* SER_REDUCE = "theme/reduceanimations";
    const char* SER_UNLOCKFPS = "theme/unlockframerate";
    const char* SER_STARTUP = "theme/startupanimation";
    const char* SER_HOSTCOVERS = "theme/hostpagecovers";
    const char* SER_MODE = "theme/mode";
    const char* SER_SCALE = "theme/scalepercent";
    const char* SER_LIBRARY_GRID = "theme/librarygrid";
    const char* SER_LIBRARY_COVER_SIZE = "theme/librarycoversize";
    const char* SER_LIBRARY_SHOW_TITLES = "theme/libraryshowtitles";

    /**
     * Perceived brightness, not the average of the channels: green reads far lighter than blue
     * at the same numeric value, and an average would put dark text on a blue accent nobody can
     * read against. Rec. 709 coefficients on linearised channels — the same maths the contrast
     * guidelines use.
     */
    qreal relativeLuminance(const QColor& c)
    {
        auto lin = [](qreal v) {
            return v <= 0.03928 ? v / 12.92 : qPow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
    }

    /**
     * Sunrise and sunset on @p day at @p lat / @p lon (degrees, north and east positive), as ms
     * since the epoch. The sunrise equation with the usual refraction and disc correction
     * (-0.833°), good to a minute or two away from the poles. Returns 0 with both times set,
     * 1 when the sun does not set that day, -1 when it does not rise.
     */
    int sunEvents(const QDate& day, double lat, double lon, qint64* rise, qint64* set)
    {
        const double rad = 3.14159265358979323846 / 180.0;
        const double n   = double(day.toJulianDay() - 2451545);
        const double js  = n - lon / 360.0;                                  // mean solar noon
        const double m   = std::fmod(357.5291 + 0.98560028 * js, 360.0);      // mean anomaly
        const double c   = 1.9148 * std::sin(m * rad) + 0.02 * std::sin(2 * m * rad)
                         + 0.0003 * std::sin(3 * m * rad);                    // equation of centre
        const double l   = std::fmod(m + c + 180.0 + 102.9372, 360.0);       // ecliptic longitude
        const double jt  = 2451545.0 + js + 0.0053 * std::sin(m * rad) - 0.0069 * std::sin(2 * l * rad);
        const double dec = std::asin(std::sin(l * rad) * std::sin(23.4397 * rad));
        const double cw  = (std::sin(-0.833 * rad) - std::sin(lat * rad) * std::sin(dec))
                         / (std::cos(lat * rad) * std::cos(dec));
        if (cw > 1.0)  return -1;
        if (cw < -1.0) return 1;
        const double w = std::acos(cw) / rad;
        auto ms = [](double j) { return qint64((j - 2440587.5) * 86400000.0); };
        *rise = ms(jt - w / 360.0);
        *set  = ms(jt + w / 360.0);
        return 0;
    }
}


Theme::Theme(QObject* parent)
    : QObject(parent)
{
    QSettings settings;
    m_AccentDark = QColor(settings.value(SER_ACCENT, DefaultAccent.name()).toString());
    if (!m_AccentDark.isValid()) {
        m_AccentDark = DefaultAccent;
    }
    // Each theme keeps its own accent (13/10/2026). Until Light has one, it starts from Dark's:
    // an update must not change the colour of an app that was already set up.
    m_AccentLight = QColor(settings.value(SER_ACCENT_LIGHT, m_AccentDark.name()).toString());
    if (!m_AccentLight.isValid()) {
        m_AccentLight = m_AccentDark;
    }
    m_ReduceAnimations = settings.value(SER_REDUCE, false).toBool();
    m_UnlockFrameRate = settings.value(SER_UNLOCKFPS, false).toBool();
    // Here rather than in main(): Theme is built by the QML engine when the first page asks for
    // it, before any animation runs, and building it earlier would change who owns it.
    UiAnimationDriver::apply(m_UnlockFrameRate);
    m_StartupAnimation = settings.value(SER_STARTUP, true).toBool();
    m_LibraryGrid = settings.value(SER_LIBRARY_GRID, false).toBool();
    m_LibraryCoverSize = qBound(0, settings.value(SER_LIBRARY_COVER_SIZE, 1).toInt(), 2);
    m_LibraryShowTitles = settings.value(SER_LIBRARY_SHOW_TITLES, true).toBool();
    // The waves by default: what 7.0.0 introduced. The covers are 6.5.1's look, on request.
    m_HostPageCovers = settings.value(SER_HOSTCOVERS, false).toBool();
    // Dark by default: it is what every install had up to 6.5, and a theme that changes on its
    // own after an update would look like a bug.
    m_Mode = qBound(int(ModeDark), settings.value(SER_MODE, int(ModeDark)).toInt(), int(ModeAuto));
    // 90-110: the three steps Settings offers since 09/10/2026. A value saved under the wider
    // range (80-150) lands on the nearest end, where there is a pill for it.
    m_ScalePercent = qBound(90, settings.value(SER_SCALE, 100).toInt(), 110);

    m_AutoHow   = qBound(0, settings.value(SER_AUTOHOW, 0).toInt(), 1);
    m_LastLight = settings.value(SER_LASTLIGHT, false).toBool();
    m_LightFrom = qBound(0, settings.value(SER_LIGHTFROM, 7 * 60).toInt(), 24 * 60 - 1);
    m_DarkFrom  = qBound(0, settings.value(SER_DARKFROM, 19 * 60).toInt(), 24 * 60 - 1);
    m_Forced    = qBound(-1, settings.value(SER_FORCED, -1).toInt(), 1);
    m_ForcedUntil = settings.value(SER_FORCEDUNTIL, 0).toLongLong();
    // A hold only makes sense under the mode it was made in; under System it ended with the
    // session that made it (Windows may have changed scheme since), under Auto at its time.
    if (m_Mode != ModeAuto || m_ForcedUntil <= QDateTime::currentMSecsSinceEpoch()) {
        m_Forced = -1;
        m_ForcedUntil = 0;
    }

    // Under Auto with the sun, the position is asked for at once (refreshSun does it on the
    // first day it sees); until Windows answers, scheduleLight() keeps m_LastLight.
    refreshSun();
    m_Light = computeLight();
    m_Accent = m_Light ? m_AccentLight : m_AccentDark;

    // System follows Windows while the app is open, not only at launch — and a change of
    // Windows' scheme is "the next change" that ends the Home toggle's hold.
    if (qGuiApp != nullptr) {
        connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
            if (m_Mode == ModeSystem) {
                if (m_Forced >= 0) {
                    clearForce();
                }
                reevaluate(true);
            }
        });
    }

    // Auto has nothing to notify it: the clock is checked twice a minute, which also catches
    // a return from sleep within half a minute. Cheap — a comparison, and the sun once a day.
    m_Clock.setInterval(30 * 1000);
    connect(&m_Clock, &QTimer::timeout, this, [this]() { reevaluate(); });
    m_Clock.start();
}

// ── The automatic switch ──────────────────────────────────────────────────────────

bool Theme::systemLight() const
{
    return qGuiApp != nullptr && qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Light;
}

bool Theme::computeLight() const
{
    switch (m_Mode) {
    case ModeLight:
        return true;
    case ModeSystem:
        return m_Forced >= 0 ? m_Forced == 1 : systemLight();
    case ModeAuto:
        return m_Forced >= 0 ? m_Forced == 1 : scheduleLight(QDateTime::currentMSecsSinceEpoch());
    default:
        return false;
    }
}

namespace
{
    bool inWindow(int minute, int from, int to)
    {
        // [from, to), wrapping past midnight when from > to. Equal ends: never Light.
        return from <= to ? (minute >= from && minute < to) : (minute >= from || minute < to);
    }
}

int Theme::effectiveAutoHow() const
{
    // Sunrise and sunset need the position: without it Auto runs on the set hours, and the
    // choice is kept for when Windows has a position again.
    return m_AutoHow == 0 && m_LocState == LocUnavailable ? 1 : m_AutoHow;
}

bool Theme::scheduleLight(qint64 nowMs) const
{
    if (effectiveAutoHow() == 1) {
        const QTime t = QDateTime::fromMSecsSinceEpoch(nowMs).time();
        return inWindow(t.hour() * 60 + t.minute(), m_LightFrom, m_DarkFrom);
    }

    // Windows has not answered yet (the first seconds after launch): keep the theme the app
    // last showed rather than flash the hours' choice and then the sun's.
    if (m_LocState != LocAvailable) {
        return m_LastLight;
    }

    const QDate day = QDateTime::fromMSecsSinceEpoch(nowMs).date();
    qint64 rise = 0, set = 0;
    const int st = sunEvents(day, m_Lat, m_Lon, &rise, &set);
    if (st != 0) {
        return st > 0;   // the sun never sets (Light) or never rises (Dark) here today
    }
    return nowMs >= rise && nowMs < set;
}

qint64 Theme::nextTransition(qint64 nowMs) const
{
    qint64 best = 0;
    auto consider = [&](qint64 t) { if (t > nowMs && (best == 0 || t < best)) best = t; };
    const QDate today = QDateTime::fromMSecsSinceEpoch(nowMs).date();

    if (effectiveAutoHow() == 1) {
        for (int d = 0; d < 2; d++) {
            for (int m : { m_LightFrom, m_DarkFrom }) {
                consider(QDateTime(today.addDays(d), QTime(m / 60, m % 60)).toMSecsSinceEpoch());
            }
        }
    }
    else if (m_LocState == LocAvailable) {
        for (int d = 0; d < 3; d++) {
            qint64 rise = 0, set = 0;
            if (sunEvents(today.addDays(d), m_Lat, m_Lon, &rise, &set) == 0) {
                consider(rise);
                consider(set);
            }
        }
    }

    // Polar day or night, or no position yet: hold for a while, then look again.
    return best != 0 ? best : nowMs + (m_LocState == LocAvailable ? 24LL : 1LL) * 3600 * 1000;
}

void Theme::refreshSun()
{
    const QDate today = QDate::currentDate();
    if (today != m_SunDate && m_Mode == ModeAuto) {
        // A new day (or the first look): the position is asked again too, at most this often —
        // a laptop may have moved, and location may have been switched on or off.
        askLocation(false);
    }
    if (today == m_SunDate) {
        return;
    }

    m_SunDate = today;
    int rise = -1, set = -1;
    qint64 riseMs = 0, setMs = 0;
    if (m_LocState == LocAvailable && sunEvents(today, m_Lat, m_Lon, &riseMs, &setMs) == 0) {
        const QTime r = QDateTime::fromMSecsSinceEpoch(riseMs).time();
        const QTime s = QDateTime::fromMSecsSinceEpoch(setMs).time();
        rise = r.hour() * 60 + r.minute();
        set  = s.hour() * 60 + s.minute();
    }
    if (rise != m_SunriseMin || set != m_SunsetMin) {
        m_SunriseMin = rise;
        m_SunsetMin = set;
        emit scheduleChanged();
    }
}

void Theme::askLocation(bool force)
{
    // One question at a time, and not more than once every ten minutes unless the user just
    // asked for Auto: Settings asks each time the Interface tab is shown.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_LocBusy || (!force && m_LocAskedAt != 0 && now - m_LocAskedAt < 10LL * 60 * 1000)) {
        return;
    }
    m_LocBusy = true;
    m_LocAskedAt = now;
    WindowsLocation::request([this](bool ok, double lat, double lon) {
        QMetaObject::invokeMethod(this, [this, ok, lat, lon]() { onLocation(ok, lat, lon); },
                                  Qt::QueuedConnection);
    });
}

void Theme::onLocation(bool ok, double lat, double lon)
{
    m_LocBusy = false;
    const int state = ok ? LocAvailable : LocUnavailable;
    // Kept in memory only: the position is never written anywhere.
    const bool moved = ok && (qAbs(lat - m_Lat) > 0.01 || qAbs(lon - m_Lon) > 0.01);
    if (state == m_LocState && !moved) {
        return;
    }
    m_LocState = state;
    if (ok) {
        m_Lat = lat;
        m_Lon = lon;
    }
    m_SunDate = QDate();   // recompute today's times for the new answer
    refreshSun();
    emit scheduleChanged();
    reevaluate();
}

void Theme::refreshLocation()
{
    if (m_Mode == ModeAuto) {
        askLocation(false);
    }
}

void Theme::reevaluate(bool notify)
{
    const bool wasForced = m_Forced >= 0;
    if (m_Forced >= 0 && m_Mode == ModeAuto && QDateTime::currentMSecsSinceEpoch() >= m_ForcedUntil) {
        clearForce();
    }
    refreshSun();

    // While a stream is up the clock does not repaint the app behind it (see hold); an
    // explicit choice (notify) still does.
    if (m_Hold && !notify) {
        if (wasForced != (m_Forced >= 0)) {
            emit changed();
        }
        return;
    }

    const bool l = computeLight();
    if (l != m_Light) {
        m_Light = l;
        m_Accent = l ? m_AccentLight : m_AccentDark;
        // The one thing remembered across launches for Auto: what was on screen, so the next
        // launch opens on it while Windows looks for the position.
        m_LastLight = l;
        QSettings().setValue(SER_LASTLIGHT, l);
        emit changed();
    }
    else if (notify || wasForced != (m_Forced >= 0)) {
        emit changed();
    }
}

void Theme::clearForce()
{
    m_Forced = -1;
    m_ForcedUntil = 0;
    save();
}

void Theme::toggleLight()
{
    const bool target = !m_Light;
    if (m_Mode == ModeDark || m_Mode == ModeLight) {
        setMode(target ? ModeLight : ModeDark);
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool natural = m_Mode == ModeSystem ? systemLight() : scheduleLight(now);
    if (target == natural) {
        m_Forced = -1;
        m_ForcedUntil = 0;
    }
    else {
        m_Forced = target ? 1 : 0;
        m_ForcedUntil = m_Mode == ModeAuto ? nextTransition(now) : 0;
    }
    save();
    reevaluate(true);
}

int Theme::forcedUntilMinutes() const
{
    if (m_Forced < 0 || m_Mode != ModeAuto || m_ForcedUntil <= 0) {
        return -1;
    }
    const QTime t = QDateTime::fromMSecsSinceEpoch(m_ForcedUntil).time();
    return t.hour() * 60 + t.minute();
}

void Theme::setHold(bool on)
{
    if (on == m_Hold) {
        return;
    }
    m_Hold = on;
    emit holdChanged();
    if (!on) {
        reevaluate();   // whatever changed while the stream was up, now
    }
}

void Theme::setAutoHow(int how)
{
    how = qBound(0, how, 1);
    if (how == m_AutoHow) {
        return;
    }
    m_AutoHow = how;
    if (m_Forced >= 0) {
        clearForce();   // a hold timed by the old rule would end at the wrong moment
    }
    save();
    if (how == 0) {
        askLocation(true);
    }
    emit scheduleChanged();
    reevaluate(true);
}

void Theme::setLightFrom(int minutes)
{
    minutes = ((minutes % 1440) + 1440) % 1440;
    if (minutes == m_LightFrom) {
        return;
    }
    m_LightFrom = minutes;
    if (m_Forced >= 0) {
        clearForce();
    }
    save();
    emit scheduleChanged();
    reevaluate(true);
}

void Theme::setDarkFrom(int minutes)
{
    minutes = ((minutes % 1440) + 1440) % 1440;
    if (minutes == m_DarkFrom) {
        return;
    }
    m_DarkFrom = minutes;
    if (m_Forced >= 0) {
        clearForce();
    }
    save();
    emit scheduleChanged();
    reevaluate(true);
}


QColor Theme::ink() const
{
    // 3:1 — the bar for a line, a ring, an underline.
    return light() ? darkenedAccent(3.0) : m_Accent;
}

QColor Theme::inkText() const
{
    // 4.5:1 — the bar for text this size.
    return light() ? darkenedAccent(4.5) : m_Accent;
}

QColor Theme::darkenedAccent(qreal ratio) const
{
    // Lower the lightness, keep hue and saturation, until the colour holds the ratio against
    // the card. Most accents need a few steps; the pale row needs many. A colour that already
    // holds is returned untouched.
    const qreal bg = relativeLuminance(card());
    float h, s, l, a;
    m_Accent.getHslF(&h, &s, &l, &a);
    QColor c = m_Accent;
    while (l > 0.05f) {
        const qreal fg = relativeLuminance(c);
        if ((qMax(fg, bg) + 0.05) / (qMin(fg, bg) + 0.05) >= ratio) {
            break;
        }
        l -= 0.01f;
        c = QColor::fromHslF(h < 0 ? 0 : h, s, l);
    }
    return c;
}

void Theme::setMode(int m)
{
    m = qBound(int(ModeDark), m, int(ModeAuto));
    if (m == m_Mode) {
        return;
    }

    // A hold belongs to the mode it was made in.
    m_Mode = m;
    m_Forced = -1;
    m_ForcedUntil = 0;
    save();
    if (m == ModeAuto) {
        askLocation(true);
    }
    reevaluate(true);
}

Theme* Theme::get(QQmlEngine* qmlEngine)
{
    static Theme* s_Theme = nullptr;
    if (s_Theme == nullptr) {
        s_Theme = new Theme(qmlEngine);
    }
    return s_Theme;
}

QColor Theme::accentFill() const
{
    return light() ? darkenedAccent(4.5) : m_Accent;
}

QColor Theme::onAccent() const
{
    // On the fill, not on the raw accent: in Light the fill is the deepened accent.
    return onColor(accentFill());
}

QColor Theme::onColor(const QColor& background) const
{
    // Whichever of near-black or white reads better, by contrast and not by a luminance
    // threshold: the old 0.42 cut put white on Rose (3.3:1) where near-black measures 6.3:1.
    // One function for every "what goes on this colour" in the app, so a button and the
    // label beside it can never disagree.
    const QColor dark(0x08, 0x12, 0x0c), white(0xff, 0xff, 0xff);
    return contrast(dark, background) >= contrast(white, background) ? dark : white;
}

qreal Theme::contrast(const QColor& a, const QColor& b) const
{
    const qreal la = relativeLuminance(a), lb = relativeLuminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

QColor Theme::ground() const
{
    // Near-black with a trace of the accent in it. This is the difference between an interface
    // that has been recoloured and one that looks designed around its colour: at 4% the hue is
    // never nameable, but the screen stops feeling like a neutral grey box.
    const QColor base = light() ? floor() : QColor(0x06, 0x08, 0x0a);
    const float k = 0.04f;
    return QColor::fromRgbF(base.redF()   * (1 - k) + m_Accent.redF()   * k,
                            base.greenF() * (1 - k) + m_Accent.greenF() * k,
                            base.blueF()  * (1 - k) + m_Accent.blueF()  * k);
}

namespace
{
    // The vivid row of the palette, in hue order. The pale row is derived, never listed:
    // two hand-written lists would drift apart the first time one of them is edited.
    struct NamedColor { const char* name; QRgb rgb; };
    const NamedColor VividAccents[] = {
        { "Rose",    0xff4d6d }, { "Ember",  0xff6a3d }, { "Amber",   0xffb300 },
        { "Lemon",   0xffe14d }, { "Lime",   0xa6e22e }, { "Signal",  0x00e676 },
        { "Mint",    0x2ee6b6 }, { "Ion",    0x00d3f2 }, { "Azure",   0x3d9bff },
        { "Indigo",  0x6e74ff }, { "Ultra",  0xc060ff }, { "Magenta", 0xff4fd8 },
    };

    QColor paleOf(const QColor& c)
    {
        const qreal k = 0.45;
        return QColor::fromRgbF(c.redF()   + (1 - c.redF())   * k,
                                c.greenF() + (1 - c.greenF()) * k,
                                c.blueF()  + (1 - c.blueF())  * k);
    }
}

QVariantList Theme::accentPalette() const
{
    QVariantList out;
    for (const auto& v : VividAccents) {
        out.append(QVariantMap { { QStringLiteral("name"), QString::fromLatin1(v.name) },
                                 { QStringLiteral("color"), QColor(v.rgb) } });
    }
    for (const auto& v : VividAccents) {
        // The colour is rounded to 8 bits per channel here, once, so the swatch, the saved
        // value and accentName() all compare the same three bytes.
        out.append(QVariantMap { { QStringLiteral("name"), QString() },
                                 { QStringLiteral("color"), QColor(paleOf(QColor(v.rgb)).name()) } });
    }
    return out;
}

QString Theme::accentName(const QColor& c) const
{
    for (const auto& v : VividAccents) {
        if (QColor(v.rgb).rgb() == c.rgb()) {
            return QString::fromLatin1(v.name);
        }
    }
    return QString();
}

void Theme::previewAccent(const QColor& c)
{
    if (!c.isValid() || c == m_Accent) {
        return;
    }

    m_Accent = c;
    emit changed();
}

void Theme::setAccent(const QColor& c)
{
    if (!c.isValid()) {
        return;
    }

    // Saved even when the colour is already the one on screen: after previewAccent() it is,
    // and the popup's confirmation is precisely what has to reach the registry. Into the
    // theme in force: each theme keeps its own accent (13/10/2026).
    const bool same = (c == m_Accent);
    m_Accent = c;
    (m_Light ? m_AccentLight : m_AccentDark) = c;
    save();
    if (!same) {
        emit changed();
    }
}

void Theme::setUiScale(qreal s)
{
    // `s` is the fit to the window. Same clamp the pages apply to their own copy, then the
    // user's Interface scale on top (7.0.0). Guarded against the pointless churn of a resize
    // that moves the scale by a thousandth: every dialog in the app binds to this.
    if (s < 0.62) s = 0.62;
    if (s > 1.60) s = 1.60;
    m_FitScale = s;
    const qreal scaled = s * scaleFactor();
    if (qAbs(scaled - m_UiScale) < 0.001) {
        return;
    }

    m_UiScale = scaled;
    emit uiScaleChanged();
}

void Theme::setScalePercent(int pct)
{
    pct = qBound(90, pct, 110);
    if (pct == m_ScalePercent) {
        return;
    }

    m_ScalePercent = pct;
    m_UiScale = m_FitScale * scaleFactor();
    save();
    emit uiScaleChanged();
}

void Theme::setReduceAnimations(bool on)
{
    if (on == m_ReduceAnimations) {
        return;
    }

    m_ReduceAnimations = on;
    save();
    emit changed();
}

int Theme::interfaceRefreshHz() const
{
    return UiAnimationDriver::screenHz();
}

void Theme::setUnlockFrameRate(bool on)
{
    if (on == m_UnlockFrameRate) {
        return;
    }

    m_UnlockFrameRate = on;
    UiAnimationDriver::apply(on);
    save();
    emit changed();
}

void Theme::setStartupAnimation(bool on)
{
    if (on == m_StartupAnimation) {
        return;
    }

    m_StartupAnimation = on;
    save();
    emit changed();
}

void Theme::setHostPageCovers(bool on)
{
    if (on == m_HostPageCovers) {
        return;
    }

    m_HostPageCovers = on;
    save();
    emit changed();
}

void Theme::setLibraryGrid(bool on)
{
    if (on == m_LibraryGrid) {
        return;
    }

    m_LibraryGrid = on;
    save();
    emit libraryGridChanged();
}

void Theme::setLibraryCoverSize(int size)
{
    size = qBound(0, size, 2);
    if (size == m_LibraryCoverSize) {
        return;
    }

    m_LibraryCoverSize = size;
    save();
    emit libraryAppearanceChanged();
}

void Theme::setLibraryShowTitles(bool on)
{
    if (on == m_LibraryShowTitles) {
        return;
    }

    m_LibraryShowTitles = on;
    save();
    emit libraryAppearanceChanged();
}

void Theme::save() const
{
    // Written on every change rather than at teardown: a process that is killed never reaches
    // a teardown hook, which is exactly how the link-matching preference used to lose itself.
    QSettings settings;
    settings.setValue(SER_ACCENT, m_AccentDark.name());
    settings.setValue(SER_ACCENT_LIGHT, m_AccentLight.name());
    settings.setValue(SER_AUTOHOW, m_AutoHow);
    settings.setValue(SER_LIGHTFROM, m_LightFrom);
    settings.setValue(SER_DARKFROM, m_DarkFrom);
    settings.setValue(SER_FORCED, m_Forced);
    settings.setValue(SER_FORCEDUNTIL, m_ForcedUntil);
    settings.setValue(SER_REDUCE, m_ReduceAnimations);
    settings.setValue(SER_UNLOCKFPS, m_UnlockFrameRate);
    settings.setValue(SER_STARTUP, m_StartupAnimation);
    settings.setValue(SER_HOSTCOVERS, m_HostPageCovers);
    settings.setValue(SER_MODE, m_Mode);
    settings.setValue(SER_SCALE, m_ScalePercent);
    settings.setValue(SER_LIBRARY_GRID, m_LibraryGrid);
    settings.setValue(SER_LIBRARY_COVER_SIZE, m_LibraryCoverSize);
    settings.setValue(SER_LIBRARY_SHOW_TITLES, m_LibraryShowTitles);
}
