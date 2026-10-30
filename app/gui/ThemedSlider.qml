import QtQuick 2.15
import QtQuick.Controls 2.15
import Theme 1.0

/*
 * The app's slider (8.3.0, issue #48) — variant B "Pieno" of docs/mockups/cursori, chosen by
 * Marcello on 30/10/2026, without two of its parts: no ring around the handle and no bubble with
 * the value. The value is already written beside it on the row, and a ring read as one more
 * control. Under the mouse, held or with the pad's focus, the handle only grows a little.
 *
 * A 6 px track with the part done filled, and a full handle with a ring of the card's colour that
 * lifts it off the track. Both in `tint`: the accent fill by default; the profile dialogs pass
 * Theme.text while the row still inherits its value, as every inherited control there is neutral.
 * `marker` (0–1, or -1 for none) puts a small tick on the track — the profile dialogs use it for
 * where the global value is.
 *
 * Replaces five sliders drawn by hand with a 3 px white track and a 14 px dot, or (Background) a
 * filled thin track. Only the look: values, steps and keys stay with each slider.
 */
Slider {
    id: control

    property color tint: Theme.accentFill
    property real marker: -1

    function _px(n) { return Math.max(1, Math.round(n * Theme.uiScale)) }

    readonly property bool _active: hovered || pressed || activeFocus
    readonly property color _tint: enabled ? tint : Theme.offline

    hoverEnabled: true
    implicitHeight: _px(28)

    background: Item {
        x: control.leftPadding
        y: control.topPadding
        width: control.availableWidth
        height: control.availableHeight

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: control._px(6)
            radius: height / 2
            color: Theme.line

            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: control._tint
            }
        }

        Rectangle {
            visible: control.marker >= 0
            anchors.verticalCenter: parent.verticalCenter
            x: control.marker * (parent.width - width)
            width: Math.max(2, control._px(2))
            height: control._px(14)
            radius: width / 2
            color: Theme.text3
        }
    }

    handle: Rectangle {
        readonly property int _d: control._active ? control._px(22) : control._px(20)
        x: control.leftPadding + control.visualPosition * (control.availableWidth - control._px(20))
           - (width - control._px(20)) / 2
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: _d
        height: _d
        radius: width / 2
        color: control._tint
        border.color: Theme.card
        border.width: control._px(3)
        Behavior on width { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 100 } }
        Behavior on height { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 100 } }
    }
}
