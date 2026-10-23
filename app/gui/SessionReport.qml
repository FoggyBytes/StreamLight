import QtQuick 2.15
import Theme 1.0
import "sessionfmt.js" as Fmt

/*
 * A session's report in the Sessions tab (8.0.0): what it was with its verdict beside the title,
 * the four checks behind the verdict (it is the worst of them, as on the host), the figures, then
 * the timeline one track at a time. Layout A of docs/mockups/sessioni, chosen on 21/10/2026, with
 * the verdict moved up so the timeline keeps its height at 1080p.
 */
Item {
    id: rep
    property var detail: null           // SESSION reply
    property string cover: ""           // base64 PNG, "" when the host has none
    property var tracks: []             // the tracks this session has, as SessionsTab.allTracks
    property int trackIndex: 0
    property var points: []
    property real cursor: 0.5
    property int zoom: 1
    property bool active: false         // the pad is in the timeline
    property bool marked: false

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }
    function _fpx(n) { return Math.round(n * _u * Theme.textScale) }
    function _gradeColor(g) { return g === 1 ? Theme.online : g === 2 ? Theme.gradeGood : g === 3 ? Theme.danger : Theme.text3 }
    // The dot and the badge: Good's mark is brighter than its word (Theme.gradeGoodMark).
    function _gradeMark(g) { return g === 2 ? Theme.gradeGoodMark : _gradeColor(g) }

    readonly property var _st: detail && detail.stats ? detail.stats : ({})
    readonly property var _checks: detail ? [
        { name: qsTr("Frame drops"),  value: Fmt.num(_st.drops, 2) + " %",    g: detail.checks[0] },
        { name: qsTr("RTT"),          value: Fmt.num(_st.rtt, 1) + " ms",     g: detail.checks[1] },
        { name: qsTr("Host latency"), value: Fmt.num(_st.host_lat, 1) + " ms", g: detail.checks[2] },
        { name: qsTr("Late frames"),  value: Fmt.num(_st.late, 1) + " %",     g: detail.checks[3] }
    ] : []

    Text {
        anchors.centerIn: parent
        visible: rep.detail === null
        text: qsTr("Loading the session…")
        color: Theme.text3
        font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontBody)
    }

    Item {
        anchors.fill: parent
        visible: rep.detail !== null

        // ── What it was, and its verdict on the same line ──────────────────────────────
        Item {
            id: head
            width: parent.width
            height: rep._px(76)
            Rectangle {
                id: headCover
                width: rep._px(56); height: rep._px(75)
                anchors.verticalCenter: parent.verticalCenter
                radius: rep._px(5)
                color: Theme.cardHigh
                clip: true
                Image {
                    anchors.fill: parent
                    visible: rep.cover.length > 0
                    source: rep.cover.length ? "data:image/png;base64," + rep.cover : ""
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                }
            }
            Column {
                anchors.left: headCover.right; anchors.leftMargin: rep._px(14)
                anchors.right: verdictPill.left; anchors.rightMargin: rep._px(14)
                anchors.verticalCenter: parent.verticalCenter
                spacing: rep._px(3)
                Row {
                    width: parent.width
                    spacing: rep._px(10)
                    Text {
                        width: Math.min(implicitWidth, parent.width - (rep.marked ? markPill.width + rep._px(10) : 0))
                        elide: Text.ElideRight
                        text: Fmt.games(rep.detail)
                        color: Theme.text
                        font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontH2); font.bold: true
                    }
                    Rectangle {
                        id: markPill
                        visible: rep.marked
                        anchors.verticalCenter: parent.verticalCenter
                        width: markText.implicitWidth + rep._px(14); height: rep._px(22); radius: height / 2
                        color: "transparent"; border.color: Theme.ink
                        Text { id: markText; anchors.centerIn: parent; text: qsTr("Marked to compare"); color: Theme.inkText
                               font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontCaption); font.bold: true }
                    }
                }
                Text {
                    width: parent.width; elide: Text.ElideRight
                    text: rep.detail ? Fmt.day(rep.detail.start) + " · " + Fmt.time(rep.detail.start) + "–" + Fmt.time(rep.detail.end)
                                       + " · " + Fmt.duration(rep.detail.start, rep.detail.end) : ""
                    color: Theme.text2
                    font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontBody)
                }
                Text {
                    width: parent.width; elide: Text.ElideRight
                    text: rep.detail ? (rep.detail.client || qsTr("Unknown client")) + "   ·   "
                                       + (rep.detail.codec || qsTr("Codec unknown"))
                                       + (rep.detail.fps > 0 ? "   ·   " + qsTr("%1 fps target").arg(rep.detail.fps) : "") : ""
                    color: Theme.text3
                    font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontSmall)
                }
            }
            Rectangle {
                id: verdictPill
                readonly property color _c: rep._gradeColor(rep.detail ? rep.detail.grade : 0)
                readonly property color _m: rep._gradeMark(rep.detail ? rep.detail.grade : 0)
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: vText.implicitWidth + rep._px(28); height: rep._px(44); radius: rep._px(10)
                color: Qt.rgba(_m.r, _m.g, _m.b, 0.16)
                border.color: _m
                Text {
                    id: vText
                    anchors.centerIn: parent
                    text: Fmt.gradeName(rep.detail ? rep.detail.grade : 0); color: verdictPill._c
                    font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontTitle); font.bold: true
                }
            }
        }

        // ── The four checks behind the verdict ─────────────────────────────────────────
        Row {
            id: checksRow
            anchors.top: head.bottom
            anchors.topMargin: rep._px(12)
            width: parent.width
            spacing: rep._px(10)
            Repeater {
                model: rep._checks
                Rectangle {
                    width: (checksRow.width - rep._px(10) * 3) / 4
                    height: rep._px(48); radius: rep._px(10)
                    color: Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, 0.75)
                    border.color: Theme.line
                    Row {
                        anchors.left: parent.left; anchors.leftMargin: rep._px(12)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: rep._px(10)
                        Rectangle { width: rep._px(10); height: width; radius: width / 2; color: rep._gradeMark(modelData.g)
                                    anchors.verticalCenter: parent.verticalCenter }
                        Column {
                            Text { text: modelData.name; color: Theme.text3
                                   font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontCaption); font.bold: true
                                   font.capitalization: Font.AllUppercase }
                            Text { text: modelData.value; color: Theme.text
                                   font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontBody); font.bold: true }
                        }
                    }
                }
            }
        }

        // ── The figures ────────────────────────────────────────────────────────────────
        Row {
            id: figures
            anchors.top: checksRow.bottom
            anchors.topMargin: rep._px(10)
            spacing: rep._px(26)
            Repeater {
                model: [
                    [qsTr("Bitrate"),    Fmt.num(rep._st.bitrate, 0) + " Mbps"],
                    [qsTr("Jitter"),     Fmt.num(rep._st.jitter, 2) + " ms"],
                    [qsTr("Decode"),     Fmt.num(rep._st.decode, 2) + " ms"],
                    [qsTr("Frame rate"), qsTr("%1 fps avg").arg(Fmt.num(rep._st.fps_avg, 0))],
                    [qsTr("GPU"),        Fmt.num(rep._st.gpu, 0) + " %"],
                    [qsTr("GPU temp"),   Fmt.num(rep._st.gpu_temp, 0) + " °C"],
                    [qsTr("CPU"),        Fmt.num(rep._st.cpu, 0) + " %"]
                ]
                Column {
                    Text { text: modelData[0]; color: Theme.text3
                           font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontCaption); font.bold: true
                           font.capitalization: Font.AllUppercase }
                    Text { text: modelData[1]; color: Theme.text2
                           font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontSmall) }
                }
            }
        }

        // ── The tracks: LB / RB ────────────────────────────────────────────────────────
        Item {
            id: trackBar
            anchors.top: figures.bottom
            anchors.topMargin: rep._px(10)
            width: parent.width
            height: rep._px(40)
            ActionHint {
                id: lbHint
                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                buttonKey: "LB"; keyLabel: "Q"; size: rep._px(22)
                opacity: rep.active ? 1 : 0.35
            }
            Row {
                anchors.left: lbHint.right; anchors.leftMargin: rep._px(12)
                anchors.right: rbHint.left; anchors.rightMargin: rep._px(12)
                anchors.verticalCenter: parent.verticalCenter
                spacing: rep._px(4)
                clip: true
                Repeater {
                    model: rep.tracks
                    Item {
                        readonly property bool _on: index === rep.trackIndex
                        width: trackName.implicitWidth + rep._px(22); height: rep._px(34)
                        Text {
                            id: trackName
                            anchors.centerIn: parent
                            text: modelData.name
                            color: parent._on ? Theme.text : Theme.text3
                            font.family: Theme.family; font.pixelSize: rep._fpx(Theme.fontSmall); font.bold: parent._on
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - rep._px(12); height: rep._px(3); radius: height / 2
                            visible: parent._on; color: Theme.ink
                        }
                    }
                }
            }
            ActionHint {
                id: rbHint
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                buttonKey: "RB"; keyLabel: "E"; size: rep._px(22)
                opacity: rep.active ? 1 : 0.35
            }
        }

        Rectangle {
            anchors.top: trackBar.bottom
            anchors.topMargin: rep._px(6)
            anchors.bottom: parent.bottom
            width: parent.width
            radius: rep._px(12)
            color: Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, 0.75)
            border.color: rep.active ? Theme.ink : Theme.line
            border.width: rep.active ? 2 : 1
            SessionTimeline {
                anchors.fill: parent
                anchors.margins: rep._px(14)
                detail: rep.detail
                points: rep.points
                track: rep.tracks.length ? rep.tracks[Math.min(rep.trackIndex, rep.tracks.length - 1)] : null
                cursor: rep.cursor
                zoom: rep.zoom
                active: rep.active
            }
        }
    }
}
