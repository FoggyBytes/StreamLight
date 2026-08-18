import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

import ComputerManager 1.0

// Add-host-by-IP popup. Standalone file: Overlay.modal cannot resolve inline.
// D-pad: TextField → Down → Add; Add ⇄ Cancel via Left/Right; Up returns to TextField.
Popup {
    id: pop

    // Shared dialog measurements — see Theme.uiScale.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    signal accepted(string ipAddress)

    modal: true
    Overlay.modal: Rectangle { color: "#cc000000" }
    focus: true
    // Horizontally centred but offset higher up the screen so a virtual
    // keyboard (handheld 1080p) does not cover the dialog while typing.
    x: (Overlay.overlay ? (Overlay.overlay.width  - width)  / 2 : 0)
    y: (Overlay.overlay ? Math.max(40, Overlay.overlay.height * 0.12) : 40)
    closePolicy: Popup.CloseOnEscape
    padding: pop._px(32)

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: pop._px(12)
    }

    contentItem: ColumnLayout {
        spacing: pop._px(22)

        Label {
            text: qsTr("ADD HOST")
            font.family: "DM Sans"
            font.pixelSize: pop._px(13)
            font.bold: true
            font.letterSpacing: 1.6
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Enter the IP address of your host PC")
            font.family: "DM Sans"
            font.pixelSize: pop._px(18)
            color: "#f0f0f0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: pop._px(520)
        }

        TextField {
            id: ipField
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: pop._px(360)
            implicitHeight: pop._px(48)
            color: "#f0f0f0"
            selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
            selectedTextColor: Theme.onAccent
            font.family: "DM Sans"
            font.pixelSize: pop._px(20)
            font.bold: true
            horizontalAlignment: TextInput.AlignHCenter
            inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhUrlCharactersOnly

            background: Rectangle {
                color: "#0f0f0f"
                radius: pop._px(8)
                border.color: ipField.activeFocus ? Theme.accent : Theme.line
                border.width: ipField.activeFocus ? 2 : 1
            }

            Keys.onReturnPressed: pop._commit()
            Keys.onEnterPressed:  pop._commit()
            Keys.onDownPressed:   okBtn.forceActiveFocus()
            Keys.onTabPressed:    okBtn.forceActiveFocus()
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: pop._px(4)
            spacing: pop._px(14)

            Button {
                id: okBtn
                text: qsTr("Add")
                activeFocusOnTab: true
                onClicked: pop._commit()
                Keys.onReturnPressed: pop._commit()
                Keys.onEnterPressed:  pop._commit()
                Keys.onSpacePressed:  pop._commit()
                Keys.onRightPressed:  cancelBtn.forceActiveFocus()
                Keys.onUpPressed:     ipField.forceActiveFocus()

                background: Rectangle {
                    implicitWidth: pop._px(140)
                    implicitHeight: pop._px(42)
                    radius: pop._px(8)
                    color: okBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : okBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                     "#1f1f1f"
                    border.color: okBtn.activeFocus ? Theme.accent
                                : okBtn.hovered     ? "#3a3a3a"
                                :                     Theme.line
                    border.width: okBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: okBtn.text
                    color: Theme.accent
                    font.family: "DM Sans"
                    font.pixelSize: pop._px(15)
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                id: cancelBtn
                text: qsTr("Cancel")
                activeFocusOnTab: true
                onClicked: pop.close()
                Keys.onReturnPressed: pop.close()
                Keys.onEnterPressed:  pop.close()
                Keys.onSpacePressed:  pop.close()
                Keys.onLeftPressed:   okBtn.forceActiveFocus()
                Keys.onUpPressed:     ipField.forceActiveFocus()

                background: Rectangle {
                    implicitWidth: pop._px(140)
                    implicitHeight: pop._px(42)
                    radius: pop._px(8)
                    color: cancelBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : cancelBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         "#1f1f1f"
                    border.color: cancelBtn.activeFocus ? Theme.accent
                                : cancelBtn.hovered     ? "#3a3a3a"
                                :                         Theme.line
                    border.width: cancelBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: cancelBtn.text
                    color: "#f0f0f0"
                    font.family: "DM Sans"
                    font.pixelSize: pop._px(15)
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    function _commit() {
        var t = ipField.text.trim()
        if (t.length === 0) return
        pop.accepted(t)
        pop.close()
    }

    onOpened: ipField.forceActiveFocus()
    onClosed: ipField.clear()
}
