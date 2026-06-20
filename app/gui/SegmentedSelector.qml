import QtQuick 2.15
import QtQuick.Controls 2.5

// Row of pill buttons; one selected at a time. D-pad ◀/▶ cycle, ▲/▼ propagate.
// Flat replacement for ComboBox dropdowns in SettingsScreen.
FocusScope {
    id: selector

    property var labels: []
    property int currentIndex: 0
    signal activated(int index)

    // Indices that are shown greyed-out and cannot be selected (skipped by ◀/▶
    // navigation and by clicks). Empty by default.
    property var disabledIndices: []

    function isDisabled(i) {
        for (var k = 0; k < disabledIndices.length; ++k)
            if (disabledIndices[k] === i)
                return true
        return false
    }

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
                readonly property bool _disabled: selector.isDisabled(index)

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 5
                    color: pill._selected ? selector._accent : "transparent"
                    opacity: pill._disabled ? 0.4 : 1.0
                }
                Label {
                    id: pillLabel
                    anchors.centerIn: parent
                    text: modelData
                    color: pill._disabled ? "#555555"
                         : pill._selected ? selector._textOn
                         :                  selector._textOff
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: pill._selected
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !pill._disabled
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

    // At a boundary we leave the event UNaccepted so KeyNavigation.left/right
    // (if set on the instance) can move focus to a neighbour — e.g. Right past
    // the last profile tab focuses the "+ Add" button.
    Keys.onLeftPressed: {
        var i = selector.currentIndex - 1
        while (i >= 0 && selector.isDisabled(i)) i--
        if (i >= 0) {
            selector.currentIndex = i
            selector.activated(i)
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
    Keys.onRightPressed: {
        var i = selector.currentIndex + 1
        while (i < selector.labels.length && selector.isDisabled(i)) i++
        if (i < selector.labels.length) {
            selector.currentIndex = i
            selector.activated(i)
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
}
