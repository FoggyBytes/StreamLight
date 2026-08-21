import Theme 1.0
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

    /*
     * The window scale, published for everything that cannot measure the window itself.
     *
     * The pages each compute this from their own width; the dialogs could not, because a
     * dialog lives in the overlay above the pages rather than inside one, and so every
     * dialog in the app was written in fixed pixels and came out around half the size of
     * the page behind it on a large screen. The shell is the one thing that is always the
     * size of the window, so it is where the number comes from.
     *
     * ⚠️ Same divisor as AppsScreen and HostStage. If it changes there it changes here, or
     * a dialog is drawn to a different scale than the page it is covering — which is the
     * whole defect this exists to close.
     */
    onWidthChanged: Theme.uiScale = width / 1330
    Component.onCompleted: Theme.uiScale = width / 1330

    // Design tokens (mirrored from main.qml — id scopes are per-document).
    readonly property color _bg1:      "#151515"
    readonly property color _border:   "#2a2a2a"
    readonly property color _borderS:  "#404040"
    readonly property color _bgHov:    "#262626"
    readonly property color _bg2:      "#1a1a1a"
    readonly property color _text:     "#f0f0f0"
    readonly property color _textDim:  "#a0a0a0"
    readonly property string _version: "5.1.3"
    readonly property string _mono:    "DM Sans"

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

    // What the status bar sits on. Transparent by default — see the note on statusBar.color.
    property color  statusBarFloor: "transparent"

    // ── Remote "Update host" job (state owned by HomeScreen; mirrored for the
    //    global status-bar chip + Select reopen shortcut) ─────────────────────────
    readonly property bool   _updateActive:  homeLoader.item ? homeLoader.item.updateJobActive  : false
    readonly property string _updateHost:    homeLoader.item ? homeLoader.item.updateJobHostName : ""
    readonly property string _updatePhase:   homeLoader.item ? homeLoader.item.updateJobPhase    : "IDLE"

    // ── Host link restore watch (state owned by HomeScreen, mirrored for the chip) ──
    // The host-link state is read by whoever is showing that host — the card on Home, the
    // header on the host page — so the shell no longer mirrors it into the status bar.
    // Read by the host page's header, for the case where the user answers the prompt and then
    // walks straight into the host. The card on Home says it too, from its own state.
    readonly property bool   _linkRestoreActive: homeLoader.item ? homeLoader.item.linkRestoreVisible : false
    readonly property bool   _linkRestoreDone:   homeLoader.item ? homeLoader.item.linkRestoreDone   : false

    // For the host page's own header: the host currently selected on Home is the one being
    // browsed, so its record already carries what the header needs.
    readonly property bool _hostLinkChanging:
        (homeLoader.item && homeLoader.item.currentHost
         && homeLoader.item.currentHost.linkChanging === true) || false

    // Called from the Apps screen after any end of a stream, to remember that this host may
    // have a link to put back. Nothing happens now: the host holds the speed, and the question
    // is asked on the way back to the host list.
    function noteStreamEnded(idx, hostName) {
        if (homeLoader.item) homeLoader.item.noteStreamEnded(idx, hostName)
    }

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
    Keys.onEscapePressed: function(event) { event.accepted = _goBack() }
    Keys.onBackPressed:   function(event) { event.accepted = _goBack() }

    // On Home, two pairs that must not be confused — the legends on the card name both:
    //
    //   host    LT/RT  (Key_F14/F15)   ·  keyboard PgUp/PgDn
    //   profile LB/RB  (Key_F16/F17)   ·  keyboard Q/E, handled in HomeScreen
    //
    // The shoulders carry their own inert keys precisely so they cannot land in the host
    // handler the way they did when they still sent PageUp/PageDown — see the note in
    // sdlgamepadkeynavigation.cpp. Select (Key_F13) reopens the running "Update host" view
    // when a host update job is active.
    Keys.onPressed: function(event) {
        if (currentPage !== 0)
            return
        if (event.key === Qt.Key_F13) {
            if (appShell._updateActive) {
                appShell.reopenUpdateDialog()
                event.accepted = true
            }
        } else if (event.key === Qt.Key_PageDown) {
            if (homeLoader.item) { homeLoader.item.cycleHostTab(1);  event.accepted = true }
        } else if (event.key === Qt.Key_PageUp) {
            if (homeLoader.item) { homeLoader.item.cycleHostTab(-1); event.accepted = true }
        } else if (event.key === Qt.Key_F17) {
            if (homeLoader.item) { homeLoader.item.cycleFocusedProfile(1);  event.accepted = true }
        } else if (event.key === Qt.Key_F16) {
            if (homeLoader.item) { homeLoader.item.cycleFocusedProfile(-1); event.accepted = true }
        }
    }

    function showHome() {
        currentPage = 0
    }

    // Ctrl+N. main.qml looks for this on stackView.currentItem, which is this shell — so the
    // shortcut has been reaching nothing since the shell was introduced, because the function
    // it wants has always lived on HomeScreen. Forwarding it costs four lines and makes an
    // advertised shortcut work again.
    function openAddPc() {
        currentPage = 0
        if (homeLoader.item && homeLoader.item.openAddPc) homeLoader.item.openAddPc()
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

    // Active-profile context passed into Settings: when Settings is opened from a
    // host's app view and that host has an active profile, the rows the profile
    // overrides are shown greyed + disabled (changing them wouldn't affect that
    // host). Empty when opened from Home (no host context).
    property var    _settingsProfileOverride: ({})
    property string _settingsProfileName: ""

    // Also called by main.qml Keys.onMenuPressed.
    function openSettings() {
        if (currentPage !== 2) {
            _settingsReturnPage = currentPage
            // Determine the host context: the app-view host, or (from Home) the
            // highlighted host card. Its active profile drives the greyed rows.
            var mdl = null, idx = -1
            if (currentPage === 1 && _appsModel && _appsIdx >= 0) {
                mdl = _appsModel; idx = _appsIdx
            } else if (currentPage === 0 && homeLoader.item
                       && homeLoader.item.computerModel
                       && homeLoader.item.currentHostIndex >= 0) {
                mdl = homeLoader.item.computerModel
                idx = homeLoader.item.currentHostIndex
            }
            if (mdl && idx >= 0) {
                _settingsProfileOverride = mdl.hostActiveOverride(idx)
                _settingsProfileName     = mdl.hostActiveProfileName(idx)
            } else {
                _settingsProfileOverride = ({})
                _settingsProfileName     = ""
            }
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

    // Which page we came from. The link-restore prompt is only ever offered on the way back
    // from a host page — that is the trip that follows a session, and asking anywhere else
    // (landing on Home at startup, stepping out of Settings) would be asking out of nowhere.
    property int _prevPage: 0

    onCurrentPageChanged: {
        var cameFromApps = (_prevPage === 1)
        _prevPage = currentPage

        if      (currentPage === 0 && homeLoader.item)     homeLoader.item.forceActiveFocus()
        else if (currentPage === 1 && appsLoader.item)     appsLoader.item.forceActiveFocus()
        else if (currentPage === 2 && settingsLoader.item) settingsLoader.item.forceActiveFocus()

        // Back on the host list: drop any "force Tailscale" session pin, and ask the host for
        // its last session again — the most likely reason we are landing here is that one just
        // ended, and the card would otherwise still be describing the one before it.
        if (currentPage === 0 && homeLoader.item) {
            if (homeLoader.item.clearTailscalePreferences)
                homeLoader.item.clearTailscalePreferences()
            if (homeLoader.item.refreshLastSession)
                homeLoader.item.refreshLastSession()
            if (cameFromApps && homeLoader.item.maybeAskLinkRestore)
                homeLoader.item.maybeAskLinkRestore()
        }
    }

    // Ambient vertical gradient — charcoal top → accent-washed bottom.
    //
    // It runs the FULL height, behind the status bar as well. Stopping it at the bar's top
    // edge is what made the bar read as a separate slab: the wash faded up to a line and then
    // a flat dark rectangle started, and the seam was the most visible edge on the screen.
    // The bar itself is transparent and carries no rule, so the floor of the app is one
    // uninterrupted surface from the content down to the button prompts.
    // The wash lives in AmbientBackground now, so the screens that cover this one whole — the
    // quit screen, the PIN pad — can stand on exactly the same ground instead of a copy of it.
    AmbientBackground {
        id: ambientBackground
        z: -1
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
                item.activeProfileOverride = appShell._settingsProfileOverride
                item.activeProfileName     = appShell._settingsProfileName
                item.forceActiveFocus()
            }
        }
    }

    /*
     * Clock, date and battery — the top counterpart of the status bar below, and it lives
     * here for the same reason that one does.
     *
     * ⚠️ It used to be declared on each of the three pages, and each page anchored it to
     * whatever its own header happened to be: the brand icon on Home, the header's centre on
     * the host page (in `_px` units, so it also moved with the window size), the title on
     * Settings — with right margins of 44, `_px(44)` and 30. Three coordinates for one
     * object, so it hopped a few pixels on every page change. Declared once in the shell it
     * cannot drift again, whatever the pages do to their headers.
     *
     * Fixed pixels, not the pages' `_u` scale: this is chrome, like the status bar's own
     * 44px height and 16px gutter, and it belongs to the window rather than to the content.
     * The margins put its centre on the wordmark's, which is the one header it has to agree
     * with — the other two are close enough that no one reading them will see a difference.
     *
     * Declared after contentArea so it draws over the pages; popups have their own overlay
     * layer above both.
     */
    StatusCluster {
        anchors.top: parent.top
        anchors.topMargin: 22
        anchors.right: parent.right
        anchors.rightMargin: 44
    }

    // Status bar — gamepad prompts + version. Glyphs swap by controller type.
    Rectangle {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 44
        // Normally transparent, so the page's own ambient gradient runs behind it unbroken.
        //
        // A page that paints its own floor has to say so, though: the host page ends in
        // near-black under its blurred artwork while the shell's gradient ends accent-tinted,
        // and where the two met there was a visible step across the foot of the screen. So
        // the page sets this to whatever it ends in and the bar borrows it.
        color: appShell.statusBarFloor

        // No rule along the top. There was one, and once the ambient gradient ran the full
        // height behind it the line was the only thing left drawing a border where there is
        // no longer an edge — the prompts sit on the same surface as everything above them,
        // and the row of glyphs is enough to say where they live.

        readonly property bool _padIsPs: SdlGamepadKeyNavigation.controllerType === "ps"
        readonly property bool _padIsSwitch: SdlGamepadKeyNavigation.controllerType === "switch"

        // Glyphs are chosen by SDL button POSITION. Nintendo swaps A/B and X/Y
        // relative to Xbox, so the Switch glyph for the south button (_iconA)
        // is the one labeled "B", the east button (_iconB) is labeled "A", etc.
        readonly property string _iconA: _padIsPs ? "qrc:/res/pad_ps_cross.svg"    : _padIsSwitch ? "qrc:/res/pad_switch_b.svg" : "qrc:/res/pad_xbox_a.svg"
        readonly property string _iconB: _padIsPs ? "qrc:/res/pad_ps_circle.svg"   : _padIsSwitch ? "qrc:/res/pad_switch_a.svg" : "qrc:/res/pad_xbox_b.svg"
        readonly property string _iconX: _padIsPs ? "qrc:/res/pad_ps_square.svg"   : _padIsSwitch ? "qrc:/res/pad_switch_y.svg" : "qrc:/res/pad_xbox_x.svg"
        readonly property string _iconY: _padIsPs ? "qrc:/res/pad_ps_triangle.svg" : _padIsSwitch ? "qrc:/res/pad_switch_x.svg" : "qrc:/res/pad_xbox_y.svg"
        readonly property string _iconL: _padIsPs ? "qrc:/res/pad_ps_l1.svg"       : _padIsSwitch ? "qrc:/res/pad_switch_l.svg" : "qrc:/res/pad_xbox_lb.svg"
        readonly property string _iconR: _padIsPs ? "qrc:/res/pad_ps_r1.svg"       : _padIsSwitch ? "qrc:/res/pad_switch_r.svg" : "qrc:/res/pad_xbox_rb.svg"
        // Select / Back / View / Create / − button.
        readonly property string _iconSelect: _padIsPs ? "qrc:/res/pad_ps_create.svg" : _padIsSwitch ? "qrc:/res/pad_switch_minus.svg" : "qrc:/res/pad_xbox_view.svg"
        // (The trigger glyphs used to be resolved here too, for the "Prev/Next host" prompts.
        //  Those moved onto the host strip itself, which resolves its own — see HomeScreen.)

        /*
         * A is absent: its glyph is drawn on the stage's focused button, where the word for
         * what it will do sits next to it. The bar cannot follow the focus and name the
         * target at the same time, so saying it twice is how the two came to disagree.
         *
         * The triggers and the shoulders are absent for a different reason — they were moved
         * to where what they move actually is: LT/RT to the end of the host strip, LB/RB onto
         * the host card beside its buttons. A legend attached to the thing it controls costs
         * nothing to find; the same legend in a row at the foot of the screen has to be
         * connected back to it.
         *
         * X stays here. It is the one shortcut that works from anywhere on this screen and
         * belongs to no single control, which is exactly what this row is for — and keeping
         * the destructive one in the same place on every screen is worth more than symmetry.
         */
        // btn = the controller button, key = the keyboard equivalent. Both travel together and
        // ActionHint picks; a prompt with no key stays on the glyph.
        readonly property var _hintsHome: [
            { btn: "X", key: "P",   act: qsTr("Shutdown"), kind: "shutdownHost" },
            { btn: "Y", key: "S",   act: qsTr("Settings"), kind: "settings" },
            { btn: "B", key: "Esc", act: qsTr("Exit"),     kind: "back" }
        ]
        // Same as Home, and for the same reason: A, X and Select are drawn on the host
        // page's own buttons, next to the words for what they do, so the bar does not say it
        // a second time. A prompt row that repeats what a button already carries is the
        // arrangement that let the two disagree.
        //
        // What is left is what has no button to sit on: Y opens a screen that is not on this
        // page, and B leaves it. "Hosts", not "Back" — B always lands in the same place from
        // here, and naming the destination is the one thing the removed corner button did
        // that the bar could not. "Back" says you are leaving, "Hosts" says where you arrive.
        readonly property var _hintsApps: [
            { btn: "Y", key: "S",   act: qsTr("Settings"), kind: "settings" },
            { btn: "B", key: "Esc", act: qsTr("Hosts"),    kind: "back" }
        ]
        // Settings prompts add "X · Default" when the bitrate differs from recommended.
        readonly property bool _showDefaultHint:
            currentPage === 2
            && settingsLoader.item
            && settingsLoader.item.bitrateNonDefault === true
        property var _hintsSettings: {
            var base = [
                { btn: "A",  key: "Enter", act: qsTr("Change"),   kind: "change" },
                { btn: "LB", key: "PgUp",  act: qsTr("Prev tab"), kind: "prevTab" },
                { btn: "RB", key: "PgDn",  act: qsTr("Next tab"), kind: "nextTab" },
                { btn: "B",  key: "Esc",   act: qsTr("Back"),     kind: "back" }
            ]
            if (statusBar._showDefaultHint) {
                base.splice(1, 0, { btn: "X", key: "D", act: qsTr("Default"), kind: "defaultBitrate" })
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

                        // ABXY circle 26×26, LB/RB rounded rect 40×24 — the sizes ActionHint
                        // and PadGlyph now share, so a prompt here and a combo in Settings are
                        // the same button at the same size.
                        ActionHint {
                            anchors.verticalCenter: parent.verticalCenter
                            buttonKey: modelData.btn
                            keyLabel:  modelData.key
                            size: 26
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

            // (The host-link chip used to live here. It was in the status bar because there was
            //  nowhere else to put it; now the host card says it on Home and the header says it
            //  on the host page — both attached to the host they are talking about, instead of
            //  a line in the corner that had to name it.)

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
                        source: statusBar._iconSelect
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
                        // (A mini progress bar and a percentage used to sit here. The figure
                        //  behind them only moves between files and stands still through the
                        //  whole of a single-update download — see the note in UpdateDialog.
                        //  The phase word above is what the chip is for, and it does change.)
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
