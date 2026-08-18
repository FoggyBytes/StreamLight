import QtQuick 2.15

/*
 * One shoulder button beside the thing it moves: the glyph, a hit area big enough for a
 * mouse, and a hover lift.
 *
 * It exists because the pair is drawn twice on the same row — once either side of the
 * profile badge — and a hover state duplicated is a hover state that drifts. Call sites say
 * which button it is and what it does; everything else is here.
 *
 * ⚠️ It draws an ActionHint, so it follows the device in hand and the controller vendor by
 * itself. Never hard-code "LB" as text at a call site: see the note in ActionHint.
 */
Item {
    id: shoulder

    property string buttonKey: ""    // "LB" / "RB"
    property string keyLabel:  ""    // keyboard equivalent shown once the keyboard is touched
    property int    size:      26

    signal triggered()

    // The hit area is deliberately larger than the glyph — a shoulder pill is small, and this
    // is a mouse target as much as a legend.
    implicitWidth:  Math.round(size * 40 / 26)
    implicitHeight: Math.round(size * 28 / 26)

    ActionHint {
        id: glyph
        anchors.centerIn: parent
        buttonKey: shoulder.buttonKey
        keyLabel:  shoulder.keyLabel
        size:      shoulder.size
        opacity:   mouse.containsMouse ? 1.0 : 0.85
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: shoulder.triggered()
    }
}
