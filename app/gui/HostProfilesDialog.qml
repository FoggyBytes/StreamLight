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
    readonly property color _danger: "#ef4444"
    readonly property color _text:   Theme.text
    readonly property color _dim:    Theme.text2
    readonly property color _line:   "#242424"
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
    readonly property int   _chromeH: _px(44) + _px(52) + _px(52) + _px(48)
    readonly property int   _maxProfiles: 3
    readonly property int   _maxNameLen: 14

    // Currently-edited slot (== active slot). -1 means OFF (no profile active):
    // profiles may still exist, but the host falls back to the global settings.
    property int editingSlot: -1
    property int profileCount: 0
    property var _tabLabels: []
    property bool _loading: false
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

    function _globalText(key) {
        var v = _globalValues[key]
        return (v === undefined || v === "") ? qsTr("Global") : qsTr("Global") + " · " + v
    }

    // The option that would repeat what the Global pill already says — hidden, so the strip
    // never offers the same answer twice. Read the long note on the twin of this function in
    // AppSettingsDialog: same rule, including the one case it must not hide.
    function _dupIndices(labels, key, current) {
        var v = _globalValues[key]
        if (v === undefined || v === "") return []
        for (var i = 1; i < labels.length; i++)
            if (labels[i] === v && i !== current) return [i]
        return []
    }

    // value tables (index 0 == Global placeholder) — mirror AppSettingsDialog
    //
    // ⚠️ One table per row, even where two rows offer the same three words. Six of these
    // used to share _hdrLabels because they were all "Global / On / Off"; now that index 0
    // carries a value, sharing would print HDR's answer on the V-Sync row.
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

    readonly property var _resLabels: [_globalText("resolution")].concat(
        _video.res.map(function(e) { return e.label }))
    readonly property var _resW:      [0].concat(_video.res.map(function(e) { return e.width }))
    readonly property var _resH:      [0].concat(_video.res.map(function(e) { return e.height }))
    readonly property var _resNative: _nativeIndices(_video.res)
    readonly property var _fpsLabels: [_globalText("fps")].concat(
        _video.fps.map(function(e) { return e.label }))
    readonly property var _fpsVals:   [0].concat(_video.fps.map(function(e) { return e.value }))
    readonly property var _fpsNative: _nativeIndices(_video.fps)
    readonly property var _hdrLabels: [_globalText("hdr"), "On", "Off"]
    readonly property var _vsyncLabels:   [_globalText("vsync"), "On", "Off"]
    readonly property var _fracVsyncLabels: [_globalText("fractionalvsync"), "On", "Off"]
    readonly property var _linkLabels:    [_globalText("matchlink"), "On", "Off"]
    readonly property var _waitLabels:    [_globalText("waitgame"), "On", "Off"]
    readonly property var _hueLabels:     [_globalText("hue"), "On", "Off"]
    readonly property var _codecLabels: [_globalText("codec"), "H.264", "HEVC", "AV1"]
    readonly property var _codecVals:   [-1, 1, 2, 4]
    readonly property var _fpLabels:  [_globalText("framepacing"), "Off", "On"]
    readonly property var _fpVals:    [-1, 0, 1]
    readonly property var _audLabels: [_globalText("audio"), "Stereo", "5.1", "7.1"]
    readonly property var _audVals:   [-1, 0, 1, 2]
    readonly property var _dmLabels:  [_globalText("displaymode"), "Fullscreen", "Borderless", "Windowed"]
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

    property bool _bitrateOverridden: false
    // Custom resolution override for the edited slot (0 == none / using a preset).
    property int _customResW: 0
    property int _customResH: 0
    // Custom frame-rate override, same tri-state (5.5.0).
    property int _customFps: 0
    readonly property bool _hasProfiles: profileCount > 0

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
        for (var i = 0; i < profileCount; i++)
            labels.push(computerModel.hostProfileName(pcIndex, i))
        _tabLabels = labels
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
        nameField.text = computerModel.hostProfileName(pcIndex, slot)
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
        computerModel.setHostProfileSettings(pcIndex, editingSlot, m)
    }

    function saveName() {
        if (_loading || editingSlot < 0) return
        computerModel.setHostProfileName(pcIndex, editingSlot, nameField.text)
        _refreshTabs()
    }

    function selectSlot(slot) {
        if (slot < 0 || slot >= profileCount) return
        editingSlot = slot
        profileTabs.currentIndex = slot
        computerModel.setHostActiveProfile(pcIndex, slot)
        loadSlot(slot)
    }

    // "OFF": no profile active — the host falls back to the global settings.
    // Profiles are kept; only the active selection is cleared (cursor unchanged).
    function selectOff() {
        editingSlot = -1
        computerModel.setHostActiveProfile(pcIndex, -1)
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

    contentItem: ColumnLayout {
        spacing: dlg._px(0)

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
                    font.family: "DM Sans"; font.pixelSize: dlg._px(18); font.bold: true
                    color: dlg._text
                }
                Label {
                    text: dlg.hostName
                    font.family: "DM Sans"; font.pixelSize: dlg._px(13)
                    color: dlg._dim; elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        // ── Profile bar: tabs + Add ─────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._rowH
            Label {
                anchors.left: parent.left; anchors.leftMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Profile")
                font.family: "DM Sans"; font.pixelSize: dlg._px(15); font.bold: true
                color: dlg._text
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(10)

                // "OFF" pill — leftmost. Selecting it deactivates all profiles
                // (host falls back to the global settings). Styled like one
                // SegmentedSelector pill, mirroring the others.
                FocusScope {
                    id: offBtn
                    activeFocusOnTab: true
                    anchors.verticalCenter: parent.verticalCenter
                    property bool selected: dlg.editingSlot < 0
                    implicitHeight: dlg._tabH
                    implicitWidth: offPill.implicitWidth + 8
                    width: implicitWidth; height: dlg._tabH

                    Rectangle {
                        anchors.fill: parent
                        radius: dlg._px(8)
                        color: Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07))
                        border.color: offBtn.activeFocus ? dlg._accent : Theme.line
                        border.width: offBtn.activeFocus ? 3 : 1
                    }
                    Item {
                        id: offPill
                        anchors.centerIn: parent
                        implicitWidth: offLabel.implicitWidth + dlg._px(28)
                        implicitHeight: dlg._px(30)
                        width: implicitWidth; height: 30
                        Rectangle {
                            anchors.fill: parent; anchors.margins: 2; radius: dlg._px(5)
                            color: offBtn.selected ? dlg._accent : "transparent"
                        }
                        Label {
                            id: offLabel
                            anchors.centerIn: parent
                            text: qsTr("Off")
                            color: offBtn.selected ? Theme.onAccent : Theme.text2
                            font.family: "DM Sans"; font.pixelSize: dlg._px(13); font.bold: offBtn.selected
                        }
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
                    KeyNavigation.down: dlg._editing ? nameField : addBtn
                }

                // Profile pills — each its own separate button (not one block),
                // matching the Off pill. One focusable element; ◀/▶ move between
                // pills (and activate them); the active pill is green.
                FocusScope {
                    id: profileTabs
                    visible: dlg._hasProfiles
                    anchors.verticalCenter: parent.verticalCenter
                    activeFocusOnTab: true
                    height: dlg._tabH
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
                                width: ptLabel.implicitWidth + dlg._px(28)
                                height: dlg._tabH
                                radius: dlg._px(8)
                                color: Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07))
                                property bool _active: index === dlg.editingSlot
                                property bool _cursor: profileTabs.activeFocus && index === profileTabs.currentIndex
                                border.color: _cursor ? dlg._accent : Theme.line
                                border.width: _cursor ? 3 : 1
                                Rectangle {
                                    anchors.fill: parent; anchors.margins: 2; radius: dlg._px(5)
                                    color: ptTile._active ? dlg._accent : "transparent"
                                }
                                Label {
                                    id: ptLabel
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: ptTile._active ? Theme.onAccent : Theme.text2
                                    font.family: "DM Sans"; font.pixelSize: dlg._px(13); font.bold: ptTile._active
                                }
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
                    // ◀/▶ move the cursor only — they do NOT activate. A profile is
                    // activated solely by A (Return/Enter/Space) or a click.
                    Keys.onLeftPressed: {
                        if (currentIndex > 0) { currentIndex--; event.accepted = true }
                        else event.accepted = false
                    }
                    Keys.onRightPressed: {
                        if (currentIndex < labels.length - 1) { currentIndex++; event.accepted = true }
                        else event.accepted = false
                    }
                    Keys.onReturnPressed: activated(currentIndex)
                    Keys.onEnterPressed:  activated(currentIndex)
                    Keys.onSpacePressed:  activated(currentIndex)
                    KeyNavigation.left: offBtn
                    KeyNavigation.right: addBtn
                    KeyNavigation.down: dlg._editing ? nameField : addBtn
                }

                // "+ Add" pill — same look/size as the Off pill (just a touch wider).
                FocusScope {
                    id: addBtn
                    activeFocusOnTab: true
                    enabled: dlg.profileCount < dlg._maxProfiles
                    opacity: enabled ? 1.0 : 0.4
                    anchors.verticalCenter: parent.verticalCenter
                    implicitHeight: dlg._tabH
                    implicitWidth: addPill.implicitWidth + dlg._px(12)
                    width: implicitWidth; height: dlg._tabH

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
                            font.family: "DM Sans"; font.pixelSize: dlg._px(13); font.bold: true
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
                    KeyNavigation.down: dlg._editing ? nameField : doneBtn
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                x: dlg._padX; width: parent.width - dlg._padX * 2; height: 1; color: dlg._line
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
                font.family: "DM Sans"; font.pixelSize: dlg._px(14)
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
                font.family: "DM Sans"; font.pixelSize: dlg._px(14)
                color: dlg._dim
            }
        }

        // ── Name + override rows (only while a profile is selected) ─────────
        Flickable {
            id: flick
            Layout.fillWidth: true
            visible: dlg._editing
            Layout.preferredHeight: dlg._editing ? Math.min(
                (Overlay.overlay ? Overlay.overlay.height - dlg._chromeH : dlg._px(800)),
                rowsCol.implicitHeight) : 0
            contentHeight: rowsCol.implicitHeight
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
                if (!inside) return

                var margin = 12
                var pos    = activeFocusItem.mapToItem(rowsCol, 0, 0)
                var top    = pos.y
                var bottom = pos.y + activeFocusItem.height

                if (top < contentY + margin) {
                    contentY = Math.max(0, top - margin)
                } else if (bottom > contentY + height - margin) {
                    contentY = Math.min(Math.max(0, contentHeight - height),
                                        bottom - height + margin)
                }
            }

            Column {
                id: rowsCol
                width: flick.width
                spacing: dlg._px(0)

                component SettingRow: Item {
                    id: row
                    width: rowsCol.width
                    // Grows only when a reason is shown, so every other row keeps its height.
                    height: row.detail.length > 0
                            ? Math.max(dlg._rowH, labelCol.implicitHeight + dlg._px(20))
                            : dlg._rowH
                    property string label: ""
                    // Optional second line, used to say why a row is greyed out. A locked
                    // control with no reason given is worse than no lock at all — the user
                    // is left guessing which other setting is holding it.
                    property string detail: ""
                    default property alias content: holder.data
                    opacity: enabled ? 1.0 : 0.4

                    Column {
                        id: labelCol
                        anchors.left: parent.left; anchors.leftMargin: dlg._padX
                        anchors.right: holder.left; anchors.rightMargin: dlg._px(16)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: dlg._px(2)
                        Label {
                            width: parent.width
                            text: row.label
                            font.family: "DM Sans"; font.pixelSize: dlg._px(15); font.bold: true
                            color: dlg._text
                            elide: Text.ElideRight
                        }
                        Label {
                            width: parent.width
                            visible: row.detail.length > 0
                            text: row.detail
                            font.family: "DM Sans"; font.pixelSize: dlg._px(12)
                            color: dlg._dim
                            wrapMode: Text.WordWrap
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

                // Name (editable, char-limited)
                Item {
                    width: rowsCol.width
                    height: dlg._rowH
                    Label {
                        anchors.left: parent.left; anchors.leftMargin: dlg._padX
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Name")
                        font.family: "DM Sans"; font.pixelSize: dlg._px(15); font.bold: true
                        color: dlg._text
                    }
                    TextField {
                        id: nameField
                        anchors.right: parent.right; anchors.rightMargin: dlg._padX
                        anchors.verticalCenter: parent.verticalCenter
                        width: dlg._px(320); height: dlg._px(38)
                        leftPadding: dlg._px(12); rightPadding: dlg._px(12)
                        maximumLength: dlg._maxNameLen
                        font.family: "DM Sans"; font.pixelSize: dlg._px(14)
                        color: dlg._text
                        selectByMouse: true
                        background: Rectangle {
                            radius: dlg._px(9)
                            color: "#101316"
                            border.color: nameField.activeFocus ? dlg._accent : Theme.line
                            border.width: nameField.activeFocus ? 2 : 1
                        }
                        onEditingFinished: dlg.saveName()
                        KeyNavigation.up: profileTabs
                        KeyNavigation.down: resSel
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        x: dlg._padX; width: parent.width - dlg._padX * 2; height: 1; color: dlg._line
                    }
                }

                SettingRow {
                    label: qsTr("Resolution")
                    Row {
                        spacing: dlg._px(12)
                        SegmentedSelector {
                            id: resSel; labels: dlg._resLabels
                            nativeIndices: dlg._resNative
                            hiddenIndices: dlg._dupIndices(dlg._resLabels, "resolution", currentIndex)
                            KeyNavigation.up: nameField
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
                    Row {
                        spacing: dlg._px(12)
                        SegmentedSelector {
                            id: fpsSel; labels: dlg._fpsLabels
                            nativeIndices: dlg._fpsNative
                            hiddenIndices: dlg._dupIndices(dlg._fpsLabels, "fps", currentIndex)
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
                            KeyNavigation.down: bitrateGlobalBtn
                            KeyNavigation.left: fpsSel
                        }
                    }
                }

                // ── Bitrate: isolated Global pill + Settings-identical slider ───
                Item {
                    width: rowsCol.width
                    height: dlg._rowH
                    Label {
                        anchors.left: parent.left; anchors.leftMargin: dlg._padX
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Bitrate (Mbps)")
                        font.family: "DM Sans"; font.pixelSize: dlg._px(15); font.bold: true
                        color: dlg._text
                    }

                    Row {
                        anchors.right: parent.right; anchors.rightMargin: dlg._padX
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: dlg._px(16)

                        // The twin of the one in AppSettingsDialog — read the note there for
                        // why forty lines of hand-built pill became this.
                        PillButton {
                            id: bitrateGlobalBtn
                            anchors.verticalCenter: parent.verticalCenter
                            selected: !dlg._bitrateOverridden
                            // Bare number, no unit: the row is labelled "Bitrate (Mbps)".
                            text: dlg._globalText("bitrate")
                            onClicked: dlg.setBitrateGlobal()
                            KeyNavigation.up: fpsSel
                            KeyNavigation.down: bitrateSlider
                            KeyNavigation.right: bitrateSlider
                        }

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
                                Keys.onPressed: {
                                    if (event.isAutoRepeat) { event.accepted = true; return }
                                    if (event.key === Qt.Key_Left)  { _startAccel(-1); event.accepted = true }
                                    if (event.key === Qt.Key_Right) { _startAccel(+1); event.accepted = true }
                                }
                                Keys.onReleased: {
                                    if (event.isAutoRepeat) { event.accepted = true; return }
                                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) { _stopAccel(); event.accepted = true }
                                }
                                KeyNavigation.up: bitrateGlobalBtn
                                KeyNavigation.down: hdrSel

                                background: Rectangle {
                                    x: bitrateSlider.leftPadding
                                    y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                    width: bitrateSlider.availableWidth
                                    height: dlg._px(3); radius: dlg._px(2)
                                    color: "#f0f0f0"
                                }
                                handle: Rectangle {
                                    x: bitrateSlider.leftPadding + bitrateSlider.visualPosition * (bitrateSlider.availableWidth - width)
                                    y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                    implicitWidth: dlg._px(14); implicitHeight: dlg._px(14); radius: dlg._px(7)
                                    color: bitrateSlider.pressed ? Qt.lighter(dlg._accent, 1.2)
                                         : bitrateSlider.hovered ? Qt.lighter(dlg._accent, 1.1)
                                         :                         dlg._accent
                                    border.color: dlg._accent; border.width: 1
                                }
                            }
                        }

                        Label {
                            id: brValue
                            anchors.verticalCenter: parent.verticalCenter
                            width: dlg._px(78)
                            text: (bitrateSlider.value / 1000).toFixed(0) + qsTr(" Mbps")
                            color: dlg._accent
                            font.family: "DM Sans"; font.pixelSize: dlg._px(13); font.bold: true
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        x: dlg._padX; width: parent.width - dlg._padX * 2; height: 1; color: dlg._line
                    }
                }

                SettingRow {
                    label: qsTr("HDR")
                    SegmentedSelector {
                        id: hdrSel; labels: dlg._hdrLabels
                        hiddenIndices: dlg._dupIndices(dlg._hdrLabels, "hdr", currentIndex)
                        KeyNavigation.up: bitrateSlider
                        KeyNavigation.down: codecSel
                        onActivated: dlg.saveOverride()
                    }
                }
                SettingRow {
                    label: qsTr("Video codec")
                    SegmentedSelector {
                        id: codecSel; labels: dlg._codecLabels
                        hiddenIndices: dlg._dupIndices(dlg._codecLabels, "codec", currentIndex)
                        KeyNavigation.up: hdrSel
                        KeyNavigation.down: dmSel
                        onActivated: dlg.saveOverride()
                    }
                }
                // ⚠️ Row order groups by what a setting acts on, not by when it was added:
                // picture first, then audio, then the network one, then the two that are
                // about the session around the stream. Within each group the order mirrors
                // Settings, so there is one order to learn and not two. Anything appended
                // to the end from now on has to earn its place here as well — and the
                // KeyNavigation chain below has to be walked end to end when it moves.
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
                    SegmentedSelector {
                        id: dmSel; labels: dlg._dmLabels
                        hiddenIndices: dlg._dupIndices(dlg._dmLabels, "displaymode", currentIndex)
                        KeyNavigation.up: codecSel
                        KeyNavigation.down: vsyncSel
                        onActivated: dlg.saveOverride()
                    }
                }
                SettingRow {
                    label: qsTr("V-Sync")
                    SegmentedSelector {
                        id: vsyncSel; labels: dlg._vsyncLabels
                        hiddenIndices: dlg._dupIndices(dlg._vsyncLabels, "vsync", currentIndex)
                        KeyNavigation.up: dmSel
                        KeyNavigation.down: fpSel
                        onActivated: dlg.saveOverride()
                    }
                }
                SettingRow {
                    label: qsTr("Frame pacing")
                    enabled: dlg._effVsync
                    detail: enabled ? "" : qsTr("Needs V-Sync")
                    SegmentedSelector {
                        id: fpSel; labels: dlg._fpLabels
                        hiddenIndices: dlg._dupIndices(dlg._fpLabels, "framepacing", currentIndex)
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
                    enabled: dlg._effPacing
                    detail: enabled ? ""
                          : (dlg._effVsync ? qsTr("Needs Frame pacing") : qsTr("Needs V-Sync"))
                    SegmentedSelector {
                        id: fracVsyncSel; labels: dlg._fracVsyncLabels
                        hiddenIndices: dlg._dupIndices(dlg._fracVsyncLabels, "fractionalvsync", currentIndex)
                        KeyNavigation.up: fpSel
                        KeyNavigation.down: audSel
                        onActivated: dlg.saveOverride()
                    }
                }
                SettingRow {
                    label: qsTr("Audio")
                    SegmentedSelector {
                        id: audSel; labels: dlg._audLabels
                        hiddenIndices: dlg._dupIndices(dlg._audLabels, "audio", currentIndex)
                        KeyNavigation.up: fracVsyncSel
                        KeyNavigation.down: linkSel
                        onActivated: dlg.saveOverride()
                    }
                }
                // A profile describes a situation, not just a picture quality: "docked" and
                // "handheld" want different answers here as much as they want different
                // resolutions. Global = follow the setting in Settings → Network.
                SettingRow {
                    label: qsTr("Match host link speed")
                    SegmentedSelector {
                        id: linkSel; labels: dlg._linkLabels
                        hiddenIndices: dlg._dupIndices(dlg._linkLabels, "matchlink", currentIndex)
                        KeyNavigation.up: audSel
                        KeyNavigation.down: waitGameSel
                        onActivated: dlg.saveOverride()
                    }
                }
                // Same reason as the row above: it belongs to a situation. A profile used at the
                // desk, where the host is an arm's length away, has little to gain from the
                // wait; one used on a TV in another room does, because the alternative is
                // looking at that host's desktop rearranging itself.
                // Global = follow the setting in Settings → Session.
                SettingRow {
                    label: qsTr("Wait for the game to appear")
                    SegmentedSelector {
                        id: waitGameSel; labels: dlg._waitLabels
                        hiddenIndices: dlg._dupIndices(dlg._waitLabels, "waitgame", currentIndex)
                        KeyNavigation.up: linkSel
                        KeyNavigation.down: hueSel
                        onActivated: dlg.saveOverride()
                    }
                }
                // Sits with the row above because it belongs to the same context: both are
                // about what happens around the stream rather than to the picture, and both
                // live in Settings → Session.
                SettingRow {
                    label: qsTr("Philips Hue")
                    SegmentedSelector {
                        id: hueSel; labels: dlg._hueLabels
                        hiddenIndices: dlg._dupIndices(dlg._hueLabels, "hue", currentIndex)
                        KeyNavigation.up: waitGameSel
                        KeyNavigation.down: removeBtn
                        onActivated: dlg.saveOverride()
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        // ── Footer: Remove (left) · Done + Reset to Global (right) ──────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._px(52)

            DialogButton {
                id: removeBtn
                visible: dlg._editing
                anchors.left: parent.left; anchors.leftMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Remove")
                danger: true
                fontSize: 13
                width: dlg._px(84); height: dlg._tabH
                onActivated: dlg.removeProfile()
                KeyNavigation.up: dlg._editing ? hueSel : addBtn
                KeyNavigation.right: doneBtn
            }

            Row {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(8)

                DialogButton {
                    id: doneBtn
                    text: qsTr("Done")
                    affirmative: true
                    fontSize: 13
                    width: dlg._px(76); height: dlg._tabH
                    onActivated: dlg.close()
                    KeyNavigation.up: dlg._editing ? hueSel : addBtn
                    KeyNavigation.left: dlg._editing ? removeBtn : null
                    KeyNavigation.right: dlg._editing ? resetBtn : null
                }
                DialogButton {
                    id: resetBtn
                    visible: dlg._editing
                    text: qsTr("Reset to Global")
                    fontSize: 13
                    width: dlg._px(128); height: dlg._tabH
                    onActivated: dlg.resetAll()
                    KeyNavigation.up: hueSel
                    KeyNavigation.left: doneBtn
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
