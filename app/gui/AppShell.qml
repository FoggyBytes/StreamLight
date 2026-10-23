import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.15

import SdlGamepadKeyNavigation 1.0
import InputHints 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import AppUpdate 1.0

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
    // ⚠️ 8.0.0 (#43): the height counts too. By width alone a 3840 × 1200 screen scaled to the
    // ceiling while its height held about 1.53 of it, and the host card spilled over itself. 748
    // is the height of a 16:9 window 1330 wide; on 16:9, 16:10 and 21:9 nothing changes.
    function _fit() {
        Theme.heightFit = height / 748
        Theme.uiScale = Math.min(width / 1330, height / 748)
    }
    onWidthChanged: _fit()
    onHeightChanged: _fit()
    Component.onCompleted: {
        _fit()
        // The opening animation (6.0.0) — decided here, once. See StartupSplash.
        _splashOnLaunch = Theme.startupAnimation && !Theme.reduceAnimations
                          && !(typeof initialView !== "undefined" && initialView && initialView.length > 0)
        if (_splashOnLaunch)
            startupSplash.start()
        // The release lookup behind the startup update prompt (see _maybePromptUpdate at the
        // bottom). Settings runs the same lookup again when it opens.
        AppUpdate.checkLatest()
    }

    // (Seven local colour tokens used to sit here, "mirrored from main.qml". Five of them —
    //  _bg1, _bg2, _border, _borderS, _bgHov — were never read by anything in this file, and
    //  the two that were are Theme.text and Theme.text2 under other names. The mirror is what
    //  let this shell drift a shade away from the pages it frames, so there is no mirror now.)

    // ⚠️ Read from the binary, never written here. This was a literal from 4.3.0 to
    // 5.2.0 and was bumped by hand at every release — until 5.2.1, where the bump was
    // missed and the app spent a release telling users it was the previous version.
    // SystemProperties.versionString is VERSION_STR, which qmake takes from
    // app/version.txt, so the label and the installer can no longer disagree.
    readonly property string _version: SystemProperties.versionString

    // Set once in Component.onCompleted and never again: it is "this launch opens with the
    // splash", not a mirror of the setting: turning the setting on in Settings changes the
    // next launch, never this one.
    property bool _splashOnLaunch: false

    // 0 = Home, 1 = Apps, 2 = Settings, 3 = the host section (8.0.0)
    property int currentPage: 0

    // 8.0.0: the host section — StreamTweak's mark on the host card. The host by uuid, its row
    // read back when needed, for the same reason as _appsUuid below.
    property string _hsUuid:  ""
    property var    _hsModel: null
    property string _hsName:  ""
    readonly property int _hsIdx: _rowOf(_hsModel, _hsUuid)

    function openHostSection(uuid, computerModel, hostName) {
        _hsUuid  = uuid
        _hsModel = computerModel
        _hsName  = hostName || ""
        currentPage = 3
    }

    // Passed from HomeScreen when navigating to Apps. The host by uuid, its row read back when
    // needed: the page can stay up for a whole evening, and the host list moves under it
    // whenever a host is renamed, found or removed (6.3.1, see HomeScreen.hostIndexOf()).
    property string _appsUuid:    ""
    readonly property int _appsIdx: _rowOf(_appsModel, _appsUuid)
    property var    _appsModel:   null
    property bool   _appsShowAll: false
    property string _appsHostName:    ""
    property string _appsHostAddress: ""
    property string _appsHostGpu:     ""
    property bool   _appsIsTailscaleClone: false

    // Where to return when leaving Settings (Home or Apps).
    property int    _settingsReturnPage: 0

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

    // A new launch from the host page asks first whether the host is locked, and puts the PIN
    // pad in front of it if so (7.0.0, #35). The unlock lives on HomeScreen with the wake's.
    /*
     * The host page's host went to its lock screen (7.0.0, #35): back to Home, where its card
     * says Unlock. Checked when the lock state changes, when a stream or the PIN pad comes off
     * the stack and when the page comes back from Settings — whichever happens last.
     *
     * ⚠️ Never with anything on top of the shell, and never while an unlock is under way: a
     * launch from this page that found the host locked shows the pad ABOVE the page, and then
     * launches from it — leaving the page in between would drop that launch
     * (AppsScreen.Component.onDestruction → dropLaunchWhenUnlocked).
     */
    function _leaveLockedHostPage() {
        if (currentPage !== 1 || !homeLoader.item) return
        if (stackView.currentItem !== appShell) return
        if (homeLoader.item.unlockBusy) return
        if (!homeLoader.item.isHostLocked(_appsUuid)) return
        console.log("[unlock] the host page's host is locked — back to Home")
        currentPage = 0
    }
    Connections {
        target: homeLoader.item
        ignoreUnknownSignals: true
        function onHostLockChanged(uuid, locked) {
            if (locked && uuid === appShell._appsUuid) appShell._leaveLockedHostPage()
        }
        function onUnlockBusyChanged() { appShell._leaveLockedHostPage() }
    }
    Connections {
        target: stackView
        function onCurrentItemChanged() { Qt.callLater(appShell._leaveLockedHostPage) }
    }

    // 7.0.2: after a Quit the host reported done, ask StreamTweak a few seconds later whether
    // the game really went (ComputerModel.checkGameAfterQuit); the answer lands below.
    function checkGameAfterQuit(hostUuid, appName) {
        var mdl = homeLoader.item ? homeLoader.item.computerModel : null
        if (mdl && hostUuid) mdl.checkGameAfterQuit(hostUuid, appName)
    }
    Connections {
        target: homeLoader.item ? homeLoader.item.computerModel : null
        ignoreUnknownSignals: true
        function onGameStillRunningAfterQuit(hostUuid, gameName) {
            appShell.notice(qsTr("%1 is still running on the host").arg(gameName), 6000)
        }
    }

    function launchWhenUnlocked(idx, hostName, launchFn, owner) {
        if (homeLoader.item) homeLoader.item.launchWhenUnlocked(idx, hostName, launchFn, owner)
        else if (launchFn)   launchFn()
    }
    function dropLaunchWhenUnlocked(owner) {
        if (homeLoader.item) homeLoader.item.dropLaunchWhenUnlocked(owner)
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
        if (currentPage === 3) {
            currentPage = 0
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
    //   host    LB/RB  (Key_F16/F17)   ·  keyboard PgUp/PgDn
    //   profile LT/RT  (Key_F14/F15)   ·  keyboard Q/E, handled in HomeScreen
    //
    // ⚠️ Swapped on 13/10/2026 (7.0.0), to match the host page, where LB/RB move between tabs
    // and LT/RT change the profile: the profile is on the triggers on both screens now. Until
    // then Home had the hosts on the triggers and the profile on the shoulders.
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
            if (homeLoader.item) { homeLoader.item.cycleHostTab(1);  event.accepted = true }
        } else if (event.key === Qt.Key_F16) {
            if (homeLoader.item) { homeLoader.item.cycleHostTab(-1); event.accepted = true }
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

    // The same rule as HomeScreen.hostIndexOf(): layoutRevision is read so that a binding over
    // this re-runs when the rows move, which indexOfUuid() alone would never cause.
    function _rowOf(model, uuid) {
        if (!model || model.layoutRevision < 0 || !uuid) return -1
        return model.indexOfUuid(uuid)
    }

    function showApps(computerIndex, computerModel, showAll, hostName, hostAddress, hostGpu, isTailscaleClone) {
        // A host at its lock screen has no page to open (7.0.0, #35): its card offers Unlock.
        // Here rather than at each caller, because seven paths lead here — the end of a stream
        // among them — and every one of them lands on Home instead.
        var uuid = computerModel ? computerModel.uuidAt(computerIndex) : ""
        if (homeLoader.item && homeLoader.item.isHostLocked(uuid)) {
            console.log("[unlock] host is locked — staying on Home")
            currentPage = 0
            return
        }
        _appsUuid             = uuid
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

    // The same host context, used by the StreamTweak tab. The model so the tab can list every
    // configured host and switch each one; the name and flag of the highlighted host so the
    // settings that depend on the integration can grey themselves and say which host they
    // mean — the same shape as the profile locks above, and for the same reason: a global row
    // that quietly does nothing is worse than one that says why.
    //
    // Defaults to true so that with no host context nothing greys: "I don't know which host"
    // must never read as "the integration is off".
    property var    _settingsHostModel: null
    property string _settingsHostName: ""
    property string _settingsHostUuid: ""        // by uuid, see _appsUuid
    readonly property int _settingsHostIndex: _rowOf(_settingsHostModel, _settingsHostUuid)
    property bool   _settingsHostStEnabled: true

    // Also called by main.qml on Y (Keys.onHangupPressed) and the Preferences shortcut.
    function openSettings() {
        if (currentPage !== 2) {
            _settingsReturnPage = currentPage
            // Determine the host context: the app-view host, or (from Home) the
            // highlighted host card. Its active profile drives the greyed rows.
            var mdl = null, idx = -1
            if (currentPage === 1 && _appsModel && _appsIdx >= 0) {
                mdl = _appsModel; idx = _appsIdx
            } else if (currentPage === 3 && _hsModel && _hsIdx >= 0) {
                // From the host section: that host, like from its host page.
                mdl = _hsModel; idx = _hsIdx
            } else if (currentPage === 0 && homeLoader.item
                       && homeLoader.item.computerModel
                       && homeLoader.item.currentHostIndex >= 0) {
                mdl = homeLoader.item.computerModel
                idx = homeLoader.item.currentHostIndex
            }
            if (mdl && idx >= 0) {
                _settingsProfileOverride = mdl.hostActiveOverride(idx)
                _settingsProfileName     = mdl.hostActiveProfileName(idx)
                _settingsHostStEnabled   = mdl.streamTweakEnabled(idx)
                _settingsHostUuid        = mdl.uuidAt(idx)
                _settingsHostName        = currentPage === 1 ? _appsHostName
                    : currentPage === 3 ? _hsName
                    : (homeLoader.item.currentHost ? homeLoader.item.currentHost.name : "")
            } else {
                _settingsProfileOverride = ({})
                _settingsProfileName     = ""
                _settingsHostStEnabled   = true
                _settingsHostUuid        = ""
                _settingsHostName        = ""
            }

            // The whole model, not just the highlighted host: the StreamTweak tab is a list
            // of every configured host. Resolved from Home whenever it exists, because that
            // is the one model that holds them all.
            _settingsHostModel = (homeLoader.item && homeLoader.item.computerModel)
                                 ? homeLoader.item.computerModel : mdl
        }
        currentPage = 2
    }

    /*
     * A panel that takes over the status bar (7.0.0). A popup with no scrim — the Background
     * panel, the accent palette — leaves this bar in plain sight, and the bar goes on saying what
     * the page behind would do: X = Power, B = Exit, while in the panel X clears and B cancels.
     * A footer of the panel's own beside it only made two rows of prompts that seemed to be two
     * sets of controls. So the panel lends its prompts to the bar instead, for as long as it is
     * open, and gets them back on click.
     *
     * The owner is any object with `barHints` (rows shaped like the bar's own) and
     * `barAction(kind)`. Its prompts are drawn by barTakeover, a popup over the bar, because
     * the bar sits under the overlay: a click on it would land on the open panel's "outside"
     * and close it. Opening or closing the owner calls these two.
     */
    readonly property bool _scrollHint: SdlGamepadKeyNavigation.scrollAvailable && InputHints.padActive
    property var _barOwner: null
    function takeOverBar(owner) {
        _barOwner = owner
        barTakeover.open()
    }
    function releaseBar(owner) {
        if (_barOwner !== owner) return
        _barOwner = null
        barTakeover.close()
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
        case "theme":
            appShell.toggleTheme()
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
        case "move":
            if (appsLoader.item && appsLoader.item.moveFocused) appsLoader.item.moveFocused()
            break
        case "pin":
            if (appsLoader.item && appsLoader.item.togglePinFocused) appsLoader.item.togglePinFocused()
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
        default:
            // "section:<action>": a prompt the StreamTweak section lent the bar (its Sessions tab).
            if (kind.indexOf("section:") === 0 && hostSectionLoader.item && hostSectionLoader.item.barAction)
                hostSectionLoader.item.barAction(kind.substring(8))
            break
        }
    }

    // Which page we came from. The link-restore prompt is only ever offered on the way back
    // from a host page — that is the trip that follows a session, and asking anywhere else
    // (landing on Home at startup, stepping out of Settings) would be asking out of nowhere.
    property int _prevPage: 0

    /*
     * Page transitions (7.0.0). Two kinds, picked by the pair of pages:
     *   zoom  Home ↔ host page — the host page grows out of the card's side (left) while Home
     *         fades back, and shrinks back into it on the way out;
     *   push  anything ↔ Settings — Settings slides in from the right while the page behind
     *         moves a third of the way left and dims (iOS), and the reverse on B.
     * The page leaving stays loaded until the motion ends (_outgoing), then goes as before.
     * Off with Reduce animations, and never while the opening animation holds the screen.
     */
    property int  _outgoing: -1
    property real _t: 1
    // Settings and the host section both slide in from the right (8.0.0: the section is a page
    // of settings for the host, and it is entered and left like one).
    function _isPush(n) { return n === 2 || n === 3 }

    // Settings and the StreamTweak section draw their text a size smaller, in one proportion
    // (21/10/2026). Held through the page's own slide, so it does not change size on the way out.
    Binding {
        target: Theme
        property: "textScale"
        value: (appShell._isPush(appShell.currentPage) || appShell._isPush(appShell._outgoing)) ? 0.88 : 1.0
    }
    readonly property bool _push: _isPush(currentPage) || _isPush(_outgoing)
    NumberAnimation {
        id: pageAnim
        target: appShell; property: "_t"
        from: 0; to: 1
        // Quicker since 09/10/2026 (was 380 / 340): moving around should not wait on the motion.
        duration: appShell._push ? 260 : 240
        easing.type: Easing.OutCubic
        onFinished: appShell._outgoing = -1
    }
    function _pageOpacity(n) {
        if (_outgoing < 0) return 1
        if (n === currentPage) return _push ? (_isPush(n) ? 1 : 0.5 + 0.5 * _t) : _t
        if (n === _outgoing)   return _push ? (_isPush(n) ? 1 : 1 - 0.5 * _t) : 1 - _t
        return 1
    }
    function _pageShift(n) {
        if (_outgoing < 0 || !_push) return 0
        var w = contentArea.width
        if (n === currentPage) return _isPush(n) ? (1 - _t) * w : -(1 - _t) * 0.3 * w
        if (n === _outgoing)   return _isPush(n) ? _t * w : -_t * 0.3 * w
        return 0
    }
    function _pageScale(n) {
        if (_outgoing < 0 || _push) return 1
        if (n === currentPage) return n === 1 ? 0.94 + 0.06 * _t : 1.04 - 0.04 * _t
        if (n === _outgoing)   return n === 1 ? 1 - 0.06 * _t : 1 + 0.04 * _t
        return 1
    }

    onCurrentPageChanged: {
        Qt.callLater(_leaveLockedHostPage)      // back from Settings onto a locked host (#35)
        var cameFromApps = (_prevPage === 1)
        if (!Theme.reduceAnimations && !startupSplash.blocking && _prevPage !== currentPage) {
            _outgoing = _prevPage
            _t = 0
            pageAnim.restart()
        }
        _prevPage = currentPage

        if      (currentPage === 0 && homeLoader.item)     homeLoader.item.forceActiveFocus()
        else if (currentPage === 1 && appsLoader.item)     appsLoader.item.forceActiveFocus()
        else if (currentPage === 2 && settingsLoader.item) settingsLoader.item.forceActiveFocus()
        else if (currentPage === 3 && hostSectionLoader.item) hostSectionLoader.item.forceActiveFocus()

        // Back on the host list: drop any "force Tailscale" session pin, and ask the host for
        // its last session again — the most likely reason we are landing here is that one just
        // ended, and the card would otherwise still be describing the one before it.
        if (currentPage === 0 && homeLoader.item) {
            if (homeLoader.item.clearTailscalePreferences)
                homeLoader.item.clearTailscalePreferences()
            if (homeLoader.item.refreshLastPlayed)
                homeLoader.item.refreshLastPlayed()
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
        // Waves on Home, Settings (6.1.0) and, since 7.0.0, the host page — unless the user chose
        // its 6.5.1 background, the focused game's cover (13/10/2026), and it is drawing.
        waves: currentPage === 0 || currentPage === 2 || currentPage === 3
               || (currentPage === 1 && !appShell._appsCover)
        // The waves run twice as fast while a host is streaming (6.0.0).
        streaming: homeLoader.item ? homeLoader.item.anyStreaming : false
        // The waves rise from the bottom only as the first act of the opening animation, on its
        // clock. With it off (splash never running) this is 1: Home opens on waves in place.
        rise: startupSplash.rise
    }

    FocusScope {
        id: contentArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: statusBar.top
        focus: true
        // Invisible and deaf while the opening animation runs; see StartupSplash.
        opacity: startupSplash.homeOpacity
        enabled: !startupSplash.blocking

        // Home stays always-active so ComputerModel survives Apps/Settings.
        Loader {
            id: homeLoader
            anchors.fill: parent
            active: true
            visible: currentPage === 0 || appShell._outgoing === 0
            focus: currentPage === 0
            opacity: appShell._pageOpacity(0)
            scale: appShell._pageScale(0)
            transform: Translate { x: appShell._pageShift(0) }
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
            active: currentPage === 1 || appShell._outgoing === 1
            visible: currentPage === 1 || appShell._outgoing === 1
            focus: currentPage === 1
            opacity: appShell._pageOpacity(1)
            // Grows out of the card, which stands on the left of Home.
            transformOrigin: Item.Left
            scale: appShell._pageScale(1)
            transform: Translate { x: appShell._pageShift(1) }
            source: "qrc:/gui/AppsScreen.qml"
            onLoaded: {
                // The model before the host: the page builds its library when it learns the
                // host, and must have the model to find its row by then.
                item.hostComputerModel = appShell._appsModel
                item.hostUuid          = appShell._appsUuid
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
            active: currentPage === 2 || appShell._outgoing === 2
            visible: currentPage === 2 || appShell._outgoing === 2
            focus: currentPage === 2
            opacity: appShell._pageOpacity(2)
            transform: Translate { x: appShell._pageShift(2) }
            source: "qrc:/gui/SettingsScreen.qml"
            onLoaded: {
                item.activeProfileOverride = appShell._settingsProfileOverride
                item.activeProfileName     = appShell._settingsProfileName
                item.hostModel             = appShell._settingsHostModel
                item.hostName              = appShell._settingsHostName
                item.hostIndex             = Qt.binding(function() { return appShell._settingsHostIndex })
                item.hostStreamTweakEnabled = appShell._settingsHostStEnabled
                item.appShell              = appShell
                item.forceActiveFocus()
            }
        }

        // 8.0.0: the host section.
        Loader {
            id: hostSectionLoader
            anchors.fill: parent
            active: currentPage === 3 || appShell._outgoing === 3
            visible: currentPage === 3 || appShell._outgoing === 3
            focus: currentPage === 3
            opacity: appShell._pageOpacity(3)
            transform: Translate { x: appShell._pageShift(3) }
            source: "qrc:/gui/HostSectionScreen.qml"
            onLoaded: {
                item.computerModel = appShell._hsModel
                item.hostUuid      = appShell._hsUuid
                item.hostName      = appShell._hsName
                item.home          = homeLoader.item
                item.appShell      = appShell
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
     * ⚠️ It used to be in FIXED pixels, on the argument that chrome belongs to the window
     * rather than to the content. That argument holds on a desktop and fails on a handheld:
     * the content scales to 1.60 while this stayed at 1.00, so on the Ally the clock and the
     * battery were drawn at roughly two thirds the size of everything they sat beside — the
     * one place on the screen where you have to squint, on the device most likely to be at
     * arm's length.
     *
     * Scaled as a whole rather than number by number: this is a small illustration with a
     * battery drawn in tenths of a pixel (2.1, 5.2, 1.3), and rounding each of those
     * independently is how a drawing stops lining up with itself. TopRight origin so it
     * grows down and inward from the corner it is anchored to, which keeps the margin the
     * margin at every scale.
     *
     * Declared after contentArea so it draws over the pages; popups have their own overlay
     * layer above both.
     */
    StatusCluster {
        anchors.top: parent.top
        anchors.topMargin: Math.round(22 * Theme.uiScale)
        anchors.right: parent.right
        anchors.rightMargin: Math.round(44 * Theme.uiScale)
        transformOrigin: Item.TopRight
        scale: Theme.uiScale
        opacity: startupSplash.homeOpacity
    }

    // One prompt of the status bar: the button and what it does. Shared by the bar's own row
    // and by barTakeover, so a panel's prompts are the bar's prompts, not a likeness of them.
    component StatusHint: Item {
        id: hint
        property var model: ({})
        signal triggered(string kind)

        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        implicitWidth:  promptRow.implicitWidth + Math.round(8 * Theme.uiScale)
        implicitHeight: Math.round(30 * Theme.uiScale)

        Row {
            id: promptRow
            anchors.centerIn: parent
            spacing: Math.round(9 * Theme.uiScale)

            // ABXY circle 26×26, LB/RB rounded rect 40×24 — the sizes ActionHint
            // and PadGlyph now share, so a prompt here and a combo in Settings are
            // the same button at the same size.
            ActionHint {
                anchors.verticalCenter: parent.verticalCenter
                buttonKey: hint.model.btn
                keyLabel:  hint.model.key
                size: Math.round(26 * Theme.uiScale)
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: hint.model.act
                color: Theme.text
                font.pixelSize: Math.round(Theme.fontBody * Theme.uiScale)
                font.family: Theme.family
            }
        }

        // "none": a prompt that only informs (Scroll — the stick has no click to stand for).
        MouseArea {
            anchors.fill: parent
            enabled: hint.model.kind !== "none"
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: hint.triggered(hint.model.kind)
        }
    }

    // Status bar — gamepad prompts + version. Glyphs swap by controller type.
    Rectangle {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.round(44 * Theme.uiScale)
        // Transparent, so the floor runs behind it unbroken — except under the host page's cover
        // background (Host page background = Game cover), which ends in its own colour: the bar
        // takes it, or there is a step across the foot of the screen (6.5.1's statusBarFloor).
        color: appShell._appsCover ? appsLoader.item.coverFoot : "transparent"
        opacity: startupSplash.homeOpacity
        enabled: !startupSplash.blocking

        // No rule along the top. There was one, and once the ambient gradient ran the full
        // height behind it the line was the only thing left drawing a border where there is
        // no longer an edge — the prompts sit on the same surface as everything above them,
        // and the row of glyphs is enough to say where they live.

        readonly property bool _padIsPs: SdlGamepadKeyNavigation.controllerType === "ps"
        readonly property bool _padIsSwitch: SdlGamepadKeyNavigation.controllerType === "switch"
        readonly property bool _padIsSteam: SdlGamepadKeyNavigation.controllerType === "steam"

        // (Six more glyphs used to be resolved here — A, B, X, Y and the shoulders — and none of
        //  them was read by anything any more: the prompts draw their own. Removed in 6.0.0.
        //  Only Select is still used, by the prompt below.)
        // Select / Back / View / Create / − button.
        readonly property string _iconSelect: _padIsPs ? "qrc:/res/pad_ps_create.svg" : _padIsSwitch ? "qrc:/res/pad_switch_minus.svg" : _padIsSteam ? "qrc:/res/pad_steam_view.svg" : "qrc:/res/pad_xbox_view.svg"
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
        // 7.0.0 (13/10/2026): the theme toggle — right stick click / T — naming the theme it
        // leads to, like every other prompt names what it does.
        readonly property var _hintsHome: [
            { btn: "X",  key: "P",   act: qsTr("Power"),    kind: "shutdownHost" },
            { btn: "Y",  key: "S",   act: qsTr("Settings"), kind: "settings" },
            { btn: "RS", key: "T",   act: Theme.light ? qsTr("Dark theme") : qsTr("Light theme"), kind: "theme" },
            { btn: "B",  key: "Esc", act: qsTr("Exit"),     kind: "back" }
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
        // 5.9.0: LT/RT switched the host page between GAMES and APPS, from here. 6.1.0: the
        // tabs are on LB/RB, drawn at the ends of the strip, and LT/RT on the profile badge.
        // 6.0.0: Start / P pins the selected game. In the bar rather than on the spotlight's
        // buttons, by decision, and only while a game is selected — the APPS tab has nothing to
        // pin, so there the prompt is not drawn at all. The word follows the row: Unpin on a
        // pinned game.
        readonly property var _hintsApps: {
            var h = [
                { btn: "Y",  key: "S",    act: qsTr("Settings"), kind: "settings" },
                { btn: "B",  key: "Esc",  act: qsTr("Hosts"),    kind: "back" }
            ]
            var page = appsLoader.item
            if (page && page.focusedPinnable === true)
                h.push({ btn: "START", key: "P",
                         act: page.focusedPinned === true ? qsTr("Unpin") : qsTr("Pin"),
                         kind: "pin" })
            // 6.1.0: the tabs moved to LB/RB, drawn at the ends of the tab strip, so the bar
            // no longer carries them. It carries the right stick instead, on GAMES and APPS:
            // it moves the selected entry to the other one, and the word says which way.
            if (page && page.focusedMoveLabel)
                h.push({ btn: "RS", key: "M", act: page.focusedMoveLabel, kind: "move" })
            return h
        }
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

        // 8.0.0: the host section — the tabs on the shoulders like Settings, B back to the hosts.
        // Y opens Settings from here too, and B in Settings comes back here — so it is said.
        readonly property var _hintsHostSection: [
            { btn: "A",  key: "Enter", act: qsTr("Change"),   kind: "change" },
            { btn: "LB", key: "PgUp",  act: qsTr("Prev tab"), kind: "prevTab" },
            { btn: "RB", key: "PgDn",  act: qsTr("Next tab"), kind: "nextTab" },
            { btn: "Y",  key: "S",     act: qsTr("Settings"), kind: "settings" },
            { btn: "B",  key: "Esc",   act: qsTr("Hosts"),    kind: "back" }
        ]

        property var _hints:
              currentPage === 0 ? _hintsHome
            : currentPage === 1 ? _hintsApps
            : currentPage === 2 ? _hintsSettings
            // The section's Sessions tab lends its own (they change with where the pad is in it).
            : currentPage === 3 ? (hostSectionLoader.item && hostSectionLoader.item.barHints
                                   ? hostSectionLoader.item.barHints : _hintsHostSection)
            : []

        Row {
            id: hintRow
            anchors.left: parent.left
            anchors.leftMargin: Math.round(20 * Theme.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(20 * Theme.uiScale)
            // Handed to the panel that took the bar over, which draws its own in this place.
            visible: !appShell._barOwner

            Repeater {
                model: statusBar._hints
                delegate: StatusHint {
                    model: modelData
                    onTriggered: function(kind) { appShell.triggerStatusBarAction(kind) }
                }
            }
                // 7.0.0: the right stick scrolls the view that holds the focus, when it has room
                // to — said here only while it would do something, and only to a pad user (the
                // keyboard and mouse have the wheel and need no prompt).
                StatusHint {
                    visible: appShell._scrollHint
                    model: ({ btn: "RSCROLL", key: "", act: qsTr("Scroll"), kind: "none" })
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
            anchors.rightMargin: Math.round(16 * Theme.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(22 * Theme.uiScale)

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
                    spacing: Math.round(10 * Theme.uiScale)

                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        source: statusBar._iconSelect
                        width: Math.round(40 * Theme.uiScale); height: Math.round(24 * Theme.uiScale)
                        sourceSize.width: 80; sourceSize.height: 48
                        fillMode: Image.PreserveAspectFit; smooth: true
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Math.round(3 * Theme.uiScale)
                        Text {
                            width: Math.min(implicitWidth, Math.round(260 * Theme.uiScale))
                            elide: Text.ElideRight
                            text: qsTr("Update")
                                  + (appShell._updateHost.length ? " · " + appShell._updateHost : "")
                                  + "   " + appShell._updatePhaseLabel(appShell._updatePhase)
                            color: Theme.text; font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale); font.family: Theme.family
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
                color: Theme.text2
                font.family: Theme.family
                font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale)
                font.letterSpacing: 1
            }
        }
    }

    // ── Startup update prompt ─────────────────────────────────────────────────
    // Offered once per launch when AppUpdate finds a newer release it can update to and the
    // user has not asked not to be reminded of it (AppUpdate::shouldPrompt). The lookup is
    // started in Component.onCompleted above; the answer arrives as latestChanged.
    //
    // Yes only opens Settings → About, where Update now is — nothing is downloaded from here.
    property bool _updatePrompted: false

    function _maybePromptUpdate() {
        if (_updatePrompted || !AppUpdate.shouldPrompt()) return
        // Not over the opening animation: asked again when it ends (startupSplash.onFinished).
        if (startupSplash.running) return
        // Never over a stream or its launch screen: both are pushed on the global stackView
        // above this shell. Not marked as prompted, so a later lookup (Settings runs one) can
        // still offer it this launch.
        if (typeof stackView !== "undefined" && stackView.depth > 1) return
        _updatePrompted = true
        updatePrompt.latestVersion = AppUpdate.latestStreamLight
        updatePrompt.open()
    }

    Connections {
        target: AppUpdate
        function onLatestChanged() { appShell._maybePromptUpdate() }
    }

    /*
     * The status bar's prompts while a panel has taken it over — see takeOverBar(). Laid exactly
     * over the bar's own row, which hides meanwhile; the version and the update chip on the
     * right stay the bar's.
     *
     * A popup, because that is the only layer above the open panel's "outside": the bar itself
     * is under the overlay, so a click on it would close the panel. ⚠️ Non-modal, no focus and
     * never closed by a press: it must take nothing from the panel — not the keys, not Esc —
     * and only the panel decides when it goes. z above the panels so its clicks reach it and
     * not the panel's press-outside.
     */
    Popup {
        id: barTakeover
        x: 0
        y: statusBar.y
        width: appShell.width
        height: statusBar.height
        padding: 0
        modal: false
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        z: 100
        background: Item {}
        // The bar does not move; neither do its prompts.
        enter: Transition {}
        exit: Transition {}

        contentItem: Item {
            Row {
                anchors.left: parent.left
                anchors.leftMargin: Math.round(20 * Theme.uiScale)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(20 * Theme.uiScale)

                Repeater {
                    model: appShell._barOwner ? appShell._barOwner.barHints : []
                    delegate: StatusHint {
                        model: modelData
                        onTriggered: function(kind) {
                            if (appShell._barOwner) appShell._barOwner.barAction(kind)
                        }
                    }
                }
                StatusHint {
                    visible: appShell._scrollHint
                    model: ({ btn: "RSCROLL", key: "", act: qsTr("Scroll"), kind: "none" })
                }
            }
        }
    }

    /*
     * The opening animation (6.0.0). Above the pages, the clock and the status bar, which it
     * keeps at opacity 0 and without input until its fade (see contentArea). Popups have their
     * own overlay above this.
     */
    StartupSplash {
        id: startupSplash
        anchors.fill: parent
        // Input returns to Home when the fade starts, not when it ends: by then Home is
        // visible, and a press during the last 0.4 s should do what it looks like it does.
        onBlockingChanged: {
            if (!blocking && appShell.currentPage === 0 && homeLoader.item)
                homeLoader.item.forceActiveFocus()
        }
        onFinished: {
            appShell._maybePromptUpdate()
            appShell._maybeShowSteamBanner()
        }
    }

    // The host page is standing on its cover (Settings → Interface → Host page background).
    readonly property bool _appsCover: currentPage === 1 && appsLoader.item !== null
                                       && appsLoader.item.coverDrawing === true

    // ── The theme toggle (7.0.0, 13/10/2026) ─────────────────────────────────────
    /*
     * Home's right stick click / T / the prompt. Under Dark or Light it sets the other mode;
     * under System or Auto it holds the other theme until the next change (Theme.toggleLight),
     * and a short notice says how long — the one thing the prompt cannot say.
     */
    function toggleTheme() {
        Theme.toggleLight()
        if (Theme.mode === Theme.ModeSystem || Theme.mode === Theme.ModeAuto)
            themeNotice.show(_themeNoticeText())
    }

    // A short line above the status bar, in the theme toggle's notice (7.0.0): used by the
    // PyroWave Test to say where the bitrate went.
    // `ms` (7.0.2) for a message that needs longer than a theme name: how long it stays up.
    function notice(text, ms) {
        themeNotice.show(text, ms)
    }

    function _hm(minutes) {
        var d = new Date(2000, 0, 1, Math.floor(minutes / 60), minutes % 60)
        return Qt.formatTime(d, StreamingPreferences.clockFormat === StreamingPreferences.CF_12H
                                ? "h:mm AP" : "HH:mm")
    }

    function _themeNoticeText() {
        var name = Theme.light ? qsTr("Light") : qsTr("Dark")
        if (!Theme.forced)
            return Theme.mode === Theme.ModeSystem ? qsTr("Following Windows") : qsTr("Automatic theme")
        if (Theme.mode === Theme.ModeSystem)
            return qsTr("%1 until Windows changes theme").arg(name)
        var until = Theme.forcedUntilMinutes
        if (until < 0)
            return name
        // A held Light ends when the schedule turns light anyway — at sunrise; a held Dark at
        // sunset. With set hours it is just a time.
        if (Theme.effectiveAutoHow === 0)
            return (Theme.light ? qsTr("Light until sunrise %1, then automatic again")
                                : qsTr("Dark until sunset %1, then automatic again")).arg(_hm(until))
        return qsTr("%1 until %2, then automatic again").arg(name).arg(_hm(until))
    }

    // While a stream or its launch screen is up (pushed over the shell), the automatic switch
    // waits: no point repainting the app behind the stream, and the curtain stays as it is.
    Binding {
        target: Theme
        property: "hold"
        value: typeof stackView !== "undefined" && stackView.depth > 1
    }

    Rectangle {
        id: themeNotice
        z: 50
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: statusBar.top
        anchors.bottomMargin: Math.round(14 * Theme.uiScale)
        width: themeNoticeText.implicitWidth + Math.round(32 * Theme.uiScale)
        height: themeNoticeText.implicitHeight + Math.round(18 * Theme.uiScale)
        radius: Math.round(8 * Theme.uiScale)
        color: Theme.card
        border.color: Theme.lineHigh
        border.width: 1
        opacity: 0
        visible: opacity > 0

        function show(text, ms) {
            themeNoticeText.text = text
            themeNoticeTimer.interval = ms ? ms : 2800
            opacity = 1
            themeNoticeTimer.restart()
        }

        Behavior on opacity {
            enabled: !Theme.reduceAnimations
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }

        Timer {
            id: themeNoticeTimer
            interval: 2800
            onTriggered: themeNotice.opacity = 0
        }

        Text {
            id: themeNoticeText
            anchors.centerIn: parent
            color: Theme.text
            font.family: Theme.family
            font.pixelSize: Math.round(Theme.fontBody * Theme.uiScale)
        }
    }

    // ── Steam Desktop Layout notice (6.5.1, #24) ──────────────────────────────────
    // Reported by SdlGamepadKeyNavigation once per launch. Held over the opening animation;
    // over a stream or its launch screen it is dropped — the next launch says it again if the
    // layout is still Desktop.
    property bool _steamBannerPending: false
    property bool _steamBannerMenuHold: false

    function _maybeShowSteamBanner() {
        if (!_steamBannerPending || startupSplash.running) return
        _steamBannerPending = false
        if (typeof stackView !== "undefined" && stackView.depth > 1) return
        steamLayoutBanner.show(_steamBannerMenuHold)
    }

    Connections {
        target: SdlGamepadKeyNavigation
        function onSteamDesktopLayoutDetected(menuHoldSwitches) {
            appShell._steamBannerMenuHold = menuHoldSwitches
            appShell._steamBannerPending = true
            appShell._maybeShowSteamBanner()
        }
    }

    SteamLayoutBanner {
        id: steamLayoutBanner
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.round(72 * Theme.uiScale)
    }

    UpdatePromptDialog {
        id: updatePrompt
        onAccepted: function(dontRemind) {
            if (dontRemind) AppUpdate.skipLatestVersion()
            appShell.openSettings()
            if (settingsLoader.item && settingsLoader.item.showAbout)
                settingsLoader.item.showAbout()
        }
        onDeclined: function(dontRemind) {
            if (dontRemind) AppUpdate.skipLatestVersion()
        }
        // The popup took the focus from the page under it; give it back, or the pad drives
        // nothing until something is clicked. Not after Yes: Settings takes the focus itself.
        onClosed: if (appShell.currentPage === 0 && homeLoader.item) homeLoader.item.forceActiveFocus()
    }
}
