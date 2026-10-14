import QtQuick 2.15

import Theme 1.0

/*
 * The press, felt (7.0.0): a control shrinks by 4% for a moment and springs back. Mouse
 * presses are `down` already; a press of A arrives as a key, so `kick()` plays the same dip.
 *
 * Bind the control's (or its background's) scale to `scale` and call kick() from the key
 * handlers. With Reduce animations on it never moves.
 */
QtObject {
    id: bounce

    // True while the mouse holds the control down.
    property bool down: false
    // The scale the control has when not pressed — a focused control may stand a little proud.
    property real rest: 1.0

    property bool _kicked: false
    readonly property real scale: Theme.reduceAnimations ? 1.0 : (bounce.down || bounce._kicked ? 0.96 : bounce.rest)

    function kick() {
        if (Theme.reduceAnimations) return
        bounce._kicked = true
        _release.restart()
    }

    property Timer _release: Timer {
        interval: 110
        onTriggered: bounce._kicked = false
    }
}
