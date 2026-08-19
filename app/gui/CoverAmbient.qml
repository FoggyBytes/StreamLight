import QtQuick 2.15
import QtQuick.Effects
import Theme 1.0

/*
 * The blurred box art behind a screen, under the veil that keeps text readable on it.
 *
 * Extracted from AppsScreen so that the host page, the launch screen and the quit screen
 * stand on the same artwork instead of on three drawings that merely resemble each other.
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

    // Whether anything is actually being drawn. A caller needs this to match its own chrome:
    // the host page paints the status bar to the veil's near-black, which is right under the
    // artwork and wrong without it — with reduced animations that left the bar dark against
    // the accent gradient, the same step the floor exists to remove, inverted.
    readonly property alias drawing: ambientLayer.visible

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
        if (source === _current) return
        _pending = source
        ambientSwap.restart()
    }

    SequentialAnimation {
        id: ambientSwap
        NumberAnimation { target: ambientLayer; property: "opacity"; to: 0; duration: 140 }
        ScriptAction { script: root._current = root._pending }
        NumberAnimation { target: ambientLayer; property: "opacity"; to: root.artOpacity; duration: 260 }
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
            saturation: 0.25
            autoPaddingEnabled: false
        }
    }

    // The veil. Without it the text sits on whatever the artwork happens to be, and loses.
    Rectangle {
        anchors.fill: parent
        visible: ambientLayer.visible
        gradient: Gradient {
            // Fully opaque by the bottom edge, not 93%. The artwork is clipped to the content
            // area while the page background continues behind the status bar, so any residual
            // transparency here left the blurred image ending on a hard horizontal line — read
            // on screen as a solid band across the foot of the page.
            GradientStop { position: 0.0;  color: "#d005080a" }
            GradientStop { position: 0.44; color: "#bb05080a" }
            GradientStop { position: 0.88; color: "#f205080a" }
            GradientStop { position: 1.0;  color: "#ff05080a" }
        }
    }
}
