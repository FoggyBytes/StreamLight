import QtQuick 2.12
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2

import ComputerModel 1.0
import ComputerManager 1.0
import StreamingPreferences 1.0
import SdlGamepadKeyNavigation 1.0
import SystemProperties 1.0

// Root is FocusScope (not Item) — required for activeFocus propagation from
// the Loader above us down into pcList. Plain Items do not propagate.
FocusScope {
    id: homeScreen
    anchors.fill: parent
    focus: true

    property var appShell: null
    property ComputerModel computerModel: createModel()

    // Index of the currently-highlighted host card (for the Settings active-profile
    // greying when Settings is opened from Home).
    readonly property int currentHostIndex: pcList.currentIndex

    // Whether Tailscale is installed on THIS client (drives the greyed "Tailscale"
    // host option). Evaluated once — the client's install state rarely changes mid-run.
    readonly property bool _clientHasTailscale: computerModel ? computerModel.clientHasTailscale() : false

    // Opens the POWER chooser for the currently-focused host card. Used by the
    // status-bar "X Shutdown" shortcut (mouse click + gamepad X). When the focused
    // cell is an online+paired host, opens the full Host/Client/Both chooser
    // (mirrors the Options → "Power…" entry). Otherwise — an offline host, the
    // "+ Add" tile, or no hosts at all — it still opens the chooser for THIS client
    // only, so the user can always power off the device they're holding.
    function openPowerForCurrent() {
        var cur = pcList.currentItem
        if (cur && cur.isPowerableHost && cur.openPower)
            cur.openPower()
        else
            openPowerClientOnly()
    }

    // Power chooser limited to the local client (no reachable/authorized host).
    function openPowerClientOnly() {
        powerDialog.clientOnly         = true
        powerDialog.pcIndex            = -1
        powerDialog.hostName           = ""
        powerDialog.authState          = "none"   // Host & Both disabled; Client default
        powerDialog.clientUpdateState  = SystemProperties.updatesPending() ? "pending" : "none"
        powerDialog.hostUpdateState    = "unavailable"
        powerDialog.open()
    }

    // Drops any "force Tailscale" session pin so the poller reverts to LAN-first.
    // Called when returning to the host list (after an apps/stream session).
    function clearTailscalePreferences() {
        if (computerModel) computerModel.clearTailscalePreferences()
    }

    // ── Per-host profile switching (LB/RB status-bar shortcut) ────────────────
    // Number of profiles on the currently-focused host card (0 for the "+ Add"
    // tile / no hosts). Drives the LB/RB hints, which hide when ≤ 1.
    readonly property int focusedProfileCount:
        (pcList.currentItem && pcList.currentItem.profileCount !== undefined)
            ? pcList.currentItem.profileCount : 0

    // Cycles the focused host's active profile (dir: -1 prev / +1 next). No-op
    // unless the focused card is a host with more than one profile.
    function cycleFocusedProfile(dir) {
        if (!computerModel) return
        var cur = pcList.currentItem
        if (cur && cur.profileCount !== undefined && cur.profileCount > 1)
            computerModel.cycleHostProfile(pcList.currentIndex, dir)
    }

    // ── Remote "Update host" job state (one at a time, survives popup close) ───
    property bool   updateJobActive: false
    property int    updateJobHostIndex: -1
    property string updateJobHostName: ""
    property string updateJobPhase: "IDLE"
    property int    updateJobPercent: -1
    property string updateJobMessage: ""
    property string updateJobError: ""
    property var    updateJobUpdates: []
    property var    updateJobCounts: ({})
    property bool   _updateInstallStarted: false
    property int    _updateMisses: 0

    function startUpdateCheckFor(index, name) {
        if (updateJobActive) { openUpdateDialog(); return }   // one job at a time
        updateJobHostIndex = index
        updateJobHostName = name
        updateJobActive = true
        _updateInstallStarted = false
        _updateMisses = 0
        updateJobPhase = "CHECKING"; updateJobPercent = -1
        updateJobMessage = ""; updateJobError = ""
        updateJobUpdates = []; updateJobCounts = ({})
        computerModel.startUpdateCheck(index)
        updatePollTimer.restart()
        openUpdateDialog()
    }
    function startUpdateInstall(scope) {
        _updateInstallStarted = true
        updateJobPhase = "DOWNLOADING"; updateJobPercent = 0
        updateJobMessage = qsTr("Preparing…")
        computerModel.startUpdateInstall(updateJobHostIndex, scope)
    }
    function clearUpdateJob() {
        updatePollTimer.stop()
        updateJobActive = false
        updateJobPhase = "IDLE"
        if (updateDialog.opened) updateDialog.close()
    }
    function openUpdateDialog() {
        if (!updateDialog.opened) updateDialog.open()
    }

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
        // Inner pcCard is 320 high; Profiles/Options sit side-by-side below it
        // (one row, 30 high + spacing 4 + margins 8 ≈ 380 total).
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

        // Sub-focus inside a host cell:
        //   0 = card body (Open) · 1 = Profiles button · 2 = Options button.
        // Down from the card focuses Profiles; the two buttons sit side-by-side
        // and are walked with Left/Right; Up returns to the card.
        property int subFocus: 0

        // ── Gamepad / keyboard handling ────────────────────────────────────────
        // A (Space/Return) → activate selected card
        // X (Menu)         → open the POWER chooser for the selected host
        // Y (Hangup)       → shortcut to Settings (consistent with AppsScreen)
        // A acts on the sub-focus: Options button → open menu, card → open host.
        Keys.onReturnPressed: { _activate(); event.accepted = true }
        Keys.onEnterPressed:  { _activate(); event.accepted = true }
        Keys.onSpacePressed:  { _activate(); event.accepted = true }
        Keys.onHangupPressed: { if (appShell) appShell.openSettings();        event.accepted = true }
        Keys.onMenuPressed:   { homeScreen.openPowerForCurrent();             event.accepted = true }

        function _activate() {
            if (!currentItem) return
            if (pcList.subFocus === 2)      currentItem.openCardMenu()
            else if (pcList.subFocus === 1) currentItem.openProfiles()
            else                            currentItem.activateCard()
        }

        // Cross-area navigation + card/Options sub-focus.
        // (The left-edge wrap that used to refocus the sidebar was removed
        //  with the sidebar itself in 3.0; we now let GridView handle the
        //  arrow internally, which simply does nothing on the first column.)
        Keys.onUpPressed: {
            // From either button (Profiles/Options) → back to the card body.
            if (pcList.subFocus > 0) {
                pcList.subFocus = 0
                event.accepted = true
                return
            }
            // First row of cards has nothing above (top action buttons removed).
            // Let the GridView handle internal navigation; on the very top
            // row, Qt simply ignores the keystroke.
            event.accepted = false
        }
        Keys.onDownPressed: {
            if (count <= 0) {
                event.accepted = false
                return
            }
            // Card → Profiles (left button of the side-by-side pair).
            if (pcList.subFocus === 0) {
                pcList.subFocus = 1
                event.accepted = true
                return
            }
            // Already on a button: drop sub-focus and let the GridView move down.
            pcList.subFocus = 0
            event.accepted = false
        }
        Keys.onLeftPressed: {
            // On the buttons row: Options → Profiles; stay on Profiles.
            if (pcList.subFocus === 2) {
                pcList.subFocus = 1
                event.accepted = true
                return
            }
            if (pcList.subFocus === 1) {
                event.accepted = true
                return
            }
            // Card focus: let GridView do its column navigation.
            event.accepted = false
        }
        Keys.onRightPressed: {
            // On the buttons row: Profiles → Options; stay on Options.
            if (pcList.subFocus === 1) {
                pcList.subFocus = 2
                event.accepted = true
                return
            }
            if (pcList.subFocus === 2) {
                event.accepted = true
                return
            }
            // Card focus: from the last card jump to the inline "+ Add Hosts"
            // tile; otherwise fall through to GridView's column navigation.
            if (count > 0 && currentIndex === count - 1) {
                addHostTile.forceActiveFocus()
                event.accepted = true
                return
            }
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

            // True when this host can be powered off remotely (drives the X shortcut
            // fallback to client-only when it isn't).
            readonly property bool isPowerableHost: model.online && model.paired

            // Number of saved streaming profiles for this host (drives the chip and
            // the LB/RB status-bar shortcut). Aliased so it tracks model updates.
            property int profileCount: model.profileCount

            // Exposed to the GridView's key handlers
            function activateCard() { pcCard.doActivate() }
            function openCardMenu() {
                if (!hostOptsDropdown.opened) hostOptsDropdown.open()
            }
            // Opens the POWER chooser for this host. Shared by the Options → "Power…"
            // entry and the status-bar "X Shutdown" shortcut. Gated to online+paired
            // hosts (the X shortcut can land on any focused card).
            function openPower() {
                if (!model.online || !model.paired)
                    return
                powerDialog.clientOnly           = false
                powerDialog.pcIndex              = index
                powerDialog.hostName             = model.name
                powerDialog.authState            = pcCard.streamTweakAuth
                // Seed update status: client read synchronously; host arrives async.
                powerDialog.clientUpdateState    = SystemProperties.updatesPending() ? "pending" : "none"
                if (pcCard.streamTweakAuth === "authorized") {
                    powerDialog.hostUpdateState  = "checking"
                    homeScreen.computerModel.requestUpdateState(index)
                } else {
                    powerDialog.hostUpdateState  = "unavailable"
                }
                powerDialog.open()
            }

            // Opens the per-host streaming-profiles manager. Shared by the
            // Profiles button and (future) shortcuts.
            function openProfiles() {
                hostProfilesDialog.pcIndex  = index
                hostProfilesDialog.hostName = model.name
                hostProfilesDialog.reload()
                hostProfilesDialog.open()
            }

            // Items to show in the dropdown — re-evaluated each time the
            // host's state flags change.
            // Tiles for the Options popup: { kind, icon (emoji), label (compact), danger? }.
            property var menuItems: {
                var items = []
                if (model.online && model.paired)        items.push({ kind: "viewAllApps",        icon: "🎮", label: qsTr("All Apps") })
                // Tailscale: opens the host's apps over the 100.x endpoint. Greyed
                // (non-clickable) when Tailscale isn't installed on this client.
                if (model.online && model.paired && model.hasTailscale)
                                                         items.push({ kind: "tailscale", iconSource: "qrc:/res/tailscale.svg",
                                                                      label: qsTr("Tailscale"), disabled: !homeScreen._clientHasTailscale })
                if (!model.online && model.wakeable)     items.push({ kind: "wake",               icon: "⏰", label: qsTr("Wake") })
                items.push({ kind: "testNetwork",        icon: "📡", label: qsTr("Test Network") })
                items.push({ kind: "rename",             icon: "✏️", label: qsTr("Rename") })
                items.push({ kind: "delete",             icon: "🗑️", label: qsTr("Delete"), danger: true })
                items.push({ kind: "viewDetails",        icon: "ℹ️", label: qsTr("Details") })
                if (model.online && model.paired)        items.push({ kind: "prepareStreamTweak", icon: "🚀", label: qsTr("Link-speed switch") })
                if (model.online && model.paired)        items.push({ kind: "power",              icon: "🔌", label: qsTr("Power") })
                if (model.online && model.paired && pcCard.streamTweakAuth === "authorized")
                                                         items.push({ kind: "updateHost",         icon: "🪟", label: qsTr("Windows Update") })
                if (model.online && model.paired
                    && (pcCard.streamTweakAuth === "pending" || pcCard.streamTweakAuth === "denied"))
                                                         items.push({ kind: "requestStAuth",       icon: "🔑", label: qsTr("Request Access") })
                return items
            }

            function triggerOption(kind) {
                hostOptsDropdown.close()   // close the Options popup before opening any target dialog
                switch (kind) {
                case "viewAllApps":
                    homeScreen.appShell.showApps(index, homeScreen.computerModel, true,
                                                  model.name, model.address, model.gpuModel,
                                                  model.isTailscaleClone)
                    break
                case "tailscale":
                    // Force the active connection onto Tailscale, then open the host's
                    // apps on the 100.x endpoint.
                    if (homeScreen.computerModel.prepareTailscaleSession(index)) {
                        homeScreen.appShell.showApps(index, homeScreen.computerModel, true,
                                                      model.name, model.tailscaleAddress, model.gpuModel,
                                                      true)
                    }
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
                case "power":
                    pcCellWrap.openPower()
                    break
                case "updateHost":
                    homeScreen.startUpdateCheckFor(index, model.name)
                    break
                }
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
                readonly property bool _isCurrent:        pcList.currentIndex === index
                readonly property bool _current:          _isCurrent && !homeScreen._pointerMode
                readonly property bool _focused:          _current && pcList.activeFocus && pcList.subFocus === 0
                readonly property bool _profilesFocused:  _current && pcList.activeFocus && pcList.subFocus === 1
                readonly property bool _optsFocused:      _current && pcList.activeFocus && pcList.subFocus === 2
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
                            // Once authorized, learn the host's Tailscale endpoint (if any).
                            if (state === "authorized")
                                homeScreen.computerModel.refreshTailscale(index)
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

                // ── Tailscale badge — top left ────────────────────────────────
                // Shown whenever the host advertises a Tailscale endpoint. Two lines
                // ("TAILSCALE" + "AVAILABLE") when the LAN path is also up; a single
                // "TAILSCALE" line when the host is reachable only via Tailscale.
                Rectangle {
                    id: tailscaleBadge
                    visible: model.hasTailscale === true
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.topMargin: 12
                    anchors.leftMargin: 12
                    width: tailscaleBadgeCol.implicitWidth + 16
                    height: tailscaleBadgeCol.implicitHeight + 10
                    radius: 3
                    color: Qt.rgba(0, 0, 0, 0.45)
                    border.color: "#ffffff"
                    border.width: 1

                    Column {
                        id: tailscaleBadgeCol
                        anchors.centerIn: parent
                        spacing: 1

                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("TAILSCALE")
                            color: "#ffffff"
                            font.family: "DM Sans"
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: !model.tailscaleActive   // LAN path is also available
                            text: qsTr("AVAILABLE")
                            color: "#00E676"
                            font.family: "DM Sans"
                            font.pixelSize: 9
                            font.bold: true
                            font.letterSpacing: 1.5
                        }
                    }
                }

                // ── Status badge — top right ──────────────────────────────────
                // Single box, two stacked lines (matches the TAILSCALE/AVAILABLE badge):
                // connectivity on top, StreamTweak access state below (when present).
                Rectangle {
                    id: statusBadge
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: 12
                    anchors.rightMargin: 12
                    width: statusBadgeCol.implicitWidth + 16
                    height: statusBadgeCol.implicitHeight + 10
                    radius: 3
                    color: Qt.rgba(0, 0, 0, 0.45)
                    border.color: "#ffffff"
                    border.width: 1

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

                    property bool _hasAuth: pcCard.streamTweakAuth === "authorized"
                                         || pcCard.streamTweakAuth === "pending"
                                         || pcCard.streamTweakAuth === "denied"
                    property color _authColor:
                          pcCard.streamTweakAuth === "authorized" ? homeScreen._green
                        : pcCard.streamTweakAuth === "pending"    ? homeScreen._amber
                        :                                           homeScreen._red
                    property string _authLabel:
                          pcCard.streamTweakAuth === "authorized" ? qsTr("AUTHORIZED")
                        : pcCard.streamTweakAuth === "pending"    ? qsTr("PENDING")
                        :                                           qsTr("DENIED")

                    Column {
                        id: statusBadgeCol
                        anchors.centerIn: parent
                        spacing: 1

                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: statusBadge._stateLabel
                            color: statusBadge._stateColor
                            font.family: "DM Sans"
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: statusBadge._hasAuth
                            text: statusBadge._authLabel
                            color: statusBadge._authColor
                            font.family: "DM Sans"
                            font.pixelSize: 9
                            font.bold: true
                            font.letterSpacing: 1.5
                        }
                    }
                }

                // ── Active-profile chip (bottom-right) ────────────────────────
                // Styled like the top badges (white border, all-white text).
                // Shown only when at least one profile exists. Click → next profile.
                Rectangle {
                    id: profileChip
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.bottomMargin: 14
                    anchors.rightMargin: 14
                    visible: model.profileCount >= 1 && model.activeProfileSlot >= 0
                    width: profileChipCol.implicitWidth + 16
                    height: profileChipCol.implicitHeight + 10
                    radius: 3
                    color: Qt.rgba(0, 0, 0, 0.45)
                    border.color: "#ffffff"
                    border.width: 1

                    Column {
                        id: profileChipCol
                        anchors.centerIn: parent
                        spacing: 1

                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("PROFILE %1").arg(model.activeProfileSlot + 1)
                            color: "#ffffff"
                            font.family: "DM Sans"
                            font.pixelSize: 9
                            font.bold: true
                            font.letterSpacing: 1.5
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: model.activeProfileName
                            color: "#ffffff"
                            font.family: "DM Sans"
                            font.pixelSize: 12
                            font.bold: true
                            font.letterSpacing: 0.5
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // Only cycles when there is more than one profile.
                        onClicked: homeScreen.computerModel.cycleHostProfile(index, 1)
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

                    // Physical (LAN) IP — kept even when reached via Tailscale.
                    // With Tailscale the Tailscale IP is shown on the line below.
                    // GPU (when known) sits inline on this physical-IP line.
                    Row {
                        spacing: 6
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !StreamingPreferences.hideHostIps
                            text: {
                                var ip = model.hasTailscale
                                         ? (model.physicalAddress && model.physicalAddress.length ? model.physicalAddress : model.address)
                                         : model.address
                                return ip && ip.length > 0 ? ip : qsTr("N/A")
                            }
                            font.family: homeScreen._mono
                            font.pixelSize: 13
                            color: homeScreen._textDim
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            // Only between a visible IP and the GPU.
                            visible: model.gpuModel && model.gpuModel.length > 0
                                  && !StreamingPreferences.hideHostIps
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
                    // Tailscale IP — only when the host advertises a Tailscale endpoint.
                    Label {
                        visible: model.hasTailscale === true
                              && model.tailscaleAddress && model.tailscaleAddress.length > 0
                              && !StreamingPreferences.hideHostIps
                        text: model.tailscaleAddress || ""
                        font.family: homeScreen._mono
                        font.pixelSize: 13
                        color: homeScreen._textDim
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

            // ── Profiles button — below the card, left half (side-by-side) ────
            // D-pad Down from card focuses it; Right moves to Options.
            // A / click → opens the profiles manager popup.
            Rectangle {
                id: profilesBtn
                anchors.top: pcCard.bottom
                anchors.left: pcCard.left
                anchors.right: pcCard.horizontalCenter
                anchors.topMargin: 4
                anchors.rightMargin: 3
                height: 30
                radius: 6
                color: pcCard._profilesFocused ? Qt.rgba(0.063, 0.486, 0.063, 0.28)
                     : pcCard._current         ? Qt.rgba(1,1,1,0.05)
                     :                           Qt.rgba(1,1,1,0.03)
                border.color: pcCard._profilesFocused ? homeScreen._green
                            : pcCard._current         ? Qt.rgba(0.063, 0.486, 0.063, 0.45)
                            :                           Qt.rgba(1,1,1,0.10)
                border.width: pcCard._profilesFocused ? 2 : 1

                Label {
                    anchors.centerIn: parent
                    text: qsTr("Profiles")
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
                        pcList.subFocus = 1
                        pcList.forceActiveFocus()
                        pcCellWrap.openProfiles()
                    }
                }
            }

            // ── Options button — below the card, right half (side-by-side) ────
            // D-pad Right from Profiles focuses it. Press A or click → opens the
            // options tile grid.
            Rectangle {
                id: optsBtn
                anchors.top: pcCard.bottom
                anchors.left: pcCard.horizontalCenter
                anchors.right: pcCard.right
                anchors.topMargin: 4
                anchors.leftMargin: 3
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
                        pcList.subFocus = 2
                        pcList.forceActiveFocus()
                        pcCellWrap.openCardMenu()
                    }
                }
            }

            // Options chooser — wide centered tile grid (replaces the old dropdown list).
            HostOptionsDialog {
                id: hostOptsDropdown
                hostName: model.name
                items: pcCellWrap.menuItems
                onChosen: function(kind) { pcCellWrap.triggerOption(kind) }
            }

            // Per-host streaming profiles manager.
            HostProfilesDialog {
                id: hostProfilesDialog
                computerModel: homeScreen.computerModel
                // Restore gamepad focus to the host grid so navigation keeps working
                // after the modal closes (otherwise nothing is focused on a handheld).
                onClosed: {
                    pcList.currentIndex = index
                    pcList.subFocus = 1
                    pcList.forceActiveFocus()
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
        // X (Menu): no host to power off here, but still let the user shut down
        // this client (matches the host cards' X shortcut).
        Keys.onMenuPressed:   { homeScreen.openPowerClientOnly(); event.accepted = true }
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
        Keys.onMenuPressed:   { homeScreen.openPowerClientOnly(); event.accepted = true }
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

    // ── Power-off chooser (host / client / both) ──────────────────────────────
    PowerDialog {
        id: powerDialog
        onConfirmed: function(target, installUpdates) {
            if (target === "host") {
                homeScreen.computerModel.shutdownHost(powerDialog.pcIndex, installUpdates)
            } else if (target === "client") {
                SystemProperties.shutdownClient(installUpdates)
            } else if (target === "both") {
                // Send the host shutdown first, then power off the client after a
                // short delay so the bridge socket finishes writing the command.
                homeScreen.computerModel.shutdownHost(powerDialog.pcIndex, installUpdates)
                bothShutdownTimer.installUpdates = installUpdates
                bothShutdownTimer.restart()
            }
        }
    }

    // Receives the host's update state (async) and resolves the Power dialog's host row.
    Connections {
        target: computerModel
        function onUpdateStateReceived(idx, pending) {
            if (idx === powerDialog.pcIndex)
                powerDialog.hostUpdateState = pending ? "pending" : "none"
        }
    }

    Timer {
        id: bothShutdownTimer
        property bool installUpdates: false
        interval: 1800
        repeat: false
        onTriggered: SystemProperties.shutdownClient(bothShutdownTimer.installUpdates)
    }

    // ── Remote "Update host" — dialog, poll timer, progress wiring ─────────────
    UpdateDialog {
        id: updateDialog
        hostName:  homeScreen.updateJobHostName
        phase:     homeScreen.updateJobPhase
        percent:   homeScreen.updateJobPercent
        message:   homeScreen.updateJobMessage
        errorText: homeScreen.updateJobError
        updates:   homeScreen.updateJobUpdates
        counts:    homeScreen.updateJobCounts
        onInstall: function(scope) { homeScreen.startUpdateInstall(scope) }
        onHideRequested: updateDialog.close()        // background: job + chip stay alive
        onDismissed:     homeScreen.clearUpdateJob()
    }

    Timer {
        id: updatePollTimer
        interval: 1500
        repeat: true
        onTriggered: if (homeScreen.updateJobActive)
                         homeScreen.computerModel.requestUpdateProgress(homeScreen.updateJobHostIndex)
    }

    Connections {
        target: computerModel
        function onUpdateProgressReceived(idx, state) {
            if (idx !== homeScreen.updateJobHostIndex || !homeScreen.updateJobActive)
                return
            var phase = state.phase ? state.phase : "IDLE"
            if (phase === "IDLE") {
                // Host unreachable. If the install had started, the box is rebooting
                // (success); otherwise tolerate a few misses before declaring it lost.
                if (homeScreen._updateInstallStarted) {
                    homeScreen.updateJobPhase = "REBOOTING"
                    homeScreen.updateJobPercent = 100
                    homeScreen.updateJobMessage = qsTr("Host is restarting to finish updates.")
                    updatePollTimer.stop()
                } else if (++homeScreen._updateMisses >= 4) {
                    homeScreen.updateJobPhase = "ERROR"
                    homeScreen.updateJobMessage = qsTr("Lost contact with the host.")
                    updatePollTimer.stop()
                }
                return
            }
            homeScreen._updateMisses = 0
            if (phase === "DOWNLOADING" || phase === "INSTALLING")
                homeScreen._updateInstallStarted = true
            homeScreen.updateJobPhase   = phase
            homeScreen.updateJobPercent = (state.percent !== undefined) ? state.percent : -1
            homeScreen.updateJobMessage = state.message ? state.message : ""
            homeScreen.updateJobError   = state.error ? state.error : ""
            if (state.updates !== undefined) homeScreen.updateJobUpdates = state.updates
            if (state.counts  !== undefined) homeScreen.updateJobCounts  = state.counts
            // Terminal / restarting → stop polling; the view stays until the user closes it.
            if (phase === "DONE" || phase === "NO_UPDATES" || phase === "ERROR" || phase === "REBOOTING")
                updatePollTimer.stop()
        }
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
