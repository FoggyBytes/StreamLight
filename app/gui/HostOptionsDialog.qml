import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5

// Host "Options" chooser — a wide, centered modal popup showing the host actions as a
// grid of tiles (emoji + short label) instead of the old dropdown list. Fully pad- and
// mouse-navigable: D-pad moves between tiles, A/Enter/Space activates, B/Esc closes.
// Items: [{ kind, icon, label, danger? }]; emits chosen(kind) on activation.
Popup {
    id: dlg

    property string hostName: ""
    property var    items: []
    signal chosen(string kind)

    readonly property int _cols:  3
    readonly property int _cellW: 200
    readonly property int _cellH: 124
    readonly property int _count: items ? items.length : 0
    readonly property int _rows:  Math.max(1, Math.ceil(_count / _cols))

    modal: true
    Overlay.modal: Item {}
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 28

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: 12
    }

    contentItem: Column {
        spacing: 18

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("OPTIONS") + (dlg.hostName.length ? "  ·  " + dlg.hostName : "")
            font.family: Theme.family; font.pixelSize: 13; font.bold: true; font.letterSpacing: 1.6
            color: Theme.text3
        }

        GridView {
            id: grid
            anchors.horizontalCenter: parent.horizontalCenter
            width:  dlg._cols * dlg._cellW
            height: dlg._rows * dlg._cellH
            cellWidth:  dlg._cellW
            cellHeight: dlg._cellH
            model: dlg.items
            focus: true
            interactive: false
            keyNavigationEnabled: true
            keyNavigationWraps: false
            currentIndex: 0

            function _activate() {
                if (currentIndex >= 0 && dlg.items && currentIndex < dlg.items.length) {
                    var it = dlg.items[currentIndex]
                    if (it.disabled === true) return     // greyed, unusable
                    dlg.chosen(it.kind)
                }
            }
            Keys.onReturnPressed: { _activate(); event.accepted = true }
            Keys.onEnterPressed:  { _activate(); event.accepted = true }
            Keys.onSpacePressed:  { _activate(); event.accepted = true }

            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property bool _sel:      grid.currentIndex === index
                readonly property bool _danger:   modelData.danger === true
                readonly property bool _disabled: modelData.disabled === true
                readonly property bool _hasImg:   modelData.iconSource !== undefined && modelData.iconSource !== ""

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 8
                    radius: 10
                    opacity: _disabled ? 0.4 : 1.0
                    color: _sel ? (_danger ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.18)
                                           : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16))
                                : Qt.rgba(1, 1, 1, 0.04)
                    border.color: _sel ? (_danger ? Theme.danger : Theme.accent)
                                       : Theme.line
                    border.width: _sel ? 2 : 1

                    // Same snap as every other focusable thing in the app.
                    Behavior on scale {
                        enabled: !Theme.reduceAnimations
                        NumberAnimation { duration: 130; easing.type: Easing.OutBack; easing.overshoot: 2.0 }
                    }
                    scale: _sel && !Theme.reduceAnimations ? 1.04 : 1.0

                    Column {
                        anchors.centerIn: parent
                        spacing: 10
                        // Icon: an SVG image when iconSource is set, else the emoji glyph.
                        Item {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 32; height: 32
                            Image {
                                anchors.centerIn: parent
                                visible: _hasImg
                                source: modelData.iconSource || ""
                                width: 30; height: 30
                                sourceSize.width: 60; sourceSize.height: 60
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: !_hasImg
                                text: modelData.icon || ""
                                font.pixelSize: 30
                            }
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: grid.cellWidth - 28
                            text: modelData.label
                            color: (_danger && _sel) ? Qt.lighter(Theme.danger, 1.25) : Theme.text
                            font.family: Theme.family; font.pixelSize: 14; font.bold: _sel
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                        // Why a greyed tile is greyed. Two or three words — the tile is small and
                        // the question is only "is this worth going to fix?", not "what is it".
                        // Without it a disabled tile is a dead end: the app says why everywhere
                        // else it withholds something, and this dialog was the exception.
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: grid.cellWidth - 28
                            visible: _disabled && text.length > 0
                            text: modelData.reason || ""
                            color: Theme.text3
                            font.family: Theme.family; font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: _disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onEntered: grid.currentIndex = index
                        onClicked: if (!_disabled) dlg.chosen(modelData.kind)
                    }
                }
            }
        }
    }

    onOpened: { grid.currentIndex = 0; grid.forceActiveFocus() }
    onClosed: { if (typeof stackView !== "undefined" && stackView) stackView.forceActiveFocus() }
}
