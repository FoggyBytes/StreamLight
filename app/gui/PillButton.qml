import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5

// Single pill-shaped focusable button, visually identical to one
// SegmentedSelector pill (same container, geometry and colours). Used for the
// standalone "Custom" resolution button placed next to the resolution
// SegmentedSelector. D-pad navigation is left to the instance via KeyNavigation.
FocusScope {
    id: btn

    property string text: ""
    property bool   selected: false
    signal clicked()

    readonly property color _accent: Theme.accent
    // Same tokens as one SegmentedSelector pill — the two are meant to be indistinguishable,
    // so they have to derive their colours the same way rather than each hardcoding them.
    readonly property color _bgPill: Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                                 Theme.accent.b, 0.07))

    // Same scale, read the same way, as SegmentedSelector — read the note there. These two
    // are meant to be indistinguishable, so they cannot take it from different places.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    activeFocusOnTab: true
    implicitHeight: btn._px(36)
    implicitWidth: pill.implicitWidth + btn._px(8)
    width: implicitWidth
    height: implicitHeight

    // Bordered control, so hover goes on the border — see HoverState. The inner pill is the
    // selected state, not a hover surface.
    HoverState { id: hov }

    Rectangle {
        anchors.fill: parent
        radius: btn._px(8)
        color: btn._bgPill
        border.color: hov.keyFocused ? btn._accent
                    : hov.active     ? Theme.lineHigh
                    :                  Theme.line
        border.width: hov.keyFocused ? 3 : 1

        Behavior on border.color {
            enabled: !Theme.reduceAnimations
            ColorAnimation { duration: 120; easing.type: Easing.OutQuad }
        }
    }

    Item {
        id: pill
        anchors.centerIn: parent
        implicitWidth: lbl.implicitWidth + btn._px(28)
        implicitHeight: btn._px(30)
        width: implicitWidth; height: implicitHeight

        Rectangle {
            anchors.fill: parent; anchors.margins: btn._px(2); radius: btn._px(5)
            color: btn.selected ? btn._accent : "transparent"
        }
        Label {
            id: lbl
            anchors.centerIn: parent
            text: btn.text
            color: btn.selected ? Theme.onAccent : Theme.text2
            font.family: Theme.family; font.pixelSize: btn._px(13)
            font.bold: btn.selected
        }
    }

    MouseArea {
        anchors.fill: parent
        // cursorShape moved to HoverState: left here it would keep showing a pointing hand
        // after the state had gone back to an arrow for the pad.
        onClicked: { btn.forceActiveFocus(); btn.clicked() }
    }

    Keys.onReturnPressed: btn.clicked()
    Keys.onEnterPressed:  btn.clicked()
    Keys.onSpacePressed:  btn.clicked()
}
