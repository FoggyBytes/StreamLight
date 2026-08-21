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

    // Shared dialog measurements — see Theme.uiScale.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: Math.min(_px(520), parent ? parent.width * 0.8 : _px(520))
    padding: _px(28)

    background: Rectangle {
        // Theme.card, not Theme.ground: ground is the colour of the page underneath, so a
        // panel painted with it does not lift off what it is covering.
        color: Theme.card
        radius: dialog._px(14)
        border.color: Theme.line
        border.width: 1
    }

    Overlay.modal: Rectangle { color: "#cc000000" }

    contentItem: Column {
        spacing: dialog._px(16)
        width: dialog.availableWidth

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("WAKE")
            font.family: Theme.family
            // The grey, not the accent: in this interface the accent means "the focus is
            // here", and an eyebrow is never focusable.
            font.pixelSize: dialog._px(13)
            font.bold: true
            font.letterSpacing: dialog._u * 1.6
            color: Theme.text3
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: dialog.hostName
            font.family: Theme.family
            font.pixelSize: dialog._px(22)
            font.bold: true
            color: Theme.text
        }

        // The block is centred, not each row: centring the rows individually would leave the
        // ticks in a ragged column, and the point of that column is that it lines up.
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: dialog._px(9)

            Repeater {
                // Three, and it ends here: the link match that follows is shown on the host
                // card, which is where the user will be looking by then.
                model: [qsTr("Magic packet sent"),
                        qsTr("Host on the network"),
                        qsTr("StreamTweak ready")]

                Row {
                    spacing: dialog._px(10)

                    // Done · in progress · not yet, in one glyph column so the labels line up.
                    // 26px, not 14: that dates from Material BusyIndicator, which shrank to
                    // a speck at label height. Spinner scales properly, so the size is now a
                    // layout choice rather than a workaround.
                    Item {
                        width: dialog._px(26)
                        height: dialog._px(26)
                        anchors.verticalCenter: parent.verticalCenter

                        Label {
                            anchors.centerIn: parent
                            visible: index < dialog.step
                            text: "✓"
                            font.family: Theme.family
                            font.pixelSize: dialog._px(15)
                            color: Theme.online
                        }
                        Spinner {
                            anchors.centerIn: parent
                            bodySize: dialog._px(15)
                            visible: index === dialog.step
                            running: visible
                        }
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        font.family: Theme.family
                        font.pixelSize: dialog._px(15)
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
            font.pixelSize: dialog._px(13)
            color: Theme.text3
        }

        DialogButton {
            id: cancelBtn
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Cancel")
            onActivated: dialog.cancelled()
            Keys.onEscapePressed: dialog.cancelled()
        }
    }

    onOpened: cancelBtn.forceActiveFocus()
}
