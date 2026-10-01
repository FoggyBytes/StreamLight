import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Host pairing popup. Standalone file: Overlay.modal cannot resolve inline.
Popup {
    id: pop

    property string pin: "0000"

    modal: true
    // Opened on Home: a scrim, like every dialog opened on a page (6.3.1, §80.9 — see
    // NavigableDialog). It had no dim at all.
    Overlay.modal: Rectangle { color: Theme.scrim }
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape

    // The window scale every other dialog is drawn at (6.3.1): this one was in fixed pixels,
    // so on a large screen it opened smaller than the page behind it. Hairlines stay 1 px.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    padding: _px(32)

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: pop._px(12)
    }

    contentItem: ColumnLayout {
        spacing: pop._px(22)

        Label {
            text: qsTr("PAIR WITH HOST")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: 1.6 * pop._u
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Enter this PIN on the host to finish pairing.")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontTitle)
            color: Theme.text
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: pop._px(520)
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: pop._px(4)
            Layout.bottomMargin: pop._px(4)
            implicitWidth:  pinText.implicitWidth  + pop._px(64)
            implicitHeight: pinText.implicitHeight + pop._px(28)
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
            border.color: Theme.accent
            border.width: 2
            radius: pop._px(12)

            Label {
                id: pinText
                anchors.centerIn: parent
                text: pop.pin
                // The one place monospace survives the 5.0.0 unification onto DM Sans. These
                // four digits exist to be read off this screen and compared against another,
                // and a 1 that looks like an l is exactly the failure a monospaced face is for.
                font.family: Theme.monoFamily
                font.pixelSize: pop._px(64)
                font.bold: true
                font.letterSpacing: 10 * pop._u
                color: Theme.accent
            }
        }

        Label {
            text: qsTr("This window will close automatically when pairing completes.")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            color: Theme.text2
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: pop._px(520)
        }

        Label {
            text: qsTr("Open the web UI on the host and enter the PIN there.")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontCaption)
            font.italic: true
            color: Theme.text3
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: pop._px(520)
        }

        // The app's own dialog button. This was a copy of it that had drifted: its border
        // turned green on mouse hover as well as on focus, where the rule everywhere else is
        // green for focus only, and it stayed in fixed pixels.
        DialogButton {
            id: pairCancelBtn
            text: qsTr("Cancel")
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: pop._px(8)
            focus: true
            onActivated: pop.close()
        }
    }

    onOpened: pairCancelBtn.forceActiveFocus()
}
