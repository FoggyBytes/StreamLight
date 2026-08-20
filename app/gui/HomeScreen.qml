import Theme 1.0
import QtQuick 2.12
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2

import AppModel 1.0
import ComputerModel 1.0
import ComputerManager 1.0
import StreamingPreferences 1.0
import SdlGamepadKeyNavigation 1.0
import SystemProperties 1.0

/*
 * Home — the stage.
 *
 * The hosts are tabs under the wordmark and the host you picked gets the whole screen. What
 * this replaced was a grid of portrait tiles where every host cost three D-pad stops (the
 * tile, Profiles, Options), so reaching "Add a host" past two hosts took six. Here changing
 * host is a trigger, not a stop, and there are two navigation zones in total: the tab strip
 * and the action row.
 *
 * This file is the machine. It owns the model, the Windows-Update job, the link-restore
 * watch, the pairing PIN, the Tailscale pinning and every dialog — everything that has to
 * outlive a screen change, which is why the Home loader is the one AppShell never unloads.
 * Drawing the host is HostStage.qml's job and it does nothing else.
 */
FocusScope {
    id: homeScreen
    anchors.fill: parent
    focus: true

    property var appShell: null
    property ComputerModel computerModel: createModel()

    // ── Selection ────────────────────────────────────────────────────────────
    // One index across the whole strip: 0..hostCount-1 are hosts, hostCount is the
    // "Add a host" tab. Making the add panel the last tab is what removed it from the grid
    // — it is now reached the same way a host is, instead of being a tile you navigate past.
    property int tabIndex: 0
    readonly property int  hostCount: hostProbes.count
    readonly property bool addTabSelected: tabIndex >= hostCount

    // The host index, or -1 on the add tab. AppShell reads this to decide which host's
    // active profile greys out rows in Settings, and already guards on >= 0 — so the add
    // tab must report -1 rather than an index one past the end.
    readonly property int currentHostIndex: addTabSelected ? -1 : tabIndex

    // 0 = tab strip · 1 = action row. The app opens with the focus on the actions and
    // "Open" selected, because that is what a user came to press.
    property int focusZone: 1

    // The selected host's full record, pushed up by its probe below. One object rather than
    // a dozen bindings into a delegate: the stage needs all of it at once and nothing else
    // needs any of it.
    property var currentHost: null

    // (The trigger glyphs used to be resolved here. ActionHint does it now — vendor, size and
    // the keyboard alternative all in one place — so the strip just names the button.)

    // Tracks the last-used input device so the pad highlight and the mouse hover never
    // light up two different things at once.
    readonly property bool _pointerMode: SdlGamepadKeyNavigation.inputMode === "pointer"
    readonly property bool _keyMode:     SdlGamepadKeyNavigation.inputMode === "key"

    // Whether Tailscale is installed on THIS client (drives the greyed "Tailscale" host
    // option). Evaluated once — the client's install state rarely changes mid-run.
    readonly property bool _clientHasTailscale: computerModel ? computerModel.clientHasTailscale() : false

    // ── What the shell reads ─────────────────────────────────────────────────
    // Number of profiles on the selected host (0 on the add tab). Drives the LB/RB hints,
    // which hide when ≤ 1.
    readonly property int focusedProfileCount:
        (currentHost && currentHost.profileCount !== undefined) ? currentHost.profileCount : 0

    // Moves the selection along the tab strip (dir: -1 prev / +1 next). Public because the
    // trigger legend at the end of the strip is clickable, and a mouse has no triggers.
    function cycleHost(dir) {
        var next = tabIndex + dir
        if (next < 0 || next > hostCount) return
        tabIndex = next
    }

    // Cycles the selected host's active profile (dir: -1 prev / +1 next).
    // Root-level so AppShell can drive it wherever the focus happens to be, like the profile
    // cycle beside it. The tab itself lives on navRoot, which is where Qt delivers keys.
    function cycleHostTab(dir) {
        navRoot._selectTab(tabIndex + dir)
    }

    function cycleFocusedProfile(dir) {
        if (!computerModel || currentHostIndex < 0) return
        if (focusedProfileCount > 1)
            computerModel.cycleHostProfile(currentHostIndex, dir)
    }

    // Opens the POWER chooser for the selected host. Used by the status-bar "X Shutdown"
    // shortcut (mouse click + gamepad X). On a host that can't be powered off — offline,
    // unpaired, or the add tab — it still opens the chooser for THIS client, so the user
    // can always switch off the device they are holding.
    function openPowerForCurrent() {
        // Straight to the action rather than through the stage: Shutdown is no longer one of
        // the stage's buttons, and `runAction("power")` already falls back to the client-only
        // chooser when the host cannot be powered off.
        if (!addTabSelected && currentHost) { runAction("power"); return }
        openPowerClientOnly()
    }

    function openPowerClientOnly() {
        powerDialog.clientOnly         = true
        powerDialog.pcIndex            = -1
        powerDialog.hostName           = ""
        powerDialog.authState          = "none"   // Host & Both disabled; Client default
        powerDialog.clientUpdateState  = SystemProperties.updatesPending() ? "pending" : "none"
        powerDialog.hostUpdateState    = "unavailable"
        powerDialog.open()
    }

    // Re-asks every authorized host for its last session. Called by the shell when the client
    // returns to the host list: a session that just ended is the commonest reason to be here,
    // and the card would otherwise go on describing the one before it until the next restart.
    function refreshLastSession() {
        for (var i = 0; i < hostProbes.count; i++) {
            var p = hostProbes.itemAt(i)
            if (p && p.refreshLastSession) p.refreshLastSession()
        }
    }

    // Drops any "force Tailscale" session pin so the poller reverts to LAN-first.
    // Called when returning to the host list (after an apps/stream session).
    function clearTailscalePreferences() {
        if (computerModel) computerModel.clearTailscalePreferences()
    }

    // ── Wake and remote unlock ────────────────────────────────────────────────
    // Waking a host is no longer one magic packet and a shrug. Once it answers, it is very
    // likely sitting at a lock screen — Windows signs itself in after a shutdown and locks —
    // so the flow continues into the PIN pad and only calls the host ready once its link
    // speed has been matched. Every step is asked, never assumed: a host that is already
    // unlocked skips the pad, and a host that does not know LOCKSTATE skips the whole thing.
    property int    wakeIndex : -1
    property string wakeHostName : ""
    property int    wakeStep : 0        // mirrors WakeDialog's step rows
    property bool   wakeActive : false
    property string wakeDetail : ""

    // The pad is up and doing its own LOCKSTATE polling. Without this we would answer those
    // same replies here — wakeActive is still true — and start a second unlock session on
    // every poll that came back still locked.
    property bool   wakeUnlocking : false

    function startWake(idx, hostName) {
        wakeIndex    = idx
        wakeHostName = hostName
        wakeStep      = 0
        wakeDetail    = ""
        wakeActive    = true
        wakeUnlocking = false
        appListWaitTimer.tries = 0
        appListWaitTimer.stop()

        computerModel.wakeComputer(idx)
        wakeDialog.open()
        wakeWaitTimer.elapsed = 0
        wakeWaitTimer.restart()
    }

    // The host that just came all the way through, so its card can say so. Transient: on a
    // host that is simply online "ready" is not news, it is the normal state, and a chip that
    // never goes away stops being read.
    property int readyIndex : -1

    function _endWake(ready) {
        if (ready === true && wakeIndex >= 0) {
            readyIndex = wakeIndex
            readyTimer.restart()
        }
        wakeActive     = false
        wakeUnlocking  = false
        _waitingForQuit = false
        wakeIndex      = -1
        wakeWaitTimer.stop()
        appListWaitTimer.stop()
        quitWaitTimer.stop()
        wakeDialog.close()
    }

    Timer {
        id: readyTimer
        interval: 12000
        onTriggered: homeScreen.readyIndex = -1
    }

    Timer {
        id: appListWaitTimer
        property int tries : 0
        interval: 500
        onTriggered: if (homeScreen.wakeActive) homeScreen._wakeStartUnlock()
    }

    // ⚠️ The gate is StreamTweak *answering*, not a flag saying it once did.
    //
    // This used to wait for the host probe's auth state to read "authorized", which is only
    // ever written when a poll succeeds and is never cleared when the host goes away. After a
    // wake it therefore still held the value from before the shutdown, and the wait ended four
    // seconds after the host answered — when what was answering was the streaming server, not
    // StreamTweak, which takes about fifty seconds from boot to come up. The reply was empty,
    // read as "this host doesn't know the command", and the pad was skipped.
    //
    // Asking LOCKSTATE itself has no such gap: an empty reply means keep waiting, an answer
    // means StreamTweak is up, and the same call carries the thing we came for.
    function _wakeAskLockState() {
        if (wakeIndex < 0) return
        computerModel.requestLockState(wakeIndex)
    }

    function _wakeStartUnlock() {
        var comp = Qt.createComponent("StreamSegue.qml")
        if (comp.status !== Component.Ready) {
            console.warn("[unlock] StreamSegue.qml not ready:", comp.errorString())
            wakeDetail = qsTr("Could not open the PIN pad")
            return
        }

        // The Desktop app: the host has nothing else to offer while nobody is logged in, and
        // this session is only a carrier for keystrokes anyway.
        //
        // The list may not be here yet. The poller fetches it only once the host is online and
        // paired, while we get here as soon as StreamTweak approves us — a different timer, so
        // the two race. Waiting is right; giving up silently, which is what this did first, is
        // indistinguishable from the feature being broken.
        // showHidden = true: this is not the user browsing a library, it is us looking for one
        // specific entry. Someone who hid Desktop from their grid still needs it here.
        unlockAppModel.initialize(ComputerManager, wakeIndex, true)
        var appIndex = unlockAppModel.indexOfAppNamed("Desktop")
        if (appIndex < 0) {
            if (appListWaitTimer.tries < 20) {
                appListWaitTimer.tries++
                appListWaitTimer.restart()
                return
            }
            console.warn("[unlock] no Desktop app on this host after waiting for the app list")
            wakeDetail = qsTr("This host has no Desktop entry")
            return
        }

        var session = unlockAppModel.createSessionForApp(appIndex)
        if (!session) {
            console.warn("[unlock] could not create the session")
            wakeDetail = qsTr("Could not open the PIN pad")
            return
        }
        session.setUnlockMode(true)

        // Declared before the session exists, so the host has the mark in hand by the time
        // its streaming server reports a client.
        computerModel.markUnlockSession(wakeIndex, true)
        wakeUnlocking = true

        var segue = comp.createObject(stackView, {
            "appName":        "Desktop",
            "session":        session,
            "unlockMode":     true,
            "computerModel":  computerModel,
            "pcIndex":        wakeIndex,
            "hostName":       wakeHostName,
            "onUnlockResultFn": function (ok) { homeScreen._wakeUnlockDone(ok) }
        })
        if (Window.window) Window.window.markStreamLaunching()

        wakeDialog.close()
        stackView.push(segue)
    }

    function _wakeUnlockDone(ok) {
        wakeUnlocking = false
        if (!ok) { _endWake(); return }

        // Disconnecting is not enough. The streaming server keeps a session alive on purpose
        // so it can be resumed, so the host would go on reporting itself as streaming — and
        // opening it afterwards would show a session nobody asked for. Ask it to close the app.
        //
        // And wait for that to finish before touching the link: the host refuses to renegotiate
        // its adapter while a session is running, so matching now would be quietly discarded.
        console.log("[unlock] unlocked — closing the Desktop session on the host")
        _waitingForQuit = true
        quitWaitTimer.restart()
        unlockAppModel.quitRunningApp()
    }

    // Set while the host is being asked to close the unlock session.
    property bool _waitingForQuit : false

    function _quitSettled() {
        if (!_waitingForQuit) return
        _waitingForQuit = false
        quitWaitTimer.stop()
        _wakeMatchLink()
    }

    Timer {
        id: quitWaitTimer
        // Fallback only. The completion signal is the real trigger; this is here so a host that
        // never answers leaves the wake finished rather than hanging on the last step.
        interval: 8000
        onTriggered: {
            console.warn("[unlock] no confirmation that the session closed — carrying on")
            homeScreen._quitSettled()
        }
    }

    Connections {
        target: ComputerManager

        function onQuitAppCompleted(error) {
            if (error !== undefined && error !== null && String(error).length > 0)
                console.warn("[unlock] closing the session reported:", error)
            homeScreen._quitSettled()
        }
    }

    // The last step, and the reason the host card waits before saying it is ready: matching
    // the link takes about twenty-five seconds, and paying it here means not paying it when
    // a game is launched.
    function _wakeMatchLink() {
        if (wakeIndex < 0) { _endWake(); return }
        wakeStep = 3
        // The dialog's work is done: from here the host card carries it, so the last twenty
        // seconds are spent looking at the thing that will say "ready" rather than at a box
        // in front of it.
        wakeDialog.close()
        computerModel.matchHostLinkSpeed(wakeIndex)
    }

    Timer {
        id: wakeWaitTimer

        // Two minutes: a cold boot plus the wait for StreamTweak to come up (measured at
        // ~50 s after boot on a machine that signs itself in) fits comfortably, and beyond
        // that the host is not coming.
        // Two clocks, because they answer two different questions. `elapsed` is "is this host
        // ever going to boot?"; `onlineFor` is "is StreamTweak ever going to come up?" — and
        // the second cannot be judged before the first is answered. Measuring both from the
        // button is what made a wake give up nineteen seconds before the host had even
        // finished booting.
        property int elapsed : 0
        property int onlineFor : 0

        interval: 1000
        repeat: true
        onTriggered: {
            elapsed += 1
            if (!homeScreen.wakeActive || homeScreen.wakeIndex < 0) { stop(); return }

            var probe = hostProbes.itemAt(homeScreen.wakeIndex)
            if (!probe) return

            // The streaming server answering is not StreamTweak answering — it is up long
            // before, which is why the wait continues past this point.
            if (!probe.pOnline) {
                homeScreen.wakeStep = 1
                onlineFor = 0
                if (elapsed > 150) {
                    stop()
                    console.warn("[unlock] the host never came online — giving up")
                    homeScreen._endWake()
                }
                return
            }

            if (homeScreen.wakeStep < 2) homeScreen.wakeStep = 2
            onlineFor += 1

            // Two minutes for a host we have seen run StreamTweak before — worth waiting out,
            // since it takes the better part of a minute to come up after a boot. A host that
            // never has gets a fraction of that: there is nothing coming, and standing there
            // for two minutes is the feature pretending to work.
            // 60 and not 25 for an unknown host: StreamTweak takes the better part of a minute
            // to come up after a boot, and on the very first wake after installing StreamLight
            // there is nothing remembered yet. Of the two ways to be wrong, waiting a minute on
            // a plain Moonlight host is a nuisance; skipping the pad on a host that does have
            // StreamTweak looks like the feature is broken.
            var cap = homeScreen.computerModel.hostEverHadStreamTweak(homeScreen.wakeIndex)
                      ? 120 : 60
            if (onlineFor > cap) {
                stop()
                console.warn("[unlock] StreamTweak never answered in " + cap
                             + "s of the host being online — carrying on without the pad")
                homeScreen._wakeMatchLink()
                return
            }

            // Every other tick: one in flight at a time is plenty, and the reply itself is
            // what ends this wait.
            if (onlineFor % 2 === 0) homeScreen._wakeAskLockState()
        }
        onRunningChanged: if (running) { elapsed = 0; onlineFor = 0 }
    }

    Connections {
        target: computerModel

        function onLockStateReceived(index, supported, locked) {
            if (!homeScreen.wakeActive || homeScreen.wakeUnlocking) return
            if (index !== homeScreen.wakeIndex) return
            // No answer yet: StreamTweak is still coming up, or this host does not know the
            // command. The two look identical from here, so we keep asking until the wait
            // times out — and a host that genuinely never answers ends up where it belongs,
            // at the link match, without ever offering a pad it cannot back up.
            if (!supported) return

            console.log("[unlock] LOCKSTATE answered: locked=" + locked)
            wakeWaitTimer.stop()
            if (locked) homeScreen._wakeStartUnlock()
            else        homeScreen._wakeMatchLink()
        }

        function onLinkMatchProgress(index, running, detail) {
            if (!homeScreen.wakeActive || index !== homeScreen.wakeIndex) return
            homeScreen.wakeDetail = detail
            if (!running) homeScreen._endWake(true)
        }
    }

    WakeDialog {
        id: wakeDialog
        hostName: homeScreen.wakeHostName
        step: homeScreen.wakeStep
        detail: homeScreen.wakeDetail
        onCancelled: homeScreen._endWake()
    }

    // Only ever used to find the Desktop app for an unlock. Initialised on demand so a
    // normal session never pays for it.
    AppModel {
        id: unlockAppModel
    }

    // ── Host link restore watch ───────────────────────────────────────────────
    // Lives here, like the update job, because HomeScreen is always loaded: the watch has to
    // outlive the Apps screen we were on when the session was stopped. Purely informational —
    // it never blocks anything, it just answers "can I switch the host off yet?", since putting
    // the link back takes about twenty seconds and used to happen with no sign of it at all.
    property bool   linkRestoreActive: false      // a watch is running
    property bool   linkRestoreVisible: false     // …and there is something worth showing
    property int    linkRestoreHostIndex: -1
    property string linkRestoreHostName: ""
    property bool   linkRestoreDone: false
    property string linkRestoreSpeed: ""

    // Is the host currently on screen the one being restored? The watch follows a host index,
    // the card shows whichever tab is selected, and only their intersection should say anything.
    readonly property bool _restoringHere: linkRestoreVisible && !linkRestoreDone
                                           && tabIndex === linkRestoreHostIndex
    readonly property bool _restoredHere:  linkRestoreVisible && linkRestoreDone
                                           && tabIndex === linkRestoreHostIndex

    // The user said yes. Show the chip as soon as the host confirms there is a switch to undo
    // — not before. Announcing it the moment we ask meant claiming "link restored" after a
    // session on a host that had never switched at all.
    function startLinkRestoreWatch(idx, hostName) {
        if (!_armLinkRestoreWatch(idx, hostName, 90000)) return
        computerModel.restoreHostLink(idx)
    }

    // ── "Shall I put the host's link back?" ───────────────────────────────────
    // The host no longer decides this for itself: it holds the streaming speed until asked.
    // So a session ending only *records* that this host may have something to put back, and
    // the question is put on the way back to the host list — once per session, never while
    // one is still running.
    property int    linkAskHostIndex: -1
    property string linkAskHostName: ""
    property bool   linkAskProbing: false

    function noteStreamEnded(idx, hostName) {
        if (idx < 0) return
        linkAskHostIndex = idx
        linkAskHostName  = hostName
    }

    // Arriving on the host list from a host page. Ask the host what state it is actually in
    // before putting a question on screen — "switched" is the only thing that makes the
    // question meaningful, and a session still running means it is not over yet.
    function maybeAskLinkRestore() {
        if (linkAskHostIndex < 0 || linkRestoreActive || linkAskProbing) return
        linkAskProbing = true
        linkAskProbeTimer.restart()
        computerModel.requestHostNetInfo(linkAskHostIndex)
    }

    function _resolveLinkAsk(info) {
        linkAskProbing = false
        linkAskProbeTimer.stop()

        var idx  = linkAskHostIndex
        var name = linkAskHostName
        // Asked once per session either way: whether they say yes, say no, or the host turns
        // out to have nothing to put back, the question is spent until the next session.
        linkAskHostIndex = -1
        linkAskHostName  = ""

        if (info.switched !== true) return          // nothing was ever switched
        if (info.sessionActive === true) return     // still streaming, or paused and resumable

        linkRestoreDialog.hostName = name
        linkRestoreDialog.pcIndex  = idx
        linkRestoreDialog.open()
    }

    Timer {
        // The host didn't answer in time — off, busy, momentarily unreachable. Only the probe
        // is abandoned, not the question: it stays armed for the next time the user comes back
        // to the host list. Spending it here would lose the prompt for that whole session over
        // four seconds of silence from a host that was streaming a moment earlier.
        id: linkAskProbeTimer
        interval: 4000
        onTriggered: homeScreen.linkAskProbing = false
    }

    function _armLinkRestoreWatch(idx, hostName, giveUpMs) {
        if (idx < 0) return false
        // ⚠️ Stop the previous confirmation before starting: it clears the whole watch when it
        // fires, so left running it would tear down the one being armed here a few seconds in.
        linkRestoreDoneTimer.stop()
        linkRestoreHostIndex = idx
        linkRestoreHostName  = hostName
        linkRestoreDone      = false
        linkRestoreVisible   = false
        linkRestoreSpeed     = ""
        linkRestoreActive    = true
        linkRestoreGiveUpTimer.interval = giveUpMs
        linkRestoreGiveUpTimer.restart()
        // No poller of its own. The per-host probe already asks every authorized host for
        // NETINFO every two seconds and the reply reaches the same handler, so a second timer
        // on the same host for the same data bought nothing. This one ask is just to avoid
        // waiting out that interval: on a host that never switched it is what ends the watch
        // quietly before anything is shown.
        computerModel.requestHostNetInfo(idx)
        return true
    }

    function _clearLinkRestoreWatch() {
        linkRestoreGiveUpTimer.stop()
        linkRestoreDoneTimer.stop()
        linkRestoreActive   = false
        linkRestoreVisible  = false
        linkRestoreDone     = false
        linkRestoreHostIndex = -1
    }

    Timer {
        // The host may be off, unreachable, or older than 8.1.0. Never leave the chip up forever.
        id: linkRestoreGiveUpTimer
        interval: 90000
        onTriggered: homeScreen._clearLinkRestoreWatch()
    }

    Timer {
        // Let the "back to X" confirmation linger long enough to be read, then clear.
        id: linkRestoreDoneTimer
        interval: 6000
        onTriggered: homeScreen._clearLinkRestoreWatch()
    }

    Connections {
        target: computerModel
        function onHostNetInfoReceived(idx, info) {
            // The one-shot probe behind the "put the link back?" question, asked on arriving
            // here from a host page. An empty reply — host off, or older than 8.1.0 — resolves
            // to "don't ask", which is the safe reading: never invent a question about a host
            // that has not said it is switched.
            if (homeScreen.linkAskProbing && idx === homeScreen.linkAskHostIndex) {
                homeScreen._resolveLinkAsk(info)
                return
            }

            if (!homeScreen.linkRestoreActive || homeScreen.linkRestoreDone) return
            if (idx !== homeScreen.linkRestoreHostIndex) return
            // An empty map means the host didn't answer — mid-renegotiation it is unreachable,
            // which is the feature working, so keep waiting rather than declaring anything.
            if (info.switched === undefined) return

            // Every watch is now an explicit one — the user answered a prompt, so there is
            // something to put back and it is about to happen. switched=true is enough;
            // state=changing is kept as the second way in, for the seconds when the adapter
            // is already down and the host cannot answer with anything else.
            if (!homeScreen.linkRestoreVisible &&
                (info.switched === true || info.state === "changing")) {
                homeScreen.linkRestoreVisible = true
                linkRestoreGiveUpTimer.interval = 90000
                linkRestoreGiveUpTimer.restart()
            }

            if (info.switched === false && info.state === "idle") {
                // Nothing was switched to begin with: end the silent watch without a word.
                if (!homeScreen.linkRestoreVisible) { homeScreen._clearLinkRestoreWatch(); return }
                homeScreen.linkRestoreSpeed = homeScreen.formatMbpsShort(info.currentMbps)
                homeScreen.linkRestoreDone  = true
                linkRestoreGiveUpTimer.stop()
                linkRestoreDoneTimer.restart()
            }
        }
    }

    function formatMbpsShort(mbps) {
        if (!mbps || mbps <= 0) return ""
        if (mbps >= 1000) {
            var gbps = mbps / 1000
            return (gbps === Math.floor(gbps) ? gbps.toFixed(0) : gbps.toFixed(1)) + " Gbps"
        }
        return mbps + " Mbps"
    }

    // ── Remote "Update host" job state (one at a time, survives popup close) ───
    property bool   updateJobActive: false
    property int    updateJobHostIndex: -1
    property string updateJobHostName: ""
    property string updateJobPhase: "IDLE"
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
        updateJobPhase = "CHECKING"
        updateJobMessage = ""; updateJobError = ""
        updateJobUpdates = []; updateJobCounts = ({})
        computerModel.startUpdateCheck(index)
        updatePollTimer.restart()
        openUpdateDialog()
    }
    function startUpdateInstall(scope) {
        _updateInstallStarted = true
        updateJobPhase = "DOWNLOADING"
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
        ComputerManager.computerAddCompleted.connect(addComplete)
    }

    Component.onDestruction: {
        ComputerManager.computerAddCompleted.disconnect(addComplete)
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

    // Per-host colour pair — deterministic by host name hash. Feeds the stage backdrop, and
    // is the fallback for as long as a host has no picture of its own.
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

    function formatStreamTweakStatus(raw) {
        if (raw === "" || raw === null || raw === undefined) return ""
        var mbps = parseInt(raw)
        if (isNaN(mbps)) return raw
        return formatMbpsShort(mbps)
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Host probes — one per host, no pixels
    // ═════════════════════════════════════════════════════════════════════════
    /*
     * Each host keeps its own StreamTweak conversation running exactly as the old tile did:
     * a NIC-speed poll every 2 s and an access poll every 2.5 s until it settles. That has
     * to stay per-host and not just for the selected one, or a host whose approval is
     * pending would never raise its PIN popup until you happened to tab to it.
     *
     * The selected probe pushes its whole record up to `currentHost`. Everything the stage
     * and the dialogs read comes from there, which is why nothing below needs to reach into
     * a delegate to find out what host it is talking about.
     */
    Repeater {
        id: hostProbes
        model: computerModel

        delegate: Item {
            id: probe
            visible: false

            readonly property bool isCurrent: index === homeScreen.tabIndex

            // Plain bindings onto the roles the tab strip needs. The strip reads these off
            // the probe object rather than calling record(): a function result is a snapshot
            // and would never update, whereas these are real bindings that notify.
            readonly property string pName:    model.name
            readonly property bool   pOnline:  model.online
            readonly property bool   pPaired:  model.paired
            readonly property bool   pUnknown: model.statusUnknown

            property string stAuth: ""
            property bool   stLinkChanging: false
            property double stLinkChangedAt: 0
            property string stSpeedRaw: ""
            property bool   stAllowsLink: false
            property bool   stSwitched: false
            property int    stLocalMbps: 0

            // The host refuses SETSPEED while it considers a session to be running, and it goes on
            // counting one for its whole inactivity grace after the client disconnects. So this
            // is not only "a stream is live": for about half a minute after the last session it
            // is also "the grace timer has not fired yet". Either way the host will decline, and
            // the card must not promise a change that will not happen. Defaults false so a host
            // that never answers behaves exactly as before.
            property bool   stSessionActive: false

            // The host's last finished session, as StreamTweak reports it. Empty map until it
            // answers, {has:false} on a host that does not know the command — so the panel is
            // absent rather than empty on anything that cannot fill it.
            property var    stLastSession: ({})

            // This device's wired link to *this* host (0 when Wi-Fi/Tailscale/unknown) versus
            // the host's own. Both are needed before claiming a switch is coming: promising
            // one the host would refuse is worse than saying nothing.
            readonly property int stHostMbps: {
                var m = parseInt(stSpeedRaw)
                return isNaN(m) ? 0 : m
            }
            // ⚠️ The EFFECTIVE setting, not the global one. A per-host profile can turn link
            // matching off (AppOverride.matchlink), and the card was reading the global toggle
            // straight — so a profile with matching disabled still announced a change that
            // LinkMatcher would then correctly decline to make. Same class of bug as the one
            // fixed in LinkMatcher itself (§38): the cascade is built into a cloned prefs
            // object, and anything reading the singleton is reading the bottom of it.
            readonly property var stOverride: {
                model.activeProfileSlot;   // re-resolve when the active profile changes
                model.activeProfileName;
                return homeScreen.computerModel
                       ? homeScreen.computerModel.hostActiveOverride(index) : ({})
            }
            readonly property bool stMatchLink:
                (stOverride && stOverride.matchlink !== undefined)
                    ? stOverride.matchlink === true
                    : StreamingPreferences.matchHostLinkSpeed

            readonly property bool willSwitchLink:
                stMatchLink && stAllowsLink && !stSessionActive
                && stLocalMbps > 0 && stHostMbps > stLocalMbps

            // The mirror case: this host is faster than our link, so matching *would* help,
            // but the host isn't offering it. Without this the client toggle looks enabled
            // and simply does nothing, with the reason buried in the log.
            //
            // ⚠️ Also suppressed while the host counts a session, and that is not the same
            // situation: there the host does offer the change and is merely busy, so saying
            // "enable it in StreamTweak" would send the user to a setting that is already on.
            // Nothing is a better answer than the wrong instruction for a state that clears
            // itself within the grace window.
            readonly property bool cantSwitchLink:
                stMatchLink && !stAllowsLink && !stSessionActive
                && stLocalMbps > 0 && stHostMbps > stLocalMbps

            function record() {
                return {
                    index:             index,
                    name:              model.name,
                    online:            model.online,
                    paired:            model.paired,
                    busy:              model.busy,
                    statusUnknown:     model.statusUnknown,
                    wakeable:          model.wakeable,
                    serverSupported:   model.serverSupported,
                    details:           model.details,
                    address:           model.address,
                    physicalAddress:   model.physicalAddress,
                    tailscaleAddress:  model.tailscaleAddress,
                    hasTailscale:      model.hasTailscale,
                    tailscaleActive:   model.tailscaleActive,
                    isTailscaleClone:  model.isTailscaleClone,
                    gpuModel:          model.gpuModel,
                    profileCount:      model.profileCount,
                    activeProfileSlot: model.activeProfileSlot,
                    activeProfileName: model.activeProfileName,
                    // The active profile's override map, so the stage can show the settings
                    // the next launch would actually use. stOverride re-resolves on
                    // activeProfileSlot, so cycling profiles with LB/RB moves the badges.
                    streamOverride:    stOverride,
                    stageColorFrom:    model.stageColorFrom,
                    stageColorTo:      model.stageColorTo,
                    stageImage:        model.stageImage,
                    stageSeedColor:    model.stageSeed,
                    auth:              stAuth,
                    linkText:          homeScreen.formatStreamTweakStatus(stSpeedRaw),
                    willSwitchLink:    willSwitchLink,
                    cantSwitchLink:    cantSwitchLink,
                    localMbps:         stLocalMbps,
                    linkChanging:      stLinkChanging,
                    linkSwitched:      stSwitched,
                    allowsLinkControl: stAllowsLink,
                    // The EFFECTIVE setting (profile over global), not the singleton — see the
                    // note on stMatchLink. The Options tile has to know it or it offers an
                    // action LinkMatcher will decline without a word.
                    matchLink:         stMatchLink,
                    sessionActive:     stSessionActive,
                    lastSession:       stLastSession
                }
            }

            // Re-asks for the last session. Called when the client comes back to the host list,
            // because the usual reason for that is a session having just ended.
            function refreshLastSession() {
                if (stAuth === "authorized")
                    homeScreen.computerModel.requestLastSession(index)
            }

            function push() { if (isCurrent) homeScreen.currentHost = record() }

            // An array binding re-evaluates when any element does, and yields a new object
            // each time, so this fires on every model role change without needing a handler
            // per role. It is the cheapest way to keep one plain record in step with a model.
            readonly property var _watch: [
                model.name, model.online, model.paired, model.busy, model.statusUnknown,
                model.wakeable, model.serverSupported, model.address, model.physicalAddress,
                model.tailscaleAddress, model.hasTailscale, model.tailscaleActive,
                model.gpuModel, model.profileCount, model.activeProfileSlot,
                model.activeProfileName, model.stageColorFrom, model.stageColorTo,
                model.stageImage, model.stageSeed, stAuth, stSpeedRaw,
                willSwitchLink, cantSwitchLink, stLastSession, stMatchLink,
                stLinkChanging, stSwitched, stAllowsLink, stSessionActive
            ]
            on_WatchChanged: push()
            onIsCurrentChanged: push()
            Component.onCompleted: {
                push()
                if (model.online && model.paired) {
                    homeScreen.computerModel.requestStreamTweakStatus(index)
                    homeScreen.computerModel.requestStreamTweakAuth(index)
                }
            }

            Timer {
                interval: 2000
                repeat: true
                running: model.online && model.paired
                onTriggered: homeScreen.computerModel.requestStreamTweakStatus(index)
            }

            // NETINFO alongside STATUS, because STATUS returns a number and nothing else: while
            // the adapter renegotiates that number goes unknown and then changes, and every
            // reading derived from it — the speed, the "on launch" promise — flickered with it.
            // NETINFO carries the host's own view of what it is doing, so the card can say
            // "changing" and hold still instead of narrating each intermediate value.
            Timer {
                interval: 2000
                repeat: true
                running: model.online && model.paired && probe.stAuth === "authorized"
                onTriggered: homeScreen.computerModel.requestHostNetInfo(index)
            }

            // Poll the access state until it settles (authorized, or "open" when the host
            // doesn't enforce auth). A later switch to enforced auth is still caught via the
            // STATUS ERR_UNAUTHORIZED path.
            Timer {
                interval: 2500
                repeat: true
                running: model.online && model.paired
                         && probe.stAuth !== "authorized" && probe.stAuth !== "open"
                onTriggered: homeScreen.computerModel.requestStreamTweakAuth(index)
            }

            Connections {
                target: computerModel

                function onStreamTweakStatusReceived(idx, status) {
                    // Held while the adapter is renegotiating: the values it returns in that
                    // window are the transition, not the answer.
                    if (idx === index && !probe.stLinkChanging) probe.stSpeedRaw = status
                }

                function onHostNetInfoReceived(idx, info) {
                    if (idx !== index) return

                    if (info.allowsLinkControl !== undefined)
                        probe.stAllowsLink = info.allowsLinkControl === true

                    // Drives the Options tile: "Restore NIC speed" when the host is sitting on
                    // a speed a client asked for, "Match link speed" when it is on its own.
                    if (info.switched !== undefined)
                        probe.stSwitched = info.switched === true

                    // Guarded like the two above: an empty reply is the host mid-change, not
                    // a host telling us no session is running.
                    if (info.sessionActive !== undefined)
                        probe.stSessionActive = info.sessionActive === true

                    // ⚠️ An empty reply here means the opposite of what it means everywhere
                    // else in this app. While the adapter is down the host is unreachable, so
                    // silence during a change IS the change — but only silence that follows a
                    // "changing" we actually saw, and only for as long as the cap allows.
                    if (info.state === undefined) {
                        if (probe.stLinkChanging
                            && Date.now() - probe.stLinkChangedAt > 60000) {
                            probe.stLinkChanging = false
                        }
                        return
                    }

                    if (info.state === "changing") {
                        if (!probe.stLinkChanging) probe.stLinkChangedAt = Date.now()
                        probe.stLinkChanging = true
                        return
                    }

                    probe.stLinkChanging = false
                    if (info.currentMbps !== undefined && info.currentMbps > 0)
                        probe.stSpeedRaw = String(info.currentMbps)
                }

                function onLastSessionReceived(idx, s) {
                    if (idx === index) probe.stLastSession = s
                }

                function onStreamTweakAuthReceived(idx, state, pin) {
                    if (idx !== index) return
                    probe.stAuth = state
                    // Once authorized, learn the host's Tailscale endpoint (if any) and
                    // measure the link we reach it on — the speed the host will be asked to
                    // match before a stream starts.
                    if (state === "authorized") {
                        homeScreen.computerModel.refreshTailscale(index)
                        var link = homeScreen.computerModel.probeLocalLink(index)
                        probe.stLocalMbps = link.usable === true ? link.mbps : 0
                        homeScreen.computerModel.requestHostNetInfo(index)
                        homeScreen.computerModel.requestLastSession(index)
                    }
                    if (state === "pending" && pin.length > 0)
                        homeScreen.stShowPin(index, model.name, model.address, pin)
                    else
                        homeScreen.stHidePin(index)
                }
            }
        }
    }

    // Keep the selection inside the strip as hosts come and go.
    //
    // Discovering the first host needs no special case, which is the quiet advantage of the
    // add panel being the last tab rather than a tile: with no hosts, index 0 IS the add tab,
    // and the moment a host appears index 0 becomes that host. Someone staring at an empty
    // screen is moved onto the host that just showed up without anything having to decide it.
    onHostCountChanged: {
        if (tabIndex > hostCount) tabIndex = hostCount
        if (addTabSelected) currentHost = null
    }

    onTabIndexChanged: if (addTabSelected) currentHost = null

    // ═════════════════════════════════════════════════════════════════════════
    // Navigation
    // ═════════════════════════════════════════════════════════════════════════
    /*
     * Two zones, one row each, and the triggers cut across both.
     *
     * The keys live on a full-size focus sink behind the visuals rather than on the root
     * FocusScope, mirroring what the old grid did: the scope hands active focus to its
     * `focus: true` child, and that child is where Qt delivers the key events.
     */
    Item {
        id: navRoot
        anchors.fill: parent
        focus: true

        function _selectTab(i) {
            if (i < 0 || i > homeScreen.hostCount) return false
            homeScreen.tabIndex = i
            return true
        }

        // LT / RT — switch host from anywhere on Home. They are the reason changing host
        // stopped costing a D-pad stop. LB/RB stay on prev/next profile, which is exactly
        // why the hosts had to go on the triggers.
        // ⚠️ Handled here, on the page, rather than as global Shortcuts. Key events reach the
        // focused item first, so a host-name field or the add-host dialog swallows the letter
        // and typing "Peppe" cannot shut a machine down. A global Shortcut would fire through
        // any text field, which is the reason single-key shortcuts are usually avoided at all.
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_F14) {
                _selectTab(homeScreen.tabIndex - 1)
                event.accepted = true
            } else if (event.key === Qt.Key_F15) {
                _selectTab(homeScreen.tabIndex + 1)
                event.accepted = true
            }
            // Keyboard equivalents of the controller prompts. Q/E mirror LB/RB (profile);
            // PgUp/PgDn mirror LT/RT (host) and are handled in AppShell so they work wherever
            // the focus is, exactly like the triggers do.
            else if (event.key === Qt.Key_Q) {
                homeScreen.cycleFocusedProfile(-1); event.accepted = true
            } else if (event.key === Qt.Key_E) {
                homeScreen.cycleFocusedProfile(1);  event.accepted = true
            } else if (event.key === Qt.Key_P) {
                homeScreen.openPowerForCurrent(); event.accepted = true
            } else if (event.key === Qt.Key_S) {
                if (appShell) appShell.openSettings(); event.accepted = true
            }
        }

        Keys.onLeftPressed: function(event) {
            if (homeScreen.focusZone === 0) _selectTab(homeScreen.tabIndex - 1)
            else                            hostStage.moveAction(-1)
            event.accepted = true
        }
        Keys.onRightPressed: function(event) {
            if (homeScreen.focusZone === 0) _selectTab(homeScreen.tabIndex + 1)
            else                            hostStage.moveAction(1)
            event.accepted = true
        }
        Keys.onUpPressed: function(event) {
            homeScreen.focusZone = 0
            event.accepted = true
        }
        Keys.onDownPressed: function(event) {
            homeScreen.focusZone = 1
            event.accepted = true
        }

        // A — activate. On the tab strip it means "take me to this host's actions", which is
        // the only sensible reading of pressing A on something already selected.
        function _activate() {
            if (homeScreen.focusZone === 0) homeScreen.focusZone = 1
            else                            hostStage.activateFocused()
        }
        Keys.onReturnPressed: function(event) { _activate(); event.accepted = true }
        Keys.onEnterPressed:  function(event) { _activate(); event.accepted = true }
        Keys.onSpacePressed:  function(event) { _activate(); event.accepted = true }

        // X — Shut down, wherever the focus is. Y — Settings. Both keep the same meaning on
        // every screen so the destructive one always lives in the same place.
        Keys.onMenuPressed:   function(event) { homeScreen.openPowerForCurrent();        event.accepted = true }
        Keys.onHangupPressed: function(event) { if (appShell) appShell.openSettings();   event.accepted = true }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Brand
    // ═════════════════════════════════════════════════════════════════════════
    Item {
        id: brandRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 44
        anchors.rightMargin: 44
        /*
         * 22 and 60 are the clock's own top and height, not free numbers.
         *
         * The right corner is one strong figure with a quiet line under it; this is the same
         * object mirrored, built from the same two sizes — 30 over 13, four pixels apart, which
         * is the cluster's own spacing. So the two ends of the row open at y22 and close at y82
         * together and read as one band rather than two blocks sharing a row.
         *
         * ⚠️ The wordmark used to be 23 against the clock's 30, which put the app's own name
         * below a piece of chrome. It is levelled rather than raised past it: growing it further
         * only moves the same argument to the other corner. The tracking is not multiplied with
         * the size — 3.2 at 23 is 0.14em, and large type wants proportionally less, so 30 takes
         * 3.6 and not the 4.2 the arithmetic would give.
         *
         * Keep this block 60 tall. The clock is the shell's now, pinned once at AppShell's
         * `topMargin: 22` for all three screens, so a brand block that matches its height is one
         * number nobody has to re-tune here later.
         */
        anchors.topMargin: 22
        height: 60

        Image {
            id: brandIcon
            source: "qrc:/streamlight.ico"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 46; height: 46
            // Rasterise at device-pixel resolution so HiDPI displays get a sharp image
            // regardless of Windows scaling (100% / 150% / 200%).
            sourceSize.width:  46 * Screen.devicePixelRatio
            sourceSize.height: 46 * Screen.devicePixelRatio
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }

        Column {
            anchors.left: brandIcon.right
            anchors.leftMargin: 14
            anchors.verticalCenter: brandIcon.verticalCenter
            // The cluster's own spacing, so the gap under the wordmark matches the gap under
            // the clock opposite it.
            spacing: 4

            // Uniform wordmark: "STREAMLIGHT" all caps, single size, Black weight, wide
            // letter-spacing. Avoids the optical-weight mismatch of synthesised small-caps.
            Label {
                text: "STREAMLIGHT"
                font.family: Theme.family
                font.pixelSize: 30
                font.weight: Font.Black
                font.bold: true
                font.letterSpacing: 3.6
                color: Theme.text
            }

            Label {
                text: qsTr("a Moonlight fork")
                font.family: Theme.family
                font.pixelSize: 13
                color: Theme.text3
            }
        }

        // (The clock, the date and the battery used to close this row. They are the shell's
        //  now — see StatusCluster in AppShell — so that they land in the same corner here,
        //  on the host page and in Settings instead of shifting with each header.)
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Tab strip — zone 1
    // ═════════════════════════════════════════════════════════════════════════
    Item {
        id: tabStrip
        anchors.top: brandRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 18
        anchors.leftMargin: 44
        anchors.rightMargin: 44
        height: 44

        /*
         * The strip, with its triggers as its two ends.
         *
         * The legend used to be a block at the far right of this row, which made it one of two
         * floating captions on the screen — this one and the profile shoulders on the card,
         * neither aligned to the other. Sitting at the ends of the strip they move along, they
         * need no caption and there is nothing left to align: the row itself is the label.
         */
        Row {
            id: stripRow
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14

            // One host is enough: the strip always carries "Add a host" as its last tab, so
            // there are still two stops and the triggers do move. Requiring two would hide
            // them exactly when someone is looking for how to add another.
            Repeater {
                model: homeScreen.hostCount >= 1
                       ? [{ btn: "LT", key: "PgUp", dir: -1 }] : []
                delegate: Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44; height: 30
                    ActionHint {
                        anchors.centerIn: parent
                        buttonKey: modelData.btn
                        keyLabel:  modelData.key
                        size: 28
                        opacity: ltMouse.containsMouse ? 1.0 : 0.85
                    }
                    // Clickable, because a mouse has no triggers and the strip must not become
                    // the one control that needs a pad.
                    MouseArea {
                        id: ltMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: homeScreen.cycleHost(modelData.dir)
                    }
                }
            }

        Row {
            id: tabRow
            spacing: 8

            Repeater {
                model: hostProbes.count + 1     // hosts, then "Add a host"

                delegate: Rectangle {
                    id: tabItem

                    readonly property bool _isAdd:    index >= hostProbes.count
                    readonly property var  _probe:    _isAdd ? null : hostProbes.itemAt(index)
                    readonly property bool _selected: index === homeScreen.tabIndex
                    readonly property bool _focused:
                        _selected && homeScreen.focusZone === 0 && !homeScreen._pointerMode
                    readonly property bool _hovered:  tabMouse.containsMouse && homeScreen._pointerMode

                    height: 40
                    width: tabContent.implicitWidth + 40
                    radius: 9
                    color: _selected || _hovered ? Theme.cardHigh : "transparent"
                    border.width: _focused ? 2 : 1
                    border.color: _focused  ? Theme.accent
                                : _selected ? Theme.lineHigh
                                :             "transparent"

                    Row {
                        id: tabContent
                        anchors.centerIn: parent
                        spacing: 10

                        // Connectivity at a glance, so a strip of five hosts says which ones
                        // are up without selecting each in turn.
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !tabItem._isAdd
                            width: 8; height: 8
                            radius: 4
                            color: !tabItem._probe          ? Theme.offline
                                 : tabItem._probe.pUnknown  ? Theme.text3
                                 : tabItem._probe.pOnline && tabItem._probe.pPaired ? Theme.online
                                 : tabItem._probe.pOnline   ? Theme.warning
                                 :                            Theme.offline
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: tabItem._isAdd ? "+  " + qsTr("Add a host")
                                                 : (tabItem._probe ? tabItem._probe.pName.toUpperCase() : "")
                            color: tabItem._selected ? Theme.text : Theme.text3
                            font.family: Theme.family
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                    }

                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            homeScreen.tabIndex = index
                            homeScreen.focusZone = 0
                            navRoot.forceActiveFocus()
                        }
                    }
                }
            }
        }

            Repeater {
                model: homeScreen.hostCount >= 1
                       ? [{ btn: "RT", key: "PgDn", dir: 1 }] : []
                delegate: Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44; height: 30
                    ActionHint {
                        anchors.centerIn: parent
                        buttonKey: modelData.btn
                        keyLabel:  modelData.key
                        size: 28
                        opacity: rtMouse.containsMouse ? 1.0 : 0.85
                    }
                    MouseArea {
                        id: rtMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: homeScreen.cycleHost(modelData.dir)
                    }
                }
            }
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // The stage — zone 2
    // ═════════════════════════════════════════════════════════════════════════
    HostStage {
        id: hostStage
        anchors.top: tabStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: 14
        anchors.leftMargin: 44
        anchors.rightMargin: 44
        anchors.bottomMargin: 18

        readonly property var _h: homeScreen.currentHost

        addMode:     homeScreen.addTabSelected
        discovering: StreamingPreferences.enableMdns

        hostName:          _h ? _h.name              : ""
        online:            _h ? _h.online            : false
        paired:            _h ? _h.paired            : false
        statusUnknown:     _h ? _h.statusUnknown     : false
        wakeable:          _h ? _h.wakeable          : false
        serverSupported:   _h ? _h.serverSupported   : true
        hasTailscale:      _h ? _h.hasTailscale      : false
        tailscaleActive:   _h ? _h.tailscaleActive   : false
        tailscaleAddress:  _h ? _h.tailscaleAddress  : ""
        gpuModel:          _h ? _h.gpuModel          : ""
        profileCount:      _h ? _h.profileCount      : 0
        activeProfileSlot: _h ? _h.activeProfileSlot : -1
        activeProfileName: _h ? _h.activeProfileName : ""
        streamOverride:    (_h && _h.streamOverride) ? _h.streamOverride : ({})
        authState:         _h ? _h.auth              : ""
        hostLinkText:      _h ? _h.linkText          : ""
        willSwitchLink:    _h ? _h.willSwitchLink    : false
        cantSwitchLink:    _h ? _h.cantSwitchLink    : false
        linkSwitchText:    _h ? homeScreen.formatMbpsShort(_h.localMbps) : ""

        // Either the host says it is renegotiating, or the wake flow knows it asked for one.
        // The host's own word covers every other path — a launch, a restore, another client —
        // which is what used to make the numbers flicker with nothing to explain them.
        linkChanging:      (_h && _h.linkChanging === true)
                           || (homeScreen.wakeActive && homeScreen.wakeStep === 3
                               && homeScreen.tabIndex === homeScreen.wakeIndex)
        // A restore is the same mechanism as a match and looks identical from the card, so it
        // borrows the same two states. Without this the restore the user just asked for showed
        // an amber chip and then simply stopped, never saying it had finished — the one thing
        // they were waiting to be told, and the reason to watch it at all.
        linkChangeLabel:   homeScreen._restoringHere ? qsTr("Restoring") : qsTr("Matching")
        linkChangeText:    homeScreen._restoringHere ? qsTr("putting the link back")
                                                     : homeScreen.wakeDetail
        justReady:         (homeScreen.readyIndex >= 0
                            && homeScreen.tabIndex === homeScreen.readyIndex)
                           || homeScreen._restoredHere
        justReadyText:     homeScreen._restoredHere && homeScreen.linkRestoreSpeed.length > 0
                               ? qsTr("Link back to %1").arg(homeScreen.linkRestoreSpeed)
                               : qsTr("Ready")
        hideAddresses:     StreamingPreferences.hideHostIps
        lastSession:       (_h && _h.lastSession) ? _h.lastSession : ({})

        // The physical (LAN) address stays the headline even when we are reaching the host
        // over Tailscale — the 100.x one gets its own field rather than replacing it.
        address: _h ? (_h.hasTailscale
                       ? (_h.physicalAddress && _h.physicalAddress.length ? _h.physicalAddress : _h.address)
                       : _h.address)
                    : ""

        // What the user picked for this host, or the colours derived from its name until
        // they pick something. The fallback is not a placeholder — most hosts will keep it,
        // and it already gives every host a colour of its own.
        backdropFrom: (_h && _h.stageColorFrom && _h.stageColorFrom.length > 0)
                      ? _h.stageColorFrom : homeScreen.hostColorPair(_h ? _h.name : "")[0]
        backdropTo:   (_h && _h.stageColorTo && _h.stageColorTo.length > 0)
                      ? _h.stageColorTo : homeScreen.hostColorPair(_h ? _h.name : "")[1]
        backdropImage: (_h && _h.stageImage) ? _h.stageImage : ""

        zoneActive:  homeScreen.focusZone === 1
        pointerMode: homeScreen._pointerMode

        onActivated: function(kind) {
            // The profile shoulders are a legend, not a button: clicking one changes the
            // profile and leaves the focus where it was. Sending them through the action
            // path would drag the pad cursor down onto the action row for something the pad
            // can already do from anywhere.
            if (kind === "prevProfile") { homeScreen.cycleFocusedProfile(-1); return }
            if (kind === "nextProfile") { homeScreen.cycleFocusedProfile(1);  return }

            homeScreen.focusZone = 1
            navRoot.forceActiveFocus()
            homeScreen.runAction(kind)
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Actions
    // ═════════════════════════════════════════════════════════════════════════
    // Everything the stage and the Options grid can ask for lands here. Keeping one switch
    // means the mouse, the D-pad and the face-button shortcuts cannot drift apart.
    function runAction(kind) {
        var h = currentHost

        switch (kind) {
        case "addHost":
            addPcDialog.open()
            return
        case "refresh":
            ComputerManager.startPolling()
            return
        }

        if (!h) return

        switch (kind) {
        case "open":
            if (h.statusUnknown) return
            if (!h.serverSupported) {
                errorDialog.text = qsTr("The version of GeForce Experience on %1 is not supported by this build of StreamLight. You must update StreamLight to stream from %1.").arg(h.name)
                errorDialog.helpText = ""
                errorDialog.open()
                return
            }
            appShell.showApps(h.index, computerModel, false,
                              h.name, h.address, h.gpuModel, h.isTailscaleClone)
            break

        case "pair":
            var pin = computerModel.generatePinString()
            computerModel.pairComputer(h.index, pin)
            pairDialog.pin = pin
            pairDialog.open()
            break

        case "wake":
            startWake(h.index, h.name)
            break

        case "profiles":
            hostProfilesDialog.pcIndex  = h.index
            hostProfilesDialog.hostName = h.name
            hostProfilesDialog.reload()
            hostProfilesDialog.open()
            break

        case "options":
            hostOptionsDialog.hostName = h.name
            hostOptionsDialog.items    = menuItemsFor(h)
            hostOptionsDialog.open()
            break

        case "viewAllApps":
            appShell.showApps(h.index, computerModel, true,
                              h.name, h.address, h.gpuModel, h.isTailscaleClone)
            break

        case "tailscale":
            // Force the active connection onto Tailscale, then open the host's apps on the
            // 100.x endpoint.
            if (computerModel.prepareTailscaleSession(h.index)) {
                appShell.showApps(h.index, computerModel, true,
                                  h.name, h.tailscaleAddress, h.gpuModel, true)
            }
            break

        case "testNetwork":
            computerModel.testConnectionForComputer(h.index)
            testConnectionDialog.open()
            break

        case "rename":
            renamePcDialog.pcIndex = h.index
            renamePcDialog.originalName = h.name
            renamePcDialog.open()
            break

        case "delete":
            deletePcDialog.pcIndex = h.index
            deletePcDialog.pcName = h.name
            deletePcDialog.open()
            break

        case "viewDetails":
            showPcDetailsDialog.pcDetails = h.details
            showPcDetailsDialog.open()
            break

        case "background":
            stageBackgroundDialog.hostName     = h.name
            stageBackgroundDialog.currentImage = h.stageImage    || ""
            stageBackgroundDialog.currentSeed  = h.stageSeedColor || ""
            stageBackgroundDialog.pcIndex      = h.index
            stageBackgroundDialog.open()
            break

        case "requestStAuth":
            stForcePin(h.index)
            computerModel.requestStreamTweakAuth(h.index)
            break

        // Both directions of the same control. Restoring shows the chip and its "back to X"
        // confirmation, exactly as answering the prompt does; matching is silent and simply
        // leaves the host ready, which is what the next launch would have waited for.
        case "linkSpeed":
            if (h.linkSwitched) startLinkRestoreWatch(h.index, h.name)
            else                computerModel.matchHostLinkSpeed(h.index)
            break

        case "power":
            if (!h.online || !h.paired) { openPowerClientOnly(); return }
            powerDialog.clientOnly        = false
            powerDialog.pcIndex           = h.index
            powerDialog.hostName          = h.name
            powerDialog.authState         = h.auth
            // Seed update status: client read synchronously; host arrives async.
            powerDialog.clientUpdateState = SystemProperties.updatesPending() ? "pending" : "none"
            if (h.auth === "authorized") {
                powerDialog.hostUpdateState = "checking"
                computerModel.requestUpdateState(h.index)
            } else {
                powerDialog.hostUpdateState = "unavailable"
            }
            powerDialog.open()
            break

        case "updateHost":
            startUpdateCheckFor(h.index, h.name)
            break
        }
    }

    // Tiles for the Options popup: { kind, icon (emoji) | iconSource, label, danger? }.
    function menuItemsFor(h) {
        var items = []
        if (h.online && h.paired)
            items.push({ kind: "viewAllApps", icon: "🎮", label: qsTr("All Apps") })
        // Tailscale: opens the host's apps over the 100.x endpoint. Greyed (non-clickable)
        // when Tailscale isn't installed on this client.
        if (h.online && h.paired && h.hasTailscale)
            items.push({ kind: "tailscale", iconSource: "qrc:/res/tailscale.svg",
                         label: qsTr("Tailscale"), disabled: !_clientHasTailscale,
                         reason: qsTr("not installed here") })
        if (!h.online && h.wakeable)
            items.push({ kind: "wake", icon: "⏰", label: qsTr("Wake") })
        items.push({ kind: "testNetwork", icon: "📡", label: qsTr("Test Network") })
        items.push({ kind: "rename",      icon: "✏️", label: qsTr("Rename") })
        items.push({ kind: "delete",      icon: "🗑️", label: qsTr("Delete"), danger: true })
        items.push({ kind: "background",  icon: "🎨", label: qsTr("Background") })
        items.push({ kind: "viewDetails", icon: "ℹ️", label: qsTr("Details") })
        // The link, by hand. Not the 4.6.0 tile that was removed: that one sent PREPARE and let
        // the *host* pick a speed it had configured, which is the whole design the 8.1.0 pair
        // undid. This asks for the same speed the launch would ask for, so it can only agree
        // with it — either putting the link back now, or getting it ready so the next launch
        // doesn't have to wait. Greyed with the reason when the host isn't offering the control.
        //
        // It takes the slot the Power tile had: X on this screen already opens the same power
        // chooser, and the status-bar prompt is clickable, so the tile was a second door to one
        // room.
        // ⚠️ Two ways to be unable to do this, and only one of them used to grey the tile. The
        // host refusing (allowsLinkControl) was covered; the *client's own* matching being off —
        // globally or by the active profile — was not, so the tile looked live and LinkMatcher
        // then declined in silence. Restoring is always sensible, so only the matching direction
        // depends on the preference.
        if (h.online && h.paired && h.auth === "authorized") {
            var restoring = h.linkSwitched === true
            // ⚠️ The session clause applies to the matching direction only. A restore asked for
            // during a live stream is not refused — the host schedules it and puts the link back
            // once the stream is done — so grey that one and the tile would be lying the other
            // way. Last in the chain because "matching is off" is the actionable reason when
            // both are true, while this one clears itself.
            var why = h.linkChanging          ? qsTr("changing")
                    : !h.allowsLinkControl    ? qsTr("host declined")
                    : (!restoring && !h.matchLink) ? qsTr("matching is off")
                    : (!restoring && h.sessionActive) ? qsTr("host busy")
                    : ""
            items.push({ kind: "linkSpeed", icon: "🔀",
                         label: restoring ? qsTr("Restore NIC Speed")
                                          : qsTr("Match Link Speed"),
                         disabled: why.length > 0,
                         reason: why })
        }
        if (h.online && h.paired && h.auth === "authorized")
            items.push({ kind: "updateHost", icon: "🪟", label: qsTr("Windows Update") })
        if (h.online && h.paired && (h.auth === "pending" || h.auth === "denied"))
            items.push({ kind: "requestStAuth", icon: "🔑", label: qsTr("Request Access") })
        return items
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Dialogs
    // ═════════════════════════════════════════════════════════════════════════

    // Options chooser — wide centred tile grid.
    HostOptionsDialog {
        id: hostOptionsDialog
        // Close before running, never after: several of these actions open a dialog of their
        // own, and opening one modal on top of another leaves the focus in the wrong popup.
        onChosen: function(kind) {
            hostOptionsDialog.close()
            homeScreen.runAction(kind)
        }
        onClosed: navRoot.forceActiveFocus()
    }

    // Per-host streaming profiles manager.
    HostProfilesDialog {
        id: hostProfilesDialog
        computerModel: homeScreen.computerModel
        // Restore gamepad focus so navigation keeps working after the modal closes
        // (otherwise nothing is focused on a handheld).
        onClosed: navRoot.forceActiveFocus()
    }

    // Per-host stage backdrop. Applied immediately rather than on Done, so the choice is
    // judged against the actual stage instead of a swatch.
    StageBackgroundDialog {
        id: stageBackgroundDialog
        property int pcIndex: -1
        onChosen: function(imagePath, seedColor) {
            if (pcIndex < 0) return
            homeScreen.computerModel.setHostStageBackground(pcIndex, imagePath, seedColor)
            currentImage = imagePath
            currentSeed  = seedColor
        }
        onClosed: navRoot.forceActiveFocus()
    }

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
        onClosed: navRoot.forceActiveFocus()
    }

    NavigableMessageDialog {
        id: testConnectionDialog
        headerText: qsTr("NETWORK TEST")
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok
        onClosed: navRoot.forceActiveFocus()

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
        onClosed: { editText.clear(); navRoot.forceActiveFocus() }
        onAccepted: {
            if (editText.text) {
                computerModel.renameComputer(pcIndex, editText.text)
            }
        }

        ColumnLayout {
            spacing: 22

            Label {
                text: qsTr("RENAME PC")
                font.family: Theme.family
                font.pixelSize: 13
                font.bold: true
                font.letterSpacing: 1.6
                color: Theme.text3
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: renamePcDialog.label
                font.family: Theme.family
                font.pixelSize: 18
                color: Theme.text
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
                /*
                 * The same 15 characters Windows itself allows for a computer name — the
                 * NetBIOS limit, and what the OS rename dialog enforces.
                 *
                 * A discovered name can never exceed it, since it arrives as `hostname` in
                 * the server info, but this field could and was the only way past it. That
                 * matters because the host name is drawn in a Row with no width constraint
                 * on both the Home card and the host page's header, so its `elide` never
                 * fires: a long enough name runs on until it reaches the clock. Capping the
                 * one input that can produce one is cheaper than constraining every place it
                 * is read, and takes nothing away — nobody deliberately names a host longer
                 * than the machine it stands for. (Profile names are capped at 14 in
                 * HostProfilesDialog for the same reason.)
                 */
                maximumLength: 15
                color: Theme.text
                selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
                selectedTextColor: Theme.onAccent
                font.family: Theme.family
                font.pixelSize: 18
                font.bold: true
                horizontalAlignment: TextInput.AlignHCenter
                focus: true

                background: Rectangle {
                    color: "#0f0f0f"
                    radius: 8
                    border.color: editText.activeFocus ? Theme.accent : Theme.line
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
        onClosed: navRoot.forceActiveFocus()
    }

    // NB: no onClosed override here — AddHostDialog uses its own to clear the address field,
    // and replacing it would leave the last typed IP sitting there on the next open.
    AddHostDialog {
        id: addPcDialog
        onAccepted: function(ip) { ComputerManager.addNewHostManually(ip) }
    }

    // ── Power-off chooser (host / client / both) ──────────────────────────────
    LinkRestoreDialog {
        id: linkRestoreDialog
        onClosed: navRoot.forceActiveFocus()
        onRestoreChosen: function(index) {
            homeScreen.startLinkRestoreWatch(index, linkRestoreDialog.hostName)
        }
    }

    PowerDialog {
        id: powerDialog
        onClosed: navRoot.forceActiveFocus()
        onConfirmed: function(target, installUpdates) {
            if (target === "host") {
                homeScreen.computerModel.shutdownHost(powerDialog.pcIndex, installUpdates)
            } else if (target === "client") {
                SystemProperties.shutdownClient(installUpdates)
            } else if (target === "both") {
                // Send the host shutdown first, then power off the client after a short
                // delay so the bridge socket finishes writing the command.
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
        message:   homeScreen.updateJobMessage
        errorText: homeScreen.updateJobError
        updates:   homeScreen.updateJobUpdates
        counts:    homeScreen.updateJobCounts
        onInstall: function(scope) { homeScreen.startUpdateInstall(scope) }
        onHideRequested: updateDialog.close()        // background: job + chip stay alive
        onDismissed:     homeScreen.clearUpdateJob()
        onClosed:        navRoot.forceActiveFocus()
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
            // (state.percent is deliberately dropped: the host still reports it, but it only
            //  moves between files — see the note in UpdateDialog.)
            homeScreen.updateJobPhase   = phase
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
        onClosed: navRoot.forceActiveFocus()

        background: Rectangle {
            color: Theme.card
            border.color: Theme.line
            border.width: 1
            radius: 12
        }

        contentItem: Column {
            spacing: 18

            Label {
                text: qsTr("STREAMTWEAK ACCESS")
                font.family: Theme.family
                font.pixelSize: 13
                font.bold: true
                font.letterSpacing: 1.6
                color: Theme.text3
            }
            Label {
                width: stPinDialog.availableWidth
                wrapMode: Text.Wrap
                text: qsTr("StreamTweak on %1 (%2) is asking to allow this device. Check that the PIN below matches the one shown on the host, then click Allow there.\n\nStreaming works without this — authorizing only enables host metrics, NIC speed, store badges and session reports.").arg(homeScreen.stPinHostName).arg(homeScreen.stPinHostAddr)
                font.family: Theme.family
                font.pixelSize: 14
                color: Theme.text
            }
            // The one place the monospaced face survives: these four digits exist to be read
            // off one screen and compared against another, and a 1 that looks like an l is
            // precisely the failure a monospaced face exists to prevent.
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: homeScreen.stPinValue
                font.family: Theme.monoFamily
                font.pixelSize: 52
                font.bold: true
                font.letterSpacing: 10
                color: Theme.accent
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 130; height: 42; radius: 8
                color: stPinClose.containsMouse ? Theme.cardHigh : "#1a1a1a"
                border.color: Theme.line
                border.width: 1
                Label {
                    anchors.centerIn: parent
                    text: qsTr("Dismiss")
                    color: Theme.text
                    font.family: Theme.family
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
