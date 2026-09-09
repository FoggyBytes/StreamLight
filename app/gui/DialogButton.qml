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

    // This button already brightened its border under the pointer — it is where the app-wide
    // hover idiom comes from. What HoverState adds is the cursor and the two guards, which it
    // never had, and one correction: hover no longer touches the fill. It used to REPLACE
    // "#14ffffff" (alpha 0.078) with rgba(1,1,1,0.05), so the pointer made the button very
    // slightly darker while the border said the opposite.
    HoverState { id: hov }

    background: Rectangle {
        implicitWidth: Math.round(150 * btn._u)
        implicitHeight: Math.round(42 * btn._u)
        radius: Math.round(8 * btn._u)
        /*
         * ⚠️ The disabled state is drawn HERE and not on the instance.
         *
         * A Button with `enabled: false` was visually identical to one that worked: HoverState
         * already refuses to light up a disabled control, so the pointer got no promise — but
         * standing still, at rest, nothing said the button was inert. The first button in the
         * app that needed to be greyed out would otherwise have carried its own opacity, and
         * the second one a different opacity.
         */
        opacity: btn.enabled ? 1.0 : 0.45
        color: !btn.enabled   ? "transparent"
             : hov.keyFocused ? (btn.danger ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.20)
                                             : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20))
             :                   "#14ffffff"
        border.color: !btn.enabled    ? Theme.line
                    : hov.keyFocused ? (btn.danger ? Theme.danger : Theme.accent)
                    : hov.active      ? Theme.lineHigh
                    :                   Theme.line
        border.width: (btn.enabled && hov.keyFocused) ? 2 : 1

        // Colour only, so the width never moves and the pointer cannot nudge the geometry.
        Behavior on border.color {
            enabled: !Theme.reduceAnimations
            ColorAnimation { duration: 120; easing.type: Easing.OutQuad }
        }

        // The same snap the action rows on Home and the host page use. The colour grammar
        // here stays as §22 defined it — accent as a border and a tint, not a fill — but the
        // motion is shared, so focus moves the same way everywhere.
        Behavior on scale {
            enabled: !Theme.reduceAnimations
            NumberAnimation { duration: 130; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
        }
        scale: hov.keyFocused && !Theme.reduceAnimations ? 1.03 : 1.0
    }
    contentItem: Label {
        text: btn.text
        // Disabled loses the role colour as well as the strength: a red word at 45% still
        // reads as "destructive, and available". Theme.offline is the app's inert grey, and a
        // disabled control is the one place its contrast is not held to the text threshold.
        color: !btn.enabled    ? Theme.offline
             : btn.danger      ? Theme.danger
             : btn.affirmative ? Theme.accent
             :                   Theme.text
        font.family: Theme.family; font.pixelSize: Math.round(btn.fontSize * btn._u); font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
