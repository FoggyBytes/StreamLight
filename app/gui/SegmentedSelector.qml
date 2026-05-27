import QtQuick 2.15
import QtQuick.Controls 2.5

// Row of pill buttons; one selected at a time. D-pad ◀/▶ cycle, ▲/▼ propagate.
// Flat replacement for ComboBox dropdowns in SettingsScreen.
FocusScope {
    id: selector

    property var labels: []
    property int currentIndex: 0
    signal activated(int index)

    readonly property color _accent:    "#00E676"
    readonly property color _bgPill:    "#1f2722"
    readonly property color _border:    "#2a2a2a"
    readonly property color _textOn:    "#0d1410"
    readonly property color _textOff:   "#a0a0a0"
    readonly property int   _pillPadX:  14

    activeFocusOnTab: true

    implicitWidth: row.implicitWidth + 8
    implicitHeight: 36

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: selector._bgPill
        border.color: selector.activeFocus ? selector._accent : selector._border
        border.width: selector.activeFocus ? 3 : 1
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 0

        Repeater {
            model: selector.labels
            delegate: Item {
                id: pill
                width: pillLabel.implicitWidth + selector._pillPadX * 2
                height: 30

                readonly property bool _selected: selector.currentIndex === index

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 5
                    color: pill._selected ? selector._accent : "transparent"
                }
                Label {
                    id: pillLabel
                    anchors.centerIn: parent
                    text: modelData
                    color: pill._selected ? selector._textOn : selector._textOff
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: pill._selected
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    onClicked: {
                        selector.forceActiveFocus()
                        if (selector.currentIndex !== index) {
                            selector.currentIndex = index
                            selector.activated(index)
                        }
                    }
                }
            }
        }
    }

    Keys.onLeftPressed: {
        if (selector.currentIndex > 0) {
            selector.currentIndex--
            selector.activated(selector.currentIndex)
        }
        event.accepted = true
    }
    Keys.onRightPressed: {
        if (selector.currentIndex < selector.labels.length - 1) {
            selector.currentIndex++
            selector.activated(selector.currentIndex)
        }
        event.accepted = true
    }
}
