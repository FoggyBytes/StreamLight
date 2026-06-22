import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import StreamingPreferences 1.0

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

    property var computerModel: null
    property int pcIndex: -1
    property string hostName: ""

    readonly property color _accent: "#00E676"
    readonly property color _danger: "#ef4444"
    readonly property color _text:   "#ECECEC"
    readonly property color _dim:    "#9A9A9A"
    readonly property color _line:   "#242424"
    readonly property int   _rowH:   52
    readonly property int   _padX:   28
    readonly property int   _tabH:   36
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

    // value tables (index 0 == Global placeholder) — mirror AppSettingsDialog
    readonly property var _resLabels: ["Global", "720p", "1080p", "1440p", "4K"]
    readonly property var _resW:      [0, 1280, 1920, 2560, 3840]
    readonly property var _resH:      [0, 720, 1080, 1440, 2160]
    readonly property var _fpsLabels: ["Global", "30", "60", "90", "120"]
    readonly property var _fpsVals:   [0, 30, 60, 90, 120]
    readonly property var _hdrLabels: ["Global", "On", "Off"]
    readonly property var _codecLabels: ["Global", "H.264", "HEVC", "AV1"]
    readonly property var _codecVals:   [-1, 1, 2, 4]
    readonly property var _fpLabels:  ["Global", "Off", "Automatic", "Software", "Hardware"]
    readonly property var _fpVals:    [-1, 0, 1, 2, 3]
    readonly property var _audLabels: ["Global", "Stereo", "5.1", "7.1"]
    readonly property var _audVals:   [-1, 0, 1, 2]

    property bool _bitrateOverridden: false
    // Custom resolution override for the edited slot (0 == none / using a preset).
    property int _customResW: 0
    property int _customResH: 0
    readonly property bool _hasProfiles: profileCount > 0

    modal: true
    dim: true
    focus: true                       // grab keyboard/gamepad focus when shown
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    width: 780
    padding: 0

    Overlay.modal: Rectangle { color: "#CC000000" }

    background: Rectangle {
        color: "#15171A"
        radius: 16
        border.color: "#2A2A2A"
        border.width: 1
    }

    onOpened: {
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
            var rpi = _resW.indexOf(ov.width)
            if (rpi > 0 && _resH[rpi] === ov.height) {
                resSel.currentIndex = rpi
            } else {
                resSel.currentIndex = -1
                _customResW = ov.width
                _customResH = ov.height
            }
        } else {
            resSel.currentIndex = 0
        }
        fpsSel.currentIndex   = (ov.fps !== undefined) ? _idxByVal(_fpsVals, ov.fps) : 0
        hdrSel.currentIndex   = (ov.hdr !== undefined) ? (ov.hdr ? 1 : 2) : 0
        codecSel.currentIndex = (ov.codec !== undefined) ? _idxByVal(_codecVals, ov.codec) : 0
        fpSel.currentIndex    = (ov.framepacing !== undefined) ? _idxByVal(_fpVals, ov.framepacing) : 0
        audSel.currentIndex   = (ov.audio !== undefined) ? _idxByVal(_audVals, ov.audio) : 0
        hueSel.currentIndex   = (ov.hue !== undefined) ? (ov.hue ? 1 : 2) : 0
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
        if (fpsSel.currentIndex > 0)   m.fps = _fpsVals[fpsSel.currentIndex]
        if (_bitrateOverridden && bitrateSlider.value >= bitrateSlider.from)
                                       m.bitrate = Math.round(bitrateSlider.value)
        if (hdrSel.currentIndex > 0)   m.hdr = (hdrSel.currentIndex === 1)
        if (codecSel.currentIndex > 0) m.codec = _codecVals[codecSel.currentIndex]
        if (fpSel.currentIndex > 0)    m.framepacing = _fpVals[fpSel.currentIndex]
        if (audSel.currentIndex > 0)   m.audio = _audVals[audSel.currentIndex]
        if (hueSel.currentIndex > 0)   m.hue = (hueSel.currentIndex === 1)
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
        spacing: 0

        // ── Header (title + host name inline) ───────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: dlg._padX
                anchors.rightMargin: dlg._padX
                spacing: 12
                Image {
                    source: "qrc:/res/tune.svg"
                    sourceSize.width: 22; sourceSize.height: 22
                    Layout.preferredWidth: 22; Layout.preferredHeight: 22
                }
                Label {
                    text: qsTr("Host profiles")
                    font.family: "DM Sans"; font.pixelSize: 18; font.bold: true
                    color: dlg._text
                }
                Label {
                    text: dlg.hostName
                    font.family: "DM Sans"; font.pixelSize: 13
                    color: dlg._dim; elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#2A2A2A" }

        // ── Profile bar: tabs + Add ─────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._rowH
            Label {
                anchors.left: parent.left; anchors.leftMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Profile")
                font.family: "DM Sans"; font.pixelSize: 15; font.bold: true
                color: dlg._text
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

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
                        radius: 8
                        color: "#1f2722"
                        border.color: offBtn.activeFocus ? dlg._accent : "#2a2a2a"
                        border.width: offBtn.activeFocus ? 3 : 1
                    }
                    Item {
                        id: offPill
                        anchors.centerIn: parent
                        implicitWidth: offLabel.implicitWidth + 28
                        implicitHeight: 30
                        width: implicitWidth; height: 30
                        Rectangle {
                            anchors.fill: parent; anchors.margins: 2; radius: 5
                            color: offBtn.selected ? dlg._accent : "transparent"
                        }
                        Label {
                            id: offLabel
                            anchors.centerIn: parent
                            text: qsTr("Off")
                            color: offBtn.selected ? "#0d1410" : "#a0a0a0"
                            font.family: "DM Sans"; font.pixelSize: 13; font.bold: offBtn.selected
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
                        spacing: 10
                        Repeater {
                            model: profileTabs.labels
                            delegate: Rectangle {
                                id: ptTile
                                width: ptLabel.implicitWidth + 28
                                height: dlg._tabH
                                radius: 8
                                color: "#1f2722"
                                property bool _active: index === dlg.editingSlot
                                property bool _cursor: profileTabs.activeFocus && index === profileTabs.currentIndex
                                border.color: _cursor ? dlg._accent : "#2a2a2a"
                                border.width: _cursor ? 3 : 1
                                Rectangle {
                                    anchors.fill: parent; anchors.margins: 2; radius: 5
                                    color: ptTile._active ? dlg._accent : "transparent"
                                }
                                Label {
                                    id: ptLabel
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: ptTile._active ? "#0d1410" : "#a0a0a0"
                                    font.family: "DM Sans"; font.pixelSize: 13; font.bold: ptTile._active
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
                    implicitWidth: addPill.implicitWidth + 12
                    width: implicitWidth; height: dlg._tabH

                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: "#1f2722"
                        border.color: addBtn.activeFocus ? dlg._accent : "#2a2a2a"
                        border.width: addBtn.activeFocus ? 3 : 1
                    }
                    Item {
                        id: addPill
                        anchors.centerIn: parent
                        implicitWidth: addLabel.implicitWidth + 28
                        implicitHeight: 30
                        width: implicitWidth; height: 30
                        Label {
                            id: addLabel
                            anchors.centerIn: parent
                            text: qsTr("+ Add")
                            color: dlg._accent
                            font.family: "DM Sans"; font.pixelSize: 13; font.bold: true
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
            Layout.preferredHeight: 130
            visible: dlg.profileCount === 0
            Label {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.25
                text: qsTr("No profiles yet.\nAdd one to override streaming settings for this host\n(e.g. a “docked” 4K profile and a “portable” 1080p profile).")
                font.family: "DM Sans"; font.pixelSize: 14
                color: dlg._dim
            }
        }

        // ── OFF state (profiles exist but none active) ──────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            visible: dlg.profileCount > 0 && !dlg._editing
            Label {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.25
                text: qsTr("No profile active — this host uses your global settings.\nSelect a profile above to activate and edit it.")
                font.family: "DM Sans"; font.pixelSize: 14
                color: dlg._dim
            }
        }

        // ── Name + override rows (only while a profile is selected) ─────────
        Flickable {
            id: flick
            Layout.fillWidth: true
            visible: dlg._editing
            Layout.preferredHeight: dlg._editing ? Math.min(
                (Overlay.overlay ? Overlay.overlay.height - 170 : 800),
                rowsCol.implicitHeight) : 0
            contentHeight: rowsCol.implicitHeight
            clip: true
            interactive: contentHeight > height
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: rowsCol
                width: flick.width
                spacing: 0

                component SettingRow: Item {
                    width: rowsCol.width
                    height: dlg._rowH
                    property string label: ""
                    default property alias content: holder.data
                    Label {
                        anchors.left: parent.left; anchors.leftMargin: dlg._padX
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.label
                        font.family: "DM Sans"; font.pixelSize: 15; font.bold: true
                        color: dlg._text
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
                        font.family: "DM Sans"; font.pixelSize: 15; font.bold: true
                        color: dlg._text
                    }
                    TextField {
                        id: nameField
                        anchors.right: parent.right; anchors.rightMargin: dlg._padX
                        anchors.verticalCenter: parent.verticalCenter
                        width: 320; height: 38
                        leftPadding: 12; rightPadding: 12
                        maximumLength: dlg._maxNameLen
                        font.family: "DM Sans"; font.pixelSize: 14
                        color: dlg._text
                        selectByMouse: true
                        background: Rectangle {
                            radius: 9
                            color: "#101316"
                            border.color: nameField.activeFocus ? dlg._accent : "#2a2a2a"
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
                        spacing: 12
                        SegmentedSelector {
                            id: resSel; labels: dlg._resLabels
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
                    SegmentedSelector {
                        id: fpsSel; labels: dlg._fpsLabels
                        KeyNavigation.up: resCustomBtn
                        KeyNavigation.down: bitrateGlobalBtn
                        onActivated: dlg.saveOverride()
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
                        font.family: "DM Sans"; font.pixelSize: 15; font.bold: true
                        color: dlg._text
                    }

                    Row {
                        anchors.right: parent.right; anchors.rightMargin: dlg._padX
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 16

                        // Global pill — built to match a single SegmentedSelector
                        // pill exactly (same container, pill geometry and sizes).
                        FocusScope {
                            id: bitrateGlobalBtn
                            activeFocusOnTab: true
                            anchors.verticalCenter: parent.verticalCenter
                            property bool selected: !dlg._bitrateOverridden
                            implicitHeight: 36
                            implicitWidth: gPill.implicitWidth + 8
                            width: implicitWidth; height: 36

                            Rectangle {
                                anchors.fill: parent
                                radius: 8
                                color: "#1f2722"
                                border.color: bitrateGlobalBtn.activeFocus ? dlg._accent : "#2a2a2a"
                                border.width: bitrateGlobalBtn.activeFocus ? 3 : 1
                            }
                            Item {
                                id: gPill
                                anchors.centerIn: parent
                                implicitWidth: gLabel.implicitWidth + 28
                                implicitHeight: 30
                                width: implicitWidth; height: 30
                                Rectangle {
                                    anchors.fill: parent; anchors.margins: 2; radius: 5
                                    color: bitrateGlobalBtn.selected ? dlg._accent : "transparent"
                                }
                                Label {
                                    id: gLabel
                                    anchors.centerIn: parent
                                    text: qsTr("Global")
                                    color: bitrateGlobalBtn.selected ? "#0d1410" : "#a0a0a0"
                                    font.family: "DM Sans"; font.pixelSize: 13
                                    font.bold: bitrateGlobalBtn.selected
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { bitrateGlobalBtn.forceActiveFocus(); dlg.setBitrateGlobal() }
                            }
                            Keys.onReturnPressed: dlg.setBitrateGlobal()
                            Keys.onEnterPressed:  dlg.setBitrateGlobal()
                            Keys.onSpacePressed:  dlg.setBitrateGlobal()
                            KeyNavigation.up: fpsSel
                            KeyNavigation.down: bitrateSlider
                            KeyNavigation.right: bitrateSlider
                        }

                        // Focus ring + slider — replicates Settings → Video → Video bitrate.
                        Item {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 240; height: 36

                            Rectangle {   // focus ring (FocusFrame equivalent)
                                anchors.fill: parent; anchors.margins: -2
                                radius: 6; color: "transparent"
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
                                    height: 3; radius: 2
                                    color: "#f0f0f0"
                                }
                                handle: Rectangle {
                                    x: bitrateSlider.leftPadding + bitrateSlider.visualPosition * (bitrateSlider.availableWidth - width)
                                    y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                    implicitWidth: 14; implicitHeight: 14; radius: 7
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
                            width: 78
                            text: (bitrateSlider.value / 1000).toFixed(0) + qsTr(" Mbps")
                            color: dlg._accent
                            font.family: "JetBrains Mono"; font.pixelSize: 13; font.bold: true
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
                        KeyNavigation.up: bitrateSlider
                        KeyNavigation.down: codecSel
                        onActivated: dlg.saveOverride()
                    }
                }
                SettingRow {
                    label: qsTr("Video codec")
                    SegmentedSelector {
                        id: codecSel; labels: dlg._codecLabels
                        KeyNavigation.up: hdrSel
                        KeyNavigation.down: fpSel
                        onActivated: dlg.saveOverride()
                    }
                }
                SettingRow {
                    label: qsTr("Frame pacing")
                    SegmentedSelector {
                        id: fpSel; labels: dlg._fpLabels
                        KeyNavigation.up: codecSel
                        KeyNavigation.down: audSel
                        onActivated: dlg.saveOverride()
                    }
                }
                SettingRow {
                    label: qsTr("Audio")
                    SegmentedSelector {
                        id: audSel; labels: dlg._audLabels
                        KeyNavigation.up: fpSel
                        KeyNavigation.down: hueSel
                        onActivated: dlg.saveOverride()
                    }
                }
                SettingRow {
                    label: qsTr("Philips Hue")
                    SegmentedSelector {
                        id: hueSel; labels: dlg._hdrLabels
                        KeyNavigation.up: audSel
                        KeyNavigation.down: removeBtn
                        onActivated: dlg.saveOverride()
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2A2A2A" }

        // ── Footer: Remove (left) · Done + Reset to Global (right) ──────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 52

            DialogButton {
                id: removeBtn
                visible: dlg._editing
                anchors.left: parent.left; anchors.leftMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Remove")
                danger: true
                fontSize: 13
                width: 84; height: 36
                onActivated: dlg.removeProfile()
                KeyNavigation.up: dlg._editing ? hueSel : addBtn
                KeyNavigation.right: doneBtn
            }

            Row {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                DialogButton {
                    id: doneBtn
                    text: qsTr("Done")
                    affirmative: true
                    fontSize: 13
                    width: 76; height: 36
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
                    width: 128; height: 36
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
}
