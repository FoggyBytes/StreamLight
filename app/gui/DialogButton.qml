import QtQuick 2.15
import QtQuick.Controls 2.5

// Small reusable dialog button following the app convention (§22): green accent reserved
// for FOCUS; affirmative role shown by green text at rest, the focused button gets a green
// 2px border + faint tint. Emits activated() on click / Return / Enter / Space.
Button {
    id: btn
    property bool affirmative: false
    signal activated()

    activeFocusOnTab: true
    onClicked: btn.activated()
    Keys.onReturnPressed: btn.activated()
    Keys.onEnterPressed:  btn.activated()
    Keys.onSpacePressed:  btn.activated()

    background: Rectangle {
        implicitWidth: 150
        implicitHeight: 42
        radius: 8
        color: btn.activeFocus ? Qt.rgba(0, 0.9, 0.46, 0.20)
             : btn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
             :                   "#1f1f1f"
        border.color: btn.activeFocus ? "#00E676"
                    : btn.hovered     ? "#3a3a3a"
                    :                   "#2a2a2a"
        border.width: btn.activeFocus ? 2 : 1
    }
    contentItem: Label {
        text: btn.text
        color: btn.affirmative ? "#00E676" : "#f0f0f0"
        font.family: "DM Sans"; font.pixelSize: 15; font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
