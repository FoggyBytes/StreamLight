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
 *
 * 6.0.0: plus the waves above the wash — see AmbientWaves. The dither stays underneath
 * it and is still what keeps the ramp clean; the mockup that preceded this showed exactly
 * what the floor looks like without it.
 */
Item {
    id: ambient
    anchors.fill: parent

    /**
     * A host is streaming right now. The waves run twice as fast while it is. Only AppShell
     * sets it; the quit and launch screens leave it false and get the normal drift.
     */
    property bool streaming: false

    /** How far the waves have risen, 0..1 — see AmbientWaves.rise. Only AppShell drives it,
     *  from the opening animation. The quit and launch screens stand on a floor in place. */
    property real rise: 1.0

    /**
     * Draw the waves at all (6.1.0). They belong to Home, Settings and the PIN pad only; every
     * other screen standing on this floor — the host page, the launch and quit screens —
     * keeps the wash and nothing on it. Hidden, the layer's FrameAnimation stops too.
     */
    property bool waves: true

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

    AmbientWaves {
        anchors.fill: parent
        visible: ambient.waves
        speed: ambient.streaming ? 2.0 : 1.0
        rise: ambient.rise
    }
}
