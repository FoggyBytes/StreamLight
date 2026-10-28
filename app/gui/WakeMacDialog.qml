import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

/*
 * Wake-on-LAN: a second MAC for the magic packet (8.1.0, issue #45). Option A of the mockup,
 * chosen by Marcello on 28/10/2026: a tile in the host's Options opens this.
 *
 * The host reports the MAC of the card it answers on — the one it streams on — and on a PC with
 * two cards that may not be the one that wakes it. The MAC typed here is sent IN ADDITION,
 * never instead (NvComputer::wakeMacAddress): a wrong one cannot break a wake that already works.
 *
 * Built like the rename dialog in HomeScreen, in a file of its own because HomeScreen imports
 * Controls 2.2, which has neither placeholderTextColor nor Dialog.standardButton(). The pad types
 * with the hex keys of the on-screen keyboard; separators are optional.
 *
 * The owner sets `model` (the ComputerModel) and `pcIndex` before open(), and handles onClosed
 * for the focus: a handler there replaces one here, so this file keeps none.
 */
NavigableDialog {
    id: dlg

    property var model: null
    property int pcIndex: -1
    property string pcName: ""
    property string _reported: ""

    standardButtons: Dialog.Ok | Dialog.Cancel

    // Same rule as ComputerModel::setHostWakeMac, so OK is never offered for a value it would
    // refuse: empty (= remove), or twelve hex digits, not all zero, not a group address.
    readonly property string _digits: macField.text.replace(/[:\-. ]/g, "")
    readonly property bool _valid: _digits.length === 0
                                   || (/^[0-9A-Fa-f]{12}$/.test(_digits)
                                       && !/^0{12}$/.test(_digits)
                                       && (parseInt(_digits.substring(0, 2), 16) & 1) === 0)

    onAboutToShow: {
        var macs = model && pcIndex >= 0 ? model.hostWakeMacs(pcIndex) : ({})
        _reported = macs.reported || ""
        macField.text = macs.extra || ""
    }
    onOpened: {
        var ok = standardButton(Dialog.Ok)
        if (ok) ok.enabled = Qt.binding(function() { return dlg._valid })
        macField.forceActiveFocus()
        macField.selectAll()
    }
    onAccepted: {
        if (model && pcIndex >= 0 && _valid) model.setHostWakeMac(pcIndex, macField.text)
    }

    ColumnLayout {
        spacing: dlg._px(18)

        Label {
            text: qsTr("WAKE-ON-LAN")
            font.family: Theme.family
            font.pixelSize: dlg._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: 1.6 * dlg._u
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: dlg._reported.length ? qsTr("%1 reports %2").arg(dlg.pcName).arg(dlg._reported)
                                       : qsTr("%1 has not reported a MAC address").arg(dlg.pcName)
            font.family: Theme.family
            font.pixelSize: dlg._px(Theme.fontBody)
            color: Theme.text2
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: dlg._px(520)
        }

        Label {
            text: qsTr("Also wake with")
            font.family: Theme.family
            font.pixelSize: dlg._px(Theme.fontTitle)
            color: Theme.text
            Layout.alignment: Qt.AlignHCenter
        }

        TextField {
            id: macField
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: dlg._px(360)
            implicitHeight: dlg._px(48)
            maximumLength: 17
            // No placeholder: Material floats it up to the border as the field takes the focus,
            // which here is at once (28/10). The example lives in the line under the field.
            color: Theme.text
            selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
            selectedTextColor: Theme.text
            font.family: Theme.monoFamily
            font.pixelSize: dlg._px(Theme.fontTitle)
            font.bold: true
            horizontalAlignment: TextInput.AlignHCenter
            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhUppercaseOnly
            focus: true

            background: Rectangle {
                color: Theme.ground
                radius: dlg._px(8)
                border.color: !dlg._valid ? Theme.danger : macField.activeFocus ? Theme.ink : Theme.line
                border.width: macField.activeFocus || !dlg._valid ? 2 : 1
            }

            // The pad's A opens the on-screen keyboard; Enter on a keyboard saves.
            Keys.onReturnPressed: function(event) { if (!macKeyboard.offer(event) && dlg._valid) dlg.accept() }
            Keys.onEnterPressed:  function(event) { if (!macKeyboard.offer(event) && dlg._valid) dlg.accept() }
            Keys.onSpacePressed:  function(event) { event.accepted = macKeyboard.offer(event) }
        }

        Label {
            text: dlg._valid ? qsTr("Another network card of this PC, like D8:BB:C1:52:0E:9F. Empty = none.")
                             : qsTr("Twelve hex digits, like D8:BB:C1:52:0E:9F")
            font.family: Theme.family
            font.pixelSize: dlg._px(Theme.fontSmall)
            color: dlg._valid ? Theme.text2 : Theme.danger
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: dlg._px(520)
        }
    }

    // Hex keys: digits and A-F are all a MAC needs.
    VirtualKeyboard {
        id: macKeyboard
        target: macField
        mode: "hex"
        onAccepted: { if (dlg._valid) { close(); dlg.accept() } }
    }
}
