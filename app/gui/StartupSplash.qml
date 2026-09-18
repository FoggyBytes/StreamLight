// ⚠️ Unversioned on purpose, like AmbientWaves: QtQuick.Effects and a versioned
// `import QtQuick 2.15` do not mix safely, and qmlcachegen would not say so at build time.
import QtQuick
import QtQuick.Effects
import QtQuick.Window

import Theme 1.0

/*
 * The opening animation (6.0.0): "B · Scia di luce", chosen by Marcello from a mockup of three
 * on 18/09/2026.
 *
 * It is NOT a screen of its own. The waves are AmbientBackground's — the same floor Home stands
 * on, so when Home fades in nothing under it restarts — and this item drives how far they have
 * risen (`rise`). It adds what sits above the waves — the icon, a ring, the wordmark — and
 * publishes `homeOpacity`, which AppShell puts on the page, the clock and the status bar.
 *
 * One clock, `t`, in seconds, and everything is a binding on it (as in the mockup). Timeline:
 *
 *   0.0 – 1.6   the waves rise (OutCubic) — for a moment they are all there is
 *   0.5 – 1.3   the icon fades in, growing from 94%
 *   0.9 – 1.8   one ring leaves the icon, like a wave
 *   1.2 – 2.3   a band of light sweeps STREAMLIGHT left to right: a letter appears as the band
 *               reaches it and glows while the band is on it
 *   2.3 – 2.5   still
 *   2.5 – 2.9   icon and wordmark fade out, Home fades in
 *
 * ⚠️ WHEN t starts, and why it is not "when this is created" (18/09/2026, on the Ally, while the
 * animation still had a sound: the sound came in late and ended before the picture did). The
 * waves' rise and this timeline used to start at creation, each on its own clock, and creation
 * is not when anything reaches the screen: Home is still being built, the first frame comes
 * later, and on a handheld noticeably later. Now:
 *
 *   1. start() hides Home and sinks the waves, and waits for the window's FIRST FRAME
 *      (frameSwapped; a fallback timer in case none ever comes);
 *   2. then t starts, and it is WALL-CLOCK time, read each frame, not an animation's own
 *      count: a slow frame makes the picture skip ahead instead of stretching it.
 *
 * The sound is out for now (Marcello, 18/09/2026: he wants to work on it separately). How it
 * was wired, and the entry/exit cushions it needed, are in docs/notes §74.16–§74.17 — putting
 * it back means a PlaySound at step 2 and a lead-in before t starts.
 *
 * Any key, pad button or click before the fade jumps to it. Input to Home is held off until
 * then (`blocking`): the page is invisible, and A would otherwise open a host nobody can see.
 *
 * ⚠️ AppShell decides ONCE, at launch, whether this plays (Theme.startupAnimation, not under
 * reduceAnimations, not for a command-line launch). Flipping the setting later changes the
 * next start, never the running app.
 */
FocusScope {
    id: splash

    signal finished()

    readonly property real total: 2.9
    readonly property real fadeAt: 2.5

    property bool running: false
    property real t: 0

    /** Home must not take input yet. */
    readonly property bool blocking: running && t < fadeAt
    /** 0 while the splash owns the screen, 1 once Home does. */
    readonly property real homeOpacity: running ? _inOut(_clamp((t - fadeAt) / (total - fadeAt))) : 1
    /** How far the waves have risen — AppShell hands it to AmbientBackground. 1 when not running. */
    readonly property real rise: running ? _outCubic(_clamp(t / 1.6)) : 1

    // Wall-clock origin of t, in ms (Date.now()). 0 until the picture starts moving.
    property real _t0: 0
    property bool _waitingFrame: false
    property real _startedAt: 0

    readonly property real _u: Theme.uiScale
    readonly property real _iconSize: Math.round(175 * _u)
    readonly property real _fontPx: Math.round(42 * _u)
    readonly property string _word: "STREAMLIGHT"

    // The accent, half way to white: what a letter shows at the peak of its glow.
    readonly property color _hot: Qt.rgba(Theme.accent.r + (1 - Theme.accent.r) * 0.55,
                                          Theme.accent.g + (1 - Theme.accent.g) * 0.55,
                                          Theme.accent.b + (1 - Theme.accent.b) * 0.55, 1)

    // Phases, as in the mockup.
    readonly property real _pIcon: _clamp((t - 0.5) / 0.8)
    readonly property real _pRing: _clamp((t - 0.9) / 0.9)
    // Where the band is, across the word: 0 = left edge, 1 = right edge. Starts a little
    // before the word and ends a little after it, so the first and last letters get the whole
    // band like the others.
    readonly property real _band: (t - 1.2) / 1.1 * 1.3 - 0.15

    visible: running

    function _clamp(x) { return Math.max(0, Math.min(1, x)) }
    function _inOut(x) { return x < 0.5 ? 2 * x * x : 1 - Math.pow(-2 * x + 2, 2) / 2 }
    function _outCubic(x) { return 1 - Math.pow(1 - x, 3) }

    function start() {
        running = true
        t = 0
        _t0 = 0
        _startedAt = Date.now()
        _waitingFrame = true
        firstFrameFallback.start()
        forceActiveFocus()
    }

    // Step 2: the window has something on screen — the picture starts now.
    function _onFirstFrame(how) {
        if (!_waitingFrame) return
        _waitingFrame = false
        firstFrameFallback.stop()
        console.info("[splash] " + how + " " + (Date.now() - _startedAt) + " ms after start")
        _t0 = Date.now()
    }

    function _finish() {
        console.info("[splash] done " + (Date.now() - _startedAt) + " ms after start")
        running = false
        finished()
    }

    function skip() {
        if (!running || t >= fadeAt) return
        // Also before the picture has even started: stop waiting for the first frame.
        _waitingFrame = false
        firstFrameFallback.stop()
        // Straight to the fade, not to the end: Home still arrives the way it always does.
        _t0 = Date.now() - fadeAt * 1000
        t = fadeAt
    }

    Connections {
        target: splash.Window.window
        enabled: splash._waitingFrame
        function onFrameSwapped() { splash._onFirstFrame("first frame") }
    }
    // Belt and braces: a window that never renders (started hidden, or behind another app)
    // must not leave Home hidden behind a splash waiting for a frame that is not coming.
    Timer {
        id: firstFrameFallback
        interval: 1500
        onTriggered: splash._onFirstFrame("no frame yet, going anyway")
    }
    FrameAnimation {
        running: splash.running && splash._t0 > 0
        onTriggered: {
            splash.t = Math.min(splash.total, (Date.now() - splash._t0) / 1000)
            if (splash.t >= splash.total) splash._finish()
        }
    }

    Keys.onPressed: function(event) {
        splash.skip()
        event.accepted = true
    }

    MouseArea {
        anchors.fill: parent
        enabled: splash.blocking
        onClicked: splash.skip()
    }

    Column {
        id: brand
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(parent.height * 0.44 - height / 2)
        spacing: Math.round(27 * splash._u)
        opacity: 1 - splash.homeOpacity

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: splash._iconSize
            height: splash._iconSize

            // The ring, behind the icon. One, once.
            Rectangle {
                anchors.centerIn: parent
                width: splash._iconSize * (1.02 + 1.08 * splash._outCubic(splash._pRing))
                height: width
                radius: width / 2
                color: "transparent"
                border.color: Theme.accent
                border.width: Math.max(1, (1 + 2 * (1 - splash._pRing)) * splash._u)
                opacity: (1 - splash._pRing) * 0.8
                visible: splash._pRing > 0 && splash._pRing < 1
            }

            Image {
                anchors.fill: parent
                source: "qrc:/streamlight.ico"
                // Rasterised at device-pixel size, like the brand icon on Home.
                sourceSize.width:  splash._iconSize * Screen.devicePixelRatio
                sourceSize.height: splash._iconSize * Screen.devicePixelRatio
                fillMode: Image.PreserveAspectFit
                smooth: true
                opacity: splash._inOut(splash._pIcon)
                scale: 0.94 + 0.06 * splash._outCubic(splash._pIcon)
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            // Wide tracking, as in the mockup (0.32 em). A Row's spacing sits only BETWEEN
            // letters, so the word stays centred without the trailing gap letter-spacing adds.
            spacing: Math.round(splash._fontPx * 0.32)

            Repeater {
                model: splash._word.length

                delegate: Item {
                    id: letter

                    // The letter's centre, across the word.
                    readonly property real _c: (index + 0.5) / splash._word.length
                    readonly property real _d: splash._band - _c
                    // 0 → 1 as the band arrives; the band's own width is 0.06 of the word.
                    readonly property real _shown: splash._clamp(_d / 0.06 + 1)
                    // Brightest with the band on the letter, gone once it has passed.
                    readonly property real _glow: Math.exp(-(_d * _d) / (2 * 0.055 * 0.055))

                    width: glyph.implicitWidth
                    height: glyph.implicitHeight
                    opacity: _shown

                    // The glow: a copy of the letter in the accent, blurred, under the letter.
                    // A blur and not a MultiEffect shadow on purpose — a shadow draws its source
                    // too (see HeroCover), and all a slip of the padded rect could move here
                    // is a soft halo.
                    Text {
                        id: glowSource
                        text: splash._word.charAt(index)
                        font.family: Theme.family
                        font.pixelSize: splash._fontPx
                        font.bold: true
                        color: Theme.accent
                        visible: false
                    }
                    MultiEffect {
                        anchors.fill: glowSource
                        source: glowSource
                        blurEnabled: true
                        blur: 1.0
                        blurMax: 32
                        brightness: 0.2
                        opacity: letter._glow
                        // Off when there is nothing to show: eleven blurs for the whole of the
                        // splash would be GPU spent on zeros.
                        visible: letter._glow > 0.02
                    }

                    Text {
                        id: glyph
                        text: splash._word.charAt(index)
                        font.family: Theme.family
                        font.pixelSize: splash._fontPx
                        font.bold: true
                        // White-hot while it glows, then the normal text colour.
                        color: Qt.tint(Theme.text, Qt.rgba(splash._hot.r, splash._hot.g,
                                                           splash._hot.b, letter._glow))
                    }
                }
            }
        }
    }
}
