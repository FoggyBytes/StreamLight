#include "overlaymanager.h"
#include "path.h"

#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QStringList>
#include <QSvgRenderer>
#include <cmath>

using namespace Overlay;

namespace {

// Rasterize a self-contained button SVG (Qt resource path) into dst at (x, y),
// fitted to a square of the given size. No-op if the resource is invalid.
void blitSvgIcon(SDL_Surface* dst, const QString& path, int x, int y, int size)
{
    if (path.isEmpty() || size <= 0) return;

    QSvgRenderer renderer(path);
    if (!renderer.isValid()) return;

    QImage img(size, size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        renderer.render(&p);
    }

    // QImage Format_ARGB32 and SDL_PIXELFORMAT_ARGB8888 share byte layout on
    // little-endian (B,G,R,A), so we can copy scanlines directly.
    SDL_Surface* tmp = SDL_CreateRGBSurfaceWithFormat(
        0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!tmp) return;
    for (int row = 0; row < size; row++) {
        memcpy((Uint8*)tmp->pixels + row * tmp->pitch, img.scanLine(row), size * 4);
    }
    SDL_SetSurfaceBlendMode(tmp, SDL_BLENDMODE_BLEND);
    SDL_Rect d = { x, y, size, size };
    SDL_BlitSurface(tmp, nullptr, dst, &d);
    SDL_FreeSurface(tmp);
}

// ---- Low-level surface drawing helpers (ARGB8888, no blending: values are set
// directly; semi-transparent looks are achieved by pre-mixing over the card bg).

inline Uint32 mapColor(SDL_Surface* s, SDL_Color c)
{
    return SDL_MapRGBA(s->format, c.r, c.g, c.b, c.a);
}

void fillRect(SDL_Surface* s, int x, int y, int w, int h, SDL_Color c)
{
    SDL_Rect r = { x, y, w, h };
    SDL_FillRect(s, &r, mapColor(s, c));
}

// Filled rounded rectangle. Corners outside the radius are left untouched
// (transparent), so the card reads as rounded over the video.
void fillRoundedRect(SDL_Surface* s, int x, int y, int w, int h, int radius, SDL_Color c)
{
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    Uint32 col = mapColor(s, c);

    // Centre band (full width)
    SDL_Rect mid = { x, y + radius, w, h - radius * 2 };
    SDL_FillRect(s, &mid, col);

    // Top/bottom bands, narrowed per-scanline to follow the corner arcs
    for (int i = 0; i < radius; i++) {
        double dy = radius - i - 0.5;
        int inset = (int)std::lround(radius - std::sqrt((double)radius * radius - dy * dy));
        int rowW = w - inset * 2;
        if (rowW <= 0) continue;
        SDL_Rect top = { x + inset, y + i, rowW, 1 };
        SDL_FillRect(s, &top, col);
        SDL_Rect bot = { x + inset, y + h - 1 - i, rowW, 1 };
        SDL_FillRect(s, &bot, col);
    }
}

void fillCircle(SDL_Surface* s, int cx, int cy, int r, SDL_Color c)
{
    Uint32 col = mapColor(s, c);
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)std::lround(std::sqrt((double)r * r - (double)dy * dy));
        SDL_Rect row = { cx - dx, cy + dy, dx * 2 + 1, 1 };
        SDL_FillRect(s, &row, col);
    }
}

// Small horizontal triangle (arrow). dir < 0 points left ('‹'), dir > 0 right ('›').
void fillArrow(SDL_Surface* s, int x, int y, int w, int h, int dir, SDL_Color c)
{
    Uint32 col = mapColor(s, c);
    for (int i = 0; i < h; i++) {
        double t = std::abs((double)i - (h - 1) / 2.0) / ((h - 1) / 2.0); // 0 at tip row, 1 at base edges
        int len = (int)std::lround(w * (1.0 - t));
        if (len <= 0) continue;
        int rx = (dir < 0) ? (x + (w - len)) : x;     // left arrow: tip on the left
        SDL_Rect row = { rx, y + i, len, 1 };
        SDL_FillRect(s, &row, col);
    }
}

// Render one UTF-8 text segment, optionally blitting it onto dst at (x, baselineY
// centred). Returns the rendered width; pass dst=nullptr to measure only.
int textSeg(TTF_Font* f, const QString& str, SDL_Color c, SDL_Surface* dst, int x, int centreY)
{
    if (!f || str.isEmpty()) return 0;
    QByteArray u = str.toUtf8();
    if (!dst) {
        int w = 0, h = 0;
        TTF_SizeUTF8(f, u.constData(), &w, &h);
        return w;
    }
    SDL_Surface* t = TTF_RenderUTF8_Blended(f, u.constData(), c);
    if (!t) return 0;
    int w = t->w;
    SDL_Rect r = { x, centreY - t->h / 2, t->w, t->h };
    SDL_BlitSurface(t, nullptr, dst, &r);
    SDL_FreeSurface(t);
    return w;
}

} // namespace

OverlayManager::OverlayManager() :
    m_Renderer(nullptr),
    m_FontData(Path::readDataFile("RobotoMono.ttf"))
{
    memset(m_Overlays, 0, sizeof(m_Overlays));

    // Top-left stats card, nudged a few px off the top-left corner.
    m_Overlays[OverlayType::OverlayDebug].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayDebug].fontSize = 20;
    m_Overlays[OverlayType::OverlayDebug].x = 12;
    m_Overlays[OverlayType::OverlayDebug].y = 12;

    m_Overlays[OverlayType::OverlayStatusUpdate].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayStatusUpdate].fontSize = 36;
    m_Overlays[OverlayType::OverlayStatusUpdate].x = 0;
    m_Overlays[OverlayType::OverlayStatusUpdate].y = 0;

    // Live "Stream Settings" overlay — top-right (positioned by the renderer).
    // Font size matches the stats card (20) for a consistent overlay look.
    m_Overlays[OverlayType::OverlayStreamSettings].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayStreamSettings].fontSize = 20;
    m_Overlays[OverlayType::OverlayStreamSettings].x = 0;
    m_Overlays[OverlayType::OverlayStreamSettings].y = 0;

    // While TTF will usually not be initialized here, it is valid for that not to
    // be the case, since Session destruction is deferred and could overlap with
    // the lifetime of a new Session object.
    //SDL_assert(TTF_WasInit() == 0);

    if (TTF_Init() != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_Init() failed: %s",
                    TTF_GetError());
        return;
    }
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
        if (m_Overlays[i].font != nullptr) {
            TTF_CloseFont(m_Overlays[i].font);
        }
        if (m_Overlays[i].smallFont != nullptr) {
            TTF_CloseFont(m_Overlays[i].smallFont);
        }
    }

    TTF_Quit();

    // For similar reasons to the comment in the constructor, this will usually,
    // but not always, deinitialize TTF. In the cases where Session objects overlap
    // in lifetime, there may be an additional reference on TTF for the new Session
    // that means it will not be cleaned up here.
    //SDL_assert(TTF_WasInit() == 0);
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

char* OverlayManager::getOverlayText(OverlayType type)
{
    return m_Overlays[type].text;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    m_Overlays[type].isPanel = false;
    strncpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));
    m_Overlays[type].text[getOverlayMaxTextLength() - 1] = '\0';

    setOverlayTextUpdated(type);
}

void OverlayManager::updateOverlayPanel(OverlayType type, const OverlayPanel& panel)
{
    m_Overlays[type].isPanel = true;
    m_Panels[type] = panel;

    setOverlayTextUpdated(type);
}

int OverlayManager::getOverlayMaxTextLength()
{
    return sizeof(m_Overlays[0].text);
}

int OverlayManager::getOverlayFontSize(OverlayType type)
{
    return m_Overlays[type].fontSize;
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
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
            // Set the text to empty string on disable
            m_Overlays[type].text[0] = 0;
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Color OverlayManager::getOverlayColor(OverlayType type)
{
    return m_Overlays[type].color;
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    m_Renderer = renderer;
}

void OverlayManager::setOverlayPosition(OverlayType type, float x, float y)
{
    m_Overlays[type].x = x;
    m_Overlays[type].y = y;
}

float OverlayManager::getOverlayX(OverlayType type) const
{
    return m_Overlays[type].x;
}

float OverlayManager::getOverlayY(OverlayType type) const
{
    return m_Overlays[type].y;
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    if (m_Renderer == nullptr) {
        return;
    }

    // Construct the required font to render the overlay
    if (m_Overlays[type].font == nullptr) {
        if (m_FontData.isEmpty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL overlay font failed to load");
            return;
        }

        // m_FontData must stay around until the font is closed
        m_Overlays[type].font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                               1,
                                               m_Overlays[type].fontSize);
        if (m_Overlays[type].font == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont() failed: %s",
                        TTF_GetError());

            // Can't proceed without a font
            return;
        }
    }

    // Build the new surface (rounded card with text, or a structured panel).
    SDL_Surface* newSurface = nullptr;
    if (m_Overlays[type].enabled) {
        newSurface = m_Overlays[type].isPanel ? buildPanelSurface(type)
                                              : buildTextSurface(type);
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

TTF_Font* OverlayManager::panelSmallFont(OverlayType type)
{
    if (m_Overlays[type].smallFont == nullptr) {
        if (m_FontData.isEmpty()) {
            return nullptr;
        }
        int sz = m_Overlays[type].fontSize - 5;
        if (sz < 12) sz = 12;
        m_Overlays[type].smallFont = TTF_OpenFontRW(
            SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()), 1, sz);
    }
    return m_Overlays[type].smallFont;
}

// Plain multi-line text overlay (stats / status) on a rounded dark card.
// Rendered line-by-line and sized to the longest line — TTF_..._Wrapped pads the
// surface out to the full wrap width, which left a large empty gap on short text.
SDL_Surface* OverlayManager::buildTextSurface(OverlayType type)
{
    TTF_Font* font = m_Overlays[type].font;
    const SDL_Color color = m_Overlays[type].color;

    QStringList lines = QString::fromUtf8(m_Overlays[type].text).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();  // drop trailing blank line from a final '\n'
    }
    if (lines.isEmpty()) {
        return nullptr;
    }

    // Render each line; track the widest for the (tight) card width.
    QVector<SDL_Surface*> lineSurfs;
    lineSurfs.reserve(lines.size());
    int maxW = 0;
    for (const QString& ln : lines) {
        SDL_Surface* s = nullptr;
        if (!ln.isEmpty()) {
            QByteArray u = ln.toUtf8();
            s = TTF_RenderUTF8_Blended(font, u.constData(), color);
            if (s != nullptr) {
                maxW = qMax(maxW, s->w);
            }
        }
        lineSurfs.append(s);
    }

    const int kPadX = 12, kPadY = 11, radius = 9;
    const int lineSkip = TTF_FontLineSkip(font);
    SDL_Surface* bg = SDL_CreateRGBSurfaceWithFormat(
        0, maxW + kPadX * 2, lineSkip * lines.size() + kPadY * 2,
        32, SDL_PIXELFORMAT_ARGB8888);
    if (bg != nullptr) {
        SDL_FillRect(bg, nullptr, SDL_MapRGBA(bg->format, 0, 0, 0, 0));
        fillRoundedRect(bg, 0, 0, bg->w, bg->h, radius, SDL_Color{0x18, 0x18, 0x1A, 0xF0});

        int y = kPadY;
        for (SDL_Surface* s : lineSurfs) {
            if (s != nullptr) {
                SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
                SDL_Rect d = { kPadX, y, s->w, s->h };
                SDL_BlitSurface(s, nullptr, bg, &d);
            }
            y += lineSkip;
        }
    }

    for (SDL_Surface* s : lineSurfs) {
        if (s != nullptr) {
            SDL_FreeSurface(s);
        }
    }
    return bg;
}

// Structured panel (Stream Settings): rounded card, header + target chip,
// selectable value rows with ‹ › arrows, divider, status line, footer badges.
SDL_Surface* OverlayManager::buildPanelSurface(OverlayType type)
{
    const OverlayPanel& panel = m_Panels[type];
    TTF_Font* main = m_Overlays[type].font;
    TTF_Font* small = panelSmallFont(type);
    if (main == nullptr) {
        return nullptr;
    }
    if (small == nullptr) {
        small = main;
    }

    const int mainH  = TTF_FontHeight(main);
    const int smallH = TTF_FontHeight(small);

    // Layout metrics
    const int kPadX = 16;
    const int kPadTop = 12, kPadBottom = 14;
    const int rowH = mainH + 10;
    const int headerH = smallH + 12;
    const int gapHeaderRows = 8;
    const int dividerGap = 9;
    const int statusH = smallH + 8;
    const int footerH = mainH + 12;
    const int colGap = 18;
    const int arrowW = 6, arrowH = qMax(8, mainH / 2), arrowGap = 9;
    const int hintGap = 9;
    const int radius = 12;

    auto measMain  = [&](const QString& s) { return textSeg(main,  s, {}, nullptr, 0, 0); };
    auto measSmall = [&](const QString& s) { return textSeg(small, s, {}, nullptr, 0, 0); };

    auto labelStartX = [&](const OverlayRow& r) {
        return kPadX + (r.indent ? 16 : 0);
    };
    auto clusterWidth = [&](const OverlayRow& r) {
        int w = measMain(r.value);
        if (r.showArrows) w += (arrowW + arrowGap) * 2;
        if (!r.hint.isEmpty()) w += measSmall(r.hint) + hintGap;
        if (!r.trailingGlyph.isEmpty()) w += measSmall(r.trailingGlyph) + 10;
        return w;
    };

    // --- Measure: find the panel inner width
    int maxRight = 0;
    for (const OverlayRow& r : panel.rows) {
        int right = labelStartX(r) + measMain(r.label) + colGap + clusterWidth(r);
        maxRight = qMax(maxRight, right);
    }
    {
        int hdr = kPadX + measSmall(panel.title) + 28 + measSmall(panel.badgeText);
        maxRight = qMax(maxRight, hdr);
    }
    if (!panel.statusLine.isEmpty()) {
        maxRight = qMax(maxRight, kPadX + measSmall(panel.statusLine));
    }
    const int badgeR = smallH / 2 + 2;
    const int iconSize = mainH + 2;
    auto badgeDim = [&](const OverlayBadge& b) {
        return b.iconPath.isEmpty() ? badgeR * 2 : iconSize;
    };
    {
        int fr = kPadX;
        for (int i = 0; i < panel.footerBadges.size(); i++) {
            fr += badgeDim(panel.footerBadges[i]) + 7 + measMain(panel.footerBadges[i].label);
            if (i + 1 < panel.footerBadges.size()) fr += 18;
        }
        maxRight = qMax(maxRight, fr);
    }

    const int panelW = maxRight + kPadX;
    int panelH = kPadTop + headerH + 1 + gapHeaderRows
               + panel.rows.size() * rowH
               + dividerGap + 1 + dividerGap
               + (panel.statusLine.isEmpty() ? 0 : statusH)
               + (panel.footerBadges.isEmpty() ? 0 : footerH)
               + kPadBottom;

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(
        0, panelW, panelH, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surf == nullptr) {
        return nullptr;
    }
    SDL_FillRect(surf, nullptr, SDL_MapRGBA(surf->format, 0, 0, 0, 0));

    // Card background
    fillRoundedRect(surf, 0, 0, panelW, panelH, radius, SDL_Color{0x16, 0x16, 0x19, 0xF2});

    // Colors
    const SDL_Color cTitle   = {0xB8, 0xBC, 0xC2, 0xFF};
    const SDL_Color cDivider = {0x33, 0x33, 0x39, 0xFF};
    const SDL_Color cLabel   = {0xCE, 0xD2, 0xD6, 0xFF};
    const SDL_Color cLabelSel= {0x4A, 0xDE, 0x80, 0xFF};
    const SDL_Color cHint    = {0x76, 0x7A, 0x80, 0xFF};
    const SDL_Color cArrow   = {0x66, 0x6A, 0x70, 0xFF};
    const SDL_Color cArrowSel= {0xC2, 0xC6, 0xCC, 0xFF};
    const SDL_Color cSelBar  = {0x22, 0xC5, 0x5E, 0xFF};
    const SDL_Color cSelRow  = {0x21, 0x2F, 0x26, 0xF2};
    const SDL_Color cBadgeTxt = {0xFF, 0xFF, 0xFF, 0xFF};
    const SDL_Color cFootLbl  = {0xCE, 0xD2, 0xD6, 0xFF};

    int y = kPadTop;

    // --- Header: title (left) + target chip (right)
    {
        int cy = y + headerH / 2;
        textSeg(small, panel.title, cTitle, surf, kPadX, cy);
        if (!panel.badgeText.isEmpty()) {
            int bw = measSmall(panel.badgeText);
            textSeg(small, panel.badgeText, panel.badgeColor, surf,
                    panelW - kPadX - bw, cy);
        }
        y += headerH;
        fillRect(surf, kPadX, y, panelW - kPadX * 2, 1, cDivider);
        y += 1 + gapHeaderRows;
    }

    // --- Rows
    for (const OverlayRow& r : panel.rows) {
        int rowCentre = y + rowH / 2;

        if (r.selected) {
            fillRect(surf, 5, y + 2, panelW - 10, rowH - 4, cSelRow);
            fillRoundedRect(surf, 8, y + 5, 3, rowH - 10, 1, cSelBar);
        }

        // A dimmed row is read-only (e.g. frame pacing while V-Sync is off):
        // label and value are drawn in the muted hint colour so it reads as
        // information rather than as something the user failed to select.
        SDL_Color lc = r.dimmed ? cHint
                     : r.selected ? cLabelSel
                     : (r.labelColor.a ? r.labelColor : cLabel);
        textSeg(main, r.label, lc, surf, labelStartX(r), rowCentre);

        SDL_Color ac = r.selected ? cArrowSel : cArrow;

        // Lay the right cluster from the right edge inward.
        int curRight = panelW - kPadX;
        if (!r.trailingGlyph.isEmpty()) {
            int tw = measSmall(r.trailingGlyph);
            textSeg(small, r.trailingGlyph, r.trailingColor, surf, curRight - tw, rowCentre);
            curRight -= tw + 10;
        }
        if (r.showArrows) {
            fillArrow(surf, curRight - arrowW, rowCentre - arrowH / 2, arrowW, arrowH, +1, ac);
            curRight -= arrowW + arrowGap;
        }
        int vw = measMain(r.value);
        SDL_Color vc = r.dimmed ? cHint
                     : r.valueColor.a ? r.valueColor
                     : SDL_Color{0xF0, 0xF0, 0xF0, 0xFF};
        textSeg(main, r.value, vc, surf, curRight - vw, rowCentre);
        curRight -= vw;
        if (r.showArrows) {
            curRight -= arrowGap;
            fillArrow(surf, curRight - arrowW, rowCentre - arrowH / 2, arrowW, arrowH, -1, ac);
            curRight -= arrowW;
        }
        if (!r.hint.isEmpty()) {
            curRight -= hintGap;
            int hw = measSmall(r.hint);
            textSeg(small, r.hint, cHint, surf, curRight - hw, rowCentre);
        }

        y += rowH;
    }

    // --- Divider before footer
    y += dividerGap;
    fillRect(surf, kPadX, y, panelW - kPadX * 2, 1, cDivider);
    y += 1 + dividerGap;

    // --- Status line
    if (!panel.statusLine.isEmpty()) {
        textSeg(small, panel.statusLine, panel.statusColor, surf, kPadX, y + statusH / 2);
        y += statusH;
    }

    // --- Footer badges: vendor controller-button SVG (or flat circle) + label
    if (!panel.footerBadges.isEmpty()) {
        int cy = y + footerH / 2;
        int x = kPadX;
        for (const OverlayBadge& b : panel.footerBadges) {
            int dim = badgeDim(b);
            if (!b.iconPath.isEmpty()) {
                blitSvgIcon(surf, b.iconPath, x, cy - iconSize / 2, iconSize);
            } else {
                int cx = x + badgeR;
                fillCircle(surf, cx, cy, badgeR, b.color);
                int gw = measSmall(b.glyph);
                textSeg(small, b.glyph, cBadgeTxt, surf, cx - gw / 2, cy);
            }
            x += dim + 7;
            int lw = measMain(b.label);
            textSeg(main, b.label, cFootLbl, surf, x, cy);
            x += lw + 18;
        }
        y += footerH;
    }

    return surf;
}
