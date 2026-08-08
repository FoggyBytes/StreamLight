import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5

// Small reusable dialog button following the app convention (§22): green accent reserved
// for FOCUS; affirmative role shown by green text at rest, the focused button gets a green
// 2px border + faint tint. Emits activated() on click / Return / Enter / Space.
Button {
    id: btn
    property bool affirmative: false
    // Destructive action: red text + red focus border/tint (§22).
    property bool danger: false
    // Label size — lower it for compact footers.
    property int fontSize: 15
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
        color: btn.activeFocus ? (btn.danger ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.20)
                                             : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20))
             : btn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
             :                   "#14ffffff"
        border.color: btn.activeFocus ? (btn.danger ? Theme.danger : Theme.accent)
                    : btn.hovered     ? Theme.lineHigh
                    :                   Theme.line
        border.width: btn.activeFocus ? 2 : 1

        // The same snap the action rows on Home and the host page use. The colour grammar
        // here stays as §22 defined it — accent as a border and a tint, not a fill — but the
        // motion is shared, so focus moves the same way everywhere.
        Behavior on scale {
            enabled: !Theme.reduceAnimations
            NumberAnimation { duration: 130; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
        }
        scale: btn.activeFocus && !Theme.reduceAnimations ? 1.03 : 1.0
    }
    contentItem: Label {
        text: btn.text
        color: btn.danger      ? Theme.danger
             : btn.affirmative ? Theme.accent
             :                   Theme.text
        font.family: Theme.family; font.pixelSize: btn.fontSize; font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
