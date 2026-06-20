import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.2
import QtQuick.Window 2.2

import StreamingPreferences 1.0
import ComputerManager 1.0
import SdlGamepadKeyNavigation 1.0
import SystemProperties 1.0

// SettingsScreen — Xbox-style flat settings panel.
// 6 tabs: Video, Audio, Input, Decoder, Network, Session.
// Each tab body is a Column of "sections"; each section is a header label
// (uppercase, dim) followed by a panel of rows. Each row has a label on the
// left and a control on the right.
// Root is FocusScope (not Item) — required for activeFocus propagation from
// the Loader above to the controls inside (TabBar, switches, dropdowns).
FocusScope {
    id: settingsScreen
    anchors.fill: parent
    focus: true

    readonly property color _bg2:       "#1a1a1a"
    readonly property color _bg3:       "#202020"
    readonly property color _border:    "#2a2a2a"
    readonly property color _borderS:   "#3a3a3a"
    readonly property color _text:      "#f0f0f0"
    readonly property color _textDim:   "#a0a0a0"
    readonly property color _textMut:   "#707070"
    // Bright-green accent used everywhere (_green/_greenLk kept as aliases).
    readonly property color _green:     "#00E676"
    readonly property color _greenLk:   "#00E676"
    readonly property color _focus:     "#00E676"
    readonly property int   _focusBd:   3
    readonly property int   _rowHeight: 58
    readonly property int   _rowHeightTall: 76
    readonly property int   _gapY:      24

    // Read by AppShell to show the "X · Default" status-bar prompt.
    property bool bitrateNonDefault: false

    // Active host-profile context (set by AppShell when Settings is opened from a
    // host with an active profile). Rows whose key the profile overrides are shown
    // greyed + disabled, since editing them here wouldn't affect that host.
    // Direct per-key bindings (not a function) so they react when the map is set.
    property var    activeProfileOverride: ({})
    property string activeProfileName: ""

    readonly property bool _lockResFps:      activeProfileOverride
                                             && (activeProfileOverride.width !== undefined
                                                 || activeProfileOverride.fps !== undefined)
    readonly property bool _lockBitrate:     activeProfileOverride && activeProfileOverride.bitrate !== undefined
    readonly property bool _lockHdr:         activeProfileOverride && activeProfileOverride.hdr !== undefined
    readonly property bool _lockCodec:       activeProfileOverride && activeProfileOverride.codec !== undefined
    readonly property bool _lockFramePacing: activeProfileOverride && activeProfileOverride.framepacing !== undefined
    readonly property bool _lockAudio:       activeProfileOverride && activeProfileOverride.audio !== undefined
    readonly property bool _lockHue:         activeProfileOverride && activeProfileOverride.hue !== undefined

    // Latest-release tags fetched once per Settings open from the GitHub API.
    property string streamLightLatest: ""
    property string streamTweakLatest: ""

    function _fetchLatestTag(repo, callback) {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", "https://api.github.com/repos/FoggyBytes/" + repo + "/releases/latest")
        xhr.setRequestHeader("Accept", "application/vnd.github+json")
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            if (xhr.status !== 200) { callback(""); return }
            try {
                var data = JSON.parse(xhr.responseText)
                callback(data.tag_name || "")
            } catch (e) { callback("") }
        }
        xhr.send()
    }

    function resetBitrateToDefault() {
        if (!bitrateSlider) return
        var def = StreamingPreferences.getDefaultBitrate(
                      StreamingPreferences.width, StreamingPreferences.height,
                      StreamingPreferences.fps, StreamingPreferences.enableYUV444)
        StreamingPreferences.bitrateKbps       = def
        StreamingPreferences.autoAdjustBitrate = true
        bitrateSlider.value                    = def
    }

    // LB/RB cycle tabs; X (Menu) resets the bitrate when non-default.
    Keys.onPressed: {
        if (event.key === Qt.Key_PageUp) {
            if (tabBar.currentIndex > 0) tabBar.currentIndex--
            event.accepted = true
        } else if (event.key === Qt.Key_PageDown) {
            if (tabBar.currentIndex < tabBar.count - 1) tabBar.currentIndex++
            event.accepted = true
        } else if (event.key === Qt.Key_Menu && settingsScreen.bitrateNonDefault) {
            settingsScreen.resetBitrateToDefault()
            event.accepted = true
        }
    }

    // Focus the first control of the given tab (so D-pad starts inside the body).
    function focusFirstControl(idx) {
        switch (idx) {
            case 0: if (resolutionSelector)    resolutionSelector.forceActiveFocus();    break
            case 1: if (audioConfigSelector)   audioConfigSelector.forceActiveFocus();   break
            case 2: if (absMouseSwitch)        absMouseSwitch.forceActiveFocus();        break
            case 3: if (decoderSelector)       decoderSelector.forceActiveFocus();       break
            case 4: if (mdnsSwitch)            mdnsSwitch.forceActiveFocus();            break
            case 5: if (gameOptSwitch)         gameOptSwitch.forceActiveFocus();         break
            case 6: if (overlayModeSelector)   overlayModeSelector.forceActiveFocus();   break
            case 7: if (aboutSlGithubBtn)      aboutSlGithubBtn.forceActiveFocus();      break
        }
    }

    // Re-focus on every activation (the Loader transfers focus AFTER ctor).
    onActiveFocusChanged: {
        if (activeFocus) {
            Qt.callLater(function() { focusFirstControl(tabBar.currentIndex) })
        }
    }

    Component.onCompleted: {
        SdlGamepadKeyNavigation.setUiNavMode(true)
        Qt.callLater(function() { focusFirstControl(tabBar.currentIndex) })
        _fetchLatestTag("StreamLight",  function(t) { settingsScreen.streamLightLatest  = t })
        _fetchLatestTag("StreamTweak",  function(t) { settingsScreen.streamTweakLatest  = t })
    }

    Component.onDestruction: {
        SdlGamepadKeyNavigation.setUiNavMode(false)
        StreamingPreferences.save()
    }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        height: 56

        Label {
            id: headerTitle
            text: qsTr("Settings")
            font.family: "DM Sans"
            font.pixelSize: 28
            font.bold: true
            color: settingsScreen._text
            anchors.top: parent.top
            anchors.left: parent.left
        }

        Label {
            text: qsTr("Configure quality, audio, input and network for your streaming sessions")
            font.family: "DM Sans"
            font.pixelSize: 14
            color: settingsScreen._textDim
            anchors.top: headerTitle.bottom
            anchors.left: parent.left
            anchors.topMargin: 4
        }
    }

    TabBar {
        id: tabBar
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        anchors.topMargin: 8
        height: 48
        // Tab switching is LB/RB only — never via D-pad focus traversal.
        focusPolicy: Qt.NoFocus
        onCurrentIndexChanged: settingsScreen.focusFirstControl(currentIndex)

        background: Rectangle {
            color: "transparent"
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: settingsScreen._border
            }
        }

        TabButton {
            text: qsTr("Video")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 0 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 0 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Audio")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 1 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 1 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Input")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 2 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 2 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Decoder")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 3 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 3 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Network")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 4 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 4 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Session")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 5 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 5 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Overlay")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 6 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 6 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("About")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 7 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 7 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // Green outline shown around the active control when it has focus.
    // Hidden in pointer mode so the keyboard/gamepad focus ring does not
    // linger on the last-focused control while the user is driving with
    // the mouse (would otherwise highlight two controls at once).
    component FocusFrame: Rectangle {
        property Item target
        color: "transparent"
        radius: 6
        border.color: settingsScreen._focus
        border.width: settingsScreen._focusBd
        visible: target && target.activeFocus && SdlGamepadKeyNavigation.inputMode !== "pointer"
        z: 1
    }

    // Notice placed at the top of any settings sub-block that contains rows locked
    // by the active host profile. Collapses to zero height when not active.
    component ProfileLockNotice: Item {
        property bool active: false
        width: parent ? parent.width : 0
        visible: active && settingsScreen.activeProfileName.length > 0
        height: visible ? settingsScreen._rowHeight : 0
        Row {
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Label {
                text: "🔒"; font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                width: parent.width - 28
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Greyed settings are controlled by the active host profile “%1”.")
                      .arg(settingsScreen.activeProfileName)
                font.family: "DM Sans"; font.pixelSize: 13
                color: settingsScreen._textDim
                wrapMode: Text.WordWrap
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            x: 16; width: parent.width - 32; height: 1; color: settingsScreen._border
        }
    }

    Flickable {
        id: contentFlick
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        anchors.topMargin: 16
        anchors.bottomMargin: 20

        boundsBehavior: Flickable.OvershootBounds
        clip: true
        contentWidth: tabContent.width
        contentHeight: tabContent.implicitHeight

        // LB/RB must reach the tab switcher even if focus is inside the Flickable.
        Keys.forwardTo: [settingsScreen]

        // Auto-scroll: keep the focused row in view as D-pad navigation moves
        // through the tab body. Otherwise focus would land on off-screen rows.
        // focus to rows that the user can't see until they mouse-scroll.
        property Item activeFocusItem: Window.activeFocusItem
        onActiveFocusItemChanged: {
            if (!activeFocusItem) return
            // Walk up to verify the focused item belongs to this Flickable.
            var p = activeFocusItem
            var inside = false
            while (p) {
                if (p === contentFlick) { inside = true; break }
                p = p.parent
            }
            if (!inside) return

            var pos    = activeFocusItem.mapToItem(tabContent, 0, 0)
            var top    = pos.y
            var bottom = pos.y + activeFocusItem.height
            var margin = 24

            if (top < contentY + margin) {
                contentY = Math.max(0, top - margin)
            } else if (bottom > contentY + height - margin) {
                contentY = Math.min(
                    Math.max(0, contentHeight - height),
                    bottom - height + margin
                )
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: contentFlick.contentHeight > contentFlick.height
                    ? ScrollBar.AsNeeded
                    : ScrollBar.AlwaysOff
        }

        Item {
            id: tabContent
            width: contentFlick.width - 16
            // Use only the active tab's column height; childrenRect would
            // include invisible sibling tabs and oversize the scrollbar.
            implicitHeight: {
                switch (tabBar.currentIndex) {
                    case 0: return videoTab.implicitHeight
                    case 1: return audioTab.implicitHeight
                    case 2: return inputTab.implicitHeight
                    case 3: return decoderTab.implicitHeight
                    case 4: return networkTab.implicitHeight
                    case 5: return sessionTab.implicitHeight
                    case 6: return overlayTab.implicitHeight
                    case 7: return aboutTab.implicitHeight
                }
                return 0
            }

            // ──────────────────────────────────────────────────────────────────
            //                              VIDEO TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: videoTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 0
                spacing: 16

                // ── Section: VIDEO ────────────────────────────────────────────
                Label {
                    text: qsTr("Video")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: videoCol.implicitHeight + 8

                    Column {
                        id: videoCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        // Active-profile notice — shown only when this VIDEO block
                        // actually has a setting locked by the active profile.
                        ProfileLockNotice {
                            active: settingsScreen._lockResFps
                                    || settingsScreen._lockBitrate
                                    || settingsScreen._lockFramePacing
                        }

                        // ── Resolution / Frame rate ───────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight
                            enabled: !settingsScreen._lockResFps
                            opacity: enabled ? 1.0 : 0.4

                            Label {
                                text: qsTr("Resolution / Frame rate")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: resolutionSelector
                                anchors.right: fpsSelector.left
                                anchors.rightMargin: 24
                                anchors.verticalCenter: parent.verticalCenter

                                property var _widths:  [1280, 1920, 2560, 3840]
                                property var _heights: [720,  1080, 1440, 2160]
                                property var _presetLabels: ["720p", "1080p", "1440p", "4K"]

                                function _resync() {
                                    for (var i = 0; i < _widths.length; i++) {
                                        if (_widths[i] === StreamingPreferences.width
                                         && _heights[i] === StreamingPreferences.height) {
                                            currentIndex = i
                                            return
                                        }
                                    }
                                    // Non-preset → -1 (no pill highlighted). If we are at
                                    // construction time, prepend a "Custom" pill instead so
                                    // the user can still see / re-select their saved value.
                                    currentIndex = -1
                                }

                                Component.onCompleted: {
                                    for (var i = 0; i < _widths.length; i++) {
                                        if (_widths[i] === StreamingPreferences.width
                                         && _heights[i] === StreamingPreferences.height) {
                                            labels = _presetLabels
                                            currentIndex = i
                                            return
                                        }
                                    }
                                    var custom = qsTr("Custom")
                                    labels   = [custom].concat(_presetLabels)
                                    _widths  = [StreamingPreferences.width].concat(_widths)
                                    _heights = [StreamingPreferences.height].concat(_heights)
                                    currentIndex = 0
                                }

                                // Re-sync if width/height change from elsewhere.
                                Connections {
                                    target: StreamingPreferences
                                    function onWidthChanged()  { resolutionSelector._resync() }
                                    function onHeightChanged() { resolutionSelector._resync() }
                                }

                                onActivated: function(idx) {
                                    var w = _widths[idx], h = _heights[idx]
                                    if (StreamingPreferences.width !== w || StreamingPreferences.height !== h) {
                                        StreamingPreferences.width  = w
                                        StreamingPreferences.height = h
                                        if (StreamingPreferences.autoAdjustBitrate) {
                                            StreamingPreferences.bitrateKbps = StreamingPreferences.getDefaultBitrate(
                                                w, h, StreamingPreferences.fps, StreamingPreferences.enableYUV444)
                                            bitrateSlider.value = StreamingPreferences.bitrateKbps
                                        }
                                    }
                                }
                            }

                            SegmentedSelector {
                                id: fpsSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                property var _fps: [30, 60, 90, 120]
                                property var _presetLabels: ["30", "60", "90", "120"]

                                function _resync() {
                                    for (var i = 0; i < _fps.length; i++) {
                                        if (_fps[i] === StreamingPreferences.fps) {
                                            currentIndex = i
                                            return
                                        }
                                    }
                                    currentIndex = -1
                                }

                                Component.onCompleted: {
                                    for (var i = 0; i < _fps.length; i++) {
                                        if (_fps[i] === StreamingPreferences.fps) {
                                            labels = _presetLabels
                                            currentIndex = i
                                            return
                                        }
                                    }
                                    labels = [String(StreamingPreferences.fps)].concat(_presetLabels)
                                    _fps   = [StreamingPreferences.fps].concat(_fps)
                                    currentIndex = 0
                                }

                                Connections {
                                    target: StreamingPreferences
                                    function onFpsChanged() { fpsSelector._resync() }
                                }

                                onActivated: function(idx) {
                                    var f = _fps[idx]
                                    if (StreamingPreferences.fps !== f) {
                                        StreamingPreferences.fps = f
                                        if (StreamingPreferences.autoAdjustBitrate) {
                                            StreamingPreferences.bitrateKbps = StreamingPreferences.getDefaultBitrate(
                                                StreamingPreferences.width, StreamingPreferences.height,
                                                f, StreamingPreferences.enableYUV444)
                                            bitrateSlider.value = StreamingPreferences.bitrateKbps
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Video bitrate ─────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: !settingsScreen._lockBitrate
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Video bitrate")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Raise for higher quality on fast connections")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: bitrateSlider; anchors.margins: -6; target: bitrateSlider }

                            // Bridges current vs recommended → AppShell status-bar X·Default hint.
                            Binding {
                                target: settingsScreen
                                property: "bitrateNonDefault"
                                value: StreamingPreferences.bitrateKbps !== StreamingPreferences.getDefaultBitrate(
                                            StreamingPreferences.width, StreamingPreferences.height,
                                            StreamingPreferences.fps, StreamingPreferences.enableYUV444)
                            }

                            Slider {
                                id: bitrateSlider
                                anchors.right: bitrateValueLabel.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                width: 240

                                from: 500
                                to: StreamingPreferences.unlockBitrate ? 500000 : 150000
                                stepSize: 500
                                snapMode: Slider.SnapAlways
                                value: StreamingPreferences.bitrateKbps

                                onValueChanged: { StreamingPreferences.bitrateKbps = value }
                                onMoved:        { StreamingPreferences.autoAdjustBitrate = false }

                                // Hold-to-accelerate on ◀/▶: tap = ±0.5 Mbps, hold ramps 1×→20×.
                                property int  _accelDir: 0       // -1 / 0 / +1
                                property int  _accelTicks: 0

                                Timer {
                                    id: bitrateAccelTimer
                                    interval: 60
                                    repeat: true
                                    onTriggered: {
                                        if (bitrateSlider._accelDir === 0) { stop(); return }
                                        bitrateSlider._accelTicks++
                                        var mult = Math.min(20, 1 + Math.floor(bitrateSlider._accelTicks / 4))
                                        var delta = bitrateSlider._accelDir * bitrateSlider.stepSize * mult
                                        var v = Math.max(bitrateSlider.from,
                                                Math.min(bitrateSlider.to, bitrateSlider.value + delta))
                                        if (v !== bitrateSlider.value) {
                                            bitrateSlider.value = v
                                            StreamingPreferences.autoAdjustBitrate = false
                                        }
                                    }
                                }

                                function _startAccel(dir) {
                                    var v = Math.max(from, Math.min(to, value + dir * stepSize))
                                    if (v !== value) { value = v; StreamingPreferences.autoAdjustBitrate = false }
                                    _accelDir   = dir
                                    _accelTicks = 0
                                    bitrateAccelTimer.start()
                                }
                                function _stopAccel() {
                                    _accelDir = 0
                                    _accelTicks = 0
                                    bitrateAccelTimer.stop()
                                }

                                Keys.onPressed: {
                                    if (event.isAutoRepeat) { event.accepted = true; return }
                                    if (event.key === Qt.Key_Left)  { _startAccel(-1); event.accepted = true }
                                    if (event.key === Qt.Key_Right) { _startAccel(+1); event.accepted = true }
                                }
                                Keys.onReleased: {
                                    if (event.isAutoRepeat) { event.accepted = true; return }
                                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                                        _stopAccel()
                                        event.accepted = true
                                    }
                                }

                                background: Rectangle {
                                    x: bitrateSlider.leftPadding
                                    y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                    width: bitrateSlider.availableWidth
                                    height: 3
                                    radius: 2
                                    color: "#f0f0f0"
                                }
                                handle: Rectangle {
                                    x: bitrateSlider.leftPadding + bitrateSlider.visualPosition * (bitrateSlider.availableWidth - width)
                                    y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                    implicitWidth: 14
                                    implicitHeight: 14
                                    radius: 7
                                    color: bitrateSlider.pressed ? Qt.lighter(settingsScreen._green, 1.2)
                                         : bitrateSlider.hovered ? Qt.lighter(settingsScreen._green, 1.1)
                                         :                         settingsScreen._green
                                    border.color: settingsScreen._green
                                    border.width: 1
                                }
                            }

                            Label {
                                id: bitrateValueLabel
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: (StreamingPreferences.bitrateKbps / 1000).toFixed(0) + " Mbps"
                                font.family: "JetBrains Mono"
                                font.pixelSize: 13
                                font.bold: true
                                color: settingsScreen._green
                                horizontalAlignment: Text.AlignRight
                                width: 80
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Display mode ──────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Display mode")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Fullscreen has the best performance. Borderless windowed allows Alt+Tab, screenshots and overlays.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            SegmentedSelector {
                                id: displayModeSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Fullscreen"), qsTr("Borderless"), qsTr("Windowed")]
                                property var _values: [
                                    StreamingPreferences.WM_FULLSCREEN,
                                    StreamingPreferences.WM_FULLSCREEN_DESKTOP,
                                    StreamingPreferences.WM_WINDOWED
                                ]

                                // Reactive: re-evaluates whenever the underlying preference
                                // changes (here or elsewhere), keeping the highlighted pill
                                // in sync without an explicit setter call.
                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.windowMode
                                        for (var i = 0; i < displayModeSelector._values.length; i++) {
                                            if (displayModeSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.windowMode = _values[idx] }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── V-Sync ────────────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("V-Sync")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Disabling reduces latency but may cause visible tearing")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: vsyncSwitch; anchors.margins: -3; target: vsyncSwitch }
                            STSwitch {
                                id: vsyncSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.enableVsync
                                onCheckedChanged: { StreamingPreferences.enableVsync = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Frame Pacing ──────────────────────────────────────
                        Item {
                            width: parent.width
                            height: Math.max(settingsScreen._rowHeightTall, fpCol.implicitHeight + 16)
                            enabled: !settingsScreen._lockFramePacing
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                id: fpCol
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: framePacingSelector.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Frame Pacing")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("Removes judder on high-refresh displays. Software paces every frame; Hardware locks the GPU cadence for a multiple-refresh display (e.g. 120 Hz / 60 FPS = 2×, 240 Hz / 60 FPS = 4×).")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            SegmentedSelector {
                                id: framePacingSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Off"), qsTr("Automatic"), qsTr("Software"), qsTr("Hardware")]
                                property var _values: [
                                    StreamingPreferences.FP_OFF,
                                    StreamingPreferences.FP_AUTO,
                                    StreamingPreferences.FP_MATCHED,
                                    StreamingPreferences.FP_MULTIPLE
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.framePacingMode
                                        for (var i = 0; i < framePacingSelector._values.length; i++) {
                                            if (framePacingSelector._values[i] === v) return i
                                        }
                                        return 0
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.framePacingMode = _values[idx] }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                              AUDIO TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: audioTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 1
                spacing: 16

                Label {
                    text: qsTr("Audio")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: audioCol.implicitHeight + 8

                    Column {
                        id: audioCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        ProfileLockNotice { active: settingsScreen._lockAudio }

                        // ── Audio configuration ───────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight
                            enabled: !settingsScreen._lockAudio
                            opacity: enabled ? 1.0 : 0.4

                            Label {
                                text: qsTr("Audio configuration")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: audioConfigSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Stereo"), qsTr("5.1"), qsTr("7.1")]
                                property var _values: [
                                    StreamingPreferences.AC_STEREO,
                                    StreamingPreferences.AC_51_SURROUND,
                                    StreamingPreferences.AC_71_SURROUND
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.audioConfig
                                        for (var i = 0; i < audioConfigSelector._values.length; i++) {
                                            if (audioConfigSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.audioConfig = _values[idx] }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Mute host PC speakers ─────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Mute host PC speakers during streaming")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Restart any in-progress game for this to take effect")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: muteHostSwitch; anchors.margins: -3; target: muteHostSwitch }
                            STSwitch {
                                id: muteHostSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !StreamingPreferences.playAudioOnHost
                                onCheckedChanged: { StreamingPreferences.playAudioOnHost = !checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Mute when window not focused ──────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Mute audio when window is not focused")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Mutes when you Alt+Tab away or click on another window")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: muteFocusSwitch; anchors.margins: -3; target: muteFocusSwitch }
                            STSwitch {
                                id: muteFocusSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.muteOnFocusLoss
                                onCheckedChanged: { StreamingPreferences.muteOnFocusLoss = checked }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                              INPUT TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: inputTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 2
                spacing: 16

                // ── MOUSE & KEYBOARD section ──────────────────────────────────
                Label {
                    text: qsTr("Mouse & Keyboard")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: mkCol.implicitHeight + 8

                    Column {
                        id: mkCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        // ── Optimize mouse for remote desktop ─────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Optimize mouse for remote desktop")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Seamless cursor without capture. Toggle live with Ctrl+Alt+Shift+M.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: absMouseSwitch; anchors.margins: -3; target: absMouseSwitch }
                            STSwitch {
                                id: absMouseSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.absoluteMouseMode
                                onCheckedChanged: { StreamingPreferences.absoluteMouseMode = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Capture system keyboard shortcuts ─────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Capture system keyboard shortcuts")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Forwards shortcuts like Alt+Tab to the host. Ctrl+Alt+Del cannot be intercepted.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            SegmentedSelector {
                                id: captureSysKeysSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Never"), qsTr("In Game"), qsTr("Always")]
                                property var _values: [
                                    StreamingPreferences.CSK_OFF,
                                    StreamingPreferences.CSK_FULLSCREEN,
                                    StreamingPreferences.CSK_ALWAYS
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.captureSysKeysMode
                                        for (var i = 0; i < captureSysKeysSelector._values.length; i++) {
                                            if (captureSysKeysSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.captureSysKeysMode = _values[idx] }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Use touchscreen as virtual trackpad ───────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Use touchscreen as virtual trackpad")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("On: behaves like a trackpad.  Off: directly controls the pointer.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: touchSwitch; anchors.margins: -3; target: touchSwitch }
                            STSwitch {
                                id: touchSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !StreamingPreferences.absoluteTouchMode
                                onCheckedChanged: { StreamingPreferences.absoluteTouchMode = !checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Swap mouse buttons ────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Swap left and right mouse buttons")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: swapMouseSwitch; anchors.margins: -3; target: swapMouseSwitch }
                            STSwitch {
                                id: swapMouseSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.swapMouseButtons
                                onCheckedChanged: { StreamingPreferences.swapMouseButtons = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Reverse scroll direction ──────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Reverse scroll direction")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: revScrollSwitch; anchors.margins: -3; target: revScrollSwitch }
                            STSwitch {
                                id: revScrollSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.reverseScrollDirection
                                onCheckedChanged: { StreamingPreferences.reverseScrollDirection = checked }
                            }
                        }
                    }
                }

                // ── GAMEPAD section ───────────────────────────────────────────
                Label {
                    text: qsTr("Gamepad")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: gpCol.implicitHeight + 8

                    Column {
                        id: gpCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Swap A/B and X/Y gamepad buttons")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Nintendo-style button layout")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: swapFaceSwitch; anchors.margins: -3; target: swapFaceSwitch }
                            STSwitch {
                                id: swapFaceSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.swapFaceButtons
                                onCheckedChanged: { StreamingPreferences.swapFaceButtons = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Force gamepad #1 always connected")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Keeps a virtual pad on the host. Enable only for games that don't support hot-plug.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: singleCtrlSwitch; anchors.margins: -3; target: singleCtrlSwitch }
                            STSwitch {
                                id: singleCtrlSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !StreamingPreferences.multiController
                                onCheckedChanged: { StreamingPreferences.multiController = !checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Mouse control with gamepad (Start)")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: gamepadMouseSwitch; anchors.margins: -3; target: gamepadMouseSwitch }
                            STSwitch {
                                id: gamepadMouseSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.gamepadMouse
                                onCheckedChanged: { StreamingPreferences.gamepadMouse = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Process gamepad input in background")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Captures gamepad input even when the window is not focused")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: bgGamepadSwitch; anchors.margins: -3; target: bgGamepadSwitch }
                            STSwitch {
                                id: bgGamepadSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.backgroundGamepad
                                onCheckedChanged: { StreamingPreferences.backgroundGamepad = checked }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                            DECODER TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: decoderTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 3
                spacing: 16

                Label {
                    text: qsTr("Video decoder & codec")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: decCol.implicitHeight + 8

                    Column {
                        id: decCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        ProfileLockNotice {
                            active: settingsScreen._lockCodec || settingsScreen._lockHdr
                        }

                        // ── Video decoder ─────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Video decoder")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: decoderSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Auto"), qsTr("Software"), qsTr("Hardware")]
                                property var _values: [
                                    StreamingPreferences.VDS_AUTO,
                                    StreamingPreferences.VDS_FORCE_SOFTWARE,
                                    StreamingPreferences.VDS_FORCE_HARDWARE
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.videoDecoderSelection
                                        for (var i = 0; i < decoderSelector._values.length; i++) {
                                            if (decoderSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.videoDecoderSelection = _values[idx] }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Video codec ───────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight
                            enabled: !settingsScreen._lockCodec
                            opacity: enabled ? 1.0 : 0.4

                            Label {
                                text: qsTr("Video codec")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: codecSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Auto"), qsTr("H.264"), qsTr("HEVC"), qsTr("AV1")]
                                property var _values: [
                                    StreamingPreferences.VCC_AUTO,
                                    StreamingPreferences.VCC_FORCE_H264,
                                    StreamingPreferences.VCC_FORCE_HEVC,
                                    StreamingPreferences.VCC_FORCE_AV1
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.videoCodecConfig
                                        for (var i = 0; i < codecSelector._values.length; i++) {
                                            if (codecSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.videoCodecConfig = _values[idx] }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Enable HDR ────────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: !settingsScreen._lockHdr
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Enable HDR")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Some games require an HDR monitor on the host to enable HDR")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: hdrSwitch; anchors.margins: -3; target: hdrSwitch }
                            STSwitch {
                                id: hdrSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.enableHdr
                                onCheckedChanged: { StreamingPreferences.enableHdr = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Enable YUV 4:4:4 ──────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Enable YUV 4:4:4 (experimental)")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Better for desktop and text-heavy games. Not recommended for fast-paced action.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: yuv444Switch; anchors.margins: -3; target: yuv444Switch }
                            STSwitch {
                                id: yuv444Switch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.enableYUV444
                                onCheckedChanged: {
                                    if (StreamingPreferences.enableYUV444 !== checked) {
                                        StreamingPreferences.enableYUV444 = checked
                                        if (StreamingPreferences.autoAdjustBitrate) {
                                            StreamingPreferences.bitrateKbps = StreamingPreferences.getDefaultBitrate(
                                                StreamingPreferences.width, StreamingPreferences.height,
                                                StreamingPreferences.fps, StreamingPreferences.enableYUV444)
                                            bitrateSlider.value = StreamingPreferences.bitrateKbps
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Unlock bitrate limit ──────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Unlock bitrate limit (experimental)")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Allows very high bitrates with Sunshine hosts. Use only over wired LAN.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: vbrSwitch; anchors.margins: -3; target: vbrSwitch }
                            STSwitch {
                                id: vbrSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.unlockBitrate
                                onCheckedChanged: {
                                    StreamingPreferences.unlockBitrate = checked
                                    StreamingPreferences.bitrateKbps = Math.min(StreamingPreferences.bitrateKbps, bitrateSlider.to)
                                    bitrateSlider.value = StreamingPreferences.bitrateKbps
                                }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                            NETWORK TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: networkTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 4
                spacing: 16

                Label {
                    text: qsTr("Network")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: netCol.implicitHeight + 8

                    Column {
                        id: netCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Automatically discover PCs on local network")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: mdnsSwitch; anchors.margins: -3; target: mdnsSwitch }
                            STSwitch {
                                id: mdnsSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.enableMdns
                                onCheckedChanged: {
                                    if (StreamingPreferences.enableMdns !== checked) {
                                        StreamingPreferences.enableMdns = checked
                                    }
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Automatically detect blocked connections")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: blockDetectSwitch; anchors.margins: -3; target: blockDetectSwitch }
                            STSwitch {
                                id: blockDetectSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.detectNetworkBlocking
                                onCheckedChanged: { StreamingPreferences.detectNetworkBlocking = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Auto-reconnect on no video ────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: autoReconnectSwitch.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Automatically reconnect if the host is slow to start")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("If the host doesn't send video right away (e.g. a virtual display or HDR/AV1 encoder still warming up), StreamLight quietly retries once instead of showing an error.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                }
                            }

                            FocusFrame { anchors.fill: autoReconnectSwitch; anchors.margins: -3; target: autoReconnectSwitch }
                            Switch {
                                id: autoReconnectSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.autoReconnectNoVideo
                                onCheckedChanged: { StreamingPreferences.autoReconnectNoVideo = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // Auto-start Tailscale at StreamLight launch. When ON, Tailscale
                        // is started in the background on every StreamLight boot (only
                        // if it is not already running), so remote hosts reachable via
                        // their Tailscale IP can be discovered and streamed. The OFF
                        // toggle does NOT kill the running Tailscale instance — it only
                        // prevents the next launch. Requires the official installer
                        // from https://tailscale.com/download (Microsoft Store package
                        // is not supported).
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Auto-start Tailscale on launch")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Launches Tailscale in the background so remote hosts can be reached via Tailscale IP.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: tailscaleSwitch; anchors.margins: -3; target: tailscaleSwitch }
                            STSwitch {
                                id: tailscaleSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.tailscaleAutoStart
                                onCheckedChanged: {
                                    if (StreamingPreferences.tailscaleAutoStart !== checked) {
                                        StreamingPreferences.tailscaleAutoStart = checked
                                        // Persist immediately so the new value survives
                                        // the imminent restart (Yes) or any later quit.
                                        StreamingPreferences.save()
                                        if (checked) {
                                            tailscaleRestartDialog.open()
                                        } else {
                                            tailscaleStopNoticeDialog.open()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                              SESSION TAB
            //   Two sections: HOST (host-side behaviour) and INTERFACE
            //   (StreamLight client UI/UX preferences).
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: sessionTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 5
                spacing: 16

                // ── HOST section ──────────────────────────────────────────────
                Label {
                    text: qsTr("Host")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: hostCol.implicitHeight + 8

                    Column {
                        id: hostCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        // ── Optimize game settings for streaming ──────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Optimize game settings for streaming")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: gameOptSwitch; anchors.margins: -3; target: gameOptSwitch }
                            STSwitch {
                                id: gameOptSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.gameOptimizations
                                onCheckedChanged: { StreamingPreferences.gameOptimizations = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Quit app on host after closing the stream ─────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Quit app on host after closing the stream")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Closes the game when the stream ends. Unsaved progress will be lost.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: quitAppSwitch; anchors.margins: -3; target: quitAppSwitch }
                            STSwitch {
                                id: quitAppSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.quitAppAfter
                                onCheckedChanged: { StreamingPreferences.quitAppAfter = checked }
                            }
                        }
                    }
                }

                // ── INTERFACE section ─────────────────────────────────────────
                Label {
                    text: qsTr("Interface")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: ifaceCol.implicitHeight + 8

                    Column {
                        id: ifaceCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        ProfileLockNotice { active: settingsScreen._lockHue }

                        // ── GUI mode (dropdown) ───────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("GUI mode")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: uiModeSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Windowed"), qsTr("Maximized"), qsTr("Fullscreen")]
                                property var _values: [
                                    StreamingPreferences.UI_WINDOWED,
                                    StreamingPreferences.UI_MAXIMIZED,
                                    StreamingPreferences.UI_FULLSCREEN
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.uiDisplayMode
                                        for (var i = 0; i < uiModeSelector._values.length; i++) {
                                            if (uiModeSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.uiDisplayMode = _values[idx] }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Show connection quality warnings ──────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Show connection quality warnings")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: connWarnSwitch; anchors.margins: -3; target: connWarnSwitch }
                            STSwitch {
                                id: connWarnSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.connectionWarnings
                                onCheckedChanged: { StreamingPreferences.connectionWarnings = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Show configuration warnings ───────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Show configuration warnings")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: configWarnSwitch; anchors.margins: -3; target: configWarnSwitch }
                            STSwitch {
                                id: configWarnSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.configurationWarnings
                                onCheckedChanged: { StreamingPreferences.configurationWarnings = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Discord Rich Presence ─────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Discord Rich Presence")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Shows the streamed game in your Discord status")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: discordSwitch; anchors.margins: -3; target: discordSwitch }
                            STSwitch {
                                id: discordSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.richPresence
                                onCheckedChanged: { StreamingPreferences.richPresence = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Keep display awake while streaming ────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Keep display awake while streaming")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Prevents screensaver and display sleep during streaming")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: keepAwakeSwitch; anchors.margins: -3; target: keepAwakeSwitch }
                            STSwitch {
                                id: keepAwakeSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.keepAwake
                                onCheckedChanged: { StreamingPreferences.keepAwake = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Launch Philips Hue Sync during streaming ──────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: !settingsScreen._lockHue
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Launch Philips Hue Sync during streaming")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Auto-launches Hue Sync at stream start and closes it at end")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: hueSyncSwitch; anchors.margins: -3; target: hueSyncSwitch }
                            STSwitch {
                                id: hueSyncSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.hueSyncIntegration
                                onCheckedChanged: { StreamingPreferences.hueSyncIntegration = checked }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Hide host IP addresses ────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Hide host IP addresses")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Masks host IPs across the app for privacy (e.g. screenshots)")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: hideIpsSwitch; anchors.margins: -3; target: hideIpsSwitch }
                            STSwitch {
                                id: hideIpsSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.hideHostIps
                                onCheckedChanged: { StreamingPreferences.hideHostIps = checked }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                              OVERLAY TAB
            //   A 4-state profile selector (Off / Minimal / Default / Full) plus a
            //   live mockup that previews exactly which stats each profile shows
            //   in the in-stream overlay. Values in the mockup are illustrative.
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: overlayTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 6
                spacing: 16

                // Illustrative lines shown in the preview box, one entry per mode.
                // Mirrors FFmpegVideoDecoder::stringifyVideoStats output so the
                // preview matches what actually renders while streaming.
                function _previewLines(mode) {
                    if (mode === StreamingPreferences.OM_MINIMAL) {
                        return [
                            { t: "1920x1080 | 60 FPS | HEVC", dim: false },
                            { t: "Bitrate: 41 Mbps", dim: false },
                            { t: "RTT 11 ms | Net drops 0.10%", dim: false }
                        ]
                    }
                    if (mode === StreamingPreferences.OM_DEFAULT) {
                        return [
                            { t: "--- Client Metrics (StreamLight) ---", dim: true },
                            { t: "Video stream: 1920x1080 60.00 FPS (Codec: HEVC)", dim: false },
                            { t: "Bitrate: 41.2 Mbps", dim: false },
                            { t: "Frames dropped by your network connection: 0.10%", dim: false },
                            { t: "Frames dropped due to network jitter: 0.02%", dim: false },
                            { t: "Average network latency: 11 ms (variance: 2 ms)", dim: false },
                            { t: "Average decoding time: 3.21 ms", dim: false },
                            { t: "Frame pacing: Hardware (2:2 cadence)", dim: false },
                            { t: "--- Host Metrics (StreamTweak) ---", dim: true },
                            { t: "GPU: 47% | Enc: 12% | Temp: 62C", dim: false },
                            { t: "CPU: 18% | Net TX: 41 Mbps", dim: false }
                        ]
                    }
                    if (mode === StreamingPreferences.OM_FULL) {
                        return [
                            { t: "--- Client Metrics (StreamLight) ---", dim: true },
                            { t: "Video stream: 1920x1080 60.00 FPS (Codec: HEVC)", dim: false },
                            { t: "Bitrate: 41.2 Mbps, Peak (5s): 58.4", dim: false },
                            { t: "Incoming frame rate from network: 60.00 FPS", dim: false },
                            { t: "Decoding frame rate: 60.00 FPS", dim: false },
                            { t: "Rendering frame rate: 60.00 FPS", dim: false },
                            { t: "Host processing latency min/max/average: 1.2/4.8/2.1 ms", dim: false },
                            { t: "Frames dropped by your network connection: 0.10%", dim: false },
                            { t: "Frames dropped due to network jitter: 0.02%", dim: false },
                            { t: "Average network latency: 11 ms (variance: 2 ms)", dim: false },
                            { t: "Average decoding time: 3.21 ms", dim: false },
                            { t: "Average frame queue delay: 0.40 ms", dim: false },
                            { t: "Average rendering time (including monitor V-sync latency): 1.10 ms", dim: false },
                            { t: "Frame pacing: Hardware (2:2 cadence)", dim: false },
                            { t: "--- Host Metrics (StreamTweak) ---", dim: true },
                            { t: "GPU: 47% | Enc: 12% | Temp: 62C | VRAM: 4096 / 8192 MB", dim: false },
                            { t: "CPU: 18% | Net TX: 41 Mbps", dim: false }
                        ]
                    }
                    // OM_OFF
                    return [
                        { t: "Overlay is off — no stats are shown while streaming.", dim: true }
                    ]
                }

                // ── Section: OVERLAY ──────────────────────────────────────────
                Label {
                    text: qsTr("Overlay")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: overlayCol.implicitHeight + 8

                    Column {
                        id: overlayCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        // ── Profile selector ──────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: overlayModeSelector.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Performance overlay")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("Real-time stats while streaming. The hotkey cycles Off → Minimal → Default → Full: Ctrl+Alt+O (keyboard) or Select + L1 + R1 + X (gamepad).")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            SegmentedSelector {
                                id: overlayModeSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Off"), qsTr("Minimal"), qsTr("Default"), qsTr("Full")]
                                property var _values: [
                                    StreamingPreferences.OM_OFF,
                                    StreamingPreferences.OM_MINIMAL,
                                    StreamingPreferences.OM_DEFAULT,
                                    StreamingPreferences.OM_FULL
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.overlayMode
                                        for (var i = 0; i < overlayModeSelector._values.length; i++) {
                                            if (overlayModeSelector._values[i] === v) return i
                                        }
                                        return 0
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.overlayMode = _values[idx] }
                            }
                        }

                        // ── Live preview — same panel, directly below the selector ──
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        Item {
                            width: parent.width
                            height: previewWrap.implicitHeight + 24

                            Column {
                                id: previewWrap
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                anchors.top: parent.top
                                anchors.topMargin: 12
                                spacing: 12

                                Label {
                                    text: qsTr("How the overlay appears in the top-left corner while streaming. Values are illustrative.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                }

                                // The dark stats box, styled like the real in-stream overlay
                                // (RobotoMono there, JetBrains Mono here — the loaded app mono).
                                Rectangle {
                                    id: previewBox
                                    width: parent.width
                                    radius: 6
                                    color: "#202020"
                                    opacity: 0.96
                                    implicitHeight: previewTextCol.implicitHeight + 20

                                    Column {
                                        id: previewTextCol
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 10
                                        spacing: 2

                                        Repeater {
                                            model: overlayTab._previewLines(StreamingPreferences.overlayMode)
                                            delegate: Text {
                                                width: previewTextCol.width
                                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                text: modelData.t
                                                color: modelData.dim ? "#8a8a8a" : "#e8e8e8"
                                                font.family: "JetBrains Mono"
                                                font.pixelSize: 12
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                              ABOUT TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: aboutTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 7
                spacing: 16

                // StreamLight card — title + version + author on the left,
                // action buttons aligned to the right edge.
                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: 76

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 18
                        spacing: 12
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "StreamLight"
                            font.family: "DM Sans"
                            font.pixelSize: 22
                            font.bold: true
                            color: settingsScreen._text
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: settingsScreen.streamLightLatest.length > 0
                                  ? settingsScreen.streamLightLatest
                                  : qsTr("checking…")
                            font.family: "JetBrains Mono"
                            font.pixelSize: 14
                            color: settingsScreen._greenLk
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("by FoggyBytes")
                            font.family: "DM Sans"
                            font.pixelSize: 13
                            color: settingsScreen._textDim
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 16
                        spacing: 10
                        AboutLinkButton {
                            id: aboutSlGithubBtn
                            label: qsTr("GitHub releases")
                            url:   "https://github.com/FoggyBytes/StreamLight/releases"
                            KeyNavigation.right: aboutSlGplBtn
                            KeyNavigation.down:  aboutStGithubBtn
                        }
                        AboutLinkButton {
                            id: aboutSlGplBtn
                            label: qsTr("GPL v3")
                            url:   "https://www.gnu.org/licenses/gpl-3.0.html"
                            KeyNavigation.left:  aboutSlGithubBtn
                            KeyNavigation.right: aboutSlDonateBtn
                            KeyNavigation.down:  aboutStGithubBtn
                        }
                        AboutLinkButton {
                            id: aboutSlDonateBtn
                            label: qsTr("Donate")
                            url:   "https://paypal.me/foggypunk"
                            KeyNavigation.left: aboutSlGplBtn
                            KeyNavigation.down: aboutStGithubBtn
                        }
                    }
                }

                // StreamTweak card — same single-row layout.
                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: 76

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 18
                        spacing: 12
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "StreamTweak"
                            font.family: "DM Sans"
                            font.pixelSize: 22
                            font.bold: true
                            color: settingsScreen._text
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: settingsScreen.streamTweakLatest.length > 0
                                  ? settingsScreen.streamTweakLatest
                                  : qsTr("checking…")
                            font.family: "JetBrains Mono"
                            font.pixelSize: 14
                            color: settingsScreen._greenLk
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("by FoggyBytes")
                            font.family: "DM Sans"
                            font.pixelSize: 13
                            color: settingsScreen._textDim
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 16
                        AboutLinkButton {
                            id: aboutStGithubBtn
                            label: qsTr("GitHub releases")
                            url:   "https://github.com/FoggyBytes/StreamTweak/releases"
                            KeyNavigation.up: aboutSlGithubBtn
                        }
                    }
                }
            }
        }
    }

    // Shown after the user enables "Auto-start Tailscale". A restart is required
    // because Tailscale is launched once at StreamLight startup; toggling the
    // preference at runtime does not retroactively spawn it. Yes → restart now;
    // No → preference is still saved, takes effect at the next manual launch.
    NavigableMessageDialog {
        id: tailscaleRestartDialog
        headerText: qsTr("RESTART REQUIRED")
        text: qsTr("StreamLight needs to restart to start Tailscale in the background. Restart now?")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: SystemProperties.restartApplication()
    }

    // Shown after the user disables "Auto-start Tailscale". The currently
    // running Tailscale instance is intentionally NOT killed — if the user
    // started it manually or in a previous StreamLight session, killing it
    // would be surprising. The toggle only affects future StreamLight launches.
    NavigableMessageDialog {
        id: tailscaleStopNoticeDialog
        headerText: qsTr("TAILSCALE")
        text: qsTr("Tailscale will keep running until you close it manually or reboot. StreamLight will no longer start it automatically on future launches.")
        standardButtons: Dialog.Ok
    }

    // Reusable styled link button — opens `url` in the system browser / shell.
    component AboutLinkButton: Button {
        id: btn
        property string label: ""
        property string url: ""
        text: label
        activeFocusOnTab: true
        onClicked: Qt.openUrlExternally(url)
        Keys.onReturnPressed: Qt.openUrlExternally(url)
        Keys.onEnterPressed:  Qt.openUrlExternally(url)
        Keys.onSpacePressed:  Qt.openUrlExternally(url)

        // Suppress focus highlight in pointer mode, hover highlight in key mode,
        // so the two never light up two different buttons at the same time.
        readonly property bool _keyFocused: activeFocus && SdlGamepadKeyNavigation.inputMode !== "pointer"
        readonly property bool _hoverVisible: hovered && SdlGamepadKeyNavigation.inputMode !== "key"

        background: Rectangle {
            implicitWidth:  170
            implicitHeight: 36
            radius: 6
            color: btn._keyFocused   ? Qt.rgba(0, 0.9, 0.46, 0.12)
                 : btn._hoverVisible ? "#262626"
                 :                     "#1f1f1f"
            border.color: (btn._keyFocused || btn._hoverVisible)
                          ? settingsScreen._greenLk
                          : settingsScreen._border
            border.width: btn._keyFocused ? 2 : 1
        }
        contentItem: Label {
            text: btn.label
            color: settingsScreen._text
            font.family: "DM Sans"
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
