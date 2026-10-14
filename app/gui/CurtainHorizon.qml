import QtQuick
import Theme 1.0
import WindowMove 1.0

/*
 * The launch and quit curtain's foreground (7.0.0, "Horizon", chosen 13/10/2026 from the
 * mockup in docs/mockups/tenda). StreamSegue and QuitSegue both draw it, so the two screens
 * that frame a stream stay one design. The background is NOT here: each screen keeps the one
 * it had (the app's dark floor and the game's blurred cover), on purpose.
 *
 *   top left      the host and what this stream will be, in the Home card's chip styles
 *   lower left    the cover, a kicker (NOW STARTING / RESUMING / QUITTING), the game,
 *                 one status line with a breathing dot, an amber line when something is slow
 *   along the foot one bar, a segment per step — Connect, Launch, and Load only when the
 *                 launch waits for the game — the current one with a light running along it
 *
 * Everything is sized from the screen's height, like the curtain it replaces, so the same
 * layout holds from a handheld to a 4K TV. `rise` (0 → 1) is the entrance, driven by the
 * screen: the chips, the block and the bar come up out of the black one after the other.
 *
 * ⚠️ Every animation that loops here pauses while the window is dragged and is off under
 * Reduce animations, as all of the app's looping animations must (§76.2).
 */
Item {
    id: root

    property string hostName: ""
    property var    badges: []
    property url    boxArt
    property string kicker: ""
    property string gameName: ""
    property string status: ""
    property string detail: ""
    property string amber: ""
    // busy | done | bad
    property string tone: "busy"
    // Segment labels; empty hides the bar.
    property var    segments: []
    // Index of the segment under way; segments.length (or more) = all done.
    property int    stage: 0
    property real   rise: 1

    readonly property real _h: height > 0 ? height : 1080
    readonly property bool _loop: !Theme.reduceAnimations
    readonly property real _margin: _h * 0.08

    function _phase(from) { return Math.max(0, Math.min(1, (rise - from) / 0.55)) }
    function _ease(p) { return 1 - Math.pow(1 - p, 3) }

    // ── Chips, top left ──────────────────────────────────────────────────────
    Row {
        id: chips
        x: root._margin
        y: root._h * 0.06
        spacing: root._h * 0.009
        opacity: root._ease(root._phase(0))
        visible: root.hostName !== "" || root.badges.length > 0

        // The host: the Home card's state chip — square-ish corners, a thin line, a dot.
        Rectangle {
            visible: root.hostName !== ""
            height: root._h * 0.034
            width: hostChipRow.implicitWidth + root._h * 0.024
            radius: root._h * 0.005
            color: "transparent"
            border.color: Theme.stageLineHigh
            border.width: 1
            Row {
                id: hostChipRow
                anchors.centerIn: parent
                spacing: root._h * 0.0065
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: root._h * 0.0065; height: width; radius: width / 2
                    color: Theme.stageOnline
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.hostName.toUpperCase()
                    color: Theme.stageText2
                    font.family: Theme.family
                    font.pixelSize: root._h * 0.0145
                    font.weight: Font.DemiBold
                    font.letterSpacing: root._h * 0.0012
                }
            }
        }

        // The stream: the Home card's setting badges.
        Repeater {
            model: root.badges
            delegate: Rectangle {
                required property string modelData
                height: root._h * 0.034
                width: badgeText.implicitWidth + root._h * 0.019
                radius: root._h * 0.0065
                color: Qt.rgba(Theme.stageText.r, Theme.stageText.g, Theme.stageText.b, 0.07)
                border.width: 1
                border.color: Qt.rgba(Theme.stageText.r, Theme.stageText.g, Theme.stageText.b, 0.26)
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: modelData
                    color: Theme.stageText
                    font.family: Theme.family
                    font.pixelSize: root._h * 0.015
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    // ── The block, lower left ────────────────────────────────────────────────
    Item {
        id: block
        readonly property real _p: root._ease(root._phase(0.12))
        x: root._margin - (1 - _p) * root._h * 0.035
        opacity: _p
        anchors.bottom: bar.visible ? bar.top : parent.bottom
        anchors.bottomMargin: bar.visible ? root._h * 0.05 : root._h * 0.1
        width: parent.width - root._margin * 2
        height: root._h * 0.30

        HeroCover {
            id: cover
            visible: root.boxArt.toString() !== ""
            source: root.boxArt
            height: parent.height
            radius: root._h * 0.010
            shadowOffset: root._h * 0.008
        }

        Column {
            anchors.left: cover.visible ? cover.right : parent.left
            anchors.leftMargin: cover.visible ? root._h * 0.04 : 0
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: root._h * 0.012

            Text {
                text: root.kicker
                color: root.tone === "bad" ? Theme.stageDanger : Theme.accent
                font.family: Theme.family
                font.pixelSize: root._h * 0.017
                font.weight: Font.DemiBold
                font.letterSpacing: root._h * 0.004
            }

            Text {
                width: parent.width
                text: root.gameName
                color: Theme.stageText
                font.family: Theme.family
                font.pixelSize: root._h * 0.058
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            // The status: a dot that breathes while something is under way, green and still
            // once it is done, gone when it failed — the red words say that.
            Item {
                width: parent.width
                height: statusText.implicitHeight

                Rectangle {
                    id: dot
                    visible: root.tone !== "bad"
                    anchors.verticalCenter: statusText.verticalCenter
                    width: root._h * 0.012; height: width; radius: width / 2
                    color: root.tone === "done" ? Theme.stageOnline : Theme.accent
                    Behavior on color { ColorAnimation { duration: 300 } }

                    SequentialAnimation on opacity {
                        running: root._loop && root.tone === "busy" && root.visible
                        paused: running && WindowMove.moving
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) dot.opacity = 1
                        NumberAnimation { to: 0.3; duration: 1100; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 1100; easing.type: Easing.InOutSine }
                    }
                }

                Text {
                    id: statusText
                    anchors.left: dot.visible ? dot.right : parent.left
                    anchors.leftMargin: dot.visible ? root._h * 0.012 : 0
                    text: root.status
                    color: root.tone === "bad" ? Theme.stageDanger : Theme.stageText
                    font.family: Theme.family
                    font.pixelSize: root._h * 0.027
                    // Each new status arrives with a short fade and lift, so a change of step
                    // reads as a step rather than a flicker.
                    onTextChanged: if (root._loop) statusIn.restart()
                    ParallelAnimation {
                        id: statusIn
                        NumberAnimation { target: statusText; property: "opacity"; from: 0; to: 1; duration: 280; easing.type: Easing.OutCubic }
                        NumberAnimation { target: statusShift; property: "y"; from: root._h * 0.008; to: 0; duration: 280; easing.type: Easing.OutCubic }
                    }
                    transform: Translate { id: statusShift }
                }

                Text {
                    anchors.left: statusText.right
                    anchors.leftMargin: root._h * 0.012
                    anchors.baseline: statusText.baseline
                    visible: root.detail !== ""
                    text: "·  " + root.detail
                    color: Theme.stageText2
                    font.family: Theme.family
                    font.pixelSize: root._h * 0.019
                }
            }

            // Amber, one line: a link that did not switch, a launch taking long, a host not
            // answering. None of them stops anything, so none of them looks like an error.
            Text {
                visible: root.amber !== ""
                text: "⚠  " + root.amber
                color: Theme.stageWarning
                font.family: Theme.family
                font.pixelSize: root._h * 0.018
            }
        }
    }

    // ── The bar, along the foot ──────────────────────────────────────────────
    Row {
        id: bar
        visible: root.segments.length > 0
        opacity: root._ease(root._phase(0.24))
        x: root._margin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root._h * 0.08
        spacing: root._h * 0.012
        readonly property real _segW: root.segments.length > 0
            ? (root.width - root._margin * 2 - spacing * (root.segments.length - 1)) / root.segments.length
            : 0

        Repeater {
            model: root.segments
            delegate: Column {
                id: segment
                required property int index
                required property string modelData
                readonly property bool done: index < root.stage
                readonly property bool current: index === root.stage
                spacing: root._h * 0.012

                Rectangle {
                    id: track
                    width: bar._segW
                    height: Math.max(3, Math.round(root._h * 0.005))
                    radius: height / 2
                    color: Qt.rgba(Theme.stageText.r, Theme.stageText.g, Theme.stageText.b, 0.18)
                    clip: true

                    // Done: the accent fills the segment from the left as the step completes.
                    Rectangle {
                        height: parent.height
                        radius: parent.radius
                        color: Theme.accent
                        width: segment.done ? parent.width : 0
                        Behavior on width {
                            enabled: root._loop
                            NumberAnimation { duration: 450; easing.type: Easing.OutCubic }
                        }
                    }

                    // Under way: a soft light running along the segment. Still, at the start,
                    // under Reduce animations.
                    Rectangle {
                        id: runner
                        visible: segment.current
                        width: parent.width * 0.35
                        height: parent.height
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0) }
                            GradientStop { position: 0.5; color: Theme.accent }
                            GradientStop { position: 1.0; color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0) }
                        }
                        NumberAnimation on x {
                            running: root._loop && segment.current && root.visible
                            paused: running && WindowMove.moving
                            loops: Animation.Infinite
                            from: -runner.width
                            to: track.width
                            duration: 1800
                            easing.type: Easing.InOutSine
                        }
                    }
                }

                Text {
                    text: segment.modelData
                    color: segment.current ? Theme.stageText
                         : segment.done    ? Theme.stageText2
                         :                   Theme.stageOffline
                    font.family: Theme.family
                    font.pixelSize: root._h * 0.016
                    font.weight: segment.current ? Font.Medium : Font.Normal
                }
            }
        }
    }
}
