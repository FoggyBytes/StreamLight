// ⚠️ Unversioned on purpose, like AmbientWaves: QtQuick.Effects and a versioned
// `import QtQuick 2.15` do not mix safely, and qmlcachegen would not say so at build time.
import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import QtQuick.Window

import Theme 1.0
import InputHints 1.0

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
 *
 * 8.2.0 — the CHOICE (`choice`, a launch by the sign-in task, backend/signinlaunch.h). Mockup B
 * «Sotto il nome», chosen by Marcello on 28/10/2026 (docs/mockups/avvio/). The timeline stops
 * at fadeAt instead of fading into Home: icon and word rise a little, two buttons come up under
 * the word — A Start StreamLight, B Windows desktop — and the ring comes back around the icon
 * as a 10 s countdown towards StreamLight, which the first key or mouse movement stops. A picks
 * StreamLight and the fade into Home runs as usual; B asks AppShell to close the app. With the
 * animation off (setting or Reduce animations) the choice still shows, from its first frame.
 * ⚠️ Home is not loaded and no host is polled until StreamLight is picked — that is AppShell's
 * and main.qml's half (SignInLaunch.holding), and why a pick must emit before `_picked` moves.
 */
FocusScope {
    id: splash

    signal finished()

    readonly property real total: 2.9
    readonly property real fadeAt: 2.5

    property bool running: false
    property real t: 0

    /** 8.2.0: this launch came from the sign-in task — stop on the choice instead of fading. */
    property bool choice: false
    /**
     * 8.2.0: the session is still behind the lock screen (SignInLaunch.waitingForUnlock). The
     * splash has started but its clock has not: nothing moves and nothing counts down until it
     * turns false — the choice must be seen to be made.
     */
    property bool held: false
    property bool _animate: true
    signal streamLightChosen()
    signal desktopChosen()

    readonly property real countdownSecs: 10
    property real cdLeft: countdownSecs
    // Nobody has touched anything yet: the countdown runs.
    property bool _counting: true
    property bool _picked: false
    property bool _leaving: false
    // Wall-clock start of the choice (ms) and the seconds since — the choice has its own clock,
    // because t holds still at fadeAt while it is on screen.
    property real _choiceAt: 0
    property real _ct: 0
    // Where the pointer was when the choice came up, in scene coordinates (-1: not seen yet).
    property real _mouseX: -1
    property real _mouseY: -1

    /** The two buttons are on screen and answer. */
    readonly property bool choosing: running && choice && !_picked && !_leaving && t >= fadeAt
    /** How far the choice has come in, 0 → 1 over 0.7 s, and 1 from then on. */
    readonly property real _ch: !choice ? 0
                                : Theme.reduceAnimations ? (t >= fadeAt ? 1 : 0)
                                : _outCubic(_clamp(_ct / 0.7))

    /** Home must not take input yet. */
    readonly property bool blocking: running && (t < fadeAt || (choice && !_picked))
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
    // 7.0.0, Light: the splash stands on the app's floor, which is pale there, and a glow half
    // way to white would not show on it — so the peak is the ink, the accent darkened to hold.
    readonly property color _hot: Theme.light ? Theme.ink
                                  : Qt.rgba(Theme.accent.r + (1 - Theme.accent.r) * 0.55,
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

    // atSignIn (8.2.0): stop on the choice. animate === false: no animation, the choice from
    // the first frame (a sign-in launch with the animation off).
    function start(atSignIn, animate) {
        _animate = animate !== false
        choice = atSignIn === true
        _picked = false
        _leaving = false
        _counting = true
        cdLeft = countdownSecs
        _choiceAt = 0
        _ct = 0
        _mouseX = -1
        _mouseY = -1
        running = true
        t = 0
        _t0 = 0
        _waitingFrame = false
        forceActiveFocus()
        // Behind the lock screen: the floor only, and the clock starts at the unlock (onHeldChanged).
        if (!held)
            _begin()
    }

    function _begin() {
        _startedAt = Date.now()
        if (!_animate) {
            _waitingFrame = false
            _t0 = Date.now() - fadeAt * 1000
            t = fadeAt
        } else {
            _waitingFrame = true
            firstFrameFallback.start()
        }
    }

    onHeldChanged: {
        if (!held && running && _t0 === 0 && !_waitingFrame)
            _begin()
    }

    // Someone is there: the countdown stops, and the log says what stopped it.
    function _stopCountdown(why) {
        if (!_counting) return
        _counting = false
        console.info("[signin] countdown stopped by " + why + ", " + cdLeft.toFixed(1) + " s left")
    }

    /*
     * The pointer moved. ⚠️ A real move only: when the buttons come up under a pointer that is
     * standing still, Qt still sends a position event to the hover areas that appear beneath it,
     * and the countdown stopped by itself at sign-in (measured 28/10/2026, first build). So the
     * first position seen is only remembered, and it takes a few pixels away from it.
     */
    function _pointerAt(x, y) {
        if (!choosing) return
        if (_mouseX < 0) {
            _mouseX = x
            _mouseY = y
            return
        }
        if (Math.abs(x - _mouseX) + Math.abs(y - _mouseY) > 6)
            _stopCountdown("the mouse")
    }

    function pick(streamLight) {
        if (!choosing) return
        _counting = false
        console.info("[signin] " + (streamLight ? "StreamLight" : "desktop") + " chosen "
                     + _ct.toFixed(1) + " s into the choice")
        if (!streamLight) {
            // AppShell closes the app; the choice stays on screen, deaf, until it does.
            _leaving = true
            desktopChosen()
            return
        }
        // ⚠️ First the signal — AppShell releases the hosts and loads Home, which must exist
        // before blocking turns false — then the fade, as at the end of the plain animation.
        streamLightChosen()
        _picked = true
        _t0 = Date.now() - (Theme.reduceAnimations ? total : fadeAt) * 1000
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
        if (!running || held || t >= fadeAt) return
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
            // Held at fadeAt while the choice is up.
            var cap = (splash.choice && !splash._picked) ? splash.fadeAt : splash.total
            splash.t = Math.min(cap, (Date.now() - splash._t0) / 1000)
            if (splash.choosing) {
                if (splash._choiceAt === 0) splash._choiceAt = Date.now()
                splash._ct = (Date.now() - splash._choiceAt) / 1000
                // From when the buttons are fully in.
                if (splash._counting && splash._ct > 0.7) {
                    splash.cdLeft = Math.max(0, splash.countdownSecs - (splash._ct - 0.7))
                    if (splash.cdLeft <= 0) splash.pick(true)
                }
            }
            if (splash.t >= splash.total) splash._finish()
        }
    }

    Keys.onPressed: function(event) {
        event.accepted = true
        if (splash.choosing) {
            // Any key: someone is there, the countdown stops. A and B also choose — on the pad
            // A is Return (Space in unlock mode), B is Escape.
            var k = event.key
            splash._stopCountdown("key 0x" + k.toString(16))
            if (k === Qt.Key_Return || k === Qt.Key_Enter || k === Qt.Key_Space)
                splash.pick(true)
            else if (k === Qt.Key_Escape || k === Qt.Key_Back)
                splash.pick(false)
            return
        }
        splash.skip()
    }

    MouseArea {
        anchors.fill: parent
        enabled: splash.blocking
        hoverEnabled: splash.choosing
        onClicked: splash.skip()
        onPositionChanged: function(mouse) {
            var p = mapToItem(null, mouse.x, mouse.y)
            splash._pointerAt(p.x, p.y)
        }
    }

    Column {
        id: brand
        anchors.horizontalCenter: parent.horizontalCenter
        // 8.2.0: a little higher with the choice in, to make room for the buttons under it.
        y: Math.round(parent.height * (0.44 - 0.08 * splash._ch) - height / 2)
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
                border.color: Theme.ink
                border.width: Math.max(1, (1 + 2 * (1 - splash._pRing)) * splash._u)
                opacity: (1 - splash._pRing) * 0.8
                visible: splash._pRing > 0 && splash._pRing < 1
            }

            // 8.2.0: the ring again, as the countdown — it closes on the icon in 10 s, and goes
            // when the countdown stops.
            Shape {
                id: countdownRing
                anchors.centerIn: parent
                readonly property real r: splash._iconSize * 0.64
                readonly property real stroke: Math.max(2, 3 * splash._u)
                width: Math.ceil(r * 2 + stroke * 2)
                height: width
                visible: splash.choice && splash._ch > 0
                opacity: splash._ch * (splash._counting ? 1 : 0)
                Behavior on opacity {
                    enabled: !Theme.reduceAnimations
                    NumberAnimation { duration: 300 }
                }
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.line
                    strokeWidth: countdownRing.stroke
                    fillColor: "transparent"
                    PathAngleArc {
                        centerX: countdownRing.width / 2; centerY: countdownRing.height / 2
                        radiusX: countdownRing.r; radiusY: countdownRing.r
                        startAngle: 0; sweepAngle: 360
                    }
                }
                ShapePath {
                    strokeColor: Theme.ink
                    strokeWidth: countdownRing.stroke
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc {
                        centerX: countdownRing.width / 2; centerY: countdownRing.height / 2
                        radiusX: countdownRing.r; radiusY: countdownRing.r
                        startAngle: -90
                        sweepAngle: 360 * splash.cdLeft / splash.countdownSecs
                    }
                }
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
                        color: Theme.ink
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

    // ── 8.2.0: the choice ─────────────────────────────────────────────────────────────────
    // A button with its prompt: the pad glyph while a pad is in use, the key otherwise.
    component ChoiceButton: Rectangle {
        id: btn
        property string buttonKey: "A"
        property string keyLabel: "Enter"
        property string text: ""
        property bool primary: false
        // An inline component cannot see this file's ids: the scale and the state come in.
        property real u: 1
        property bool live: false
        signal activated()
        // Where the pointer is, in scene coordinates — see splash._pointerAt().
        signal touched(real x, real y)

        readonly property color _fg: primary ? Theme.onAccent : Theme.text

        height: Math.round(58 * btn.u)
        width: btnRow.implicitWidth + Math.round(48 * btn.u)
        radius: height / 2
        color: primary ? (btnMouse.containsMouse ? Qt.lighter(Theme.accentFill, 1.12) : Theme.accentFill)
                       : (btnMouse.containsMouse ? Theme.cardHigh : "transparent")
        border.color: primary ? "transparent" : Theme.lineHigh
        border.width: primary ? 0 : Math.max(1, 1.5 * btn.u)

        Row {
            id: btnRow
            anchors.centerIn: parent
            spacing: Math.round(12 * btn.u)

            PadGlyph {
                visible: InputHints.padActive
                anchors.verticalCenter: parent.verticalCenter
                buttonKey: btn.buttonKey
                label: btn.buttonKey
                size: Math.round(26 * btn.u)
            }
            Rectangle {
                visible: !InputHints.padActive
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(height, capText.implicitWidth + Math.round(14 * btn.u))
                height: Math.round(24 * btn.u)
                radius: Math.round(6 * btn.u)
                color: "transparent"
                border.color: Qt.rgba(btn._fg.r, btn._fg.g, btn._fg.b, 0.45)
                border.width: 1
                Text {
                    id: capText
                    anchors.centerIn: parent
                    text: btn.keyLabel
                    color: btn._fg
                    font.family: Theme.family
                    font.pixelSize: Math.max(11, Math.round(12 * btn.u))
                    font.bold: true
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: btn.text
                color: btn._fg
                font.family: Theme.family
                font.pixelSize: Math.round(Theme.fontTitle * btn.u)
                font.weight: btn.primary ? Font.DemiBold : Font.Medium
            }
        }

        MouseArea {
            id: btnMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: btn.live
            onPositionChanged: function(mouse) {
                var p = btnMouse.mapToItem(null, mouse.x, mouse.y)
                btn.touched(p.x, p.y)
            }
            onClicked: btn.activated()
        }
    }

    Column {
        id: choiceBlock
        visible: splash.choice && splash._ch > 0
        anchors.horizontalCenter: parent.horizontalCenter
        y: brand.y + brand.height + Math.round(52 * splash._u) + (1 - splash._ch) * 22 * splash._u
        spacing: Math.round(26 * splash._u)
        opacity: splash._ch * (1 - splash.homeOpacity)

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Math.round(20 * splash._u)

            ChoiceButton {
                primary: true
                u: splash._u
                live: splash.choosing
                onTouched: function(x, y) { splash._pointerAt(x, y) }
                buttonKey: "A"
                keyLabel: "Enter"
                text: qsTr("Start StreamLight")
                onActivated: splash.pick(true)
            }
            ChoiceButton {
                u: splash._u
                live: splash.choosing
                onTouched: function(x, y) { splash._pointerAt(x, y) }
                buttonKey: "B"
                keyLabel: "Esc"
                text: qsTr("Windows desktop")
                onActivated: splash.pick(false)
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("StreamLight opens in %1 s").arg(Math.ceil(splash.cdLeft))
            color: Theme.text3
            font.family: Theme.family
            font.pixelSize: Math.round(Theme.fontBody * splash._u)
            opacity: splash._counting ? 1 : 0
            Behavior on opacity {
                enabled: !Theme.reduceAnimations
                NumberAnimation { duration: 300 }
            }
        }
    }
}
