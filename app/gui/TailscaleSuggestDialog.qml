import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Suggestion popup shown after a host is paired via its LAN IP if StreamTweak
// on the host confirms Tailscale is installed. Lets the user create a second
// tile pinned to the Tailscale endpoint for remote access from outside the LAN.
Popup {
    id: pop

    property string parentUuid: ""
    property string parentName: ""
    property string tailscaleIp: ""

    // Set by the parent screen to wire the actions back to ComputerModel.
    property var computerModel: null

    modal: true
    Overlay.modal: Item {}
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    padding: 32

    background: Rectangle {
        color: "#1a1a1a"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 12
    }

    contentItem: ColumnLayout {
        spacing: 22

        Label {
            text: qsTr("TAILSCALE DETECTED")
            font.family: "DM Sans"
            font.pixelSize: 13
            font.bold: true
            font.letterSpacing: 1.6
            color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("StreamTweak reports Tailscale is installed on \"%1\".").arg(pop.parentName)
            font.family: "DM Sans"
            font.pixelSize: 18
            color: "#f0f0f0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 560
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            implicitWidth:  ipText.implicitWidth  + 48
            implicitHeight: ipText.implicitHeight + 22
            color: Qt.rgba(0, 0.9, 0.46, 0.08)
            border.color: "#00E676"
            border.width: 2
            radius: 10

            Label {
                id: ipText
                anchors.centerIn: parent
                text: pop.tailscaleIp
                font.family: "JetBrains Mono"
                font.pixelSize: 28
                font.bold: true
                color: "#00E676"
            }
        }

        Label {
            text: qsTr("Add a separate tile pinned to this Tailscale address so you can stream from outside your LAN? Both the LAN tile and the Tailscale tile will be preserved across restarts and the Tailscale tile will never be overwritten by host discovery.")
            font.family: "DM Sans"
            font.pixelSize: 14
            color: "#a0a0a0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 560
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            spacing: 12

            // ---- Primary: Add Tailscale tile ----
            Button {
                id: addBtn
                text: qsTr("Add Tailscale tile")
                focus: true
                onClicked: {
                    if (pop.computerModel) {
                        pop.computerModel.addTailscaleClone(pop.parentUuid, pop.tailscaleIp)
                    }
                    pop.close()
                }
                background: Rectangle {
                    implicitWidth: 180
                    implicitHeight: 40
                    radius: 8
                    color: addBtn.activeFocus || addBtn.hovered
                           ? Qt.rgba(0, 0.9, 0.46, 0.18)
                           : Qt.rgba(0, 0.9, 0.46, 0.10)
                    border.color: "#00E676"
                    border.width: addBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: addBtn.text
                    color: "#f0f0f0"
                    font.family: "DM Sans"
                    font.pixelSize: 14
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // ---- Secondary: Not now ----
            Button {
                id: notNowBtn
                text: qsTr("Not now")
                onClicked: pop.close()
                background: Rectangle {
                    implicitWidth: 110
                    implicitHeight: 40
                    radius: 8
                    color: notNowBtn.activeFocus || notNowBtn.hovered ? "#262626" : "#1f1f1f"
                    border.color: notNowBtn.activeFocus || notNowBtn.hovered ? "#3a3a3a" : "#2a2a2a"
                    border.width: 1
                }
                contentItem: Label {
                    text: notNowBtn.text
                    color: "#d0d0d0"
                    font.family: "DM Sans"
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // ---- Tertiary: Don't ask again ----
            Button {
                id: dismissBtn
                text: qsTr("Don't ask again")
                onClicked: {
                    if (pop.computerModel) {
                        pop.computerModel.dismissTailscaleSuggestion(pop.parentUuid)
                    }
                    pop.close()
                }
                background: Rectangle {
                    implicitWidth: 150
                    implicitHeight: 40
                    radius: 8
                    color: dismissBtn.activeFocus || dismissBtn.hovered ? "#262626" : "#1f1f1f"
                    border.color: dismissBtn.activeFocus || dismissBtn.hovered ? "#3a3a3a" : "#2a2a2a"
                    border.width: 1
                }
                contentItem: Label {
                    text: dismissBtn.text
                    color: "#909090"
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    onOpened: addBtn.forceActiveFocus()
}
