import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import ShortcutManager 1.0

// Records a keyboard hotkey (modifiers + key) for one action. The "Record"
// button enters a listening state so D-pad navigation still works the rest of
// the time; the first valid combination captured exits listening.
Popup {
    id: pop

    property int action: -1
    property string actionName: ""

    signal captured(int action, int modifiers, int sdlKey, int sdlScan, string label)

    // pending capture
    property bool _have: false
    property bool _listening: false
    property int _mods: 0
    property int _key: 0
    property int _scan: 0
    property string _label: ""
    property string _err: ""
    property int _conflict: -1

    function _modBits(qmod) {
        var m = 0
        if (qmod & Qt.ControlModifier) m |= ShortcutManager.SMOD_CTRL
        if (qmod & Qt.AltModifier)     m |= ShortcutManager.SMOD_ALT
        if (qmod & Qt.ShiftModifier)   m |= ShortcutManager.SMOD_SHIFT
        if (qmod & Qt.MetaModifier)    m |= ShortcutManager.SMOD_GUI
        return m
    }
    function _isModifierKey(k) {
        return k === Qt.Key_Control || k === Qt.Key_Alt || k === Qt.Key_Shift
            || k === Qt.Key_Meta || k === Qt.Key_AltGr
            || k === Qt.Key_Super_L || k === Qt.Key_Super_R
    }

    readonly property bool _canSave: _have && _err === "" && _conflict < 0

    modal: true
    Overlay.modal: Item {}
    focus: true
    x: (Overlay.overlay ? (Overlay.overlay.width  - width)  / 2 : 0)
    y: (Overlay.overlay ? Math.max(40, Overlay.overlay.height * 0.14) : 40)
    // While listening, the record button intercepts Escape (to stop listening)
    // and accepts it, so it never reaches here; otherwise Escape/B closes.
    closePolicy: Popup.CloseOnEscape
    padding: 32

    background: Rectangle {
        color: "#1a1a1a"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 12
    }

    contentItem: ColumnLayout {
        spacing: 18

        Label {
            text: qsTr("REBIND SHORTCUT")
            font.family: "DM Sans"; font.pixelSize: 13; font.bold: true
            font.letterSpacing: 1.6; color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: pop.actionName
            font.family: "DM Sans"; font.pixelSize: 19; color: "#f0f0f0"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }

        // Record zone — focused button that captures the combo while listening.
        Button {
            id: recordBtn
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 360
            Layout.preferredHeight: 64
            activeFocusOnTab: true
            focus: true

            onClicked: pop._listening = true
            Keys.onReturnPressed: if (!pop._listening) pop._listening = true
            Keys.onEnterPressed:  if (!pop._listening) pop._listening = true
            Keys.onSpacePressed:  if (!pop._listening) pop._listening = true

            Keys.onPressed: {
                if (!pop._listening) {
                    // Not listening: let Down reach the buttons, Esc close.
                    if (event.key === Qt.Key_Down) { saveBtn.forceActiveFocus(); event.accepted = true }
                    return
                }
                event.accepted = true
                if (event.isAutoRepeat) return
                if (event.key === Qt.Key_Escape) { pop._listening = false; return }
                if (pop._isModifierKey(event.key)) return // wait for a real key

                var mods = pop._modBits(event.modifiers)
                var t = ShortcutManager.translateQtKey(event.key)
                if (!t.ok) { pop._err = qsTr("Unsupported key — try a letter, digit or function key."); return }
                if (mods === 0) { pop._err = qsTr("Add at least one modifier (Ctrl / Alt / Shift / Win)."); return }

                pop._mods = mods; pop._key = t.sdlKey; pop._scan = t.sdlScan; pop._label = t.label
                pop._err = ""
                pop._have = true
                pop._conflict = ShortcutManager.keyboardConflict(pop.action, mods, t.sdlKey, t.sdlScan)
                pop._listening = false
            }

            background: Rectangle {
                radius: 10
                color: pop._listening ? Qt.rgba(0, 0.9, 0.46, 0.12) : "#0f0f0f"
                border.color: (pop._listening || recordBtn.activeFocus) ? "#00E676" : "#2a2a2a"
                border.width: (pop._listening || recordBtn.activeFocus) ? 2 : 1
            }
            contentItem: Item {
                Label {
                    visible: pop._listening
                    anchors.centerIn: parent
                    text: qsTr("Press a key combination…")
                    color: "#00E676"; font.family: "DM Sans"; font.pixelSize: 16; font.bold: true
                }
                Row {
                    visible: !pop._listening
                    anchors.centerIn: parent
                    spacing: 7
                    Repeater {
                        model: pop._have ? ShortcutManager.modifierLabel(pop._mods).split(" + ").concat([pop._label]) : []
                        delegate: Rectangle {
                            width: capLbl.implicitWidth + 18; height: 30; radius: 6
                            color: "#23262e"; border.color: "#3a3f4a"; border.width: 1
                            Label {
                                id: capLbl; anchors.centerIn: parent; text: modelData
                                color: "#dfe2e8"; font.family: "JetBrains Mono"; font.pixelSize: 13; font.bold: true
                            }
                        }
                    }
                    Label {
                        visible: !pop._have
                        text: qsTr("Click or press to record")
                        color: "#808080"; font.family: "DM Sans"; font.pixelSize: 15
                    }
                }
            }
        }

        Label {
            text: pop._err
            visible: pop._err !== ""
            color: "#ff6b6b"; font.family: "DM Sans"; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 380
        }
        Label {
            text: qsTr("This combination is already used by another shortcut.")
            visible: pop._conflict >= 0
            color: "#f5a623"; font.family: "DM Sans"; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 380
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
                Keys.onUpPressed:     recordBtn.forceActiveFocus()
                background: Rectangle {
                    implicitWidth: 140; implicitHeight: 42; radius: 8
                    color: saveBtn.activeFocus ? Qt.rgba(0, 0.9, 0.46, 0.20) : "#1f1f1f"
                    border.color: saveBtn.activeFocus ? "#00E676" : "#2a2a2a"
                    border.width: saveBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: saveBtn.text; color: "#00E676"
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
                Keys.onUpPressed:     recordBtn.forceActiveFocus()
                background: Rectangle {
                    implicitWidth: 140; implicitHeight: 42; radius: 8
                    color: cancelBtn.activeFocus ? Qt.rgba(0, 0.9, 0.46, 0.20) : "#1f1f1f"
                    border.color: cancelBtn.activeFocus ? "#00E676" : "#2a2a2a"
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
        pop.captured(action, _mods, _key, _scan, _label)
        pop.close()
    }

    onOpened: {
        _have = false; _listening = false; _err = ""; _conflict = -1
        _mods = 0; _key = 0; _scan = 0; _label = ""
        recordBtn.forceActiveFocus()
    }
}
