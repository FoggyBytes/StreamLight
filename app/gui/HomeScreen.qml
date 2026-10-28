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
import AppUpdate 1.0
import PyroWaveTest 1.0

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

    /*
     * ── Hosts are held by uuid, never by row (6.3.1) ─────────────────────────────────────
     *
     * The rows are sorted by name, so a host renamed, discovered, removed or cloned for
     * Tailscale moves every row after it. A row number is only good for the call it is read
     * in. Anything that lives longer — a wake, an update, a dialog left open, the end of a
     * session hours later — keeps the host's uuid and asks for its row when it needs one.
     *
     * ⚠️ This used to hold rows, and nothing noticed the list move: a Power, an Update now or
     * a Delete confirmed after a host appeared went to whichever host had taken the row, while
     * the dialog still showed the name of the one the user had picked.
     */
    function hostIndexOf(uuid) {
        // layoutRevision is read for the binding, not for its value (it is never negative):
        // indexOfUuid() is an invokable and QML cannot see what it depends on, so this read is
        // what makes every binding over this function re-run when the rows move.
        if (!computerModel || computerModel.layoutRevision < 0 || !uuid) return -1
        return computerModel.indexOfUuid(uuid)
    }
    function hostUuidAt(idx) {
        return computerModel ? computerModel.uuidAt(idx) : ""
    }

    // The host the strip has selected, by identity. tabIndex is a row, so on its own a rename
    // or a host appearing left the selection on the number and moved it to another host.
    property string _selectedUuid: ""

    Connections {
        target: computerModel
        function onLayoutRevisionChanged() {
            var i = homeScreen.hostIndexOf(homeScreen._selectedUuid)
            if (i >= 0 && i !== homeScreen.tabIndex) homeScreen.tabIndex = i
            homeScreen._dropFlowsOnMissingHosts()
        }
    }

    // A flow whose host has left the list can never finish, and a wake or a link restore left
    // running also holds the self-update's Install now (see the Binding on installBlocked).
    // Asked of the uuid directly, not of the derived rows: those are bindings on the same
    // signal this runs on, and nothing says which of the two is told first.
    function _dropFlowsOnMissingHosts() {
        if (wakeActive && hostIndexOf(wakeUuid) < 0)
            _endWake()
        if (updateJobActive && hostIndexOf(updateJobHostUuid) < 0)
            clearUpdateJob()
        if (linkRestoreActive && hostIndexOf(linkRestoreHostUuid) < 0)
            _clearLinkRestoreWatch()
        if (linkAskHostUuid !== "" && hostIndexOf(linkAskHostUuid) < 0) {
            linkAskHostUuid = ""
            linkAskHostName = ""
        }
    }

    // 0 = tab strip · 1 = action row. The app opens with the focus on the actions and
    // "Open" selected, because that is what a user came to press.
    property int focusZone: 1

    /*
     * The interface scale: the shell's, the one the clock across this header and every dialog
     * opened over this screen are drawn at (6.3.1). This header, the tab strip and the stage's
     * margins were the last chrome still in fixed pixels. Since the clock started scaling
     * (5.7.0) the brand row's "same 22 and 60 as the clock" held at one window size only, and
     * on a large screen the strip sat at 1.00 between a clock and a host card at 1.60.
     * Hairlines stay 1 px, as everywhere else. Always called qualified — `homeScreen._px` —
     * because HostStage, declared in this file, has a `_px` of its own at a different scale.
     */
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    // The selected host's full record, pushed up by its probe below. One object rather than
    // a dozen bindings into a delegate: the stage needs all of it at once and nothing else
    // needs any of it.
    property var currentHost: null

    // (The trigger glyphs used to be resolved here. ActionHint does it now — vendor, size and
    // the keyboard alternative all in one place — so the strip just names the button.)

    // Tracks the last-used input device so the pad highlight and the mouse hover never
    // light up two different things at once.
    readonly property bool _pointerMode: SdlGamepadKeyNavigation.inputMode === "pointer"

    // Whether Tailscale is installed on THIS client (drives the greyed "Tailscale" host
    // option). Evaluated once — the client's install state rarely changes mid-run.
    readonly property bool _clientHasTailscale: computerModel ? computerModel.clientHasTailscale() : false

    // ── What the shell reads ─────────────────────────────────────────────────
    // (focusedProfileCount lived here and was removed: its only reader was the guard in
    // cycleFocusedProfile below, and its stated rule — "the LB/RB hints hide when ≤ 1" —
    // was not what HostStage does. The hints are HostStage's, and it hides them at 0.)

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

    // ⚠️ No profile-count test here on purpose, and it is not an omission. This used to
    // read `focusedProfileCount > 1`, which made the keys dead on a host with exactly
    // one profile — the commonest setup — while HostStage still drew the hints (LT/RT since 13/10/2026),
    // because it asks the right question (>= 1). The cycle's stops are Global plus every
    // profile, so ONE profile is already two stops.
    //
    // The authority on whether the cycle can move is HostProfileManager::cycle(), which
    // returns without doing anything when a host has no profiles at all. A second copy of
    // that rule here is what drifted, so there is no second copy any more: the guards left
    // in HostStage and AppsScreen decide whether to *draw* the hints, not whether the key
    // works.
    function cycleFocusedProfile(dir) {
        if (!computerModel || currentHostIndex < 0) return
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
        powerDialog.clientOnly         = true    // no host row at all
        powerDialog.pcUuid             = ""
        powerDialog.hostName           = ""
        powerDialog.authState          = "none"
        powerDialog.clientModes        = SystemProperties.clientPowerModes()
        powerDialog.clientUpdateState  = SystemProperties.updatesPending() ? "pending" : "none"
        powerDialog.hostUpdateState    = "unavailable"
        powerDialog.open()
    }

    // Re-asks every authorized host for its last session. Called by the shell when the client
    // returns to the host list: a session that just ended is the commonest reason to be here,
    // and the card would otherwise go on describing the one before it until the next restart.
    function refreshLastPlayed() {
        for (var i = 0; i < hostProbes.count; i++) {
            var p = hostProbes.itemAt(i)
            if (p && p.refreshLastPlayed) p.refreshLastPlayed()
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
    // By uuid, see hostIndexOf(): a wake lasts up to two minutes, plenty for the rows to move.
    property string wakeUuid : ""
    readonly property int wakeIndex : hostIndexOf(wakeUuid)
    property string wakeHostName : ""
    property int    wakeStep : 0        // mirrors WakeDialog's step rows
    property bool   wakeActive : false
    property string wakeDetail : ""

    // The pad is up and doing its own LOCKSTATE polling. Without this we would answer those
    // same replies here — wakeActive is still true — and start a second unlock session on
    // every poll that came back still locked.
    property bool   wakeUnlocking : false

    // Whether this wake has a third step at all. Captured once at the start, because it is a
    // property of the host rather than of the tick.
    //
    // ⚠️ This is what the whole StreamTweak switch buys the wake flow. With the integration
    // off there is nothing that could answer LOCKSTATE, so the wait for it is time the user
    // can never get back — and it used to be paid on EVERY wake, behind a modal dialog,
    // because the client only ever remembered the positive answer. The switch is the answer,
    // so nothing here has to predict, remember, or investigate anything.
    property bool   _wakeWaitsForStreamTweak : true

    /*
     * ── How long each phase took ─────────────────────────────────────────────
     *
     * The dialog draws these; this is where they are measured. One start stamp, one settled
     * duration and one outcome per phase, in three arrays indexed the way the dialog indexes
     * its rows.
     *
     * ⚠️ Assigned whole, never written through — `wakeStepMs[1] = x` changes the array in
     * place and QML has nothing to notice, so the row would go on showing the old figure
     * until something unrelated happened to re-evaluate it.
     */
    property var  wakeStepStart  : [0, 0, 0]
    property var  wakeStepMs     : [-1, -1, -1]
    property var  wakeStepResult : ["", "", ""]
    property real wakeNow        : 0

    readonly property bool _wakeFailed: wakeStepResult.indexOf("fail") >= 0

    function _wakeResetSteps() {
        wakeStepStart  = [0, 0, 0]
        wakeStepMs     = [-1, -1, -1]
        wakeStepResult = ["", "", ""]
        wakeNow        = Date.now()
    }

    function _wakeBeginStep(i) {
        if (i < 0 || i > 2 || wakeStepStart[i] > 0) return
        var a = wakeStepStart.slice()
        a[i] = Date.now()
        wakeStepStart = a
        wakeNow = a[i]
    }

    // Settles one phase and opens the next. Chaining the two here is what guarantees a phase
    // is never timed from zero: whatever ends a step is the only thing that knows the next
    // one has begun, and there is no other moment at which to say so.
    function _wakeEndStep(i, ok) {
        if (i < 0 || i > 2 || wakeStepMs[i] >= 0) return
        _wakeBeginStep(i)
        var d = wakeStepMs.slice()
        d[i] = Math.max(0, Date.now() - wakeStepStart[i])
        wakeStepMs = d
        var r = wakeStepResult.slice()
        r[i] = ok ? "ok" : "fail"
        wakeStepResult = r
        if (ok && i < 2) _wakeBeginStep(i + 1)
    }

    function startWake(idx, hostName) {
        // A launch is unlocking a host (#35) and owns the Desktop session's close; the wake
        // would share _waitingForQuit with it. Seconds at most, and a wake is pressed again.
        if (_lcUuid !== "") return
        wakeUuid     = hostUuidAt(idx)
        wakeHostName = hostName
        wakeStep      = 0
        wakeDetail    = ""
        wakeActive    = true
        wakeUnlocking = false
        _wakeWaitsForStreamTweak = computerModel.streamTweakEnabled(idx)
        appListWaitTimer.tries = 0
        appListWaitTimer.stop()

        // Started before the call, not after: wakeComputer() hands the send to a thread pool
        // and returns, so the only honest start for "how long did the packet take" is the
        // instant we asked for it.
        _wakeResetSteps()
        _wakeBeginStep(0)
        computerModel.wakeComputer(idx)
        wakeDialog.open()
        wakeWaitTimer.elapsed = 0
        wakeWaitTimer.restart()
    }

    // Drives the counter on whichever row is running. Its own timer rather than a ride on
    // wakeWaitTimer's 1 Hz tick: that one stops the moment StreamTweak answers, and it also
    // returns early on a host probe that is not there yet — which would freeze the clock on
    // exactly the screen whose whole point is that it is still moving.
    Timer {
        id: wakeClock
        // 100 ms because the rows show tenths of a second: at 200 the figure skipped every
        // other digit.
        interval: 100
        repeat: true
        // Nothing is being timed once a step has failed — the flow has stopped and every row
        // is showing a settled figure — so the clock stops with it rather than writing a
        // property ten times a second that nothing reads.
        running: homeScreen.wakeActive && !homeScreen._wakeFailed
        onTriggered: homeScreen.wakeNow = Date.now()
    }

    // The host that just came all the way through, so its card can say so. Transient: on a
    // host that is simply online "ready" is not news, it is the normal state, and a chip that
    // never goes away stops being read.
    property string readyUuid : ""
    readonly property int readyIndex : hostIndexOf(readyUuid)

    function _endWake(ready) {
        if (ready === true && wakeIndex >= 0) {
            readyUuid = wakeUuid
            readyTimer.restart()
        }
        // The host no longer reports the unlock session: nothing left to hide. When it still
        // does, the probe clears the mark the moment it stops (onPBusyChanged).
        if (_unlockCarrierIndex >= 0) {
            var carrier = hostProbes.itemAt(_unlockCarrierIndex)
            if (!carrier || !carrier.pBusy) _unlockCarrierUuid = ""
        }
        wakeActive     = false
        wakeUnlocking  = false
        _waitingForQuit = false
        _afterQuit     = ""
        wakeUuid       = ""
        wakeWaitTimer.stop()
        appListWaitTimer.stop()
        quitWaitTimer.stop()
        wakeDialog.close()
    }

    Timer {
        id: readyTimer
        interval: 12000
        onTriggered: homeScreen.readyUuid = ""
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

    /*
     * ── Resume the last played game, straight from the card ──────────────────────────────
     *
     * The whole point of Last played: a stream from the host list without going through the
     * library. It stands on the same machinery the PIN unlock already uses — an AppModel
     * initialised on demand, one app found by name, createSessionForApp() and a StreamSegue —
     * so nothing new had to be invented to launch from this screen.
     *
     * ⚠️ createSessionForApp() applies the cascade (global ← host profile ← per-game), so a
     * game resumed from here runs at exactly the settings it would from the host page. Doing
     * this by hand with StreamingPreferences::get() is the mistake that has already been made
     * once on this screen — see the note on streamOverride in HostStage.
     *
     * ⚠️ When something is already streaming on the host we open the library instead of
     * launching. That page owns the "quit the running game?" flow, with its dialog and its
     * box art, and a second copy of it here would be a second thing to keep in step. Since
     * 6.0.0 the card shows the running session with Resume in that case (resumeRunning), so
     * this branch is only reached when the host runs something its app list does not name.
     */
    // The two guards the host page's own launch goes through (AppsScreen's
    // launchOrResumeSelectedApp), which the card's buttons had never had (6.3.1): a press left
    // over from a stream that has just popped, and a second A while the first launch is still
    // being pushed — which would queue a second session to start the moment the first ended.
    function _launchBlocked() {
        return !!Window.window && (Window.window._streamJustEnded === true
                                   || Window.window._streamLaunching === true)
    }

    function resumeLastPlayed(h) {
        if (!h || !h.online || !h.paired || _launchBlocked()) return

        var lp = computerModel.lastPlayedFor(h.index)
        if (!lp || lp.name === undefined || lp.name === "") return

        if (h.busy) {
            appShell.showApps(h.index, computerModel, false,
                              h.name, h.address, h.gpuModel, h.isTailscaleClone)
            return
        }

        // A locked host gets the PIN pad first (7.0.0, #35): see launchWhenUnlocked(). The
        // launch itself may therefore run seconds from now, so it finds the host again by uuid.
        var uuid = hostUuidAt(h.index)
        launchWhenUnlocked(h.index, h.name,
                           function () { homeScreen._startLastPlayed(uuid, h, lp) }, "home")
    }

    function _startLastPlayed(uuid, h, lp) {
        var idx = hostIndexOf(uuid)
        if (idx < 0) return                         // the host was removed meanwhile
        if (idx !== h.index) {
            // The rows moved while the PIN pad was up: the same host, read from its new row.
            var moved = {}
            for (var k in h) moved[k] = h[k]
            moved.index = idx
            h = moved
        }

        // showHidden = true, like the unlock flow: this is not the user browsing a library,
        // it is us looking for one entry they have already chosen. A game they hid from the
        // grid but streamed last is still the game to resume.
        unlockAppModel.initialize(ComputerManager, h.index, true)
        var appIndex = unlockAppModel.indexOfAppNamed(lp.name)
        if (appIndex < 0) {
            // The gate said it was there when the card was drawn. If it has gone since, the
            // library is the honest place to land — it will show what the host does have.
            appShell.showApps(h.index, computerModel, false,
                              h.name, h.address, h.gpuModel, h.isTailscaleClone)
            return
        }

        var session = unlockAppModel.createSessionForApp(appIndex)
        if (!session) {
            console.warn("[resume] could not create the session")
            return
        }

        // Not a resume in the protocol sense — that word means "a session is already up on
        // the host and we are rejoining it", which h.busy just ruled out.
        _launchFromCard(h, lp.name, lp.cover, session, false)
    }

    /*
     * ── Rejoin the session the host is running (6.0.0) ───────────────────────────────────
     *
     * The card's Resume. It does what Resume on the host page does for the running row — a
     * session for the current game, pushed with isResume — without the trip to the library.
     *
     * ⚠️ createSessionForCurrentGame(), not an AppModel lookup: it matches on currentGameId,
     * the id the card was drawn from, so the session opened is the one running even when two
     * entries share a name (a 2.0 server's running-game copy carries the game's title). It
     * builds its preferences through the same buildPrefs() cascade as createSessionForApp().
     *
     * No Stop here, by decision: quitting a game — and the confirmation that goes with it —
     * stays on the host page.
     */
    function resumeRunning(h) {
        if (!h || !h.online || !h.paired || _launchBlocked()) return

        var running = computerModel.runningAppFor(h.index)
        if (!running || running.name === undefined || running.name === "") {
            // The session ended, or moved to something the list does not name, between drawing
            // the card and the press. The host page shows what is actually there.
            appShell.showApps(h.index, computerModel, false,
                              h.name, h.address, h.gpuModel, h.isTailscaleClone)
            return
        }

        var session = computerModel.createSessionForCurrentGame(h.index)
        if (!session) {
            console.warn("[resume] could not create a session for the running game")
            appShell.showApps(h.index, computerModel, false,
                              h.name, h.address, h.gpuModel, h.isTailscaleClone)
            return
        }

        _launchFromCard(h, running.name, running.cover, session, true)
    }

    // The segue both card buttons push. One copy, because the callback below is the part that
    // must not drift: it is what lands the user on the host page and re-arms the link prompt.
    function _launchFromCard(h, name, cover, session, isResume) {
        var comp = Qt.createComponent("StreamSegue.qml")
        if (comp.status !== Component.Ready) {
            console.warn("[resume] StreamSegue.qml not ready:", comp.errorString())
            return
        }

        // The callback below runs when the session ends, hours from now: it finds the host
        // again by uuid rather than trusting the row it had at launch (see hostIndexOf()).
        var hostUuid = hostUuidAt(h.index)

        var segue = comp.createObject(stackView, {
            "appName":  name,
            "boxArt":   (cover !== undefined ? cover : ""),
            "session":  session,
            "isResume": isResume,
            /*
             * ⚠️ This does NOT come back to Home, and the difference is not a preference.
             *
             * A session launched from here leaves `AppShell.currentPage` at 0 for its whole
             * length — the segue is pushed on the stack ABOVE the shell — so popping it used
             * to reveal the host list, one screen further out than the user had got to. The
             * host's page is where the evening actually is, with the game just played already
             * at row 0: the play-time record is written in the session's own teardown
             * (Session::endPlaytime), which runs before this callback, so the model sorts it
             * there on its own and there is no cursor to place by hand.
             *
             * It is also the only thing that can put the "shall I put the host's link back?"
             * question back on the table. That prompt is armed by the trip host page → Home
             * and nothing else (AppShell._prevPage), and a resume launched from Home never
             * made that trip, so the question was silently unaskable for the one flow most
             * likely to have switched the link. The same call the Open button makes settles
             * both, in the right order: the page changes before the segue pops.
             */
            "onSessionEndedFn": function() {
                if (!homeScreen.appShell) return
                var idx = homeScreen.hostIndexOf(hostUuid)
                if (idx < 0) return                     // the host was removed meanwhile
                homeScreen.appShell.noteStreamEnded(idx, h.name)
                homeScreen.appShell.showApps(idx, computerModel, false,
                                             h.name, h.address, h.gpuModel,
                                             h.isTailscaleClone)
            }
        })
        if (Window.window) Window.window.markStreamLaunching()
        stackView.push(segue)
    }

    /*
     * Opens the PIN pad: a hidden Desktop session on the host, carrying the keystrokes to its
     * logon screen. Shared by the wake and by a launch that finds the host locked (7.0.0, #35).
     *
     * Returns "" once the pad is on its way, "wait" when the host's app list has no Desktop
     * entry (yet — after a wake it may simply not have arrived), or the line to show.
     * `beforePush`, when given, runs just before the segue is pushed: the wake closes its
     * dialog there, before the push, so the focus the popup hands back lands on nothing that
     * matters and the segue takes it afterwards.
     */
    function _openUnlockPad(uuid, hostName, resultFn, beforePush, rejoin) {
        var idx = hostIndexOf(uuid)
        if (idx < 0) return qsTr("Could not open the PIN pad")

        var comp = Qt.createComponent("StreamSegue.qml")
        if (comp.status !== Component.Ready) {
            console.warn("[unlock] StreamSegue.qml not ready:", comp.errorString())
            return qsTr("Could not open the PIN pad")
        }

        var session = null
        if (rejoin) {
            // A game is already running on the host (Unlock on a busy host): the carrier is a
            // rejoin of that session, never a Desktop launch — the server runs one app at a
            // time, and the Desktop would have to replace the game. Nothing is closed after.
            session = computerModel.createSessionForCurrentGame(idx)
        }
        else {
            // The Desktop app: the host has nothing else to offer while nobody is logged in,
            // and this session is only a carrier for keystrokes anyway.
            // showHidden = true: this is not the user browsing a library, it is us looking for
            // one specific entry. Someone who hid Desktop from their grid still needs it here.
            unlockAppModel.initialize(ComputerManager, idx, true)
            // Either name the server gives it — see isDesktopName() in nvapp.h.
            var appIndex = unlockAppModel.indexOfDesktop()
            if (appIndex < 0) return "wait"
            session = unlockAppModel.createSessionForApp(appIndex)
        }
        if (!session) {
            console.warn("[unlock] could not create the session")
            return qsTr("Could not open the PIN pad")
        }
        session.setUnlockMode(true)
        if (!rejoin) _unlockCarrierUuid = uuid

        // Declared before the session exists, so the host has the mark in hand by the time
        // its streaming server reports a client.
        // Not for a rejoin: there the host already holds a session, and this is a reconnect to
        // it. A suppressed start would skip the host's own "reconnected within the grace"
        // handling, and the session the user is about to resume could expire under the pad.
        if (!rejoin) computerModel.markUnlockSession(idx, true)

        // A binding, not the number: the segue polls the lock state for as long as the pad
        // is up and ends the unlock mark afterwards, and the rows can move meanwhile.
        var segue = comp.createObject(stackView, {
            "appName":        "Desktop",
            "session":        session,
            "unlockMode":     true,
            "computerModel":  computerModel,
            "pcIndex":        Qt.binding(function() { return homeScreen.hostIndexOf(uuid) }),
            "hostName":       hostName,
            "onUnlockResultFn": resultFn
        })
        if (Window.window) Window.window.markStreamLaunching()

        if (beforePush) beforePush()
        stackView.push(segue)
        return ""
    }

    function _wakeStartUnlock() {
        var r = _openUnlockPad(wakeUuid, wakeHostName,
                               function (ok) { homeScreen._wakeUnlockDone(ok) },
                               function () {
                                   homeScreen.wakeUnlocking = true
                                   wakeDialog.close()
                               })
        if (r === "") return

        // The list may not be here yet. The poller fetches it only once the host is online and
        // paired, while we get here as soon as StreamTweak approves us — a different timer, so
        // the two race. Waiting is right; giving up silently, which is what this did first, is
        // indistinguishable from the feature being broken.
        if (r === "wait") {
            if (appListWaitTimer.tries < 20) {
                appListWaitTimer.tries++
                appListWaitTimer.restart()
                return
            }
            console.warn("[unlock] no Desktop app on this host after waiting for the app list")
            wakeDetail = qsTr("This host has no Desktop entry")
            return
        }
        wakeDetail = r
    }

    function _wakeUnlockDone(ok) {
        wakeUnlocking = false
        if (!ok) { _endWake(); return }
        _noteUnlocked(wakeUuid)
        console.log("[unlock] unlocked — closing the Desktop session on the host")
        _closeUnlockSession("wake")
    }

    // Disconnecting is not enough. The streaming server keeps a session alive on purpose so it
    // can be resumed, so the host would go on reporting itself as streaming — and opening it
    // afterwards would show a session nobody asked for. Ask it to close the app.
    //
    // And wait for that to finish before going on: the host refuses to renegotiate its adapter
    // while a session is running, so a link match now would be quietly discarded — and a launch
    // now would go out as a resume of the Desktop's session (Session reads currentGameId).
    // `then` says what comes after: "wake" (the link match) or "launch" (#35).
    function _closeUnlockSession(then) {
        _afterQuit = then
        _waitingForQuit = true
        quitWaitTimer.restart()
        unlockAppModel.quitRunningApp()
    }

    // Set while the host is being asked to close the unlock session.
    property bool _waitingForQuit : false
    property string _afterQuit : ""

    /*
     * ── A launch that finds the host locked (7.0.0, issue #35) ───────────────────────────
     *
     * The PIN pad used to exist only inside the wake: a host powered on any other way — by
     * hand, by a home-automation plug, by a wake sent from another device — reached the launch
     * sitting at its lock screen, and the stream opened on a logon screen the pad cannot type
     * into. Now every NEW launch asks LOCKSTATE first, and on a locked host runs the same
     * unlock the wake runs, then the launch the user chose.
     *
     * ⚠️ Only new launches. Rejoining a running session cannot be done this way: the carrier
     * is a Desktop session, and the server runs one app at a time, so opening it would mean
     * quitting the game being rejoined. A swap (QuitSegue) and a host control are left alone
     * for the same reason — a session is already up.
     *
     * ⚠️ Silence is never "locked". Only a host that says locked gets the pad; no answer, an
     * old host, the integration off, an unapproved client: the launch goes ahead exactly as it
     * always has. And the wait for the answer is capped well below the bridge's own 3 s, so a
     * host whose StreamTweak is down costs a launch at most lockCheckTimer.interval.
     *
     * Nothing on the host had to change: LOCKSTATE, UNLOCKBEGIN and the suppression of the
     * carrier session are the wake's (StreamTweak 8.1.0, double start lines 9.0.0).
     */
    property string _lcUuid     : ""        // the host being checked or unlocked
    property string _lcHostName : ""
    property var    _lcLaunchFn : null
    property string _lcOwner    : ""        // "apps" | "home" — see dropLaunchWhenUnlocked()
    property bool   _lcAsking   : false     // LOCKSTATE asked, no answer yet

    function launchWhenUnlocked(idx, hostName, launchFn, owner) {
        if (!launchFn) return
        // One at a time: a second A while the first is being checked or unlocked is dropped,
        // as the launch guards drop a second push.
        if (_lcUuid !== "") return
        // A wake has already dealt with the lock — it is past its own LOCKSTATE by the time a
        // launch can be pressed — and the integration off means nobody could answer.
        if (wakeActive || idx < 0 || !computerModel.streamTweakEnabled(idx)) {
            launchFn()
            return
        }

        _lcUuid     = hostUuidAt(idx)
        _lcHostName = hostName
        _lcLaunchFn = launchFn
        _lcOwner    = owner || ""
        _lcAsking   = true
        lockCheckTimer.restart()
        // May answer before it returns (bridge not allowed → supported=false at once): the
        // state above is already set for it.
        computerModel.requestLockState(idx)
    }

    // The page that asked for the launch is going away (AppsScreen, when the user leaves it
    // while the unlock settles). The unlock may finish; the launch must not follow.
    function dropLaunchWhenUnlocked(owner) {
        if (_lcUuid !== "" && _lcOwner === owner) _lcLaunchFn = null
    }

    function _lcAnswered(supported, locked) {
        _lcAsking = false
        lockCheckTimer.stop()
        if (supported && locked) {
            console.log("[unlock] the host is locked — PIN pad before the launch")
            _lcStartUnlock()
            return
        }
        _lcLaunch()
    }

    Timer {
        id: lockCheckTimer
        interval: 1500
        onTriggered: {
            if (!homeScreen._lcAsking) return
            homeScreen._lcAsking = false
            console.log("[unlock] no answer to LOCKSTATE in time — launching")
            homeScreen._lcLaunch()
        }
    }

    function _lcStartUnlock() {
        var r = _openUnlockPad(_lcUuid, _lcHostName,
                               function (ok) { homeScreen._lcUnlockDone(ok) }, null, _lcRejoin)
        if (r === "") return
        if (!_lcLaunchFn) {
            // The card's Unlock: there is no launch to fall back on, so say why nothing opened.
            errorDialog.text = (r === "wait") ? qsTr("This host has no Desktop entry") : r
            errorDialog.helpText = ""
            errorDialog.open()
            _lcEnd()
            return
        }
        // No pad to offer — most likely no Desktop entry on this host. The launch goes ahead as
        // it would have before #35: a stream on the logon screen is no worse than no stream.
        console.warn("[unlock] cannot open the PIN pad before the launch ("
                     + (r === "wait" ? "no Desktop entry" : r) + ") — launching")
        _lcLaunch()
    }

    function _lcUnlockDone(ok) {
        if (!ok) {
            // Backed out of the pad, or the host dropped the session: nothing is launched. The
            // user is back where they pressed, and pressing again asks again.
            console.log("[unlock] PIN pad closed without unlocking — no launch")
            _lcEnd()
            return
        }
        _noteUnlocked(_lcUuid)
        if (_lcRejoin) {
            // The carrier was the running game itself: it stays running, nothing to close.
            console.log("[unlock] unlocked — the running session is left as it was")
            _lcEnd()
            return
        }
        console.log("[unlock] unlocked — closing the Desktop session"
                    + (_lcLaunchFn ? ", then launching" : ""))
        _closeUnlockSession("launch")
    }

    /*
     * ── Unlock from the card (7.0.0, #35) ────────────────────────────────────────────────
     *
     * A host at its lock screen is not opened: its card's first button reads Unlock instead of
     * Open (HostStage.locked) and starts the PIN pad straight away — the same carrier and the
     * same close as a launch that found the host locked, with nothing launched after. On a
     * host already running a game the carrier is a rejoin of that game (_openUnlockPad), which
     * is left running.
     */
    property bool _lcRejoin : false

    readonly property bool unlockBusy : _lcUuid !== "" || wakeActive

    function startUnlock(h) {
        if (!h || !h.online || !h.paired || _lcUuid !== "" || wakeActive || _launchBlocked())
            return
        _lcUuid     = hostUuidAt(h.index)
        _lcHostName = h.name
        _lcLaunchFn = null
        _lcOwner    = "home"
        _lcRejoin   = h.busy === true
        console.log("[unlock] Unlock pressed" + (_lcRejoin ? " — rejoining the running session" : ""))
        _lcStartUnlock()
    }

    // We know before the next poll does: the card goes back to Open, and the host page is not
    // left for Home on a stale "locked" in the seconds before LOCKSTATE says so itself.
    function _noteUnlocked(uuid) {
        var p = hostProbes.itemAt(hostIndexOf(uuid))
        if (p) { p.stLocked = false; p._lockMisses = 0 }
    }

    // For AppShell, which leaves the host page when its host is locked.
    signal hostLockChanged(string uuid, bool locked)
    function isHostLocked(uuid) {
        var p = hostProbes.itemAt(hostIndexOf(uuid))
        return !!p && p.stLocked === true
    }

    function _lcAfterQuit() {
        // quitAppCompleted without an error is only emitted once the host reports no game, but
        // the fallback timer and a failed quit come here too. A launch with the Desktop still
        // running would go out as a resume of it, so in that case nothing is launched.
        if (unlockAppModel.getRunningAppId() !== 0) {
            console.warn("[unlock] the Desktop session is still running on the host — not launching")
            _lcEnd()
            return
        }
        _lcLaunch()
    }

    function _lcLaunch() {
        var fn = _lcLaunchFn
        _lcEnd()
        if (fn) fn()
    }

    function _lcEnd() {
        // As in _endWake(): the carrier mark goes now if the host has stopped reporting it,
        // otherwise the probe clears it when it does.
        if (_unlockCarrierIndex >= 0) {
            var carrier = hostProbes.itemAt(_unlockCarrierIndex)
            if (!carrier || !carrier.pBusy) _unlockCarrierUuid = ""
        }
        lockCheckTimer.stop()
        _lcAsking   = false
        _lcRejoin   = false
        _lcLaunchFn = null
        _lcOwner    = ""
        _lcHostName = ""
        _lcUuid     = ""
    }

    /*
     * ── The PIN unlock's own session is never "Streaming now" (6.0.0) ────────────────────
     *
     * The unlock streams the host's Desktop to carry the keystrokes, then asks the host to
     * close it. The host goes on reporting that session until its next serverinfo poll, a few
     * seconds later — and the card, which shows whatever the host is running, put "Streaming
     * now · Desktop · Resume" up in place of Last played for exactly that long. Nobody asked
     * for that session; offering to resume it is wrong for the whole of its life.
     *
     * Two guards, because they cover two stretches:
     *  - the wake itself, PIN pad and link match included: nothing it starts is a session;
     *  - after it, the host this unlock ran on, until it stops reporting a session
     *    (onPBusyChanged in the probe) — and only while what it reports is the Desktop, so a
     *    game the user launches next is shown as soon as it runs.
     */
    property string _unlockCarrierUuid : ""
    readonly property int _unlockCarrierIndex : hostIndexOf(_unlockCarrierUuid)

    /*
     * Any host is streaming right now (6.0.0). AppShell hands it to AmbientBackground, whose
     * waves run twice as fast while it is true.
     *
     * Over every host, not just the one on screen: the floor is the whole app's, and a session
     * running on the host you have scrolled away from is still a session. Each probe works out
     * its own answer (pStreaming) with the card's rule; this only ORs them.
     *
     * ⚠️ The early return is correct, not a shortcut that loses updates: a binding tracks what
     * it actually read, so it listens to every probe up to the first streaming one, and when
     * that one stops it re-runs and reads further.
     */
    readonly property bool anyStreaming: {
        for (var i = 0; i < hostProbes.count; i++) {
            var p = hostProbes.itemAt(i)
            if (p && p.pStreaming) return true
        }
        return false
    }

    function _runningFor(idx, live) {
        if (!live) return ({})
        if (wakeActive && idx === wakeIndex) return ({})
        var r = computerModel.runningAppFor(idx)
        if (idx === _unlockCarrierIndex && r.name !== undefined
                && String(r.name).trim().toLowerCase() === "desktop")
            return ({})
        return r
    }

    function _quitSettled() {
        if (!_waitingForQuit) return
        _waitingForQuit = false
        quitWaitTimer.stop()
        var then = _afterQuit
        _afterQuit = ""
        if (then === "launch") _lcAfterQuit()
        else                   _wakeMatchLink()
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
                onlineFor = 0
                if (elapsed > 150) {
                    stop()
                    console.warn("[unlock] the host never came online — giving up")
                    // ⚠️ The dialog stays up, where it used to vanish. Which of the three
                    // steps gave out, and how long it was waited on, is the whole of what
                    // there is to learn from a wake that failed — and closing the box was
                    // throwing it away at the one moment it mattered. Close is on the button.
                    homeScreen._wakeEndStep(1, false)
                    homeScreen.wakeDetail = qsTr("The host never came online")
                }
                return
            }

            if (homeScreen.wakeStep < 2) {
                // Both, and in this order: the packet step may still be open if its result
                // never came back, and _wakeEndStep chains each step's end into the next
                // one's start, so timing them is the same act as marking them done.
                homeScreen._wakeEndStep(0, true)
                homeScreen._wakeEndStep(1, true)
                homeScreen.wakeStep = 2
            }

            // The integration is off for this host: the host answering IS the whole wake.
            // Straight to the end rather than through _wakeMatchLink(), which would only ask
            // for a link match that is gated off and refused.
            if (!homeScreen._wakeWaitsForStreamTweak) {
                stop()
                homeScreen._endWake(true)
                return
            }

            onlineFor += 1

            // Two minutes, and one number rather than two. This used to be 120 for a host we
            // had watched run StreamTweak and 60 for one we had never been able to ask, which
            // was an attempt to guess what the switch now states outright: reaching this line
            // at all means the user has said this host runs StreamTweak, so it gets the full
            // wait — it takes the better part of a minute to come up after a boot. If they are
            // wrong, Cancel is right there, which the old guess could not offer.
            var cap = 120
            if (onlineFor > cap) {
                stop()
                console.warn("[unlock] StreamTweak never answered in " + cap
                             + "s of the host being online — carrying on without the pad")
                // Recorded as the failure it is, even though the flow carries on: this row
                // is the one that did not happen. The dialog closes on the next line, so it
                // is written for the log and for the instant it is on screen.
                homeScreen._wakeEndStep(2, false)
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

        /*
         * The packet is away — or never left. Until 5.7.1 nobody asked: wakeComputer()
         * dropped the answer on the floor, so a host with no MAC address on record produced
         * a dialog that spun for two and a half minutes and then closed with nothing said.
         *
         * ⚠️ "sent" is about this machine and nothing else. The host has not had time to
         * hear anything, so this can only ever close the FIRST row.
         */
        function onWakeCompleted(index, sent) {
            if (!homeScreen.wakeActive || index !== homeScreen.wakeIndex) return
            homeScreen._wakeEndStep(0, sent)
            if (!sent) {
                // Nothing is coming: the two rows below this one are waiting on a host that
                // was never told to wake. Stop the wait and leave the dialog up saying so.
                wakeWaitTimer.stop()
                homeScreen.wakeDetail = qsTr("Could not send the wake signal")
                return
            }
            if (homeScreen.wakeStep < 1) homeScreen.wakeStep = 1
        }

        function onLockStateReceived(index, supported, locked) {
            // A launch's check (#35). Before the wake's guard, and never both: a launch
            // during a wake does not ask (launchWhenUnlocked).
            if (homeScreen._lcAsking && index === homeScreen.hostIndexOf(homeScreen._lcUuid)) {
                homeScreen._lcAnswered(supported, locked)
                return
            }
            if (!homeScreen.wakeActive || homeScreen.wakeUnlocking) return
            if (index !== homeScreen.wakeIndex) return
            // Only while the wake is still waiting for this answer. The probes poll LOCKSTATE
            // too (stLocked), so a second reply can land after the first has already moved the
            // wake on — into the pad, the app-list retry or the link match — and it must not
            // start any of them a second time.
            if (!wakeWaitTimer.running) return
            // No answer yet: StreamTweak is still coming up, or this host does not know the
            // command. The two look identical from here, so we keep asking until the wait
            // times out — and a host that genuinely never answers ends up where it belongs,
            // at the link match, without ever offering a pad it cannot back up.
            if (!supported) return

            console.log("[unlock] LOCKSTATE answered: locked=" + locked)
            wakeWaitTimer.stop()
            // StreamTweak is up: that is what this row was waiting for, whichever way the
            // answer goes. The pad and the link match are what happens next, not part of it.
            homeScreen._wakeEndStep(2, true)
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
        waitForStreamTweak: homeScreen._wakeWaitsForStreamTweak
        stepStart:  homeScreen.wakeStepStart
        stepMs:     homeScreen.wakeStepMs
        stepResult: homeScreen.wakeStepResult
        nowMs:      homeScreen.wakeNow
        onCancelled: homeScreen._endWake()
    }

    // Used to find one app by name and launch it: the Desktop for an unlock, and the game
    // behind Last played for a resume. Initialised on demand so a normal session never pays
    // for it, and re-initialised each time — which initialize() explicitly supports.
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
    property string linkRestoreHostUuid: ""                // by uuid, see hostIndexOf()
    readonly property int linkRestoreHostIndex: hostIndexOf(linkRestoreHostUuid)
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
    // By uuid, see hostIndexOf(): the question is recorded when a session ends — possibly
    // hours after it began — and asked later still.
    property string linkAskHostUuid: ""
    readonly property int linkAskHostIndex: hostIndexOf(linkAskHostUuid)
    property string linkAskHostName: ""
    property bool   linkAskProbing: false

    // ── Holding the self-update's Install now ─────────────────────────────────
    // Installing quits the app, and everything below is state a quit would strand: a wake
    // whose last step is a link match (wakeActive stays true through it, after the dialog has
    // closed), a link being put back or changing, and the question above still to be asked —
    // the host holds the streaming speed until asked, so quitting past the question leaves it
    // there. Settings can be reached while any of these is under way, which is why it is
    // decided here, where the state lives, and not assumed away. See AppUpdate::installBlocked.
    //
    // ⚠️ The pending question only clears on the trip that asks it — host page to host list.
    // Settings returns to the page it was opened from, so the ordinary path gets there; a jump
    // straight to Home would leave it pending until the app restarts, since it is not persisted.
    Binding {
        target: AppUpdate
        property: "installBlocked"
        value: homeScreen.wakeActive
               || homeScreen._lcUuid !== ""
               || homeScreen.linkRestoreActive
               || homeScreen.linkAskHostIndex >= 0
               || homeScreen.linkAskProbing
               || linkRestoreDialog.opened
               || (!!homeScreen.currentHost && homeScreen.currentHost.linkChanging === true)
    }

    function noteStreamEnded(idx, hostName) {
        if (idx < 0) return
        linkAskHostUuid = hostUuidAt(idx)
        linkAskHostName = hostName
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

        var uuid = linkAskHostUuid
        var name = linkAskHostName
        // Asked once per session either way: whether they say yes, say no, or the host turns
        // out to have nothing to put back, the question is spent until the next session.
        linkAskHostUuid = ""
        linkAskHostName = ""

        if (info.switched !== true) return          // nothing was ever switched
        if (info.sessionActive === true) return     // still streaming, or paused and resumable

        linkRestoreDialog.hostName = name
        linkRestoreDialog.pcUuid   = uuid
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
        linkRestoreHostUuid  = hostUuidAt(idx)
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
        linkRestoreHostUuid = ""
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
    // By uuid, see hostIndexOf(): the scan runs for minutes before Install is pressed, and
    // Install is the one command here that reboots a machine.
    property string updateJobHostUuid: ""
    readonly property int updateJobHostIndex: hostIndexOf(updateJobHostUuid)
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
        updateJobHostUuid = hostUuidAt(index)
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
    // Both by uuid, see hostIndexOf(): a dismissal filed under a row would silence whichever
    // host took that row next, and leave the one dismissed free to nag again.
    property string stPinHostUuid: ""
    property var    stPinDismissed: ({})   // uuid -> true while the user has dismissed it
    function stShowPin(idx, name, addr, pin) {
        var uuid = hostUuidAt(idx)
        if (stPinDismissed[uuid]) return    // don't re-nag after a manual dismiss
        stPinValue = pin
        stPinHostName = name
        stPinHostAddr = addr
        stPinHostUuid = uuid
        stPinDialog.open()
    }
    function stHidePin(idx) {
        var uuid = hostUuidAt(idx)
        if (stPinHostUuid === uuid) {
            stPinHostUuid = ""
            stPinDialog.close()
        }
        delete stPinDismissed[uuid]         // state changed → allow showing again later
    }
    function stForcePin(idx) {              // user explicitly re-requested access
        delete stPinDismissed[hostUuidAt(idx)]
    }

    Component.onCompleted: {
        ComputerManager.computerAddCompleted.connect(addComplete)

        // The host the app opens on is selected without tabIndex moving: name it (see
        // _selectedUuid).
        if (!addTabSelected) _selectedUuid = hostUuidAt(tabIndex)

        // ⚠️ Deferred by a tick rather than opened here: on the first launch after the
        // 5.4.0 store change the host list is empty, and opening a modal Popup while
        // this screen is still being built puts it up before Overlay.overlay has the
        // size it centres on.
        if (SystemProperties.settingsWereReset) {
            Qt.callLater(function() { settingsResetDialog.open() })
        }
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
            errorDialog.text = qsTr("Could not connect to that host.")
            if (detectedPortBlocking) {
                errorDialog.text += "\n\n" + qsTr("This network is blocking StreamLight. Streaming over the Internet may not work while you are on it.")
            } else {
                errorDialog.helpText = qsTr("Open Help for possible solutions.")
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
            readonly property bool   pBusy:    model.busy

            // The unlock's Desktop session has really gone once the host stops reporting it.
            onPBusyChanged: if (!pBusy && homeScreen._unlockCarrierIndex === index)
                                homeScreen._unlockCarrierUuid = ""

            // This host's StreamTweak switch. Every probe below is bound to it, so turning it
            // off in Settings silences them in the same frame.
            readonly property bool   stEnabled: model.streamTweakEnabled

            property string stAuth: ""

            /*
             * The host is at its lock screen (7.0.0, #35): the card offers Unlock instead of
             * Open, and the host page is not entered — AppShell leaves it for Home when this
             * turns true while it is open. Polled with LOCKSTATE beside STATUS and NETINFO.
             *
             * ⚠️ Silence is not "unlocked" and not "locked" either: one missed answer keeps the
             * last value, and only three in a row drop it — so a single slow reply does not
             * flip the button, and a host whose StreamTweak has gone does not stay "locked".
             */
            property bool   stLocked: false
            property int    _lockMisses: 0
            onStLockedChanged: homeScreen.hostLockChanged(homeScreen.hostUuidAt(index), stLocked)
            onPOnlineChanged:  if (!pOnline) { stLocked = false; _lockMisses = 0 }
            onStAuthChanged:   if (stAuth !== "authorized") { stLocked = false; _lockMisses = 0 }

            // 8.0.0: from the CAPS reply that opens every access check — does the host have
            // the host-section verbs (StreamTweak 10.0.0), and which version is it.
            property bool   stHostSection: false
            property string stHostVersion: ""

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

            // What this client last played on this host: name, artwork, hours, and how the
            // last session went. Read off local disk by ComputerModel::lastPlayedFor() — no
            // bridge, no host, no poll. An empty map means the card draws no block at all:
            // nothing streamed here yet, the record was reset, or the game has left the
            // host's app list and could not be launched anyway.
            property var    stLastPlayed: ({})

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
                    asleep:            model.asleep,
                    serverRestart:     model.serverRestart,
                    pyroWaveTest:      model.pyroWaveTest,
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
                    stageImage:        model.stageImage,
                    stageOpacity:      model.stageOpacity,
                    auth:              stAuth,
                    stEnabled:         stEnabled,
                    hostSection:       stHostSection,
                    hostVersion:       stHostVersion,
                    linkText:          homeScreen.formatStreamTweakStatus(stSpeedRaw),
                    willSwitchLink:    willSwitchLink,
                    cantSwitchLink:    cantSwitchLink,
                    localMbps:         stLocalMbps,
                    hostMbps:          stHostMbps,
                    linkChanging:      stLinkChanging,
                    linkSwitched:      stSwitched,
                    allowsLinkControl: stAllowsLink,
                    // The EFFECTIVE setting (profile over global), not the singleton — see the
                    // note on stMatchLink. The Options tile has to know it or it offers an
                    // action LinkMatcher will decline without a word.
                    matchLink:         stMatchLink,
                    sessionActive:     stSessionActive,
                    locked:            stLocked,
                    lastPlayed:        stLastPlayed,
                    // What the host is streaming right now (6.0.0). Read here, on every push,
                    // because model.busy and model.online are in _watch: a session starting or
                    // ending re-evaluates the record, and with it this. A cheap in-memory
                    // lookup, no request.
                    runningApp:        homeScreen._runningFor(index, model.busy && model.online)
                }
            }

            /*
             * Re-reads what this client last played on this host. Called when the list comes
             * back into view, because the usual reason for that is a session having just
             * ended.
             *
             * ⚠️ A local read, not a request. Until 5.6.1 this asked the host over the bridge
             * and a ten-second poll had to sit here as well, because leaving the host page
             * does not end the session on the host — the streaming server holds it open to be
             * resumed and StreamTweak only files it once its grace expires, so the first
             * answer described the session BEFORE the one that just ended. Nothing races any
             * more: the record is written by Session::endPlaytime() before the UI is back,
             * so one read at the right moment is always right. The poll went with it.
             */
            function refreshLastPlayed() {
                probe.stLastPlayed = homeScreen.computerModel.lastPlayedFor(index)
            }

            function push() { if (isCurrent) homeScreen.currentHost = record() }

            /*
             * This host is streaming right now (6.0.0), for the background's waves —
             * see anyStreaming on the root. The SAME expression as the card's runningApp
             * above, on purpose: the flow must speed up exactly when "Streaming now" shows,
             * which means the PIN unlock's own Desktop session and a wake in progress do not
             * count. _runningFor() already knows both; a second rule here would drift from it.
             *
             * It re-evaluates on model.busy / model.online and on the wake and unlock state
             * that _runningFor() reads — the same dependencies that keep the card current.
             */
            readonly property bool pStreaming:
                homeScreen._runningFor(index, model.busy && model.online).name !== undefined

            // An array binding re-evaluates when any element does, and yields a new object
            // each time, so this fires on every model role change without needing a handler
            // per role. It is the cheapest way to keep one plain record in step with a model.
            readonly property var _watch: [
                model.name, model.online, model.paired, model.busy, model.statusUnknown,
                model.wakeable, model.serverSupported, model.address, model.physicalAddress,
                model.tailscaleAddress, model.hasTailscale, model.tailscaleActive,
                model.gpuModel, model.profileCount, model.activeProfileSlot,
                model.activeProfileName, model.stageImage,
                model.stageOpacity, model.serverRestart, stAuth, stSpeedRaw, stEnabled, stHostSection, stHostVersion,
                willSwitchLink, cantSwitchLink, stLastPlayed, stMatchLink,
                stLinkChanging, stSwitched, stAllowsLink, stSessionActive, stLocked,
                // What _runningFor() reads to hide the PIN unlock's own session (6.0.0).
                homeScreen.wakeActive, homeScreen.wakeIndex, homeScreen._unlockCarrierIndex
            ]
            on_WatchChanged: push()
            // Refresh on arrival as well as on the timer: a host the user has just tabbed to
            // stopped being polled while it was off screen, so waiting out the ten seconds
            // would show them the panel as it was when they last looked at it.
            onIsCurrentChanged: { push(); if (isCurrent) refreshLastPlayed() }
            // Switched off after having been on: the probes stop by themselves, but the values
            // they left behind would not, so the access chip and every Options tile gated on
            // "authorized" would stay on screen describing an integration that is gone.
            onStEnabledChanged: {
                if (stEnabled) {
                    if (model.online && model.paired)
                        homeScreen.computerModel.requestStreamTweakAuth(index)
                    return
                }
                stAuth = ""
                stHostSection = false
                stHostVersion = ""
                stSpeedRaw = ""
                stAllowsLink = false
                stSwitched = false
                stSessionActive = false
                stLinkChanging = false
                stLocalMbps = 0
                stLastPlayed = ({})
                stLocked = false
                _lockMisses = 0
            }

            Component.onCompleted: {
                // Before push(), so the first record already carries it and the card draws
                // the block on its first frame instead of a beat later. It costs a settings
                // read and reaches no network, unlike everything else in here.
                refreshLastPlayed()
                push()
                // Access only. Asking for STATUS here used to make sense; it no longer can,
                // because STATUS now requires an access state and there is none yet — the
                // reply comes back empty and the auth answer starts the STATUS timer anyway.
                if (stEnabled && model.online && model.paired)
                    homeScreen.computerModel.requestStreamTweakAuth(index)
            }

            Timer {
                interval: 2000
                repeat: true
                // ⚠️ The access condition is not tidiness: STATUS answers ERR_UNAUTHORIZED to
                // anyone who isn't approved and nothing at all to a host without StreamTweak,
                // so without it this asked a question it could not get an answer to — every
                // two seconds, on every paired host, for as long as the app was open, and
                // during a stream too, since HomeScreen is never unloaded. The revocation
                // path still works: it fires while we believe we ARE authorized, which is
                // exactly when this timer runs.
                running: probe.stEnabled && model.online && model.paired
                         && (probe.stAuth === "authorized" || probe.stAuth === "open")
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
                running: probe.stEnabled && model.online && model.paired
                         && probe.stAuth === "authorized"
                onTriggered: homeScreen.computerModel.requestHostNetInfo(index)
            }

            // LOCKSTATE, for stLocked. Same gate as NETINFO: only an approved client is answered.
            // ⚠️ Its replies also reach the wake, the launch check and the PIN pad's own polling,
            // which all listen to lockStateReceived; each of them guards on its own state, and
            // an answer from here means what their own request would have meant.
            Timer {
                interval: 2500
                repeat: true
                running: probe.stEnabled && model.online && model.paired
                         && probe.stAuth === "authorized"
                triggeredOnStart: true
                onTriggered: homeScreen.computerModel.requestLockState(index)
            }

            // Poll the access state until it settles (authorized, or "open" when the host
            // doesn't enforce auth). A later switch to enforced auth is still caught via the
            // STATUS ERR_UNAUTHORIZED path.
            Timer {
                interval: 2500
                repeat: true
                // ⚠️ Without the switch in this condition, this ran forever on any host that
                // doesn't run StreamTweak: the reply is "none", which is neither "authorized"
                // nor "open", so the settling condition was never met.
                running: probe.stEnabled && model.online && model.paired
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

                function onLockStateReceived(idx, supported, locked) {
                    if (idx !== index) return
                    if (supported) {
                        probe._lockMisses = 0
                        probe.stLocked = locked
                        return
                    }
                    if (++probe._lockMisses >= 3) probe.stLocked = false
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

                function onStreamTweakCapsReceived(idx, hostSection, version) {
                    if (idx !== index) return
                    probe.stHostSection = hostSection
                    probe.stHostVersion = version
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
        // The first host to show up is selected without tabIndex moving, so it is named here.
        if (_selectedUuid === "" && !addTabSelected) _selectedUuid = hostUuidAt(tabIndex)
    }

    onTabIndexChanged: {
        if (addTabSelected) currentHost = null
        _selectedUuid = addTabSelected ? "" : hostUuidAt(tabIndex)
        _slideStage()
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Navigation
    // ═════════════════════════════════════════════════════════════════════════
    /*
     * Two zones, one row each, and the shoulders (host) and triggers (profile) cut across both.
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

        // LT / RT — the focused host's profile, as on the host page (7.0.0: they switched the
        // host until 13/10/2026; the hosts are on LB/RB now, handled in AppShell with
        // PgUp/PgDn so they work wherever the focus is).
        // ⚠️ Handled here, on the page, rather than as global Shortcuts. Key events reach the
        // focused item first, so a host-name field or the add-host dialog swallows the letter
        // and typing "Peppe" cannot shut a machine down. A global Shortcut would fire through
        // any text field, which is the reason single-key shortcuts are usually avoided at all.
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_F14) {
                homeScreen.cycleFocusedProfile(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_F15) {
                homeScreen.cycleFocusedProfile(1)
                event.accepted = true
            }
            // Keyboard equivalents of the controller prompts. Q/E mirror LT/RT (profile);
            // PgUp/PgDn mirror LB/RB (host) and are handled in AppShell so they work wherever
            // the focus is, exactly like the shoulders do.
            else if (event.key === Qt.Key_Q) {
                homeScreen.cycleFocusedProfile(-1); event.accepted = true
            } else if (event.key === Qt.Key_E) {
                homeScreen.cycleFocusedProfile(1);  event.accepted = true
            } else if (event.key === Qt.Key_P) {
                homeScreen.openPowerForCurrent(); event.accepted = true
            } else if (event.key === Qt.Key_S) {
                if (appShell) appShell.openSettings(); event.accepted = true
            }
            // The theme toggle (7.0.0): right stick click (Key_F19) on the pad, T on the keyboard.
            else if (event.key === Qt.Key_F19 || event.key === Qt.Key_T) {
                if (appShell) appShell.toggleTheme(); event.accepted = true
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
        anchors.leftMargin: homeScreen._px(44)
        anchors.rightMargin: homeScreen._px(44)
        /*
         * 22 and 60 are the clock's own top and height, not free numbers — at the clock's own
         * scale too (see homeScreen._u), or they stop matching the moment the window is not
         * 1330 wide.
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
        anchors.topMargin: homeScreen._px(22)
        height: homeScreen._px(60)

        Image {
            id: brandIcon
            source: "qrc:/streamlight.ico"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: homeScreen._px(46); height: homeScreen._px(46)
            // Rasterise at device-pixel resolution so HiDPI displays get a sharp image
            // regardless of Windows scaling (100% / 150% / 200%).
            sourceSize.width:  homeScreen._px(46) * Screen.devicePixelRatio
            sourceSize.height: homeScreen._px(46) * Screen.devicePixelRatio
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }

        Column {
            anchors.left: brandIcon.right
            anchors.leftMargin: homeScreen._px(14)
            anchors.verticalCenter: brandIcon.verticalCenter
            // The cluster's own spacing, so the gap under the wordmark matches the gap under
            // the clock opposite it.
            spacing: homeScreen._px(4)

            // Uniform wordmark: "STREAMLIGHT" all caps, single size, Black weight, wide
            // letter-spacing. Avoids the optical-weight mismatch of synthesised small-caps.
            Label {
                text: "STREAMLIGHT"
                font.family: Theme.family
                font.pixelSize: homeScreen._px(Theme.fontH1)
                font.weight: Font.Black
                font.bold: true
                font.letterSpacing: 3.6 * homeScreen._u
                color: Theme.text
            }

            Label {
                text: qsTr("a Moonlight fork")
                font.family: Theme.family
                font.pixelSize: homeScreen._px(Theme.fontSmall)
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
        anchors.topMargin: homeScreen._px(18)
        anchors.leftMargin: homeScreen._px(44)
        anchors.rightMargin: homeScreen._px(44)
        height: homeScreen._px(44)

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
            spacing: homeScreen._px(14)

            // One host is enough: the strip always carries "Add a host" as its last tab, so
            // there are still two stops and the shoulders do move. Requiring two would hide
            // them exactly when someone is looking for how to add another.
            Repeater {
                model: homeScreen.hostCount >= 1
                       ? [{ btn: "LB", key: "PgUp", dir: -1 }] : []
                delegate: Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: homeScreen._px(44); height: homeScreen._px(30)
                    ActionHint {
                        anchors.centerIn: parent
                        buttonKey: modelData.btn
                        keyLabel:  modelData.key
                        size: homeScreen._px(28)
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
            spacing: homeScreen._px(8)

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

                    height: homeScreen._px(40)
                    width: tabContent.implicitWidth + homeScreen._px(40)
                    radius: homeScreen._px(9)
                    color: _selected || _hovered ? Theme.cardHigh : "transparent"
                    border.width: _focused ? 2 : 1
                    border.color: _focused  ? Theme.ink
                                : _selected ? Theme.lineHigh
                                :             "transparent"

                    Row {
                        id: tabContent
                        anchors.centerIn: parent
                        spacing: homeScreen._px(10)

                        // Connectivity at a glance, so a strip of five hosts says which ones
                        // are up without selecting each in turn.
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !tabItem._isAdd
                            width: homeScreen._px(8); height: width
                            radius: width / 2
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
                            font.pixelSize: homeScreen._px(Theme.fontTitle)
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
                       ? [{ btn: "RB", key: "PgDn", dir: 1 }] : []
                delegate: Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: homeScreen._px(44); height: homeScreen._px(30)
                    ActionHint {
                        anchors.centerIn: parent
                        buttonKey: modelData.btn
                        keyLabel:  modelData.key
                        size: homeScreen._px(28)
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
    /*
     * Changing host slides the stage (7.0.0): the new host's card comes in from the side of
     * the direction you moved (LT/RT, or a tab clicked to the right or left) and fades up. The
     * card's content swaps at once underneath; this is only how it arrives.
     */
    property int _lastTabIndex: 0
    function _slideStage() {
        var dir = tabIndex > _lastTabIndex ? 1 : -1
        _lastTabIndex = tabIndex
        if (Theme.reduceAnimations) return
        stageShift.x = dir * homeScreen._px(60)
        hostStage.opacity = 0.25
        stageIn.restart()
    }
    ParallelAnimation {
        id: stageIn
        NumberAnimation { target: stageShift; property: "x"; to: 0; duration: 200; easing.type: Easing.OutCubic }
        NumberAnimation { target: hostStage; property: "opacity"; to: 1; duration: 160; easing.type: Easing.OutCubic }
    }

    HostStage {
        id: hostStage
        transform: Translate { id: stageShift }
        anchors.top: tabStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: homeScreen._px(14)
        anchors.leftMargin: homeScreen._px(44)
        anchors.rightMargin: homeScreen._px(44)
        anchors.bottomMargin: homeScreen._px(18)

        readonly property var _h: homeScreen.currentHost

        addMode:     homeScreen.addTabSelected
        discovering: StreamingPreferences.enableMdns

        hostName:          _h ? _h.name              : ""
        online:            _h ? _h.online            : false
        paired:            _h ? _h.paired            : false
        statusUnknown:     _h ? _h.statusUnknown     : false
        wakeable:          _h ? _h.wakeable          : false
        locked:            _h ? _h.locked === true   : false
        asleep:            _h ? _h.asleep            : false
        serverRestart:     _h && _h.serverRestart ? _h.serverRestart : ""
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
        streamTweakReason: homeScreen.streamTweakReasonFor(_h)
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
        lastPlayed:        (_h && _h.lastPlayed) ? _h.lastPlayed : ({})
        runningApp:        (_h && _h.runningApp) ? _h.runningApp : ({})

        // The physical (LAN) address stays the headline even when we are reaching the host
        // over Tailscale — the 100.x one gets its own field rather than replacing it.
        address: _h ? (_h.hasTailscale
                       ? (_h.physicalAddress && _h.physicalAddress.length ? _h.physicalAddress : _h.address)
                       : _h.address)
                    : ""

        // 7.0.0: the card is the theme's surface; a host may only add a picture over it.
        backdropImage: (_h && _h.stageImage) ? _h.stageImage : ""
        // Percent in the model, 0..1 on the card. The model already applies the default to a
        // host that never set one, so the fallback here only covers the moment before a record.
        backdropOpacity: ((_h && _h.stageOpacity) ? _h.stageOpacity
                                                  : homeScreen.computerModel.stageOpacityDefault) / 100

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
    // `forHost` (8.0.0): a record from hostRecordFor(), when the host section asks for a host
    // that is not necessarily the one selected on the strip.
    function runAction(kind, forHost) {
        var h = forHost ? forHost : currentHost

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
        case "unlock":
            startUnlock(h)
            break

        case "open":
            if (h.statusUnknown) return
            // Locked between the card being drawn and the press: unlock rather than open a
            // page AppShell would leave at once (7.0.0, #35).
            if (h.locked) { startUnlock(h); return }
            if (!h.serverSupported) {
                errorDialog.text = qsTr("The version of GeForce Experience on %1 is not supported by this build of StreamLight. You must update StreamLight to stream from %1.").arg(h.name)
                errorDialog.helpText = ""
                errorDialog.open()
                return
            }
            appShell.showApps(h.index, computerModel, false,
                              h.name, h.address, h.gpuModel, h.isTailscaleClone)
            break

        case "continue":
            resumeLastPlayed(h)
            break

        case "resume":
            resumeRunning(h)
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

        // The dialogs below are handed the host by uuid and read its row back through a
        // binding: any of them can stay open while the list moves (see hostIndexOf()).
        case "profiles":
            hostProfilesDialog.pcUuid   = hostUuidAt(h.index)
            hostProfilesDialog.hostName = h.name
            hostProfilesDialog.reload()
            hostProfilesDialog.open()
            break

        case "options":
            hostOptionsDialog.hostName = h.name
            hostOptionsDialog.items    = menuItemsFor(h)
            hostOptionsDialog.open()
            break

        case "pyrowaveTest":
            pyroWaveTestDialog.hostUuid = hostUuidAt(h.index)
            pyroWaveTestDialog.hostName = h.name
            pyroWaveTestDialog.open()
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
            renamePcDialog.pcUuid = hostUuidAt(h.index)
            renamePcDialog.originalName = h.name
            renamePcDialog.open()
            break

        case "wakeMac":
            wakeMacDialog.pcUuid = hostUuidAt(h.index)
            wakeMacDialog.pcName = h.name
            wakeMacDialog.open()
            break

        case "delete":
            deletePcDialog.pcUuid = hostUuidAt(h.index)
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
            stageBackgroundDialog.opacityMin     = homeScreen.computerModel.stageOpacityMin
            stageBackgroundDialog.currentOpacity = h.stageOpacity
                                                   || homeScreen.computerModel.stageOpacityDefault
            stageBackgroundDialog.pcUuid       = hostUuidAt(h.index)
            stageBackgroundDialog.open()
            break

        case "requestStAuth":
            stForcePin(h.index)
            computerModel.requestStreamTweakAuth(h.index)
            break

        // 8.0.0: the host section, from StreamTweak's mark on the card.
        case "streamtweak":
            if (streamTweakReasonFor(h).length > 0) return
            appShell.openHostSection(hostUuidAt(h.index), computerModel, h.name)
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
            powerDialog.pcUuid            = hostUuidAt(h.index)
            powerDialog.hostName          = h.name
            powerDialog.authState         = h.auth
            // This device's modes are read here, on every open; the host's arrive over the
            // bridge (POWERCAPS) and fill its row in when they do.
            powerDialog.clientModes       = SystemProperties.clientPowerModes()
            powerDialog.hostCaps          = "checking"
            powerDialog.hostModes         = []
            powerDialog.hostWakeLan       = true
            powerDialog.hostWakeable      = h.wakeable
            // A magic packet is a LAN broadcast: through Tailscale it never reaches the host.
            powerDialog.hostAway          = h.tailscaleActive || h.isTailscaleClone
            // Seed update status: client read synchronously; host arrives async.
            powerDialog.clientUpdateState = SystemProperties.updatesPending() ? "pending" : "none"
            if (h.auth === "authorized") {
                powerDialog.hostUpdateState = "checking"
                computerModel.requestUpdateState(h.index)
                computerModel.requestPowerCaps(h.index)
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

    // ═════════════════════════════════════════════════════════════════════════
    // StreamLight 8.0 — the host section (HostSectionScreen.qml)
    // ═════════════════════════════════════════════════════════════════════════

    /*
     * Why StreamTweak's logo on the card cannot open the section right now, or "" when it can.
     * Shown greyed with this reason, never taken away (plan 8.0 / 10.0, "Accesso").
     *
     * ⚠️ Since 21/10/2026 the section also holds the integration's own switch for this host (its
     * Setup tab, moved out of Settings), so it opens whenever there is a host to talk about: with
     * StreamTweak switched off, not found, too old, pending or denied it opens on Setup alone,
     * which says which. Only a host that is not paired — the bridge trusts the pairing — or is still
     * being checked keeps it grey.
     */
    function streamTweakReasonFor(h) {
        if (!h) return ""
        if (h.statusUnknown)           return qsTr("Checking the host…")
        if (!h.paired && h.online)     return qsTr("Pair this host first")
        return ""
    }

    // The live record of a host, by uuid — what the card reads, for the section to read too.
    function hostRecordFor(uuid) {
        var p = hostProbes.itemAt(hostIndexOf(uuid))
        return p ? p.record() : null
    }

    /*
     * Runs one of the card's actions for a host the section names by uuid: "linkSpeed",
     * "updateHost", "requestStAuth". Through runAction, so the section and the old Options
     * tiles share one implementation — the restore watch, the update flow, the PIN pop-up.
     */
    function runActionFor(uuid, kind) {
        var r = hostRecordFor(uuid)
        if (r) runAction(kind, r)
    }

    /*
     * The link control of the Dashboard's Network card: both directions and the four reasons
     * for grey. Moved here from the Options tile it was until 7.0.x.
     *
     * The link, by hand. Not the 4.6.0 tile that was removed: that one sent PREPARE and let the
     * *host* pick a speed it had configured, which is the whole design the 8.1.0 pair undid.
     * This asks for the same speed the launch would ask for, so it can only agree with it —
     * either putting the link back now, or getting it ready so the next launch doesn't have to
     * wait. Greyed with the reason when the host isn't offering the control.
     *
     * ⚠️ Two ways to be unable to do this. The host refusing (allowsLinkControl), and the
     * *client's own* matching being off — globally or by the active profile — which once left
     * the control looking live while LinkMatcher declined in silence. Restoring is always
     * sensible, so only the matching direction depends on the preference.
     *
     * ⚠️ The session clause applies to the matching direction only. A restore asked for during
     * a live stream is not refused — the host schedules it and puts the link back once the
     * stream is done — so greying that one would be lying the other way. Last in the chain
     * because "matching is off" is the actionable reason when both are true.
     */
    function linkActionFor(uuid) {
        var h = hostRecordFor(uuid)
        if (!h || h.auth !== "authorized") return { shown: false }
        var restoring = h.linkSwitched === true
        // 8.0.0: matching that would change nothing is greyed too — this device's wired link is
        // unknown here (Wi-Fi, Tailscale, no rate), or the host is not faster than it. Restoring is
        // never affected: putting the link back is always a real change.
        var why = h.linkChanging                     ? qsTr("changing")
                : !h.allowsLinkControl               ? qsTr("host declined")
                : (!restoring && !h.matchLink)       ? qsTr("matching is off")
                : (!restoring && h.sessionActive)    ? qsTr("host busy")
                : (!restoring && !(h.localMbps > 0)) ? qsTr("no wired link to compare from this device")
                : (!restoring && !(h.hostMbps > h.localMbps)) ? qsTr("already at this device's speed")
                : ""
        return { shown: true, restoring: restoring,
                 label: restoring ? qsTr("Restore NIC speed") : qsTr("Match link speed"),
                 disabled: why.length > 0, reason: why,
                 linkText: h.linkText, allowsLinkControl: h.allowsLinkControl === true }
    }

    // Tiles for the Options popup: { kind, icon (emoji) | iconSource, label, danger? }.
    // In the order of what you came for (09/10/2026): getting in (Tailscale, Wake),
    // a test, reading about it, making it yours, and Delete at the very end. Since 8.0.0
    // Options is the host as a streaming server; what StreamTweak does is in the host section.
    function menuItemsFor(h) {
        var items = []
        // All Apps is gone (7.0.0): it opened the same page as Open on the card. Its one
        // difference, showing hidden games, had nothing to show — nothing in StreamLight can
        // hide a game — and its place went to PyroWave Test, so the grid stays at nine.
        // Tailscale: opens the host's apps over the 100.x endpoint. Greyed (non-clickable)
        // when Tailscale isn't installed on this client.
        if (h.online && h.paired && h.hasTailscale)
            items.push({ kind: "tailscale", iconSource: Theme.light ? "qrc:/res/tailscale_dark.svg" : "qrc:/res/tailscale.svg",
                         label: qsTr("Tailscale"), disabled: !_clientHasTailscale,
                         reason: qsTr("not installed here") })
        if (!h.online && h.wakeable)
            items.push({ kind: "wake", icon: "⏰", label: qsTr("Wake") })
        // 8.1.0 (issue #45): a second MAC for the magic packet, for a PC whose streaming card is
        // not the one that wakes it. The note says one is set; NEW only while it has nothing to say.
        var extraMac = computerModel.hostWakeMacs(h.index).extra || ""
        items.push({ kind: "wakeMac", icon: "🔌", label: qsTr("Wake-on-LAN"),
                     since: extraMac.length ? "" : "8.1.0",
                     note: extraMac.length ? qsTr("+1 MAC") : "" })
        // 8.0.0: Match / Restore link speed, Windows Update and Request Access moved to the
        // host section (StreamTweak's mark on the card): its Dashboard and its access page. Their
        // actions are still here — runAction("linkSpeed" / "updateHost" / "requestStAuth") — and
        // the section calls them through runActionFor(), so one implementation serves both.
        items.push({ kind: "testNetwork", icon: "📡", label: qsTr("Test Network") })
        // PyroWave Test (7.0.0): only on a host that offers PyroWave at all. Greyed with the
        // reason when it can't run; once run, the tile says when and what it found.
        if (h.online && h.paired && h.pyroWaveTest > 0) {
            var pwWhy = !SystemProperties.hasPyroWave ? qsTr("not on this device")
                      : h.pyroWaveTest < 2           ? qsTr("host too old")
                      : h.tailscaleActive            ? qsTr("local network only")
                      : ""
            var pwSaved = pwWhy.length ? ({}) : PyroWaveTest.savedResult(hostUuidAt(h.index))
            // NEW only while the tile has nothing else to say under its name: the tile is too
            // short for a reason or a note AND the badge.
            items.push({ kind: "pyrowaveTest", icon: "〰️", label: qsTr("PyroWave Test"),
                         since: (pwWhy.length || pwSaved.bitrateKbps) ? "" : "7.0.0",
                         disabled: pwWhy.length > 0, reason: pwWhy,
                         note: pwSaved.bitrateKbps ? qsTr("tested %1 · %2 Mbps").arg(pwSaved.testedDay)
                                                                            .arg(Math.round(pwSaved.bitrateKbps / 1000))
                                                   : "" })
        }
        items.push({ kind: "viewDetails", icon: "ℹ️", label: qsTr("Details") })
        items.push({ kind: "rename",      icon: "✏️", label: qsTr("Rename") })
        // since: the release that rebuilt it, for the NEW badge on the tile (7.0.0).
        items.push({ kind: "background",  icon: "🎨", label: qsTr("Background"), since: "7.0.0" })
        // Last, so the destructive one is never where the cursor happens to land.
        items.push({ kind: "delete",      icon: "🗑️", label: qsTr("Delete"), danger: true })
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
        property string pcUuid: ""
        pcIndex: homeScreen.hostIndexOf(pcUuid)
        // Restore gamepad focus so navigation keeps working after the modal closes
        // (otherwise nothing is focused on a handheld).
        onClosed: navRoot.forceActiveFocus()
    }

    // Per-host stage backdrop, a side panel since 7.0.0. Every move is applied to the card at
    // once — the card is the preview — and the panel puts it back itself unless A confirms.
    StageBackgroundDialog {
        id: stageBackgroundDialog
        property string pcUuid: ""
        readonly property int pcIndex: homeScreen.hostIndexOf(pcUuid)
        computerModel: homeScreen.computerModel
        hostIndex: pcIndex
        appShell: homeScreen.appShell
        // Centred beside the card, in the part of the stage the card does not cover.
        dockItem: hostStage
        dockLeft: hostStage._cardW
        onChosen: function(imagePath) {
            if (pcIndex < 0) return
            homeScreen.computerModel.setHostStageBackground(pcIndex, imagePath)
            currentImage = imagePath
        }
        // Applied as it moves, like the colour: the card behind the dialog is the preview.
        onOpacityChosen: function(percent) {
            if (pcIndex < 0) return
            homeScreen.computerModel.setHostStageOpacity(pcIndex, percent)
        }
        onClosed: navRoot.forceActiveFocus()
    }

    ErrorMessageDialog {
        id: errorDialog
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide"
    }

    PairDialog { id: pairDialog }

    // PyroWave Test (7.0.0), from Options. The notice after "Use" is the shell's.
    PyroWaveTestDialog {
        id: pyroWaveTestDialog
        notify: function(text) { if (homeScreen.appShell) homeScreen.appShell.notice(text) }
        onClosed: navRoot.forceActiveFocus()
    }

    NavigableMessageDialog {
        id: deletePcDialog
        property string pcUuid: ""
        readonly property int pcIndex: homeScreen.hostIndexOf(pcUuid)
        property string pcName: ""
        headerText: qsTr("DELETE HOST")
        affirmativeIsDanger: true
        text: qsTr("Are you sure you want to remove '%1'?").arg(pcName)
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: if (pcIndex >= 0) computerModel.deleteComputer(pcIndex)
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
                text = qsTr("The test could not run — none of StreamLight's test servers were reachable. Check this device's Internet connection and try again.")
            } else if (result === 0) {
                text = qsTr("This network does not appear to be blocking StreamLight. If connecting still fails, check the host's firewall.") + "\n\n" +
                       qsTr("To stream over the Internet, run the Moonlight Internet Hosting Tool on the host and use its Internet Streaming Tester.")
            } else {
                text = qsTr("This network is blocking StreamLight. Streaming over the Internet may not work while you are on it.") + "\n\n" +
                       qsTr("The following network ports were blocked:") + "\n"
                text += blockedPorts
            }
            showSpinner = false
        }
    }

    NavigableDialog {
        id: renamePcDialog
        property string label: qsTr("Enter the new name for this host")
        property string originalName
        property string pcUuid: ""
        readonly property int pcIndex: homeScreen.hostIndexOf(pcUuid)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            editText.text = renamePcDialog.originalName
            editText.forceActiveFocus()
            editText.selectAll()
        }
        onClosed: { editText.clear(); navRoot.forceActiveFocus() }
        onAccepted: {
            if (editText.text && pcIndex >= 0) {
                computerModel.renameComputer(pcIndex, editText.text)
            }
        }

        // At the dialog's scale, like the message dialogs it shares its frame with.
        ColumnLayout {
            spacing: renamePcDialog._px(22)

            Label {
                text: qsTr("RENAME HOST")
                font.family: Theme.family
                font.pixelSize: renamePcDialog._px(Theme.fontSmall)
                font.bold: true
                font.letterSpacing: 1.6 * renamePcDialog._u
                color: Theme.text3
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: renamePcDialog.label
                font.family: Theme.family
                font.pixelSize: renamePcDialog._px(Theme.fontTitle)
                color: Theme.text
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: renamePcDialog._px(520)
            }

            TextField {
                id: editText
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: renamePcDialog._px(360)
                implicitHeight: renamePcDialog._px(48)
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
                selectedTextColor: Theme.text
                font.family: Theme.family
                font.pixelSize: renamePcDialog._px(Theme.fontTitle)
                font.bold: true
                horizontalAlignment: TextInput.AlignHCenter
                focus: true

                background: Rectangle {
                    color: Theme.ground
                    radius: renamePcDialog._px(8)
                    border.color: editText.activeFocus ? Theme.ink : Theme.line
                    border.width: editText.activeFocus ? 2 : 1
                }

                // The pad's A opens the on-screen keyboard (8.0.0); Enter on a keyboard accepts.
                Keys.onReturnPressed: function(event) { if (!renameKeyboard.offer(event)) renamePcDialog.accept() }
                Keys.onEnterPressed:  function(event) { if (!renameKeyboard.offer(event)) renamePcDialog.accept() }
                Keys.onSpacePressed:  function(event) { event.accepted = renameKeyboard.offer(event) }
            }
        }

        VirtualKeyboard {
            id: renameKeyboard
            target: editText
            mode: "text"
            onAccepted: { close(); renamePcDialog.accept() }
        }
    }

    // Wake-on-LAN: a second MAC (8.1.0, issue #45) — see WakeMacDialog.qml.
    WakeMacDialog {
        id: wakeMacDialog
        property string pcUuid: ""
        model: computerModel
        pcIndex: homeScreen.hostIndexOf(pcUuid)
        onClosed: navRoot.forceActiveFocus()
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

    // ── One-time notice: 5.4.0 moved off Moonlight's shared settings store ────
    // Opened from Component.onCompleted below, and only ever once — acknowledging it
    // stamps the marker that makes SystemProperties.settingsWereReset false forever.
    SettingsResetDialog {
        id: settingsResetDialog
        onClosed: {
            SystemProperties.acknowledgeSettingsReset()
            navRoot.forceActiveFocus()
        }
    }

    LinkRestoreDialog {
        id: linkRestoreDialog
        property string pcUuid: ""
        pcIndex: homeScreen.hostIndexOf(pcUuid)
        onClosed: navRoot.forceActiveFocus()
        onRestoreChosen: function(index) {
            homeScreen.startLinkRestoreWatch(index, linkRestoreDialog.hostName)
        }
    }

    // ── Power chooser: one row per machine (6.2.0) ────────────────────────────
    // The host always goes first, and this device only follows once the host has taken the
    // command: once this device is asleep or off, nothing is left to send it.
    PowerDialog {
        id: powerDialog
        property string pcUuid: ""
        pcIndex: homeScreen.hostIndexOf(pcUuid)
        onClosed: navRoot.forceActiveFocus()
        onConfirmed: function(hostMode, hostUpdates, clientMode, clientUpdates) {
            if (hostMode === "keep") {
                if (clientMode !== "keep")
                    SystemProperties.powerClient(clientMode, clientUpdates)
                return
            }

            clientPowerTimer.mode = clientMode
            clientPowerTimer.installUpdates = clientUpdates

            // The host this dialog was opened on, or nobody: gone from the list means nothing
            // is sent — and this device, which only ever follows the host, stays on as well.
            if (powerDialog.pcIndex < 0)
                return

            if (powerDialog.hostCaps === "legacy") {
                // StreamTweak older than 8.6.0: the old SHUTDOWN, fire-and-forget, so this
                // device waits a moment for the bridge socket to finish writing it.
                homeScreen.computerModel.shutdownHost(powerDialog.pcIndex, hostUpdates)
                if (clientMode !== "keep")
                    clientPowerTimer.restart()
                return
            }

            // POWER is answered: this device acts on the host's OK (onPowerHostResult).
            clientPowerTimer.pendingUuid = powerDialog.pcUuid
            homeScreen.computerModel.powerHost(powerDialog.pcIndex, hostMode, hostUpdates)
        }
    }

    // Receives the host's update state and power modes (async) and resolves its row.
    Connections {
        target: computerModel
        function onUpdateStateReceived(idx, pending) {
            if (idx === powerDialog.pcIndex)
                powerDialog.hostUpdateState = pending ? "pending" : "none"
        }
        function onPowerCapsReceived(idx, supported, modes, wakeLan) {
            if (idx !== powerDialog.pcIndex)
                return
            if (supported) {
                powerDialog.hostModes   = modes
                powerDialog.hostWakeLan = wakeLan
                powerDialog.hostCaps    = "ready"
            } else {
                powerDialog.hostCaps    = "legacy"
            }
        }
        function onPowerHostResult(idx, mode, ok) {
            if (clientPowerTimer.pendingUuid === ""
                    || idx !== homeScreen.hostIndexOf(clientPowerTimer.pendingUuid))
                return
            clientPowerTimer.pendingUuid = ""
            // A refusal (or no answer) leaves this device on too: switching off the machine in
            // your hands while the host stayed up would hide that anything went wrong.
            if (ok && clientPowerTimer.mode !== "keep")
                clientPowerTimer.restart()
        }
    }

    Timer {
        id: clientPowerTimer
        property string mode: "keep"
        property bool installUpdates: false
        property string pendingUuid: ""          // the host whose OK this device waits for
        // After an answered POWER the host already has the command; the pause only matters
        // for the old fire-and-forget SHUTDOWN, whose socket must finish writing.
        interval: powerDialog.hostCaps === "legacy" ? 1800 : 300
        repeat: false
        onTriggered: SystemProperties.powerClient(clientPowerTimer.mode, clientPowerTimer.installUpdates)
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
    // Its own file since 6.3.1, so it can lay Theme.scrim over the page like every other
    // dialog opened on one — see the note at the top of StreamTweakPinDialog.qml.
    StreamTweakPinDialog {
        id: stPinDialog
        pin: homeScreen.stPinValue
        hostName: homeScreen.stPinHostName
        hostAddress: homeScreen.stPinHostAddr

        /*
         * Every way out is a dismissal: the button (A or a click) and B alike (6.3.1).
         *
         * ⚠️ Only the mouse used to count. B closed the popup through closePolicy without
         * recording anything, so the next access reply — a few seconds later, for as long as
         * the host sat on pending — opened it again: with a pad there was no way to make it
         * stay shut. And the button was a MouseArea the pad could not reach at all.
         *
         * The close stHidePin() makes when the host answers is not one: it clears the uuid
         * first, so nothing is recorded here.
         */
        onClosed: {
            if (homeScreen.stPinHostUuid !== "")
                homeScreen.stPinDismissed[homeScreen.stPinHostUuid] = true
            navRoot.forceActiveFocus()
        }
    }

    // Invoked from main.qml Ctrl+N shortcut.
    function openAddPc() {
        addPcDialog.open()
    }
}
