import QtQuick 2.9
import QtQuick.Controls 2.2

import SdlGamepadKeyNavigation 1.0

// AppShell — two-panel shell (sidebar + content area).
// Pushed as the initial StackView item by main.qml.
// Segue screens (StreamSegue, QuitSegue) are still pushed on top of the
// global stackView — they cover the full window, sidebar included.
//
// Root must be a FocusScope (not a plain Item) — only FocusScopes propagate
// activeFocus to their `focus: true` children. Without this the focus chain
// stops here and the inner GridView never receives D-pad key events.
FocusScope {
    id: appShell

    // Design tokens (mirrored from main.qml — id scopes are per-document).
    readonly property color _bg1:      "#151515"
    readonly property color _border:   "#2a2a2a"
    readonly property color _borderS:  "#404040"
    readonly property color _bgHov:    "#262626"
    readonly property color _bg2:      "#1a1a1a"
    readonly property color _text:     "#f0f0f0"
    readonly property color _textDim:  "#a0a0a0"
    readonly property color _green:    "#00E676"
    readonly property string _version: "3.4.1"
    readonly property string _mono:    "JetBrains Mono"

    // 0 = Home, 1 = Apps, 2 = Settings
    property int currentPage: 0

    // Passed from HomeScreen when navigating to Apps
    property int    _appsIdx:     0
    property var    _appsModel:   null
    property bool   _appsShowAll: false
    property string _appsHostName:    ""
    property string _appsHostAddress: ""
    property string _appsHostGpu:     ""
    property bool   _appsIsTailscaleClone: false

    // Where to return when leaving Settings (Home or Apps).
    property int    _settingsReturnPage: 0

    // ── Remote "Update host" job (state owned by HomeScreen; mirrored for the
    //    global status-bar chip + RB reopen shortcut) ───────────────────────────
    readonly property bool   _updateActive:  homeLoader.item ? homeLoader.item.updateJobActive  : false
    readonly property string _updateHost:    homeLoader.item ? homeLoader.item.updateJobHostName : ""
    readonly property string _updatePhase:   homeLoader.item ? homeLoader.item.updateJobPhase    : "IDLE"
    readonly property int    _updatePercent: homeLoader.item ? homeLoader.item.updateJobPercent  : -1

    function reopenUpdateDialog() {
        if (!homeLoader.item || !homeLoader.item.updateJobActive) return
        currentPage = 0
        homeLoader.item.openUpdateDialog()
    }
    function _updatePhaseLabel(phase) {
        switch (phase) {
        case "CHECKING":    return qsTr("Checking…")
        case "CHECK_READY": return qsTr("Updates found")
        case "DOWNLOADING": return qsTr("Downloading")
        case "INSTALLING":  return qsTr("Installing")
        case "REBOOTING":   return qsTr("Restarting host")
        case "DONE":        return qsTr("Done")
        case "NO_UPDATES":  return qsTr("Up to date")
        case "ERROR":       return qsTr("Failed")
        }
        return ""
    }

    focus: true

    // B / Escape: Settings → return page, Apps → Home, Home → propagate (quit).
    function _goBack() {
        if (currentPage === 2) {
            currentPage = _settingsReturnPage
            return true
        }
        if (currentPage === 1) {
            currentPage = 0
            return true
        }
        return false
    }
    Keys.onEscapePressed: { event.accepted = _goBack() }
    Keys.onBackPressed:   { event.accepted = _goBack() }

    // RB (PageDown) reopens the running "Update host" view. Only on Home (where
    // PageDown is otherwise unused) and only while a job is active, so it never
    // steals the Settings tab-navigation.
    Keys.onPressed: {
        if (event.key === Qt.Key_PageDown && appShell._updateActive && currentPage === 0) {
            appShell.reopenUpdateDialog()
            event.accepted = true
        }
    }

    function showHome() {
        currentPage = 0
    }

    function showApps(computerIndex, computerModel, showAll, hostName, hostAddress, hostGpu, isTailscaleClone) {
        _appsIdx              = computerIndex
        _appsModel            = computerModel
        _appsShowAll          = showAll || false
        _appsHostName         = hostName    || ""
        _appsHostAddress      = hostAddress || ""
        _appsHostGpu          = hostGpu     || ""
        _appsIsTailscaleClone = isTailscaleClone === true
        currentPage           = 1
    }

    // Also called by main.qml Keys.onMenuPressed.
    function openSettings() {
        if (currentPage !== 2) {
            _settingsReturnPage = currentPage
        }
        currentPage = 2
    }

    // Mouse click on a status-bar prompt → trigger the same action the pad would.
    function triggerStatusBarAction(kind) {
        switch (kind) {
        case "settings":
            openSettings()
            break
        case "back":
            // Same as B/Escape: Settings → return page, Apps → Home,
            // Home → quit dialog.
            if (!_goBack() && typeof stackView !== "undefined" && stackView.depth <= 1) {
                quitConfirmationDialog.open()
            }
            break
        case "openCard":
            if (homeLoader.item) SdlGamepadKeyNavigation.simulateKey(Qt.Key_Return)
            break
        case "shutdownHost":
            // Open the POWER chooser for the currently-focused host card.
            if (homeLoader.item && homeLoader.item.openPowerForCurrent)
                homeLoader.item.openPowerForCurrent()
            break
        case "cardMenu":
            SdlGamepadKeyNavigation.simulateKey(Qt.Key_Menu)
            break
        case "launchApp":
            SdlGamepadKeyNavigation.simulateKey(Qt.Key_Return)
            break
        case "stopApp":
            if (appsLoader.item && appsLoader.item.stopFocusedApp) {
                appsLoader.item.stopFocusedApp()
            }
            break
        case "change":
            SdlGamepadKeyNavigation.simulateKey(Qt.Key_Space)
            break
        case "prevTab":
            SdlGamepadKeyNavigation.simulateKey(Qt.Key_PageUp)
            break
        case "nextTab":
            SdlGamepadKeyNavigation.simulateKey(Qt.Key_PageDown)
            break
        case "defaultBitrate":
            if (settingsLoader.item && settingsLoader.item.resetBitrateToDefault) {
                settingsLoader.item.resetBitrateToDefault()
            }
            break
        }
    }

    onCurrentPageChanged: {
        if      (currentPage === 0 && homeLoader.item)     homeLoader.item.forceActiveFocus()
        else if (currentPage === 1 && appsLoader.item)     appsLoader.item.forceActiveFocus()
        else if (currentPage === 2 && settingsLoader.item) settingsLoader.item.forceActiveFocus()

        // Back on the host list: drop any "force Tailscale" session pin.
        if (currentPage === 0 && homeLoader.item && homeLoader.item.clearTailscalePreferences)
            homeLoader.item.clearTailscalePreferences()
    }

    // Ambient vertical gradient — charcoal top → deep green bottom.
    Rectangle {
        id: ambientBackground
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: statusBar.top
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#151515" }
            GradientStop { position: 0.7; color: "#13231B" }
            GradientStop { position: 1.0; color: "#0A3B22" }
        }
    }

    FocusScope {
        id: contentArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: statusBar.top
        focus: true

        // Home stays always-active so ComputerModel survives Apps/Settings.
        Loader {
            id: homeLoader
            anchors.fill: parent
            active: true
            visible: currentPage === 0
            focus: currentPage === 0
            source: "qrc:/gui/HomeScreen.qml"
            onLoaded: {
                item.appShell = appShell
                if (currentPage === 0)
                    item.forceActiveFocus()
            }
        }

        Loader {
            id: appsLoader
            anchors.fill: parent
            active: currentPage === 1
            visible: currentPage === 1
            focus: currentPage === 1
            source: "qrc:/gui/AppsScreen.qml"
            onLoaded: {
                item.computerIndex     = appShell._appsIdx
                item.hostComputerModel = appShell._appsModel
                item.showHiddenGames   = appShell._appsShowAll
                item.appShell          = appShell
                item.hostName          = appShell._appsHostName
                item.hostAddress       = appShell._appsHostAddress
                item.hostGpu           = appShell._appsHostGpu
                item.isTailscaleClone  = appShell._appsIsTailscaleClone
                item.forceActiveFocus()
            }
        }

        Loader {
            id: settingsLoader
            anchors.fill: parent
            active: currentPage === 2
            visible: currentPage === 2
            focus: currentPage === 2
            source: "qrc:/gui/SettingsScreen.qml"
            onLoaded: {
                item.forceActiveFocus()
            }
        }
    }

    // Status bar — gamepad prompts + version. Glyphs swap by controller type.
    Rectangle {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 44
        color: appShell._bg1

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: appShell._border
        }

        readonly property bool _padIsPs: SdlGamepadKeyNavigation.controllerType === "ps"

        readonly property string _iconA: _padIsPs ? "qrc:/res/pad_ps_cross.svg"    : "qrc:/res/pad_xbox_a.svg"
        readonly property string _iconB: _padIsPs ? "qrc:/res/pad_ps_circle.svg"   : "qrc:/res/pad_xbox_b.svg"
        readonly property string _iconX: _padIsPs ? "qrc:/res/pad_ps_square.svg"   : "qrc:/res/pad_xbox_x.svg"
        readonly property string _iconY: _padIsPs ? "qrc:/res/pad_ps_triangle.svg" : "qrc:/res/pad_xbox_y.svg"
        readonly property string _iconL: _padIsPs ? "qrc:/res/pad_ps_l1.svg"       : "qrc:/res/pad_xbox_lb.svg"
        readonly property string _iconR: _padIsPs ? "qrc:/res/pad_ps_r1.svg"       : "qrc:/res/pad_xbox_rb.svg"

        readonly property var _hintsHome: [
            { icon: _iconA, act: qsTr("Open"),     sh: false, kind: "openCard" },
            { icon: _iconX, act: qsTr("Shutdown"), sh: false, kind: "shutdownHost" },
            { icon: _iconY, act: qsTr("Settings"), sh: false, kind: "settings" },
            { icon: _iconB, act: qsTr("Exit"),     sh: false, kind: "back" }
        ]
        // Apps prompts swap to Resume/Stop when the focused app is the running one.
        readonly property bool _focusedAppRunning:
            currentPage === 1
            && appsLoader.item
            && appsLoader.item.focusedAppIsRunning === true
        property var _hintsApps: {
            if (statusBar._focusedAppRunning) {
                return [
                    { icon: _iconA, act: qsTr("Resume"),   sh: false, kind: "launchApp" },
                    { icon: _iconX, act: qsTr("Stop"),     sh: false, kind: "stopApp" },
                    { icon: _iconB, act: qsTr("Back"),     sh: false, kind: "back" },
                    { icon: _iconY, act: qsTr("Settings"), sh: false, kind: "settings" }
                ]
            }
            return [
                { icon: _iconA, act: qsTr("Launch"),   sh: false, kind: "launchApp" },
                { icon: _iconB, act: qsTr("Back"),     sh: false, kind: "back" },
                { icon: _iconY, act: qsTr("Settings"), sh: false, kind: "settings" }
            ]
        }
        // Settings prompts add "X · Default" when the bitrate differs from recommended.
        readonly property bool _showDefaultHint:
            currentPage === 2
            && settingsLoader.item
            && settingsLoader.item.bitrateNonDefault === true
        property var _hintsSettings: {
            var base = [
                { icon: _iconA, act: qsTr("Change"),   sh: false, kind: "change" },
                { icon: _iconL, act: qsTr("Prev tab"), sh: true,  kind: "prevTab" },
                { icon: _iconR, act: qsTr("Next tab"), sh: true,  kind: "nextTab" },
                { icon: _iconB, act: qsTr("Back"),     sh: false, kind: "back" }
            ]
            if (statusBar._showDefaultHint) {
                base.splice(1, 0, { icon: _iconX, act: qsTr("Default"), sh: false, kind: "defaultBitrate" })
            }
            return base
        }

        property var _hints:
              currentPage === 0 ? _hintsHome
            : currentPage === 1 ? _hintsApps
            : currentPage === 2 ? _hintsSettings
            : []

        Row {
            id: hintRow
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 20

            Repeater {
                model: statusBar._hints
                delegate: Item {
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth:  promptRow.implicitWidth + 8
                    implicitHeight: 30

                    Row {
                        id: promptRow
                        anchors.centerIn: parent
                        spacing: 9

                        // ABXY circle 26×26, LB/RB rounded rect 40×24.
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            source: modelData.icon
                            width:  modelData.sh ? 40 : 26
                            height: modelData.sh ? 24 : 26
                            sourceSize.width:  modelData.sh ? 80 : 52
                            sourceSize.height: modelData.sh ? 48 : 52
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.act
                            color: appShell._text
                            font.pixelSize: 15
                            font.family: "DM Sans"
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: appShell.triggerStatusBarAction(modelData.kind)
                    }
                }
            }
        }

        // Right cluster: optional "Update host" chip + version label, laid out by a
        // SINGLE Row so the chip can never overlap the version. (The old approach
        // anchored the chip's right edge to versionLabel.left; because the chip was a
        // Row whose implicitWidth resolves to 0 until the async RB icon finishes
        // loading, the anchor positioned the children with width 0 and they painted
        // from versionLabel.left *rightward*, colliding with "v3.3.0" → the garbled
        // bottom-right corner. A positioner skips invisible children, so the version
        // sits flush-right when no job is active and the chip slots to its left when
        // one is.)
        Row {
            id: rightCluster
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 22

            // Global "Update host" chip: visible whenever a remote update job is
            // running, on any page. Shows host + phase + a mini progress bar; RB (or a
            // click) reopens the full dialog. Lets the update run in the background.
            Item {
                id: updateChip
                visible: appShell._updateActive
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth:  chipContent.implicitWidth
                implicitHeight: chipContent.implicitHeight

                Row {
                    id: chipContent
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        source: statusBar._iconR
                        width: 40; height: 24
                        sourceSize.width: 80; sourceSize.height: 48
                        fillMode: Image.PreserveAspectFit; smooth: true
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            width: Math.min(implicitWidth, 260)
                            elide: Text.ElideRight
                            text: qsTr("Update")
                                  + (appShell._updateHost.length ? " · " + appShell._updateHost : "")
                                  + "   " + appShell._updatePhaseLabel(appShell._updatePhase)
                            color: appShell._text; font.pixelSize: 13; font.family: "DM Sans"
                        }
                        Row {
                            spacing: 8
                            visible: appShell._updatePercent >= 0
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 120; height: 5; radius: 3; color: "#2a2a2a"
                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(100, appShell._updatePercent)) / 100
                                    height: parent.height; radius: 3; color: appShell._green
                                }
                            }
                            Text {
                                text: appShell._updatePercent + "%"
                                color: appShell._textDim; font.pixelSize: 11; font.family: "DM Sans"
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: appShell.reopenUpdateDialog()
                }
            }

            Text {
                id: versionLabel
                anchors.verticalCenter: parent.verticalCenter
                text: "v" + appShell._version
                color: appShell._textDim
                font.family: appShell._mono
                font.pixelSize: 13
                font.letterSpacing: 1
            }
        }
    }
}
