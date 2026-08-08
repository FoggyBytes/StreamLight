import QtQuick 2.15
import QtQuick.Controls 2.15

// "Put the host's link speed back?" — asked on returning to the host list after a session,
// and only when the host says it is still on a speed this client asked for.
//
// It is a question rather than something that just happens because the answer genuinely
// depends on what the user does next: putting the link back costs about twenty seconds, and
// twenty more to match it again on the next launch. Someone about to stream again wants it
// left alone; someone finished for the evening wants their network back.
Popup {
    id: dlg

    property string hostName: ""
    property int    pcIndex: -1

    signal restoreChosen(int index)

    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: 0

    Overlay.modal: Rectangle { color: "#CC000000" }

    background: Rectangle {
        color: "#15171A"
        radius: 16
        border.color: "#2A2A2A"
        border.width: 1
    }

    // Focus on "Keep it" — the option that changes nothing. Same rule as the other
    // destructive-ish dialogs: the safe answer is the one under the cursor.
    onOpened: keepBtn.forceActiveFocus()

    contentItem: Column {
        spacing: 0

        Item { width: 1; height: 24 }

        Label {
            x: 28
            width: parent.width - 56
            text: qsTr("Restore host link speed?")
            font.family: "DM Sans"; font.pixelSize: 19; font.bold: true
            color: "#ECECEC"
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: 10 }

        Label {
            x: 28
            width: parent.width - 56
            // What it is and what each answer does. The reasoning lives in the changelog.
            text: qsTr("%1 is still on the speed matched for streaming.").arg(dlg.hostName)
            font.family: "DM Sans"; font.pixelSize: 14
            color: "#9A9A9A"
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: 22 }

        Row {
            x: 28
            spacing: 8

            DialogButton {
                id: restoreBtn
                text: qsTr("Restore")
                affirmative: true
                fontSize: 13
                width: 108; height: 38
                onActivated: { dlg.restoreChosen(dlg.pcIndex); dlg.close() }
                KeyNavigation.right: keepBtn
            }
            DialogButton {
                id: keepBtn
                text: qsTr("Keep it")
                fontSize: 13
                width: 108; height: 38
                onActivated: dlg.close()
                KeyNavigation.left: restoreBtn
            }
        }

        Item { width: 1; height: 24 }
    }
}
