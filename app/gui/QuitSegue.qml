import QtQuick 2.15
import QtQuick.Controls 2.5

// ⚠️ 7.0.0: without this import every Theme.* below was undefined — the spinner and the
// "Quitting…" line came out black on the dark curtain.
import Theme 1.0

import ComputerManager 1.0
import Session 1.0

Item {
    id: quitSegue

    // The box art of the app being closed, so this screen can stand on it. Empty for the
    // paths that have none (the CLI), where the floor below shows through on its own.
    property url boxArt

    property string appName
    // 7.0.0: the host it runs on, for the chip at the top — when the caller knows it.
    property string hostName: ""
    property var quitRunningAppFn

    // Called once the app has actually quit. Every deliberate stop funnels through here —
    // the X on a tile, the Desktop tile, the quit prompt — so it is the one place that can
    // tell the host "finished", as opposed to a disconnect it should treat as a pause.
    property var onQuitSucceededFn: null
    property Session nextSession
    property string nextAppName : ""
    // What the next game's launch screen needs, since this screen builds it. Everything about
    // that launch has to travel through here: the artwork its curtain is drawn from, and the
    // callback that watches for the host restoring its link once that session ends.
    property url nextBoxArt : ""
    property var nextSessionEndedFn : null

    property string stageText : qsTr("Quitting %1…").arg(appName)

    // ── The entrance (7.0.0) — the launch curtain's, shorter ─────────────────
    // "fade" from the host page (the X on a tile, the quit prompt): the page fades to black
    // under this screen, then the curtain rises out of it. "rise" from the stream, whose
    // window comes back on black. See StreamSegue for the full account.
    property string entrance: "fade"
    readonly property int entranceHoldMs: entrance === "fade" && !Theme.reduceAnimations ? 500 : 0

    // Told to the StackView when this screen is created — before the push — so the page
    // underneath stays up for the fade (main.qml).
    Component.onCompleted: stackView.entranceHoldMs = entranceHoldMs

    property real _veil: entrance === "rise" ? 1 : 0
    property real _rise: Theme.reduceAnimations ? 1 : 0
    readonly property real _h: height > 0 ? height : 1080
    readonly property real _bgOpacity: Math.min(1, _rise / 0.6)

    SequentialAnimation {
        id: entranceAnim
        NumberAnimation {
            target: quitSegue; property: "_veil"; to: 1
            duration: quitSegue.entrance === "fade" ? 450 : 0
            easing.type: Easing.InOutSine
        }
        NumberAnimation {
            target: quitSegue; property: "_rise"; to: 1
            duration: 900; easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: quitSegue._veil
        visible: opacity > 0
    }

    // Same floor as the rest of the app: this used to be the one screen that showed the bare
    // window behind it, so ending a session dropped out of the app's own surface for a moment.
    // No waves here (6.1.0): they belong to Home, Settings and the PIN pad only.
    AmbientBackground { waves: false; dark: true; opacity: quitSegue._bgOpacity }   // dark in both themes (7.0.0)

    // ...with the blurred artwork of whatever is being closed over it, the same drawing the
    // host page and the launch screen use. Quitting is the other half of launching, and it
    // should not land on a different surface.
    CoverAmbient {
        source: quitSegue.boxArt
        opacity: quitSegue._bgOpacity
    }

    // The launch curtain's own layout (7.0.0), without the bar: a quit has no steps to show.
    CurtainHorizon {
        anchors.fill: parent
        rise: quitSegue._rise
        hostName: quitSegue.hostName
        boxArt: quitSegue.boxArt
        kicker: qsTr("QUITTING")
        gameName: quitSegue.appName
        status: qsTr("Quitting…")
        detail: quitSegue.nextAppName ? qsTr("next: %1").arg(quitSegue.nextAppName) : ""
    }

    function quitAppCompleted(error)
    {
        // Display a failed dialog if we got an error
        if (error !== undefined) {
            errorDialog.text = error
            errorDialog.open()
            console.error(error)
        }

        // Not when another game follows: that is a swap, not the end of the evening, and the
        // host is about to be asked for the streaming speed all over again.
        if (error === undefined && nextSession === null && onQuitSucceededFn) {
            onQuitSucceededFn()
        }

        // If we're supposed to launch another game after this, do so now
        if (error === undefined && nextSession !== null) {
            var component = Qt.createComponent("StreamSegue.qml")
            var segue = component.createObject(stackView, {
                "appName":          nextAppName,
                "boxArt":           nextBoxArt,
                "session":          nextSession,
                "onSessionEndedFn": nextSessionEndedFn,
                // The curtain is already up: the next game's replaces this one in place.
                "entrance":         "cross"
            })
            stackView.replace(segue)
        }
        else {
            // Exit this view
            stackView.pop()
        }
    }

    // From Activating, like StreamSegue: the quit starts while the page fades out.
    property bool _started: false
    StackView.onActivating: _start()
    StackView.onActivated: _start()

    function _start() {
        if (_started) return
        _started = true
        entranceAnim.start()

        // Connect the quit completion signal
        ComputerManager.quitAppCompleted.connect(quitAppCompleted)

        // Start the quit operation if requested
        if (quitRunningAppFn) {
            quitRunningAppFn()
        }
    }

    StackView.onDeactivating: {
        // Disconnect the signal
        ComputerManager.quitAppCompleted.disconnect(quitAppCompleted)
    }

    ErrorMessageDialog {
        id: errorDialog
    }
}
