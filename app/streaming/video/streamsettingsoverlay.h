#pragma once

#include <QString>
#include <QVector>
#include "settings/streamingpreferences.h"

// In-stream "Stream Settings" overlay (4.4.0): a navigable panel rendered
// top-right over the live video that lets the user change resolution / fps /
// bitrate / HDR while streaming. Changes are applied host-agnostically by a
// resume reconnect (Session::requestReconfigure), so a brief "blip" applies the
// whole batch. Driven entirely by SdlInputHandler (keyboard + controller); the
// panel itself is rendered through the existing text-overlay path
// (OverlayManager / OverlayStreamSettings), matching the stats overlay look.
//
// Frame pacing is set at decoder/renderer init, so it rides the same reconnect
// as the other rows rather than changing live. It is shown read-only when V-Sync
// is off, since the decoder ignores the mode entirely in that case.
class StreamSettingsOverlay
{
public:
    explicit StreamSettingsOverlay(StreamingPreferences* prefs);

    bool isActive() const { return m_Active; }

    void open();    // seed from current prefs, show the panel
    void close();   // hide the panel

    // Navigation, called by the input handler while active.
    void navUp();
    void navDown();
    void navLeft();
    void navRight();
    void apply();   // A — apply changed params (reconnect) then close
    void save();    // Y — persist to the active profile layer + apply
    void cancel();  // B — close without applying

private:
    // The most-specific active profile layer the Save writes to.
    enum SaveTarget { TARGET_GAME, TARGET_HOST, TARGET_GLOBAL };
    SaveTarget activeTarget(int* hostSlot = nullptr) const;
    QString targetLabel() const;
    QString saveLabel() const;   // vendor north-button glyph name (Y / Triangle / X)
    void writeCurrentTo(SaveTarget target, int hostSlot);

    enum RowId {
        ROW_RES,
        ROW_CUSTOM_W,   // only present when resolution == Custom
        ROW_CUSTOM_H,
        ROW_FPS,
        ROW_BITRATE,
        ROW_HDR,
        ROW_PACING,
    };

    void rebuildRows();
    bool isRowLocked(int rowId) const;
    void moveFocus(int delta);
    void changeFocused(int delta);
    void render();
    bool isCustom() const;
    int  effectiveWidth() const;
    int  effectiveHeight() const;
    int  changeCount() const;
    QString confirmLabel() const;
    QString cancelLabel() const;

    StreamingPreferences* m_Prefs;
    bool m_Active = false;

    // model
    int m_ResIndex = 0;     // index into s_ResPresets (last entry = Custom)
    int m_CustomW = 1920;
    int m_CustomH = 1080;
    int m_FpsIndex = 0;     // index into s_FpsPresets
    int m_BitrateKbps = 20000;
    bool m_Hdr = false;
    int m_PacingIndex = 0;  // index into s_PacingValues (FP_OFF/AUTO/MATCHED/MULTIPLE)

    // committed baseline (seeded values) for change detection
    int m_BaseW = 0, m_BaseH = 0, m_BaseFps = 0, m_BaseKbps = 0;
    bool m_BaseHdr = false;
    int m_BasePacing = 0;

    int m_Focus = 0;
    QVector<int> m_Rows;    // visible RowId order
};
