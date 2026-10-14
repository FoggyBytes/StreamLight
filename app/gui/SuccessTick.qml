import QtQuick 2.15
import QtQuick.Shapes 1.15

import Theme 1.0

/*
 * The end of a wait that went well (7.0.0): a disc pops in where the spinner was and a check
 * mark draws itself across it. Put it in the spinner's place and call play() when the wait
 * succeeds; it stays drawn until hidden. With Reduce animations on it appears complete.
 */
Item {
    id: tick

    property real size: 20
    property color color: Theme.online

    implicitWidth: size
    implicitHeight: size

    property real _s: 0     // disc scale
    property real _p: 0     // check progress, 0..1

    function play() {
        if (Theme.reduceAnimations) { _s = 1; _p = 1; return }
        _s = 0; _p = 0
        anim.restart()
    }

    SequentialAnimation {
        id: anim
        NumberAnimation { target: tick; property: "_s"; to: 1; duration: 260; easing.type: Easing.OutBack; easing.overshoot: 2.0 }
        NumberAnimation { target: tick; property: "_p"; to: 1; duration: 260; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.centerIn: parent
        width: tick.size; height: tick.size
        radius: width / 2
        color: tick.color
        scale: tick._s
    }

    // Two strokes, drawn one after the other: the short leg to 40% of the progress, the long
    // one from there to the end.
    readonly property real _ax: size * 0.27
    readonly property real _ay: size * 0.53
    readonly property real _bx: size * 0.44
    readonly property real _by: size * 0.70
    readonly property real _cx: size * 0.75
    readonly property real _cy: size * 0.33
    readonly property real _k1: Math.min(1, _p / 0.4)
    readonly property real _k2: Math.max(0, (_p - 0.4) / 0.6)

    Shape {
        anchors.fill: parent
        visible: tick._p > 0
        ShapePath {
            strokeColor: Theme.onColor(tick.color)
            strokeWidth: Math.max(1.5, tick.size * 0.12)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            startX: tick._ax; startY: tick._ay
            PathLine { x: tick._ax + (tick._bx - tick._ax) * tick._k1; y: tick._ay + (tick._by - tick._ay) * tick._k1 }
            PathLine { x: tick._bx + (tick._cx - tick._bx) * tick._k2 * (tick._k1 >= 1 ? 1 : 0)
                       y: tick._by + (tick._cy - tick._by) * tick._k2 * (tick._k1 >= 1 ? 1 : 0) }
        }
    }
}
