import QtQuick 2.12
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.2

import AppModel 1.0
import ComputerManager 1.0
import StreamingPreferences 1.0
import SdlGamepadKeyNavigation 1.0

// Root is FocusScope (not Item) — required for activeFocus propagation from
// the Loader above us down into appGrid. Plain Items do not propagate.
FocusScope {
    id: appsRoot
    anchors.fill: parent
    focus: true

    // ── Properties pushed in by AppShell ─────────────────────────────────────
    property var    appShell: null
    property int    computerIndex
    property bool   showHiddenGames
    property var    hostComputerModel: null
    property string hostName:    ""
    property string hostAddress: ""
    property string hostGpu:     ""
    property bool   isTailscaleClone: false
    // NIC speed (e.g. "2.5 Gbps") fetched from StreamTweak via the bridge.
    property string hostNicSpeed: ""
    // Active per-host streaming profile (empty when no profile is active).
    property string hostProfileName: ""
    // Active profile's override map (empty when none). The header shows the
    // EFFECTIVE config = global StreamingPreferences with this override applied.
    property var hostOverride: ({})

    readonly property int  _effW:       (hostOverride && hostOverride.width   !== undefined) ? hostOverride.width   : StreamingPreferences.width
    readonly property int  _effH:       (hostOverride && hostOverride.height  !== undefined) ? hostOverride.height  : StreamingPreferences.height
    readonly property int  _effFps:     (hostOverride && hostOverride.fps     !== undefined) ? hostOverride.fps     : StreamingPreferences.fps
    readonly property int  _effBitrate: (hostOverride && hostOverride.bitrate !== undefined) ? hostOverride.bitrate : StreamingPreferences.bitrateKbps
    readonly property bool _effHdr:     (hostOverride && hostOverride.hdr     !== undefined) ? hostOverride.hdr     : StreamingPreferences.enableHdr
    readonly property int  _effCodec:   (hostOverride && hostOverride.codec   !== undefined) ? hostOverride.codec   : StreamingPreferences.videoCodecConfig
    readonly property int  _effAudio:   (hostOverride && hostOverride.audio   !== undefined) ? hostOverride.audio   : StreamingPreferences.audioConfig

    function _codecLabel(c) { return c === 1 ? "H.264" : c === 2 ? "HEVC" : c === 4 ? "AV1" : qsTr("Auto codec") }
    function _audioLabel(a) { return a === 1 ? "5.1" : a === 2 ? "7.1" : qsTr("Stereo") }

    // ── Local design tokens ──────────────────────────────────────────────────
    readonly property color _text:    "#f0f0f0"
    readonly property color _textDim: "#a0a0a0"
    readonly property color _textMut: "#707070"
    readonly property color _greenLk: "#00E676"
    readonly property color _amber:   "#f59e0b"
    readonly property string _mono:   "JetBrains Mono"

    // Same palette as HomeScreen so the colour of the card we came from
    // matches the colour of the header we see here.
    function hostColorPair(name) {
        if (!name || name.length === 0) return ["#1c1c1c", "#0d0d0d"]
        var h = 0
        for (var i = 0; i < name.length; i++) {
            h = ((h << 5) - h) + name.charCodeAt(i)
            h |= 0
        }
        var palette = [
            ["#1a3a5c", "#0a1a2e"],
            ["#3a1a4c", "#1a0a26"],
            ["#4a1e1a", "#260a08"],
            ["#1a4c2e", "#0a2618"],
            ["#4c3a0e", "#261c06"],
            ["#0e3a4c", "#061c26"]
        ]
        return palette[Math.abs(h) % palette.length]
    }

    // Y → Settings; X (Menu) → stop running app if focused; RB → Customize.
    Keys.onHangupPressed: { if (appShell) appShell.openSettings() }
    Keys.onMenuPressed: {
        if (focusedAppIsRunning) {
            stopFocusedApp()
        }
        event.accepted = true
    }

    // Bound to delegate._running (a property — reactive) so the status-bar
    // prompts update when the host-side session ends.
    property bool focusedAppIsRunning:
        (appGrid && appGrid.currentItem) ? appGrid.currentItem._running === true : false

    function resumeFocusedApp() {
        if (appGrid && appGrid.currentItem && appGrid.currentItem.launchOrResumeSelectedApp) {
            appGrid.currentItem.launchOrResumeSelectedApp(true)
        }
    }
    function stopFocusedApp() {
        if (appGrid && appGrid.currentItem && appGrid.currentItem.doQuitGame) {
            appGrid.currentItem.doQuitGame()
        }
    }
    function openCustomize(idx, name) {
        if (idx === undefined || idx < 0) return
        appSettingsDialog.appModel = appGrid.appModel
        appSettingsDialog.appIndex = idx
        appSettingsDialog.appName = name ? name : ""
        // So the per-game "inherit" option shows the active profile's name.
        appSettingsDialog.activeProfileName = appsRoot.hostProfileName
        appSettingsDialog.open()
    }
    function openCustomizeForFocused() {
        if (appGrid && appGrid.currentItem) {
            openCustomize(appGrid.currentIndex, appGrid.currentItem._appName)
        }
    }

    function _formatNicSpeed(raw) {
        if (raw === "" || raw === null) return qsTr("N/A")
        var mbps = parseInt(raw)
        if (isNaN(mbps)) return raw
        if (mbps >= 1000) {
            var gbps = mbps / 1000
            return (gbps === Math.floor(gbps) ? gbps.toFixed(0) : gbps.toFixed(1)) + " Gbps"
        }
        return mbps + " Mbps"
    }

    // Loader sets host properties AFTER Component.onCompleted → init here.
    onHostComputerModelChanged: _initFromHost()
    onComputerIndexChanged:    _initFromHost()

    function _initFromHost() {
        if (!hostComputerModel || computerIndex < 0) return
        hostComputerModel.requestStreamTweakStatus(computerIndex)
        hostProfileName = (hostComputerModel.hostActiveProfile(computerIndex) >= 0)
                          ? hostComputerModel.hostActiveProfileName(computerIndex) : ""
        hostOverride = hostComputerModel.hostActiveOverride(computerIndex)
        if (appGrid) {
            appGrid.storeMap = hostComputerModel.getCachedAppStores(computerIndex)
            hostComputerModel.requestAppStores(computerIndex)
        }
    }

    Connections {
        target: hostComputerModel
        function onStreamTweakStatusReceived(idx, status) {
            if (idx === appsRoot.computerIndex) {
                appsRoot.hostNicSpeed = appsRoot._formatNicSpeed(status)
            }
        }
        function onAppStoresReceived(idx, stores) {
            if (idx === appsRoot.computerIndex && appGrid) {
                appGrid.storeMap = stores
            }
        }
    }

    // Compact one-row header: hostname · ONLINE · IP · NIC · resolution · FPS.
    Rectangle {
        id: appsHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        height: 56
        radius: 10
        clip: true

        property var _colors: appsRoot.hostColorPair(appsRoot.hostName)

        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: appsHeader._colors[0] }
            GradientStop { position: 1.0; color: appsHeader._colors[1] }
        }

        Canvas {
            anchors.fill: parent
            opacity: 0.06
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = "#ffffff"
                ctx.lineWidth = 1
                var step = 14
                for (var x = -height; x < width; x += step) {
                    ctx.beginPath()
                    ctx.moveTo(x, 0)
                    ctx.lineTo(x + height, height)
                    ctx.stroke()
                }
            }
        }

        Row {
            id: headerRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 12

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: appsRoot.hostName
                color: appsRoot._text
                font.family: "DM Sans"
                font.pixelSize: 20
                font.bold: true
                elide: Label.ElideRight
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: onlineRow.implicitWidth + 14
                height: 22
                radius: 3
                color: Qt.rgba(0, 0, 0, 0.35)
                border.color: appsRoot._greenLk
                border.width: 1

                Row {
                    id: onlineRow
                    anchors.centerIn: parent
                    spacing: 6
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("ONLINE")
                        color: appsRoot._greenLk
                        font.family: "DM Sans"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.2
                    }
                }
            }

            // Active profile badge — shown only when a per-host profile is active.
            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: appsRoot.hostProfileName.length > 0
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: appsRoot.hostProfileName.length > 0
                width: profileBadgeLabel.implicitWidth + 14
                height: 22
                radius: 3
                color: Qt.rgba(0, 0, 0, 0.35)
                border.color: "#ffffff"
                border.width: 1

                Label {
                    id: profileBadgeLabel
                    anchors.centerIn: parent
                    text: appsRoot.hostProfileName.toUpperCase()
                    color: "#ffffff"
                    font.family: "DM Sans"
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1.2
                }
            }

            // TAILSCALE badge — mirrors the one on the HomeScreen tile.
            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: appsRoot.isTailscaleClone
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: appsRoot.isTailscaleClone
                width: tailscaleBadgeLabel.implicitWidth + 14
                height: 22
                radius: 3
                color: Qt.rgba(0, 0, 0, 0.35)
                border.color: "#ffffff"
                border.width: 1

                Label {
                    id: tailscaleBadgeLabel
                    anchors.centerIn: parent
                    text: qsTr("TAILSCALE")
                    color: "#ffffff"
                    font.family: "DM Sans"
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1.2
                }
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: !StreamingPreferences.hideHostIps
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: !StreamingPreferences.hideHostIps
                text: (appsRoot.hostAddress && appsRoot.hostAddress.length > 0)
                      ? appsRoot.hostAddress : qsTr("N/A")
                color: appsRoot._textDim
                font.family: appsRoot._mono
                font.pixelSize: 14
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: appsRoot.hostNicSpeed.length > 0
                      && appsRoot.hostNicSpeed !== qsTr("N/A")
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: appsRoot.hostNicSpeed.length > 0
                      && appsRoot.hostNicSpeed !== qsTr("N/A")
                text: appsRoot.hostNicSpeed
                color: appsRoot._greenLk
                font.family: appsRoot._mono
                font.pixelSize: 14
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    var w = appsRoot._effW, h = appsRoot._effH
                    if (w === 3840 && h === 2160) return "4K"
                    if (w === 2560 && h === 1440) return "1440p"
                    if (w === 1920 && h === 1080) return "1080p"
                    if (w === 1280 && h === 720)  return "720p"
                    return w + "×" + h
                }
                color: appsRoot._textDim
                font.family: appsRoot._mono
                font.pixelSize: 14
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: appsRoot._effFps + " FPS"
                color: appsRoot._textDim
                font.family: appsRoot._mono
                font.pixelSize: 14
            }

            // ── Bitrate ───────────────────────────────────────────────────
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: (appsRoot._effBitrate / 1000).toFixed(0) + " Mbps"
                color: appsRoot._textDim
                font.family: appsRoot._mono
                font.pixelSize: 14
            }

            // ── HDR (only when effectively ON) ────────────────────────────
            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: appsRoot._effHdr
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: appsRoot._effHdr
                text: "HDR"
                color: appsRoot._greenLk
                font.family: appsRoot._mono
                font.pixelSize: 14
                font.bold: true
            }

            // ── Video codec ───────────────────────────────────────────────
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: appsRoot._codecLabel(appsRoot._effCodec)
                color: appsRoot._textDim
                font.family: appsRoot._mono
                font.pixelSize: 14
            }

            // ── Audio ─────────────────────────────────────────────────────
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                color: appsRoot._textMut
                font.pixelSize: 16
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: appsRoot._audioLabel(appsRoot._effAudio)
                color: appsRoot._textDim
                font.family: appsRoot._mono
                font.pixelSize: 14
            }
        }
    }

    // ── Section caption ───────────────────────────────────────────────────────
    Label {
        id: appsSectionLabel
        anchors.top: appsHeader.bottom
        anchors.left: parent.left
        anchors.leftMargin: 30
        anchors.topMargin: 24
        text: qsTr("Apps & games")
        color: appsRoot._text
        font.family: "DM Sans"
        font.pixelSize: 26
        font.bold: true
    }

    // ══════════════════════════════════════════════════════════════════════════
    //                                APP GRID
    // ══════════════════════════════════════════════════════════════════════════
    GridView {
        id: appGrid
        anchors.top: appsSectionLabel.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: 12
        anchors.bottomMargin: 12

        // Prevent app covers from drawing over the header while scrolling.
        clip: true

        // Left-aligned grid: covers always start from top-left, matching the
        // header's left/right padding. (Was CenteredGridView before — covers
        // looked isolated when only Desktop + Steam Big Picture were present.)
        leftMargin: 30
        rightMargin: 30
        boundsBehavior: Flickable.OvershootBounds

        property AppModel appModel: createModel()
        property bool activated
        property bool showGames
        property var storeMap: ({})

        focus: true
        activeFocusOnTab: true
        topMargin: 4
        bottomMargin: 4
        // Cover 200×267 (StreamLight 2.3.1 / Moonlight ratio 3:4).
        // 230 px columns: same compact layout as 2.3.1, fits 5+ at 1280p.
        // Row height 350 px reserves ~60 px below each cover so the inline
        // name tooltip (see appDelegate) can render in its own band without
        // ever overlapping the row beneath.
        cellWidth: 230; cellHeight: 350

        Component.onCompleted: {
            currentIndex = 0
            appModel.computerLost.connect(computerLost)
            activated = true

            if (!showGames && !appsRoot.showHiddenGames) {
                var directLaunchAppIndex = model.getDirectLaunchAppIndex()
                if (directLaunchAppIndex >= 0) {
                    currentIndex = directLaunchAppIndex
                    currentItem.launchOrResumeSelectedApp(false)
                    showGames = true
                }
            }
        }

        Component.onDestruction: {
            appModel.computerLost.disconnect(computerLost)
            activated = false
        }

        function computerLost() {
            if (appsRoot.appShell) appsRoot.appShell.showHome()
        }

        Keys.onReturnPressed: { if (currentItem) currentItem.launchOrResumeSelectedApp(true); event.accepted = true }
        Keys.onEnterPressed:  { if (currentItem) currentItem.launchOrResumeSelectedApp(true); event.accepted = true }
        Keys.onSpacePressed:  { if (currentItem) currentItem.launchOrResumeSelectedApp(true); event.accepted = true }

        // Select (View/Create/−) → Customize the focused app. The Select button
        // is mapped to Key_F13 (Key_Select would be treated as an activation key
        // and launch the app instead).
        Keys.onPressed: {
            if (event.key === Qt.Key_F13) {
                appsRoot.openCustomizeForFocused()
                event.accepted = true
            }
        }

        function storeIconSource(store) {
            if (store === "Steam")           return "qrc:/res/store_steam.svg"
            if (store === "Epic Games")      return "qrc:/res/store_epic.svg"
            if (store === "GOG")             return "qrc:/res/store_gog.svg"
            if (store === "Ubisoft Connect") return "qrc:/res/store_ubisoft.svg"
            if (store === "Xbox")            return "qrc:/res/store_xbox.svg"
            if (store === "Battle.net")      return "qrc:/res/store_battlenet.svg"
            if (store === "EA App")          return "qrc:/res/store_ea.svg"
            return ""
        }

        // (handleAppStoresReceived moved to appsRoot's Connections block)

        function createModel() {
            var model = Qt.createQmlObject('import AppModel 1.0; AppModel {}', parent, '')
            model.initialize(ComputerManager, appsRoot.computerIndex, appsRoot.showHiddenGames)
            return model
        }

        model: appModel

    delegate: NavigableItemDelegate {
        id: appDelegate
        // 4 px gutter inside the cell; bottom band reserved for the name tooltip.
        width: 222; height: 342
        grid: appGrid

        property alias appNameText: appNameTextLoader.item

        // Exposed to appsRoot for the Resume/Stop status-bar swap.
        property int    _appId:   model.appid
        property string _appName: model.name
        property bool   _running: model.running

        opacity: model.hidden ? 0.4 : 1.0

        // Disable Material's default focus highlight; coverFocusBorder draws ours.
        background: Item { anchors.fill: parent }

        Image {
            property bool isPlaceholder: false

            id: appIcon
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
            source: model.boxart

            onSourceSizeChanged: {
                if (!model.isAppCollectorGame &&
                    ((sourceSize.width === 130 && sourceSize.height === 180) ||
                     (sourceSize.width === 628 && sourceSize.height === 888) ||
                     (sourceSize.width === 200 && sourceSize.height === 266)))
                {
                    isPlaceholder = true
                } else {
                    isPlaceholder = false
                }

                // 200×267 — Moonlight historic 3:4 cover ratio.
                width  = 200
                height = 267
            }
        }

        // Focus border hugging the cover. Thicker (6 px) + always pulsing for
        // the running streaming session (so the pulse is visible even when
        // the D-pad cursor is on the running app); thin (3 px) solid for
        // regular D-pad / mouse focus on any other cover.
        Rectangle {
            id: coverFocusBorder
            anchors.fill: appIcon
            anchors.margins: -3
            color: "transparent"
            radius: 8
            z: 2

            readonly property bool _focused: appDelegate.inputFocused
            readonly property bool _running: appDelegate._running

            visible: _focused || _running
            border.color: appsRoot._greenLk
            border.width: (_focused || _running) ? 3 : 0

            SequentialAnimation on opacity {
                running: coverFocusBorder._running
                loops: Animation.Infinite
                alwaysRunToEnd: true
                NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0;  duration: 900; easing.type: Easing.InOutSine }
            }
            Binding on opacity { when: !coverFocusBorder._running; value: 1.0 }
        }

        // Per-game settings ("tune") affordance, top-right of the cover. Green
        // when this game has overrides, subtle otherwise. Tap to customize.
        Rectangle {
            id: tuneBadge
            anchors.right: appIcon.right
            anchors.top: appIcon.top
            anchors.margins: 6
            width: 36; height: 36; radius: 18
            z: 4
            color: model.overridden ? "#E600C853" : "#A0000000"
            border.color: model.overridden ? "#00E676" : "#60FFFFFF"
            border.width: 1
            Image {
                anchors.centerIn: parent
                source: "qrc:/res/tune.svg"
                sourceSize.width: 20; sourceSize.height: 20
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: appsRoot.openCustomize(index, model.name)
            }
        }

        // Instant name pill below the focus border (no hover delay).
        Rectangle {
            id: nameTooltip
            anchors.top: coverFocusBorder.bottom
            anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(appIcon.width, nameTooltipLabel.implicitWidth + 16)
            height: nameTooltipLabel.implicitHeight + 8
            color: "#202020"
            border.color: "#3a3a3a"
            border.width: 1
            radius: 4
            visible: appDelegate.inputFocused
            z: 5

            Label {
                id: nameTooltipLabel
                anchors.centerIn: parent
                width: parent.width - 16
                text: model.name
                font.family: "DM Sans"
                font.pixelSize: 12
                color: "#f0f0f0"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        // "STREAMING" badge on the running app's cover — centered horizontally,
        // placed in the upper portion of the cover (about 18% from the top) so
        // it sits above the box-art's central artwork without crowding the top edge.
        Rectangle {
            id: streamingBadge
            visible: appDelegate._running
            anchors.horizontalCenter: appIcon.horizontalCenter
            anchors.top: appIcon.top
            anchors.topMargin: Math.round(appIcon.height * 0.18)
            color: "#CC0A1A12"
            border.color: appsRoot._greenLk
            border.width: 1
            radius: 4
            width:  streamingBadgeLabel.implicitWidth + 18
            height: streamingBadgeLabel.implicitHeight + 8
            z: 3

            Label {
                id: streamingBadgeLabel
                anchors.centerIn: parent
                text: qsTr("STREAMING")
                color: appsRoot._greenLk
                font.family: "DM Sans"
                font.pixelSize: 14
                font.bold: true
                font.letterSpacing: 1.5
            }
        }

        Rectangle {
            id: storeBadge

            property string store: appGrid.storeMap[model.name] || ""

            visible: store !== ""
            anchors.right: appIcon.right
            anchors.bottom: appIcon.bottom
            anchors.rightMargin: 5
            anchors.bottomMargin: 5
            color: "#CC000000"
            radius: 3
            width:  storeBadgeRow.implicitWidth + 10
            height: storeBadgeRow.implicitHeight + 6

            Row {
                id: storeBadgeRow
                anchors.centerIn: parent
                spacing: 5

                Image {
                    source: appGrid.storeIconSource(storeBadge.store)
                    width: 16; height: 16
                    sourceSize.width: 48
                    sourceSize.height: 48
                    anchors.verticalCenter: parent.verticalCenter
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }

                Label {
                    text: storeBadge.store
                    font.pointSize: 8
                    color: "white"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // (Play/Stop overlay removed in 3.0 — actions live in the status bar.)

        Loader {
            id: appNameTextLoader
            active: appIcon.isPlaceholder

            width: appIcon.width
            // Previously shortened to 175 px to leave room for the Play/Stop
            // overlay — that overlay is gone now, so use the full cover.
            height: appIcon.height

            anchors.left:   appIcon.left
            anchors.right:  appIcon.right
            anchors.bottom: appIcon.bottom

            sourceComponent: Label {
                id: appNameText
                text: model.name
                font.pointSize: 22
                leftPadding: 20
                rightPadding: 20
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                elide: Text.ElideRight
            }
        }

        function launchOrResumeSelectedApp(quitExistingApp) {
            // Drop stale events that arrive after a stream session pops.
            if (Window.window && Window.window._streamJustEnded === true) {
                return
            }
            // Drop a second push when one is already in flight: a double-tap
            // on A (very easy with a Bluetooth pad) would otherwise queue a
            // second Session that auto-fires when the first one ends.
            if (Window.window && Window.window._streamLaunching === true) {
                return
            }

            // Must use appGrid.appModel — bare appModel is not in scope.
            var m = appGrid.appModel
            var runningId = m.getRunningAppId()
            if (runningId !== 0 && runningId !== model.appid) {
                if (quitExistingApp) {
                    quitAppDialog.appName = m.getRunningAppName()
                    quitAppDialog.segueToStream = true
                    quitAppDialog.nextAppName = model.name
                    quitAppDialog.nextAppIndex = index
                    quitAppDialog.open()
                }
                return
            }

            var component = Qt.createComponent("StreamSegue.qml")
            if (component.status !== Component.Ready) {
                console.warn("StreamSegue.qml not ready:", component.errorString())
                return
            }
            var segue = component.createObject(stackView, {
                "appName":   model.name,
                "session":   m.createSessionForApp(index),
                "isResume":  runningId === model.appid
            })
            if (Window.window) Window.window.markStreamLaunching()
            stackView.push(segue)
        }

        onClicked:            launchOrResumeSelectedApp(true)
        Keys.onReturnPressed: launchOrResumeSelectedApp(true)
        Keys.onEnterPressed:  launchOrResumeSelectedApp(true)

        function doQuitGame() {
            quitAppDialog.appName = appGrid.appModel.getRunningAppName()
            quitAppDialog.segueToStream = false
            quitAppDialog.open()
        }

    }

    Row {
        anchors.centerIn: parent
        spacing: 5
        visible: appGrid.count === 0

        Label {
            text: qsTr("This computer doesn't seem to have any applications or some applications are hidden")
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    NavigableMessageDialog {
        id: quitAppDialog
        property string appName: ""
        property bool segueToStream: false
        property string nextAppName: ""
        property int nextAppIndex: 0
        text: qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml")
            var params = {
                "appName": appName,
                "quitRunningAppFn": function() { appGrid.appModel.quitRunningApp() }
            }
            if (segueToStream) {
                params.nextAppName = nextAppName
                params.nextSession = appGrid.appModel.createSessionForApp(nextAppIndex)
            } else {
                params.nextAppName = null
                params.nextSession = null
            }
            stackView.push(component.createObject(stackView, params))
        }

        onAccepted: quitApp()
    }

    ScrollBar.vertical: ScrollBar {}
    }   // end GridView (appGrid)

    AppSettingsDialog {
        id: appSettingsDialog
        onClosedByUser: appGrid.forceActiveFocus()
    }
}       // end Item (appsRoot)
