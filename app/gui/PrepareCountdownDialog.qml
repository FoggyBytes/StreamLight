import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Popup shown during the StreamTweak "preparing host" countdown.
// Standalone file (mirrors PairDialog.qml) so Overlay.modal can resolve.
Popup {
    id: pop

    property int countdown: 10

    modal: true
    Overlay.modal: Item {}
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.NoAutoClose
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
            text: qsTr("STREAMTWEAK · PREPARING")
            font.family: "DM Sans"
            font.pixelSize: 13
            font.bold: true
            font.letterSpacing: 1.6
            color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Throttling the host NIC. You can connect in")
            font.family: "DM Sans"
            font.pixelSize: 18
            color: "#f0f0f0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            implicitWidth:  countdownNumber.implicitWidth  + 80
            implicitHeight: countdownNumber.implicitHeight + 28
            color: Qt.rgba(0, 0.9, 0.46, 0.08)
            border.color: "#00E676"
            border.width: 2
            radius: 12

            Label {
                id: countdownNumber
                anchors.centerIn: parent
                text: pop.countdown
                font.family: "JetBrains Mono"
                font.pixelSize: 64
                font.bold: true
                color: "#00E676"
            }
        }

        Label {
            text: pop.countdown === 1 ? qsTr("second") : qsTr("seconds")
            font.family: "DM Sans"
            font.pixelSize: 14
            color: "#a0a0a0"
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("This dialog will close automatically when the host is ready.")
            font.family: "DM Sans"
            font.pixelSize: 12
            font.italic: true
            color: "#707070"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }
    }
}
