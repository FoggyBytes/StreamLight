import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Effects

import Theme 1.0
import PowerStatus 1.0
import StreamingPreferences 1.0
import SdlGamepadKeyNavigation 1.0

/*
 * Time, date and — on a device that has one — the battery, for the top-right of the window; and
 * since 8.7.0, on their left, the charge of the pads connected without a cable.
 *
 * ⚠️ Instantiated once, by AppShell, and never by a page. Every page it appears on had its own
 * copy anchored to its own header, and the three headers do not agree on where their corner is,
 * so the clock moved on every page change. The positioning note is on the AppShell instance.
 *
 * 8.7.0 (variant C of docs/mockups/pad-batteria, Marcello, 11/11/2026): one block on the right —
 * the time, the date under it, the device's battery under that — right-aligned and centred on the
 * pads at its left, with a thin divider between. Each pad is its maker's logo (Xbox, PlayStation,
 * Nintendo, Steam; any other a plain pad outline) over a four-cell gauge. Levels
 * and no figure: a pad on the Xbox Wireless Adapter only knows four, so a percentage would be a
 * precision it does not have. The device's own battery keeps its percentage — Windows' is exact.
 * Until 8.6.x the date and the battery shared the second line; the pads' column needed the block
 * on the right to be one piece it could centre on.
 *
 * Sizes are fixed pixels rather than the `_u` scale the cards use: this has to sit against the
 * wordmark opposite, not against the stage below it.
 *
 * ⚠️ Deliberately not shared with UnlockPad, which also draws a time and a date. That one is
 * imitating the Windows logon screen — big, light, centred — because looking like Windows is
 * how it says whose password you are typing. Same two values, opposite jobs.
 */
Row {
    id: root

    spacing: 18

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

    // The pads' charge (SdlGamepadKeyNavigation.padBatteries): only pads without a cable, only
    // while the interface has them open — so nothing during a stream, when this is not on screen.
    readonly property var _pads: SdlGamepadKeyNavigation.padBatteries
    readonly property bool _hasPads: _pads.length > 0

    // The block on the right has more empty room above the time's figures than under the last
    // line's, so its geometric centre reads high: the pads and the divider sit 2 px lower.
    readonly property int _opticalOffset: 2

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

    function _padColor(pad) {
        if (pad.charging) return Theme.online
        if (pad.critical) return Theme.danger
        if (pad.level === 1) return Theme.warning
        return Theme.text3
    }
    function _logo(brand) {
        switch (brand) {
        case "xbox":  return "qrc:/res/store_xbox.svg"
        case "ps":    return "qrc:/res/pad_logo_ps.svg"
        case "steam": return "qrc:/res/store_steam.svg"
        case "switch": return "qrc:/res/pad_logo_switch.svg"
        default:      return "qrc:/res/pad_logo_generic.svg"
        }
    }

    // The charging bolt, shared by the device's battery and the pads': outboard of the gauge
    // rather than over it — laid on top it would be invisible against the fill at high charge and
    // against nothing at low charge, and a knockout needs a flat background this page does not have.
    component Bolt: Canvas {
        id: bolt
        property color tint: Theme.online
        width: 7
        height: 11
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

    // ── The pads ─────────────────────────────────────────────────────────────
    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: root._opticalOffset
        spacing: 18
        visible: root._hasPads

        Repeater {
            model: root._pads

            delegate: Column {
                id: pad
                required property var modelData
                readonly property color tint: root._padColor(modelData)

                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: 7

                // The maker's logo, in the date's grey whatever the charge: only the charge
                // takes a colour. The SVGs are white; MultiEffect paints them.
                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 20
                    height: 20

                    Image {
                        id: logoImage
                        anchors.fill: parent
                        visible: false
                        source: root._logo(pad.modelData.brand)
                        sourceSize: Qt.size(40, 40)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    MultiEffect {
                        anchors.fill: parent
                        source: logoImage
                        colorization: 1.0
                        colorizationColor: Theme.text2
                    }
                }

                // Under it: the four cells, and the bolt while it charges.
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4

                    Bolt {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: pad.modelData.charging
                        tint: pad.tint
                    }

                    // The device battery's outline, four cells inside.
                    Item {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        height: 13

                        Rectangle {
                            id: cellsBody
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22
                            height: parent.height
                            radius: 3.5
                            color: "transparent"
                            border.width: 1.3
                            border.color: Qt.rgba(pad.tint.r, pad.tint.g, pad.tint.b, 0.6)
                        }

                        Rectangle {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2.1
                            height: 5.2
                            radius: 1.05
                            color: cellsBody.border.color
                        }

                        Row {
                            anchors.left: cellsBody.left
                            anchors.leftMargin: 2.6
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1.2

                            Repeater {
                                model: 4
                                delegate: Rectangle {
                                    required property int index
                                    width: (22 - 5.2 - 3 * 1.2) / 4
                                    height: 13 - 5.2
                                    radius: 0.8
                                    color: index < pad.modelData.level
                                           ? pad.tint
                                           : Qt.rgba(pad.tint.r, pad.tint.g, pad.tint.b, 0.16)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Between the pads and the time: as tall as the block on the right, a little short of it.
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: root._opticalOffset
        visible: root._hasPads
        width: 1
        height: clockBlock.height - 10
        color: Qt.rgba(Theme.text3.r, Theme.text3.g, Theme.text3.b, 0.4)
    }

    // ── Time, date, the device's battery ─────────────────────────────────────
    Column {
        id: clockBlock
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        Label {
            anchors.right: parent.right
            // "AP" is Qt's uppercase AM/PM, and the hour loses its leading zero with it — the two
            // go together, nobody writes "04:07 PM".
            text: Qt.formatTime(root._now,
                                StreamingPreferences.clockFormat === StreamingPreferences.CF_12H
                                ? "h:mm AP" : "HH:mm")
            font.family: Theme.family
            font.pixelSize: Theme.fontH1
            font.weight: Font.Medium
            color: Theme.text
        }

        Label {
            anchors.right: parent.right
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
            font.pixelSize: Theme.fontSmall
            color: Theme.text3
        }

        // Absent entirely on a desktop: an empty gauge or a "no battery" note would both be
        // saying something about a thing this machine does not have.
        Row {
            anchors.right: parent.right
            spacing: 6
            visible: PowerStatus.present

            Bolt {
                anchors.verticalCenter: parent.verticalCenter
                visible: PowerStatus.charging
                tint: root._batteryColor
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
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Medium
                color: root._batteryColor
            }
        }
    }
}
