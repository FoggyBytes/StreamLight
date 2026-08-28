import Theme 1.0
import SdlGamepadKeyNavigation 1.0
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

    readonly property color _accent:    Theme.accent
    // The container carries a trace of the accent, the same way the page background does, so
    // the control belongs to the palette instead of sitting on it as a grey box.
    readonly property color _bgPill:    Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                                    Theme.accent.b, 0.07))
    readonly property color _border:    Theme.line
    // Was a hardcoded near-black green. That was only ever legible because the accent was
    // green too: on an amber or lime accent it is dark green on yellow, and on a deep blue one
    // it is black on navy. Theme decides this from the accent's luminance, which is the whole
    // reason a user-chosen accent can be allowed at all.
    readonly property color _textOn:    Theme.onAccent
    readonly property color _textOff:   Theme.text2
    readonly property int   _pillPadX:  14

    activeFocusOnTab: true

    // The focus half of HoverState's rule, written out here because the HoverStates in this
    // control sit one level down, on the pills, and this border belongs to the container.
    // Same predicate, so the two cannot drift: while the mouse is in hand the ring is off and
    // the pill under the pointer washes instead.
    readonly property bool _keyFocused: selector.activeFocus
                                        && SdlGamepadKeyNavigation.inputMode !== "pointer"

    implicitWidth: row.implicitWidth + 8
    implicitHeight: 36

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: selector._bgPill
        border.color: selector._keyFocused ? selector._accent : selector._border
        border.width: selector._keyFocused ? 3 : 1
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

                // ⚠️ Disabled used to be an opacity on the fill plus `enabled: false` on the
                // MouseArea alone, which left this Item enabled — so HoverState's guard would
                // have seen nothing wrong with a greyed pill. Say it on the Item.
                enabled: !pill._disabled

                HoverState { id: hov }

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
                    color: pill._disabled ? Theme.offline
                         : pill._selected ? selector._textOn
                         :                  selector._textOff
                    font.family: Theme.family
                    font.pixelSize: 13
                    font.bold: pill._selected
                }
                // The one control in the app whose hover is a wash rather than a border: the
                // border belongs to the container around these, and a border inside a border
                // two pixels away is noise. A pill is a filled shape, so it lightens.
                //
                // ⚠️ The SELECTED pill does not light up. Hovering the option you already have
                // chosen has nothing to say, and five percent of white over a full accent
                // moves red from 0 to 13 and leaves green and blue where they are — it would
                // have been a promise the pixels could not keep. The cursor still says it is
                // clickable.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 5
                    color: Qt.rgba(1, 1, 1, 0.05)
                    opacity: (hov.active && !pill._selected) ? 1 : 0

                    Behavior on opacity {
                        enabled: !Theme.reduceAnimations
                        NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    // enabled / cursorShape / hoverEnabled are HoverState's job now.
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
