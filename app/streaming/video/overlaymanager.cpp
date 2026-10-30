#include "overlaymanager.h"
#include "settings/streamingpreferences.h"
#include "settings/theme.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QMutexLocker>
#include <QPainter>
#include <QStringList>
#include <QSvgRenderer>
#include <QtMath>
#include <atomic>

using namespace Overlay;

/*
 * The overlays, as redesigned in 8.3.0 (issue #47, mockup in docs/mockups/overlay-redesign,
 * direction A for the card and the bar, B's row of tiles in A's surfaces for the panel).
 *
 * Painted with QPainter on a QImage and handed to the renderers as an ARGB8888 SDL_Surface,
 * as before — SDL_ttf drew the text until 8.2.0. QPainter gives the app's own fonts (DM Sans,
 * JetBrains Mono, registered by main.qml's FontLoaders), anti-aliased shapes and SVG glyphs
 * in one pass, on whichever thread builds the surface.
 *
 * ⚠️ Every measure is a fraction of the viewport's HEIGHT: 1 unit = 1 px at 1080p. Until 8.2.0
 * the sizes were pixels, so a 25 px line took a fortieth of a 1080p screen and an eightieth of
 * a 4K one — the "too small at 4K, even at the largest size" of the first 8.3.0 test.
 *
 * Colours are the theme's stage palette (theme.h, stage*): the dark values, always, because
 * the overlays sit on video in either theme. The accent is the user's, as it is in Dark.
 */

namespace {

const QColor kText(0xf3, 0xf7, 0xf8);
const QColor kText2(0xa2, 0xb2, 0xba);
const QColor kText3(0x75, 0x87, 0x90);
const QColor kOnline(0x4a, 0xde, 0x80);
const QColor kWarning(0xf5, 0xa6, 0x23);
const QColor kDanger(0xf8, 0x71, 0x71);
const QColor kLine(0xff, 0xff, 0xff, 0x14);
// The box's edge: a light line that holds over a bright and a dark scene alike.
const QColor kFrame(0xff, 0xff, 0xff, 0x40);

std::atomic<int> s_ViewportWidth{0};
std::atomic<int> s_ViewportHeight{0};

double unitFor(double sizeFactor)
{
    const int h = s_ViewportHeight > 0 ? s_ViewportHeight.load() : 1080;
    return h / 1080.0 * sizeFactor;
}

int viewportWidth()
{
    return s_ViewportWidth > 0 ? s_ViewportWidth.load() : 1920;
}

QFont sansFont(double px, QFont::Weight weight = QFont::Normal)
{
    QFont f(QStringLiteral("DM Sans"));
    f.setPixelSize(qMax(1, qRound(px)));
    f.setWeight(weight);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont monoFont(double px, QFont::Weight weight = QFont::Medium)
{
    QFont f(QStringLiteral("JetBrains Mono"));
    f.setPixelSize(qMax(1, qRound(px)));
    f.setWeight(weight);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

// Small capitals with tracking: the section labels of Settings.
QFont capsFont(double px, double u)
{
    QFont f = sansFont(px, QFont::DemiBold);
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.4 * u);
    return f;
}

double textWidth(const QFont& f, const QString& s)
{
    return QFontMetricsF(f).horizontalAdvance(s);
}

QImage newCanvas(int w, int h)
{
    QImage img(qMax(1, w), qMax(1, h), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

// The renderers take straight (not premultiplied) ARGB8888, which is QImage's ARGB32 byte for
// byte on little-endian.
SDL_Surface* toSurface(const QImage& canvas)
{
    const QImage img = canvas.convertToFormat(QImage::Format_ARGB32);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, img.width(), img.height(), 32, SDL_PIXELFORMAT_ARGB8888);
    if (s == nullptr) {
        return nullptr;
    }
    for (int row = 0; row < img.height(); row++) {
        memcpy((Uint8*)s->pixels + row * s->pitch, img.constScanLine(row), img.width() * 4);
    }
    return s;
}

void beginPaint(QImage& img, QPainter& p)
{
    p.begin(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
}

// The card every overlay sits on: the app's card colour, a light edge, round corners.
void drawBox(QPainter& p, const QRectF& r, double radius, const QColor& fill, double u)
{
    const double pen = qMax(1.0, 1.25 * u);
    p.setPen(QPen(kFrame, pen));
    p.setBrush(fill);
    const double h = pen / 2;
    p.drawRoundedRect(r.adjusted(h, h, -h, -h), radius, radius);
}

void drawDot(QPainter& p, const QPointF& centre, double d, const QColor& c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(centre, d / 2, d / 2);
}

// A padlock of height h, its body's bottom-left corner at the given point: a read-only tile of
// the Stream Settings panel (30/10/2026 — greying alone did not say the value cannot change).
void drawLock(QPainter& p, const QPointF& bottomLeft, double h, const QColor& c)
{
    const double bodyH = h * 0.58, bodyW = h * 0.78;
    const QRectF body(bottomLeft.x(), bottomLeft.y() - bodyH, bodyW, bodyH);
    const double stroke = qMax(1.0, h * 0.13);
    const double shackleW = bodyW * 0.62;
    const QRectF shackle(body.center().x() - shackleW / 2, bottomLeft.y() - h + stroke / 2,
                         shackleW, (h - bodyH) * 2);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c, stroke, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(shackle, 0, 180 * 16);
    p.drawLine(QPointF(shackle.left(), shackle.center().y()), QPointF(shackle.left(), body.top()));
    p.drawLine(QPointF(shackle.right(), shackle.center().y()), QPointF(shackle.right(), body.top()));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(body, h * 0.1, h * 0.1);
}

// Text whose left edge is x and whose baseline is y.
void drawText(QPainter& p, const QFont& f, const QColor& c, double x, double baseline, const QString& s)
{
    p.setFont(f);
    p.setPen(c);
    p.drawText(QPointF(x, baseline), s);
}

// The baseline that centres a line of font f in a band from top to top + height.
double centredBaseline(const QFont& f, double top, double height)
{
    const QFontMetricsF fm(f);
    return top + (height + fm.ascent() - fm.descent()) / 2;
}

void drawSvg(QPainter& p, const QString& path, const QRectF& r)
{
    if (path.isEmpty()) {
        return;
    }
    QSvgRenderer renderer(path);
    if (renderer.isValid()) {
        renderer.render(&p, r);
    }
}

QColor fromSdl(const SDL_Color& c)
{
    return QColor(c.r, c.g, c.b, c.a);
}

// The card and bar box: the user's transparency (Settings > Overlay) on the app's card colour.
QColor statsFill()
{
    return StreamingPreferences::get()->overlayBoxColorValue();
}

double statsSizeFactor()
{
    return StreamingPreferences::get()->overlaySizeFactor();
}

} // namespace

OverlayManager::OverlayManager() :
    m_Renderer(nullptr)
{
    memset(m_Overlays, 0, sizeof(m_Overlays));

    // A new stream: its renderer will say how big it is.
    s_ViewportWidth = 0;
    s_ViewportHeight = 0;

    // The accent as the dark theme has it, read here because the Session — and so this — is
    // built on the thread the theme lives on; the surfaces are built on others.
    Theme* theme = Theme::get();
    m_Accent = theme != nullptr ? theme->darkAccent() : QColor(0x00, 0xd3, 0xf2);
    m_OnAccent = theme != nullptr ? theme->onColor(m_Accent) : QColor(0x06, 0x18, 0x1c);
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
    }
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    m_Overlays[type].kind = KindText;
    SDL_utf8strlcpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));

    setOverlayTextUpdated(type);
}

void OverlayManager::updateOverlayPanel(OverlayType type, const OverlayPanel& panel)
{
    m_Overlays[type].kind = KindPanel;
    m_Panels[type] = panel;

    setOverlayTextUpdated(type);
}

void OverlayManager::updateOverlayBar(OverlayType type, const OverlayBar& bar)
{
    {
        QMutexLocker lock(&m_ContentLock);
        m_Bars[type] = bar;
    }
    m_Overlays[type].kind = KindBar;

    setOverlayTextUpdated(type);
}

void OverlayManager::updateOverlayCard(OverlayType type, const OverlayCard& card)
{
    {
        QMutexLocker lock(&m_ContentLock);
        m_Cards[type] = card;
    }
    m_Overlays[type].kind = KindCard;

    setOverlayTextUpdated(type);
}

void OverlayManager::setOverlayTextUpdated(OverlayType type)
{
    // Only update the overlay state if it's enabled. If it's not enabled,
    // the renderer has already been notified by setOverlayState().
    if (m_Overlays[type].enabled) {
        notifyOverlayUpdated(type);
    }
}

void OverlayManager::setOverlayState(OverlayType type, bool enabled)
{
    bool stateChanged = m_Overlays[type].enabled != enabled;

    m_Overlays[type].enabled = enabled;

    if (stateChanged) {
        if (!enabled) {
            // Empty the content on disable, so switching back on does not show a second-old
            // reading.
            m_Overlays[type].text[0] = 0;
            QMutexLocker lock(&m_ContentLock);
            m_Bars[type] = OverlayBar();
            m_Cards[type] = OverlayCard();
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    m_Renderer = renderer;
}

SDL_Rect OverlayManager::overlayRect(OverlayType type, int viewportWidth, int viewportHeight,
                                     int surfaceWidth, int surfaceHeight)
{
    const int margin = qRound(24.0 * viewportHeight / 1080.0);
    SDL_Rect r = { 0, 0, surfaceWidth, surfaceHeight };

    switch (type) {
    case OverlayDebug:
        if (StreamingPreferences::get()->overlayLayout == StreamingPreferences::OVL_BAR) {
            r.x = (viewportWidth - surfaceWidth) / 2;
        }
        else {
            r.x = StreamingPreferences::get()->overlayPosition == StreamingPreferences::OVP_TOP_RIGHT
                    ? viewportWidth - surfaceWidth - margin : margin;
        }
        r.y = margin;
        break;
    case OverlayStreamSettings:
        r.x = (viewportWidth - surfaceWidth) / 2;
        r.y = viewportHeight - surfaceHeight - qRound(margin * 1.5);
        break;
    case OverlayStatusUpdate:
    default:
        r.x = margin;
        r.y = viewportHeight - surfaceHeight - margin;
        break;
    }
    return r;
}

void OverlayManager::setViewportSize(int width, int height)
{
    s_ViewportWidth = width;
    s_ViewportHeight = height;
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    if (m_Renderer == nullptr) {
        return;
    }

    // Build the new surface.
    SDL_Surface* newSurface = nullptr;
    if (m_Overlays[type].enabled) {
        switch (m_Overlays[type].kind) {
        case KindPanel: newSurface = buildPanelSurface(type); break;
        case KindBar:   newSurface = buildBarSurface(type);   break;
        case KindCard:  newSurface = buildCardSurface(type);  break;
        default:        newSurface = buildTextSurface(type);  break;
        }
    }

    // Exchange the old surface with the new one
    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr(
        (void**)&m_Overlays[type].surface,
        newSurface);

    // Notify the renderer
    m_Renderer->notifyOverlayUpdated(type);

    // Free the old surface
    if (oldSurface != nullptr) {
        SDL_FreeSurface(oldSurface);
    }
}

// The in-stream notices ("Gamepad mouse mode active", the clipboard's): their lines on a small
// card, bottom left.
SDL_Surface* OverlayManager::buildTextSurface(OverlayType type)
{
    QStringList lines = QString::fromUtf8(m_Overlays[type].text).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    if (lines.isEmpty()) {
        return nullptr;
    }

    const double u = unitFor(1.0);
    const QFont first = sansFont(17 * u, QFont::DemiBold);
    const QFont rest = sansFont(15 * u);
    const double padX = 18 * u, padY = 12 * u, lineH = 24 * u;

    double w = 0;
    for (int i = 0; i < lines.size(); i++) {
        w = qMax(w, textWidth(i == 0 ? first : rest, lines[i]));
    }

    QImage img = newCanvas(qCeil(w + padX * 2), qCeil(lineH * lines.size() + padY * 2));
    QPainter p;
    beginPaint(img, p);
    drawBox(p, QRectF(0, 0, img.width(), img.height()), 12 * u, QColor(0x0f, 0x15, 0x19, 0xE6), u);
    for (int i = 0; i < lines.size(); i++) {
        const QFont& f = i == 0 ? first : rest;
        drawText(p, f, i == 0 ? kText : kText2, padX, centredBaseline(f, padY + i * lineH, lineH), lines[i]);
    }
    p.end();
    return toSurface(img);
}

/*
 * The card (8.3.0, direction A): the app's card, a header with the host's name beside the status
 * dot (green, red when any figure shown is past its threshold), then the sections — a rule, the
 * title in small capitals, a row per figure with its label left and its value right in mono.
 * A value past its threshold is red, with a red mark at the row's left edge.
 */
SDL_Surface* OverlayManager::buildCardSurface(OverlayType type)
{
    QMutexLocker lock(&m_ContentLock);
    const OverlayCard& card = m_Cards[type];
    if (card.sections.isEmpty()) {
        return nullptr;
    }

    const double u = unitFor(statsSizeFactor());
    if (u != m_SlotUnit) {
        m_BarSlotWidths.clear();
        m_CardSlotWidths.clear();
        m_SlotUnit = u;
    }

    const QFont caps = capsFont(11 * u, u);
    const QFont chipFont = [&]() { QFont f = sansFont(11.5 * u, QFont::DemiBold); f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0 * u); return f; }();
    const QFont label = sansFont(15 * u);
    const QFont value = monoFont(15 * u);

    const double pad = 18 * u, headerH = 26 * u, rowH = 25 * u, titleH = 26 * u, gap = 10 * u;
    const double labelGap = 28 * u;

    // Width: the widest label + value, values in their grown slots; the header; a floor.
    bool alert = false;
    double contentW = 280 * u;
    double labelColW = 0, valueColW = 0;
    for (const OverlayCardSection& s : card.sections) {
        for (const OverlayCardRow& r : s.rows) {
            alert = alert || r.warn;
            const int vw = qCeil(textWidth(value, r.value));
            const int slot = qMax(vw, m_CardSlotWidths.value(r.label, 0));
            m_CardSlotWidths.insert(r.label, slot);
            labelColW = qMax(labelColW, textWidth(label, r.label));
            valueColW = qMax(valueColW, double(slot));
        }
    }
    const double chipW = textWidth(chipFont, m_HostName) + 7 * u + 6 * u + 18 * u;
    contentW = qMax(contentW, labelColW + labelGap + valueColW);
    contentW = qMax(contentW, textWidth(caps, QStringLiteral("Performance")) + 16 * u + chipW);

    double h = pad + headerH;
    for (const OverlayCardSection& s : card.sections) {
        h += gap + titleH + rowH * s.rows.size();
    }
    h += pad;

    QImage img = newCanvas(qCeil(contentW + pad * 2), qCeil(h));
    QPainter p;
    beginPaint(img, p);
    drawBox(p, QRectF(0, 0, img.width(), img.height()), 14 * u, statsFill(), u);

    // Header: PERFORMANCE, and the host's chip on the right.
    double y = pad;
    drawText(p, caps, kText2, pad, centredBaseline(caps, y, headerH), QStringLiteral("Performance"));
    {
        const double ch = 24 * u;
        const QRectF chip(pad + contentW - chipW, y + (headerH - ch) / 2, chipW, ch);
        p.setPen(QPen(QColor(0xff, 0xff, 0xff, 0x30), qMax(1.0, u)));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(chip, ch / 2, ch / 2);
        drawDot(p, QPointF(chip.left() + 9 * u + 3.5 * u, chip.center().y()), 7 * u, alert ? kDanger : kOnline);
        drawText(p, chipFont, kText, chip.left() + 9 * u + 7 * u + 6 * u, centredBaseline(chipFont, chip.top(), ch), m_HostName);
    }
    y += headerH;

    for (const OverlayCardSection& s : card.sections) {
        y += gap;
        p.setPen(QPen(kLine, qMax(1.0, u)));
        p.drawLine(QPointF(pad, y - gap / 2), QPointF(pad + contentW, y - gap / 2));
        drawText(p, caps, kText3, pad, centredBaseline(caps, y + 4 * u, titleH - 4 * u), s.title);
        y += titleH;
        for (const OverlayCardRow& r : s.rows) {
            if (r.warn) {
                p.setPen(Qt::NoPen);
                p.setBrush(kDanger);
                const double mh = rowH * 0.62;
                p.drawRoundedRect(QRectF(pad - 10 * u, y + (rowH - mh) / 2, 3 * u, mh), 1.5 * u, 1.5 * u);
            }
            drawText(p, label, kText2, pad, centredBaseline(label, y, rowH), r.label);
            const double vw = textWidth(value, r.value);
            drawText(p, value, r.warn ? kDanger : kText, pad + contentW - vw, centredBaseline(value, y, rowH), r.value);
            y += rowH;
        }
    }

    p.end();
    return toSurface(img);
}

/*
 * The bar (8.3.0, direction A): a pill at the top centre, the status dot first, then the entries
 * between thin dividers — each a label in small capitals, the value in mono and its unit. Past
 * 85% of the viewport's width it wraps, between entries only, each line centred.
 *
 * ⚠️ The width is kept steady on purpose. The bar is centred, so a width that followed the
 * digits would move both of its edges every second; instead each value has a slot as wide as
 * the widest that figure has been this session, and is right-aligned in it.
 */
SDL_Surface* OverlayManager::buildBarSurface(OverlayType type)
{
    QMutexLocker lock(&m_ContentLock);
    const OverlayBar& bar = m_Bars[type];
    if (bar.entries.isEmpty()) {
        return nullptr;
    }

    const double u = unitFor(statsSizeFactor());
    if (u != m_SlotUnit) {
        m_BarSlotWidths.clear();
        m_CardSlotWidths.clear();
        m_SlotUnit = u;
    }

    const QFont caps = capsFont(11 * u, u);
    const QFont value = monoFont(17 * u);
    const QFont unit = sansFont(12.5 * u);
    const double lineH = 44 * u, padX = 16 * u, dotD = 9 * u, space = 14 * u, inner = 7 * u;

    // Every entry's width, its values in their slots.
    struct PartLayout { double labelW; double slotW; double valueW; double unitW; };
    QVector<QVector<PartLayout>> layouts;
    QVector<double> entryW;
    bool alert = false;
    for (const OverlayBarEntry& e : bar.entries) {
        QVector<PartLayout> parts;
        double w = 0;
        for (int i = 0; i < e.parts.size(); i++) {
            const OverlayBarPart& part = e.parts[i];
            alert = alert || part.warn;
            PartLayout pl;
            pl.labelW = part.label.isEmpty() ? 0 : textWidth(caps, part.label);
            pl.valueW = textWidth(value, part.value);
            pl.slotW = qMax(pl.valueW, double(m_BarSlotWidths.value(part.key, 0)));
            m_BarSlotWidths.insert(part.key, qCeil(pl.slotW));
            pl.unitW = part.unit.isEmpty() ? 0 : textWidth(unit, part.unit);
            w += (i > 0 ? space : 0) + (pl.labelW > 0 ? pl.labelW + inner : 0) + pl.slotW + (pl.unitW > 0 ? inner * 0.7 + pl.unitW : 0);
            parts.append(pl);
        }
        layouts.append(parts);
        entryW.append(w);
    }

    // Whole entries per line; the first line also carries the dot.
    const double divider = space * 2 + qMax(1.0, u);
    const double maxLineW = viewportWidth() * 0.85 - padX * 2;
    QVector<QVector<int>> lines;
    QVector<double> lineW;
    {
        QVector<int> cur;
        double w = dotD + space;
        for (int e = 0; e < entryW.size(); e++) {
            const double add = cur.isEmpty() ? entryW[e] : divider + entryW[e];
            if (!cur.isEmpty() && w + add > maxLineW) {
                lines.append(cur);
                lineW.append(w);
                cur.clear();
                w = entryW[e];
            }
            else {
                w += add;
            }
            cur.append(e);
        }
        lines.append(cur);
        lineW.append(w);
    }
    double contentW = 0;
    for (double w : lineW) {
        contentW = qMax(contentW, w);
    }

    const double h = lineH * lines.size();
    QImage img = newCanvas(qCeil(contentW + padX * 2), qCeil(h));
    QPainter p;
    beginPaint(img, p);
    drawBox(p, QRectF(0, 0, img.width(), img.height()), lines.size() == 1 ? h / 2 : 18 * u, statsFill(), u);

    for (int l = 0; l < lines.size(); l++) {
        const double top = l * lineH;
        const double baseline = centredBaseline(value, top, lineH);
        double x = padX + (contentW - lineW[l]) / 2;
        if (l == 0) {
            drawDot(p, QPointF(x + dotD / 2, top + lineH / 2), dotD, alert ? kDanger : kOnline);
            x += dotD + space;
        }
        for (int n = 0; n < lines[l].size(); n++) {
            const int e = lines[l][n];
            if (n > 0) {
                x += space;
                p.setPen(QPen(QColor(0xff, 0xff, 0xff, 0x30), qMax(1.0, u)));
                p.drawLine(QPointF(x, top + lineH * 0.28), QPointF(x, top + lineH * 0.72));
                x += qMax(1.0, u) + space;
            }
            const OverlayBarEntry& entry = bar.entries[e];
            for (int i = 0; i < entry.parts.size(); i++) {
                const OverlayBarPart& part = entry.parts[i];
                const PartLayout& pl = layouts[e][i];
                if (i > 0) {
                    x += space;
                }
                if (pl.labelW > 0) {
                    drawText(p, caps, part.warn ? QColor(0xf8, 0x71, 0x71, 0xCC) : kText3, x, baseline, part.label);
                    x += pl.labelW + inner;
                }
                drawText(p, value, part.warn ? kDanger : kText, x + pl.slotW - pl.valueW, baseline, part.value);
                x += pl.slotW;
                if (pl.unitW > 0) {
                    x += inner * 0.7;
                    drawText(p, unit, kText2, x, baseline, part.unit);
                    x += pl.unitW;
                }
            }
        }
    }

    p.end();
    return toSurface(img);
}

/*
 * The Stream Settings panel (8.3.0: B's row of tiles in A's surfaces): a card at the bottom
 * centre — the title, what it applies to and the chip of where Save writes, then a tile per
 * setting. The selected tile is washed in the accent with an accent edge and its value in an
 * accent pill; a changed value carries the amber dot; the bitrate sits on a gauge. Under them a
 * rule, the status line and the controller prompts.
 *
 * ⚠️ Always Medium, whatever size the stats overlay has: this one is a control being operated,
 * and it has to stay legible whatever someone picked for a readout — as before 8.3.0.
 */
SDL_Surface* OverlayManager::buildPanelSurface(OverlayType type)
{
    const OverlayPanel& panel = m_Panels[type];
    if (panel.rows.isEmpty()) {
        return nullptr;
    }

    const double u = unitFor(1.0);
    const QFont title = sansFont(21 * u, QFont::DemiBold);
    const QFont subtitle = sansFont(13 * u);
    const QFont chipFont = sansFont(12.5 * u, QFont::DemiBold);
    const QFont caps = capsFont(11 * u, u);
    const QFont value = monoFont(17 * u);
    const QFont hint = sansFont(12 * u);
    const QFont status = sansFont(13 * u);
    const QFont prompt = sansFont(14 * u, QFont::Medium);

    const double pad = 22 * u, tileW = 176 * u, tileH = 112 * u, tileGap = 10 * u;
    const double headerH = 30 * u, vgap = 14 * u, footerH = 26 * u, glyph = 22 * u;

    const int n = panel.rows.size();
    const double innerW = n * tileW + (n - 1) * tileGap;
    const double w = innerW + pad * 2;
    const double h = pad + headerH + vgap + tileH + vgap + qMax(1.0, u) + vgap + footerH + pad;

    QImage img = newCanvas(qCeil(w), qCeil(h));
    QPainter p;
    beginPaint(img, p);
    drawBox(p, QRectF(0, 0, img.width(), img.height()), 16 * u, QColor(0x0f, 0x15, 0x19, 0xF0), u);

    // Header.
    double y = pad;
    {
        const double bl = centredBaseline(title, y, headerH);
        drawText(p, title, kText, pad, bl, panel.title);
        if (!panel.subtitle.isEmpty()) {
            drawText(p, subtitle, kText2, pad + textWidth(title, panel.title) + 12 * u, bl, panel.subtitle);
        }
        if (!panel.badgeText.isEmpty()) {
            const double ch = 26 * u;
            const double cw = textWidth(chipFont, panel.badgeText) + 7 * u + 6 * u + 22 * u;
            const QRectF chip(pad + innerW - cw, y + (headerH - ch) / 2, cw, ch);
            p.setPen(QPen(QColor(0xf3, 0xf7, 0xf8, 0x42), qMax(1.0, u)));
            p.setBrush(QColor(0xf3, 0xf7, 0xf8, 0x12));
            p.drawRoundedRect(chip, ch / 2, ch / 2);
            drawDot(p, QPointF(chip.left() + 11 * u + 3.5 * u, chip.center().y()), 7 * u, fromSdl(panel.badgeColor));
            drawText(p, chipFont, kText, chip.left() + 11 * u + 7 * u + 6 * u, centredBaseline(chipFont, chip.top(), ch), panel.badgeText);
        }
    }
    y += headerH + vgap;

    // The tiles.
    for (int i = 0; i < n; i++) {
        const OverlayRow& r = panel.rows[i];
        const QRectF tile(pad + i * (tileW + tileGap), y, tileW, tileH);
        const bool sel = r.selected && !r.dimmed;

        QColor fill = sel ? QColor(m_Accent.red(), m_Accent.green(), m_Accent.blue(), 0x1F) : QColor(0xf3, 0xf7, 0xf8, 0x0A);
        const double edge = sel ? qMax(1.5, 2 * u) : qMax(1.0, u);
        p.setPen(QPen(sel ? m_Accent : kLine, edge));
        p.setBrush(fill);
        p.drawRoundedRect(tile.adjusted(edge / 2, edge / 2, -edge / 2, -edge / 2), 12 * u, 12 * u);

        // Label, with the amber dot when the value differs from what the stream runs with.
        const QString lbl = r.indent ? QStringLiteral("· ") + r.label : r.label;
        const double lblBase = centredBaseline(caps, tile.top() + 8 * u, 22 * u);
        drawText(p, caps, r.dimmed ? kText3 : sel ? kText : kText3, tile.left() + 14 * u, lblBase, lbl);
        if (r.changed) {
            drawDot(p, QPointF(tile.left() + 14 * u + textWidth(caps, lbl) + 9 * u, tile.top() + 19 * u), 7 * u, kWarning);
        }
        // Read-only: a padlock in the corner, so it says why the tile is grey.
        if (r.dimmed) {
            drawLock(p, QPointF(tile.right() - 14 * u - 11 * u, tile.top() + 26 * u), 15 * u, kText3);
        }

        // The value in its pill.
        const double pillH = 34 * u;
        const double pillW = qMin(tileW - 24 * u, textWidth(value, r.value) + 22 * u);
        const QRectF pill(tile.left() + 12 * u, tile.top() + 38 * u, pillW, pillH);
        p.setPen(Qt::NoPen);
        p.setBrush(sel ? m_Accent : QColor(0xf3, 0xf7, 0xf8, r.dimmed ? 0x0A : 0x12));
        p.drawRoundedRect(pill, 8 * u, 8 * u);
        drawText(p, value, sel ? m_OnAccent : r.dimmed ? kText3 : kText,
                 pill.left() + (pill.width() - textWidth(value, r.value)) / 2, centredBaseline(value, pill.top(), pillH), r.value);

        // Under the value: what to press and the hint, and the gauge if the tile has one —
        // then thinner and at the very bottom, with the words just above it (30/10/2026: the
        // bitrate tile said nothing about ▲▼, its gauge sat where the others print it).
        QString under = r.hint;
        if (sel && r.showArrows) {
            under = QStringLiteral("▲▼  ") + (r.hint.isEmpty() ? QStringLiteral("change") : r.hint);
        }
        if (r.gauge >= 0) {
            const QRectF track(tile.left() + 14 * u, tile.bottom() - 13 * u, tileW - 28 * u, 4 * u);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xf3, 0xf7, 0xf8, 0x1F));
            p.drawRoundedRect(track, 2 * u, 2 * u);
            p.setBrush(m_Accent);
            p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * qBound(0.0, r.gauge, 1.0), track.height()), 2 * u, 2 * u);
            if (!under.isEmpty()) {
                drawText(p, hint, kText3, tile.left() + 14 * u, centredBaseline(hint, tile.top() + 74 * u, 18 * u), under);
            }
        }
        else if (!under.isEmpty()) {
            drawText(p, hint, kText3, tile.left() + 14 * u, centredBaseline(hint, tile.bottom() - 30 * u, 20 * u), under);
        }
    }
    y += tileH + vgap;

    // Rule, status, prompts.
    p.setPen(QPen(kLine, qMax(1.0, u)));
    p.drawLine(QPointF(pad, y), QPointF(pad + innerW, y));
    y += qMax(1.0, u) + vgap;

    drawText(p, status, fromSdl(panel.statusColor), pad, centredBaseline(status, y, footerH), panel.statusLine);

    double promptsW = 0;
    for (int i = 0; i < panel.footerBadges.size(); i++) {
        promptsW += (i > 0 ? 20 * u : 0) + glyph + 7 * u + textWidth(prompt, panel.footerBadges[i].label);
    }
    double x = pad + innerW - promptsW;
    for (int i = 0; i < panel.footerBadges.size(); i++) {
        const OverlayBadge& b = panel.footerBadges[i];
        if (i > 0) {
            x += 20 * u;
        }
        drawSvg(p, b.iconPath, QRectF(x, y + (footerH - glyph) / 2, glyph, glyph));
        x += glyph + 7 * u;
        drawText(p, prompt, i == 0 ? kText : kText2, x, centredBaseline(prompt, y, footerH), b.label);
        x += textWidth(prompt, b.label);
    }

    p.end();
    return toSurface(img);
}
