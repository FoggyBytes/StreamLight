import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.2

import StreamingPreferences 1.0
import SystemProperties 1.0

// Per-game settings overrides (StreamLight 4.0.0). Each control has a "Global"
// option meaning "inherit the global setting". SegmentedSelector rows use index 0
// as Global; the Bitrate row uses a Global pill + slider inside the same pill
// container so it matches the other rows. Saves live on every change. Fully
// d-pad navigable (rows → footer Done / Reset to Global).
Popup {
    id: dlg

    // Shared dialog measurements — see Theme.uiScale.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    property var appModel: null
    property int appIndex: -1
    property string appName: ""
    // The game's box art, for the header (6.0.0). Empty for an app without one — the header
    // then falls back to the plain settings glyph rather than drawing an empty frame.
    property string cover: ""
    // Name of the host's currently-active profile (empty when none). When set,
    // the "inherit" option (index 0) is labelled with it instead of "Global",
    // since per-game settings cascade on top of the active profile.
    property string activeProfileName: ""

    // Effective V-Sync for this host (its active profile, else global). Frame pacing
    // has no effect without it, and V-Sync is not a per-game setting — so the row below
    // greys itself against this rather than pretending the choice will be honoured.
    property bool effectiveVsync: true

    // Emitted when the dialog closes, so the opener can restore gamepad focus
    // to the grid behind it.
    signal closedByUser()

    readonly property color _accent: Theme.accent
    readonly property color _text:   Theme.text
    readonly property color _dim:    Theme.text2
    readonly property color _line:   Theme.line
    // ⚠️ Measurements, so they scale — see the note on the same pair in
    // HostProfilesDialog. Raw pixels here meant the row stopped growing while the
    // controls inside it kept going, which is what made the profile Name field
    // overflow its row on a large screen.
    readonly property int   _rowH:   _px(52)
    readonly property int   _padX:   _px(28)

    // What the scrolling row list does NOT get: header, footer, and enough margin
    // that the dialog never touches the top and bottom of the screen. Same note as
    // HostProfilesDialog — this was a flat 120 px while everything it stands for
    // scaled, so on a large screen the popup ran off both ends. One row row fewer
    // than the profiles dialog, which also has the profile tabs.
    //
    // 6.0.0: plus the section tabs, which sit above the scrolling list, and the prompt bar
    // above the footer buttons.
    readonly property int   _promptH: _px(32)
    // 6.0.0: the header grew from 44 to carry the game's cover and a two-line title.
    readonly property int   _headerH: _px(92)
    readonly property int   _chromeH: _headerH + _px(52) + _promptH + _px(48) + _px(48)

    // Label for the index-0 "inherit" option: the active profile's name, or "Global".
    readonly property string _inheritLabel: activeProfileName.length > 0 ? activeProfileName : qsTr("Global")

    // ── Sections (6.0.0) ─────────────────────────────────────────────────────
    /*
     * The rows split into the tabs of Settings, with Settings' own names and order — the
     * same arrangement as HostProfilesDialog, minus Network: link matching is not per game.
     * ⚠️ The row -> tab map was measured on SettingsScreen.qml (HDR is under Decoder there).
     */
    readonly property var _sections: [
        { label: qsTr("Video")   },
        { label: qsTr("Audio")   },
        { label: qsTr("Decoder") },
        { label: qsTr("Session") }
    ]
    /*
     * How many rows each tab holds of this game's own, read off the same indices
     * saveToModel() reads. A number rather than the dot it was until 6.0.0 — the twin of
     * HostProfilesDialog._sectionCounts, and for the reason given there.
     */
    readonly property var _sectionCounts: [
        _n(resSel.currentIndex !== 0 || _customResW > 0) + _n(fpsSel.currentIndex !== 0 || _customFps > 0)
            + _n(_bitrateOverridden) + _n(fpSel.currentIndex > 0),
        _n(audSel.currentIndex > 0),
        _n(codecSel.currentIndex > 0) + _n(hdrSel.currentIndex > 0),
        _n(waitGameSel.currentIndex > 0) + _n(hueSel.currentIndex > 0)
    ]

    readonly property int _totalCount: {
        var t = 0
        for (var i = 0; i < _sectionCounts.length; i++) t += _sectionCounts[i]
        return t
    }

    // The row the cursor is on, so the footer prints Y only when there is something to give
    // back. Written by SettingRow — see the note on the twin in HostProfilesDialog.
    property Item _focusedRow: null

    // Where the D-pad enters and leaves each tab: up from the footer lands on the last row.
    function _firstOfSection(i) { return [resSel, audSel, codecSel, waitGameSel][i] || resSel }
    function _lastOfSection(i)  { return [fpSel, audSel, hdrSel, hueSel][i] || fpSel }

    // Whether the pad focus was inside the rows, kept by the Flickable below.
    property bool _focusInBody: false

    // LB/RB. Same rule as HostProfilesDialog: from inside the rows the focus moves to the new
    // tab's first row; from the footer it stays put. ⚠️ Read before switching — see there.
    function _cycleSection(dir) {
        var inBody = _focusInBody
        _focusedRow = null
        sectionBar.cycle(dir)
        if (inBody) _firstOfSection(sectionBar.currentIndex).forceActiveFocus()
    }

    function _isInside(item, container) {
        for (var p = item; p; p = p.parent) if (p === container) return true
        return false
    }

    // After a switch made with the mouse: a focused row that is now hidden has taken the focus
    // out of the dialog — put it on the new tab's first row.
    function _refocusAfterSectionChange() {
        if (!dlg.opened) return
        var f = flick.activeFocusItem
        if (!f || !f.visible || !dlg._isInside(f, dlg.contentItem))
            dlg._firstOfSection(sectionBar.currentIndex).forceActiveFocus()
    }

    /*
     * ── Play time (5.7.0) ────────────────────────────────────────────────────
     *
     * Only whether there IS a record, now. This panel used to print the whole of it — hours,
     * last session, FPS against target, drops, jitter drops, RTT, host latency, decode — in a
     * two-row block of eight figures with its own heading and its own Reset button. The panel
     * is for settings, and eight read-only measurements in the middle of it made it a report
     * that happened to have switches in it.
     *
     * The figures the user actually looks for are the ones on the host page, where the game is
     * already the subject. What is left here is the button that clears them, in the footer with
     * the other two, because the panel is still the only place in the app that is about THIS
     * game and nothing else.
     *
     * ⚠️ Keep it a map, not a bool. `playtimeFor` returns an empty map both when the game has
     * never been streamed and when it is one of the entries that are never tracked, and the
     * footer button greys itself on exactly that: nothing to clear, nothing to press.
     */
    property var _playtime: ({})

    // Whether there is anything for the Reset button to delete.
    readonly property bool _hasPlaytime: _playtime.total !== undefined

    /*
     * What the level below actually holds, keyed the way the override map is:
     * {"resolution": "1080p", "fps": "60", …}. Filled on open from
     * AppModel::inheritedLabels(), which is the global settings with this host's active
     * profile on top.
     *
     * ⚠️ Without this the inherit option was a word and nothing else. Choosing it told the
     * user their game would follow "Global" — and the only way to learn what Global was
     * came to leaving the dialog, opening Settings, reading the value and coming back.
     * The same is true of a profile name, and worse, because a profile is a partial set:
     * "Docked" says nothing at all about the frame rate it does not override.
     */
    property var _inheritedValues: ({})

    /*
     * The caption under a row's label: where the value comes from — "Docked 4K", "Global" —
     * and, while this game holds one of its own, what it would go back to.
     *
     * ⚠️ This replaces the inherit PILL of 5.x, which was index 0 of the strip and was drawn
     * selected, in the accent. A game with no overrides showed eight accent pills and read
     * as eight overrides, which is the opposite of what the panel is for. The value keeps
     * being printed — that was the point of carrying it in the pill (§60) and it stands —
     * but it moved to a caption, and the accent now marks only what this game changes.
     *
     * The name matters here in a way it does not in the host dialog: per-game settings sit
     * on top of the host's active profile, so this says "Docked 4K: 120" and the user can
     * see they are overriding a profile rather than the global settings.
     */
    function _srcText(key, overridden) {
        var v = _inheritedValues[key]
        if (!overridden || v === undefined || v === "") return _inheritLabel
        return _inheritLabel + ": " + v
    }

    /*
     * Which option on the strip equals the inherited value, so that pill can carry a neutral
     * highlight while the row is inherited — and, once the row is overridden, a small mark
     * showing where it would go back to.
     *
     * The comparison is against the printed label on purpose. inheritedLabels() spells its
     * values in the same vocabulary these tables use — "1080p", "60", "HEVC", "Stereo" — so
     * a value with no matching option simply answers -1: a custom inherited resolution, or a
     * codec inherited as Auto, which is not one of the choices here. Then nothing is
     * highlighted and the caption carries the value, which is the honest answer.
     *
     * ⚠️ Unlike the _dupIndices it replaces, this hides NOTHING. An override can legitimately
     * hold the same value the level below holds — the user pinned it, and the two agreeing
     * today says nothing about tomorrow — so every option stays on the strip and stays
     * selectable. SegmentedSelector reads a press on the inherited one as "give it back".
     */
    function _inheritIdx(labels, key) {
        var v = _inheritedValues[key]
        if (v === undefined || v === "") return -1
        for (var i = 1; i < labels.length; i++)
            if (labels[i] === v) return i
        return -1
    }

    function _n(b) { return b ? 1 : 0 }

    /*
     * The resolution and frame-rate values on offer (5.5.0): our presets plus whatever this
     * machine's displays report, built in C++ so this panel, the host-profile panel and the
     * Settings screen cannot drift apart — see settings/videooptions.h.
     *
     * ⚠️ The tables below stay index-parallel, and index 0 stays the inherit placeholder.
     * Every lookup in this file is by index, so the offset has to be applied once, here,
     * and never again.
     */
    readonly property var _video: SystemProperties.videoOptions()

    // Native entries, shifted by the inherit pill sitting at index 0.
    function _nativeIndices(entries) {
        var out = []
        for (var i = 0; i < entries.length; i++)
            if (entries[i].isNative) out.push(i + 1)
        return out
    }

    // value tables (index 0 == the inherit slot, never drawn — see SegmentedSelector)
    //
    // ⚠️ Index 0 is an empty string and stays in the array: it is the stored "no override"
    // state, and every lookup in this file is by index. Since 6.0.0 it carries no text, so
    // the three On/Off tables are identical in content again — kept apart anyway, so that
    // one of them gaining a third option cannot grow it on the other two.
    readonly property var _resLabels: [""].concat(
        _video.res.map(function(e) { return e.label }))
    readonly property var _resW:      [0].concat(_video.res.map(function(e) { return e.width }))
    readonly property var _resH:      [0].concat(_video.res.map(function(e) { return e.height }))
    readonly property var _resNative: _nativeIndices(_video.res)
    readonly property var _fpsLabels: [""].concat(
        _video.fps.map(function(e) { return e.label }))
    readonly property var _fpsVals:   [0].concat(_video.fps.map(function(e) { return e.value }))
    readonly property var _fpsNative: _nativeIndices(_video.fps)
    readonly property var _hdrLabels: ["", "On", "Off"]
    readonly property var _waitLabels: ["", "On", "Off"]
    readonly property var _hueLabels:  ["", "On", "Off"]
    readonly property var _codecLabels: ["", "H.264", "HEVC", "AV1"]
    readonly property var _codecVals:   [-1, 1, 2, 4]   // VCC_FORCE_H264/HEVC/AV1
    readonly property var _fpLabels:  ["", "Off", "On"]
    readonly property var _fpVals:    [-1, 0, 1]  // FP_OFF / FP_ON
    readonly property var _audLabels: ["", "Stereo", "5.1", "7.1"]
    readonly property var _audVals:   [-1, 0, 1, 2]     // AC_STEREO/51/71

    // Bitrate override state (kbps)
    property bool _bitrateOverridden: false
    // Custom resolution override (0 == none / inheriting or using a preset).
    property int _customResW: 0
    property int _customResH: 0
    // Custom frame-rate override, same tri-state (5.5.0).
    property int _customFps: 0

    modal: true
    dim: true
    focus: true                       // grab keyboard/gamepad focus when shown
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    /*
     * 780 until 5.3.0, when the inherit pill started carrying its value. _px is proportional
     * (uiScale is width/1330), so this is 72% of the window rather than a fixed size — it does
     * not creep towards full screen on a large one.
     *
     * The number came from measuring the widest row — Resolution, its pill strip plus the
     * Custom button, with a 14-character profile name and a custom resolution inside the first
     * pill: 574 px of controls at scale 1.0, against the 723 px the whole row then needs. 780
     * left 27 px of slack; 960 leaves about 237.
     *
     * ⚠️ That slack used to vary with the window, because the pill strip was drawn at a fixed
     * size while this width scaled — so the tightest case was the SMALLEST window, which is
     * not where anyone looks for a layout problem. Since SegmentedSelector took the scale too,
     * both sides grow by the same factor and the proportion is the same everywhere. Widening
     * this further is no longer a fix for anything; if a row overflows, it overflows at every
     * size and the row is what to look at.
     */
    width: dlg._px(960)
    padding: dlg._px(0)

    Overlay.modal: Rectangle { color: "#cc000000" }

    background: Rectangle {
        color: Theme.card
        radius: dlg._px(14)
        border.color: Theme.line
        border.width: 1
    }

    onOpened: {
        loadFromModel()
        // Always from Video, whatever tab was open last time.
        sectionBar.currentIndex = 0
        flick.contentY = 0
        resSel.forceActiveFocus()
    }
    onClosed: dlg.closedByUser()

    function _idxByVal(arr, v) {
        var i = arr.indexOf(v)
        return i > 0 ? i : 0
    }

    function loadFromModel() {
        if (!appModel || appIndex < 0) return
        // Read once per opening, not per row: the level below cannot change while this
        // dialog is up, and every label table binds to it.
        _inheritedValues = appModel.inheritedLabels()
        // Same reasoning, same moment: read once per opening. It cannot change while the
        // dialog is up — a session is the only thing that moves it, and one cannot start
        // from here.
        _playtime = appModel.playtimeFor(appIndex)
        var ov = appModel.getAppOverride(appIndex)
        // Resolution is tri-state: inherit (0) / preset (>0) / custom (-1 + _customRes*).
        _customResW = 0; _customResH = 0
        if (ov.width !== undefined) {
            /*
             * ⚠️ Matched on the pair, not by width. This was `_resW.indexOf(ov.width)`
             * checked against the height afterwards, which was safe only while the four
             * presets had four different widths. A native 2560x1600 shares its width with
             * the 1440p preset, so indexOf would answer 1440p, the height check would fail,
             * and a perfectly listed resolution would come back as "Custom".
             */
            var rpi = -1
            for (var ri = 1; ri < _resW.length; ri++) {
                if (_resW[ri] === ov.width && _resH[ri] === ov.height) {
                    rpi = ri
                    break
                }
            }
            if (rpi > 0) {
                resSel.currentIndex = rpi
            } else {
                resSel.currentIndex = -1
                _customResW = ov.width
                _customResH = ov.height
            }
        } else {
            resSel.currentIndex = 0
        }
        // Frame rate is tri-state exactly like resolution: inherit (0) / listed (>0) /
        // custom (-1 + _customFps).
        _customFps = 0
        if (ov.fps !== undefined) {
            var fpi = _fpsVals.indexOf(ov.fps)
            if (fpi > 0) {
                fpsSel.currentIndex = fpi
            } else {
                fpsSel.currentIndex = -1
                _customFps = ov.fps
            }
        } else {
            fpsSel.currentIndex = 0
        }
        hdrSel.currentIndex   = (ov.hdr !== undefined) ? (ov.hdr ? 1 : 2) : 0
        codecSel.currentIndex = (ov.codec !== undefined) ? _idxByVal(_codecVals, ov.codec) : 0
        fpSel.currentIndex    = (ov.framepacing !== undefined) ? _idxByVal(_fpVals, ov.framepacing) : 0
        audSel.currentIndex   = (ov.audio !== undefined) ? _idxByVal(_audVals, ov.audio) : 0
        hueSel.currentIndex   = (ov.hue !== undefined) ? (ov.hue ? 1 : 2) : 0
        waitGameSel.currentIndex = (ov.waitgame !== undefined) ? (ov.waitgame ? 1 : 2) : 0

        _bitrateOverridden = (ov.bitrate !== undefined && ov.bitrate >= bitrateSlider.from)
        bitrateSlider.value = _bitrateOverridden ? ov.bitrate
                            : Math.max(bitrateSlider.from, StreamingPreferences.bitrateKbps)
    }

    function saveToModel() {
        if (!appModel || appIndex < 0) return
        var m = {}
        if (_customResW > 0)              { m.width = _customResW; m.height = _customResH }
        else if (resSel.currentIndex > 0) { m.width = _resW[resSel.currentIndex]; m.height = _resH[resSel.currentIndex] }
        if (_customFps > 0)               m.fps = _customFps
        else if (fpsSel.currentIndex > 0) m.fps = _fpsVals[fpsSel.currentIndex]
        if (_bitrateOverridden && bitrateSlider.value >= bitrateSlider.from)
                                       m.bitrate = Math.round(bitrateSlider.value)
        if (hdrSel.currentIndex > 0)   m.hdr = (hdrSel.currentIndex === 1)
        if (codecSel.currentIndex > 0) m.codec = _codecVals[codecSel.currentIndex]
        if (fpSel.currentIndex > 0)    m.framepacing = _fpVals[fpSel.currentIndex]
        if (audSel.currentIndex > 0)   m.audio = _audVals[audSel.currentIndex]
        if (hueSel.currentIndex > 0)   m.hue = (hueSel.currentIndex === 1)
        if (waitGameSel.currentIndex > 0) m.waitgame = (waitGameSel.currentIndex === 1)
        appModel.setAppOverride(appIndex, m)
    }

    function setBitrateGlobal() {
        _bitrateOverridden = false
        bitrateSlider.value = Math.max(bitrateSlider.from, StreamingPreferences.bitrateKbps)
        saveToModel()
    }
    function setBitrateOverride() {
        if (!_bitrateOverridden) _bitrateOverridden = true
        saveToModel()
    }

    function resetAll() {
        if (appModel && appIndex >= 0) appModel.clearAppOverride(appIndex)
        loadFromModel()
    }

    /*
     * One footer prompt. The twin of HostProfilesDialog's, declared at the top level for the
     * same reason SettingRow is — and an Item around the row, with `clickable`, for the reason
     * given there: a MouseArea inside a RowLayout is one of its cells.
     */
    component Prompt: Item {
        id: prompt
        property string buttonKey: ""
        property string keyLabel: ""
        property string action: ""
        property bool clickable: false
        signal clicked()

        implicitWidth: promptRow.implicitWidth
        implicitHeight: promptRow.implicitHeight

        RowLayout {
            id: promptRow
            anchors.fill: parent
            spacing: dlg._px(6)
            ActionHint {
                Layout.alignment: Qt.AlignVCenter
                buttonKey: prompt.buttonKey
                keyLabel: prompt.keyLabel
                size: dlg._px(18)
            }
            Label {
                Layout.alignment: Qt.AlignVCenter
                text: prompt.action
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption)
                color: Theme.text3
            }
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -dlg._px(6)
            enabled: prompt.clickable
            cursorShape: Qt.PointingHandCursor
            onClicked: prompt.clicked()
        }
    }

    // One row of the list: label, optional reason, control on the right. Declared here at the
    // dialog's top level rather than inside the StackLayout that uses it (6.0.0) — nested there,
    // the linter takes the declaration for a child the layout manages. (Not worded "qmllint …"
    // at the start of a line: qmllint reads such a comment as one of its own directives.)
    component SettingRow: Item {
        id: row
        width: flick.width
        // Grows only when a reason is shown, so every other row keeps its height.
        height: row.detail.length > 0
                ? Math.max(dlg._rowH, labelCol.implicitHeight + dlg._px(20))
                : dlg._rowH
        property string label: ""
        // Optional second line, used to say why a row is greyed out. A locked
        // control with no reason given is worse than no lock at all — the user
        // is left guessing which other setting is holding it.
        property string detail: ""
        // Where this row's value comes from (6.0.0) — "Docked 4K", or "Docked 4K: 120" once
        // the game holds something else. See dlg._srcText.
        property string source: ""
        property bool   overridden: false
        signal resetRequested()
        default property alias content: holder.data
        opacity: enabled ? 1.0 : 0.4

        // Whether the cursor is in this row — see the twin in HostProfilesDialog.
        property Item _af: Window.activeFocusItem
        readonly property bool focused: row._af !== null && row.visible
                                        && dlg._isInside(row._af, row)
        onFocusedChanged: if (row.focused) dlg._focusedRow = row

        // Y. Accepted even with nothing to reset: the pad's Y is Key_Hangup, and main.qml
        // opens Settings on it when nobody consumes it.
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Hangup || event.key === Qt.Key_Y) {
                if (row.overridden) row.resetRequested()
                event.accepted = true
            }
        }

        Column {
            id: labelCol
            anchors.left: parent.left; anchors.leftMargin: dlg._padX
            anchors.right: holder.left; anchors.rightMargin: dlg._px(16)
            anchors.verticalCenter: parent.verticalCenter
            spacing: dlg._px(2)
            Label {
                width: parent.width
                text: row.label
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontBody); font.bold: true
                color: dlg._text
                elide: Text.ElideRight
            }
            // No Y prompt on the row — the footer carries the only one. See the twin in
            // HostProfilesDialog for why a second copy here was removed.
            Label {
                width: parent.width
                visible: row.detail.length > 0 || row.source.length > 0
                text: row.detail.length > 0 ? row.detail : row.source
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption)
                color: (row.detail.length === 0 && row.overridden) ? dlg._accent : dlg._dim
                elide: Text.ElideRight
            }
        }
        Item {
            id: holder
            anchors.right: parent.right; anchors.rightMargin: dlg._padX
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
        Rectangle {
            anchors.bottom: parent.bottom
            x: dlg._padX; width: parent.width - dlg._padX * 2; height: 1; color: dlg._line
        }
    }

    contentItem: ColumnLayout {
        spacing: dlg._px(0)

        /*
         * LB/RB · PgUp/PgDn switch the section, and are accepted here so they never reach
         * the host page underneath.
         *
         * Y is swallowed as a last stop (6.0.0): a row consumes it to reset itself, and
         * anywhere else it would reach main.qml, which opens SETTINGS on Key_Hangup. LT/RT
         * are swallowed too — on the host page they switch GAMES/APPS, and there is no
         * profile to cycle from this dialog.
         */
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_F16 || event.key === Qt.Key_PageUp) {
                dlg._cycleSection(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_F17 || event.key === Qt.Key_PageDown) {
                dlg._cycleSection(1)
                event.accepted = true
            } else if (event.key === Qt.Key_Hangup
                       || event.key === Qt.Key_F14 || event.key === Qt.Key_F15) {
                event.accepted = true
            }
        }

        /*
         * ── Header: the game's cover, then what this panel is and whose (6.0.0) ─────
         *
         * The game is the subject here, so it leads — its cover, its name at title size — and
         * "Per-game settings" drops to the caption above it. The line below says what the
         * rows inherit from, which is the question every caption in the list answers one row
         * at a time.
         *
         * ⚠️ HeroCover, not a bare Image: it owns the 2:3 box and the rounded mask, and a
         * thumbnail cropped some other way here would disagree with the same game's cover
         * one screen back. No shadow — at this size it is a smudge, not depth.
         */
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._headerH
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: dlg._padX
                anchors.rightMargin: dlg._padX
                spacing: dlg._px(16)

                HeroCover {
                    visible: dlg.cover !== ""
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: dlg._px(66)
                    Layout.preferredWidth: Math.round(dlg._px(66) * 2 / 3)
                    source: dlg.cover
                    radius: dlg._px(6)
                    shadow: false
                }
                // Without artwork, the settings glyph the header always had.
                Image {
                    visible: dlg.cover === ""
                    source: "qrc:/res/tune.svg"
                    sourceSize.width: 22; sourceSize.height: 22
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: dlg._px(22); Layout.preferredHeight: dlg._px(22)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: dlg._px(2)

                    Label {
                        text: qsTr("PER-GAME SETTINGS")
                        font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption)
                        font.bold: true
                        font.letterSpacing: dlg._u * 1.4
                        color: Theme.text3
                    }
                    Label {
                        Layout.fillWidth: true
                        text: dlg.appName
                        font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontH2); font.bold: true
                        color: dlg._text
                        elide: Text.ElideRight
                    }
                    Label {
                        text: qsTr("Inherits from %1").arg(dlg._inheritLabel)
                        font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall)
                        color: dlg._dim
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

        // ── Sections (6.0.0) ────────────────────────────────────────────────
        // The strip of Settings, with LB/RB drawn at its two ends. See SectionTabBar.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._px(48)

            SectionTabBar {
                id: sectionBar
                anchors.fill: parent
                anchors.leftMargin: dlg._padX
                anchors.rightMargin: dlg._padX
                u: dlg._u
                shoulders: true
                tabs: dlg._sections
                counts: dlg._sectionCounts
                onCurrentIndexChanged: {
                    dlg._focusedRow = null
                    flick.contentY = 0
                    Qt.callLater(dlg._refocusAfterSectionChange)
                }
            }
        }

        // ── Rows, one tab at a time (content-sized; scrolls only on tiny screens) ─
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(
                (Overlay.overlay ? Overlay.overlay.height - dlg._chromeH : dlg._px(800)),
                rowsStack.implicitHeight)
            contentHeight: rowsStack.implicitHeight
            clip: true
            interactive: contentHeight > height

            // Always on when there is more below, off when everything already fits.
            // Same bar as HostProfilesDialog — the two dialogs mirror each other, and
            // a scroll hint that appears in one and not the other is worse than none.
            ScrollBar.vertical: ScrollBar {
                id: rowsScrollBar
                policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn
                                                          : ScrollBar.AlwaysOff
                width: dlg._px(6)
                anchors.right: parent.right
                anchors.rightMargin: dlg._px(7)
                contentItem: Rectangle {
                    radius: width / 2
                    color: rowsScrollBar.pressed ? dlg._accent : dlg._dim
                    opacity: rowsScrollBar.pressed ? 1.0 : 0.7
                }
                background: Rectangle {
                    radius: width / 2
                    color: dlg._line
                }
            }

            // Auto-scroll: keep the focused row in view as the D-pad moves down the
            // list. Without this the cursor walks off the bottom of the clipped
            // viewport and the last rows are selectable but invisible. Same mechanism
            // as SettingsScreen's tab body and HostProfilesDialog — if one changes,
            // change all three.
            property Item activeFocusItem: Window.activeFocusItem
            onActiveFocusItemChanged: {
                if (!activeFocusItem) return
                // Only react to focus that belongs to this Flickable: the footer
                // buttons live outside it.
                var p = activeFocusItem
                var inside = false
                while (p) {
                    if (p === flick) { inside = true; break }
                    p = p.parent
                }
                // Remembered for LB/RB — see _cycleSection. Only a visible item counts: the
                // one a tab switch just hid is not somewhere the focus can be.
                if (activeFocusItem.visible) dlg._focusInBody = inside
                if (!inside) return

                var margin = 12
                var pos    = activeFocusItem.mapToItem(rowsStack, 0, 0)
                var top    = pos.y
                var bottom = pos.y + activeFocusItem.height

                if (top < contentY + margin) {
                    contentY = Math.max(0, top - margin)
                } else if (bottom > contentY + height - margin) {
                    contentY = Math.min(Math.max(0, contentHeight - height),
                                        bottom - height + margin)
                }
            }

            StackLayout {
                id: rowsStack
                width: flick.width
                // Tall as the tallest tab — see HostProfilesDialog — so the footer stays put.
                height: implicitHeight
                currentIndex: sectionBar.currentIndex

                // VIDEO
                Column {
                    spacing: dlg._px(0)

                    SettingRow {
                        label: qsTr("Resolution")
                        overridden: resSel.currentIndex !== 0 || dlg._customResW > 0
                        source: dlg._srcText("resolution", overridden)
                        onResetRequested: {
                            dlg._customResW = 0; dlg._customResH = 0
                            resSel.currentIndex = 0
                            dlg.saveToModel()
                        }
                        Row {
                            spacing: dlg._px(12)
                            SegmentedSelector {
                                id: resSel; labels: dlg._resLabels
                                nativeIndices: dlg._resNative
                                inheritStrip: true
                                inheritIndex: dlg._inheritIdx(dlg._resLabels, "resolution")
                                KeyNavigation.up: doneBtn
                                KeyNavigation.down: resCustomBtn
                                KeyNavigation.right: resCustomBtn
                                // Selecting inherit or a preset clears any custom override.
                                onActivated: { dlg._customResW = 0; dlg._customResH = 0; dlg.saveToModel() }
                            }
                            PillButton {
                                id: resCustomBtn
                                selected: dlg._customResW > 0
                                text: selected ? (dlg._customResW + "×" + dlg._customResH) : qsTr("Custom")
                                onClicked: {
                                    resCustomDialog.initWidth  = dlg._customResW > 0 ? dlg._customResW
                                        : (resSel.currentIndex > 0 ? dlg._resW[resSel.currentIndex] : StreamingPreferences.width)
                                    resCustomDialog.initHeight = dlg._customResH > 0 ? dlg._customResH
                                        : (resSel.currentIndex > 0 ? dlg._resH[resSel.currentIndex] : StreamingPreferences.height)
                                    resCustomDialog.open()
                                }
                                KeyNavigation.up: resSel
                                KeyNavigation.down: fpsSel
                                KeyNavigation.left: resSel
                            }
                        }
                    }
                    SettingRow {
                        label: qsTr("Frame rate")
                        overridden: fpsSel.currentIndex !== 0 || dlg._customFps > 0
                        source: dlg._srcText("fps", overridden)
                        onResetRequested: {
                            dlg._customFps = 0
                            fpsSel.currentIndex = 0
                            dlg.saveToModel()
                        }
                        Row {
                            spacing: dlg._px(12)
                            SegmentedSelector {
                                id: fpsSel; labels: dlg._fpsLabels
                                nativeIndices: dlg._fpsNative
                                inheritStrip: true
                                inheritIndex: dlg._inheritIdx(dlg._fpsLabels, "fps")
                                KeyNavigation.up: resCustomBtn
                                KeyNavigation.down: fpsCustomBtn
                                KeyNavigation.right: fpsCustomBtn
                                // Selecting inherit or a listed value clears any custom override.
                                onActivated: { dlg._customFps = 0; dlg.saveToModel() }
                            }
                            PillButton {
                                id: fpsCustomBtn
                                selected: dlg._customFps > 0
                                text: selected ? String(dlg._customFps) : qsTr("Custom")
                                onClicked: {
                                    customFpsDialog.initFps = dlg._customFps > 0 ? dlg._customFps
                                        : (fpsSel.currentIndex > 0 ? dlg._fpsVals[fpsSel.currentIndex]
                                                                   : StreamingPreferences.fps)
                                    customFpsDialog.open()
                                }
                                KeyNavigation.up: fpsSel
                                KeyNavigation.down: bitrateSlider
                                KeyNavigation.left: fpsSel
                            }
                        }
                    }

                    /*
                     * ── Bitrate ──────────────────────────────────────────────────────
                     *
                     * ⚠️ The inherit pill that used to stand left of this slider is gone
                     * (6.0.0), and with it the mouse's only way to give the value back.
                     * Both of its jobs moved into the row: the caption prints what the
                     * level below holds — the whole point of §60, kept — and the Y prompt
                     * beside it resets, for the pad and the pointer alike.
                     *
                     * ⚠️ It was a PillButton, which had replaced forty lines that rebuilt
                     * PillButton by hand and had already drifted from it (fixed values where
                     * the original scaled). That history is why the pill is not coming back
                     * in any form: the row says it once, in text, for every row.
                     */
                    SettingRow {
                        label: qsTr("Bitrate (Mbps)")
                        overridden: dlg._bitrateOverridden
                        source: dlg._srcText("bitrate", overridden)
                        onResetRequested: dlg.setBitrateGlobal()

                        Row {
                            spacing: dlg._px(16)

                            // Focus ring + slider — replicates Settings → Video → Video bitrate.
                            Item {
                                anchors.verticalCenter: parent.verticalCenter
                                width: dlg._px(240); height: dlg._px(36)

                                Rectangle {   // focus ring (FocusFrame equivalent)
                                    anchors.fill: parent; anchors.margins: -2
                                    radius: dlg._px(6); color: "transparent"
                                    border.color: dlg._accent; border.width: 2
                                    visible: bitrateSlider.activeFocus
                                }

                                Slider {
                                    id: bitrateSlider
                                    anchors.fill: parent
                                    from: 500
                                    to: StreamingPreferences.unlockBitrate ? 500000 : 150000
                                    stepSize: 500
                                    snapMode: Slider.SnapAlways

                                    onMoved: dlg.setBitrateOverride()

                                    // Hold-to-accelerate on ◀/▶ (tap = ±0.5 Mbps, ramps up).
                                    property int _accelDir: 0
                                    property int _accelTicks: 0
                                    Timer {
                                        id: brAccel; interval: 60; repeat: true
                                        onTriggered: {
                                            if (bitrateSlider._accelDir === 0) { stop(); return }
                                            bitrateSlider._accelTicks++
                                            var mult = Math.min(20, 1 + Math.floor(bitrateSlider._accelTicks / 4))
                                            var delta = bitrateSlider._accelDir * bitrateSlider.stepSize * mult
                                            var v = Math.max(bitrateSlider.from, Math.min(bitrateSlider.to, bitrateSlider.value + delta))
                                            if (v !== bitrateSlider.value) { bitrateSlider.value = v; dlg.setBitrateOverride() }
                                        }
                                    }
                                    function _startAccel(dir) {
                                        var v = Math.max(from, Math.min(to, value + dir * stepSize))
                                        if (v !== value) { value = v; dlg.setBitrateOverride() }
                                        _accelDir = dir; _accelTicks = 0; brAccel.start()
                                    }
                                    function _stopAccel() { _accelDir = 0; _accelTicks = 0; brAccel.stop() }
                                    Keys.onPressed: function(event) {
                                        if (event.isAutoRepeat) { event.accepted = true; return }
                                        if (event.key === Qt.Key_Left)  { _startAccel(-1); event.accepted = true }
                                        if (event.key === Qt.Key_Right) { _startAccel(+1); event.accepted = true }
                                    }
                                    Keys.onReleased: function(event) {
                                        if (event.isAutoRepeat) { event.accepted = true; return }
                                        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) { _stopAccel(); event.accepted = true }
                                    }
                                    KeyNavigation.up: fpsCustomBtn
                                    KeyNavigation.down: fpSel

                                    // Where the level below sits, as a position on the groove:
                                    // the one row whose inherited value is a place and not an
                                    // option. Drawn only while the game has its own number —
                                    // otherwise the handle is already there.
                                    readonly property real _inheritPos: {
                                        var kbps = StreamingPreferences.bitrateKbps
                                        var t = (kbps - bitrateSlider.from) / (bitrateSlider.to - bitrateSlider.from)
                                        return Math.max(0, Math.min(1, t))
                                    }

                                    // Verbatim Settings look: solid white track + green handle.
                                    background: Rectangle {
                                        x: bitrateSlider.leftPadding
                                        y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                        width: bitrateSlider.availableWidth
                                        height: dlg._px(3); radius: dlg._px(2)
                                        color: Theme.text

                                        Rectangle {
                                            visible: dlg._bitrateOverridden
                                            width: Math.max(2, dlg._px(2))
                                            height: dlg._px(12)
                                            radius: width / 2
                                            color: Theme.text3
                                            x: bitrateSlider._inheritPos * (parent.width - width)
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                    handle: Rectangle {
                                        x: bitrateSlider.leftPadding + bitrateSlider.visualPosition * (bitrateSlider.availableWidth - width)
                                        y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                        implicitWidth: dlg._px(14); implicitHeight: dlg._px(14); radius: dlg._px(7)
                                        // Accent only while the game holds its own bitrate
                                        // (6.0.0) — see the twin in HostProfilesDialog.
                                        readonly property color _base: dlg._bitrateOverridden ? dlg._accent : Theme.text
                                        color: bitrateSlider.pressed ? Qt.lighter(_base, 1.2)
                                             : bitrateSlider.hovered ? Qt.lighter(_base, 1.1)
                                             :                         _base
                                        border.color: _base; border.width: 1
                                    }
                                }
                            }

                            Label {
                                id: brValue
                                anchors.verticalCenter: parent.verticalCenter
                                width: dlg._px(78)
                                text: (bitrateSlider.value / 1000).toFixed(0) + qsTr(" Mbps")
                                // Accent only when it is this game's own number: inherited,
                                // this reads the level below, and an accent figure there
                                // would claim an override that does not exist.
                                color: dlg._bitrateOverridden ? dlg._accent : Theme.text
                                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall); font.bold: true
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }

                    // Locked when V-Sync is off — the mode would be ignored at runtime
                    // (Session forces FP_OFF without V-Sync). V-Sync is a global-only
                    // preference, so there is nothing to override here to make it apply.
                    // Qt's KeyNavigation skips disabled items, so the chain still works.
                    // ⚠️ Reads the EFFECTIVE V-Sync, not the global one: a host profile can now
                    // turn V-Sync off, and gating on StreamingPreferences.enableVsync would have
                    // left this row enabled while the value it produces goes nowhere. The reason
                    // moved from a suffix on the label into the row's own detail line, so it
                    // reads the same way here as in Settings and in the host profile.
                    SettingRow {
                        label: qsTr("Frame pacing")
                        enabled: dlg.effectiveVsync
                        detail: enabled ? "" : qsTr("Needs V-Sync, which is off for this host.")
                        overridden: fpSel.currentIndex > 0
                        source: dlg._srcText("framepacing", overridden)
                        onResetRequested: { fpSel.currentIndex = 0; dlg.saveToModel() }
                        SegmentedSelector {
                            id: fpSel; labels: dlg._fpLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._fpLabels, "framepacing")
                            KeyNavigation.up: bitrateSlider
                            KeyNavigation.down: doneBtn
                            onActivated: dlg.saveToModel()
                        }
                    }
                }

                // AUDIO
                Column {
                    spacing: dlg._px(0)

                    // ⚠️ Rows follow Settings: the tab they sit under and, within it, their order
                    // there. Kept in step with HostProfilesDialog, which carries the same rows plus
                    // the host-only ones. Each tab is its own KeyNavigation chain — a row moved
                    // between tabs moves in _firstOfSection / _lastOfSection too.
                    SettingRow {
                        label: qsTr("Audio")
                        overridden: audSel.currentIndex > 0
                        source: dlg._srcText("audio", overridden)
                        onResetRequested: { audSel.currentIndex = 0; dlg.saveToModel() }
                        SegmentedSelector {
                            id: audSel; labels: dlg._audLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._audLabels, "audio")
                            KeyNavigation.up: doneBtn
                            KeyNavigation.down: doneBtn
                            onActivated: dlg.saveToModel()
                        }
                    }
                }

                // DECODER — Video codec above HDR, as in Settings
                Column {
                    spacing: dlg._px(0)

                    SettingRow {
                        label: qsTr("Video codec")
                        overridden: codecSel.currentIndex > 0
                        source: dlg._srcText("codec", overridden)
                        onResetRequested: { codecSel.currentIndex = 0; dlg.saveToModel() }
                        SegmentedSelector {
                            id: codecSel; labels: dlg._codecLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._codecLabels, "codec")
                            KeyNavigation.up: doneBtn
                            KeyNavigation.down: hdrSel
                            onActivated: dlg.saveToModel()
                        }
                    }
                    SettingRow {
                        label: qsTr("HDR")
                        overridden: hdrSel.currentIndex > 0
                        source: dlg._srcText("hdr", overridden)
                        onResetRequested: { hdrSel.currentIndex = 0; dlg.saveToModel() }
                        SegmentedSelector {
                            id: hdrSel; labels: dlg._hdrLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._hdrLabels, "hdr")
                            KeyNavigation.up: codecSel
                            KeyNavigation.down: doneBtn
                            onActivated: dlg.saveToModel()
                        }
                    }
                }

                // SESSION
                Column {
                    spacing: dlg._px(0)

                    // Turn it off for a game that opens its own launcher: no game window ever
                    // reaches the host's screen, so the wait has nothing to end on and sits there
                    // until the user presses B. Per game, because only the person who owns the
                    // game knows it behaves that way.
                    SettingRow {
                        label: qsTr("Wait for the game to appear")
                        overridden: waitGameSel.currentIndex > 0
                        source: dlg._srcText("waitgame", overridden)
                        onResetRequested: { waitGameSel.currentIndex = 0; dlg.saveToModel() }
                        SegmentedSelector {
                            id: waitGameSel; labels: dlg._waitLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._waitLabels, "waitgame")
                            KeyNavigation.up: doneBtn
                            KeyNavigation.down: hueSel
                            onActivated: dlg.saveToModel()
                        }
                    }
                    // Sits with the row above because it belongs to the same context: both are
                    // about what happens around the stream rather than to the picture.
                    SettingRow {
                        label: qsTr("Philips Hue")
                        overridden: hueSel.currentIndex > 0
                        source: dlg._srcText("hue", overridden)
                        onResetRequested: { hueSel.currentIndex = 0; dlg.saveToModel() }
                        SegmentedSelector {
                            id: hueSel; labels: dlg._hueLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._hueLabels, "hue")
                            KeyNavigation.up: waitGameSel
                            KeyNavigation.down: doneBtn
                            onActivated: dlg.saveToModel()
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

        /*
         * ── The prompt bar (6.0.0) ───────────────────────────────────────────
         *
         * Same bar as the host dialog, with one prompt instead of four: there is no profile
         * to rename or cycle here. It is inside the dialog because this popup dims the app's
         * status bar, prompts included — see the longer note on the twin.
         *
         * LB/RB are absent on purpose: SectionTabBar draws them at the ends of the strip.
         */
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._promptH

            Prompt {
                anchors.left: parent.left; anchors.leftMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                visible: dlg._focusedRow !== null && dlg._focusedRow.visible
                         && dlg._focusedRow.overridden
                buttonKey: "Y"; keyLabel: "Y"; action: qsTr("Reset row")
                // The mouse's way to the same action — clicking does not move the focus.
                clickable: true
                onClicked: if (dlg._focusedRow) dlg._focusedRow.resetRequested()
            }

            Label {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                text: dlg._totalCount === 0 ? qsTr("No changes")
                    : dlg._totalCount === 1 ? qsTr("1 change")
                    :                         qsTr("%1 changes").arg(dlg._totalCount)
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption)
                font.bold: dlg._totalCount > 0
                color: dlg._totalCount > 0 ? dlg._accent : Theme.text3
            }
        }

        // ── Footer: Reset stats (left) · Reset all + Done (right) ───────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._px(52)

            /*
             * Clears this game's play record — the hours and the session count the host page
             * shows under its title.
             *
             * On the left, away from the two that are not destructive, which is where
             * HostProfilesDialog already puts Remove. Otherwise it is the same button as its
             * neighbours in every respect the eye can measure: same component, same fontSize,
             * same height, same spacing. Red is the ONE difference, and `danger` is what says
             * it — red text at rest, a red ring and tint on focus. Setting a colour by hand
             * here would be a fourth red in an app that spent a release getting down to one.
             *
             * ⚠️ Greyed, not hidden, when the game has never been streamed. A button that
             * disappears makes the footer's layout move between two games; a greyed one says
             * "there is nothing here to clear", which is the actual answer. DialogButton draws
             * that state itself, so it looks the same wherever it happens next.
             */
            DialogButton {
                id: statsBtn
                anchors.left: parent.left; anchors.leftMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Reset stats")
                danger: true
                enabled: dlg._hasPlaytime
                fontSize: 13
                width: dlg._px(104); height: dlg._px(36)
                onActivated: {
                    if (dlg.appModel && dlg.appIndex >= 0)
                        dlg.appModel.resetPlaytime(dlg.appIndex)
                    // Re-read rather than assume: this is what greys the button out, and it
                    // has to agree with the record on disk, not with what we just asked for.
                    dlg._playtime = dlg.appModel ? dlg.appModel.playtimeFor(dlg.appIndex) : ({})
                }
                KeyNavigation.up: dlg._lastOfSection(sectionBar.currentIndex)
                KeyNavigation.right: resetBtn
            }

            Row {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(8)

                /*
                 * ⚠️ "Reset all", not "Reset to Global" — and the change is a correction,
                 * not a shortening. These rows inherit from the host's ACTIVE PROFILE when
                 * there is one, which is what _inheritLabel prints on every caption above:
                 * a button offering to reset "to Global" beside a row that says "Docked 4K"
                 * named a level this dialog does not reset to. The host dialog carries the
                 * same label, so the two say one thing on the same button.
                 */
                DialogButton {
                    id: resetBtn
                    text: qsTr("Reset all")
                    enabled: dlg._totalCount > 0
                    opacity: enabled ? 1.0 : 0.4
                    fontSize: 13
                    width: dlg._px(104); height: dlg._px(36)
                    onActivated: dlg.resetAll()
                    KeyNavigation.up: dlg._lastOfSection(sectionBar.currentIndex)
                    KeyNavigation.left: dlg._hasPlaytime ? statsBtn : null
                    KeyNavigation.right: doneBtn
                }
                DialogButton {
                    id: doneBtn
                    text: qsTr("Done")
                    affirmative: true
                    fontSize: 13
                    width: dlg._px(76); height: dlg._px(36)
                    onActivated: dlg.close()
                    KeyNavigation.up: dlg._lastOfSection(sectionBar.currentIndex)
                    KeyNavigation.left: resetBtn
                }
            }
        }
    }

    // Manual resolution entry for this game's override.
    CustomResolutionDialog {
        id: resCustomDialog
        onAccepted: function(w, h) {
            dlg._customResW = w
            dlg._customResH = h
            resSel.currentIndex = -1
            dlg.saveToModel()
        }
    }

    // Manual frame-rate entry for this game's override (5.5.0).
    CustomFrameRateDialog {
        id: customFpsDialog
        onAccepted: function(fps) {
            dlg._customFps = fps
            fpsSel.currentIndex = -1
            dlg.saveToModel()
        }
    }
}
