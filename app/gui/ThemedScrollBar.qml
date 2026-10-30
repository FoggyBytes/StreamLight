import QtQuick 2.15
import QtQuick.Controls 2.15
import Theme 1.0

/*
 * The app's scroll bar (8.3.0, issue #48) — variant A "Filo" of docs/mockups/barre, chosen by
 * Marcello on 30/10/2026.
 *
 * A thin stroke of the accent, always there while there is more to scroll, over a 1 px line the
 * whole height of the view: with a pad nobody hovers a bar, and it has to say "there is more
 * below" without being asked. Under the mouse, or held, stroke and track widen to 8 px. The
 * accent is Theme.ink — the accent as a line, darkened in Light until it holds 3:1 on a card.
 *
 * It replaces three bars that did not agree: Material's grey one in Settings and on the host
 * page, and the neutral 6 px one drawn by hand in the two profile dialogs. The hit area is
 * 14 px wide; the stroke sits in its middle, so it keeps clear of whatever is beside it.
 *
 * Hidden — not faded — when everything fits (size is 1), so it never implies rows that do not
 * exist.
 */
ScrollBar {
    id: bar

    function _px(n) { return Math.max(1, Math.round(n * Theme.uiScale)) }

    readonly property bool _vertical: orientation === Qt.Vertical
    readonly property bool _wide: hovered || pressed
    readonly property int _stroke: _wide ? _px(8) : _px(3)

    policy: ScrollBar.AlwaysOn
    visible: size < 1.0
    hoverEnabled: true
    padding: 0
    minimumSize: 0.06
    implicitWidth: _vertical ? _px(14) : 0
    implicitHeight: _vertical ? 0 : _px(14)

    background: Item {
        Rectangle {
            anchors.centerIn: parent
            width: bar._vertical ? (bar._wide ? bar._px(8) : 1) : parent.width
            height: bar._vertical ? parent.height : (bar._wide ? bar._px(8) : 1)
            radius: Math.min(width, height) / 2
            color: bar._wide ? Theme.line : Theme.lineHigh
            Behavior on width { enabled: !Theme.reduceAnimations && bar._vertical; NumberAnimation { duration: 120 } }
            Behavior on height { enabled: !Theme.reduceAnimations && !bar._vertical; NumberAnimation { duration: 120 } }
        }
    }

    contentItem: Item {
        implicitWidth: bar._vertical ? bar._px(14) : 0
        implicitHeight: bar._vertical ? 0 : bar._px(14)
        Rectangle {
            anchors.centerIn: parent
            width: bar._vertical ? bar._stroke : parent.width
            height: bar._vertical ? parent.height : bar._stroke
            radius: Math.min(width, height) / 2
            color: bar.pressed ? Qt.lighter(Theme.ink, Theme.light ? 0.85 : 1.2) : Theme.ink
            Behavior on width { enabled: !Theme.reduceAnimations && bar._vertical; NumberAnimation { duration: 120 } }
            Behavior on height { enabled: !Theme.reduceAnimations && !bar._vertical; NumberAnimation { duration: 120 } }
        }
    }
}
