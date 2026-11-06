import QtQuick 2.15

import Theme 1.0

/*
 * The body of a badge or a button on the host page. Put it as the first child of the badge or
 * button, filling it, with the parent's own colour and border left transparent.
 *
 * ⚠️ It wears exactly what the badges and buttons on Home wear (HostStage, as in 6.5.1): a wash
 * of the reading colour under a hairline, so the two screens match in either theme (14/10/2026,
 * Marcello). A badge is 7% under a 26% hairline; a button (`button: true`) is 8% under
 * Theme.lineHigh, like the card's buttons and Play again.
 *
 * History: from 09/10 to 14/10 this was a solid neutral plate (#2b3640 / #f6f8fa at 50%). Home
 * went back to 6.5.1's wash on 13/10 (§89.20) and this file was left behind, which is how the
 * host page came to look different.
 *
 * A lit control passes `tint` (Theme.accentFill, Theme.danger); a button under the pointer
 * passes `hovered`.
 *
 * ⚠️ Glass when lit too (05/11/2026, Marcello, variant A of docs/mockups/pulsanti): `tint` is
 * the wash TINTED by that colour (22%) under a 2 px border of it, never a solid fill, and the
 * pointer brightens the wash to 15% instead of drawing the ink outline it used to.
 */
Rectangle {
    id: plate

    // A lit control's colour; transparent = the wash.
    property color tint: "transparent"
    // An outline of the caller's own; transparent = ours.
    property color outline: "transparent"
    property int outlineWidth: 1
    // A button rather than a badge: Home draws the two with slightly different washes.
    property bool button: false
    // A button under the pointer: a brighter wash, as on the host card.
    property bool hovered: false

    readonly property bool _tinted: tint.a > 0
    color: _tinted ? Qt.rgba(tint.r, tint.g, tint.b, 0.22)
         : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b,
                   button ? (hovered ? 0.15 : 0.08) : 0.07)
    border.width: _tinted && outline.a === 0 ? 2 : outlineWidth
    Behavior on color {
        enabled: !Theme.reduceAnimations
        ColorAnimation { duration: 140; easing.type: Easing.OutCubic }
    }
    border.color: outline.a > 0 ? outline
                : _tinted ? tint
                : button  ? Theme.lineHigh
                : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.26)
}
