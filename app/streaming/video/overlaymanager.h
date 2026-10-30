#pragma once

#include <QColor>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

#include "SDL_compat.h"

namespace Overlay {

enum OverlayType {
    OverlayDebug,
    OverlayStatusUpdate,
    OverlayStreamSettings,
    OverlayMax
};

// One setting of the Stream Settings panel (see OverlayPanel), drawn as a tile.
struct OverlayRow {
    QString label;
    QString hint;                              // small dim text under the value, e.g. "this display" / "16:10"
    QString value;
    bool showArrows = false;                   // the value can be changed (▲▼ on the selected tile)
    bool selected = false;
    bool indent = false;                       // a sub-setting (custom width / height)
    bool dimmed = false;                       // read-only: greyed, never the selection
    bool changed = false;                      // differs from what the stream runs with: the amber dot
    double gauge = -1;                         // 0..1 on a track under the value (the bitrate); < 0 = none
};

// A footer prompt: a controller button glyph (a Qt resource SVG) and its action.
struct OverlayBadge {
    QString iconPath;
    QString label;
};

// The Stream Settings panel (8.3.0 design): a card at the bottom centre with a header, the
// settings as a row of tiles, a status line and the controller prompts.
struct OverlayPanel {
    QString title;
    QString subtitle;
    QString badgeText;                         // where Save writes: Global, a host profile, a game
    SDL_Color badgeColor = {0x9A, 0xA0, 0xA6, 0xFF};
    QVector<OverlayRow> rows;
    QString statusLine;
    SDL_Color statusColor = {0x80, 0x80, 0x80, 0xFF};
    QVector<OverlayBadge> footerBadges;
};

// One figure of the bar (8.3.0, issue #47): a label, its value and its unit. `key` names the
// value's slot so the bar keeps its width from one second to the next (see buildBarSurface).
// `warn` draws it in red.
struct OverlayBarPart {
    int key = 0;
    QString label;
    QString value;
    QString unit;
    bool warn = false;
};

// One entry between the bar's dividers. Usually one part; the host metrics have two (GPU, CPU).
// An entry is never split across lines.
struct OverlayBarEntry {
    QVector<OverlayBarPart> parts;
};

struct OverlayBar {
    QVector<OverlayBarEntry> entries;
};

// The card (8.3.0): the figures in sections, a label and a value per row.
struct OverlayCardRow {
    QString label;
    QString value;
    bool warn = false;
};

struct OverlayCardSection {
    QString title;
    QVector<OverlayCardRow> rows;
};

struct OverlayCard {
    QVector<OverlayCardSection> sections;
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
    // Plain lines on a small card: the in-stream notices (OverlayStatusUpdate).
    void updateOverlayText(OverlayType type, const char* text);
    // The Stream Settings panel.
    void updateOverlayPanel(OverlayType type, const OverlayPanel& panel);
    // The performance overlay as a bar (Settings > Overlay > Layout = Bar) or as the card.
    void updateOverlayBar(OverlayType type, const OverlayBar& bar);
    void updateOverlayCard(OverlayType type, const OverlayCard& card);
    void setOverlayTextUpdated(OverlayType type);
    void setOverlayState(OverlayType type, bool enabled);
    SDL_Surface* getUpdatedOverlaySurface(OverlayType type);

    void setOverlayRenderer(IOverlayRenderer* renderer);

    // The host's name on the card's chip, set once by the Session as it is built.
    void setHostName(const QString& name) { m_HostName = name; }

    /**
     * Where an overlay of @p surfaceWidth × @p surfaceHeight goes in a viewport of
     * @p viewportWidth × @p viewportHeight, as a top-left corner with y growing downwards —
     * renderers whose y grows upwards flip it. One answer for all of them (8.3.0):
     *  - the card: the top corner the user chose (Settings > Overlay > Position);
     *  - the bar: the top centre;
     *  - the Stream Settings panel: always the bottom centre, so it never meets either;
     *  - the notices: the bottom left.
     * The margin follows the viewport's height, like every other measure of the overlays.
     */
    static SDL_Rect overlayRect(OverlayType type, int viewportWidth, int viewportHeight,
                                int surfaceWidth, int surfaceHeight);

    /**
     * The size the overlays are drawn into, from the renderer. Every measure of the overlays
     * is a fraction of the height (1 unit = 1 px at 1080p), so they keep their proportions at
     * any resolution; the bar wraps between entries past 85% of the width. Renderers call it
     * whenever their output size is known or changes.
     */
    static void setViewportSize(int width, int height);

private:
    void notifyOverlayUpdated(OverlayType type);
    SDL_Surface* buildTextSurface(OverlayType type);
    SDL_Surface* buildPanelSurface(OverlayType type);
    SDL_Surface* buildBarSurface(OverlayType type);
    SDL_Surface* buildCardSurface(OverlayType type);

    enum Kind { KindText, KindPanel, KindBar, KindCard };

    // POD only — this array is memset-zeroed in the constructor. The structured content lives
    // in the arrays below.
    struct {
        bool enabled;
        int kind;
        // 2048 since 6.0.0, when this held the whole performance overlay; it holds only the
        // notices now, which are a line or two.
        char text[2048];
        SDL_Surface* surface;
    } m_Overlays[OverlayMax];
    OverlayPanel m_Panels[OverlayMax];
    // ⚠️ Bars and cards under m_ContentLock: the decoder thread writes them every second, and
    // the overlay hotkey rebuilds the surface from the main thread when it switches the
    // overlay back on.
    OverlayBar m_Bars[OverlayMax];
    OverlayCard m_Cards[OverlayMax];
    QMutex m_ContentLock;
    // Widest value each slot has shown in this session — bar parts by key, card rows by label.
    // The bar is centred and the card can sit in the right corner, so a width that followed
    // the digits would move an edge every second; a slot only grows, and only until the figure
    // has been at its widest once. Cleared when the unit changes (size or window).
    QHash<int, int> m_BarSlotWidths;
    QHash<QString, int> m_CardSlotWidths;
    double m_SlotUnit = 0;
    // The app's accent as it is in the dark theme — the overlays always sit on video —,
    // and what reads on it. Taken when the stream starts, on the thread the theme lives on.
    QColor m_Accent;
    QColor m_OnAccent;
    QString m_HostName;
    IOverlayRenderer* m_Renderer;
};

}
