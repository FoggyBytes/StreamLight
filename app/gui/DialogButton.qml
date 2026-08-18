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
    // Label size — lower it for compact footers. A design value: it is scaled below.
    property int fontSize: 15
    signal activated()

    // The window scale, the same number the pages multiply their own sizes by.
    //
    // ⚠️ Do not set width/height on an instance of this. Two dialogs used to (108 and 190),
    // which both defeated the shared size and pinned them to fixed pixels while everything
    // around them scaled. If a dialog needs a wider button, widen it here for all of them.
    readonly property real _u: Theme.uiScale

    activeFocusOnTab: true
    onClicked: btn.activated()
    Keys.onReturnPressed: btn.activated()
    Keys.onEnterPressed:  btn.activated()
    Keys.onSpacePressed:  btn.activated()

    background: Rectangle {
        implicitWidth: Math.round(150 * btn._u)
        implicitHeight: Math.round(42 * btn._u)
        radius: Math.round(8 * btn._u)
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
        font.family: Theme.family; font.pixelSize: Math.round(btn.fontSize * btn._u); font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
