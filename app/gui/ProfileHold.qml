import QtQuick 2.15

/*
 * LT/RT (Q/E) on the profile badge: a tap steps one profile, a hold opens the list (8.7.1, #53).
 *
 * Shared by Home and the host page, which both own the profile keys. The page hands it its key
 * events — press() from Keys.onPressed, release() from Keys.onReleased — and acts on tapped()
 * and held().
 *
 * ⚠️ The tap acts on the RELEASE, not on the press as it did up to 8.7.0: only the release says
 * it was not a hold. A tap is ~100 ms, so the step still feels immediate.
 * ⚠️ The pad sends the trigger's release on its own since 8.7.1 (SdlGamepadKeyNavigation, when
 * the trigger comes back under 8000); before, press and release left together and every pull
 * would have read as a tap. A keyboard's auto-repeat sends presses and releases flagged
 * isAutoRepeat while Q/E is held: they are ignored, so holding the key is a hold too.
 */
QtObject {
    id: hold

    // How long a pull has to last to open the list.
    property int holdMs: 400
    // False when there is nothing to list (no profiles): a hold is then just a slow tap.
    property bool canHold: true

    signal tapped(int dir)
    signal held(int dir)

    property int _key: 0
    property int _dir: 0

    function dirOf(key) {
        if (key === Qt.Key_F14 || key === Qt.Key_Q) return -1
        if (key === Qt.Key_F15 || key === Qt.Key_E) return 1
        return 0
    }

    // True when the key was one of ours (the caller then accepts the event).
    function press(event) {
        var d = dirOf(event.key)
        if (d === 0) return false
        if (event.isAutoRepeat || _key !== 0) return true
        _key = event.key
        _dir = d
        if (canHold) timer.restart()
        return true
    }

    function release(event) {
        if (event.isAutoRepeat || event.key !== _key || _key === 0) return dirOf(event.key) !== 0
        var d = _dir
        _key = 0
        _dir = 0
        timer.stop()
        tapped(d)
        return true
    }

    // ⚠️ The key is forgotten as the hold fires: the list takes the focus, so the release that
    // ends the hold goes to it and never comes back here — a key left set would swallow the
    // next press.
    property Timer timer: Timer {
        interval: hold.holdMs
        onTriggered: {
            var d = hold._dir
            hold._key = 0
            hold._dir = 0
            hold.held(d)
        }
    }
}
