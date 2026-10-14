import QtQuick 2.15
import QtQuick.Effects
import Theme 1.0

/*
 * The blurred box art behind a screen, under the veil that keeps text readable on it.
 *
 * Extracted from AppsScreen so that the host page, the launch screen and the quit screen
 * stood on the same artwork instead of on three drawings that merely resembled each other.
 * Since 7.0.0 the host page stands on the app's floor and waves by default (09/10/2026); the
 * launch and quit screens — dark in both themes — always use this, and the host page does
 * again when Settings → Interface → Host page background is "Game cover" (13/10/2026). For
 * that page in Light there is the "lifted" variant: the cover lightened toward the floor
 * instead of veiled in near-black, so the theme's dark text reads on it (§89.9, §89.25).
 * The launch screen used to build a gradient out of two colours sampled from the cover,
 * which is a different picture of the same game: crossing from one screen to the next the
 * background changed under you at the exact moment nothing else should move.
 *
 * ⚠️ Draws nothing at all when there is no artwork, or when the user has asked for reduced
 * animations. That is not a fallback bolted on: it is how the caller gets the app's standard
 * gradient back, because whatever sits behind this item shows through untouched.
 */
Item {
    id: root
    anchors.fill: parent

    // The artwork to stand on. Empty means this item is inert.
    property url source: ""

    // How far the artwork reaches past the edges. The blur needs material to sample out
    // there, or it smears the border of the picture along the sides of the screen.
    property real overscan: 60

    property real artOpacity: 0.5

    // Light theme on the host page: MultiEffect contrast −0.65, brightness +0.22, saturation
    // +0.4 (it computes (in − 0.5)·(1 + contrast) + 0.5 + brightness, so covers land at
    // 0.55–0.90), drawn at 80 % (the host page sets artOpacity) under a thin veil of the floor.
    // Darkened twice on 13/10/2026 at Marcello's request («si vede appena», «ancora un po' più
    // scura»): it was −0.8 / +0.28 (0.68–0.88) at 50 % under a veil of 30/10/40 %.
    // ⚠️ At this depth the theme's text2 drops to ~3.1:1 on the darkest spots, so the host page
    // draws its grey text in a darker grey over this (AppsScreen._text2/_text3, 4.6:1 at worst).
    // Darken it further and that grey has to follow. Off — 6.5.1's dark veil — everywhere
    // else, the launch and quit screens included.
    property bool lifted: false

    // Something is on screen (artwork, not reduced animations), and the colour this ends in at
    // its foot — for the status bar under it, which must not show a step.
    readonly property bool drawing: ambientLayer.visible
    readonly property color footColor: lifted ? Theme.floor : "#05080a"

    // Clipped here rather than relying on an ancestor to do it, so the overscan cannot
    // spill onto a screen that happens not to clip.
    clip: true

    property url _current: ""
    property url _pending: ""

    /*
     * Crossfade, because on the host page this follows the focused game and a hard cut on
     * every press of the D-pad reads worse than no artwork at all. The swap happens at the
     * bottom of the fade, not at the top.
     */
    onSourceChanged: {
        if (Theme.reduceAnimations || _current.toString() === "") { _current = source; return }
        if (source === _current) {
            // ⚠️ Not a plain return while a swap is running. The source can dip away and come
            // straight back — the host page's list is rebuilt when a tab is picked, so for an
            // instant nothing is focused — and returning here left the swap in flight still
            // carrying the empty picture it was started for: it faded out, installed nothing,
            // and the page stood on no artwork until the focus moved. Seen on opening a host
            // in 5.9.0, with Last played in the spotlight.
            if (ambientSwap.running) _pending = source
            return
        }
        _pending = source
        ambientSwap.restart()
    }

    SequentialAnimation {
        id: ambientSwap
        NumberAnimation { target: ambientLayer; property: "opacity"; to: 0; duration: 140 }
        ScriptAction { script: root._current = root._pending }
        NumberAnimation { target: ambientLayer; property: "opacity"; to: root.artOpacity; duration: 260 }
        // The animation wrote a number over the binding: put it back, so a change of theme
        // (artOpacity is 0.7 in Light on the host page, 0.5 otherwise) still reaches the layer.
        ScriptAction { script: ambientLayer.opacity = Qt.binding(function() { return root.artOpacity }) }
    }

    Item {
        id: ambientLayer
        anchors.fill: parent
        anchors.margins: -root.overscan
        opacity: root.artOpacity
        visible: !Theme.reduceAnimations && root._current.toString() !== ""

        Image {
            id: ambientImage
            anchors.fill: parent
            source: root._current
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            // ⚠️ Must match the other Images showing the same cover (HeroCover, the library
            // list): Qt shares one texture per picture, and a mismatch makes it log "Mipmap
            // settings changed without having image data available" and keep the old setting.
            mipmap: true
            // Hidden because MultiEffect draws it: the Image is only here as a texture
            // provider. Leaving it visible would paint the sharp copy under the blurred one.
            visible: false
        }

        MultiEffect {
            anchors.fill: parent
            source: ambientImage
            blurEnabled: true
            blur: 1.0
            blurMax: 48
            saturation: root.lifted ? 0.4 : 0.25
            contrast: root.lifted ? -0.65 : 0.0
            brightness: root.lifted ? 0.22 : 0.0
            autoPaddingEnabled: false
        }
    }

    /*
     * The veil. Without it the text sits on whatever the artwork happens to be, and loses.
     *
     * ⚠️ DitheredGradient and not a plain Rectangle gradient, and here it matters more than
     * anywhere else in the app. This veil is 81% opaque at its lightest, so the picture under
     * it arrives with its levels squeezed to a fifth — and the picture is BLURRED, which is
     * the smoothest surface there is and therefore the one with nothing of its own to hide the
     * steps. A Rectangle gradient carries no dither at all, so the banding it produced was the
     * app's worst even though this was the last place still using one.
     */
    DitheredGradient {
        anchors.fill: parent
        visible: ambientLayer.visible
        orientation: Qt.Vertical
        // Fully opaque by the bottom edge, not 93%. The artwork is clipped to the content
        // area while the page background continues behind the status bar, so any residual
        // transparency here left the blurred image ending on a hard horizontal line — read
        // on screen as a solid band across the foot of the page.
        // Lifted: the floor at 15 / 0 / 25 / 100 % — the lightened cover needs little veil, and
        // the foot still ends in the floor, where the status bar starts.
        stops: root.lifted
               ? [ { pos: 0.0,  color: Qt.rgba(Theme.floor.r, Theme.floor.g, Theme.floor.b, 0.15) },
                   { pos: 0.44, color: Qt.rgba(Theme.floor.r, Theme.floor.g, Theme.floor.b, 0.0) },
                   { pos: 0.88, color: Qt.rgba(Theme.floor.r, Theme.floor.g, Theme.floor.b, 0.25) },
                   { pos: 1.0,  color: Theme.floor } ]
               : [ { pos: 0.0,  color: "#d005080a" },
                   { pos: 0.44, color: "#bb05080a" },
                   { pos: 0.88, color: "#f205080a" },
                   { pos: 1.0,  color: "#ff05080a" } ]
    }
}
