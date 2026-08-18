import QtQuick 2.15
import QtQuick.Controls 2.15
import Theme 1.0

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

    // The shared dialog measurements, all of them multiplied by the window scale — see
    // Theme.uiScale for why a dialog cannot take this from the page it is covering.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: _px(520)
    padding: _px(28)

    Overlay.modal: Rectangle { color: "#cc000000" }

    background: Rectangle {
        color: Theme.card
        radius: dlg._px(14)
        border.color: Theme.line
        border.width: 1
    }

    // Focus on "Keep it" — the option that changes nothing. Same rule as the other
    // destructive-ish dialogs: the safe answer is the one under the cursor.
    onOpened: keepBtn.forceActiveFocus()

    // Centred, because this dialog asks a question. The ones that present something to read
    // or scroll through — the update list, the profile editor — stay left-aligned: centring
    // a paragraph or a list makes it harder to read, not more formal.
    contentItem: Column {
        spacing: 0

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("LINK SPEED")
            font.family: Theme.family; font.pixelSize: dlg._px(13)
            font.bold: true; font.letterSpacing: dlg._u * 1.6
            color: Theme.text3
        }

        Item { width: 1; height: dlg._px(12) }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Restore host link speed?")
            font.family: Theme.family; font.pixelSize: dlg._px(22); font.bold: true
            color: Theme.text
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: dlg._px(10) }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            // What it is and what each answer does. The reasoning lives in the changelog.
            text: qsTr("%1 is still on the speed matched for streaming.").arg(dlg.hostName)
            font.family: Theme.family; font.pixelSize: dlg._px(15)
            color: Theme.text2
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: dlg._px(22) }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: dlg._px(10)

            // No width or height here: DialogButton carries the shared size and scales it.
            DialogButton {
                id: restoreBtn
                text: qsTr("Restore")
                affirmative: true
                onActivated: { dlg.restoreChosen(dlg.pcIndex); dlg.close() }
                KeyNavigation.right: keepBtn
            }
            DialogButton {
                id: keepBtn
                text: qsTr("Keep it")
                onActivated: dlg.close()
                KeyNavigation.left: restoreBtn
            }
        }
    }
}
