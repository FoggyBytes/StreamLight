#pragma once

#include <QString>
#include <QVector>

#include "SDL_compat.h"
#include <SDL_ttf.h>

namespace Overlay {

enum OverlayType {
    OverlayDebug,
    OverlayStatusUpdate,
    OverlayStreamSettings,
    OverlayMax
};

// A single navigable/informational row in a structured panel (see OverlayPanel).
// Rendered as: [indent] label  ...  [hint] [‹] value [›] [trailingGlyph]
struct OverlayRow {
    QString label;
    SDL_Color labelColor = {0xE0, 0xE0, 0xE0, 0xFF};
    QString hint;                              // small dim text before the value, e.g. "custom" / "16:10"
    QString value;
    SDL_Color valueColor = {0xFF, 0xFF, 0xFF, 0xFF};
    bool showArrows = false;
    QString trailingGlyph;                     // e.g. "↻" (reset) or "⚡" (instant)
    SDL_Color trailingColor = {0x80, 0x80, 0x80, 0xFF};
    bool selected = false;
    bool indent = false;
};

// A footer action hint, e.g. a green "A" controller button + "Apply".
// If iconPath is set (a Qt resource path to a self-contained button SVG) it is
// rasterized and used; otherwise a flat colored circle + glyph letter is drawn.
struct OverlayBadge {
    QString iconPath;
    QString glyph;
    QString label;
    SDL_Color color = {0x60, 0x60, 0x60, 0xFF};
};

// A structured, multi-row panel (used by the Stream Settings overlay) rendered
// as a dark rounded card with a header/badge, selectable rows, and a footer.
struct OverlayPanel {
    QString title;
    QString badgeText;
    SDL_Color badgeColor = {0x9A, 0xA0, 0xA6, 0xFF};
    QVector<OverlayRow> rows;
    QString statusLine;
    SDL_Color statusColor = {0x80, 0x80, 0x80, 0xFF};
    QVector<OverlayBadge> footerBadges;
};

class IOverlayRenderer
{
public:
    virtual ~IOverlayRenderer() = default;

    virtual void notifyOverlayUpdated(OverlayType type) = 0;
};

class OverlayManager
{
public:
    OverlayManager();
    ~OverlayManager();

    bool isOverlayEnabled(OverlayType type);
    char* getOverlayText(OverlayType type);
    void updateOverlayText(OverlayType type, const char* text);
    // Render a structured panel (rounded card, selectable rows, header + footer)
    // instead of a plain wrapped-text block. Used by the Stream Settings overlay.
    void updateOverlayPanel(OverlayType type, const OverlayPanel& panel);
    int getOverlayMaxTextLength();
    void setOverlayTextUpdated(OverlayType type);
    void setOverlayState(OverlayType type, bool enabled);
    SDL_Color getOverlayColor(OverlayType type);
    int getOverlayFontSize(OverlayType type);
    SDL_Surface* getUpdatedOverlaySurface(OverlayType type);

    void setOverlayRenderer(IOverlayRenderer* renderer);

    // Position (used for drag support); coordinates are in pixels from top-left of the window
    void setOverlayPosition(OverlayType type, float x, float y);
    float getOverlayX(OverlayType type) const;
    float getOverlayY(OverlayType type) const;

private:
    void notifyOverlayUpdated(OverlayType type);
    SDL_Surface* buildTextSurface(OverlayType type);
    SDL_Surface* buildPanelSurface(OverlayType type);
    TTF_Font* panelSmallFont(OverlayType type);

    // POD only — this array is memset-zeroed in the constructor. Non-POD panel
    // data lives in m_Panels (default-constructed) below.
    struct {
        bool enabled;
        bool isPanel;
        int fontSize;
        SDL_Color color;
        char text[1024];

        TTF_Font* font;
        TTF_Font* smallFont;
        SDL_Surface* surface;

        float x;
        float y;
    } m_Overlays[OverlayMax];
    OverlayPanel m_Panels[OverlayMax];
    IOverlayRenderer* m_Renderer;
    QByteArray m_FontData;
};

}
