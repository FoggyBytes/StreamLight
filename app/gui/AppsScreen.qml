import QtQuick 2.12
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.2
import QtQuick.Effects

import AppModel 1.0
import Theme 1.0
import ComputerManager 1.0
import StreamingPreferences 1.0
import SdlGamepadKeyNavigation 1.0

/*
 * The host page — the showcase.
 *
 * Same grammar as Home: one thing in the spotlight and everything else in a strip, two
 * navigation zones, and the action row saying what A will do. There the spotlight is the
 * host, here it is the game — so moving between the two screens does not mean learning a
 * second way of reading.
 *
 * What the grid of covers could never show, because there was nowhere to put it: which
 * store a game comes from in words rather than a 16px badge, whether it is running right
 * now, whether it carries settings of its own, and the right verb on the button — Resume
 * for a session already up, Play for one that isn't.
 *
 * Root is FocusScope (not Item) — required for activeFocus propagation from the Loader
 * above us down into the two zones. Plain Items do not propagate.
 */
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
    // Slot and count travel with the name because the shoulders need them: -1 is Global, a
    // real position in the cycle and not the absence of one, and a host with no profiles at
    // all has nothing to move between.
    property int    hostProfileSlot: -1
    property int    hostProfileCount: 0
    readonly property bool _hasProfiles: hostProfileCount >= 1
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
    // Frame pacing does nothing without V-Sync, and the per-game dialog offers frame
    // pacing but not V-Sync — so it has to be told the effective value to grey against.
    readonly property bool _effVsync:   (hostOverride && hostOverride.vsync   !== undefined) ? hostOverride.vsync === true : StreamingPreferences.enableVsync

    function _codecLabel(c) { return c === 1 ? "H.264" : c === 2 ? "HEVC" : c === 4 ? "AV1" : qsTr("Auto codec") }
    function _audioLabel(a) { return a === 1 ? "5.1" : a === 2 ? "7.1" : qsTr("Stereo") }

    function _resLabel() {
        var w = appsRoot._effW, h = appsRoot._effH
        if (w === 3840 && h === 2160) return "4K"
        if (w === 2560 && h === 1440) return "1440p"
        if (w === 1920 && h === 1080) return "1080p"
        if (w === 1280 && h === 720)  return "720p"
        return w + "×" + h
    }

    // ── Scale ────────────────────────────────────────────────────────────────
    // Same reference and the same reasoning as HostStage — see the note there. Both screens
    // must use the same divisor or a host and its games would be drawn at two different
    // sizes, which is the one thing a shared grammar cannot survive.
    readonly property real _u: Math.max(0.62, Math.min(1.60, width / 1330))
    function _px(n) { return Math.round(n * _u) }

    // ── The two columns ──────────────────────────────────────────────────────
    /*
     * The library on the left, the spotlight on the right, both running the full height
     * under the configuration line.
     *
     * The page used to stack them: a spotlight band across the top and the list beneath
     * it. That spent the width twice over — the band ended after its two buttons, and
     * every row of the list was as wide as the screen to hold one name — while the list
     * itself got four and a half titles out of a library with dozens. Side by side, the
     * same screen shows seven and the cover finally has the size its artwork was made for.
     */
    readonly property int _sideMargin: _px(44)
    readonly property int _colGap: _px(40)
    readonly property int _libraryWidth:
        Math.round((width - _sideMargin * 2 - _colGap) * 0.52)

    // ── Focus ────────────────────────────────────────────────────────────────
    /*
     * One zone: the library. Nothing else on this page can be reached with the pad.
     *
     * The hero's buttons used to be a second zone above the list, and Up only left the list
     * from row 0 — so from the twentieth game the buttons were twenty presses away. The fix
     * is not a shorter path to them: it is that there is nothing to reach. Every action on
     * this page already has a face button of its own (A plays, Select opens the per-game
     * settings, X stops the session), so the buttons exist for the mouse and are drawn with
     * their glyph on them. Zones belong where they earn their keep — on Home the host has
     * four actions and only Shutdown has a button of its own, so there the second zone is
     * the only way to reach the other three and it stays.
     */
    function focusLibrary() { appGrid.forceActiveFocus() }

    // The host went away while a launch screen was up. Deferred rather than dropped: see
    // computerLost().
    property bool goHomeWhenIdle : false

    Connections {
        target: stackView
        function onDepthChanged() {
            if (stackView.depth === 1 && appsRoot.goHomeWhenIdle) {
                appsRoot.goHomeWhenIdle = false
                if (appsRoot.appShell) appsRoot.appShell.showHome()
            }
        }
    }

    /**
     * Builds the launch screen and pushes it.
     *
     * ⚠️ Lives here, on the page, and NOT in the list delegate where the values come from.
     * A QML object keeps the context it was created in, and a delegate's context dies with the
     * delegate — so a segue created down there is left holding an invalid context the moment
     * this page goes away, even though the object itself survives on the stack. Every function
     * on it then throws "attempted to evaluate a function in an invalid context", including
     * the ones that end the launch: displayLaunchError sets no message and sessionFinished
     * never pops. Seen on 12/08 — the host went offline mid-launch, computerLost() sent us
     * Home, appsLoader unloaded this page, and the 503 that came back six seconds later
     * arrived at a dead object, leaving the launch screen up forever with no error and no way
     * out. The delegate passes plain values; the object belongs to the page.
     */
    function launchSegue(name, art, session, resume) {
        var component = Qt.createComponent("StreamSegue.qml")
        if (component.status !== Component.Ready) {
            console.warn("StreamSegue.qml not ready:", component.errorString())
            return
        }
        var segue = component.createObject(stackView, {
            "appName":          name,
            "boxArt":           art,
            "session":          session,
            "isResume":         resume,
            // ⚠️ Built here and not passed in. A closure carries the context it was written
            // in, so one written in the delegate would die with the delegate exactly like the
            // segue used to — and this is the callback that records the session ending, which
            // is what arms the "put the host's link back?" prompt.
            "onSessionEndedFn": function() {
                if (appsRoot.appShell)
                    appsRoot.appShell.noteStreamEnded(appsRoot.computerIndex, appsRoot.hostName)
            }
        })
        if (Window.window) Window.window.markStreamLaunching()
        stackView.push(segue)
    }

    // ── Pad glyphs ───────────────────────────────────────────────────────────
    // Resolved exactly as the status bar does, by SDL button POSITION — Nintendo swaps A/B
    // and X/Y relative to Xbox, so the Switch glyph for the south button is the one labeled
    // "B". Real artwork rather than a printed letter because Select is not a letter: it is
    // View on Xbox, Create on PlayStation and − on Switch, and a badge saying "Select" would
    // be wrong on two pads out of three.
    // (Resolved by ActionHint now — vendor, size and the keyboard alternative in one place.)

    // ── The game in the spotlight ────────────────────────────────────────────
    readonly property string focusedAppName:
        (appGrid && appGrid.currentItem) ? appGrid.currentItem._appName : ""
    readonly property string focusedBoxArt:
        (appGrid && appGrid.currentItem) ? appGrid.currentItem._boxArt : ""
    readonly property bool focusedOverridden:
        (appGrid && appGrid.currentItem) ? appGrid.currentItem._overridden === true : false
    readonly property string focusedStore:
        (appGrid && appGrid.storeMap && focusedAppName.length > 0)
            ? (appGrid.storeMap[focusedAppName] || "") : ""

    // Bound to delegate._running (a property — reactive) so the status-bar prompts and the
    // hero's verb update when the host-side session ends.
    property bool focusedAppIsRunning:
        (appGrid && appGrid.currentItem) ? appGrid.currentItem._running === true : false

    // "Desktop" is not a game and "Play Desktop" reads wrong; it is the one entry where the
    // honest verb is Open.
    readonly property string focusedVerb:
        focusedAppIsRunning ? qsTr("Resume")
      : focusedAppName === "Desktop" ? qsTr("Open")
      : qsTr("Play")

    // No `focusedActionLabel` exported any more: the status bar no longer prints A on this
    // page, because the verb is written on the button that carries the A glyph. The bar
    // keeps only Y and B, which have no button to sit on — the same division as Home.

    // Y → Settings; X (Menu) → stop the running app; Select → per-game settings.
    //
    // All three live on the root rather than on the list, because they are the page's
    // shortcuts and not the list's: they have to work wherever the focus happens to be, and
    // an unhandled key travels up the focus chain to get here. Select is mapped to Key_F13
    // (Key_Select would be treated as an activation key and launch the app).
    Keys.onHangupPressed: { if (appShell) appShell.openSettings() }
    Keys.onMenuPressed: function(event) {
        if (focusedAppIsRunning) {
            stopFocusedApp()
        }
        event.accepted = true
    }
    // ⚠️ Page-level, not global Shortcuts: the focused item sees the key first, so typing in
    // a dialog on top of this page cannot trigger any of them.
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_F13) {
            openCustomizeForFocused()
            event.accepted = true
        }
        // Keyboard equivalents of the prompts drawn on this page's buttons.
        else if (event.key === Qt.Key_G) {
            openCustomizeForFocused()
            event.accepted = true
        } else if (event.key === Qt.Key_S) {
            if (appShell) appShell.openSettings()
            event.accepted = true
        } else if (event.key === Qt.Key_X && focusedAppIsRunning) {
            stopFocusedApp()
            event.accepted = true
        }
        /*
         * Profile cycling, the same pair as Home: LB/RB on the pad, Q/E on the keyboard.
         *
         * ⚠️ Handled here and not in AppShell even though the shell already owns F16/F17.
         * Its handler returns early on any page but Home, and a key travels up the focus
         * chain, so this page sees them first — which is where they belong, next to the
         * badge they move. The shoulders carry inert keys of their own precisely so they
         * cannot be confused with the host cycling on PgUp/PgDn.
         */
        else if (event.key === Qt.Key_F16 || event.key === Qt.Key_Q) {
            cycleProfile(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_F17 || event.key === Qt.Key_E) {
            cycleProfile(1)
            event.accepted = true
        }
    }

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
        appSettingsDialog.effectiveVsync = appsRoot._effVsync
        appSettingsDialog.open()
    }
    function openCustomizeForFocused() {
        if (appGrid && appGrid.currentItem) {
            openCustomize(appGrid.currentIndex, appGrid.currentItem._appName)
        }
    }

    // The store's mark, for the spotlight's meta line. Lives on the root rather than on the
    // list because the spotlight is what draws it now: it used to belong to the badge on the
    // cover tiles, which went away when the library became a list of titles, and the function
    // sat here unused ever since. Empty string for a store we have no artwork for — and for
    // Desktop and Steam Big Picture, which have no store at all.
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

    /*
     * This page paints its own floor when it has artwork to paint it with — the blurred cover
     * under a veil that reaches full opacity at the bottom — so the shell's status bar has to
     * sit on the same near-black instead of on the shell's accent-tinted gradient, or there is
     * a visible step across the foot of the screen.
     *
     * ⚠️ It has to follow whether the artwork is actually there, not just be set once. Without
     * a cover, or with reduced animations, this page falls back to the shell's gradient and a
     * hardcoded near-black bar produced that same step the other way round.
     *
     * ⚠️ On `appShellChanged`, NOT on `Component.onCompleted`: the Loader assigns `appShell`
     * after the item is constructed, so at completion it is still null and the assignment is
     * silently skipped — which is why the bar once kept the shell's lighter gradient.
     *
     * ⚠️ And restored explicitly on the way out. This was briefly a `Binding`, on the
     * assumption that it would put the old value back when the page went away. It does not,
     * and the shell outlives the page — so the near-black leaked onto Home and every other
     * screen, which is the whole reason this property defaults to transparent.
     */
    function _syncStatusBarFloor() {
        if (appShell) appShell.statusBarFloor = ambient.drawing ? "#05080a" : "transparent"
    }

    onAppShellChanged:       _syncStatusBarFloor()
    Component.onDestruction: if (appShell) appShell.statusBarFloor = "transparent"

    Connections {
        target: ambient
        function onDrawingChanged() { appsRoot._syncStatusBarFloor() }
    }

    // Loader sets host properties AFTER Component.onCompleted → init here.
    onHostComputerModelChanged: _initFromHost()
    onComputerIndexChanged:    _initFromHost()

    // Host link matching (4.6.0) — same three facts the host stage uses. This is the last
    // screen before Launch, so it's where the upcoming change is most worth stating.
    property int  _localMbps: 0
    property int  _hostMbps: 0
    property bool _hostAllowsLink: false

    // The host declines SETSPEED while it counts a session as running, and it keeps counting
    // one for its whole inactivity grace after a client disconnects — so this covers the half
    // minute after the previous session as well as a live one. Announcing a change through it
    // is announcing one that will not happen. False by default, so a host that never answers
    // behaves as it did before.
    //
    // ⚠️ A snapshot taken on entering the page, like _hostAllowsLink beside it — this screen
    // asks NETINFO once, where the host list polls it every two seconds. So a visit that
    // begins inside the grace window keeps the chip hidden even once the window has passed.
    // Left as is: the flow this exists for is finish a session, come back, launch again, which
    // happens well inside the window, and the worst the staleness costs is a missing chip
    // rather than a promise that goes unkept.
    property bool _hostSessionActive: false

    // ⚠️ The EFFECTIVE setting: a per-host profile can turn link matching off, and reading the
    // global toggle straight made the header announce a change that would then not happen.
    readonly property bool _effMatchLink:
        (hostOverride && hostOverride.matchlink !== undefined)
            ? hostOverride.matchlink === true
            : StreamingPreferences.matchHostLinkSpeed

    readonly property bool _willSwitchLink:
        _effMatchLink
        && _hostAllowsLink
        && !_hostSessionActive
        && _localMbps > 0
        && _hostMbps > _localMbps

    function _fmtMbps(m) {
        return m >= 1000 ? (m / 1000) + " Gbps" : m + " Mbps"
    }

    /*
     * Re-read everything the active profile decides.
     *
     * ⚠️ `hostOverride` is assigned, not bound — it comes from a Q_INVOKABLE, so nothing
     * invalidates it on its own. Every one of the badges on the configuration line hangs off
     * it through `_eff*`, so reassigning it here is what makes them follow the profile the
     * moment it changes; leaving it stale would show the previous profile's settings under
     * the new profile's name, which is worse than not offering the shortcut at all.
     */
    function _refreshProfile() {
        if (!hostComputerModel || computerIndex < 0) return
        hostProfileSlot = hostComputerModel.hostActiveProfile(computerIndex)
        hostProfileName = hostProfileSlot >= 0
                          ? hostComputerModel.hostActiveProfileName(computerIndex) : ""
        hostOverride    = hostComputerModel.hostActiveOverride(computerIndex)
    }

    // LB/RB and Q/E. The cycle includes Global at -1, so one profile is still two positions
    // to move between — hence `< 1` and not `< 2`.
    function cycleProfile(dir) {
        if (!hostComputerModel || computerIndex < 0 || hostProfileCount < 1) return
        hostComputerModel.cycleHostProfile(computerIndex, dir)
        _refreshProfile()
    }

    function _initFromHost() {
        if (!hostComputerModel || computerIndex < 0) return
        hostComputerModel.requestStreamTweakStatus(computerIndex)
        var link = hostComputerModel.probeLocalLink(computerIndex)
        _localMbps = link.usable === true ? link.mbps : 0
        hostComputerModel.requestHostNetInfo(computerIndex)
        hostProfileCount = hostComputerModel.hostProfileCount(computerIndex)
        _refreshProfile()
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
                var m = parseInt(status)
                appsRoot._hostMbps = isNaN(m) ? 0 : m
            }
        }
        function onHostNetInfoReceived(idx, info) {
            if (idx === appsRoot.computerIndex) {
                appsRoot._hostAllowsLink = info.allowsLinkControl === true
                // Guarded, unlike the line above: an empty reply is the host mid-change and
                // says nothing about whether a session is running, so it must not read as no.
                if (info.sessionActive !== undefined)
                    appsRoot._hostSessionActive = info.sessionActive === true
            }
        }
        function onAppStoresReceived(idx, stores) {
            if (idx === appsRoot.computerIndex && appGrid) {
                appGrid.storeMap = stores
            }
        }
    }

    // Ambient backdrop — the focused cover, blurred, behind everything.
    //
    // Lives in CoverAmbient so that the launch and quit screens can stand on the same
    // artwork rather than on their own imitation of it. The crossfade on focus changes
    // moved in there with it.
    CoverAmbient {
        id: ambient
        z: -2
        source: appsRoot.focusedBoxArt
        overscan: appsRoot._px(60)
    }


    // ═════════════════════════════════════════════════════════════════════════
    // Header
    // ═════════════════════════════════════════════════════════════════════════
    // No "back" button here on purpose: the status bar's B prompt is already clickable and
    // is how every other screen is left, so a second control would be a duplicate that only
    // exists on this page.
    Item {
        id: appsHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: appsRoot._px(24)
        anchors.leftMargin: appsRoot._px(44)
        anchors.rightMargin: appsRoot._px(44)
        height: appsRoot._px(40)

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: appsRoot._px(16)

            Image {
                anchors.verticalCenter: parent.verticalCenter
                source: "qrc:/streamlight.ico"
                width: appsRoot._px(40); height: width
                sourceSize.width:  40 * Screen.devicePixelRatio
                sourceSize.height: 40 * Screen.devicePixelRatio
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: appsRoot.hostName
                color: Theme.text
                font.family: Theme.family
                font.pixelSize: appsRoot._px(30)
                font.bold: true
                font.letterSpacing: -appsRoot._u * 0.45
                elide: Label.ElideRight
            }

            /*
             * The chips, beside the host name rather than in the far corner.
             *
             * They were on the right only because nothing else was, and once the clock arrived
             * the two ended up sharing one corner without sharing a baseline — chips one line
             * tall, clock two — which read as a dense block instead of two separate readings.
             * They describe *this host*, and the host's name is here, so this is where they are
             * read. It is also how the Home card already arranges them.
             */
            Row {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: appsRoot._px(6)
                spacing: appsRoot._px(8)

            Repeater {
                model: {
                    var c = [{ text: qsTr("Online"), dot: Theme.online }]
                    /*
                     * The profile, with its shoulders on either side — the same arrangement
                     * as the host card on Home, and for the same reason: LB/RB attached to
                     * the thing they move need no caption, because the badge between them
                     * says what they change.
                     *
                     * ⚠️ Which is why it reads "Global" rather than disappearing when no
                     * profile is active. With the shoulders on it, an empty slot would leave
                     * them pointing at nothing; Global is a real position in the cycle. A
                     * host with no profiles at all shows neither, since naming the only
                     * possible state tells the user nothing.
                     */
                    if (appsRoot._hasProfiles)
                        c.push({ text: appsRoot.hostProfileSlot >= 0 && appsRoot.hostProfileName.length > 0
                                       ? appsRoot.hostProfileName : qsTr("Global"),
                                 dot: Theme.accent, kind: "profile" })
                    if (appsRoot.isTailscaleClone)
                        c.push({ text: qsTr("Tailscale"), dot: Theme.text2 })
                    // ⚠️ Same words as the host card on Home, in the same order of precedence.
                    // These are the same three states seen from a different screen, and they
                    // used to have a vocabulary each — "Link changing" here against "Matching"
                    // there, "Link restored" against "Link back to 2.5 Gbps" — so moving between
                    // the two screens during one renegotiation read as two different events.
                    //
                    // A restore takes precedence over the bare "the adapter is moving", because
                    // during a restore both are true and only one of them says why.
                    if (appsRoot.appShell && appsRoot.appShell._linkRestoreActive)
                        c.push({ text: appsRoot.appShell._linkRestoreDone
                                       ? qsTr("Link back") : qsTr("Restoring"),
                                 dot: appsRoot.appShell._linkRestoreDone ? Theme.online : Theme.warning,
                                 fg:  appsRoot.appShell._linkRestoreDone ? Theme.online : Theme.warning })
                    else if (appsRoot.appShell && appsRoot.appShell._hostLinkChanging)
                        c.push({ text: qsTr("Matching"), dot: Theme.warning, fg: Theme.warning })
                    else if (appsRoot._willSwitchLink)
                        c.push({ text: qsTr("On launch → %1").arg(appsRoot._fmtMbps(appsRoot._localMbps)),
                                 dot: Theme.warning, fg: Theme.warning })
                    return c
                }

                // The shoulders are drawn inside the delegate rather than as a separate block
                // beside the row: it keeps the profile in its place in the order — after
                // Online, before Tailscale — with one delegate and no second copy of the chip.
                // A Row skips invisible children, so on every other chip the two collapse and
                // the spacing closes by itself.
                delegate: Row {
                    spacing: appsRoot._px(8)

                    ProfileShoulder {
                        visible: modelData.kind === "profile"
                        anchors.verticalCenter: parent.verticalCenter
                        buttonKey: "LB"; keyLabel: "Q"
                        onTriggered: appsRoot.cycleProfile(-1)
                    }

                    Rectangle {
                        height: appsRoot._px(26)
                        width: chipRow.implicitWidth + appsRoot._px(26)
                        radius: appsRoot._px(6)
                        color: "transparent"
                        border.color: Theme.line
                        border.width: 1

                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: appsRoot._px(7)

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: appsRoot._px(8); height: width
                                radius: width / 2
                                color: modelData.dot
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.text.toUpperCase()
                                color: modelData.fg !== undefined ? modelData.fg : Theme.text2
                                font.family: Theme.family
                                font.pixelSize: appsRoot._px(13)
                                font.weight: Font.DemiBold
                                font.letterSpacing: appsRoot._u
                            }
                        }
                    }

                    ProfileShoulder {
                        visible: modelData.kind === "profile"
                        anchors.verticalCenter: parent.verticalCenter
                        buttonKey: "RB"; keyLabel: "E"
                        onTriggered: appsRoot.cycleProfile(1)
                    }
                }
            }
        }
        }

        // (The clock is the shell's — see StatusCluster in AppShell. Anchored here it took
        //  this header's `_px` margins, so it sat in a different corner from Home's.)
    }

    // ── The configuration line ────────────────────────────────────────────────
    /*
     * Everything that decides what the next launch looks like, on one line, because the
     * answer to "why does this look wrong" is always somewhere in it.
     *
     * Each value now sits in its own outlined box under a group label — Host, then Stream —
     * instead of being a run of words separated by spaces. Boxes turn the line into
     * something the eye scans rather than reads: the eye finds "120 FPS" by shape and
     * position, where a sentence has to be parsed to the end.
     *
     * Nothing on this line is green. HDR used to be the one green value out of six, which
     * made the other five look switched off — green in this interface means "live and
     * healthy", and none of these is reporting health. The link speed is not green either:
     * it is a fact about the wire, and the same reasoning applies to it. The one colour left
     * on this screen is the amber of a link change that has not happened yet.
     */
    Row {
        id: cfgLine
        anchors.top: appsHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: appsRoot._px(13)
        anchors.leftMargin: appsRoot._px(44)
        anchors.rightMargin: appsRoot._px(44)
        spacing: appsRoot._px(9)

        readonly property int _rowH: appsRoot._px(33)

        Repeater {
            // "g" = group label · "b" = value badge · "s" = separator.
            model: {
                var m = []
                var showAddr  = !StreamingPreferences.hideHostIps
                var showSpeed = appsRoot.hostNicSpeed.length > 0
                                && appsRoot.hostNicSpeed !== qsTr("N/A")

                if (showAddr || showSpeed) {
                    m.push({ t: "g", text: qsTr("Host") })
                    if (showAddr)
                        m.push({ t: "b", text: (appsRoot.hostAddress && appsRoot.hostAddress.length > 0)
                                               ? appsRoot.hostAddress : qsTr("N/A") })
                    if (showSpeed)
                        m.push({ t: "b", text: appsRoot.hostNicSpeed })
                    m.push({ t: "s", text: "" })
                }

                m.push({ t: "g", text: qsTr("Stream") })
                m.push({ t: "b", text: appsRoot._resLabel() })
                m.push({ t: "b", text: appsRoot._effFps + " FPS" })
                m.push({ t: "b", text: (appsRoot._effBitrate / 1000).toFixed(0) + " Mbps" })
                if (appsRoot._effHdr)
                    m.push({ t: "b", text: "HDR" })
                m.push({ t: "b", text: appsRoot._codecLabel(appsRoot._effCodec) })
                m.push({ t: "b", text: appsRoot._audioLabel(appsRoot._effAudio) })
                return m
            }

            delegate: Rectangle {
                id: cfgChip
                readonly property bool _isSep: modelData.t === "s"
                readonly property bool _isGrp: modelData.t === "g"

                height: cfgLine._rowH
                width: _isSep ? appsRoot._px(13)
                              : cfgText.implicitWidth + appsRoot._px(_isGrp ? 4 : 26)
                radius: (_isGrp || _isSep) ? 0 : appsRoot._px(7)
                color: (_isGrp || _isSep) ? "transparent" : "#0affffff"
                border.width: (_isGrp || _isSep) ? 0 : 1
                border.color: Theme.lineHigh

                // A hairline, centred in its own slot, rather than the delegate itself being
                // one pixel wide — that way the gap on either side comes from the rule and
                // not from the Row's spacing doing double duty.
                Rectangle {
                    anchors.centerIn: parent
                    visible: cfgChip._isSep
                    width: 1
                    height: appsRoot._px(26)
                    color: Theme.line
                }

                Label {
                    id: cfgText
                    anchors.centerIn: parent
                    visible: !cfgChip._isSep
                    text: cfgChip._isGrp ? String(modelData.text).toUpperCase() : String(modelData.text)
                    color: cfgChip._isGrp ? Theme.text3 : Theme.text
                    font.family: Theme.family
                    font.pixelSize: appsRoot._px(13)
                    font.weight: cfgChip._isGrp ? Font.Normal : Font.DemiBold
                    font.letterSpacing: cfgChip._isGrp ? appsRoot._u * 1.2 : 0
                }
            }
        }
    }

    /*
     * A "hero cover regeneration" workaround used to live here — a forced source reload 150 ms
     * after the window came back from a stream, because the spotlight cover returned oversized
     * and stretched. It was removed in 5.1.2, put back inside HeroCover.qml in 5.2.0, and is
     * now gone for good: the cause was found and fixed at the source (the drop shadow's
     * automatic padding resizing the effect item), so there is nothing left to repair.
     *
     * Nothing about the deformation belongs in this file. Read the block at the top of
     * HeroCover.qml before touching cover geometry anywhere.
     */

    // ═════════════════════════════════════════════════════════════════════════
    // The spotlight
    // ═════════════════════════════════════════════════════════════════════════
    Item {
        id: hero
        anchors.top: cfgLine.bottom
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: appsRoot._px(16)
        anchors.rightMargin: appsRoot._sideMargin
        anchors.bottomMargin: appsRoot._px(58)
        width: appsRoot.width - appsRoot._sideMargin * 2
               - appsRoot._colGap - appsRoot._libraryWidth
        visible: appGrid.count > 0

        // Cover, name, meta and actions as one centred stack. The column is what the
        // library leaves, so everything in it is laid out from the centre outwards.
        Column {
            id: heroStack
            anchors.centerIn: parent
            width: parent.width
            spacing: 0

        // ── The big cover ────────────────────────────────────────────────────
        HeroCover {
            id: heroArtHolder
            anchors.horizontalCenter: parent.horizontalCenter

            /*
             * ⚠️ 340 is a ceiling as much as a size: the covers themselves top out at
             * 600x900 (Steam's library capsule, and there is no larger portrait asset),
             * and on a 4K panel at 200% scaling — where _u is back at 1.32 while the
             * device pixel ratio is 2 — a design height of 340 asks for exactly 900
             * physical pixels. Larger than this and the biggest cover on the screen
             * starts being the softest.
             *
             * The width, the 2:3 box, the crop, the rounded corners and the shadow all
             * live in HeroCover now, shared with the launch curtain so the two screens
             * cannot drift apart.
             */
            height: Math.min(appsRoot._px(340), hero.height - appsRoot._px(150))
            source: appsRoot.focusedBoxArt
            radius: appsRoot._px(8)
            shadow: !Theme.reduceAnimations
            shadowOffset: appsRoot._px(8)

            // Placeholder box art carries no title, so the name has to be drawn over it or
            // the spotlight shows an anonymous rectangle.
            Label {
                anchors.fill: parent
                anchors.margins: appsRoot._px(10)
                visible: appsRoot.focusedBoxArt === "" || heroArtHolder.status === Image.Error
                text: appsRoot.focusedAppName
                color: Theme.text2
                font.family: Theme.family
                font.pixelSize: appsRoot._px(16)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                elide: Text.ElideRight
            }
        }

        // ── Name, meta, actions ──────────────────────────────────────────────
        // Under the cover now rather than beside it, and centred on it: the column is
        // narrower than the old full-width band, so a name set against its left edge would
        // sit off to one side of the artwork it belongs to.
        Item { width: 1; height: appsRoot._px(20) }

            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                spacing: 0

                /*
                 * The name in full, always — a long title gets smaller, never cut.
                 *
                 * It used to elide on one line, which is how "Torment: Tides of Numenera"
                 * became "Torment: Tides of Numene…" — the one thing on the page whose whole
                 * job is to say which game this is, saying most of it.
                 *
                 * fontSizeMode does the work: Fit shrinks the text until it fits the box, and
                 * minimumPixelSize is the floor. Two lines are allowed so that past the floor
                 * it wraps rather than carrying on shrinking — below about 26 the title stops
                 * reading as the title, and a name long enough to need that has room to wrap.
                 * Only past both does it finally elide, which no name in a real library reaches.
                 */
                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: appsRoot.focusedAppName
                    color: Theme.text
                    font.family: Theme.family
                    // Down from 54 while everything around it went up — the host name from
                    // 24 to 30, the setting values from 14 to 16, the list rows from 76 to
                    // 84. It was never too small: it was out of proportion with the rest,
                    // and shrinking the one that was shouting was half of fixing that.
                    font.pixelSize: appsRoot._px(40)
                    fontSizeMode: Text.Fit
                    minimumPixelSize: appsRoot._px(26)
                    font.bold: true
                    font.letterSpacing: -appsRoot._u * 0.9
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Item { width: 1; height: appsRoot._px(4) }

                // The store — its mark and its name — whether the game is running, and whether
                // it carries settings of its own: three facts the cover grid had no room for.
                // The mark earns its place by being recognisable before the word is read, which
                // is the whole reason storefronts have one.
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: appsRoot._px(8)

                    Image {
                        id: heroStoreIcon
                        anchors.verticalCenter: parent.verticalCenter
                        visible: source != ""
                        source: appsRoot.storeIconSource(appsRoot.focusedStore)
                        width: appsRoot._px(19); height: width
                        sourceSize.width: 38; sourceSize.height: 38
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        // Its natural width, capped by what the column leaves once the mark has
                        // taken its share — so the row stays centred on short text and still
                        // elides instead of running past the column on long text.
                        width: Math.min(implicitWidth,
                                        hero.width - (heroStoreIcon.visible
                                                      ? heroStoreIcon.width + parent.spacing : 0))
                        text: {
                            var parts = []
                            if (appsRoot.focusedStore.length > 0) parts.push(appsRoot.focusedStore)
                            if (appsRoot.focusedAppIsRunning)     parts.push(qsTr("running now"))
                            if (appsRoot.focusedOverridden)       parts.push(qsTr("custom settings"))
                            return parts.join(" · ")
                        }
                        color: appsRoot.focusedAppIsRunning ? Theme.online : Theme.text2
                        font.family: Theme.family
                        font.pixelSize: appsRoot._px(16)
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }

                Item { width: 1; height: appsRoot._px(14) }

                // ── Action row ───────────────────────────────────────────────
                /*
                 * Drawn, clickable, and deliberately outside the focus chain.
                 *
                 * Every one of these already has a face button — A plays, Select opens the
                 * per-game settings, X stops — so the pad has nothing to walk to and the
                 * glyph on each button says so. They stay on screen because the mouse has no
                 * face buttons, and because a legend that names its key is worth more than a
                 * round icon left to be guessed at.
                 *
                 * None of them is filled with the accent: in this interface an accent fill
                 * means "the focus is here", and painting it on a button the pad can never
                 * reach would be the interface lying about itself.
                 */
                Row {
                    id: heroActions
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: appsRoot._px(10)

                    readonly property var actions: {
                        var a = [{ kind: "launch", label: appsRoot.focusedVerb,
                                   btn: "A", key: "Enter", danger: false }]
                        a.push({ kind: "customize", label: qsTr("Per-game settings"),
                                 btn: "SELECT", key: "G", danger: false })
                        // "Stop", not "Quit game": the entry holding the session is sometimes
                        // the Desktop, and "quit" is the wrong word for it. It appears
                        // whenever the thing in the spotlight is the one currently streaming.
                        if (appsRoot.focusedAppIsRunning)
                            a.push({ kind: "quit", label: qsTr("Stop"),
                                     btn: "X", key: "X", danger: true })
                        return a
                    }

                    function run(kind) {
                        switch (kind) {
                        case "launch":    appsRoot.resumeFocusedApp();        break
                        case "customize": appsRoot.openCustomizeForFocused(); break
                        case "quit":      appsRoot.stopFocusedApp();          break
                        }
                    }

                    Repeater {
                        model: heroActions.actions

                        delegate: Rectangle {
                            id: heroBtn

                            readonly property bool _hovered: heroBtnMouse.containsMouse

                            height: appsRoot._px(34)
                            width: heroBtnRow.implicitWidth + appsRoot._px(26)
                            radius: appsRoot._px(9)

                            color: _hovered ? "#1fffffff" : "#0dffffff"
                            border.width: 1
                            border.color: _hovered ? Theme.accent : Theme.lineHigh

                            Behavior on color {
                                enabled: !Theme.reduceAnimations
                                ColorAnimation { duration: 110 }
                            }

                            Row {
                                id: heroBtnRow
                                anchors.centerIn: parent
                                spacing: appsRoot._px(9)

                                // The last prompts that still drew a fixed controller glyph.
                                // ActionHint handles vendor, size and the keyboard alternative,
                                // so these follow the device in hand like everything else.
                                ActionHint {
                                    anchors.verticalCenter: parent.verticalCenter
                                    buttonKey: modelData.btn
                                    keyLabel:  modelData.key
                                    size: appsRoot._px(19)
                                }

                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.label
                                    color: modelData.danger ? Theme.danger : Theme.text
                                    font.family: Theme.family
                                    font.pixelSize: appsRoot._px(15)
                                    // The launch verb carries a shade more weight — it is the
                                    // primary action — without an accent fill, which would
                                    // read as focus on a button the pad cannot reach.
                                    font.weight: modelData.kind === "launch" ? Font.DemiBold : Font.Normal
                                }
                            }

                            MouseArea {
                                id: heroBtnMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: heroActions.run(modelData.kind)
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Rail caption ──────────────────────────────────────────────────────────
    Label {
        id: railLabel
        anchors.top: cfgLine.bottom
        anchors.left: parent.left
        anchors.leftMargin: appsRoot._sideMargin
        anchors.topMargin: appsRoot._px(16)
        text: qsTr("ALL APPS")
        color: Theme.text3
        font.family: Theme.family
        font.pixelSize: appsRoot._px(13)
        font.letterSpacing: appsRoot._u * 1.6
    }

    // ═════════════════════════════════════════════════════════════════════════
    // The library — the only zone
    // ═════════════════════════════════════════════════════════════════════════
    /*
     * A vertical list of titles, not a wall of covers.
     *
     * The cover already has a place — the spotlight above, at a size worth looking at — so
     * repeating it forty times small was showing the same thing twice and reading neither
     * well. A list gives every game its name in full, which a 200px cover cannot, and one
     * axis to move along instead of two: on a pad that is the difference between arriving at
     * a game and hunting for it.
     */
    ListView {
        id: appGrid
        anchors.top: railLabel.bottom
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.topMargin: appsRoot._px(6)
        anchors.leftMargin: appsRoot._sideMargin
        anchors.bottomMargin: appsRoot._px(58)
        width: appsRoot._libraryWidth

        // The list runs the full height of the page now, which is where the extra titles
        // come from: the spotlight moved out of its way instead of sitting on top of it.
        clip: true
        boundsBehavior: Flickable.OvershootBounds
        // Keeps the focused row off the edges while walking with the pad, so the next title
        // is always already visible rather than appearing as you reach it.
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: appsRoot._px(60)
        preferredHighlightEnd: height - appsRoot._px(60)
        highlightMoveDuration: Theme.reduceAnimations ? 0 : 160

        property AppModel appModel: createModel()
        property bool activated
        property bool showGames
        property var storeMap: ({})

        focus: true
        activeFocusOnTab: true

        readonly property int _rowH: appsRoot._px(84)
        readonly property int _gap:  appsRoot._px(6)

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
            // ⚠️ Never while a launch screen is up. Going Home unloads this page, and this
            // page owns the launch screen's QML context — pulling it out from under a launch
            // in flight leaves an object that is still on screen but whose every function
            // throws "invalid context", so nothing can report the failure and nothing pops it.
            // That is the 12/08 hang: the host went offline six seconds before answering 503.
            // A host that has gone away is worth leaving, just not this second.
            if (stackView.depth > 1) {
                appsRoot.goHomeWhenIdle = true
                return
            }
            if (appsRoot.appShell) appsRoot.appShell.showHome()
        }

        Keys.onReturnPressed: function(event) { if (currentItem) currentItem.launchOrResumeSelectedApp(true); event.accepted = true }
        Keys.onEnterPressed:  function(event) { if (currentItem) currentItem.launchOrResumeSelectedApp(true); event.accepted = true }
        Keys.onSpacePressed:  function(event) { if (currentItem) currentItem.launchOrResumeSelectedApp(true); event.accepted = true }

        // No Up handler: Up is the ListView's own, and at the first row it does nothing
        // because there is nowhere above to go. That is the whole point of the single zone —
        // the boundary test that used to live here existed only to hand the focus to the
        // buttons, and the buttons are no longer a stop.

        function createModel() {
            var model = Qt.createQmlObject('import AppModel 1.0; AppModel {}', parent, '')
            model.initialize(ComputerManager, appsRoot.computerIndex, appsRoot.showHiddenGames)
            return model
        }

        model: appModel

        delegate: NavigableItemDelegate {
            id: appDelegate
            width: appGrid.width
            height: appGrid._rowH
            grid: appGrid

            // Exposed to appsRoot for the hero and the status-bar prompts.
            property int    _appId:      model.appid
            property string _appName:    model.name
            property bool   _running:    model.running
            property string _boxArt:     model.boxart
            property bool   _overridden: model.overridden

            opacity: model.hidden ? 0.45 : 1.0

            // Selected is not the same as focused: a dialog on top of the page takes the
            // focus away while the spotlight still shows the selected game, so the row keeps
            // a quiet marker in that case and only lights up when the list itself has it.
            readonly property bool _selected: appGrid.currentIndex === index
            readonly property bool _lit:
                appDelegate.inputFocused || (_selected && appGrid.activeFocus)

            // Disable Material's default focus highlight; the row draws its own.
            background: Item { anchors.fill: parent }

            Rectangle {
                id: row
                anchors.fill: parent
                anchors.bottomMargin: appGrid._gap
                radius: appsRoot._px(10)

                color: appDelegate._lit     ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                     : appDelegate._selected ? "#14ffffff"
                     :                         "transparent"
                border.width: appDelegate._lit ? 2 : (appDelegate._selected ? 1 : 0)
                border.color: appDelegate._lit ? Theme.accent : Theme.line

                Behavior on color { enabled: !Theme.reduceAnimations; ColorAnimation { duration: 120 } }

                // ── Thumbnail ────────────────────────────────────────────────
                // Fixed box, PreserveAspectFit, no cropping. Box art is not one shape:
                // Steam ships 600x900 (2:3) while Moonlight's own placeholders are 3:4, so a
                // box that forces either ratio cuts the edges off half the library. The box is
                // cut for the taller of the two and the shorter one letterboxes by a few
                // pixels — which is invisible, where a crop through the artwork is not.
                Item {
                    id: thumbBox
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: appsRoot._px(14)
                    height: parent.height - appsRoot._px(16)
                    width: Math.round(height * 2 / 3)

                    Image {
                        id: cover
                        anchors.fill: parent
                        source: model.boxart
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        smooth: true
                        mipmap: true
                    }

                    // Placeholder art carries no title, and at this size neither would a
                    // failed load — the name is already beside it, so a plain frame is enough.
                    Rectangle {
                        anchors.fill: parent
                        visible: cover.status !== Image.Ready
                        color: "#18ffffff"
                        radius: appsRoot._px(4)
                    }
                }

                // ── Title, and the store under it ────────────────────────────
                Column {
                    anchors.left: thumbBox.right
                    anchors.right: runTag.visible ? runTag.left : parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: appsRoot._px(16)
                    anchors.rightMargin: appsRoot._px(16)
                    spacing: appsRoot._px(3)

                    Label {
                        width: parent.width
                        text: model.name
                        color: Theme.text
                        font.family: Theme.family
                        font.pixelSize: appsRoot._px(22)
                        font.weight: appDelegate._lit ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    Label {
                        property string store: appGrid.storeMap[model.name] || ""
                        width: parent.width
                        visible: store.length > 0 || model.overridden
                        text: {
                            var parts = []
                            if (store.length > 0)   parts.push(store)
                            if (model.overridden)   parts.push(qsTr("custom settings"))
                            return parts.join("  ·  ")
                        }
                        color: Theme.text3
                        // The same body as the store line in the spotlight: one size for the
                        // page's secondary text instead of a 15 here and a 17 there.
                        font.pixelSize: appsRoot._px(16)
                        font.family: Theme.family
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }

                // ── The running session ──────────────────────────────────────
                // Marked on the row itself and not only in the spotlight: the session may be
                // held by a game far down the list, and "what is running right now" is the one
                // thing that has to be findable without walking the whole library.
                Rectangle {
                    id: runTag
                    visible: appDelegate._running
                    anchors.right: parent.right
                    anchors.rightMargin: appsRoot._px(16)
                    anchors.verticalCenter: parent.verticalCenter
                    width: runTagLabel.implicitWidth + appsRoot._px(22)
                    height: appsRoot._px(26)
                    radius: appsRoot._px(6)
                    color: Theme.accent

                    SequentialAnimation on opacity {
                        running: runTag.visible && !Theme.reduceAnimations
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 0.45; duration: 900; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0;  duration: 900; easing.type: Easing.InOutSine }
                    }

                    Label {
                        id: runTagLabel
                        anchors.centerIn: parent
                        text: qsTr("STREAMING")
                        color: Theme.onAccent
                        font.family: Theme.family
                        font.pixelSize: appsRoot._px(13)
                        font.bold: true
                        font.letterSpacing: appsRoot._u
                    }
                }

                // (A round tune button used to sit here, at the end of every row. It was a
                //  mouse target dressed up as information: it said nothing the subtitle does
                //  not already say — "custom settings" is written there in words — and the
                //  mouse reaches the same dialog from the Per-game settings button above.)
            }

            function launchOrResumeSelectedApp(quitExistingApp) {
                // Drop stale events that arrive after a stream session pops.
                if (Window.window && Window.window._streamJustEnded === true) {
                    return
                }
                // Drop a second push when one is already in flight: a double-tap on A (very
                // easy with a Bluetooth pad) would otherwise queue a second Session that
                // auto-fires when the first one ends.
                if (Window.window && Window.window._streamLaunching === true) {
                    return
                }

                // Must use appGrid.appModel — bare appModel is not in scope.
                var m = appGrid.appModel
                var runningId = m.getRunningAppId()
                if (runningId !== 0 && runningId !== model.appid) {
                    if (quitExistingApp) {
                        quitAppDialog.appName = m.getRunningAppName()
                        quitAppDialog.boxArt = m.getRunningAppBoxArt()
                        quitAppDialog.segueToStream = true
                        quitAppDialog.nextAppName = model.name
                        // Captured here and not in quitApp(): this is the delegate, the only
                        // scope where `model` exists. The dialog is a sibling of the list and
                        // has nothing but the index to go on.
                        quitAppDialog.nextBoxArt = model.boxart
                        quitAppDialog.nextAppIndex = index
                        quitAppDialog.open()
                    }
                    return
                }

                // The curtain wants the box art, and this delegate is the only place that has
                // it: it comes from the app model, while NvApp carries no artwork at all.
                // Plain values only — the segue is built by the page, see launchSegue().
                appsRoot.launchSegue(model.name,
                                     model.boxart,
                                     m.createSessionForApp(index),
                                     runningId === model.appid)
            }

            onClicked: {
                appGrid.currentIndex = index
                appsRoot.focusLibrary()
                launchOrResumeSelectedApp(true)
            }
            Keys.onReturnPressed: launchOrResumeSelectedApp(true)
            Keys.onEnterPressed:  launchOrResumeSelectedApp(true)

            function doQuitGame() {
                quitAppDialog.appName = appGrid.appModel.getRunningAppName()
                quitAppDialog.boxArt = appGrid.appModel.getRunningAppBoxArt()
                quitAppDialog.segueToStream = false
                quitAppDialog.open()
            }
        }

        /*
         * On the left edge of the list, not the right.
         *
         * A ScrollBar sits at the trailing edge by default, which was fine when the list was
         * the whole page. With the library in the left column that edge lands in the middle
         * of the screen, between the two halves, where it reads as a divider rather than as
         * the scrollbar of anything. On the outside edge it separates nothing and stays
         * attached to the column it describes.
         *
         * ⚠️ mirrored, not a negative x: the handle has to grow from the correct side, and
         * LayoutMirroring on the bar alone flips it without touching the list's own layout.
         */
        ScrollBar.vertical: ScrollBar {
            parent: appGrid
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: -appsRoot._px(14)
            LayoutMirroring.enabled: true
        }
    }

    // ── Empty state ───────────────────────────────────────────────────────────
    Label {
        anchors.centerIn: parent
        width: parent.width * 0.6
        visible: appGrid.count === 0
        text: qsTr("This computer doesn't seem to have any applications or some applications are hidden")
        color: Theme.text2
        font.family: Theme.family
        font.pixelSize: appsRoot._px(20)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    // ── Dialogs ───────────────────────────────────────────────────────────────
    NavigableMessageDialog {
        id: quitAppDialog
        property string appName: ""
        property url boxArt: ""
        property bool segueToStream: false
        property string nextAppName: ""
        property string nextBoxArt: ""
        property int nextAppIndex: 0
        text: qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml")
            var params = {
                "appName": appName,
                "boxArt": boxArt,
                "quitRunningAppFn": function() { appGrid.appModel.quitRunningApp() },
                // A deliberate stop no longer puts the link back by itself — the question is
                // put once, on returning to the host list.
                "onQuitSucceededFn": function() {
                    if (appsRoot.appShell)
                        appsRoot.appShell.noteStreamEnded(appsRoot.computerIndex, appsRoot.hostName)
                }
            }
            if (segueToStream) {
                params.nextAppName = nextAppName
                params.nextBoxArt  = nextBoxArt
                params.nextSession = appGrid.appModel.createSessionForApp(nextAppIndex)
                // The same record the direct-launch path keeps. onQuitSucceededFn above is
                // deliberately not fired when a game follows — that is a swap, not the end of
                // the evening — so without this the session we are about to start would end
                // with nothing having noted that this host has a link to put back.
                params.nextSessionEndedFn = function() {
                    if (appsRoot.appShell)
                        appsRoot.appShell.noteStreamEnded(appsRoot.computerIndex, appsRoot.hostName)
                }
            } else {
                params.nextAppName = null
                params.nextSession = null
            }
            stackView.push(component.createObject(stackView, params))
        }

        onAccepted: quitApp()
        onClosed: appsRoot.focusLibrary()
    }

    AppSettingsDialog {
        id: appSettingsDialog
        onClosedByUser: appsRoot.focusLibrary()
    }
}
