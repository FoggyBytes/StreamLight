import QtQuick 2.12
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2

import ComputerModel 1.0
import ComputerManager 1.0
import StreamingPreferences 1.0
import SdlGamepadKeyNavigation 1.0

// Root is FocusScope (not Item) — required for activeFocus propagation from
// the Loader above us down into pcList. Plain Items do not propagate.
FocusScope {
    id: homeScreen
    anchors.fill: parent
    focus: true

    property var appShell: null
    property ComputerModel computerModel: createModel()

    // Local design tokens (mirrored from main.qml — IDs from parent components
    // are not reliably visible inside dynamically loaded Loader children).
    readonly property color _bg1:       "#151515"
    readonly property color _bg2:       "#1a1a1a"
    readonly property color _bgHov:     "#262626"
    readonly property color _border:    "#2a2a2a"
    readonly property color _borderS:   "#404040"
    readonly property color _text:      "#f0f0f0"
    readonly property color _textDim:   "#a0a0a0"
    readonly property color _textMut:   "#707070"
    readonly property color _green:     "#00E676"
    readonly property color _greenLk:   "#00E676"
    readonly property color _amber:     "#f59e0b"
    readonly property color _red:       "#ef4444"
    readonly property string _mono:     "JetBrains Mono"

    // Tracks the last-used input device so highlight (gamepad/keyboard focus)
    // and hover (mouse) never light up two different cards at the same time.
    readonly property bool _pointerMode: SdlGamepadKeyNavigation.inputMode === "pointer"
    readonly property bool _keyMode:     SdlGamepadKeyNavigation.inputMode === "key"

    // ── StreamTweak access PIN popup ──────────────────────────────────────────
    // Shown while a host's approval is pending; displays the 4-digit PIN the user
    // must confirm matches the prompt on the host. Auto-closes once approved.
    property string stPinValue: ""
    property string stPinHostName: ""
    property string stPinHostAddr: ""
    property int    stPinHost: -1
    property var    stPinDismissed: ({})   // idx -> true while the user has dismissed it
    function stShowPin(idx, name, addr, pin) {
        if (stPinDismissed[idx]) return     // don't re-nag after a manual dismiss
        stPinValue = pin
        stPinHostName = name
        stPinHostAddr = addr
        stPinHost = idx
        stPinDialog.open()
    }
    function stHidePin(idx) {
        if (stPinHost === idx) {
            stPinHost = -1
            stPinDialog.close()
        }
        delete stPinDismissed[idx]          // state changed → allow showing again later
    }
    function stForcePin(idx) {              // user explicitly re-requested access
        delete stPinDismissed[idx]
    }

    Component.onCompleted: {
        // Always start with a valid selection so the focus highlight is
        // visible immediately, gamepad or not. Best-practice for D-pad nav:
        // never leave currentIndex at -1.
        pcList.currentIndex = 0
        ComputerManager.computerAddCompleted.connect(addComplete)
    }

    Component.onDestruction: {
        ComputerManager.computerAddCompleted.disconnect(addComplete)
    }

    onVisibleChanged: {
        if (visible && pcList.currentIndex === -1 && pcList.count > 0) {
            pcList.currentIndex = 0
        }
    }

    function pairingComplete(error) {
        pairDialog.close()
        if (error !== undefined) {
            errorDialog.text = error
            errorDialog.helpText = ""
            errorDialog.open()
        }
    }

    function addComplete(success, detectedPortBlocking) {
        if (!success) {
            errorDialog.text = qsTr("Unable to connect to the specified PC.")
            if (detectedPortBlocking) {
                errorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking StreamLight. Streaming over the Internet may not work while connected to this network.")
            } else {
                errorDialog.helpText = qsTr("Click the Help button for possible solutions.")
            }
            errorDialog.open()
        }
    }

    function createModel() {
        var model = Qt.createQmlObject('import ComputerModel 1.0; ComputerModel {}', parent, '')
        model.initialize(ComputerManager)
        model.pairingCompleted.connect(pairingComplete)
        model.connectionTestCompleted.connect(testConnectionDialog.connectionTestComplete)
        model.tailscaleSuggestion.connect(function(parentUuid, parentName, tailscaleIp) {
            tailscaleSuggestDialog.parentUuid = parentUuid
            tailscaleSuggestDialog.parentName = parentName
            tailscaleSuggestDialog.tailscaleIp = tailscaleIp
            tailscaleSuggestDialog.computerModel = model
            tailscaleSuggestDialog.open()
        })
        return model
    }

    // Per-host color palette — deterministic by host name hash.
    // 6 dark saturated tones designed to coexist with the Xbox-style UI.
    function hostColorPair(name) {
        if (!name || name.length === 0) return ["#1c1c1c", "#0d0d0d"]
        var h = 0
        for (var i = 0; i < name.length; i++) {
            h = ((h << 5) - h) + name.charCodeAt(i)
            h |= 0
        }
        var palette = [
            ["#1a3a5c", "#0a1a2e"],   // navy blue
            ["#3a1a4c", "#1a0a26"],   // purple
            ["#4a1e1a", "#260a08"],   // rust red
            ["#1a4c2e", "#0a2618"],   // forest green
            ["#4c3a0e", "#261c06"],   // amber
            ["#0e3a4c", "#061c26"]    // teal
        ]
        return palette[Math.abs(h) % palette.length]
    }

    // (Top "Add computer" + "Refresh" buttons were removed — those actions
    //  are now reachable as inline tiles to the right of the last host card.
    //  See addHostTile + refreshTile below pcList.)

    // ── Brand row (StreamLight identity, above "My Hosts") ────────────────────
    Item {
        id: brandRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 18
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        height: 64

        Image {
            id: brandIcon
            source: "qrc:/streamlight.ico"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 56; height: 56
            // Rasterise at device-pixel resolution so HiDPI displays get a sharp image
            // regardless of Windows scaling (100% / 150% / 200%).
            sourceSize.width:  56 * Screen.devicePixelRatio
            sourceSize.height: 56 * Screen.devicePixelRatio
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }

        Column {
            anchors.left: brandIcon.right
            anchors.leftMargin: 14
            anchors.verticalCenter: brandIcon.verticalCenter
            spacing: 2

            // Uniform wordmark: "STREAMLIGHT" all caps, single size, Black
            // weight, wide letter-spacing — Nike/Sony-style logotype. Avoids
            // the optical-weight mismatch of synthesised small-caps.
            Label {
                id: brandName
                text: "STREAMLIGHT"
                font.family: "DM Sans"
                font.pixelSize: 22
                font.weight: Font.Black
                font.bold: true
                font.letterSpacing: 3
                color: homeScreen._text
            }

            Label {
                id: brandTag
                text: qsTr("a Moonlight fork")
                font.family: "DM Sans"
                font.pixelSize: 12
                font.bold: false
                font.letterSpacing: 0.5
                color: homeScreen._textDim
            }
        }
    }

    // ── Header (title + description) ──────────────────────────────────────────
    Item {
        id: header
        anchors.top: brandRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 18
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        height: 60

        Label {
            id: headerTitle
            text: qsTr("My Hosts")
            font.family: "DM Sans"
            font.pixelSize: 32
            font.bold: true
            color: homeScreen._text
            anchors.top: parent.top
            anchors.left: parent.left
        }

        Label {
            text: qsTr("Paired and discovered computers on your local network")
            font.family: "DM Sans"
            font.pixelSize: 13
            color: homeScreen._textDim
            anchors.top: headerTitle.bottom
            anchors.left: parent.left
            anchors.topMargin: 4
        }
    }

    // ── Hosts grid ────────────────────────────────────────────────────────────
    GridView {
        id: pcList
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20
        anchors.leftMargin: 30
        anchors.rightMargin: 30

        // Grid height is exactly what we need for the rows we have. The
        // "Actions" section sits underneath, anchored to pcList.bottom.
        height: count === 0 ? 220 : Math.ceil(count / pcList._columns) * cellHeight

        // Portrait tile: taller than wide, like an app cover.
        // Inner pcCard is 320 high, Options button 30 + spacing 4 + margins 8 ≈ 380 total.
        property int _columns: Math.max(1, Math.floor(width / 270))
        cellWidth:  width / _columns
        cellHeight: 380

        focus: true
        activeFocusOnTab: true
        interactive: false
        // Qt 6 disables keyboard navigation on Flickables when interactive
        // is false; we keep arrow-key navigation by re-enabling it here.
        keyNavigationEnabled: true
        keyNavigationWraps: false
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // Sub-focus inside a host cell: false = card body (Open),
        // true = Options button under the card. D-pad Down/Up flips it.
        property bool subFocusOptions: false

        // ── Gamepad / keyboard handling ────────────────────────────────────────
        // A (Space/Return) → activate selected card
        // X (Menu)         → open context menu of the selected card
        // Y (Hangup)       → shortcut to Settings (consistent with AppsScreen)
        // A acts on the sub-focus: Options button → open menu, card → open host.
        Keys.onReturnPressed: { _activate(); event.accepted = true }
        Keys.onEnterPressed:  { _activate(); event.accepted = true }
        Keys.onSpacePressed:  { _activate(); event.accepted = true }
        Keys.onHangupPressed: { if (appShell) appShell.openSettings();        event.accepted = true }

        function _activate() {
            if (!currentItem) return
            if (pcList.subFocusOptions) currentItem.openCardMenu()
            else                        currentItem.activateCard()
        }

        // Cross-area navigation + card/Options sub-focus.
        // (The left-edge wrap that used to refocus the sidebar was removed
        //  with the sidebar itself in 3.0; we now let GridView handle the
        //  arrow internally, which simply does nothing on the first column.)
        Keys.onUpPressed: {
            // From Options → back to its card.
            if (pcList.subFocusOptions) {
                pcList.subFocusOptions = false
                event.accepted = true
                return
            }
            // First row of cards has nothing above (top action buttons removed).
            // Let the GridView handle internal navigation; on the very top
            // row, Qt simply ignores the keystroke.
            event.accepted = false
        }
        Keys.onRightPressed: {
            // From the last card in the grid → jump to the inline "+ Add Hosts"
            // tile. Other right-arrow presses fall through to GridView's
            // default column navigation.
            if (count > 0 && currentIndex === count - 1 && !pcList.subFocusOptions) {
                addHostTile.forceActiveFocus()
                event.accepted = true
                return
            }
            event.accepted = false
        }
        Keys.onDownPressed: {
            if (count <= 0) {
                event.accepted = false
                return
            }
            // First press from card → sub-focus the Options of the same cell.
            if (!pcList.subFocusOptions) {
                pcList.subFocusOptions = true
                event.accepted = true
                return
            }
            // Already on Options: drop sub-focus and let the GridView move down.
            pcList.subFocusOptions = false
            event.accepted = false
        }

        // (Empty-state message was moved out of pcList so it does not
        //  visually collide with the addHostTile that we always render at
        //  the leftmost cell. See `emptyStateRow` below, anchored to the
        //  right of the addHostTile + refreshTile pair.)

        model: computerModel

        delegate: Item {
            id: pcCellWrap
            width: pcList.cellWidth
            height: pcList.cellHeight

            // Exposed to the GridView's key handlers
            function activateCard() { pcCard.doActivate() }
            function openCardMenu() {
                if (!hostOptsDropdown.opened) hostOptsDropdown.open()
            }

            // Items to show in the dropdown — re-evaluated each time the
            // host's state flags change.
            property var menuItems: {
                var items = []
                if (model.online && model.paired)        items.push({ kind: "viewAllApps",        text: qsTr("View All Apps") })
                if (!model.online && model.wakeable)     items.push({ kind: "wake",               text: qsTr("Wake PC") })
                items.push({ kind: "testNetwork",        text: qsTr("Test Network") })
                items.push({ kind: "rename",             text: qsTr("Rename PC") })
                items.push({ kind: "delete",             text: qsTr("Delete PC") })
                items.push({ kind: "viewDetails",        text: qsTr("View Details") })
                if (model.online && model.paired)        items.push({ kind: "prepareStreamTweak", text: qsTr("StreamTweak Streaming Mode") })
                if (model.online && model.paired
                    && (pcCard.streamTweakAuth === "pending" || pcCard.streamTweakAuth === "denied"))
                                                         items.push({ kind: "requestStAuth", text: qsTr("Request StreamTweak access") })
                return items
            }

            function triggerOption(kind) {
                switch (kind) {
                case "viewAllApps":
                    homeScreen.appShell.showApps(index, homeScreen.computerModel, true,
                                                  model.name, model.address, model.gpuModel,
                                                  model.isTailscaleClone)
                    break
                case "wake":
                    homeScreen.computerModel.wakeComputer(index)
                    break
                case "testNetwork":
                    homeScreen.computerModel.testConnectionForComputer(index)
                    testConnectionDialog.open()
                    break
                case "rename":
                    renamePcDialog.pcIndex = index
                    renamePcDialog.originalName = model.name
                    renamePcDialog.open()
                    break
                case "delete":
                    deletePcDialog.pcIndex = index
                    deletePcDialog.pcName = model.name
                    deletePcDialog.open()
                    break
                case "viewDetails":
                    showPcDetailsDialog.pcDetails = model.details
                    showPcDetailsDialog.open()
                    break
                case "prepareStreamTweak":
                    homeScreen.computerModel.prepareStreamTweak(index)
                    prepareCountdownDialog.countdown = 10
                    prepareCountdownDialog.open()
                    prepareCountdownTimer.restart()
                    break
                case "requestStAuth":
                    homeScreen.stForcePin(index)
                    homeScreen.computerModel.requestStreamTweakAuth(index)
                    break
                }
                hostOptsDropdown.close()
            }

            // Inner card — portrait, fixed height; Options button sits underneath.
            Rectangle {
                id: pcCard
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                height: 320
                radius: 12
                clip: true

                // (pcContextMenu alias removed — see hostOptsDropdown below)
                property string streamTweakStatus: ""
                // StreamTweak access state: "" (unknown) | "none" | "authorized" | "pending" | "denied"
                property string streamTweakAuth: ""
                property var _colors: homeScreen.hostColorPair(model.name)

                // NIC speed: fetch on card show + poll every 2 s while online.
                Component.onCompleted: {
                    if (model.online && model.paired) {
                        homeScreen.computerModel.requestStreamTweakStatus(index)
                        homeScreen.computerModel.requestStreamTweakAuth(index)
                    }
                }
                Timer {
                    id: nicPollTimer
                    interval: 2000
                    repeat: true
                    running: model.online && model.paired
                    onTriggered: {
                        homeScreen.computerModel.requestStreamTweakStatus(index)
                    }
                }
                // Re-poll the access state until the host user approves it, then stop.
                // Poll the access state until it settles (authorized, or "open" when
                // the host doesn't enforce auth). A later switch to enforced auth is
                // still caught via the STATUS ERR_UNAUTHORIZED path.
                Timer {
                    id: authPollTimer
                    interval: 2500
                    repeat: true
                    running: model.online && model.paired
                             && pcCard.streamTweakAuth !== "authorized"
                             && pcCard.streamTweakAuth !== "open"
                    onTriggered: {
                        homeScreen.computerModel.requestStreamTweakAuth(index)
                    }
                }

                // _current = this cell is the currentIndex of the GridView.
                // _focused = currentIndex + GridView has activeFocus + sub-focus
                //            is on the card (not on the Options button).
                // _optsFocused = same but sub-focus is on the Options button.
                // All three are suppressed when the user is driving with the
                // mouse (pointer mode) so the keyboard/gamepad selection ring
                // never coexists with the hover ring on a different card.
                readonly property bool _isCurrent:   pcList.currentIndex === index
                readonly property bool _current:     _isCurrent && !homeScreen._pointerMode
                readonly property bool _focused:     _current && pcList.activeFocus && !pcList.subFocusOptions
                readonly property bool _optsFocused: _current && pcList.activeFocus &&  pcList.subFocusOptions
                readonly property bool _hoverVisible: cardMouse.containsMouse && !homeScreen._keyMode

                function doActivate() {
                    if (model.statusUnknown) return
                    if (model.online && model.paired) {
                        if (!model.serverSupported) {
                            errorDialog.text = qsTr("The version of GeForce Experience on %1 is not supported by this build of StreamLight. You must update StreamLight to stream from %1.").arg(model.name)
                            errorDialog.helpText = ""
                            errorDialog.open()
                        } else {
                            // Use homeScreen.appShell explicitly — same scope
                            // safety pattern as appGrid.appModel in AppsScreen.
                            homeScreen.appShell.showApps(index, homeScreen.computerModel, false,
                                                          model.name, model.address, model.gpuModel,
                                                          model.isTailscaleClone)
                        }
                    } else if (model.online && !model.paired) {
                        var pin = homeScreen.computerModel.generatePinString()
                        homeScreen.computerModel.pairComputer(index, pin)
                        pairDialog.pin = pin
                        pairDialog.open()
                    } else if (!model.online && model.wakeable) {
                        homeScreen.computerModel.wakeComputer(index)
                    }
                }

                // Diagonal-gradient background (top-left lighter → bottom-right darker).
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: pcCard._colors[0] }
                    GradientStop { position: 1.0; color: pcCard._colors[1] }
                }

                // Border highlights selection. Bright when focused (gamepad
                // or keyboard active), softer when only the current item but
                // the grid lacks focus, mouse hover always green.
                border.color: pcCard._focused        ? homeScreen._green
                             : pcCard._current       ? Qt.rgba(0.063, 0.486, 0.063, 0.55)
                             : pcCard._hoverVisible  ? homeScreen._green
                             :                         "transparent"
                border.width: (pcCard._focused || pcCard._current || pcCard._hoverVisible) ? 2 : 0

                function formatStreamTweakStatus(raw) {
                    if (raw === "" || raw === null) return qsTr("N/A")
                    var mbps = parseInt(raw)
                    if (isNaN(mbps)) return raw
                    if (mbps >= 1000) {
                        var gbps = mbps / 1000
                        return (gbps === Math.floor(gbps) ? gbps.toFixed(0) : gbps.toFixed(1)) + " Gbps"
                    }
                    return mbps + " Mbps"
                }

                Connections {
                    target: computerModel
                    function onStreamTweakStatusReceived(idx, status) {
                        if (idx === index) {
                            pcCard.streamTweakStatus = pcCard.formatStreamTweakStatus(status)
                        }
                    }
                    function onStreamTweakAuthReceived(idx, state, pin) {
                        if (idx === index) {
                            pcCard.streamTweakAuth = state
                            if (state === "pending" && pin.length > 0)
                                homeScreen.stShowPin(index, model.name, model.address, pin)
                            else
                                homeScreen.stHidePin(index)
                        }
                    }
                }

                // ── Decorative diagonal stripe pattern (very subtle) ──────────
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

                // ── Tailscale badge — top left (clone tiles only) ─────────────
                Rectangle {
                    id: tailscaleBadge
                    visible: model.isTailscaleClone === true
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.topMargin: 12
                    anchors.leftMargin: 12
                    height: 24
                    width: tailscaleBadgeRow.implicitWidth + 16
                    radius: 3
                    color: Qt.rgba(0, 0, 0, 0.45)
                    border.color: "#ffffff"
                    border.width: 1

                    Row {
                        id: tailscaleBadgeRow
                        anchors.centerIn: parent
                        spacing: 6

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("TAILSCALE")
                            color: "#ffffff"
                            font.family: "DM Sans"
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1
                        }
                    }
                }

                // ── Status badge — top right ──────────────────────────────────
                Rectangle {
                    id: statusBadge
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: 12
                    anchors.rightMargin: 12
                    height: 24
                    width: badgeRow.implicitWidth + 16
                    radius: 3
                    color: Qt.rgba(0, 0, 0, 0.45)
                    border.color: badgeRow._stateColor
                    border.width: 1

                    Row {
                        id: badgeRow
                        anchors.centerIn: parent
                        spacing: 6

                        property string _stateLabel:
                              model.statusUnknown          ? qsTr("CHECKING")
                            : model.online && model.paired ? qsTr("ONLINE")
                            : model.online                 ? qsTr("REACHABLE")
                            :                                qsTr("OFFLINE")

                        property color _stateColor:
                              model.statusUnknown          ? homeScreen._textMut
                            : model.online && model.paired ? homeScreen._greenLk
                            : model.online                 ? homeScreen._amber
                            :                                homeScreen._textMut

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: badgeRow._stateLabel
                            color: badgeRow._stateColor
                            font.family: "DM Sans"
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1
                        }
                    }
                }

                // ── StreamTweak access badge — under the status badge ─────────
                // Shows the host's authorization state for this client. Hidden when
                // StreamTweak is absent / legacy / unreachable (state "none" or "").
                Rectangle {
                    id: authBadge
                    visible: pcCard.streamTweakAuth === "authorized"
                          || pcCard.streamTweakAuth === "pending"
                          || pcCard.streamTweakAuth === "denied"
                    anchors.top: statusBadge.bottom
                    anchors.right: parent.right
                    anchors.topMargin: 6
                    anchors.rightMargin: 12
                    height: 24
                    width: authBadgeRow.implicitWidth + 16
                    radius: 3
                    color: Qt.rgba(0, 0, 0, 0.45)
                    border.color: authBadgeRow._color
                    border.width: 1

                    Row {
                        id: authBadgeRow
                        anchors.centerIn: parent
                        spacing: 6

                        property color _color:
                              pcCard.streamTweakAuth === "authorized" ? homeScreen._green
                            : pcCard.streamTweakAuth === "pending"    ? homeScreen._amber
                            :                                           homeScreen._red

                        property string _label:
                              pcCard.streamTweakAuth === "authorized" ? qsTr("AUTHORIZED")
                            : pcCard.streamTweakAuth === "pending"    ? qsTr("PENDING")
                            :                                           qsTr("DENIED")

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: authBadgeRow._label
                            color: authBadgeRow._color
                            font.family: "DM Sans"
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1
                        }
                    }
                }

                // ── Central monitor icon (semi-transparent) ───────────────────
                Image {
                    id: monitorIcon
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -16
                    source: "qrc:/res/desktop_windows-48px.svg"
                    sourceSize { width: 96; height: 96 }
                    width: 96; height: 96
                    opacity: model.online ? 0.55 : 0.25
                }

                // Busy spinner for unknown state — overlays the monitor icon
                BusyIndicator {
                    anchors.centerIn: monitorIcon
                    width: 80; height: 80
                    visible: model.statusUnknown
                    running: visible
                }

                // ── Bottom block: name + ip · gpu ─────────────────────────────
                Column {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 18
                    anchors.bottomMargin: 18
                    spacing: 4

                    Label {
                        text: model.name
                        font.family: "DM Sans"
                        font.pixelSize: 26
                        font.bold: true
                        color: model.online ? homeScreen._text : homeScreen._textDim
                        elide: Label.ElideRight
                    }

                    // IP address (and optional · GPU on the same line)
                    Row {
                        spacing: 6
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: model.address && model.address.length > 0 ? model.address : qsTr("N/A")
                            font.family: homeScreen._mono
                            font.pixelSize: 13
                            color: homeScreen._textDim
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: model.gpuModel && model.gpuModel.length > 0
                            text: " · "
                            font.family: "DM Sans"
                            font.pixelSize: 13
                            color: homeScreen._textMut
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: model.gpuModel && model.gpuModel.length > 0
                            text: model.gpuModel || ""
                            font.family: homeScreen._mono
                            font.pixelSize: 13
                            color: homeScreen._textDim
                        }
                    }
                    // NIC speed (separate line below IP, fetched from StreamTweak)
                    Label {
                        visible: pcCard.streamTweakStatus.length > 0
                              && pcCard.streamTweakStatus !== qsTr("N/A")
                        text: pcCard.streamTweakStatus
                        font.family: homeScreen._mono
                        font.pixelSize: 13
                        color: homeScreen._greenLk
                    }
                }

                // ── Click handling ────────────────────────────────────────────
                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        pcList.currentIndex = index
                        pcList.forceActiveFocus()
                        pcCard.doActivate()
                    }
                }

                // (legacy "⋯" button removed — Options is now a button under
                // the card, see optsBtn below.)

                // (pcContextMenuLoader / NavigableMenu replaced by the inline
                //  hostOptsDropdown Popup below — same set of actions, but
                //  rendered as a flat list under the Options button.)
            }

            // ── Options button — below the card ───────────────────────────────
            // Independently navigable: D-pad Down from card focuses it. Press A
            // (✕) or click → opens the dropdown list under the button.
            Rectangle {
                id: optsBtn
                anchors.top: pcCard.bottom
                anchors.left: pcCard.left
                anchors.right: pcCard.right
                anchors.topMargin: 4
                height: 30
                radius: 6
                color: pcCard._optsFocused ? Qt.rgba(0.063, 0.486, 0.063, 0.28)
                     : pcCard._current     ? Qt.rgba(1,1,1,0.05)
                     : Qt.rgba(1,1,1,0.03)
                border.color: pcCard._optsFocused ? homeScreen._green
                            : pcCard._current     ? Qt.rgba(0.063, 0.486, 0.063, 0.45)
                            :                       Qt.rgba(1,1,1,0.10)
                border.width: pcCard._optsFocused ? 2 : 1

                Label {
                    anchors.centerIn: parent
                    text: qsTr("Options")
                    // Always white — was previously dimmed when not focused
                    // which made the label hard to read against the dark card.
                    color: homeScreen._text
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 0.6
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        pcList.currentIndex = index
                        pcList.subFocusOptions = true
                        pcList.forceActiveFocus()
                        pcCellWrap.openCardMenu()
                    }
                }
            }

            // Options dropdown opens upwards above the Options button. Non-modal.
            Popup {
                id: hostOptsDropdown
                x: optsBtn.x
                y: optsBtn.y - hostOptsDropdown.implicitHeight - 4
                width: optsBtn.width
                implicitHeight: optsList.implicitHeight
                padding: 0
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside | Popup.CloseOnPressOutsideParent

                contentItem: ListView {
                    id: optsList
                    implicitHeight: contentHeight + 8
                    topMargin: 4
                    bottomMargin: 4
                    focus: true
                    keyNavigationEnabled: true
                    interactive: false
                    currentIndex: 0
                    model: pcCellWrap.menuItems
                    clip: true

                    delegate: Item {
                        width: optsList.width
                        height: 32

                        Rectangle {
                            anchors.fill: parent
                            color: optsList.currentIndex === index ? Qt.rgba(1,1,1,0.06) : "transparent"
                        }
                        // Left accent bar when selected (StreamLight green)
                        Rectangle {
                            visible: optsList.currentIndex === index
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 3
                            color: homeScreen._greenLk
                        }
                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.text
                            color: homeScreen._text
                            font.family: "DM Sans"
                            font.pixelSize: 13
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered:  optsList.currentIndex = index
                            onClicked:  pcCellWrap.triggerOption(modelData.kind)
                        }
                    }

                    Keys.onReturnPressed: { pcCellWrap.triggerOption(model[currentIndex].kind); event.accepted = true }
                    Keys.onEnterPressed:  { pcCellWrap.triggerOption(model[currentIndex].kind); event.accepted = true }
                    Keys.onSpacePressed:  { pcCellWrap.triggerOption(model[currentIndex].kind); event.accepted = true }
                }

                background: Rectangle {
                    color: "#1a1a1a"
                    border.color: "#2a2a2a"
                    border.width: 1
                    radius: 6
                }

                onOpened: {
                    optsList.currentIndex = 0
                    optsList.forceActiveFocus()
                }
            }
        }
    }

    // "+ Add Hosts" tile + small square Refresh, slotted after the last host.
    Item {
        id: addHostTile
        x: pcList.x + ((pcList.count % pcList._columns) * pcList.cellWidth)
        y: pcList.y + (Math.floor(pcList.count / pcList._columns) * pcList.cellHeight)
        width: pcList.cellWidth
        height: pcList.cellHeight

        activeFocusOnTab: true

        readonly property bool _keyFocused: activeFocus && !homeScreen._pointerMode
        readonly property bool _hoverVisible: addHostMouse.containsMouse && !homeScreen._keyMode

        Rectangle {
            anchors.fill: parent
            anchors.margins: 8
            radius: 12
            color: addHostTile._keyFocused    ? Qt.rgba(0.0, 0.9, 0.46, 0.10)
                 : addHostTile._hoverVisible  ? Qt.rgba(0.0, 0.9, 0.46, 0.06)
                 :                              homeScreen._bg2
            border.color: (addHostTile._keyFocused || addHostTile._hoverVisible)
                          ? homeScreen._green
                          : homeScreen._border
            border.width: addHostTile._keyFocused   ? 3
                        : addHostTile._hoverVisible ? 2 : 1

            Column {
                anchors.centerIn: parent
                spacing: 14

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "+"
                    font.family: "DM Sans"
                    font.pixelSize: 64
                    font.bold: true
                    color: homeScreen._green
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Add Hosts")
                    font.family: "DM Sans"
                    font.pixelSize: 20
                    font.bold: true
                    color: homeScreen._text
                }
            }

            MouseArea {
                id: addHostMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    addHostTile.forceActiveFocus()
                    addPcDialog.open()
                }
            }
        }

        Keys.onReturnPressed: { addPcDialog.open(); event.accepted = true }
        Keys.onEnterPressed:  { addPcDialog.open(); event.accepted = true }
        Keys.onSpacePressed:  { addPcDialog.open(); event.accepted = true }
        Keys.onRightPressed:  { refreshTile.forceActiveFocus(); event.accepted = true }
        Keys.onLeftPressed: {
            if (pcList.count > 0) {
                pcList.currentIndex = pcList.count - 1
                pcList.forceActiveFocus()
                event.accepted = true
            } else {
                event.accepted = false
            }
        }
        Keys.onUpPressed: {
            if (pcList.count > pcList._columns) {
                // Jump up to the row above the Add Hosts tile.
                pcList.currentIndex = Math.max(0, pcList.count - pcList._columns)
                pcList.forceActiveFocus()
                event.accepted = true
            } else {
                event.accepted = false
            }
        }
    }

    Item {
        id: refreshTile
        x: addHostTile.x + addHostTile.width
        // Vertically centre the small button on the Add Hosts tile.
        y: addHostTile.y + (addHostTile.height - height) / 2
        width: 88
        height: 88

        activeFocusOnTab: true

        readonly property bool _keyFocused: activeFocus && !homeScreen._pointerMode
        readonly property bool _hoverVisible: refreshMouse.containsMouse && !homeScreen._keyMode

        Rectangle {
            anchors.fill: parent
            anchors.margins: 8
            radius: 8
            color: refreshTile._keyFocused    ? Qt.rgba(0.0, 0.9, 0.46, 0.10)
                 : refreshTile._hoverVisible  ? Qt.rgba(0.0, 0.9, 0.46, 0.06)
                 :                              homeScreen._bg2
            border.color: (refreshTile._keyFocused || refreshTile._hoverVisible)
                          ? homeScreen._green
                          : homeScreen._border
            border.width: refreshTile._keyFocused   ? 3
                        : refreshTile._hoverVisible ? 2 : 1

            Label {
                anchors.centerIn: parent
                text: "↻"
                font.family: "DM Sans"
                font.pixelSize: 28
                font.bold: true
                color: homeScreen._text
            }

            MouseArea {
                id: refreshMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    refreshTile.forceActiveFocus()
                    ComputerManager.startPolling()
                }
            }
        }

        Keys.onReturnPressed: { ComputerManager.startPolling(); event.accepted = true }
        Keys.onEnterPressed:  { ComputerManager.startPolling(); event.accepted = true }
        Keys.onSpacePressed:  { ComputerManager.startPolling(); event.accepted = true }
        Keys.onLeftPressed:   { addHostTile.forceActiveFocus(); event.accepted = true }
    }

    // Empty-state message, visible only when no host is paired/discovered yet.
    Row {
        id: emptyStateRow
        anchors.left: refreshTile.right
        anchors.leftMargin: 20
        anchors.verticalCenter: addHostTile.verticalCenter
        spacing: 10
        visible: pcList.count === 0

        BusyIndicator {
            id: searchSpinner
            visible: StreamingPreferences.enableMdns
            running: visible
            width: 28; height: 28
        }

        Label {
            anchors.verticalCenter: searchSpinner.verticalCenter
            elide: Label.ElideRight
            text: StreamingPreferences.enableMdns
                ? qsTr("Searching for compatible hosts on your local network…")
                : qsTr("Automatic PC discovery is disabled. Add your PC manually.")
            font.family: "DM Sans"
            font.pixelSize: 14
            color: homeScreen._textDim
        }
    }

    // ── Dialogs ───────────────────────────────────────────────────────────────

    ErrorMessageDialog {
        id: errorDialog
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide"
    }

    PairDialog { id: pairDialog }

    TailscaleSuggestDialog { id: tailscaleSuggestDialog }

    NavigableMessageDialog {
        id: deletePcDialog
        property int pcIndex: -1
        property string pcName: ""
        headerText: qsTr("DELETE PC")
        affirmativeIsDanger: true
        text: qsTr("Are you sure you want to remove '%1'?").arg(pcName)
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: computerModel.deleteComputer(pcIndex)
    }

    NavigableMessageDialog {
        id: testConnectionDialog
        headerText: qsTr("NETWORK TEST")
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok

        onAboutToShow: {
            text = qsTr("StreamLight is testing your network connection to determine if any required ports are blocked.") +
                   "\n\n" + qsTr("This may take a few seconds…")
            showSpinner = true
        }

        function connectionTestComplete(result, blockedPorts) {
            if (result === -1) {
                text = qsTr("The network test could not be performed because none of StreamLight's connection testing servers were reachable from this PC. Check your Internet connection or try again later.")
                imageSrc = "qrc:/res/baseline-warning-24px.svg"
            } else if (result === 0) {
                text = qsTr("This network does not appear to be blocking StreamLight. If you still have trouble connecting, check your PC's firewall settings.") + "\n\n" +
                       qsTr("If you are trying to stream over the Internet, install the Moonlight Internet Hosting Tool on your gaming PC and run the included Internet Streaming Tester to check your gaming PC's Internet connection.")
                imageSrc = "qrc:/res/baseline-check_circle_outline-24px.svg"
            } else {
                text = qsTr("Your PC's current network connection seems to be blocking StreamLight. Streaming over the Internet may not work while connected to this network.") + "\n\n" +
                       qsTr("The following network ports were blocked:") + "\n"
                text += blockedPorts
                imageSrc = "qrc:/res/baseline-error_outline-24px.svg"
            }
            showSpinner = false
        }
    }

    NavigableDialog {
        id: renamePcDialog
        property string label: qsTr("Enter the new name for this PC")
        property string originalName
        property int pcIndex: -1
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            editText.text = renamePcDialog.originalName
            editText.forceActiveFocus()
            editText.selectAll()
        }
        onClosed: editText.clear()
        onAccepted: {
            if (editText.text) {
                computerModel.renameComputer(pcIndex, editText.text)
            }
        }

        ColumnLayout {
            spacing: 22

            Label {
                text: qsTr("RENAME PC")
                font.family: "DM Sans"
                font.pixelSize: 13
                font.bold: true
                font.letterSpacing: 1.6
                color: "#707070"
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: renamePcDialog.label
                font.family: "DM Sans"
                font.pixelSize: 18
                color: "#f0f0f0"
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: 520
            }

            TextField {
                id: editText
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 360
                implicitHeight: 48
                color: "#f0f0f0"
                selectionColor: Qt.rgba(0, 0.9, 0.46, 0.30)
                selectedTextColor: "#0d1410"
                font.family: "DM Sans"
                font.pixelSize: 18
                font.bold: true
                horizontalAlignment: TextInput.AlignHCenter
                focus: true

                background: Rectangle {
                    color: "#0f0f0f"
                    radius: 8
                    border.color: editText.activeFocus ? "#00E676" : "#2a2a2a"
                    border.width: editText.activeFocus ? 2 : 1
                }

                Keys.onReturnPressed: renamePcDialog.accept()
                Keys.onEnterPressed:  renamePcDialog.accept()
            }
        }
    }

    NavigableMessageDialog {
        id: showPcDetailsDialog
        headerText: qsTr("HOST DETAILS")
        property string pcDetails: ""
        text: pcDetails
        standardButtons: Dialog.Ok
    }

    PrepareCountdownDialog {
        id: prepareCountdownDialog
    }

    Timer {
        id: prepareCountdownTimer
        interval: 1000
        repeat: true
        onTriggered: {
            prepareCountdownDialog.countdown--
            if (prepareCountdownDialog.countdown <= 0) {
                prepareCountdownTimer.stop()
                prepareCountdownDialog.close()
            }
        }
    }

    AddHostDialog {
        id: addPcDialog
        onAccepted: function(ip) { ComputerManager.addNewHostManually(ip) }
    }

    // ── StreamTweak access PIN popup ──────────────────────────────────────────
    Popup {
        id: stPinDialog
        modal: true
        closePolicy: Popup.CloseOnEscape
        width: 440
        height: 300
        x: (homeScreen.width - width) / 2
        y: (homeScreen.height - height) / 2

        background: Rectangle {
            color: "#1a1a1a"
            border.color: "#2a2a2a"
            border.width: 1
            radius: 12
        }

        contentItem: Column {
            spacing: 18

            Label {
                text: qsTr("STREAMTWEAK ACCESS")
                font.family: "DM Sans"
                font.pixelSize: 13
                font.bold: true
                font.letterSpacing: 1.6
                color: homeScreen._textMut
            }
            Label {
                width: stPinDialog.availableWidth
                wrapMode: Text.Wrap
                text: qsTr("StreamTweak on %1 (%2) is asking to allow this device. Check that the PIN below matches the one shown on the host, then click Allow there.\n\nStreaming works without this — authorizing only enables host metrics, NIC speed, store badges and session reports.").arg(homeScreen.stPinHostName).arg(homeScreen.stPinHostAddr)
                font.family: "DM Sans"
                font.pixelSize: 14
                color: homeScreen._text
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: homeScreen.stPinValue
                font.family: homeScreen._mono
                font.pixelSize: 52
                font.bold: true
                font.letterSpacing: 10
                color: homeScreen._green
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 130; height: 42; radius: 8
                color: stPinClose.containsMouse ? homeScreen._bgHov : homeScreen._bg2
                border.color: homeScreen._border
                border.width: 1
                Label {
                    anchors.centerIn: parent
                    text: qsTr("Dismiss")
                    color: homeScreen._text
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                }
                MouseArea {
                    id: stPinClose
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (homeScreen.stPinHost >= 0)
                            homeScreen.stPinDismissed[homeScreen.stPinHost] = true
                        stPinDialog.close()
                    }
                }
            }
        }
    }

    // Invoked from main.qml Ctrl+N shortcut.
    function openAddPc() {
        addPcDialog.open()
    }
}
