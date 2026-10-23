import QtQuick 2.15
import Theme 1.0
import "sessionfmt.js" as Fmt

/*
 * Two sessions side by side (8.0.0, the Sessions tab), as StreamTweak's Compare: session 1 read
 * against session 2, green where 1 did better, red where it did worse; load figures are context
 * and stay grey. A difference too small to print is no difference.
 */
Item {
    id: cmp
    property var a: null                // SESSION replies
    property var b: null
    property string coverA: ""
    property string coverB: ""

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }
    function _fpx(n) { return Math.round(n * _u * Theme.textScale) }

    // [label, stats key, decimals, unit, sense]: sense -1 = lower is better, +1 = higher, 0 = context.
    readonly property var _rows: [
        [qsTr("RTT"), "rtt", 1, "ms", -1], [qsTr("Host latency"), "host_lat", 1, "ms", -1],
        [qsTr("Late frames"), "late", 1, "%", -1], [qsTr("Frame drops"), "drops", 2, "%", -1],
        [qsTr("Jitter"), "jitter", 2, "ms", -1], [qsTr("Decode"), "decode", 2, "ms", -1],
        [qsTr("Bitrate"), "bitrate", 0, "Mbps", 1], [qsTr("Frame rate"), "fps_avg", 0, "fps", 1],
        [qsTr("GPU"), "gpu", 0, "%", 0], [qsTr("GPU temp"), "gpu_temp", 0, "°C", 0], [qsTr("CPU"), "cpu", 0, "%", 0]
    ]

    // ⚠️ An inline component is its own scope: it cannot see `cmp` (CustomResolutionDialog says
    // the same), so it carries its own measures.
    component Head: Row {
        id: hd
        property var s: null
        property string cover: ""
        property string tag: ""
        function px(n) { return Math.round(n * Theme.uiScale) }
        function fpx(n) { return Math.round(n * Theme.uiScale * Theme.textScale) }
        readonly property color _gc: !s ? Theme.text3 : s.grade === 1 ? Theme.online : s.grade === 2 ? Theme.gradeGood
                                    : s.grade === 3 ? Theme.danger : Theme.text3
        spacing: hd.px(14)
        Rectangle {
            width: hd.px(60); height: hd.px(80); radius: hd.px(6); color: Theme.cardHigh; clip: true
            Image { anchors.fill: parent; visible: hd.cover.length > 0
                    source: hd.cover.length ? "data:image/png;base64," + hd.cover : ""; fillMode: Image.PreserveAspectCrop }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: hd.px(3)
            Text { text: hd.tag; color: Theme.inkText; font.family: Theme.family; font.pixelSize: hd.fpx(Theme.fontCaption); font.bold: true
                   font.capitalization: Font.AllUppercase; font.letterSpacing: 0.8 }
            Text { text: Fmt.games(hd.s); color: Theme.text; font.family: Theme.family; font.pixelSize: hd.fpx(Theme.fontTitle); font.bold: true }
            Text { text: hd.s ? Fmt.day(hd.s.start) + " · " + Fmt.time(hd.s.start) + " · " + Fmt.duration(hd.s.start, hd.s.end)
                                + " · " + (hd.s.client || qsTr("Unknown client")) : ""
                   color: Theme.text2; font.family: Theme.family; font.pixelSize: hd.fpx(Theme.fontSmall) }
            Text { text: Fmt.gradeName(hd.s ? hd.s.grade : 0); color: hd._gc
                   font.family: Theme.family; font.pixelSize: hd.fpx(Theme.fontSmall); font.bold: true }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: cmp.a === null || cmp.b === null
        text: qsTr("Loading the two sessions…")
        color: Theme.text3
        font.family: Theme.family; font.pixelSize: cmp._fpx(Theme.fontBody)
    }

    Item {
        anchors.fill: parent
        visible: cmp.a !== null && cmp.b !== null
        Row {
            id: heads
            width: parent.width
            Head { s: cmp.a; cover: cmp.coverA; tag: qsTr("Session 1"); width: heads.width / 2 }
            Head { s: cmp.b; cover: cmp.coverB; tag: qsTr("Session 2"); width: heads.width / 2 }
        }

        Rectangle {
            anchors.top: heads.bottom
            anchors.topMargin: cmp._px(18)
            width: parent.width
            height: table.implicitHeight + cmp._px(24)
            radius: cmp._px(12)
            color: Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, 0.75)
            border.color: Theme.line
            Column {
                id: table
                x: cmp._px(20); y: cmp._px(12)
                width: parent.width - cmp._px(40)
                Row {
                    height: cmp._px(34)
                    Repeater {
                        model: [qsTr("Metric"), qsTr("Session 1"), qsTr("Session 2"), qsTr("Δ 1 vs 2")]
                        Text { width: index === 0 ? table.width * 0.34 : table.width * 0.22; height: parent.height
                               verticalAlignment: Text.AlignVCenter; horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                               text: modelData; color: Theme.text3; font.family: Theme.family
                               font.pixelSize: cmp._fpx(Theme.fontCaption); font.bold: true; font.capitalization: Font.AllUppercase }
                    }
                }
                Repeater {
                    model: cmp.a && cmp.b ? cmp._rows : []
                    Row {
                        readonly property var _sa: cmp.a.stats || ({})
                        readonly property var _sb: cmp.b.stats || ({})
                        readonly property real _va: _sa[modelData[1]] !== undefined ? _sa[modelData[1]] : -1
                        readonly property real _vb: _sb[modelData[1]] !== undefined ? _sb[modelData[1]] : -1
                        readonly property bool _known: _va >= 0 && _vb >= 0
                        readonly property real _d: _known ? Number((_va - _vb).toFixed(modelData[2])) : 0
                        readonly property color _dc: !_known || modelData[4] === 0 || _d === 0 ? Theme.text3
                                                   : (_d * modelData[4] > 0 ? Theme.online : Theme.danger)
                        height: cmp._px(36)
                        Text { width: table.width * 0.34; height: parent.height; verticalAlignment: Text.AlignVCenter
                               text: modelData[0]; color: Theme.text; font.family: Theme.family; font.pixelSize: cmp._fpx(Theme.fontBody) }
                        Text { width: table.width * 0.22; height: parent.height; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignRight
                               text: Fmt.num(parent._va, modelData[2]) + " " + modelData[3]; color: Theme.text
                               font.family: Theme.family; font.pixelSize: cmp._fpx(Theme.fontBody) }
                        Text { width: table.width * 0.22; height: parent.height; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignRight
                               text: Fmt.num(parent._vb, modelData[2]) + " " + modelData[3]; color: Theme.text2
                               font.family: Theme.family; font.pixelSize: cmp._fpx(Theme.fontBody) }
                        Text { width: table.width * 0.22; height: parent.height; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignRight
                               text: parent._known ? (parent._d > 0 ? "+" : "") + (parent._d === 0 ? 0 : parent._d).toFixed(modelData[2]) : "—"
                               color: parent._dc; font.family: Theme.family; font.pixelSize: cmp._fpx(Theme.fontBody); font.bold: true }
                    }
                }
            }
        }
    }
}
