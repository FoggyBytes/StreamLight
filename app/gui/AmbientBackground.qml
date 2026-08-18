import Theme 1.0
import QtQuick 2.15

/*
 * The app's floor: charcoal at the top, washed with the accent towards the bottom.
 *
 * Extracted from AppShell so the screens that cover it whole — quitting, the PIN pad — stand
 * on the same ground instead of on a flat rectangle. The wash is derived rather than fixed:
 * a hardcoded green would have made the accent a lie the moment the user chose anything else.
 *
 * ⚠️ Drawn through DitheredGradient and not as a Rectangle gradient, and that is not
 * belt-and-braces. This is the worst banding case in the app: the green channel travels
 * 21 → 34 across seventy percent of the window, so a plain 8-bit ramp lays down one hard
 * edge every fifty pixels, in the dark end where they show most. Reported as "quelle bande
 * veramente brutte" and it was exactly that.
 */
Item {
    anchors.fill: parent

    DitheredGradient {
        anchors.fill: parent
        orientation: Qt.Vertical
        stops: [
            { pos: 0.0, color: "#151515" },
            { pos: 0.7, color: Qt.tint("#151515", Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                          Theme.accent.b, 0.07)) },
            { pos: 1.0, color: Qt.tint("#151515", Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                          Theme.accent.b, 0.20)) }
        ]
    }
}
