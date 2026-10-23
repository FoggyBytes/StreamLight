import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import SdlGamepadKeyNavigation 1.0

// Host "Options" chooser — a wide, centered modal popup showing the host actions as a
// grid of tiles (emoji + short label) instead of the old dropdown list. Fully pad- and
// mouse-navigable: D-pad moves between tiles, A/Enter/Space activates, B/Esc closes.
// Items: [{ kind, icon, label, danger? }]; emits chosen(kind) on activation.
Popup {
    id: dlg

    // 7.0.0: the shared spring in and quick out (PopEnter/PopExit), off with Reduce animations.
    // Grows out of the card's Options button, low on the left of the card (7.0.0).
    transformOrigin: Popup.BottomLeft
    enter: PopEnter { fromScale: 0.55 }
    exit: PopExit {}

    // Shared dialog measurements — see Theme.uiScale.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    property string hostName: ""
    property var    items: []
    signal chosen(string kind)

    /*
     * The tile the cursor was on last time (7.0.0, 09/10/2026): Options opens on it again, for
     * as long as the app runs. By KIND, not by index — the tiles change with the host and its
     * state (Wake, Tailscale, the link tile come and go), so an index would land on another one.
     * ⚠️ Placed in onAboutToShow, before the popup is drawn. It was reset to the first tile in
     * onOpened, after the opening animation: the cursor was seen on the last tile for the whole
     * spring and then jumped to the top left.
     */
    property string _lastKind: ""
    function _indexOfKind(kind) {
        for (var i = 0; i < dlg._count; i++) if (dlg.items[i].kind === kind) return i
        return -1
    }

    // Four columns for seven or eight tiles (8.0.0): with the link, Windows Update and access
    // tiles gone to the host section, three left Delete alone on a row of its own.
    readonly property int _cols:  (_count === 7 || _count === 8) ? 4 : 3
    readonly property int _cellW: dlg._px(200)
    readonly property int _cellH: dlg._px(124)
    readonly property int _count: items ? items.length : 0
    readonly property int _rows:  Math.max(1, Math.ceil(_count / _cols))

    modal: true
    Overlay.modal: Rectangle { color: Theme.scrim }
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: dlg._px(28)

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: dlg._px(12)
    }

    contentItem: Column {
        spacing: dlg._px(18)

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("OPTIONS") + (dlg.hostName.length ? "  ·  " + dlg.hostName : "")
            font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall); font.bold: true; font.letterSpacing: 1.6
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
            onCurrentIndexChanged: if (dlg.opened && currentIndex >= 0 && currentIndex < dlg._count)
                                       dlg._lastKind = dlg.items[currentIndex].kind

            // The focus glides (7.0.0): with the pad or the keyboard, the lit tile is one shape
            // that slides between tiles; the tiles then draw no light.
            // ⚠️ Moved by the GridView, and only for a step the user took (09/10/2026). It had a
            // spring of its own on x and y, so every opening — currentIndex back to 0 — flew it
            // from the tile chosen last time across the grid.
            property bool _stepping: false
            Timer { id: stepWindow; interval: 350; onTriggered: grid._stepping = false }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                        || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    grid._stepping = true
                    stepWindow.restart()
                }
                event.accepted = false
            }
            highlightFollowsCurrentItem: true
            highlightMoveDuration: Theme.reduceAnimations || !grid._stepping ? 0 : 120
            readonly property bool _glide: SdlGamepadKeyNavigation.inputMode === "key" && !Theme.reduceAnimations
            readonly property bool _curDanger: currentIndex >= 0 && dlg.items && currentIndex < dlg.items.length
                                               && dlg.items[currentIndex].danger === true
            highlight: Item {
              Rectangle {
                visible: grid._glide && grid.currentItem !== null
                anchors.fill: parent
                anchors.margins: 8
                radius: dlg._px(10)
                color: grid._curDanger ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.18)
                                       : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                border.color: grid._curDanger ? Theme.danger : Theme.ink
                border.width: 2
              }
            }

            function _activate() {
                if (currentIndex >= 0 && dlg.items && currentIndex < dlg.items.length) {
                    var it = dlg.items[currentIndex]
                    if (it.disabled === true) return     // greyed, unusable
                    dlg.chosen(it.kind)
                }
            }
            Keys.onReturnPressed: function(event) { _activate(); event.accepted = true }
            Keys.onEnterPressed:  function(event) { _activate(); event.accepted = true }
            Keys.onSpacePressed:  function(event) { _activate(); event.accepted = true }

            delegate: Item {
                id: tile
                width: grid.cellWidth
                height: grid.cellHeight

                // The tiles arrive one after another when the grid opens (7.0.0): each rises
                // 10 px and fades in, 20 ms after the one before.
                property real _in: 1
                opacity: _in
                transform: Translate { y: (1 - tile._in) * dlg._px(10) }
                Connections {
                    target: dlg
                    function onAboutToShow() {
                        if (Theme.reduceAnimations) { tile._in = 1; return }
                        tile._in = 0
                        tileIn.restart()
                    }
                }
                SequentialAnimation {
                    id: tileIn
                    PauseAnimation { duration: 30 + index * 20 }
                    NumberAnimation { target: tile; property: "_in"; to: 1; duration: 170; easing.type: Easing.OutCubic }
                }
                readonly property bool _sel:      grid.currentIndex === index
                readonly property bool _danger:   modelData.danger === true
                readonly property bool _disabled: modelData.disabled === true
                readonly property bool _hasImg:   modelData.iconSource !== undefined && modelData.iconSource !== ""
                readonly property bool _winMark:  modelData.winMark === true

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 8
                    radius: dlg._px(10)
                    opacity: _disabled ? 0.4 : 1.0
                    color: (_sel && grid._glide) ? Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.04)
                         : _sel ? (_danger ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.18)
                                           : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16))
                                : Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.04)
                    border.color: (_sel && !grid._glide) ? (_danger ? Theme.danger : Theme.ink)
                                       : Theme.line
                    border.width: (_sel && !grid._glide) ? 2 : 1

                    // Same snap as every other focusable thing in the app.
                    Behavior on scale {
                        enabled: !Theme.reduceAnimations
                        NumberAnimation { duration: 130; easing.type: Easing.OutBack; easing.overshoot: 2.0 }
                    }
                    // 7.0.0: a press dips it before it springs back.
                    scale: Theme.reduceAnimations ? 1.0 : tileMouse.pressed ? 0.96 : _sel ? 1.04 : 1.0

                    Column {
                        anchors.centerIn: parent
                        spacing: dlg._px(10)
                        // Icon: an SVG image when iconSource is set, else the emoji glyph.
                        Item {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: dlg._px(32); height: dlg._px(32)
                            Image {
                                anchors.centerIn: parent
                                visible: _hasImg
                                source: modelData.iconSource || ""
                                width: dlg._px(30); height: dlg._px(30)
                                sourceSize.width: 60; sourceSize.height: 60
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: !_hasImg && !_winMark
                                text: modelData.icon || ""
                                font.pixelSize: dlg._px(Theme.fontH1)
                            }
                            // The Windows mark, drawn — the same four squares and colours as the
                            // Power dialog's (PowerDialog.WinMark), at tile size: 2×12 + 2 = 26 px,
                            // the footprint of the emoji glyphs beside it. Grid fills row by row:
                            // top left, top right, bottom left, bottom right.
                            Grid {
                                anchors.centerIn: parent
                                visible: _winMark
                                columns: 2
                                spacing: dlg._px(2)
                                Repeater {
                                    model: ["#6CD2FE", "#4ACFFF", "#38C0FF", "#20AEFF"]
                                    Rectangle {
                                        required property string modelData
                                        width: dlg._px(12); height: dlg._px(12)
                                        color: modelData
                                    }
                                }
                            }
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: grid.cellWidth - dlg._px(28)
                            text: modelData.label
                            color: (_danger && _sel) ? Qt.lighter(Theme.danger, 1.25) : Theme.text
                            font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall); font.bold: _sel
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                        // Why a greyed tile is greyed. Two or three words — the tile is small and
                        // the question is only "is this worth going to fix?", not "what is it".
                        // Without it a disabled tile is a dead end: the app says why everywhere
                        // else it withholds something, and this dialog was the exception.
                        // On a live tile, `note` takes the same place: a fact worth seeing before
                        // choosing it (7.0.0 — PyroWave Test: when it last ran, and its answer).
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: grid.cellWidth - dlg._px(28)
                            visible: text.length > 0
                            text: _disabled ? (modelData.reason || "") : (modelData.note || "")
                            color: Theme.text3
                            font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption)
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                        // NEW on a tile the installed release added or rebuilt (7.0.0).
                        NewBadge {
                            anchors.horizontalCenter: parent.horizontalCenter
                            since: modelData.since || ""
                        }
                    }

                    MouseArea {
                        id: tileMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: _disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
                        // Hover selects only with the mouse in hand, as on the host page: a
                        // pointer parked over the grid must not move the pad's tile (#24).
                        onEntered: if (SdlGamepadKeyNavigation.inputMode !== "key") grid.currentIndex = index
                        onClicked: if (!_disabled) dlg.chosen(modelData.kind)
                    }
                }
            }
        }
    }

    onAboutToShow: grid.currentIndex = Math.max(0, dlg._indexOfKind(dlg._lastKind))
    onOpened: grid.forceActiveFocus()
    onClosed: { if (typeof stackView !== "undefined" && stackView) stackView.forceActiveFocus() }
}
