import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

import ComputerManager 1.0

// Add-host-by-IP popup. Standalone file: Overlay.modal cannot resolve inline.
// D-pad: TextField → Down → Add; Add ⇄ Cancel via Left/Right; Up returns to TextField.
Popup {
    id: pop

    // 7.0.0: the shared spring in and quick out (PopEnter/PopExit), off with Reduce animations.
    enter: PopEnter {}
    exit: PopExit {}

    // Shared dialog measurements — see Theme.uiScale.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    signal accepted(string ipAddress)

    modal: true
    Overlay.modal: Rectangle { color: Theme.scrim }
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
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: 1.6
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Enter the host's IP address")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontTitle)
            color: Theme.text
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
            color: Theme.text
            selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
            selectedTextColor: Theme.text
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontTitle)
            font.bold: true
            horizontalAlignment: TextInput.AlignHCenter
            inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhUrlCharactersOnly

            background: Rectangle {
                color: Theme.ground
                radius: pop._px(8)
                border.color: ipField.activeFocus ? Theme.ink : Theme.line
                border.width: ipField.activeFocus ? 2 : 1
            }

            // The pad's A opens the on-screen keyboard (8.0.0); Enter on a keyboard adds.
            Keys.onReturnPressed: function(event) { if (!ipKeyboard.offer(event)) pop._commit() }
            Keys.onEnterPressed:  function(event) { if (!ipKeyboard.offer(event)) pop._commit() }
            Keys.onSpacePressed:  function(event) { event.accepted = ipKeyboard.offer(event) }
            Keys.onDownPressed:   okBtn.forceActiveFocus()
            // Shift+Tab (the pad's up in Settings arrives as Tab + Shift) keeps going back.
            Keys.onTabPressed:    function(event) { if ((event.modifiers & Qt.ShiftModifier)) { event.accepted = false; return } okBtn.forceActiveFocus() }
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
                         : okBtn.hovered     ? Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.05)
                         :                     Theme.card
                    border.color: okBtn.activeFocus ? Theme.ink
                                : okBtn.hovered     ? Theme.lineHigh
                                :                     Theme.line
                    border.width: okBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: okBtn.text
                    color: Theme.inkText
                    font.family: Theme.family
                    font.pixelSize: pop._px(Theme.fontBody)
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
                         : cancelBtn.hovered     ? Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.05)
                         :                         Theme.card
                    border.color: cancelBtn.activeFocus ? Theme.ink
                                : cancelBtn.hovered     ? Theme.lineHigh
                                :                         Theme.line
                    border.width: cancelBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: cancelBtn.text
                    color: Theme.text
                    font.family: Theme.family
                    font.pixelSize: pop._px(Theme.fontBody)
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

    VirtualKeyboard {
        id: ipKeyboard
        target: ipField
        mode: "address"
        onAccepted: { close(); pop._commit() }
    }

    onOpened: ipField.forceActiveFocus()
    onClosed: ipField.clear()
}
