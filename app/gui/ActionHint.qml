import QtQuick 2.15

import Theme 1.0
import InputHints 1.0

// One button prompt, drawn for whichever device the user is actually holding: the vendor
// glyph while a controller is in use, the keyboard key once they touch the keyboard or the
// mouse.
//
// Call sites say what the action IS — the pad button and its keyboard equivalent — never
// which of the two to draw. That decision belongs here and nowhere else, which is the same
// reason PadGlyph resolves the vendor by itself: a prompt that each screen decides for
// itself is a prompt that will eventually disagree with the one next to it.
Item {
    id: hint

    property string buttonKey: ""   // "A","B","X","Y","LB","RB","SELECT","START"
    property string keyLabel:  ""   // keyboard equivalent, e.g. "S", "Esc", "PgDn"
    property int    size:      26   // face-button height, as in the status bar

    // No keyboard equivalent means there is nothing honest to show a keyboard user, so the
    // glyph stays. Better an unreachable prompt than one naming a key that does nothing.
    readonly property bool _pad: InputHints.padActive || keyLabel === ""

    implicitWidth:  _pad ? padGlyph.implicitWidth  : cap.implicitWidth
    implicitHeight: _pad ? padGlyph.implicitHeight : cap.implicitHeight

    PadGlyph {
        id: padGlyph
        visible: hint._pad
        buttonKey: hint.buttonKey
        label: hint.buttonKey
        size: hint.size
    }

    // Same height as a pill-shaped pad glyph so a row of prompts keeps one baseline whichever
    // device is in use, and grows with the text rather than clipping "PgDn".
    Rectangle {
        id: cap
        visible: !hint._pad
        implicitWidth: Math.max(Math.round(hint.size * 24 / 26), capText.implicitWidth + 14)
        implicitHeight: Math.round(hint.size * 24 / 26)
        width: implicitWidth
        height: implicitHeight
        radius: 6
        color: Qt.rgba(1, 1, 1, 0.06)
        border.color: Theme.line
        border.width: 1

        Text {
            id: capText
            anchors.centerIn: parent
            text: hint.keyLabel
            color: Theme.text
            font.family: Theme.family
            font.pixelSize: Math.max(11, Math.round(hint.size * 0.46))
            font.bold: true
        }
    }
}
