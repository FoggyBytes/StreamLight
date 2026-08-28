import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.2

import StreamingPreferences 1.0

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
    readonly property color _line:   "#242424"
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
    readonly property int   _chromeH: _px(44) + _px(52) + _px(48)

    // Label for the index-0 "inherit" option: the active profile's name, or "Global".
    readonly property string _inheritLabel: activeProfileName.length > 0 ? activeProfileName : qsTr("Global")

    // value tables (index 0 == inherit placeholder, labelled by _inheritLabel)
    readonly property var _resLabels: [_inheritLabel, "720p", "1080p", "1440p", "4K"]
    readonly property var _resW:      [0, 1280, 1920, 2560, 3840]
    readonly property var _resH:      [0, 720, 1080, 1440, 2160]
    readonly property var _fpsLabels: [_inheritLabel, "30", "60", "90", "120"]
    readonly property var _fpsVals:   [0, 30, 60, 90, 120]
    readonly property var _hdrLabels: [_inheritLabel, "On", "Off"]
    readonly property var _codecLabels: [_inheritLabel, "H.264", "HEVC", "AV1"]
    readonly property var _codecVals:   [-1, 1, 2, 4]   // VCC_FORCE_H264/HEVC/AV1
    readonly property var _fpLabels:  [_inheritLabel, "Off", "On"]
    readonly property var _fpVals:    [-1, 0, 1]  // FP_OFF / FP_ON
    readonly property var _audLabels: [_inheritLabel, "Stereo", "5.1", "7.1"]
    readonly property var _audVals:   [-1, 0, 1, 2]     // AC_STEREO/51/71

    // Bitrate override state (kbps)
    property bool _bitrateOverridden: false
    // Custom resolution override (0 == none / inheriting or using a preset).
    property int _customResW: 0
    property int _customResH: 0

    modal: true
    dim: true
    focus: true                       // grab keyboard/gamepad focus when shown
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    width: dlg._px(780)
    padding: dlg._px(0)

    Overlay.modal: Rectangle { color: "#cc000000" }

    background: Rectangle {
        color: Theme.card
        radius: dlg._px(14)
        border.color: Theme.line
        border.width: 1
    }

    onOpened: { loadFromModel(); resSel.forceActiveFocus() }
    onClosed: dlg.closedByUser()

    function _idxByVal(arr, v) {
        var i = arr.indexOf(v)
        return i > 0 ? i : 0
    }

    function loadFromModel() {
        if (!appModel || appIndex < 0) return
        var ov = appModel.getAppOverride(appIndex)
        // Resolution is tri-state: inherit (0) / preset (>0) / custom (-1 + _customRes*).
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
        if (fpsSel.currentIndex > 0)   m.fps = _fpsVals[fpsSel.currentIndex]
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

    contentItem: ColumnLayout {
        spacing: dlg._px(0)

        // ── Header (title + app name inline) ────────────────────────────────
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
                    text: qsTr("Per-game settings")
                    font.family: "DM Sans"; font.pixelSize: dlg._px(18); font.bold: true
                    color: dlg._text
                }
                Label {
                    text: dlg.appName
                    font.family: "DM Sans"; font.pixelSize: dlg._px(13)
                    color: dlg._dim; elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        // ── Scrollable rows (content-sized; scrolls only on tiny screens) ────
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(
                (Overlay.overlay ? Overlay.overlay.height - dlg._chromeH : dlg._px(800)),
                rowsCol.implicitHeight)
            contentHeight: rowsCol.implicitHeight
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

                SettingRow {
                    label: qsTr("Resolution")
                    Row {
                        spacing: dlg._px(12)
                        SegmentedSelector {
                            id: resSel; labels: dlg._resLabels
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
                    SegmentedSelector {
                        id: fpsSel; labels: dlg._fpsLabels
                        KeyNavigation.up: resCustomBtn
                        KeyNavigation.down: bitrateGlobalBtn
                        onActivated: dlg.saveToModel()
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

                        // Global pill — built to match a single SegmentedSelector
                        // pill exactly (same container, pill geometry and sizes).
                        FocusScope {
                            id: bitrateGlobalBtn
                            activeFocusOnTab: true
                            anchors.verticalCenter: parent.verticalCenter
                            property bool selected: !dlg._bitrateOverridden
                            implicitHeight: dlg._px(36)
                            implicitWidth: gPill.implicitWidth + 8
                            width: implicitWidth; height: 36

                            Rectangle {
                                anchors.fill: parent
                                radius: dlg._px(8)
                                color: Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07))
                                border.color: bitrateGlobalBtn.activeFocus ? dlg._accent : Theme.line
                                border.width: bitrateGlobalBtn.activeFocus ? 3 : 1
                            }
                            Item {
                                id: gPill
                                anchors.centerIn: parent
                                implicitWidth: gLabel.implicitWidth + dlg._px(28)
                                implicitHeight: dlg._px(30)
                                width: implicitWidth; height: 30
                                Rectangle {
                                    anchors.fill: parent; anchors.margins: 2; radius: dlg._px(5)
                                    color: bitrateGlobalBtn.selected ? dlg._accent : "transparent"
                                }
                                Label {
                                    id: gLabel
                                    anchors.centerIn: parent
                                    text: dlg._inheritLabel
                                    color: bitrateGlobalBtn.selected ? Theme.onAccent : Theme.text2
                                    font.family: "DM Sans"; font.pixelSize: dlg._px(13)
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

                                // Verbatim Settings look: solid white track + green handle.
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
                        KeyNavigation.up: bitrateSlider
                        KeyNavigation.down: codecSel
                        onActivated: dlg.saveToModel()
                    }
                }
                SettingRow {
                    label: qsTr("Video codec")
                    SegmentedSelector {
                        id: codecSel; labels: dlg._codecLabels
                        KeyNavigation.up: hdrSel
                        KeyNavigation.down: fpSel
                        onActivated: dlg.saveToModel()
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
                    SegmentedSelector {
                        id: fpSel; labels: dlg._fpLabels
                        KeyNavigation.up: codecSel
                        KeyNavigation.down: audSel
                        onActivated: dlg.saveToModel()
                    }
                }
                // ⚠️ Row order groups by what a setting acts on, not by when it was added:
                // picture first, then audio, then the two that are about the session around
                // the stream. Kept in step with HostProfilesDialog, which carries the same
                // rows plus the host-only ones. Anything appended to the end from now on has
                // to earn its place — and the KeyNavigation chain has to be walked end to end
                // whenever a row moves.
                SettingRow {
                    label: qsTr("Audio")
                    SegmentedSelector {
                        id: audSel; labels: dlg._audLabels
                        KeyNavigation.up: fpSel
                        KeyNavigation.down: waitGameSel
                        onActivated: dlg.saveToModel()
                    }
                }
                // Turn it off for a game that opens its own launcher: no game window ever
                // reaches the host's screen, so the wait has nothing to end on and sits there
                // until the user presses B. Per game, because only the person who owns the
                // game knows it behaves that way.
                SettingRow {
                    label: qsTr("Wait for the game to appear")
                    SegmentedSelector {
                        id: waitGameSel; labels: dlg._hdrLabels
                        KeyNavigation.up: audSel
                        KeyNavigation.down: hueSel
                        onActivated: dlg.saveToModel()
                    }
                }
                // Sits with the row above because it belongs to the same context: both are
                // about what happens around the stream rather than to the picture.
                SettingRow {
                    label: qsTr("Philips Hue")
                    SegmentedSelector {
                        id: hueSel; labels: dlg._hdrLabels
                        KeyNavigation.up: waitGameSel
                        KeyNavigation.down: doneBtn
                        onActivated: dlg.saveToModel()
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        // ── Footer: Done + Reset to Global (right) ──────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: dlg._px(52)

            Row {
                anchors.right: parent.right; anchors.rightMargin: dlg._padX
                anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(8)

                DialogButton {
                    id: doneBtn
                    text: qsTr("Done")
                    affirmative: true
                    fontSize: 13
                    width: dlg._px(76); height: dlg._px(36)
                    onActivated: dlg.close()
                    KeyNavigation.up: hueSel
                    KeyNavigation.right: resetBtn
                }
                DialogButton {
                    id: resetBtn
                    text: qsTr("Reset to Global")
                    fontSize: 13
                    width: dlg._px(128); height: dlg._px(36)
                    onActivated: dlg.resetAll()
                    KeyNavigation.up: hueSel
                    KeyNavigation.left: doneBtn
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
}
