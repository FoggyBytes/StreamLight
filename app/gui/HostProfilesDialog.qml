import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.2

import StreamingPreferences 1.0
import SystemProperties 1.0

// Per-host streaming profiles (StreamLight 4.0.0). Up to 3 named profiles per
// host, each overriding a subset of the global StreamingPreferences (same model
// as the per-game dialog). One profile is "active" — applied as a host-level
// override at launch (below any per-game override), shown by the tile chip and
// switched by the LB/RB status-bar shortcut.
//
// Switching the profile tab makes that profile active. Editing name/settings
// writes live. Fully d-pad navigable. Mirrors AppSettingsDialog's look.
Popup {
    id: dlg

    // Shared dialog measurements — see Theme.uiScale.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    property var computerModel: null
    property int pcIndex: -1
    property string hostName: ""

    readonly property color _accent: Theme.accent
    readonly property color _danger: Theme.danger
    readonly property color _text:   Theme.text
    readonly property color _dim:    Theme.text2
    readonly property color _line:   Theme.line
    // ⚠️ These are measurements, so they scale like everything else in the dialog.
    // They used to be raw pixels while their contents were already scaled, which meant
    // the gap between a row and the control inside it shrank as the screen grew: the
    // Name field (_px(38)) fitted a flat 52 px row at 1.0, touched its edges at 1.32,
    // and at 1.60 stood 61 px tall in a 52 px row, bleeding over the separators into
    // the rows above and below. Declaring a constant rather than a binding is what hid
    // them from both conversion passes in §46 — neither looked at property definitions.
    readonly property int   _rowH:   _px(52)
    readonly property int   _padX:   _px(28)
    readonly property int   _tabH:   _px(36)

    // Everything the scrolling row list does NOT get: the header, the profile tab
    // row, the footer, and enough margin that the dialog never touches the top and
    // bottom of the screen.
    //
    // ⚠️ This was a flat 170 px while every piece it stands for scaled with the
    // dialog. At 1.0 that was about right; at 1.60 the chrome really takes ~314 and
    // the popup ran off the screen at both ends, taking the footer buttons with it.
    // Derived from the same constants the chrome is built from, so the two cannot
    // drift apart again.
    //
    // 6.0.0: plus the section tabs while a profile is being edited — they sit outside the
    // scrolling list now, so they are chrome like the rest. The Name row that used to be
    // counted here is gone: the name is edited on the chip, with X.
    readonly property int   _chipsH:  _px(64)
    readonly property int   _promptH: _px(32)
    readonly property int   _chromeH: _px(44) + _chipsH + _promptH + _px(52) + _px(48)
                                      + (_editing ? _px(48) : 0)
    readonly property int   _maxProfiles: 3
    readonly property int   _maxNameLen: 14

    // Currently-edited slot (== active slot). -1 means OFF (no profile active):
    // profiles may still exist, but the host falls back to the global settings.
    property int editingSlot: -1
    property int profileCount: 0
    property var _tabLabels: []
    // How many overrides each stored profile holds, for the chips. The profile being edited
    // uses _totalCount instead — this one is read from the model and would not move while
    // its rows are being changed.
    property var _tabCounts: []
    property bool _loading: false

    /*
     * The row the cursor is on, so the footer can print Y only when there is something to
     * reset. Written by SettingRow when it takes the focus rather than looked up here: the
     * dialog would have to walk the focus chain and match it against fourteen controls, and
     * the row already knows.
     *
     * ⚠️ Cleared on a section switch. The row that a tab change just hid is not somewhere
     * the cursor can be, and a stale pointer would keep offering Y for an invisible row.
     */
    property Item _focusedRow: null
    // True when a profile is selected for editing (not OFF and at least one exists).
    readonly property bool _editing: profileCount > 0 && editingSlot >= 0

    /*
     * What "Global" holds, per row, keyed the way the override map is. Filled on open
     * from ComputerModel::globalLabels(). See the longer note in AppSettingsDialog: a
     * profile that says it follows Global, without saying what Global is, sends the user
     * out to Settings to find out and back again.
     *
     * A profile sits directly on the global settings, so there is no middle level here —
     * this is the global value and nothing else.
     */
    property var _globalValues: ({})

    /*
     * The caption under a row's label: where its value comes from, and — while the profile
     * holds one of its own — what it would go back to.
     *
     * ⚠️ This replaces the "Global · 4K" PILL of 5.x, and the change is the point of the
     * 6.0.0 pass. That pill was index 0 of the strip and was drawn selected, in the accent:
     * an untouched profile showed nine accent pills and read as nine overrides. The value
     * now lives in a caption and the accent is spent only on what this profile changes —
     * see SegmentedSelector.inheritStrip for the other half.
     */
    function _srcText(key, overridden) {
        var v = _globalValues[key]
        if (!overridden || v === undefined || v === "") return qsTr("Global")
        return qsTr("Global") + ": " + v
    }

    /*
     * Which option on the strip equals what Global holds, so the pill can be highlighted
     * neutrally while the row is inherited. -1 when Global holds something the strip does
     * not offer — a custom resolution, a frame rate no display reports — and that is a
     * legitimate answer: then nothing is highlighted and the caption carries the value.
     *
     * ⚠️ Matched on the printed label, like the _dupIndices it replaces: globalLabels()
     * spells the value the same way the strip does, on purpose, so one string comparison
     * settles it for fourteen rows of different types.
     */
    function _inheritIdx(labels, key) {
        var v = _globalValues[key]
        if (v === undefined || v === "") return -1
        for (var i = 1; i < labels.length; i++)
            if (labels[i] === v) return i
        return -1
    }

    function _n(b) { return b ? 1 : 0 }

    // value tables (index 0 == the inherit slot, never drawn) — mirror AppSettingsDialog
    //
    // ⚠️ Index 0 is an empty string and stays in the array. It is the stored "no override"
    // state that saveOverride() and loadSlot() are both written around, so taking it out
    // would shift every index-keyed lookup in the file; SegmentedSelector hides it instead.
    // Since 6.0.0 it carries no text — the value it used to print is in the row's caption —
    // so the six On/Off tables are once again identical in content and could be shared.
    // They are kept apart anyway: the day one of them gains a third option, a shared table
    // would grow it on five other rows.
    /*
     * The resolution and frame-rate values on offer (5.5.0): our presets plus whatever this
     * machine's displays report, built in C++ and shared with the per-game panel and the
     * Settings screen — see settings/videooptions.h.
     *
     * ⚠️ Index 0 stays the Global placeholder and the tables stay index-parallel: every
     * lookup here is by index, so the offset is applied once and never again.
     */
    readonly property var _video: SystemProperties.videoOptions()

    function _nativeIndices(entries) {
        var out = []
        for (var i = 0; i < entries.length; i++)
            if (entries[i].isNative) out.push(i + 1)
        return out
    }

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
    readonly property var _vsyncLabels:   ["", "On", "Off"]
    readonly property var _fracVsyncLabels: ["", "On", "Off"]
    readonly property var _vrrLabels:       ["", "On", "Off"]
    readonly property var _linkLabels:    ["", "On", "Off"]
    readonly property var _waitLabels:    ["", "On", "Off"]
    readonly property var _hueLabels:     ["", "On", "Off"]
    readonly property var _codecLabels: ["", "H.264", "HEVC", "AV1"]
    readonly property var _codecVals:   [-1, 1, 2, 4]
    readonly property var _fpLabels:  ["", "Off", "On"]
    readonly property var _fpVals:    [-1, 0, 1]
    readonly property var _audLabels: ["", "Stereo", "5.1", "7.1"]
    readonly property var _audVals:   [-1, 0, 1, 2]
    readonly property var _dmLabels:  ["", "Fullscreen", "Borderless", "Windowed"]
    readonly property var _dmVals:    [-1, 0, 1, 2]   // WM_FULLSCREEN / _DESKTOP / WINDOWED

    // Effective values of the two settings other rows depend on: this profile's own
    // override when it has one, otherwise the global setting. The dependent rows
    // below grey themselves out against these, so the condition is always visible
    // two rows above the control it governs.
    readonly property int  _effWindowMode: dmSel.currentIndex > 0
                                           ? _dmVals[dmSel.currentIndex]
                                           : StreamingPreferences.windowMode
    readonly property bool _effVsync:      vsyncSel.currentIndex > 0
                                           ? (vsyncSel.currentIndex === 1)
                                           : StreamingPreferences.enableVsync

    // Frame pacing resolved the same way, for the row below it. ⚠️ It carries V-Sync's
    // answer with it rather than reporting the stored value on its own: with V-Sync off
    // the session runs FP_OFF whatever the profile holds, so a row that read this as "on"
    // would offer a choice that could not act. This is the same collapse Session performs
    // when it builds the decoder parameters.
    readonly property bool _effPacing:     _effVsync &&
                                           (fpSel.currentIndex > 0
                                            ? (_fpVals[fpSel.currentIndex] === StreamingPreferences.FP_ON)
                                            : StreamingPreferences.framePacingMode !== StreamingPreferences.FP_OFF)

    // VRR resolved the same way (6.0.0). It needs V-Sync, and it switches Fractional
    // V-Sync off: the two are mutually exclusive and VRR wins. That resolution lives in
    // Session::snapshotPresentationSettings(); this only lets the rows say so.
    readonly property var  _vrrRec:        SystemProperties.vrrRecommendation()
    readonly property int  _effFps:        _customFps > 0 ? _customFps
                                           : (fpsSel.currentIndex > 0 ? _fpsVals[fpsSel.currentIndex]
                                                                      : StreamingPreferences.fps)
    readonly property bool _vrrOn:         vrrSel.currentIndex > 0 ? (vrrSel.currentIndex === 1)
                                                                    : StreamingPreferences.enableVrr
    // Above the display's refresh Session refuses VRR, so nothing is forced — same
    // exception as SettingsScreen._vrrActive.
    readonly property bool _vrrOverRate:   _effFps > (_vrrRec.refreshHz || 100000)
    // ⚠️ The rows VRR commands lock and say the forced value in `detail`, but their
    // currentIndex is NOT forced here as it is in Settings: saveOverride() reads the
    // indices, so forcing them would rewrite the profile the moment any row is saved.
    readonly property bool _effVrr:        _effVsync && _vrrOn && !_vrrOverRate

    property bool _bitrateOverridden: false
    // Custom resolution override for the edited slot (0 == none / using a preset).
    property int _customResW: 0
    property int _customResH: 0
    // Custom frame-rate override, same tri-state (5.5.0).
    property int _customFps: 0
    readonly property bool _hasProfiles: profileCount > 0

    // ── Sections (6.0.0) ─────────────────────────────────────────────────────
    /*
     * The override rows split into the tabs of Settings, with Settings' own names and order,
     * so an option sits under the label it has there. Only tabs with at least one row: Input,
     * Overlay, Shortcuts, StreamTweak and About have nothing a profile can override.
     *
     * ⚠️ The row -> tab map was MEASURED on SettingsScreen.qml, not assumed: HDR sits under
     * Decoder there, not Video, and link matching under Network. Move a row in Settings and
     * it moves here too — and so do _firstOfSection / _lastOfSection below.
     */
    readonly property var _sections: [
        { label: qsTr("Video")   },
        { label: qsTr("Audio")   },
        { label: qsTr("Decoder") },
        { label: qsTr("Network") },
        { label: qsTr("Session") }
    ]
    /*
     * How many rows each tab holds that are not Global, read off the same indices
     * saveOverride() reads — so the badge and the stored profile cannot disagree.
     *
     * ⚠️ It was a boolean per tab until 6.0.0 ("this tab has something"), which left the
     * user opening five tabs to find the two rows they had changed. Same badge, a number
     * in it. Each row is counted exactly once: the resolution and frame-rate rows are one
     * override whether it came from the strip or from the Custom dialog.
     */
    readonly property var _sectionCounts: [
        _n(resSel.currentIndex !== 0 || _customResW > 0) + _n(fpsSel.currentIndex !== 0 || _customFps > 0)
            + _n(_bitrateOverridden) + _n(dmSel.currentIndex > 0) + _n(vsyncSel.currentIndex > 0)
            + _n(fpSel.currentIndex > 0) + _n(fracVsyncSel.currentIndex > 0) + _n(vrrSel.currentIndex > 0),
        _n(audSel.currentIndex > 0),
        _n(codecSel.currentIndex > 0) + _n(hdrSel.currentIndex > 0),
        _n(linkSel.currentIndex > 0),
        _n(waitGameSel.currentIndex > 0) + _n(hueSel.currentIndex > 0)
    ]

    // The same figure for the whole profile, for the footer and for the chip of the profile
    // being edited. Derived from the live indices rather than from the stored map, so it
    // moves with the row the moment it is changed.
    readonly property int _totalCount: {
        var t = 0
        for (var i = 0; i < _sectionCounts.length; i++) t += _sectionCounts[i]
        return t
    }

    // Where the D-pad enters and leaves each tab: down from Name, up from the footer.
    function _firstOfSection(i) { return [resSel, audSel, codecSel, linkSel, waitGameSel][i] || resSel }
    function _lastOfSection(i)  { return [vrrSel, audSel, hdrSel, linkSel, hueSel][i] || vrrSel }

    // Whether the pad focus was inside the rows, kept by the Flickable below.
    property bool _focusInBody: false

    /*
     * LB/RB. The focus stays on Profile or Name when it is there — you can look through the
     * tabs without losing your place — and moves to the new tab's first row when it was
     * inside the rows, which have just been hidden.
     *
     * ⚠️ Read BEFORE switching: hiding the focused row moves the focus out of the list, and a
     * check made afterwards would always answer "not in the body".
     */
    function _cycleSection(dir) {
        var inBody = _focusInBody
        _focusedRow = null
        sectionBar.cycle(dir)
        if (inBody) _firstOfSection(sectionBar.currentIndex).forceActiveFocus()
    }

    /*
     * LT/RT: the profile, from anywhere in the dialog (6.0.0).
     *
     * Without it the only way to change profile is to walk the cursor back up to the chips,
     * which from the bottom of Video is eight presses each way. The ring includes Off, in
     * the order the chips are drawn, and wraps — same ring the status bar cycles on Home.
     *
     * ⚠️ The triggers send Key_F14/F15, which the host page uses for GAMES/APPS. They are
     * accepted here whatever the state, exactly like LB/RB, so they never reach the page
     * underneath this popup.
     */
    function _cycleProfile(dir) {
        if (profileCount <= 0) return
        // -1 == Off, then one slot per profile.
        var at = editingSlot + 1                       // 0 == Off
        var next = (at + dir + profileCount + 1) % (profileCount + 1)
        if (next === 0) selectOff()
        else selectSlot(next - 1)
    }

    function _isInside(item, container) {
        for (var p = item; p; p = p.parent) if (p === container) return true
        return false
    }

    // After a switch made with the mouse: if the pad focus was on a row that is now hidden,
    // it has left the dialog altogether — put it on the new tab's first row.
    function _refocusAfterSectionChange() {
        if (!dlg.opened || !dlg._editing) return
        var f = flick.activeFocusItem
        if (!f || !f.visible || !dlg._isInside(f, dlg.contentItem))
            dlg._firstOfSection(sectionBar.currentIndex).forceActiveFocus()
    }

    modal: true
    dim: true
    focus: true                       // grab keyboard/gamepad focus when shown
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    // Same widening, and the same measurement, as AppSettingsDialog — read the note there.
    // This dialog's own worst row is Display mode, four pills with "Global · Borderless"
    // in the first: 425 px of controls against the per-game dialog's 574.
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
        // Read once per opening, not per row: Settings cannot be reached from here, so
        // the global values cannot change while this dialog is up.
        if (computerModel) _globalValues = computerModel.globalLabels()
        // Always from Video: the tab left open last time says nothing about this visit.
        sectionBar.currentIndex = 0
        flick.contentY = 0
        if (_editing)          profileTabs.forceActiveFocus()
        else if (_hasProfiles) offBtn.forceActiveFocus()
        else                   addBtn.forceActiveFocus()
    }

    function _idxByVal(arr, v) {
        var i = arr.indexOf(v)
        return i > 0 ? i : 0
    }

    function _refreshTabs() {
        var labels = []
        var counts = []
        for (var i = 0; i < profileCount; i++) {
            labels.push(computerModel.hostProfileName(pcIndex, i))
            counts.push(Object.keys(computerModel.hostProfileSettings(pcIndex, i)).length)
        }
        _tabLabels = labels
        _tabCounts = counts
    }

    // What a chip says under its name. The edited profile reads the live rows; the others
    // read what is stored.
    function _chipCount(slot) {
        if (slot === editingSlot) return _totalCount
        return _tabCounts[slot] !== undefined ? _tabCounts[slot] : 0
    }

    // Reads the host's profile state and (re)loads the whole dialog.
    function reload() {
        if (!computerModel || pcIndex < 0) return
        profileCount = computerModel.hostProfileCount(pcIndex)
        _refreshTabs()
        if (profileCount > 0) {
            editingSlot = computerModel.hostActiveProfile(pcIndex)   // -1 == OFF
            // Cursor: the active pill if any, else the first (so OFF still has
            // a sensible landing spot when you navigate into the tabs).
            profileTabs.currentIndex = editingSlot >= 0 ? editingSlot : 0
            if (editingSlot >= 0) loadSlot(editingSlot)
        } else {
            editingSlot = -1
        }
    }

    function loadSlot(slot) {
        if (slot < 0) return
        _loading = true
        var ov = computerModel.hostProfileSettings(pcIndex, slot)
        // Resolution is tri-state: Global (0) / preset (>0) / custom (-1 + _customRes*).
        _customResW = 0; _customResH = 0
        if (ov.width !== undefined) {
            // ⚠️ Matched on the pair, not by width — see the same spot in
            // AppSettingsDialog.qml for what a native 2560x1600 did to indexOf().
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
        // Frame rate is tri-state exactly like resolution: Global (0) / listed (>0) /
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
        linkSel.currentIndex  = (ov.matchlink !== undefined) ? (ov.matchlink ? 1 : 2) : 0
        waitGameSel.currentIndex = (ov.waitgame !== undefined) ? (ov.waitgame ? 1 : 2) : 0
        dmSel.currentIndex    = (ov.displaymode !== undefined) ? _idxByVal(_dmVals, ov.displaymode) : 0
        vsyncSel.currentIndex = (ov.vsync !== undefined) ? (ov.vsync ? 1 : 2) : 0
        fracVsyncSel.currentIndex = (ov.fractionalvsync !== undefined) ? (ov.fractionalvsync ? 1 : 2) : 0
        vrrSel.currentIndex = (ov.vrr !== undefined) ? (ov.vrr ? 1 : 2) : 0
        _bitrateOverridden = (ov.bitrate !== undefined && ov.bitrate >= bitrateSlider.from)
        bitrateSlider.value = _bitrateOverridden ? ov.bitrate
                            : Math.max(bitrateSlider.from, StreamingPreferences.bitrateKbps)
        _loading = false
    }

    function saveOverride() {
        if (_loading || editingSlot < 0) return
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
        if (linkSel.currentIndex > 0)  m.matchlink = (linkSel.currentIndex === 1)
        if (waitGameSel.currentIndex > 0) m.waitgame = (waitGameSel.currentIndex === 1)
        if (dmSel.currentIndex > 0)    m.displaymode = _dmVals[dmSel.currentIndex]
        // ⚠️ Saved even when the row is greyed out, exactly like Frame pacing under a
        // V-Sync it does not have: the profile keeps the choice it was given, and it
        // starts acting the day the display mode above it becomes Fullscreen. Dropping
        // it here would silently rewrite the profile the moment the condition lapsed.
        if (vsyncSel.currentIndex > 0) m.vsync = (vsyncSel.currentIndex === 1)
        // Same rule again, one level further down the chain: kept even while the row is
        // greyed, so turning V-Sync or Frame pacing back on restores the choice the
        // profile was given instead of a silently rewritten one.
        if (fracVsyncSel.currentIndex > 0) m.fractionalvsync = (fracVsyncSel.currentIndex === 1)
        // Kept while greyed too, for the same reason as the two above.
        if (vrrSel.currentIndex > 0) m.vrr = (vrrSel.currentIndex === 1)
        computerModel.setHostProfileSettings(pcIndex, editingSlot, m)
    }

    // 6.0.0: the name arrives from ProfileNameDialog rather than from a field in this
    // dialog, so it is an argument. _refreshTabs() rebuilds the chip labels, which is what
    // makes the new name appear on the chip and on the host card behind it.
    function saveName(name) {
        if (_loading || editingSlot < 0) return
        computerModel.setHostProfileName(pcIndex, editingSlot, name)
        _refreshTabs()
    }

    // Only the numbers on the chips, not the labels: rebuilding _tabLabels would hand the
    // chip Repeater a new model and recreate every chip — the same trap SectionTabBar's
    // note describes for its tabs.
    function _refreshCounts() {
        if (!computerModel || pcIndex < 0) return
        var counts = []
        for (var i = 0; i < profileCount; i++)
            counts.push(Object.keys(computerModel.hostProfileSettings(pcIndex, i)).length)
        _tabCounts = counts
    }

    function selectSlot(slot) {
        if (slot < 0 || slot >= profileCount) return
        editingSlot = slot
        profileTabs.currentIndex = slot
        computerModel.setHostActiveProfile(pcIndex, slot)
        loadSlot(slot)
        _focusedRow = null
        _refreshCounts()
    }

    // X on a chip, or on the active profile from anywhere in the dialog. The slot has to be
    // the edited one first: the dialog writes the name through saveName(), which works on
    // editingSlot, and a rename that landed on another slot would be silent and wrong.
    function renameProfile(slot) {
        if (slot < 0 || slot >= profileCount) return
        if (slot !== editingSlot) selectSlot(slot)
        nameDialog.initName = computerModel.hostProfileName(pcIndex, slot)
        nameDialog.maxLength = dlg._maxNameLen
        nameDialog.open()
    }

    // "OFF": no profile active — the host falls back to the global settings.
    // Profiles are kept; only the active selection is cleared (cursor unchanged).
    function selectOff() {
        editingSlot = -1
        computerModel.setHostActiveProfile(pcIndex, -1)
        _focusedRow = null
        _refreshCounts()
    }

    function addProfile() {
        if (!computerModel || pcIndex < 0) return
        var slot = computerModel.addHostProfile(pcIndex)
        if (slot < 0) return
        computerModel.setHostActiveProfile(pcIndex, slot)
        reload()
        // Keep focus on the tabs (not the text field) so pad navigation stays sane.
        Qt.callLater(function() { profileTabs.forceActiveFocus() })
    }

    function removeProfile() {
        if (editingSlot < 0) return
        computerModel.removeHostProfile(pcIndex, editingSlot)
        _focusedRow = null
        reload()
        Qt.callLater(function() {
            if (_editing)          profileTabs.forceActiveFocus()
            else if (_hasProfiles) offBtn.forceActiveFocus()
            else                   addBtn.forceActiveFocus()
        })
    }

    function setBitrateGlobal() {
        _bitrateOverridden = false
        bitrateSlider.value = Math.max(bitrateSlider.from, StreamingPreferences.bitrateKbps)
        saveOverride()
    }
    function setBitrateOverride() {
        if (_loading) return
        if (!_bitrateOverridden) _bitrateOverridden = true
        saveOverride()
    }

    function resetAll() {
        if (editingSlot < 0) return
        computerModel.setHostProfileSettings(pcIndex, editingSlot, {})
        loadSlot(editingSlot)
    }

    /*
     * One footer prompt: the button and what it does there. ActionHint decides which of the
     * two devices to draw for, so a call site says the action and never the glyph.
     *
     * ⚠️ Declared at the dialog's top level, beside SettingRow, and for the same reason the
     * note there gives: nested inside the positioner that uses it, qmllint reads the
     * declaration as a child that positioner manages.
     *
     * ⚠️ An Item around a RowLayout, not the RowLayout itself, and `clickable` instead of a
     * MouseArea written at the call site. It was a RowLayout, and Y Reset row put its
     * MouseArea inside it: a child of a layout is a CELL of that layout, so the MouseArea's
     * anchors.fill fought the layout for its geometry — undefined, and qmllint said so. Here
     * the MouseArea is a sibling of the row, outside any layout.
     */
    component Prompt: Item {
        id: prompt
        property string buttonKey: ""
        property string keyLabel: ""
        property string action: ""
        /** Draws a hand cursor and emits clicked(): the mouse's way to the same action. */
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
        // Clicking does not move the focus, so the dialog's idea of the focused row survives.
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
        /*
         * Where this row's value comes from (6.0.0): "Global", or "Global: 4K" once the
         * profile holds something else. It replaces the strip's old first pill — see
         * dlg._srcText. A reason, when there is one, wins the line: it is about why the
         * control cannot act, which matters more than where the value came from.
         */
        property string source: ""
        property bool   overridden: false
        // Y. Raised by the prompt inside the row and by the pad button; the row does not
        // know HOW to undo its own override — the instance wires that to its controls.
        signal resetRequested()
        default property alias content: holder.data
        opacity: enabled ? 1.0 : 0.4

        /*
         * Whether the cursor is in this row, for the Y prompt and for the footer.
         *
         * Read off the window's focus rather than from the control inside: a row holds a
         * selector, sometimes a Custom pill beside it, sometimes a slider, and asking each
         * of them to report upwards would be three wirings per row instead of one here.
         */
        property Item _af: Window.activeFocusItem
        readonly property bool focused: row._af !== null && row.visible
                                        && dlg._isInside(row._af, row)
        onFocusedChanged: if (row.focused) dlg._focusedRow = row

        // Y, wherever the focus sits inside the row. Accepted even with nothing to reset:
        // the pad's Y is Key_Hangup, which main.qml opens Settings on when nobody consumes
        // it — and Settings opening over a profile dialog is the bug that would cause.
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
            // ⚠️ No Y prompt on the row. There was one, beside this caption, and it showed
            // the same prompt twice at once — here and in the footer. The footer's is the one
            // kept, and it is clickable; the row still handles the KEY (Keys.onPressed above).
            Label {
                width: parent.width
                visible: row.detail.length > 0 || row.source.length > 0
                text: row.detail.length > 0 ? row.detail : row.source
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption)
                // The accent says the same thing here as on a pill: this value is the
                // profile's own. A row following Global stays quiet.
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
         * The buttons that work from anywhere in the dialog. All of them are accepted here
         * whatever the state, so none of them reaches the page underneath — where the same
         * keys do other things.
         *
         *   LB/RB · PgUp/PgDn  the section
         *   LT/RT · Q/E        the profile (6.0.0). On the host page the triggers switch
         *                      GAMES/APPS; Q/E are the keys Home cycles a profile with.
         *   X                  rename the active profile. Key_Menu on the pad, the letter on
         *                      a keyboard. A chip under the cursor renames that one instead —
         *                      it handles the key first and never lets it get here.
         *   Y                  the last stop for Key_Hangup. A row consumes it to reset
         *                      itself; anywhere else it would reach main.qml, which opens
         *                      SETTINGS on it. Swallowed here so it cannot.
         */
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_F16 || event.key === Qt.Key_PageUp) {
                if (dlg._editing) dlg._cycleSection(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_F17 || event.key === Qt.Key_PageDown) {
                if (dlg._editing) dlg._cycleSection(1)
                event.accepted = true
            } else if (event.key === Qt.Key_F14 || event.key === Qt.Key_Q) {
                dlg._cycleProfile(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_F15 || event.key === Qt.Key_E) {
                dlg._cycleProfile(1)
                event.accepted = true
            } else if (event.key === Qt.Key_Menu || event.key === Qt.Key_X) {
                if (dlg._editing) dlg.renameProfile(dlg.editingSlot)
                event.accepted = true
            } else if (event.key === Qt.Key_Hangup) {
                event.accepted = true
            }
        }

        // ── Header (title + host name inline) ───────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._px(44)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: dlg._padX
                anchors.rightMargin: dlg._padX
                spacing: dlg._px(12)
                Image {
                    source: "qrc:/res/tune.svg"
                    sourceSize.width: 22; sourceSize.height: 22
                    Layout.preferredWidth: dlg._px(22); Layout.preferredHeight: dlg._px(22)
                }
                Label {
                    text: qsTr("Host profiles")
                    font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontTitle); font.bold: true
                    color: dlg._text
                }
                Label {
                    text: dlg.hostName
                    font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall)
                    color: dlg._dim; elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

        // ── Profile chips: name, overrides, and X to rename (6.0.0) ─────────
        //
        // The three focusable elements are the same as 5.x — Off, the chips, + Add — so the
        // navigation into and out of them is unchanged. What changed is what a chip says: the
        // name, how many settings this profile overrides, and the prompt that renames it. The
        // Name row that used to sit below is gone; see ProfileNameDialog.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._chipsH
            Label {
                anchors.left: parent.left; anchors.leftMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Profile")
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontBody); font.bold: true
                color: dlg._text
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(10)

                // LT at the left end, RT at the right one: the triggers cycle the profile from
                // anywhere in the dialog (_cycleProfile), and this is where they are printed —
                // beside the thing they move, the way SectionTabBar prints LB/RB beside the
                // tabs. They were in the footer's prompt bar first, which put them as far from
                // the chips as the dialog allows. Not focusable: the D-pad never lands on them.
                // Q/E on a keyboard, the same pair Home uses to cycle a host's profile.
                ProfileShoulder {
                    visible: dlg._hasProfiles
                    anchors.verticalCenter: parent.verticalCenter
                    buttonKey: "LT"; keyLabel: "Q"
                    size: dlg._px(26)
                    onTriggered: dlg._cycleProfile(-1)
                }

                // "OFF" pill — leftmost. Selecting it deactivates all profiles
                // (host falls back to the global settings). Styled like one
                // SegmentedSelector pill, mirroring the others.
                //
                // ⚠️ Selected is FILLED WITH THE ACCENT, here and on the chips, the way the
                // lit Open / Profiles / Options buttons are on Home — text in Theme.onAccent,
                // which turns black on a light accent. A raised neutral was tried first, to
                // keep the accent for overrides only, and the active profile was too hard to
                // tell apart from the others (Marcello, 18/09/2026). The profile IS where the
                // overrides live, so the accent is not lying about it.
                //
                // The cursor ring cannot be the accent on a chip that is already accent: there
                // it is drawn in Theme.text instead, so "where you are" still shows.
                FocusScope {
                    id: offBtn
                    activeFocusOnTab: true
                    anchors.verticalCenter: parent.verticalCenter
                    property bool selected: dlg.editingSlot < 0
                    implicitHeight: dlg._px(52)
                    implicitWidth: offLabel.implicitWidth + dlg._px(36)
                    width: implicitWidth; height: implicitHeight

                    Rectangle {
                        anchors.fill: parent
                        radius: dlg._px(8)
                        color: offBtn.selected
                               ? dlg._accent
                               : Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07))
                        border.color: offBtn.activeFocus ? (offBtn.selected ? Theme.text : dlg._accent)
                                    : offBtn.selected    ? dlg._accent
                                    :                      Theme.line
                        border.width: offBtn.activeFocus ? 3 : 1
                    }
                    Label {
                        id: offLabel
                        anchors.centerIn: parent
                        text: qsTr("Off")
                        color: offBtn.selected ? Theme.onAccent : Theme.text2
                        font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall)
                        font.bold: offBtn.selected
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { offBtn.forceActiveFocus(); dlg.selectOff() }
                    }
                    Keys.onReturnPressed: dlg.selectOff()
                    Keys.onEnterPressed:  dlg.selectOff()
                    Keys.onSpacePressed:  dlg.selectOff()
                    KeyNavigation.right: dlg._hasProfiles ? profileTabs : addBtn
                    KeyNavigation.down: dlg._editing ? dlg._firstOfSection(sectionBar.currentIndex)
                                                     : addBtn
                }

                // Profile pills — each its own separate button (not one block),
                // matching the Off pill. One focusable element; ◀/▶ move between
                // pills (and activate them); the active pill is green.
                FocusScope {
                    id: profileTabs
                    visible: dlg._hasProfiles
                    anchors.verticalCenter: parent.verticalCenter
                    activeFocusOnTab: true
                    height: dlg._px(52)
                    property var labels: dlg._tabLabels
                    property int currentIndex: 0
                    signal activated(int index)
                    implicitWidth: tabsRow.implicitWidth
                    width: implicitWidth

                    Row {
                        id: tabsRow
                        spacing: dlg._px(10)
                        Repeater {
                            model: profileTabs.labels
                            delegate: Rectangle {
                                id: ptTile
                                // Room for the widest of the two lines. (There was room for a
                                // rename glyph on the chip too; it moved to the footer — see
                                // the Rename prompt there.)
                                width: Math.max(dlg._px(116),
                                                Math.max(ptLabel.implicitWidth, ptCount.implicitWidth)
                                                + dlg._px(28))
                                height: dlg._px(52)
                                radius: dlg._px(8)
                                property bool _active: index === dlg.editingSlot
                                property bool _cursor: profileTabs.activeFocus && index === profileTabs.currentIndex
                                property int  _count: dlg._chipCount(index)
                                // Accent-filled when active — see the note on offBtn — and a
                                // Theme.text ring for the cursor on top of it.
                                color: ptTile._active
                                       ? dlg._accent
                                       : Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07))
                                border.color: ptTile._cursor ? (ptTile._active ? Theme.text : dlg._accent)
                                            : ptTile._active ? dlg._accent
                                            :                  Theme.line
                                border.width: ptTile._cursor ? 3 : 1

                                Column {
                                    anchors.left: parent.left
                                    anchors.leftMargin: dlg._px(14)
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: dlg._px(1)
                                    Label {
                                        id: ptLabel
                                        text: modelData
                                        color: ptTile._active ? Theme.onAccent : Theme.text2
                                        font.family: Theme.family
                                        font.pixelSize: dlg._px(Theme.fontSmall)
                                        font.bold: ptTile._active
                                    }
                                    // The one number that says whether this profile does
                                    // anything at all. Accent, because it counts overrides —
                                    // except on the active chip, which is accent already:
                                    // there it is the chip's own text colour, a shade quieter.
                                    Label {
                                        id: ptCount
                                        text: ptTile._count === 0 ? qsTr("No changes")
                                            : ptTile._count === 1 ? qsTr("1 change")
                                            :                       qsTr("%1 changes").arg(ptTile._count)
                                        color: ptTile._active ? Qt.rgba(Theme.onAccent.r, Theme.onAccent.g,
                                                                        Theme.onAccent.b, 0.72)
                                             : ptTile._count > 0 ? dlg._accent
                                             :                     Theme.text3
                                        font.family: Theme.family
                                        font.pixelSize: dlg._px(Theme.fontCaption)
                                        font.bold: ptTile._count > 0
                                    }
                                }

                                // ⚠️ No rename glyph on the chip (Marcello, 18/09/2026: an X
                                // inside the chip did not look right). X is printed once, in
                                // the footer's prompt bar, which renames the chip under the
                                // cursor — or the active profile when the cursor is elsewhere.

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        profileTabs.forceActiveFocus()
                                        profileTabs.currentIndex = index
                                        profileTabs.activated(index)
                                    }
                                }
                            }
                        }
                    }

                    onActivated: dlg.selectSlot(currentIndex)
                    // X renames the chip under the cursor. Key_Menu is what the pad's X
                    // sends; the letter is for a keyboard, and ActionHint prints whichever
                    // of the two the user is actually holding.
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Menu || event.key === Qt.Key_X) {
                            dlg.renameProfile(profileTabs.currentIndex)
                            event.accepted = true
                        }
                    }
                    // ◀/▶ move the cursor only — they do NOT activate. A profile is
                    // activated solely by A (Return/Enter/Space) or a click.
                    Keys.onLeftPressed: function(event) {
                        if (currentIndex > 0) { currentIndex--; event.accepted = true }
                        else event.accepted = false
                    }
                    Keys.onRightPressed: function(event) {
                        if (currentIndex < labels.length - 1) { currentIndex++; event.accepted = true }
                        else event.accepted = false
                    }
                    Keys.onReturnPressed: activated(currentIndex)
                    Keys.onEnterPressed:  activated(currentIndex)
                    Keys.onSpacePressed:  activated(currentIndex)
                    KeyNavigation.left: offBtn
                    KeyNavigation.right: addBtn
                    KeyNavigation.down: dlg._editing ? dlg._firstOfSection(sectionBar.currentIndex)
                                                     : addBtn
                }

                // "+ Add" pill — same look/size as the Off pill (just a touch wider).
                FocusScope {
                    id: addBtn
                    activeFocusOnTab: true
                    enabled: dlg.profileCount < dlg._maxProfiles
                    opacity: enabled ? 1.0 : 0.4
                    anchors.verticalCenter: parent.verticalCenter
                    implicitHeight: dlg._px(52)
                    implicitWidth: addPill.implicitWidth + dlg._px(12)
                    width: implicitWidth; height: implicitHeight

                    Rectangle {
                        anchors.fill: parent
                        radius: dlg._px(8)
                        color: Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07))
                        border.color: addBtn.activeFocus ? dlg._accent : Theme.line
                        border.width: addBtn.activeFocus ? 3 : 1
                    }
                    Item {
                        id: addPill
                        anchors.centerIn: parent
                        implicitWidth: addLabel.implicitWidth + dlg._px(28)
                        implicitHeight: dlg._px(30)
                        width: implicitWidth; height: 30
                        Label {
                            id: addLabel
                            anchors.centerIn: parent
                            text: qsTr("+ Add")
                            color: dlg._accent
                            font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall); font.bold: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: addBtn.enabled
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { addBtn.forceActiveFocus(); dlg.addProfile() }
                    }
                    Keys.onReturnPressed: dlg.addProfile()
                    Keys.onEnterPressed:  dlg.addProfile()
                    Keys.onSpacePressed:  dlg.addProfile()
                    KeyNavigation.left: dlg._hasProfiles ? profileTabs : offBtn
                    KeyNavigation.down: dlg._editing ? dlg._firstOfSection(sectionBar.currentIndex)
                                                     : doneBtn
                }

                ProfileShoulder {
                    visible: dlg._hasProfiles
                    anchors.verticalCenter: parent.verticalCenter
                    buttonKey: "RT"; keyLabel: "E"
                    size: dlg._px(26)
                    onTriggered: dlg._cycleProfile(1)
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                x: dlg._padX; width: parent.width - dlg._padX * 2; height: 1; color: dlg._line
            }
        }

        // ── Sections (6.0.0) ────────────────────────────────────────────────
        // The strip of Settings, with LB/RB drawn at its two ends. See SectionTabBar.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._px(48)
            visible: dlg._editing

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

        // ── Empty state (no profiles) ───────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._px(130)
            visible: dlg.profileCount === 0
            Label {
                anchors.centerIn: parent
                width: parent.width - dlg._px(80)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.25
                text: qsTr("No profiles yet.\nAdd one to override streaming settings for this host\n(e.g. a “docked” 4K profile and a “portable” 1080p profile).")
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall)
                color: dlg._dim
            }
        }

        // ── OFF state (profiles exist but none active) ──────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._px(130)
            visible: dlg.profileCount > 0 && !dlg._editing
            Label {
                anchors.centerIn: parent
                width: parent.width - dlg._px(80)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.25
                text: qsTr("No profile active — this host uses your global settings.\nSelect a profile above to activate and edit it.")
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall)
                color: dlg._dim
            }
        }

        // ── Override rows, one tab at a time (only while a profile is selected) ─
        Flickable {
            id: flick
            Layout.fillWidth: true
            visible: dlg._editing
            Layout.preferredHeight: dlg._editing ? Math.min(
                (Overlay.overlay ? Overlay.overlay.height - dlg._chromeH : dlg._px(800)),
                rowsStack.implicitHeight) : 0
            contentHeight: rowsStack.implicitHeight
            clip: true
            interactive: contentHeight > height

            // Always on when there is more below, and drawn rather than left to the
            // stock one: the point of this bar is to say "there is more" to someone
            // who cannot see the bottom of the list, and a bar that fades out a second
            // later says it only to whoever happened to be looking. Off — not merely
            // faded — when everything already fits, so it never implies hidden rows
            // that do not exist.
            ScrollBar.vertical: ScrollBar {
                id: rowsScrollBar
                policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn
                                                          : ScrollBar.AlwaysOff
                width: dlg._px(6)
                anchors.right: parent.right
                anchors.rightMargin: dlg._px(7)
                // Sits inside the _padX gutter, so it never covers a control.
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
            // viewport and the rows below "Match host link speed" are selectable but
            // invisible. Same mechanism as SettingsScreen's tab body — if that one is
            // ever changed, change this one with it.
            property Item activeFocusItem: Window.activeFocusItem
            onActiveFocusItemChanged: {
                if (!activeFocusItem) return
                // Only react to focus that belongs to this Flickable: the dialog's
                // tabs, name field and footer buttons live outside it.
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
                // Tall as the tallest tab (a StackLayout measures every child, hidden or not —
                // checked), so the dialog keeps one height and the footer does not move as
                // LB/RB go round.
                height: implicitHeight
                currentIndex: sectionBar.currentIndex

                // VIDEO
                Column {
                    spacing: dlg._px(0)

                    SettingRow {
                        label: qsTr("Resolution")
                        // One override whether it came from the strip or from the Custom
                        // dialog, which is also how _sectionCounts counts it.
                        overridden: resSel.currentIndex !== 0 || dlg._customResW > 0
                        source: dlg._srcText("resolution", overridden)
                        onResetRequested: {
                            dlg._customResW = 0; dlg._customResH = 0
                            resSel.currentIndex = 0
                            dlg.saveOverride()
                        }
                        Row {
                            spacing: dlg._px(12)
                            SegmentedSelector {
                                id: resSel; labels: dlg._resLabels
                                nativeIndices: dlg._resNative
                                inheritStrip: true
                                inheritIndex: dlg._inheritIdx(dlg._resLabels, "resolution")
                                KeyNavigation.up: profileTabs
                                KeyNavigation.down: resCustomBtn
                                KeyNavigation.right: resCustomBtn
                                // Selecting Global or a preset clears any custom override.
                                onActivated: { dlg._customResW = 0; dlg._customResH = 0; dlg.saveOverride() }
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
                            dlg.saveOverride()
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
                                // Selecting Global or a listed value clears any custom override.
                                onActivated: { dlg._customFps = 0; dlg.saveOverride() }
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
                     * ⚠️ The "Global · 113" pill that used to stand to the left of this
                     * slider is gone (6.0.0), and with it the only way the MOUSE had to
                     * give the value back. Both jobs moved into the row: the caption says
                     * what Global holds, and the Y prompt beside it resets — one target for
                     * the pad and the pointer, like every other row now.
                     *
                     * The groove carries a tick where the global value sits, because a
                     * slider has no pill to mark: it is the only row where the inherited
                     * value is a position rather than an option.
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
                                    KeyNavigation.down: dmSel

                                    // What Global holds, as a position on the groove. Drawn
                                    // only while the profile has a value of its own: with
                                    // the row inherited the handle is already there, and two
                                    // marks in the same place read as a defect.
                                    readonly property real _globalPos: {
                                        var kbps = StreamingPreferences.bitrateKbps
                                        var t = (kbps - bitrateSlider.from) / (bitrateSlider.to - bitrateSlider.from)
                                        return Math.max(0, Math.min(1, t))
                                    }

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
                                            x: bitrateSlider._globalPos * (parent.width - width)
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                    handle: Rectangle {
                                        x: bitrateSlider.leftPadding + bitrateSlider.visualPosition * (bitrateSlider.availableWidth - width)
                                        y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                        implicitWidth: dlg._px(14); implicitHeight: dlg._px(14); radius: dlg._px(7)
                                        // Accent only while the profile holds its own bitrate
                                        // (6.0.0): the one control left that painted the accent
                                        // on an inherited value. Neutral otherwise, like the
                                        // inherited pill of every other row.
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
                                // Accent only when it is the profile's own number: while the
                                // row follows Global this reads the global bitrate, and an
                                // accent figure there would claim an override that is not.
                                color: dlg._bitrateOverridden ? dlg._accent : Theme.text
                                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall); font.bold: true
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }

                    // ⚠️ Rows follow Settings: the tab they sit under and, within it, the order
                    // they have there, so there is one order to learn and not two. Each tab is
                    // its own KeyNavigation chain, entered from Name and left to the footer —
                    // a row moved between tabs moves in _firstOfSection / _lastOfSection too.
                    //
                    // These three are in Settings → Video's own order, which is also dependency
                    // order: V-Sync sits directly above Frame pacing, the row it governs, so a
                    // greyed control never sends you hunting for the reason. Global = follow the
                    // setting in Settings → Video.
                    //
                    // ⚠️ Display mode sits here because Match refresh rate used to follow it and
                    // depended on it. That row is gone as of 5.5.0; this one stays as an override
                    // in its own right, and V-Sync directly above Frame pacing is now the only
                    // live dependency in the group.
                    SettingRow {
                        label: qsTr("Display mode")
                        enabled: !dlg._effVrr
                        detail: enabled ? "" : qsTr("Borderless with VRR")
                        overridden: dmSel.currentIndex > 0
                        source: dlg._srcText("displaymode", overridden)
                        onResetRequested: { dmSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: dmSel; labels: dlg._dmLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._dmLabels, "displaymode")
                            KeyNavigation.up: bitrateSlider
                            KeyNavigation.down: vsyncSel
                            onActivated: dlg.saveOverride()
                        }
                    }
                    SettingRow {
                        label: qsTr("V-Sync")
                        enabled: !dlg._effVrr
                        detail: enabled ? "" : qsTr("On with VRR")
                        overridden: vsyncSel.currentIndex > 0
                        source: dlg._srcText("vsync", overridden)
                        onResetRequested: { vsyncSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: vsyncSel; labels: dlg._vsyncLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._vsyncLabels, "vsync")
                            KeyNavigation.up: dmSel
                            KeyNavigation.down: fpSel
                            onActivated: dlg.saveOverride()
                        }
                    }
                    SettingRow {
                        label: qsTr("Frame pacing")
                        enabled: dlg._effVsync && !dlg._effVrr
                        detail: enabled ? "" : (dlg._effVrr ? qsTr("On with VRR") : qsTr("Needs V-Sync"))
                        overridden: fpSel.currentIndex > 0
                        source: dlg._srcText("framepacing", overridden)
                        onResetRequested: { fpSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: fpSel; labels: dlg._fpLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._fpLabels, "framepacing")
                            KeyNavigation.up: vsyncSel
                            KeyNavigation.down: fracVsyncSel
                            onActivated: dlg.saveOverride()
                        }
                    }
                    // Third in the dependency chain, and placed directly under the row it
                    // needs so the greyed control never sends anyone hunting: V-Sync governs
                    // Frame pacing, and the two of them together govern this. _effPacing
                    // already folds V-Sync in, so switching either one off greys this row.
                    //
                    // ⚠️ The detail names the missing condition rather than saying "needs
                    // V-Sync and Frame pacing" always: with V-Sync off, Frame pacing above is
                    // greyed too, and pointing at a greyed row as the thing to fix is a dead
                    // end. Whichever row is still actionable is the one named.
                    //
                    // ⚠️ It is NOT in the per-game panel, and must not be added there — see the
                    // note on AppOverride::hasFractionalVsync. Its conditions live at this
                    // level, so a per-game copy could hold a value it had no way to satisfy.
                    SettingRow {
                        label: qsTr("Fractional V-Sync")
                        enabled: dlg._effPacing && !dlg._effVrr
                        detail: enabled ? ""
                              : (dlg._effVrr ? qsTr("Off with VRR")
                                 : (dlg._effVsync ? qsTr("Needs Frame pacing") : qsTr("Needs V-Sync")))
                        overridden: fracVsyncSel.currentIndex > 0
                        source: dlg._srcText("fractionalvsync", overridden)
                        onResetRequested: { fracVsyncSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: fracVsyncSel; labels: dlg._fracVsyncLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._fracVsyncLabels, "fractionalvsync")
                            KeyNavigation.up: fpSel
                            KeyNavigation.down: vrrSel
                            onActivated: dlg.saveOverride()
                        }
                    }
                    // VRR (6.0.0), under the row it switches off. Host-profile only, like
                    // Fractional V-Sync and for the same reason — see AppOverride::hasVrr.
                    SettingRow {
                        label: qsTr("VRR")
                        enabled: dlg._effVsync
                        detail: !enabled ? qsTr("Needs V-Sync")
                              : (dlg._vrrOn && dlg._vrrOverRate
                                 ? qsTr("Inactive at %1 FPS on this %2 Hz display").arg(dlg._effFps).arg(dlg._vrrRec.refreshHz)
                                 : "")
                        overridden: vrrSel.currentIndex > 0
                        source: dlg._srcText("vrr", overridden)
                        onResetRequested: { vrrSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: vrrSel; labels: dlg._vrrLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._vrrLabels, "vrr")
                            KeyNavigation.up: fracVsyncSel
                            KeyNavigation.down: removeBtn
                            onActivated: dlg.saveOverride()
                        }
                    }
                }

                // AUDIO
                Column {
                    spacing: dlg._px(0)

                    SettingRow {
                        label: qsTr("Audio")
                        overridden: audSel.currentIndex > 0
                        source: dlg._srcText("audio", overridden)
                        onResetRequested: { audSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: audSel; labels: dlg._audLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._audLabels, "audio")
                            KeyNavigation.up: profileTabs
                            KeyNavigation.down: removeBtn
                            onActivated: dlg.saveOverride()
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
                        onResetRequested: { codecSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: codecSel; labels: dlg._codecLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._codecLabels, "codec")
                            KeyNavigation.up: profileTabs
                            KeyNavigation.down: hdrSel
                            onActivated: dlg.saveOverride()
                        }
                    }
                    SettingRow {
                        label: qsTr("HDR")
                        overridden: hdrSel.currentIndex > 0
                        source: dlg._srcText("hdr", overridden)
                        onResetRequested: { hdrSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: hdrSel; labels: dlg._hdrLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._hdrLabels, "hdr")
                            KeyNavigation.up: codecSel
                            KeyNavigation.down: removeBtn
                            onActivated: dlg.saveOverride()
                        }
                    }
                }

                // NETWORK
                Column {
                    spacing: dlg._px(0)

                    // A profile describes a situation, not just a picture quality: "docked" and
                    // "handheld" want different answers here as much as they want different
                    // resolutions. Global = follow the setting in Settings → Network.
                    SettingRow {
                        label: qsTr("Match host link speed")
                        overridden: linkSel.currentIndex > 0
                        source: dlg._srcText("matchlink", overridden)
                        onResetRequested: { linkSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: linkSel; labels: dlg._linkLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._linkLabels, "matchlink")
                            KeyNavigation.up: profileTabs
                            KeyNavigation.down: removeBtn
                            onActivated: dlg.saveOverride()
                        }
                    }
                }

                // SESSION
                Column {
                    spacing: dlg._px(0)

                    // Same reason as the row above: it belongs to a situation. A profile used at the
                    // desk, where the host is an arm's length away, has little to gain from the
                    // wait; one used on a TV in another room does, because the alternative is
                    // looking at that host's desktop rearranging itself.
                    // Global = follow the setting in Settings → Session.
                    SettingRow {
                        label: qsTr("Wait for the game to appear")
                        overridden: waitGameSel.currentIndex > 0
                        source: dlg._srcText("waitgame", overridden)
                        onResetRequested: { waitGameSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: waitGameSel; labels: dlg._waitLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._waitLabels, "waitgame")
                            KeyNavigation.up: profileTabs
                            KeyNavigation.down: hueSel
                            onActivated: dlg.saveOverride()
                        }
                    }
                    // Sits with the row above because it belongs to the same context: both are
                    // about what happens around the stream rather than to the picture, and both
                    // live in Settings → Session.
                    SettingRow {
                        label: qsTr("Philips Hue")
                        overridden: hueSel.currentIndex > 0
                        source: dlg._srcText("hue", overridden)
                        onResetRequested: { hueSel.currentIndex = 0; dlg.saveOverride() }
                        SegmentedSelector {
                            id: hueSel; labels: dlg._hueLabels
                            inheritStrip: true
                            inheritIndex: dlg._inheritIdx(dlg._hueLabels, "hue")
                            KeyNavigation.up: waitGameSel
                            KeyNavigation.down: removeBtn
                            onActivated: dlg.saveOverride()
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

        /*
         * ── The prompt bar (6.0.0) ───────────────────────────────────────────
         *
         * What the buttons do, where you are. It is in the dialog and not in the app's
         * status bar for a plain reason: this popup is modal and dims everything behind it,
         * status bar included, so a prompt there would be greyed out under a scrim while
         * naming the very keys the user needs.
         *
         * Each prompt shows only when it applies — Y with nothing to reset is a promise the
         * dialog cannot keep — and LB/RB are missing on purpose: SectionTabBar already
         * draws them at the two ends of the strip, next to the thing they move.
         */
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._promptH

            Row {
                anchors.left: parent.left; anchors.leftMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(18)

                Prompt {
                    // The only place X is drawn (it used to sit on the chip as well). It says
                    // what X does wherever the cursor is: on the chips, the chip under the
                    // cursor — whether or not it is the active one — and elsewhere the active
                    // profile. A click does the same.
                    readonly property int _target: profileTabs.activeFocus ? profileTabs.currentIndex
                                                                           : (dlg._editing ? dlg.editingSlot : -1)
                    visible: _target >= 0
                    buttonKey: "X"; keyLabel: "X"; action: qsTr("Rename")
                    clickable: true
                    onClicked: dlg.renameProfile(_target)
                }
                Prompt {
                    // Only with a row under the cursor that has something to give back.
                    visible: dlg._focusedRow !== null && dlg._focusedRow.visible
                             && dlg._focusedRow.overridden
                    buttonKey: "Y"; keyLabel: "Y"; action: qsTr("Reset row")
                    // The mouse's way to the same action. It used to be a second Y drawn on
                    // the row itself, which showed the prompt twice at once; the row keeps the
                    // KEY, this keeps the only drawing. Clicking does not move the focus, so
                    // _focusedRow is still the row the user was on.
                    clickable: true
                    onClicked: if (dlg._focusedRow) dlg._focusedRow.resetRequested()
                }
                // LT/RT are not here: they are drawn beside the profile chips they cycle.
            }

            // How much of this host's streaming this profile actually changes — the figure
            // the chips carry, where the eye already is when it reaches the footer.
            Label {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                visible: dlg._editing
                text: dlg._totalCount === 0 ? qsTr("No changes")
                    : dlg._totalCount === 1 ? qsTr("1 change")
                    :                         qsTr("%1 changes").arg(dlg._totalCount)
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption)
                font.bold: dlg._totalCount > 0
                color: dlg._totalCount > 0 ? dlg._accent : Theme.text3
            }
        }

        // ── Footer: Remove profile (left) · Reset all + Done (right) ────────
        //
        // ⚠️ Done is the rightmost button now, and it is the one B closes the dialog with.
        // It used to sit to the LEFT of "Reset to Global", which put the destructive-ish
        // button where the affirmative one is in every other dialog of the app.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._px(52)

            DialogButton {
                id: removeBtn
                visible: dlg._editing
                anchors.left: parent.left; anchors.leftMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Remove profile")
                danger: true
                fontSize: 13
                width: dlg._px(140); height: dlg._tabH
                onActivated: dlg.removeProfile()
                KeyNavigation.up: dlg._editing ? dlg._lastOfSection(sectionBar.currentIndex) : addBtn
                KeyNavigation.right: dlg._editing ? resetBtn : doneBtn
            }

            Row {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(8)

                DialogButton {
                    id: resetBtn
                    visible: dlg._editing
                    // "Reset all", not "Reset to Global": the per-game panel's twin of this
                    // button gives its rows back to the host PROFILE as often as to Global,
                    // and the two dialogs say the same thing on the same button.
                    text: qsTr("Reset all")
                    enabled: dlg._totalCount > 0
                    opacity: enabled ? 1.0 : 0.4
                    fontSize: 13
                    width: dlg._px(104); height: dlg._tabH
                    onActivated: dlg.resetAll()
                    KeyNavigation.up: dlg._lastOfSection(sectionBar.currentIndex)
                    KeyNavigation.left: removeBtn
                    KeyNavigation.right: doneBtn
                }
                DialogButton {
                    id: doneBtn
                    text: qsTr("Done")
                    affirmative: true
                    fontSize: 13
                    width: dlg._px(76); height: dlg._tabH
                    onActivated: dlg.close()
                    KeyNavigation.up: dlg._editing ? dlg._lastOfSection(sectionBar.currentIndex) : addBtn
                    KeyNavigation.left: dlg._editing ? resetBtn : null
                }
            }
        }
    }

    // Manual resolution entry for the edited profile slot.
    CustomResolutionDialog {
        id: resCustomDialog
        onAccepted: function(w, h) {
            dlg._customResW = w
            dlg._customResH = h
            resSel.currentIndex = -1
            dlg.saveOverride()
        }
    }

    // Naming the edited profile (6.0.0), reached with X. Eight names for a pad, a field for
    // a keyboard — see ProfileNameDialog for why the field could not be the only way.
    ProfileNameDialog {
        id: nameDialog
        onAccepted: function(name) {
            dlg.saveName(name)
            // Back to the chips: X was pressed there, or on the profile they describe.
            Qt.callLater(function() { profileTabs.forceActiveFocus() })
        }
    }

    // Manual frame-rate entry for the edited profile slot (5.5.0).
    CustomFrameRateDialog {
        id: customFpsDialog
        onAccepted: function(fps) {
            dlg._customFps = fps
            fpsSel.currentIndex = -1
            dlg.saveOverride()
        }
    }
}
