import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Host pairing popup. Standalone file: Overlay.modal cannot resolve inline.
Popup {
    id: pop

    property string pin: "0000"

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
            text: qsTr("PAIR WITH HOST")
            font.family: "DM Sans"
            font.pixelSize: 13
            font.bold: true
            font.letterSpacing: 1.6
            color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Enter this PIN on your host PC to complete pairing.")
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
            implicitWidth:  pinText.implicitWidth  + 64
            implicitHeight: pinText.implicitHeight + 28
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
            border.color: Theme.accent
            border.width: 2
            radius: 12

            Label {
                id: pinText
                anchors.centerIn: parent
                text: pop.pin
                // The one place monospace survives the 5.0.0 unification onto DM Sans. These
                // four digits exist to be read off this screen and compared against another,
                // and a 1 that looks like an l is exactly the failure a monospaced face is for.
                font.family: "JetBrains Mono"
                font.pixelSize: 64
                font.bold: true
                font.letterSpacing: 10
                color: Theme.accent
            }
        }

        Label {
            text: qsTr("This window will close automatically when pairing completes.")
            font.family: "DM Sans"
            font.pixelSize: 14
            color: "#a0a0a0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }

        Label {
            text: qsTr("Open the web UI on the host and enter the PIN there.")
            font.family: "DM Sans"
            font.pixelSize: 12
            font.italic: true
            color: "#707070"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }

        Button {
            id: pairCancelBtn
            text: qsTr("Cancel")
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            focus: true
            activeFocusOnTab: true
            onClicked: pop.close()
            Keys.onReturnPressed: pop.close()
            Keys.onEnterPressed:  pop.close()
            Keys.onSpacePressed:  pop.close()

            background: Rectangle {
                implicitWidth: 140
                implicitHeight: 40
                radius: 8
                color: pairCancelBtn.activeFocus
                     ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                     : pairCancelBtn.hovered
                       ? "#262626"
                       : "#1f1f1f"
                border.color: (pairCancelBtn.activeFocus || pairCancelBtn.hovered)
                              ? Theme.accent
                              : "#2a2a2a"
                border.width: pairCancelBtn.activeFocus ? 2 : 1
            }
            contentItem: Label {
                text: pairCancelBtn.text
                color: "#f0f0f0"
                font.family: "DM Sans"
                font.pixelSize: 14
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    onOpened: pairCancelBtn.forceActiveFocus()
}
