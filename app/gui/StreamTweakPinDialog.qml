import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5

// The StreamTweak access PIN, shown while a host's approval is pending. Standalone file for the
// same reason as PairDialog: Overlay.modal cannot resolve inline in HomeScreen, whose Controls
// import predates the Overlay attached type — and this popup needs it for Theme.scrim (6.3.1).
//
// What a close means is the caller's business: HomeScreen records every close by the user as a
// dismissal (see its onClosed), so this file only draws and offers the way out.
Popup {
    id: pop

    property string pin: ""
    property string hostName: ""
    property string hostAddress: ""

    modal: true
    closePolicy: Popup.CloseOnEscape
    // Laid over the page like every other dialog opened on one (6.3.1, §80.9): it used to take
    // Material's own grey dim, the only dialog in the app that did.
    Overlay.modal: Rectangle { color: Theme.scrim }

    // At the window scale every other dialog uses (6.3.1); it was in fixed pixels.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    width: _px(440)
    height: _px(300)
    anchors.centerIn: Overlay.overlay

    onOpened: dismissBtn.forceActiveFocus()

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: pop._px(12)
    }

    contentItem: Column {
        spacing: pop._px(18)

        Label {
            text: qsTr("STREAMTWEAK ACCESS")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: 1.6 * pop._u
            color: Theme.text3
        }
        Label {
            width: pop.availableWidth
            wrapMode: Text.Wrap
            // Two lines: what to do, then what it is for. The longer version said the same
            // twice and explained the trust model on screen — that belongs in the changelog.
            text: qsTr("Check this PIN matches the one on %1 (%2), then approve there.\n\nOptional: it enables host metrics, link speed, store badges and session reports.").arg(pop.hostName).arg(pop.hostAddress)
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            color: Theme.text
        }
        // The one place the monospaced face survives: these four digits exist to be read
        // off one screen and compared against another, and a 1 that looks like an l is
        // precisely the failure a monospaced face exists to prevent.
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: pop.pin
            font.family: Theme.monoFamily
            font.pixelSize: pop._px(52)
            font.bold: true
            font.letterSpacing: 10 * pop._u
            color: Theme.accent
        }
        DialogButton {
            id: dismissBtn
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Dismiss")
            onActivated: pop.close()
        }
    }
}
