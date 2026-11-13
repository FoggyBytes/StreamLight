import QtQuick 2.15

import Theme 1.0

/*
 * The value of one override row (8.8.0, #53 — docs/mockups/profili/ProfilesAMockup.qml).
 *
 * At rest it is ONE badge: the value the row streams with. A opens it — `open` — and it becomes
 * Settings' segmented selector: the other choices come out on both sides of the badge, the nearest
 * first, and the choice glides from pill to pill. Closed again, it folds back onto the chosen one.
 *
 * `opts` are the labels, `sel` the index on them (-1 = none of them: the value is then `text`, a
 * custom resolution or rate, or an inherited value no pill carries). `over`: the value is this
 * level's own (accent); otherwise it comes from the level below (neutral).
 *
 * Measures are Settings': container 36, pills 30, text Theme.fontSmall at Settings' text scale.
 * ⚠️ Only a picture: the dialog that owns it drives `open` and `sel` from its own cursor, so no
 * focus ever lands here (see OverrideSheet).
 */
Item {
    id: vb

    property var    opts: []
    property int    sel: -1
    property string text: ""            // shown when sel is -1
    property bool   over: false
    property bool   open: false
    property real   u: Theme.uiScale
    // Settings' text scale (0.88), whatever the screen behind the dialog uses.
    property real   ts: 0.88

    signal picked(int index)

    function _px(n) { return Math.round(n * u) }
    readonly property int _pad: _px(3)
    readonly property int _pillH: _px(30)

    implicitHeight: _px(36)
    implicitWidth: wrap.width

    readonly property color _hue: Theme.accentFill

    // The choice glides only when it changes while the badge is open (◀▶). While the badge opens or
    // closes it is glued to its pill: with its own animation it chased the pill as the others grew
    // around it, and drifted to the middle before settling on the value.
    property bool _glide: false
    onSelChanged: if (open) { _glide = true; glideTimer.restart() }
    onOpenChanged: { _glide = false; glideTimer.stop() }
    Timer { id: glideTimer; interval: 220; onTriggered: vb._glide = false }

    Rectangle {
        id: wrap
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: vb._px(36)
        radius: vb._px(8)
        width: (vb.sel >= 0 || vb.open ? prow.width : freeText.implicitWidth + vb._px(24)) + 2 * vb._pad
        color: vb.open ? Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.06) : "transparent"
        border.width: vb.open ? 1 : 0
        border.color: Theme.lineHigh
        clip: true
        Behavior on color { enabled: !Theme.reduceAnimations; ColorAnimation { duration: 160 } }

        // The choice: one plate that glides between the pills.
        Rectangle {
            id: selBox
            readonly property Item it: (prow.width, pills.count > vb.sel && vb.sel >= 0) ? pills.itemAt(vb.sel) : null
            visible: vb.sel >= 0 || !vb.open
            x: vb._pad + (it ? it.x : 0)
            y: vb._pad
            width: it ? it.width : freeText.implicitWidth + vb._px(24)
            height: vb._pillH
            radius: vb._px(5)
            color: vb.over ? Qt.rgba(vb._hue.r, vb._hue.g, vb._hue.b, 0.20)
                           : Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.10)
            border.width: 1
            border.color: vb.over ? Qt.rgba(vb._hue.r, vb._hue.g, vb._hue.b, 0.6) : Theme.lineHigh
            Behavior on x { enabled: !Theme.reduceAnimations && vb._glide; NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
            Behavior on width { enabled: !Theme.reduceAnimations && vb._glide; NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
            Behavior on color { enabled: !Theme.reduceAnimations; ColorAnimation { duration: 140 } }
        }

        // A value none of the pills carries, at rest.
        Text {
            id: freeText
            visible: vb.sel < 0 && !vb.open
            x: vb._pad + vb._px(12)
            anchors.verticalCenter: parent.verticalCenter
            text: vb.text
            color: Theme.text
            font.family: Theme.family
            font.pixelSize: Math.round(Theme.fontSmall * vb.u * vb.ts)
            font.bold: true
        }

        Row {
            id: prow
            x: vb._pad; y: vb._pad
            visible: vb.sel >= 0 || vb.open
            Repeater {
                id: pills
                model: vb.opts
                Item {
                    id: pill
                    readonly property bool on: index === vb.sel
                    readonly property real full: pt.implicitWidth + vb._px(24)
                    readonly property int dist: Math.abs(index - Math.max(0, vb.sel))
                    width: (vb.open || on) ? full : 0
                    height: vb._pillH
                    clip: true
                    Behavior on width {
                        enabled: !Theme.reduceAnimations
                        SequentialAnimation {
                            // Opening, the nearest choices come out first; closing, all at once.
                            PauseAnimation { duration: vb.open ? Math.min(6, pill.dist) * 30 : 0 }
                            NumberAnimation { duration: vb.open ? 220 : 160
                                              easing.type: vb.open ? Easing.OutBack : Easing.InCubic
                                              easing.overshoot: 1.1 }
                        }
                    }
                    Text {
                        id: pt
                        anchors.centerIn: parent
                        text: modelData
                        color: pill.on ? Theme.text : Theme.text2
                        font.family: Theme.family
                        font.pixelSize: Math.round(Theme.fontSmall * vb.u * vb.ts)
                        font.bold: pill.on
                        opacity: (vb.open || pill.on) ? 1 : 0
                        Behavior on opacity { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 160 } }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: vb.open
                        cursorShape: Qt.PointingHandCursor
                        onClicked: vb.picked(index)
                    }
                }
            }
        }
    }
}
