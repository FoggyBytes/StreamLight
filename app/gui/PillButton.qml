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

    activeFocusOnTab: true
    implicitHeight: 36
    implicitWidth: pill.implicitWidth + 8
    width: implicitWidth
    height: 36

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: btn._bgPill
        border.color: btn.activeFocus ? btn._accent : Theme.line
        border.width: btn.activeFocus ? 3 : 1
    }

    Item {
        id: pill
        anchors.centerIn: parent
        implicitWidth: lbl.implicitWidth + 28
        implicitHeight: 30
        width: implicitWidth; height: 30

        Rectangle {
            anchors.fill: parent; anchors.margins: 2; radius: 5
            color: btn.selected ? btn._accent : "transparent"
        }
        Label {
            id: lbl
            anchors.centerIn: parent
            text: btn.text
            color: btn.selected ? Theme.onAccent : Theme.text2
            font.family: Theme.family; font.pixelSize: 13
            font.bold: btn.selected
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: { btn.forceActiveFocus(); btn.clicked() }
    }

    Keys.onReturnPressed: btn.clicked()
    Keys.onEnterPressed:  btn.clicked()
    Keys.onSpacePressed:  btn.clicked()
}
