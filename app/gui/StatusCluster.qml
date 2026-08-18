import QtQuick 2.15
import QtQuick.Controls 2.15

import Theme 1.0
import PowerStatus 1.0
import StreamingPreferences 1.0

/*
 * Time, date and — on a device that has one — the battery, for the top-right of the window.
 *
 * ⚠️ Instantiated once, by AppShell, and never by a page. Every page it appears on had its own
 * copy anchored to its own header, and the three headers do not agree on where their corner is,
 * so the clock moved on every page change. The positioning note is on the AppShell instance.
 *
 * One strong thing and a quiet line under it: the clock is what you look at, the date and the
 * charge are the surround and they sit together because they are the same category of thing.
 *
 * ⚠️ The battery belongs on the second line, and that is the whole layout. Given a line of its
 * own beside the clock it had nothing to align to — its centre fell between the two rows of
 * type, sharing a baseline with neither — and it competed with the one figure that should win.
 * It also settles the case the alternatives handled worst: on a desktop there is no battery at
 * all, and here that costs one chip off the end of a line while nothing else moves.
 *
 * Sizes are fixed pixels rather than the `_u` scale the cards use: this has to sit against the
 * wordmark opposite, not against the stage below it.
 *
 * ⚠️ Deliberately not shared with UnlockPad, which also draws a time and a date. That one is
 * imitating the Windows logon screen — big, light, centred — because looking like Windows is
 * how it says whose password you are typing. Same two values, opposite jobs.
 */
Column {
    id: root

    spacing: 4

    // Only the minute is on screen, so a change of minute is the only thing worth repainting
    // for. Ticking every second and comparing is what survives the machine being suspended:
    // a timer armed for the next minute boundary comes back from sleep pointing at the past.
    property date _now: new Date()

    // Charging is the one state worth colouring for its own sake — it is the answer to "can I
    // unplug and go". Below that the colours are the app's own semantics: amber is a warning,
    // red is a problem. A healthy battery takes the date's own grey, because on this line it
    // is the date's equal and not an alert.
    readonly property color _batteryColor: PowerStatus.charging ? Theme.online
                                         : PowerStatus.percent <= 10 ? Theme.danger
                                         : PowerStatus.percent <= 20 ? Theme.warning
                                         : Theme.text3

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            var d = new Date()
            if (d.getMinutes() !== root._now.getMinutes() || d.getDate() !== root._now.getDate()) {
                root._now = d
            }
        }
    }

    // ── The clock ────────────────────────────────────────────────────────────
    Label {
        anchors.right: parent.right
        // "AP" is Qt's uppercase AM/PM, and the hour loses its leading zero with it — the two
        // go together, nobody writes "04:07 PM".
        text: Qt.formatTime(root._now,
                            StreamingPreferences.clockFormat === StreamingPreferences.CF_12H
                            ? "h:mm AP" : "HH:mm")
        font.family: Theme.family
        font.pixelSize: 30
        font.weight: Font.Medium
        color: Theme.text
    }

    // ── Date, and the charge beside it ───────────────────────────────────────
    Row {
        anchors.right: parent.right
        spacing: 10

        Label {
            anchors.verticalCenter: parent.verticalCenter
            // All three patterns are numeric, so no locale comes into it: what the setting picks
            // is the order of the fields, which is the only thing people disagree about.
            text: {
                switch (StreamingPreferences.dateFormat) {
                case StreamingPreferences.DF_MDY: return Qt.formatDate(root._now, "MM/dd/yyyy")
                case StreamingPreferences.DF_YMD: return Qt.formatDate(root._now, "yyyy/MM/dd")
                default:                          return Qt.formatDate(root._now, "dd/MM/yyyy")
                }
            }
            font.family: Theme.family
            font.pixelSize: 13
            color: Theme.text3
        }

        // Separator, present only when there are two things to separate.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: PowerStatus.present
            width: 3; height: 3
            radius: 1.5
            color: Theme.text3
        }

        // Absent entirely on a desktop: an empty gauge or a "no battery" note would both be
        // saying something about a thing this machine does not have.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            visible: PowerStatus.present

            // Charging bolt, outboard of the gauge rather than over it: laid on top it would be
            // invisible against the fill at high charge and against nothing at low charge, and a
            // knockout needs a flat background this page does not have.
            Canvas {
                anchors.verticalCenter: parent.verticalCenter
                width: 7
                height: 11
                visible: PowerStatus.charging

                property color tint: root._batteryColor
                onTintChanged: requestPaint()
                onVisibleChanged: if (visible) requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = tint
                    ctx.beginPath()
                    ctx.moveTo(width * 0.78, 0)
                    ctx.lineTo(width * 0.10, height * 0.54)
                    ctx.lineTo(width * 0.46, height * 0.54)
                    ctx.lineTo(width * 0.22, height)
                    ctx.lineTo(width * 0.90, height * 0.44)
                    ctx.lineTo(width * 0.54, height * 0.44)
                    ctx.closePath()
                    ctx.fill()
                }
            }

            // The gauge: body, terminal, and a fill that is the actual charge.
            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: 13

                Rectangle {
                    id: batteryBody
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22
                    height: parent.height
                    radius: 3.5
                    color: "transparent"
                    border.width: 1.3
                    // The outline is the container, not the reading — kept dimmer than the fill
                    // so a nearly-empty battery looks empty instead of looking like an outline.
                    border.color: Qt.rgba(root._batteryColor.r, root._batteryColor.g,
                                          root._batteryColor.b, 0.6)
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 2.1
                    height: 5.2
                    radius: 1.05
                    color: batteryBody.border.color
                }

                Rectangle {
                    anchors.left: batteryBody.left
                    anchors.leftMargin: 2.6
                    anchors.verticalCenter: parent.verticalCenter
                    height: batteryBody.height - 5.2
                    // A floor of 2px: at 1% a proportional bar would round away to nothing and
                    // the gauge would read as "no data" at exactly the moment it matters most.
                    width: Math.max(2, (batteryBody.width - 5.2) *
                                       Math.max(0, Math.min(100, PowerStatus.percent)) / 100)
                    radius: 1.3
                    color: root._batteryColor

                    Behavior on width {
                        enabled: !Theme.reduceAnimations
                        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                    }
                }
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: PowerStatus.percent + "%"
                font.family: Theme.family
                font.pixelSize: 13
                font.weight: Font.Medium
                color: root._batteryColor
            }
        }
    }
}
