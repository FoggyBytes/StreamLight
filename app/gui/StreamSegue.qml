// 2.15 rather than the 2.0 this declared: Qt 6 still enforces the declared version per
// property, so anything added after 2.0 — activeFocusOnTab, function-style Connections
// handlers — fails to load the whole type rather than just that line. The unlock mode below
// uses both.
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Window 2.2

import Theme 1.0
import SdlGamepadKeyNavigation 1.0
import Session 1.0
import ShortcutManager 1.0
import SystemProperties 1.0
import StreamingPreferences 1.0
import UiSound 1.0

Item {
    id: streamSegue

    // So Esc reaches us: it is the keyboard half of "show me the host now", the other half
    // being B on the pad. Nothing else on this screen takes focus.
    focus: true
    Keys.onEscapePressed: {
        // In unlock mode there is nothing to reveal — uncovering the logon screen is the one
        // thing that must never happen here — but there has to be a way out. Until the pad
        // appears this screen is the only thing on top of a launch that may never answer: the
        // host was shut down mid-session on 07/08, came back locked, and its /launch never
        // returned, leaving "Connecting…" with no exit at all.
        if (!session) return
        if (unlockMode) _unlockFinish(false)
        else            session.revealStreamWindow(true)
    }

    // X, both halves of it: from a controller it arrives as Key_Menu (that is what
    // SdlGamepadKeyNavigation makes of X while it still owns the pad), from a keyboard as the
    // letter. Nothing else on this screen takes focus, so both land here.
    Keys.onMenuPressed: function(event) { _cancelLaunch(); event.accepted = true }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_X) { _cancelLaunch(); event.accepted = true }
    }

    property Session session
    property string appName
    property url boxArt
    property string stageText : unlockMode ? qsTr("Connecting…") :
                                isResume   ? qsTr("Resuming %1…").arg(appName) :
                                             qsTr("Starting %1…").arg(appName)
    property bool isResume : false
    property bool quitAfter : false

    // ── Remote PIN unlock ─────────────────────────────────────────────────────
    // The session behind this screen exists only to type a PIN into the host's logon
    // screen and is never revealed. The caller sets unlockMode plus the model and index
    // needed to ask the host whether it is still locked, and gets the outcome back
    // through onUnlockResultFn(ok).
    property bool unlockMode : false
    property var  computerModel : null
    property int  pcIndex : -1
    property string hostName : ""
    property var  onUnlockResultFn : null

    property int  _unlockAttempt : 0
    property int  _unlockPolls : 0
    // Did StreamTweak answer at all during this attempt's checks? Reset with the poll count
    // on every submit, because the question is about THIS attempt.
    property bool _unlockHostAnswered : false
    property bool _unlockFinished : false

    function _unlockFinish(ok) {
        if (_unlockFinished) return
        _unlockFinished = true
        lockPollTimer.stop()

        // Tidy up the host's pending declaration. Safe at any point: it clears only the
        // mark waiting to be consumed, never the suppression of a session already running.
        if (computerModel && pcIndex >= 0) computerModel.markUnlockSession(pcIndex, false)
        if (onUnlockResultFn) onUnlockResultFn(ok)
        if (session) session.interrupt()
    }

    // Host link-speed matching (4.6.0). While the host renegotiates, linkMatchDetail holds
    // the change ("2.5 Gbps → 1 Gbps") and the main line says what's happening; afterwards
    // it is cleared, and linkWarning — if the attempt failed — is shown in its place while
    // the launch carries on regardless.
    property string linkMatchDetail : ""
    property string linkWarning : ""

    // A launch that is past the usual span without the host reporting a game on screen —
    // the shape of a title that opens its own launcher, where no game window is ever coming.
    // Set by launchSlowTimer, cleared by anything that ends the wait.
    property bool _launchSlow : false

    // Is this launch holding the stream back at all? Everything that talks about the wait —
    // the B prompt, the "taking longer than usual" line — is silent when it isn't, or it
    // describes a wait that is not happening.
    readonly property bool _waiting : session ? session.waitsForGame : false

    // ── The way out, and when it is worth offering ────────────────────────────
    // Bindings rather than imperative visible= assignments, because in unlock mode the hint
    // has to disappear by itself the moment the PIN pad takes over. Which device each prompt
    // is drawn for is ActionHint's business, not this screen's.
    property bool _revealed : false

    readonly property bool _exitHintOn:
        !_revealed && (unlockMode ? !unlockPad.visible : _waiting)

    // "Give up" is worth offering on every launch, not only the ones holding a picture back:
    // the launches that need it most are the ones where nothing arrives at all. Held back a
    // few seconds by cancelHintTimer so an ordinary launch never shows it.
    property bool _cancelHintOn : false
    readonly property bool _giveUpHintOn:
        !_revealed && !unlockMode && !_cancelRequested && _cancelHintOn

    // Unlock mode goes back to the host list; a normal launch shows the host it is hiding.
    readonly property string _exitHintTail:
        unlockMode ? qsTr("go back") : qsTr("see the host")

    function linkMatchStage(detail)
    {
        linkMatchDetail = detail
    }

    function linkMatchFinished(changed, warning)
    {
        linkMatchDetail = ""
        linkWarning = warning
        // Only now does anything touch the network: the adapter has settled (or we gave up),
        // so the launch can't be cut off by a renegotiation half-way through.
        //
        // 7.0.0: and not before the screen has gone black. session.initialize() runs on the GUI
        // thread and stalls it for a moment; on a black screen that is invisible, in the middle
        // of the fade it was a stutter. _atBlack() starts the loader if this came first.
        _linkReady = true
        if (_black) streamLoader.active = true
    }

    // How many transparent auto-retries remain for a transient "no video from
    // host" failure (host display/encoder still warming up on a cold start).
    // Gated by the user setting; the retry segue passes an explicit decremented
    // value so the cap is honoured regardless of the preference.
    property int noVideoRetries : StreamingPreferences.autoReconnectNoVideo ? 1 : 0

    // Called once, when a stream ends for real. Distinct from the deliberate-stop hook in
    // QuitSegue: this one also covers the game closing itself on the host, which is the
    // case where the host restores its link with nothing on the client to say so.
    property var onSessionEndedFn : null

    // 5.9.0. A launch the host answered with 410 — a finished host action, or a request to
    // confirm by launching again (see Session::launchNotice). Handed to the caller after this
    // screen pops, because only the host page can relaunch the same entry; without a caller the
    // message is shown plainly, never as an error.
    property var onLaunchNoticeFn : null
    property string _noticeText : ""
    property bool _noticeConfirm : false

    function launchNotice(text, needsConfirmation)
    {
        hostSlowTimer.stop()
        _endLaunchWait()
        if (_cancelRequested) return
        _noticeText = text
        _noticeConfirm = needsConfirmation
        streamSegueErrorDialog.text = ""
    }

    // Resume session captured during sessionFinished (while `session` is still
    // valid) and consumed by the delayed retry. `session` is nulled by
    // readyForDeletion before the retry timer fires, so we can't build it later.
    property var _pendingRetrySession : null

    function stageStarting(stage)
    {
        // Update the spinner text
        stageText = qsTr("Starting %1…").arg(stage)

        // Something moved, so the host is alive: give it another full window before
        // complaining. A slow-but-progressing connection must never be called stalled.
        hostSlowTimer.restart()
        _hostSlow = false
    }

    // Every way the wait can end: revealed, failed, over. Kept in one place so a path added
    // later cannot leave the hint on screen behind whatever comes next.
    function _endLaunchWait()
    {
        launchSlowTimer.stop()
        cancelHintTimer.stop()
        _launchSlow = false
    }

    // ── Giving up on a launch ─────────────────────────────────────────────────
    // X, from either device. Everything that follows — the stage failing, the connection
    // ending — is the consequence of this and not something to report, so the flag lives here
    // and every error path checks it.
    property bool _cancelRequested : false
    property bool _sessionStarted  : false

    function _cancelLaunch()
    {
        if (_cancelRequested || _revealed || unlockMode) return
        _cancelRequested = true

        _endLaunchWait()
        hostSlowTimer.stop()
        stageText = qsTr("Cancelling…")
        _override = stageText
        streamSegueErrorDialog.text = ""

        if (session && _sessionStarted) {
            // The session takes it from here: the teardown it triggers ends in
            // sessionFinished(), which is the one path that pops this screen.
            session.cancelLaunch()
        }
        else {
            // Pressed inside the fraction of a second before the session is started at all.
            // startSessionTimer sees the flag and never starts it, so nothing will end and
            // nothing will pop us — do it here.
            SdlGamepadKeyNavigation.enable()
            if (Window.window && Window.window.markStreamJustEnded) {
                Window.window.markStreamJustEnded()
            }
            stackView.pop()
        }
    }

    function stageFailed(stage, errorCode, failingPorts)
    {
        hostSlowTimer.stop()
        _endLaunchWait()

        // A launch the user called off does not get an error over it. Everything that failed
        // from here on failed because they asked it to.
        if (_cancelRequested) return

        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = qsTr("Starting %1 failed: Error %2").arg(stage).arg(errorCode)

        if (failingPorts) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("Check your firewall and port forwarding rules for port(s): %1").arg(failingPorts)
        }
    }

    function connectionStarted()
    {
        hostSlowTimer.stop()

        // The session's own input handler is live from here on, so GUI navigation steps aside
        // — see the note in the loader below for why it is only now and not earlier.
        SdlGamepadKeyNavigation.disable()

        // The stream is up, but the host is still painting its logon screen. Give it a beat,
        // then knock the shade away and hand over to the pad.
        if (unlockMode) {
            unlockArmTimer.start()
            return
        }

        // Nothing is hidden here any more. The stream has begun, but the host is still
        // opening the game and what it is sending meanwhile is a desktop mid-reconfiguration
        // — so this screen stays exactly as it is, and the stream window (created hidden)
        // waits behind it. streamWindowRevealed() is what ends this screen.
        //
        // Only armed when this launch is actually waiting: with the wait off the reveal comes
        // on the first frame, and "taking longer than usual" would be describing a wait that
        // isn't happening — it would only ever appear on a host that has failed to send a
        // picture at all, where it points at the wrong thing.
        if (_waiting) launchSlowTimer.start()
    }

    function streamWindowRevealed()
    {
        _endLaunchWait()

        // The stream window is already up and in front of us, so this happens unseen. Hide
        // the contents first for the same reason the old code did: they must not flash back
        // into view when the StackView pops.
        _contentShown = false
        _revealed = true

        window.hideForStream()
    }

    function displayLaunchError(text)
    {
        hostSlowTimer.stop()
        _endLaunchWait()

        // See stageFailed(): a cancelled launch reports nothing.
        if (_cancelRequested) return

        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = text
        console.error(text)
    }

    function quitStarting()
    {
        // ⚠️ Hand the session-end hook over. This screen is *replaced*, so its own
        // onSessionEndedFn will never run — and the QuitSegue built here used to carry nothing,
        // which meant quitting from inside the stream (the common way to stop) recorded nothing
        // at all. That is why the "put the host's link back?" prompt never appeared: it is armed
        // by a session having ended, and this path never said one had.
        var component = Qt.createComponent("QuitSegue.qml")
        // Avoid the push transition animation
        stackView.replace(stackView.currentItem,
                          component.createObject(stackView, {
                              "appName":           appName,
                              "boxArt":            boxArt,
                              "onQuitSucceededFn": onSessionEndedFn,
                              // From the stream: the window comes back on black, and the
                              // quit curtain rises out of it.
                              "entrance":          "rise",
                              "hostName":          _hostName
                          }),
                          StackView.Immediate)

        // Show the Qt window again to show quit segue
        window.restoreAfterStream()
    }

    function sessionFinished(portTestResult)
    {
        // Before the early returns below: the reconfigure and no-video branches keep this
        // screen alive with a different message, and the hint must not outlive its launch.
        _endLaunchWait()

        // A cancelled launch must not be retried. The no-video branch below would otherwise
        // read the aborted handshake as a host that was slow to send a picture and bring the
        // whole thing straight back.
        if (_cancelRequested) {
            streamSegueErrorDialog.text = ""
            noVideoRetries = 0
        }

        // Live "Stream Settings" reconfigure (4.4.0): the user changed resolution/
        // fps/bitrate/HDR mid-stream, so the session was torn down on purpose to
        // resume with the new parameters. Build the resume session now (while
        // `session` is still valid) and re-launch — one brief reconnect blip.
        if (session && session.hasPendingReconfigure()) {
            _pendingRetrySession = session.createReconfiguredSession()
            if (_pendingRetrySession) {
                SdlGamepadKeyNavigation.enable()
                streamSegueErrorDialog.text = ""
                stageText = qsTr("Applying new settings…")
                _override = stageText
                _contentShown = true
                window.restoreAfterStream()
                reconfigureTimer.start()
                return
            }
            // Couldn't build the reconfigured session — fall through to normal end.
        }

        // Transient "no video from host" on a cold launch: the host's display/
        // encoder is still warming up (common with virtual displays + HDR/AV1).
        // The immediate resume reliably succeeds, so auto-retry once before
        // surfacing an error to the user.
        if (session && session.wasNoVideoTraffic() && noVideoRetries > 0) {
            // Build the resume session NOW, while `session` is still valid — it
            // gets nulled by readyForDeletion before the retry timer fires.
            _pendingRetrySession = session.createRetrySession()
            if (_pendingRetrySession) {
                SdlGamepadKeyNavigation.enable()

                // Suppress the "No video received" error that was queued.
                streamSegueErrorDialog.text = ""

                // ⚠️ The wording does not change. This retry exists to make one launch succeed,
                // so from the user's side it is still that launch — announcing "reconnecting"
                // told them about a failure they were never meant to see, and turned one smooth
                // wait into two visibly different ones. The line simply stays as it was, and
                // the replacement segue is seeded with it below so nothing flickers.
                _contentShown = true
                window.restoreAfterStream()

                noVideoRetryTimer.start()
                return
            }
            // Couldn't build a retry session — fall through to the normal
            // error-handling path below.
        }

        // Not when the failure was diagnosed as host-side: audio reached us, so a line about
        // this network blocking StreamLight contradicts the message it would be appended to.
        // It stays true in general — it just has nothing to do with this failure.
        if (portTestResult !== 0 && portTestResult !== -1 && streamSegueErrorDialog.text
                && !(session && session.hostSideVideoFailure())) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("This network is blocking StreamLight. Streaming over the Internet may not work while you are on it.")
        }

        // An unlock session that ends without the pad having decided anything — the launch
        // failed, the host dropped us, the app was quit from over there. Report it now, or the
        // wake flow waits on a callback that is never coming. No interrupt(): we are already
        // inside the end of that session.
        if (unlockMode && !_unlockFinished) {
            _unlockFinished = true
            lockPollTimer.stop()
            if (computerModel && pcIndex >= 0) computerModel.markUnlockSession(pcIndex, false)
            if (onUnlockResultFn) onUnlockResultFn(false)
        }

        // Past every early return above, so this is a genuine end — not a live-settings
        // reconfigure and not a no-video retry, both of which come straight back.
        if (onSessionEndedFn) {
            onSessionEndedFn()
        }

        // Re-enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable()

        // Suppress stale events + clear launch-guard flag so next launch is allowed.
        if (Window.window && Window.window.markStreamJustEnded) {
            Window.window.markStreamJustEnded()
        }

        // Pop the StreamSegue off the stack if this is a GUI-based app launch
        if (!quitAfter) {
            stackView.pop()
        }

        if (quitAfter && !streamSegueErrorDialog.text) {
            // If this was a CLI launch without errors, exit now
            Qt.quit()
        }
        else {
            // Show the Qt window again after streaming. Not in unlock mode: the window was
            // never hidden there (nothing was ever revealed), and restoreAfterStream()
            // recreates the native window — a visible flicker for no reason.
            if (!unlockMode) window.restoreAfterStream()

            if (_noticeText) {
                if (onLaunchNoticeFn) onLaunchNoticeFn(_noticeText, _noticeConfirm)
                else                  streamSegueErrorDialog.text = _noticeText
            }

            // Display any launch errors. We do this after
            // the Qt UI is visible again to prevent losing
            // focus on the dialog which would impact gamepad
            // users.
            if (streamSegueErrorDialog.text) {
                streamSegueErrorDialog.quitAfter = quitAfter
                streamSegueErrorDialog.open()
            }
        }
    }

    function sessionReadyForDeletion()
    {
        // Garbage collect the Session object since it's pretty heavyweight
        // and keeps other libraries (like SDL_TTF) around until it is deleted.
        session = null
        gc()
    }

    // Replace this segue with a fresh one that resumes the same app. Called a
    // short moment after a transient "no video from host" failure so the host's
    // display/encoder has time to finish warming up.
    function startNoVideoRetry()
    {
        var newSession = _pendingRetrySession
        _pendingRetrySession = null

        if (!newSession) {
            // Couldn't build a retry session — fall back to the normal error path.
            streamSegueErrorDialog.text = qsTr("No video received from host.")
            streamSegueErrorDialog.open()
            return
        }

        var component = Qt.createComponent("StreamSegue.qml")
        if (component.status !== Component.Ready) {
            console.warn("StreamSegue.qml not ready for retry:", component.errorString())
            streamSegueErrorDialog.text = qsTr("No video received from host.")
            streamSegueErrorDialog.open()
            return
        }

        var segue = component.createObject(stackView, {
            "appName":        appName,
            // Carried across, all three. This screen is replaced rather than reused, so
            // anything not handed over is simply lost: without the artwork the retry drew a
            // coverless curtain on the neutral default colours — a visibly different screen
            // for the same launch — without the callback nothing was left to notice the host
            // putting its link speed back when the session finally ended, and without the line
            // of text the wait restarted from "Resuming…" for a launch the user only ever
            // asked to start once.
            "boxArt":           boxArt,
            "onSessionEndedFn": onSessionEndedFn,
            "stageText":        stageText,
            "session":        newSession,
            "isResume":       true,
            "quitAfter":      quitAfter,
            "noVideoRetries": noVideoRetries - 1,
            "entrance":       "cross"
        })
        if (Window.window) Window.window.markStreamLaunching()
        stackView.replace(stackView.currentItem, segue)
    }

    // Replace this segue with a fresh resume carrying the reconfigured session
    // (live "Stream Settings"). Unlike the no-video retry, this is user-initiated
    // and gets a fresh no-video retry budget.
    function startReconfigureResume()
    {
        var newSession = _pendingRetrySession
        _pendingRetrySession = null
        if (!newSession) {
            streamSegueErrorDialog.text = qsTr("Could not apply the new settings.")
            streamSegueErrorDialog.open()
            return
        }

        var component = Qt.createComponent("StreamSegue.qml")
        if (component.status !== Component.Ready) {
            console.warn("StreamSegue.qml not ready for reconfigure:", component.errorString())
            streamSegueErrorDialog.text = qsTr("Could not apply the new settings.")
            streamSegueErrorDialog.open()
            return
        }

        var segue = component.createObject(stackView, {
            "appName":        appName,
            // Same hand-over as the retry above, for the same reason.
            "boxArt":           boxArt,
            "onSessionEndedFn": onSessionEndedFn,
            "session":        newSession,
            "isResume":       true,
            "quitAfter":      quitAfter,
            "noVideoRetries": StreamingPreferences.autoReconnectNoVideo ? 1 : 0,
            "entrance":       "cross"
        })
        if (Window.window) Window.window.markStreamLaunching()
        stackView.replace(stackView.currentItem, segue)
    }

    StackView.onDeactivating: {
        // (toolbar removed in 3.0 redesign — nothing to restore here)

        // Re-enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable()
    }

    // 7.0.0: from Activating, the START of the push. The page underneath stays up for the fade
    // to black (main.qml holds it for entranceHoldMs), and the link match should not wait for
    // that: it runs during the fade. Activated still calls this, for the paths that skip
    // Activating; the flag makes the second call a no-op.
    property bool _started: false
    StackView.onActivating: _start()
    StackView.onActivated: _start()

    function _start() {
        if (_started) return
        _started = true

        // The badges are latched, like the cover: the Session is released while this screen
        // can still be up (a no-video retry holds it for another second and a half).
        _hostName = session.hostName
        _badges = session.streamBadges

        // A launch's own sound. A launch started with A already has it — UiSound drops the
        // second within 200 ms — so this is for the click, and the one place both pass.
        if (entrance === "fade" && !unlockMode) UiSound.play(UiSound.Confirm)
        _beginEntrance()

        // Hook up our signals
        session.stageStarting.connect(stageStarting)
        session.stageFailed.connect(stageFailed)
        session.connectionStarted.connect(connectionStarted)
        session.streamWindowRevealed.connect(streamWindowRevealed)
        session.displayLaunchError.connect(displayLaunchError)
        session.launchNotice.connect(launchNotice)
        session.quitStarting.connect(quitStarting)
        session.sessionFinished.connect(sessionFinished)
        session.readyForDeletion.connect(sessionReadyForDeletion)
        session.linkMatchStage.connect(linkMatchStage)
        session.linkMatchFinished.connect(linkMatchFinished)

        // Ensure the SystemProperties async thread is finished,
        // since it may currently be using the SDL video subsystem
        SystemProperties.waitForAsyncLoad()

        // Hand the curtain the artwork before anything else has a chance to draw: the two
        // colours behind everything are derived from it, once, right here.
        if (session.curtain && boxArt.toString() !== "") {
            session.curtain.setCover(boxArt)
        }

        // Ask the host to match its link speed to ours *before* the loader runs, because
        // that's what calls session.initialize()/start(). Changing the adapter afterwards
        // would drop the link mid-handshake — the mistake the whole 4.6.0/8.1.0 pair exists
        // to undo. linkMatchFinished() sets streamLoader.active, and it always fires: on a
        // host that doesn't support it, on Wi-Fi, on failure, immediately when nothing is
        // needed. Connected above, so a synchronous "nothing to do" still gets through.
        session.beginLinkMatch()
    }

    Timer {
        id: unlockArmTimer
        interval: 1200
        onTriggered: {
            if (!session) return
            session.unlockClick()          // dismiss the shade
            // Made visible and nothing else: the pad puts the focus on its first key itself,
            // and forcing focus onto the pad's root afterwards would take it straight back
            // off that key — leaving the arrows with nothing to move.
            unlockPad.visible = true
            _contentShown = false
        }
    }

    Timer {
        id: lockPollTimer

        // Fast enough that a correct PIN feels immediate, and eight of them cover the few
        // seconds Windows takes to tear the logon screen down.
        interval: 700
        repeat: true
        onTriggered: {
            if (!computerModel || pcIndex < 0) return
            _unlockPolls++
            if (_unlockPolls > 8) {
                lockPollTimer.stop()

                // ⚠️ Running out of checks is not evidence of a wrong PIN. If NOT ONE of the
                // eight replies came back from StreamTweak, the host was silent throughout —
                // it crashed, was stopped, the link blipped — and we have learned nothing
                // about the PIN. Saying "wrong" there blames the user for the host, and
                // because it also burned an attempt a correct PIN could be refused three
                // times and end at "Too many attempts", locking them out over a hiccup.
                //
                // So this case gets its own wording and, deliberately, does NOT count.
                if (!_unlockHostAnswered) {
                    unlockPad.state_ = "mute"
                    unlockPad.pinLength = 0
                    if (session) session.unlockClearPin()
                    return
                }

                _unlockAttempt++
                unlockPad.attempt = _unlockAttempt
                if (_unlockAttempt >= unlockPad.maxAttempts) {
                    unlockPad.state_ = "blocked"
                }
                else {
                    unlockPad.state_ = "wrong"
                    unlockPad.pinLength = 0
                    if (session) session.unlockClearPin()
                }
                return
            }
            computerModel.requestLockState(pcIndex)
        }
    }

    Connections {
        target: unlockMode && computerModel ? computerModel : null

        function onLockStateReceived(index, supported, locked) {
            if (index !== pcIndex || !lockPollTimer.running) return
            // supported=false here means the host stopped answering mid-check — treat it as
            // "not yet", never as success: unlocking is the one thing we must be sure of.
            //
            // But remember that it DID answer at least once, because that is the difference
            // between "your PIN was wrong" and "the host went quiet" when the checks run out.
            if (supported) _unlockHostAnswered = true
            if (supported && !locked) _unlockFinish(true)
        }
    }

    Timer {
        id: noVideoRetryTimer

        // Brief delay so the host's display/encoder finishes warming up before
        // we resume. The cold attempt already gave it the full no-video timeout,
        // so this just covers RTSP teardown/settle of the failed session.
        interval: 1500
        onTriggered: startNoVideoRetry()
    }

    Timer {
        id: reconfigureTimer
        // Short settle for the intentional teardown before resuming with the new
        // stream parameters (live "Stream Settings" reconfigure).
        interval: 700
        onTriggered: startReconfigureResume()
    }

    Timer {
        id: hostSlowTimer

        // The /launch request has a two-minute timeout and, until now, said nothing while
        // it ran: a host that accepts the request and then stalls — Sunshine wedging in its
        // virtual-display setup, seen on 27/07 for 55 s — left the spinner on "Starting …"
        // with no indication that anything was wrong. Restarted on every stage, so this only
        // speaks up when nothing has moved at all.
        interval: 30000
        onTriggered: {
            stageText = qsTr("Still waiting for the host to answer…")
            streamSegue._hostSlow = true
        }
    }

    Timer {
        id: launchSlowTimer

        // From connectionStarted, because that is when the host begins opening the game —
        // the question this answers is "is the game coming?", and counting from anything
        // earlier would expire before it could be asked. Every healthy launch measured
        // reaches the host's "ready" between 9 and 27 s and reveals itself, and the host
        // gives up at 90 s, so this lands after a normal launch is already gone and long
        // before the wait ends on its own.
        interval: 20000
        onTriggered: streamSegue._launchSlow = true
    }

    Timer {
        id: cancelHintTimer

        // Long enough that a launch which simply works never advertises a way out of itself —
        // the same objection that took the B prompt off the default path — and short enough
        // that anyone still looking at this screen and wondering has already been told.
        interval: 5000
        onTriggered: streamSegue._cancelHintOn = true
    }

    Timer {
        id: startSessionTimer
        onTriggered: {
            // Called off during the toast delay: there is nothing to start, and _cancelLaunch
            // has already popped this screen.
            if (streamSegue._cancelRequested) return

            // Garbage collect QML stuff before we start streaming,
            // since we'll probably be streaming for a while and we
            // won't be able to GC during the stream.
            gc()

            // Run the streaming session to completion
            streamSegue._sessionStarted = true
            session.start()
            cancelHintTimer.start()
        }
    }

    Loader {
        id: streamLoader
        active: false
        asynchronous: true

        // Arming the watchdog here, rather than at the one call site that first sets
        // active, covers every path that starts a session: the initial launch, the
        // no-video retry and the live-settings resume.
        onActiveChanged: active ? hostSlowTimer.restart() : hostSlowTimer.stop()

        onLoaded: {
            // ⚠️ GUI gamepad navigation is NOT stopped here — it is deferred to
            // connectionStarted(), for every launch and not just the unlock one.
            //
            // Nothing pumps SDL between here and there: the session's own event loop only
            // starts once the connection is up, so stopping GUI navigation now leaves the pad
            // dead for the entire wait. That is the state the 07/08 hang landed in — a /launch
            // that never answered, the launch screen up, and the pad doing nothing because
            // both halves of the handling were switched off — and it is why X could not be
            // offered as a way out at all.
            //
            // ⚠️ The price is that the gamecontroller subsystem is still held when the session
            // initializes, so SDL sends it no arrival events; SdlInputHandler makes up for
            // them in attachAlreadyConnectedGamepads(). Do not move the disable() back here
            // without reading that: the two are one mechanism.

            // Initialize the session and probe for host/client capabilities
            if (!session.initialize(window)) {
                sessionFinished(0);
                sessionReadyForDeletion();
                return;
            }

            // The stall is over: the curtain may rise now if it was waiting for it.
            if (streamSegue._riseAfterInit) {
                streamSegue._riseAfterInit = false
                Qt.callLater(streamSegue._startRise)
            }

            // Don't wait unless we have toasts to display
            startSessionTimer.interval = 0

            // Display the toasts together in a vertical centered arrangement
            var yOffset = 0
            for (var i = 0; i < session.launchWarnings.length; i++) {
                var text = session.launchWarnings[i]
                console.warn(text)

                // Show the tooltip for 3 seconds
                var toast = Qt.createQmlObject('import QtQuick.Controls 2.2; ToolTip {}', parent, '')
                toast.timeout = 3000
                toast.text = text
                toast.y += yOffset
                toast.visible = true

                // Offset the next toast below the previous one
                yOffset = toast.y + toast.padding + toast.height

                // Allow an extra 500 ms for the tooltip's fade-out animation to finish
                startSessionTimer.interval = toast.timeout + 500;
            }

            // Start the timer to wait for toasts (or start the session immediately)
            startSessionTimer.start()
        }

        sourceComponent: Item {}
    }

    // ── The launch curtain (7.0.0: "Horizon") ─────────────────────────────────
    // Everything below reads Session.curtain and computes nothing of its own. The stream
    // window is created hidden and only appears once the host says the game is on screen, so
    // nothing else ever draws this wait. The look is CurtainHorizon's, shared with QuitSegue;
    // the background is this screen's own and is the one it always had.
    //
    // Sizes are fractions of the window height so the same layout holds from a handheld to
    // a 4K TV without a table of exceptions.
    readonly property var    _c: streamSegue.session ? streamSegue.session.curtain : null
    readonly property real   _h: height > 0 ? height : 1080

    // ⚠️ Read from this screen's own boxArt, never from the curtain (which kept a copy, as
    // coverUrl, until 6.3.1 removed it for having no reader). The curtain belongs to the
    // Session, and the Session is released while this screen is still up: a no-video retry
    // holds it here for another second and a half, and readyForDeletion lands in the middle
    // of that. The stage, the host and the badges are latched for the same reason.
    readonly property bool   _hasCover: boxArt.toString() !== ""
    property int    _stage: 0
    property string _hostName: ""
    property var    _badges: []

    Connections {
        target: streamSegue._c
        function onChanged() { if (streamSegue._c) streamSegue._stage = streamSegue._c.stage }
    }

    // What the status line says. Cancelling and Applying new settings come from this screen
    // and win over the curtain's own title — until 7.0.0 the title always won, so those two
    // lines were written and never seen.
    property string _override: ""
    property bool   _hostSlow: false
    property bool   _contentShown: true
    readonly property string _status: _override !== "" ? _override
                                    : (_c && _c.title !== "") ? _c.title : stageText

    // Connect and Launch for every launch; Load only when it waits for the game, because only
    // then is there a game window to wait for (Marcello, 13/10/2026).
    readonly property var _segments: _waiting ? [qsTr("Connect"), qsTr("Launch"), qsTr("Load")]
                                              : [qsTr("Connect"), qsTr("Launch")]

    // ── The entrance ──────────────────────────────────────────────────────────
    // "fade": from a page — that page fades to black under this screen (main.qml keeps it up
    //         for entranceHoldMs), then the curtain rises out of the black. Paced on the launch
    //         sound to come: black at 0.9 s, risen by about 2.3 s.
    // "rise": from the stream, whose window comes back on black: only the second half.
    // "cross": replacing another curtain in place (retry, new settings, the next game after a
    //         quit): no entrance, the StackView cross-fades the two.
    // Reduce animations cuts straight to the curtain.
    property string entrance: "fade"
    readonly property int entranceHoldMs: entrance === "fade" && !Theme.reduceAnimations ? 950 : 0

    // Told to the StackView when this screen is created — before the push — so the page
    // underneath stays up for the fade (main.qml).
    Component.onCompleted: stackView.entranceHoldMs = entranceHoldMs

    property real _veil: entrance === "rise" ? 1 : 0
    property real _rise: entrance === "cross" || Theme.reduceAnimations ? 1 : 0
    property bool _black: entrance !== "fade" || Theme.reduceAnimations
    property bool _linkReady: false
    property bool _riseAfterInit: false

    NumberAnimation {
        id: veilIn
        target: streamSegue; property: "_veil"; to: 1
        duration: 900; easing.type: Easing.InOutSine
        onFinished: streamSegue._atBlack()
    }
    NumberAnimation {
        id: riseIn
        target: streamSegue; property: "_rise"; to: 1
        duration: 1300; easing.type: Easing.OutCubic
    }
    // If initialize() is slow to come, the curtain does not wait on a black screen for it.
    Timer {
        id: riseFallback
        interval: 1500
        onTriggered: if (streamSegue._riseAfterInit) { streamSegue._riseAfterInit = false; streamSegue._startRise() }
    }

    function _beginEntrance() {
        if (!_black) veilIn.start()
        else _startRise()
    }
    function _atBlack() {
        _black = true
        if (_linkReady) {
            // Black and ready: the stall of initialize() happens now, unseen, and the curtain
            // rises once it is over.
            _riseAfterInit = true
            riseFallback.start()
            streamLoader.active = true
        }
        else {
            _startRise()
        }
    }
    function _startRise() {
        if (_rise < 1 && !riseIn.running) riseIn.start()
    }

    readonly property real _bgOpacity: Math.min(1, _rise / 0.6)

    // The black the page underneath fades into.
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: streamSegue._veil
        visible: opacity > 0
    }

    /*
     * Background: the app's own floor, with the blurred box art of whatever is starting
     * laid over it — the same drawing the host page uses, from the same component.
     *
     * It used to be a gradient built from two colours sampled out of the cover. That was a
     * second picture of the same game, so crossing from the host page into the launch the
     * background changed under you at the one moment when nothing else should move.
     *
     * With no artwork (a CLI launch, the PIN pad, a game with no cover) CoverAmbient draws
     * nothing and the floor below is what shows — which is also what the PIN pad wanted
     * anyway: it should look like part of the app, not like a launch that lost its picture.
     *
     * 6.1.0: the waves only on the PIN pad, never on a game's launch screen. At normal
     * speed: Home does not count the unlock's own session as a stream either
     * (HomeScreen._runningFor), and the two must agree.
     *
     * 7.0.0: unchanged, by decision (13/10/2026) — it only rises out of the black now.
     */
    AmbientBackground {
        waves: streamSegue.unlockMode
        // The curtain stays dark in Light (7.0.0); the PIN pad follows the theme.
        dark: !streamSegue.unlockMode
        opacity: streamSegue._bgOpacity
    }

    CoverAmbient {
        source: streamSegue.unlockMode ? "" : streamSegue.boxArt
        opacity: streamSegue._bgOpacity
    }

    CurtainHorizon {
        anchors.fill: parent
        // In unlock mode there is no game and no cover: the pad takes this screen.
        visible: !streamSegue.unlockMode && streamSegue._contentShown
        rise: streamSegue._rise

        hostName: streamSegue._hostName
        badges: streamSegue._badges
        boxArt: streamSegue.boxArt
        kicker: streamSegue.isResume ? qsTr("RESUMING") : qsTr("NOW STARTING")
        gameName: streamSegue._c && streamSegue._c.gameName !== "" ? streamSegue._c.gameName
                                                                    : streamSegue.appName
        status: streamSegue._status
        detail: streamSegue._c ? streamSegue._c.detail : ""
        // One amber line, the most specific first: the link, then a launch past the usual
        // span, then a host that has not answered at all.
        amber: streamSegue._c && streamSegue._c.warning !== "" ? streamSegue._c.warning
             : streamSegue.linkWarning !== ""                  ? streamSegue.linkWarning
             : streamSegue._launchSlow                          ? qsTr("Taking longer than usual")
             : streamSegue._hostSlow                            ? qsTr("Still waiting for the host to answer")
             : ""
        tone: streamSegue._c && streamSegue._c.stalled                  ? "bad"
            : streamSegue._stage >= streamSegue._segments.length        ? "done"
            :                                                            "busy"
        segments: streamSegue._segments
        stage: streamSegue._stage
    }

    // One row for both prompts and both devices. ActionHint draws the vendor glyph or the
    // keyboard cap by itself, which is why the pad and keyboard variants that used to sit at
    // opposite ends of this file are now a single row — two hints written twice each would be
    // four places for the same sentence to drift.
    //
    // 7.0.0: at the lower right, across from the curtain's text; centred on the PIN pad,
    // where there is nothing else on the screen to be across from.
    Row {
        id: hintRow
        anchors.bottom: parent.bottom
        anchors.bottomMargin: streamSegue._h * 0.035
        anchors.right: streamSegue.unlockMode ? undefined : parent.right
        anchors.rightMargin: streamSegue._h * 0.08
        anchors.horizontalCenter: streamSegue.unlockMode ? parent.horizontalCenter : undefined
        spacing: streamSegue._h * 0.024
        visible: streamSegue._exitHintOn || streamSegue._giveUpHintOn
        opacity: streamSegue._rise

        readonly property int  _glyph: Math.max(22, Math.round(streamSegue._h * 0.028))
        readonly property int  _font:  Math.max(10, Math.round(streamSegue._h * 0.016))
        readonly property color _dim:  streamSegue.unlockMode ? Theme.text2 : Theme.stageText2

        // "B" as the app means it, which is what the user's controller calls B on every
        // vendor — the glyph resolver handles that, including Nintendo's swapped faces.
        Row {
            spacing: streamSegue._h * 0.008
            visible: streamSegue._exitHintOn

            ActionHint {
                anchors.verticalCenter: parent.verticalCenter
                buttonKey: "B"
                keyLabel: "Esc"
                size: hintRow._glyph
                onDark: !streamSegue.unlockMode
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: streamSegue._exitHintTail
                font.pointSize: hintRow._font
                color: hintRow._dim
            }
        }

        Row {
            spacing: streamSegue._h * 0.008
            visible: streamSegue._giveUpHintOn

            ActionHint {
                anchors.verticalCenter: parent.verticalCenter
                buttonKey: "X"
                keyLabel: "X"
                size: hintRow._glyph
                onDark: !streamSegue.unlockMode
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("cancel")
                font.pointSize: hintRow._font
                color: hintRow._dim
            }
        }
    }

    // The wait before the pad. The curtain column above is hidden in unlock mode — it is built
    // around a game's cover and name, neither of which exists here — which left the screen
    // empty for the several seconds the connection takes. One spinner and one word.
    Row {
        anchors.centerIn: parent
        spacing: streamSegue._h * 0.016
        visible: streamSegue.unlockMode && !unlockPad.visible
        opacity: streamSegue._rise

        Spinner {
            id: unlockWaitSpinner
            running: visible
            bodySize: unlockWaitLabel.font.pixelSize
            anchors.verticalCenter: parent.verticalCenter
        }

        Label {
            id: unlockWaitLabel
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Connecting…")
            font.pointSize: Math.max(13, Math.round(streamSegue._h * 0.026))
            color: Theme.text
        }
    }

    // The PIN pad. Hidden until the shade has been knocked away, so it never appears over a
    // host that is not yet showing its logon screen.
    UnlockPad {
        id: unlockPad
        anchors.fill: parent
        visible: false
        hostName: streamSegue.hostName

        onDigitPressed: function (digit) {
            if (!session) return
            session.unlockDigit(digit)
            pinLength = session.unlockPinLength()
            if (state_ === "wrong") state_ = "entry"
        }
        onBackspacePressed: {
            if (!session) return
            session.unlockBackspace()
            pinLength = session.unlockPinLength()
        }
        onSubmitPressed: {
            if (!session || pinLength === 0) return
            session.unlockSubmitPin()
            pinLength = 0
            state_ = "checking"
            streamSegue._unlockPolls = 0
            streamSegue._unlockHostAnswered = false
            lockPollTimer.start()
        }
        onCancelPressed: streamSegue._unlockFinish(false)
    }

}
