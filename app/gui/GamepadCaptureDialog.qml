import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import ShortcutManager 1.0

// Builds a gamepad button combo by toggling buttons in a grid. Live SDL button
// capture isn't exposed to QML, and a picker is more reliable on a controller-
// only setup anyway. A valid combo must hold at least one system button
// (Start/Select/LB/RB/Guide) so it never collides with gameplay input.
Popup {
    id: pop

    property int action: -1
    property string actionName: ""

    signal captured(int action, int mask)

    property int _mask: 0
    property var _catalog: []
    property int _cols: 4

    readonly property bool _safe: ShortcutManager.gamepadMaskIsSafe(_mask)
    readonly property int  _conflict: ShortcutManager.gamepadConflict(action, _mask)
    readonly property bool _canSave: _safe && _conflict < 0

    function _toggle(flag) { _mask = (_mask & flag) ? (_mask & ~flag) : (_mask | flag) }

    modal: true
    Overlay.modal: Item {}
    focus: true
    x: (Overlay.overlay ? (Overlay.overlay.width  - width)  / 2 : 0)
    y: (Overlay.overlay ? Math.max(40, Overlay.overlay.height * 0.10) : 40)
    closePolicy: Popup.CloseOnEscape

    // The window scale every other dialog is drawn at (6.3.1): this one opens over Settings,
    // which scales, and was itself in fixed pixels. Hairlines stay 1 px.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    padding: _px(32)

    background: Rectangle {
        color: Theme.card; border.color: Theme.line; border.width: 1; radius: pop._px(12)
    }

    contentItem: ColumnLayout {
        spacing: pop._px(16)

        Label {
            text: qsTr("REBIND CONTROLLER COMBO")
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall); font.bold: true
            font.letterSpacing: 1.6 * pop._u; color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: pop.actionName
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontTitle); color: Theme.text
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("Select the buttons to hold together")
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall); color: Theme.text2
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }

        GridLayout {
            id: grid
            Layout.alignment: Qt.AlignHCenter
            columns: pop._cols
            rowSpacing: pop._px(10)
            columnSpacing: pop._px(10)

            Repeater {
                id: rep
                model: pop._catalog
                delegate: Button {
                    id: chip
                    Layout.preferredWidth: pop._px(84)
                    Layout.preferredHeight: pop._px(64)
                    activeFocusOnTab: true
                    readonly property bool _sel: (pop._mask & modelData.flag) !== 0

                    onClicked: pop._toggle(modelData.flag)
                    Keys.onReturnPressed: pop._toggle(modelData.flag)
                    Keys.onEnterPressed:  pop._toggle(modelData.flag)
                    Keys.onSpacePressed:  pop._toggle(modelData.flag)
                    Keys.onDownPressed: function(event) {
                        var ni = index + pop._cols
                        if (ni < rep.count) rep.itemAt(ni).forceActiveFocus()
                        else saveBtn.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onUpPressed: function(event) {
                        var ni = index - pop._cols
                        if (ni >= 0) { rep.itemAt(ni).forceActiveFocus(); event.accepted = true }
                    }
                    Keys.onLeftPressed: function(event) { if (index > 0) { rep.itemAt(index-1).forceActiveFocus(); event.accepted = true } }
                    Keys.onRightPressed: function(event) { if (index < rep.count-1) { rep.itemAt(index+1).forceActiveFocus(); event.accepted = true } }

                    background: Rectangle {
                        radius: pop._px(8)
                        color: chip._sel ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : Theme.card
                        border.color: chip.activeFocus ? Theme.accent
                                    : chip._sel        ? Qt.darker(Theme.accent, 1.55)
                                    :                     Theme.line
                        border.width: (chip.activeFocus || chip._sel) ? 2 : 1
                    }
                    contentItem: Item {
                        // Center the glyph (+ optional caption) block vertically so
                        // caption-less face buttons aren't top-aligned in the chip.
                        Column {
                            anchors.centerIn: parent
                            spacing: pop._px(3)
                            PadGlyph {
                                anchors.horizontalCenter: parent.horizontalCenter
                                buttonKey: modelData.key
                                label: modelData.label
                                size: pop._px(22)
                            }
                            Label {
                                // Face buttons (A/B/X/Y) carry their symbol in the
                                // glyph itself and differ per vendor, so a fixed
                                // Xbox-named caption would mislabel PS/Switch icons.
                                visible: !["A","B","X","Y"].includes(modelData.key)
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: chip._sel ? Theme.accent : Theme.text2
                                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontCaption)
                            }
                        }
                    }
                }
            }
        }

        Label {
            text: qsTr("Use at least 3 buttons, including one of Start / Select / LB / RB.")
            visible: pop._mask !== 0 && !pop._safe
            color: Theme.warning; font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall)
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: pop._px(420)
        }
        Label {
            text: qsTr("This combo is already used by another action.")
            visible: pop._conflict >= 0
            color: Theme.warning; font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall)
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: pop._px(420)
        }

        // The app's own dialog buttons, where these were hand-drawn copies of them: Save now
        // greys out the same way every other disabled button does.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: pop._px(4)
            spacing: pop._px(14)
            DialogButton {
                id: saveBtn
                text: qsTr("Save")
                affirmative: true
                enabled: pop._canSave
                onActivated: pop._commit()
                Keys.onRightPressed: cancelBtn.forceActiveFocus()
            }
            DialogButton {
                id: cancelBtn
                text: qsTr("Cancel")
                onActivated: pop.close()
                Keys.onLeftPressed: saveBtn.forceActiveFocus()
            }
        }
    }

    function _commit() {
        if (!_canSave) return
        pop.captured(action, _mask)
        pop.close()
    }

    function openFor(act, name, mask) {
        action = act; actionName = name; _mask = mask
        _catalog = ShortcutManager.gamepadButtonCatalog()
        open()
    }

    onOpened: {
        if (rep.count > 0) rep.itemAt(0).forceActiveFocus()
    }
}
