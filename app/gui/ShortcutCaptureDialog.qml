import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import ShortcutManager 1.0

// Rebind a keyboard shortcut as toggleable modifiers + a single key. Modifiers
// are toggles (Ctrl/Alt/Shift, at least two required) rather than physically
// held, and the key is captured on its own — so there's no live-combo modifier
// race and Shift never mutates digits/punctuation. The key binds by hardware
// scan code, so it works on any keyboard layout.
Popup {
    id: pop

    property int action: -1
    property string actionName: ""

    signal captured(int action, int modifiers, int sdlKey, int sdlScan, string label)

    // modifier toggles
    property bool _ctrl: true
    property bool _alt: true
    property bool _shift: true
    // selected key
    property bool _haveKey: false
    property int _key: 0
    property int _scan: 0
    property string _label: ""
    // key-capture state
    property bool _keyListening: false
    property string _err: ""
    property int _conflict: -1
    property string _conflictName: ""

    readonly property int _mods:
          (_ctrl  ? ShortcutManager.SMOD_CTRL  : 0)
        | (_alt   ? ShortcutManager.SMOD_ALT   : 0)
        | (_shift ? ShortcutManager.SMOD_SHIFT : 0)
    function _modCount() { return (_ctrl ? 1 : 0) + (_alt ? 1 : 0) + (_shift ? 1 : 0) }
    readonly property bool _canSave: _modCount() >= 2 && _haveKey && _conflict < 0

    function _isModifierKey(k) {
        return k === Qt.Key_Control || k === Qt.Key_Alt || k === Qt.Key_Shift
            || k === Qt.Key_Meta || k === Qt.Key_AltGr
            || k === Qt.Key_Super_L || k === Qt.Key_Super_R
    }
    function _recheck() {
        pop._conflict = pop._haveKey ? ShortcutManager.keyboardConflict(pop.action, pop._mods, pop._key, pop._scan) : -1
        pop._conflictName = pop._conflict >= 0 ? ShortcutManager.keyboardModel()[pop._conflict].name : ""
    }
    // Turning a modifier off is blocked when it would drop below two.
    function _toggle(which) {
        if (which === "ctrl")  { if (pop._ctrl  && pop._modCount() <= 2) return; pop._ctrl  = !pop._ctrl }
        if (which === "alt")   { if (pop._alt   && pop._modCount() <= 2) return; pop._alt   = !pop._alt }
        if (which === "shift") { if (pop._shift && pop._modCount() <= 2) return; pop._shift = !pop._shift }
        pop._recheck()
    }

    modal: true
    Overlay.modal: Item {}
    focus: true
    x: (Overlay.overlay ? (Overlay.overlay.width  - width)  / 2 : 0)
    y: (Overlay.overlay ? Math.max(40, Overlay.overlay.height * 0.12) : 40)
    closePolicy: Popup.CloseOnEscape
    padding: 32

    background: Rectangle {
        color: "#1a1a1a"; border.color: "#2a2a2a"; border.width: 1; radius: 12
    }

    component ModToggle: Button {
        id: mt
        property string lbl: ""
        property bool on: false
        signal flip()
        activeFocusOnTab: true
        implicitWidth: 96
        implicitHeight: 46
        onClicked: flip()
        Keys.onReturnPressed: flip()
        Keys.onEnterPressed:  flip()
        Keys.onSpacePressed:  flip()
        background: Rectangle {
            radius: 8
            color: mt.on ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : "#15171c"
            border.color: mt.activeFocus ? Theme.accent : mt.on ? Qt.darker(Theme.accent, 1.55) : "#2a2a2a"
            border.width: (mt.activeFocus || mt.on) ? 2 : 1
        }
        contentItem: Label {
            text: mt.lbl
            color: mt.on ? Theme.accent : "#9aa0a8"
            font.family: "DM Sans"; font.pixelSize: 15; font.bold: mt.on
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
    }

    contentItem: ColumnLayout {
        spacing: 16

        Label {
            text: qsTr("REBIND SHORTCUT")
            font.family: "DM Sans"; font.pixelSize: 13; font.bold: true
            font.letterSpacing: 1.6; color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: pop.actionName
            font.family: "DM Sans"; font.pixelSize: 19; color: "#f0f0f0"
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 520
        }

        Label {
            text: qsTr("MODIFIERS")
            font.family: "DM Sans"; font.pixelSize: 11; font.bold: true
            font.letterSpacing: 1.4; color: "#6f7681"
            Layout.alignment: Qt.AlignHCenter
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12
            ModToggle { id: ctrlTg;  lbl: "Ctrl";  on: pop._ctrl;  onFlip: pop._toggle("ctrl") }
            ModToggle { id: altTg;   lbl: "Alt";   on: pop._alt;   onFlip: pop._toggle("alt") }
            ModToggle { id: shiftTg; lbl: "Shift"; on: pop._shift; onFlip: pop._toggle("shift") }
        }

        Label {
            text: qsTr("KEY")
            font.family: "DM Sans"; font.pixelSize: 11; font.bold: true
            font.letterSpacing: 1.4; color: "#6f7681"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 2
        }
        Button {
            id: keyBtn
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 320
            Layout.preferredHeight: 56
            activeFocusOnTab: true
            focus: true

            function _start() { pop._err = ""; pop._keyListening = true }
            onClicked: if (!pop._keyListening) _start()
            Keys.onReturnPressed: if (!pop._keyListening) _start()
            Keys.onEnterPressed:  if (!pop._keyListening) _start()
            Keys.onSpacePressed:  if (!pop._keyListening) _start()

            Keys.onPressed: {
                if (!pop._keyListening) {
                    if (event.key === Qt.Key_Down) { saveBtn.forceActiveFocus(); event.accepted = true }
                    return
                }
                event.accepted = true
                if (event.isAutoRepeat) return
                if (event.key === Qt.Key_Escape) { pop._keyListening = false; return }
                if (pop._isModifierKey(event.key)) return // modifiers come from the toggles

                var t = ShortcutManager.translateQtKey(event.key, event.nativeScanCode)
                if (!t.ok) { pop._err = qsTr("Unsupported key — try a letter, number, punctuation or function key."); return }
                pop._key = t.sdlKey; pop._scan = t.sdlScan; pop._label = t.label
                pop._haveKey = true; pop._err = ""
                pop._keyListening = false
                pop._recheck()
            }

            background: Rectangle {
                radius: 10
                color: pop._keyListening ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12) : "#0f0f0f"
                border.color: (pop._keyListening || keyBtn.activeFocus) ? Theme.accent : "#2a2a2a"
                border.width: (pop._keyListening || keyBtn.activeFocus) ? 2 : 1
            }
            contentItem: Item {
                Label {
                    visible: pop._keyListening
                    anchors.centerIn: parent
                    text: qsTr("Press the key…")
                    color: Theme.accent; font.family: "DM Sans"; font.pixelSize: 16; font.bold: true
                }
                Label {
                    visible: !pop._keyListening
                    anchors.centerIn: parent
                    text: pop._haveKey ? pop._label : qsTr("Click or press to set")
                    color: pop._haveKey ? "#dfe2e8" : "#808080"
                    font.family: "DM Sans"
                    font.pixelSize: pop._haveKey ? 18 : 15
                    font.bold: pop._haveKey
                }
            }
        }

        // Combined preview
        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 2
            spacing: 7
            Repeater {
                model: {
                    var a = []
                    if (pop._ctrl)  a.push("Ctrl")
                    if (pop._alt)   a.push("Alt")
                    if (pop._shift) a.push("Shift")
                    if (pop._haveKey) a.push(pop._label)
                    return a
                }
                delegate: Rectangle {
                    width: pvl.implicitWidth + 18; height: 30; radius: 6
                    color: "#23262e"; border.color: "#3a3f4a"; border.width: 1
                    Label {
                        id: pvl; anchors.centerIn: parent; text: modelData
                        color: "#dfe2e8"; font.family: "DM Sans"; font.pixelSize: 13; font.bold: true
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
            text: qsTr("Keep at least two modifiers.")
            visible: pop._modCount() < 2
            color: "#f5a623"; font.family: "DM Sans"; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("Already used by “%1” — pick a different combination.").arg(pop._conflictName)
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
        pop.captured(action, _mods, _key, _scan, _label)
        pop.close()
    }

    onOpened: {
        // Pre-fill from the current binding.
        var row = ShortcutManager.keyboardModel()[action]
        if (row) {
            _ctrl  = (row.modifiers & ShortcutManager.SMOD_CTRL)  !== 0
            _alt   = (row.modifiers & ShortcutManager.SMOD_ALT)   !== 0
            _shift = (row.modifiers & ShortcutManager.SMOD_SHIFT) !== 0
            _haveKey = true
            _key = row.key; _scan = row.scan; _label = row.label
        } else {
            _ctrl = true; _alt = true; _shift = true
            _haveKey = false; _key = 0; _scan = 0; _label = ""
        }
        _keyListening = false; _err = ""
        _recheck()
        keyBtn.forceActiveFocus()
    }
}
