import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import SdlGamepadKeyNavigation 1.0
import Session 1.0
import SystemProperties 1.0
import StreamingPreferences 1.0

Item {
    id: streamSegue

    property Session session
    property string appName
    property string stageText : isResume ? qsTr("Resuming %1...").arg(appName) :
                                           qsTr("Starting %1...").arg(appName)
    property bool isResume : false
    property bool quitAfter : false

    // How many transparent auto-retries remain for a transient "no video from
    // host" failure (host display/encoder still warming up on a cold start).
    // Gated by the user setting; the retry segue passes an explicit decremented
    // value so the cap is honoured regardless of the preference.
    property int noVideoRetries : StreamingPreferences.autoReconnectNoVideo ? 1 : 0

    // Resume session captured during sessionFinished (while `session` is still
    // valid) and consumed by the delayed retry. `session` is nulled by
    // readyForDeletion before the retry timer fires, so we can't build it later.
    property var _pendingRetrySession : null

    function stageStarting(stage)
    {
        // Update the spinner text
        stageText = qsTr("Starting %1...").arg(stage)
    }

    function stageFailed(stage, errorCode, failingPorts)
    {
        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = qsTr("Starting %1 failed: Error %2").arg(stage).arg(errorCode)

        if (failingPorts) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("Check your firewall and port forwarding rules for port(s): %1").arg(failingPorts)
        }
    }

    function connectionStarted()
    {
        // Hide the UI contents so the user doesn't
        // see them briefly when we pop off the StackView
        stageSpinner.visible = false
        stageLabel.visible = false
        hintTextKb.visible = false
        hintRow.visible    = false

        // Hide the window now that streaming has begun
        window.visible = false
    }

    function displayLaunchError(text)
    {
        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = text
        console.error(text)
    }

    function quitStarting()
    {
        // Avoid the push transition animation
        var component = Qt.createComponent("QuitSegue.qml")
        stackView.replace(stackView.currentItem, component.createObject(stackView, {"appName": appName}), StackView.Immediate)

        // Show the Qt window again to show quit segue
        window.visible = true
    }

    function sessionFinished(portTestResult)
    {
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
                stageSpinner.visible = true
                stageLabel.visible = true
                window.visible = true
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

                // Show a brief "reconnecting" spinner while the host settles.
                stageText = qsTr("Host is starting up — reconnecting…")
                stageSpinner.visible = true
                stageLabel.visible = true
                window.visible = true

                noVideoRetryTimer.start()
                return
            }
            // Couldn't build a retry session — fall through to the normal
            // error-handling path below.
        }

        if (portTestResult !== 0 && portTestResult !== -1 && streamSegueErrorDialog.text) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking StreamLight. Streaming over the Internet may not work while connected to this network.")
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
            // Show the Qt window again after streaming
            window.visible = true

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
            "session":        newSession,
            "isResume":       true,
            "quitAfter":      quitAfter,
            "noVideoRetries": noVideoRetries - 1
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
            "session":        newSession,
            "isResume":       true,
            "quitAfter":      quitAfter,
            "noVideoRetries": StreamingPreferences.autoReconnectNoVideo ? 1 : 0
        })
        if (Window.window) Window.window.markStreamLaunching()
        stackView.replace(stackView.currentItem, segue)
    }

    StackView.onDeactivating: {
        // (toolbar removed in 3.0 redesign — nothing to restore here)

        // Re-enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable()
    }

    StackView.onActivated: {
        // (toolbar removed in 3.0 redesign — nothing to hide here)

        // Hook up our signals
        session.stageStarting.connect(stageStarting)
        session.stageFailed.connect(stageFailed)
        session.connectionStarted.connect(connectionStarted)
        session.displayLaunchError.connect(displayLaunchError)
        session.quitStarting.connect(quitStarting)
        session.sessionFinished.connect(sessionFinished)
        session.readyForDeletion.connect(sessionReadyForDeletion)

        // Ensure the SystemProperties async thread is finished,
        // since it may currently be using the SDL video subsystem
        SystemProperties.waitForAsyncLoad()

        // Kick off the stream
        spinnerTimer.start()
        streamLoader.active = true
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
        id: spinnerTimer

        // Display the spinner appearance a bit to allow us to reach
        // the code in Session.exec() that pumps the event loop.
        // If we display it immediately, it will briefly hang in the
        // middle of the animation on Windows, which looks very
        // obviously broken.
        interval: 100
        onTriggered: stageSpinner.visible = true
    }

    Timer {
        id: startSessionTimer
        onTriggered: {
            // Garbage collect QML stuff before we start streaming,
            // since we'll probably be streaming for a while and we
            // won't be able to GC during the stream.
            gc()

            // Run the streaming session to completion
            session.start()
        }
    }

    Loader {
        id: streamLoader
        active: false
        asynchronous: true

        onLoaded: {
            // Disconnect-combo hint: with a gamepad we show the four icons of
            // the actual combination (Xbox or PlayStation glyphs picked from
            // the detected controller); otherwise the keyboard shortcut.
            if (SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
                hintRow.visible = true
                hintTextKb.visible = false
            } else {
                hintRow.visible = false
                hintTextKb.visible = true
                hintTextKb.text = qsTr("Tip:") + " " + qsTr("Press %1 to disconnect your session").arg(qsTr("Ctrl+Alt+Shift+Q"))
            }

            // Stop GUI gamepad usage now
            SdlGamepadKeyNavigation.disable()

            // Initialize the session and probe for host/client capabilities
            if (!session.initialize(window)) {
                sessionFinished(0);
                sessionReadyForDeletion();
                return;
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

    Row {
        anchors.centerIn: parent
        spacing: 5

        BusyIndicator {
            id: stageSpinner
            running: visible
            visible: false
        }

        Label {
            id: stageLabel
            height: stageSpinner.height
            text: stageText
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter

            wrapMode: Text.Wrap
        }
    }

    // Keyboard fallback: shown only when no gamepad is connected.
    Label {
        id: hintTextKb
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 50
        anchors.horizontalCenter: parent.horizontalCenter
        font.pointSize: 18
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        visible: false
    }

    // Gamepad combination hint: shown only when a controller is detected.
    // Icons swap between Xbox and PlayStation based on the connected pad.
    Row {
        id: hintRow
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 50
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 8
        visible: false

        readonly property bool _padIsPs: SdlGamepadKeyNavigation.controllerType === "ps"
        readonly property string _iconA: _padIsPs ? "qrc:/res/pad_ps_create.svg"  : "qrc:/res/pad_xbox_view.svg"
        readonly property string _iconB: _padIsPs ? "qrc:/res/pad_ps_options.svg" : "qrc:/res/pad_xbox_start.svg"
        readonly property string _iconL: _padIsPs ? "qrc:/res/pad_ps_l1.svg"      : "qrc:/res/pad_xbox_lb.svg"
        readonly property string _iconR: _padIsPs ? "qrc:/res/pad_ps_r1.svg"      : "qrc:/res/pad_xbox_rb.svg"

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Press")
            font.pointSize: 16
            color: "#f0f0f0"
        }
        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: hintRow._iconA
            width: 36; height: 24
            sourceSize.width: 72; sourceSize.height: 48
            smooth: true
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: "+"
            font.pointSize: 16
            color: "#a0a0a0"
        }
        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: hintRow._iconB
            width: 36; height: 24
            sourceSize.width: 72; sourceSize.height: 48
            smooth: true
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: "+"
            font.pointSize: 16
            color: "#a0a0a0"
        }
        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: hintRow._iconL
            width: 36; height: 24
            sourceSize.width: 72; sourceSize.height: 48
            smooth: true
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: "+"
            font.pointSize: 16
            color: "#a0a0a0"
        }
        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: hintRow._iconR
            width: 36; height: 24
            sourceSize.width: 72; sourceSize.height: 48
            smooth: true
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("to disconnect")
            font.pointSize: 16
            color: "#f0f0f0"
        }
    }
}
