import QtQuick 2.15
import QtQuick.Controls 2.5

import ComputerManager 1.0
import Session 1.0

Item {
    // Same floor as the rest of the app: this used to be the one screen that showed the bare
    // window behind it, so ending a session dropped out of the app's own surface for a moment.
    AmbientBackground {}

    property string appName
    property var quitRunningAppFn

    // Called once the app has actually quit. Every deliberate stop funnels through here —
    // the X on a tile, the Desktop tile, the quit prompt — so it is the one place that can
    // tell the host "finished", as opposed to a disconnect it should treat as a pause.
    property var onQuitSucceededFn: null
    property Session nextSession : null
    property string nextAppName : ""
    // What the next game's launch screen needs, since this screen builds it. Everything about
    // that launch has to travel through here: the artwork its curtain is drawn from, and the
    // callback that watches for the host restoring its link once that session ends.
    property url nextBoxArt : ""
    property var nextSessionEndedFn : null

    property string stageText : qsTr("Quitting %1...").arg(appName)

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
                "onSessionEndedFn": nextSessionEndedFn
            })
            stackView.replace(segue)
        }
        else {
            // Exit this view
            stackView.pop()
        }
    }

    StackView.onActivated: {
        // (toolbar removed in 3.0 redesign — nothing to hide here)

        // Connect the quit completion signal
        ComputerManager.quitAppCompleted.connect(quitAppCompleted)

        // Start the quit operation if requested
        if (quitRunningAppFn) {
            quitRunningAppFn()
        }
    }

    StackView.onDeactivating: {
        // (toolbar removed in 3.0 redesign — nothing to restore here)

        // Disconnect the signal
        ComputerManager.quitAppCompleted.disconnect(quitAppCompleted)
    }

    Row {
        anchors.centerIn: parent
        spacing: 5

        BusyIndicator {
            id: stageSpinner
            running: visible
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

    ErrorMessageDialog {
        id: errorDialog
    }
}
