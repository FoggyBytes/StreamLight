// ⚠️ Unversioned on purpose, like Spinner and HeroCover: FrameAnimation arrived in Qt 6.4,
// and a versioned `import QtQuick 2.15` can hide types added after that revision. The build
// would not notice — qmlcachegen falls back to the interpreter — and the whole floor would
// fail to load at runtime.
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Window

import Theme 1.0
import WindowMove 1.0

/*
 * The moving layer of the app's floor (6.0.0): waves rising from the bottom of the window to
 * about three quarters of its height.
 *
 * It went wave → packet flow → wave. The packet flow was chosen because "a wave oscillates in
 * place, and a stream arrives"; Marcello looked at both on the real app and asked for the
 * waves back (18/09/2026). Two things are kept from the round trip: the waves travel — left
 * to right, like the packets did — and they come up from the bottom when the app opens.
 *
 * Five layers, and one number per layer, `depth` (0 = the far one, highest on screen; 1 = the
 * near one, lowest):
 *
 *   · far layers are short, low and slow, near ones long, tall and faster — perspective;
 *   · each layer is a flat, faint fill plus a slightly brighter crest line. NOT shaded inside:
 *     that was tried in the first study and turned the layers into one blur;
 *   · the fills overlap, so the tint thickens towards the bottom, where AmbientBackground's
 *     accent wash already is. The top quarter — behind the host name and the clock — has none.
 *
 * ⚠️ Each wave's path is built ONCE per size and only TRANSLATED afterwards: moving a Shape
 * item costs a transform, rebuilding its path costs a triangulation. The path is one
 * wavelength wider than the window, so shifting it by up to a wavelength never uncovers an
 * edge — and both harmonics repeat every wavelength, so the wrap is seamless.
 *
 * `speed` is 1 normally, 2 while a host is streaming, eased over 1.2 s. ⚠️ Driven by one
 * FrameAnimation advancing a shared clock, NOT by a looping NumberAnimation per layer: a
 * running animation does not pick up a new duration until it restarts, and a restart sends
 * every layer back to its start — the picture would jump the moment a session began.
 *
 * ⚠️ It stops whenever nobody can see it (window hidden — main.qml hides it for the whole of
 * a stream —, app in the background, a dialog in front, reduceAnimations) AND while the
 * window is being dragged or resized: see WindowMove.
 */
Item {
    id: waves

    /** 1 = the normal drift, 2 = a session is live somewhere. */
    property real speed: 1.0

    /**
     * How far the waves have risen: 0 = every layer below the bottom edge, 1 = in place.
     * 1 unless someone drives it — only the opening animation does (StartupSplash.rise, through
     * AmbientBackground), so that the rise, the icon and the sound run on ONE clock.
     *
     * ⚠️ It used to be an animation of its own, started when this was created. On the Ally the
     * sound then came in late and ended early against the picture: creation is not when the
     * first frame reaches the screen, and three things timed from three different starts drift.
     */
    property real rise: 1.0

    readonly property int _layers: 5
    // Designed at 1280 wide, like everything else, and grown with the window.
    readonly property real _u: Theme.uiScale

    clip: true

    property real _speed: speed
    Behavior on _speed {
        enabled: !Theme.reduceAnimations
        NumberAnimation { duration: 1200; easing.type: Easing.InOutSine }
    }

    // Flow time, in milliseconds of travel at speed 1.
    property real _t: 0

    readonly property real _rise: Math.max(0, Math.min(1, rise))

    /*
     * A dialog is in front. Every modal in the app takes the focus when it opens, and a
     * popup's items live under the window's Overlay — so the focus sitting anywhere below
     * Overlay.overlay means a popup is up. The focus and not a count of popups, because a
     * tooltip never takes the focus and no dialog should have to report itself.
     */
    property Item _af: Window.activeFocusItem
    readonly property bool _dialogInFront: {
        for (var p = _af; p; p = p.parent)
            if (p === Overlay.overlay) return true
        return false
    }

    /*
     * The window is being dragged or resized (18/09/2026, reported by Marcello: dragging the
     * windowed app stuttered, and only with the animation on).
     *
     * StreamLight runs Qt Quick on the "basic" render loop (main.cpp — upstream requires it,
     * the stream blocks the GUI thread on purpose), so every frame of this animation is drawn
     * and presented ON the GUI thread. While a window is dragged, that thread is inside
     * Windows' own move loop, and a frame that waits for the vblank there holds the move up.
     * Nothing about the waves can make that cheap enough: the answer is not to draw them
     * while the window moves.
     *
     * ⚠️ Read from WindowMove, which listens for the start and end of Windows' move loop. A
     * first version guessed it in QML from the window's x/y and a 300 ms timer, and was only
     * half a cure: it saw the drag only once the window had already moved, and resumed while
     * the button was still held with the window standing still. _t is kept, so the waves
     * resume where they were.
     */
    FrameAnimation {
        running: waves.visible && !Theme.reduceAnimations && !waves._dialogInFront
                 && !WindowMove.moving
                 && Qt.application.state === Qt.ApplicationActive
        // Clamped: the first frame after a pause reports the whole time it was away, and
        // every layer would leap.
        onTriggered: waves._t += Math.min(frameTime, 0.1) * 1000 * waves._speed
    }

    Repeater {
        model: waves._layers

        delegate: Shape {
            id: wave

            // Far to near, in painting order: the near waves are drawn over the far ones.
            readonly property real depth: index / (waves._layers - 1)

            // Where the crests sit on average, from the top: the far wave at a quarter of the
            // height — so the whole set reaches three quarters up — the near one at 0.78.
            readonly property real level: waves.height * (0.25 + 0.53 * depth)
            readonly property real amp: (12 + 22 * depth) * waves._u
            readonly property real wavelength: (720 + 780 * depth) * waves._u
            // Milliseconds to travel one wavelength at speed 1.
            readonly property real travel: 34000 - 18000 * depth
            // The swell: a slow rise and fall on top of the travel, each layer out of step.
            readonly property real swellAmp: (4 + 6 * depth) * waves._u
            readonly property real swellPeriod: 11000 + 2300 * index
            readonly property real phase: index * 1.7

            readonly property real fillAlpha: 0.022 + 0.022 * depth
            readonly property real crestAlpha: 0.07 + 0.13 * depth

            // Enough below the bottom edge that neither the swell nor the rise can lift the
            // fill's bottom into view.
            readonly property real _below: amp + swellAmp + 8 * waves._u

            width: waves.width + wavelength
            height: waves.height - level + amp + _below

            x: -wavelength + wavelength * ((waves._t / travel + index * 0.37) % 1)
            y: level - amp
               + swellAmp * Math.sin(waves._t / swellPeriod * 2 * Math.PI + phase)
               // The rise: the far layers come up first and the near ones follow, so the
               // set unfolds from the back instead of lifting as one sheet.
               + (waves.height - level + amp + swellAmp) * (1 - Math.min(1, Math.max(0,
                     waves._rise * 1.6 - depth * 0.6)))

            antialiasing: true
            preferredRendererType: Shape.CurveRenderer

            /*
             * The crest, in local coordinates: y = amp is the mean level. Two harmonics, both
             * repeating every wavelength, so the line is never a plain sine and the wrap is
             * seamless. Rebuilt only when the size or the scale changes.
             */
            readonly property var crest: {
                var pts = []
                var step = Math.max(6, 10 * waves._u)
                var n = Math.ceil(width / step)
                for (var i = 0; i <= n; ++i) {
                    var px = i * step
                    var a = px / wavelength * 2 * Math.PI
                    pts.push(Qt.point(px, amp * (1 - 0.72 * Math.sin(a + phase)
                                                   - 0.28 * Math.sin(2 * a + phase * 2.3))))
                }
                return pts
            }
            readonly property var body: {
                var pts = crest.slice()
                pts.push(Qt.point(width, height))
                pts.push(Qt.point(0, height))
                pts.push(crest[0])
                return pts
            }

            ShapePath {
                strokeWidth: -1
                strokeColor: "transparent"
                fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, wave.fillAlpha)
                PathPolyline { path: wave.body }
            }
            ShapePath {
                strokeWidth: Math.max(1, 1.5 * waves._u)
                strokeColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, wave.crestAlpha)
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin
                PathPolyline { path: wave.crest }
            }
        }
    }
}
