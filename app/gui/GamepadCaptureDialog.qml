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
    padding: 32

    background: Rectangle {
        color: "#1a1a1a"; border.color: "#2a2a2a"; border.width: 1; radius: 12
    }

    contentItem: ColumnLayout {
        spacing: 16

        Label {
            text: qsTr("REBIND CONTROLLER COMBO")
            font.family: "DM Sans"; font.pixelSize: 13; font.bold: true
            font.letterSpacing: 1.6; color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: pop.actionName
            font.family: "DM Sans"; font.pixelSize: 19; color: "#f0f0f0"
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("Select the buttons to hold together")
            font.family: "DM Sans"; font.pixelSize: 14; color: "#a0a0a0"
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }

        GridLayout {
            id: grid
            Layout.alignment: Qt.AlignHCenter
            columns: pop._cols
            rowSpacing: 10
            columnSpacing: 10

            Repeater {
                id: rep
                model: pop._catalog
                delegate: Button {
                    id: chip
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 64
                    activeFocusOnTab: true
                    readonly property bool _sel: (pop._mask & modelData.flag) !== 0

                    onClicked: pop._toggle(modelData.flag)
                    Keys.onReturnPressed: pop._toggle(modelData.flag)
                    Keys.onEnterPressed:  pop._toggle(modelData.flag)
                    Keys.onSpacePressed:  pop._toggle(modelData.flag)
                    Keys.onDownPressed: {
                        var ni = index + pop._cols
                        if (ni < rep.count) rep.itemAt(ni).forceActiveFocus()
                        else saveBtn.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onUpPressed: {
                        var ni = index - pop._cols
                        if (ni >= 0) { rep.itemAt(ni).forceActiveFocus(); event.accepted = true }
                    }
                    Keys.onLeftPressed: { if (index > 0) { rep.itemAt(index-1).forceActiveFocus(); event.accepted = true } }
                    Keys.onRightPressed: { if (index < rep.count-1) { rep.itemAt(index+1).forceActiveFocus(); event.accepted = true } }

                    background: Rectangle {
                        radius: 8
                        color: chip._sel ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : "#15171c"
                        border.color: chip.activeFocus ? Theme.accent
                                    : chip._sel        ? Qt.darker(Theme.accent, 1.55)
                                    :                     "#2a2a2a"
                        border.width: (chip.activeFocus || chip._sel) ? 2 : 1
                    }
                    contentItem: Item {
                        // Center the glyph (+ optional caption) block vertically so
                        // caption-less face buttons aren't top-aligned in the chip.
                        Column {
                            anchors.centerIn: parent
                            spacing: 3
                            PadGlyph {
                                anchors.horizontalCenter: parent.horizontalCenter
                                buttonKey: modelData.key
                                label: modelData.label
                                size: 22
                            }
                            Label {
                                // Face buttons (A/B/X/Y) carry their symbol in the
                                // glyph itself and differ per vendor, so a fixed
                                // Xbox-named caption would mislabel PS/Switch icons.
                                visible: !["A","B","X","Y"].includes(modelData.key)
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: chip._sel ? Theme.accent : "#9aa0a8"
                                font.family: "DM Sans"; font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

        Label {
            text: qsTr("Use at least 3 buttons, including one of Start / Select / LB / RB.")
            visible: pop._mask !== 0 && !pop._safe
            color: "#f5a623"; font.family: "DM Sans"; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 420
        }
        Label {
            text: qsTr("This combo is already used by another action.")
            visible: pop._conflict >= 0
            color: "#f5a623"; font.family: "DM Sans"; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 420
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            spacing: 14
            Button {
                id: saveBtn
                text: qsTr("Save")
                enabled: pop._canSave
                opacity: enabled ? 1.0 : 0.4
                activeFocusOnTab: true
                onClicked: pop._commit()
                Keys.onReturnPressed: pop._commit()
                Keys.onEnterPressed:  pop._commit()
                Keys.onSpacePressed:  pop._commit()
                Keys.onRightPressed:  cancelBtn.forceActiveFocus()
                background: Rectangle {
                    implicitWidth: 140; implicitHeight: 42; radius: 8
                    color: saveBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20) : "#1f1f1f"
                    border.color: saveBtn.activeFocus ? Theme.accent : "#2a2a2a"
                    border.width: saveBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: saveBtn.text; color: Theme.accent
                    font.family: "DM Sans"; font.pixelSize: 15; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }
            Button {
                id: cancelBtn
                text: qsTr("Cancel")
                activeFocusOnTab: true
                onClicked: pop.close()
                Keys.onReturnPressed: pop.close()
                Keys.onEnterPressed:  pop.close()
                Keys.onSpacePressed:  pop.close()
                Keys.onLeftPressed:   saveBtn.forceActiveFocus()
                background: Rectangle {
                    implicitWidth: 140; implicitHeight: 42; radius: 8
                    color: cancelBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20) : "#1f1f1f"
                    border.color: cancelBtn.activeFocus ? Theme.accent : "#2a2a2a"
                    border.width: cancelBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: cancelBtn.text; color: "#f0f0f0"
                    font.family: "DM Sans"; font.pixelSize: 15; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
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
