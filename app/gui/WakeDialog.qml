import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5

/*
 * Shown while an offline host is being woken, up to the moment the PIN pad takes over or
 * the host turns out not to need one.
 *
 * Steps rather than a bare spinner: a wake takes the better part of a minute, and knowing
 * which part is slow is the difference between waiting and wondering. Each row is the name
 * of a thing that has to happen — no sentences.
 */
Popup {
    id: dialog

    property string hostName : ""
    // 0 sent · 1 host answered · 2 StreamTweak ready · 3 network
    property int step : 0
    property string detail : ""

    signal cancelled()

    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, parent ? parent.width * 0.8 : 480)
    padding: 26

    background: Rectangle {
        color: Theme.ground
        radius: 14
        border.color: Theme.line
        border.width: 1
    }

    Overlay.modal: Rectangle { color: "#b3000000" }

    contentItem: Column {
        spacing: 16
        width: dialog.availableWidth

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("WAKE")
            font.family: Theme.family
            font.pixelSize: 11
            font.letterSpacing: 2
            color: Theme.accent
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: dialog.hostName
            font.family: Theme.family
            font.pixelSize: 22
            font.bold: true
            color: Theme.text
        }

        // The block is centred, not each row: centring the rows individually would leave the
        // ticks in a ragged column, and the point of that column is that it lines up.
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 9

            Repeater {
                // Three, and it ends here: the link match that follows is shown on the host
                // card, which is where the user will be looking by then.
                model: [qsTr("Magic packet sent"),
                        qsTr("Host on the network"),
                        qsTr("StreamTweak ready")]

                Row {
                    spacing: 10

                    // Done · in progress · not yet, in one glyph column so the labels line up.
                    // 26px, not 14: a BusyIndicator draws its ring inside the box it is given,
                    // and at label height it came out as a speck.
                    Item {
                        width: 26
                        height: 26
                        anchors.verticalCenter: parent.verticalCenter

                        Label {
                            anchors.centerIn: parent
                            visible: index < dialog.step
                            text: "✓"
                            font.family: Theme.family
                            font.pixelSize: 15
                            color: Theme.online
                        }
                        BusyIndicator {
                            anchors.centerIn: parent
                            width: 26; height: 26
                            visible: index === dialog.step
                            running: visible
                        }
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        font.family: Theme.family
                        font.pixelSize: 14
                        color: index < dialog.step ? Theme.online
                             : index === dialog.step ? Theme.text
                             : Theme.text3
                    }
                }
            }
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: dialog.detail !== ""
            text: dialog.detail
            font.family: Theme.family
            font.pixelSize: 13
            color: Theme.text3
        }

        DialogButton {
            id: cancelBtn
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Cancel")
            fontSize: 14
            onActivated: dialog.cancelled()
            Keys.onEscapePressed: dialog.cancelled()
        }
    }

    onOpened: cancelBtn.forceActiveFocus()
}
