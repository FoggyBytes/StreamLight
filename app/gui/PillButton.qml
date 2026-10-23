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
    // 7.0.0: a colour square before the text (the Accent colour row shows its colour), and a
    // chevron after it for a button that opens a popup rather than choosing a value.
    property color  swatch: "transparent"
    property bool   chevron: false
    signal clicked()

    readonly property color _accent: Theme.accentFill
    // Same tokens as one SegmentedSelector pill — the two are meant to be indistinguishable,
    // so they have to derive their colours the same way rather than each hardcoding them.
    readonly property color _bgPillSolid: Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                                      Theme.accent.b, 0.07))
    // 50% opaque (09/10/2026, Marcello, after trying 90/80/60): at full strength the light controls
    // were a bright white; half of what is behind takes the glare off, and Dark gets the same.
    readonly property color _bgPill: Qt.rgba(_bgPillSolid.r, _bgPillSolid.g, _bgPillSolid.b, 0.5)

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
        border.color: hov.keyFocused ? Theme.ink
                    : hov.active     ? Theme.lineHigh
                    :                  Theme.line
        border.width: hov.keyFocused ? 3 : 1

        Behavior on border.color {
            enabled: !Theme.reduceAnimations
            ColorAnimation { duration: 120; easing.type: Easing.OutQuad }
        }
    }

    readonly property bool _hasSwatch: btn.swatch.a > 0

    Item {
        id: pill
        anchors.centerIn: parent
        implicitWidth: content.implicitWidth + btn._px(28)
        implicitHeight: btn._px(30)
        width: implicitWidth; height: implicitHeight

        Rectangle {
            anchors.fill: parent; anchors.margins: btn._px(2); radius: btn._px(5)
            color: btn.selected ? btn._accent : "transparent"
        }
        Row {
            id: content
            anchors.centerIn: parent
            spacing: btn._px(8)
            Rectangle {
                visible: btn._hasSwatch
                anchors.verticalCenter: parent.verticalCenter
                width: btn._px(20); height: width
                radius: btn._px(5)
                color: btn.swatch
                border.color: Theme.lineHigh
                border.width: 1
            }
            Label {
                id: lbl
                anchors.verticalCenter: parent.verticalCenter
                text: btn.text
                color: btn.selected ? Theme.onAccent
                     : btn._hasSwatch ? Theme.text : Theme.text2
                font.family: Theme.family; font.pixelSize: btn._px(Theme.fontSmall * Theme.textScale)
                font.bold: btn.selected || btn._hasSwatch
            }
            Label {
                visible: btn.chevron
                anchors.verticalCenter: parent.verticalCenter
                text: "›"
                color: Theme.text3
                font.family: Theme.family; font.pixelSize: btn._px(Theme.fontTitle * Theme.textScale)
            }
        }
    }

    MouseArea {
        id: pillMouse
        anchors.fill: parent
        // cursorShape moved to HoverState: left here it would keep showing a pointing hand
        // after the state had gone back to an arrow for the pad.
        onClicked: { btn.forceActiveFocus(); btn.clicked() }
    }

    Keys.onReturnPressed: { press.kick(); btn.clicked() }
    Keys.onEnterPressed:  { press.kick(); btn.clicked() }
    Keys.onSpacePressed:  { press.kick(); btn.clicked() }

    // 7.0.0: the press dips the pill and it springs back.
    PressBounce { id: press; down: pillMouse.pressed }
    scale: press.scale
    Behavior on scale {
        enabled: !Theme.reduceAnimations
        NumberAnimation { duration: 140; easing.type: Easing.OutBack; easing.overshoot: 2.0 }
    }
}
