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
 * A lit control passes `tint` (Theme.accentFill, Theme.danger).
 */
Rectangle {
    id: plate

    // A lit control's colour; transparent = the wash.
    property color tint: "transparent"
    // An outline of the caller's own (the pointer's ink one); transparent = ours.
    property color outline: "transparent"
    property int outlineWidth: 1
    // A button rather than a badge: Home draws the two with slightly different washes.
    property bool button: false

    readonly property bool _tinted: tint.a > 0
    color: _tinted ? tint : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, button ? 0.08 : 0.07)
    border.width: outlineWidth
    border.color: outline.a > 0 ? outline
                : _tinted ? tint
                : button  ? Theme.lineHigh
                : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.26)
}
