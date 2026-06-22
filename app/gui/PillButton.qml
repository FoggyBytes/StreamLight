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

    readonly property color _accent: "#00E676"

    activeFocusOnTab: true
    implicitHeight: 36
    implicitWidth: pill.implicitWidth + 8
    width: implicitWidth
    height: 36

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: "#1f2722"
        border.color: btn.activeFocus ? btn._accent : "#2a2a2a"
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
            color: btn.selected ? "#0d1410" : "#a0a0a0"
            font.family: "DM Sans"; font.pixelSize: 13
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
