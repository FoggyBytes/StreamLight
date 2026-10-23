import QtQuick 2.15
import Theme 1.0
import "sessionfmt.js" as Fmt

/*
 * One track of a session at a time, for the pad (8.0.0, the Sessions tab): a value axis, the
 * clock under it, the grade's thresholds as dashed lines, the games played along the bottom, a
 * cursor the D-pad moves with the value under it, and a zoom (LT / RT) centred on the cursor.
 *
 * ⚠️ The clock is linear from the session's start to its end. StreamTweak's own timeline
 * compresses the pauses between streams (ChartTimeAxis over StreamSpan); a session with a long
 * pause reads its times here as spread across it. The points are what the host recorded either way.
 */
Item {
    id: tl
    property var detail: null        // SESSION reply
    property var points: []          // SERIES reply's points
    property var track: null         // { key, name, unit, dec, bars, max }
    property real cursor: 0.5
    property int zoom: 1
    property bool active: false

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    readonly property real _span: 1 / zoom
    readonly property real _from: Math.max(0, Math.min(1 - _span, cursor - _span / 2))
    readonly property real _t0: detail ? new Date(detail.start).getTime() : 0
    readonly property real _t1: detail ? new Date(detail.end).getTime() : 1

    // The grade's lines for this track: where Excellent ends and where Good ends — the same
    // thresholds as QualityGradeCalculator on the host.
    readonly property var _thresholds: {
        if (!detail || !track) return []
        var fm = 1000 / (detail.fps || 60)
        if (track.key === "HL")  return [ { v: fm * 0.6, c: Theme.warning, l: qsTr("0.6 frame") }, { v: fm, c: Theme.danger, l: qsTr("1 frame") } ]
        if (track.key === "RTT") return [ { v: 25, c: Theme.warning, l: "25 ms" }, { v: 60, c: Theme.danger, l: "60 ms" } ]
        if (track.key === "BR" && detail.stats && detail.stats.target > 0)
            return [ { v: detail.stats.target, c: Theme.ink, l: qsTr("target") } ]
        return []
    }

    function _valueAt(f) {
        var n = points.length
        if (n === 0) return -1
        return points[Math.max(0, Math.min(n - 1, Math.round(f * (n - 1))))]
    }
    readonly property real _cursorValue: _valueAt(cursor)

    // A round ceiling for the value axis, from the visible window and the lines worth seeing.
    function _niceMax() {
        if (track && track.max) return track.max
        var n = points.length, m = 0
        var i0 = Math.floor(_from * (n - 1)), i1 = Math.ceil((_from + _span) * (n - 1))
        for (var i = i0; i <= i1 && i < n; i++) m = Math.max(m, points[i])
        for (var k = 0; k < _thresholds.length; k++)
            if (_thresholds[k].v < m * 3) m = Math.max(m, _thresholds[k].v * 1.1)
        if (m <= 0) m = 1
        var p = Math.pow(10, Math.floor(Math.log(m) / Math.LN10))
        var steps = [1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10]
        for (var s = 0; s < steps.length; s++) if (steps[s] * p >= m) return steps[s] * p
        return 10 * p
    }

    readonly property int _gutter: _px(46)
    readonly property int _axisH: _px(22)
    readonly property int _gameH: _px(26)
    readonly property int _top: _px(30)

    onDetailChanged: cv.requestPaint()
    onPointsChanged: cv.requestPaint()
    onTrackChanged: cv.requestPaint()
    onCursorChanged: cv.requestPaint()
    onZoomChanged: cv.requestPaint()
    onActiveChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    onHeightChanged: cv.requestPaint()
    Connections { target: Theme; function onChanged() { cv.requestPaint() } }

    Canvas {
        id: cv
        anchors.fill: parent
        renderTarget: Canvas.Image
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            if (!tl.detail || !tl.track || tl.points.length === 0) return
            var u = tl._u
            var x0 = tl._gutter, x1 = width - tl._px(8)
            var y0 = tl._top, y1 = height - tl._axisH - tl._gameH
            var w = x1 - x0, h = y1 - y0
            if (w <= 0 || h <= 0) return
            var max = tl._niceMax()
            var fy = function(v) { return y1 - Math.min(1, Math.max(0, v / max)) * h }
            var fx = function(f) { return x0 + (f - tl._from) / tl._span * w }
            ctx.font = Math.round(Theme.fontCaption * u) + "px '" + Theme.family + "'"

            // Value axis: 0, half, max, with faint grid lines.
            ctx.textAlign = "right"; ctx.textBaseline = "middle"
            for (var g = 0; g <= 2; g++) {
                var v = max * g / 2, y = fy(v)
                ctx.strokeStyle = Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, g === 0 ? 0.25 : 0.08)
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(x0, Math.round(y) + 0.5); ctx.lineTo(x1, Math.round(y) + 0.5); ctx.stroke()
                ctx.fillStyle = Theme.text3
                ctx.fillText(Fmt.num(v, max < 10 ? 1 : 0), x0 - 6 * u, y)
            }
            ctx.textAlign = "left"
            ctx.fillText(tl.track.unit, 2 * u, y0 - 14 * u)

            // The grade's lines.
            ctx.setLineDash([4, 4])
            for (var k = 0; k < tl._thresholds.length; k++) {
                var t = tl._thresholds[k]
                if (t.v > max) continue
                var ty = Math.round(fy(t.v)) + 0.5
                ctx.strokeStyle = t.c; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(x0, ty); ctx.lineTo(x1, ty); ctx.stroke()
                ctx.fillStyle = t.c; ctx.textAlign = "right"; ctx.textBaseline = "bottom"
                ctx.fillText(t.l, x1, ty - 2 * u)
            }
            ctx.setLineDash([])

            // The data.
            var n = tl.points.length
            var i0 = Math.max(0, Math.floor(tl._from * (n - 1)) - 1)
            var i1 = Math.min(n - 1, Math.ceil((tl._from + tl._span) * (n - 1)) + 1)
            ctx.save()
            ctx.beginPath(); ctx.rect(x0, y0 - 2, w, h + 2); ctx.clip()
            if (tl.track.bars) {
                ctx.fillStyle = Theme.warning
                var bw = Math.max(1, w / ((i1 - i0) + 1) * 0.7)
                for (var b = i0; b <= i1; b++) {
                    if (tl.points[b] <= 0) continue
                    var bx = fx(b / (n - 1))
                    ctx.fillRect(bx - bw / 2, fy(tl.points[b]), bw, y1 - fy(tl.points[b]))
                }
            } else {
                ctx.beginPath()
                for (var j = i0; j <= i1; j++) {
                    var px = fx(j / (n - 1)), py = fy(tl.points[j])
                    if (j === i0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                }
                ctx.strokeStyle = Theme.ink; ctx.lineWidth = 1.6 * u; ctx.stroke()
                ctx.lineTo(fx(i1 / (n - 1)), y1); ctx.lineTo(fx(i0 / (n - 1)), y1); ctx.closePath()
                ctx.fillStyle = Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.10); ctx.fill()
            }
            ctx.restore()

            // The clock under it.
            ctx.fillStyle = Theme.text3; ctx.textBaseline = "top"
            for (var c = 0; c <= 4; c++) {
                var f = tl._from + tl._span * c / 4
                ctx.textAlign = c === 0 ? "left" : c === 4 ? "right" : "center"
                ctx.fillText(Fmt.clock(tl._t0 + f * (tl._t1 - tl._t0)), fx(f), y1 + 6 * u)
            }

            // The games along the bottom.
            var gy = y1 + tl._axisH + 2 * u
            var spans = tl.detail.game_spans || []
            for (var s = 0; s < spans.length; s++) {
                var a = (new Date(spans[s].start).getTime() - tl._t0) / (tl._t1 - tl._t0)
                var e = (new Date(spans[s].end).getTime() - tl._t0) / (tl._t1 - tl._t0)
                var gx0 = Math.max(x0, fx(a)), gx1 = Math.min(x1, fx(e))
                if (gx1 - gx0 < 2) continue
                ctx.fillStyle = Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                ctx.fillRect(gx0, gy, gx1 - gx0, tl._gameH - 6 * u)
                ctx.fillStyle = Theme.text2; ctx.textAlign = "left"; ctx.textBaseline = "middle"
                ctx.save(); ctx.beginPath(); ctx.rect(gx0, gy, gx1 - gx0, tl._gameH); ctx.clip()
                ctx.fillText(spans[s].name, gx0 + 6 * u, gy + (tl._gameH - 6 * u) / 2)
                ctx.restore()
            }

            // The cursor and its value.
            var cx = Math.round(fx(tl.cursor)) + 0.5
            ctx.strokeStyle = tl.active ? Theme.text : Theme.text3
            ctx.lineWidth = 1
            ctx.beginPath(); ctx.moveTo(cx, y0 - 4 * u); ctx.lineTo(cx, y1); ctx.stroke()
            var vv = tl._cursorValue
            if (vv >= 0) {
                ctx.fillStyle = Theme.ink
                ctx.beginPath(); ctx.arc(cx, fy(vv), 4 * u, 0, Math.PI * 2); ctx.fill()
            }
        }
    }

    // The value under the cursor, top right: what the D-pad is pointing at.
    Text {
        anchors.right: parent.right
        anchors.rightMargin: tl._px(8)
        text: tl.detail && tl.points.length ? Fmt.clockSec(tl._t0 + tl.cursor * (tl._t1 - tl._t0)) + "   "
              + (tl._cursorValue < 0 ? "—" : Fmt.num(tl._cursorValue, tl.track ? tl.track.dec : 1) + " " + (tl.track ? tl.track.unit : ""))
              : ""
        color: Theme.text
        font.family: Theme.family
        font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale)
        font.bold: true
    }
    Text {
        x: tl._gutter
        visible: tl.zoom > 1
        text: "×" + tl.zoom
        color: Theme.inkText
        font.family: Theme.family
        font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale)
        font.bold: true
    }
    // Asked and not yet here — or nothing to ask: a client that sends no telemetry (Moonlight,
    // Artemis…) leaves its sessions without a single track.
    Text {
        anchors.centerIn: parent
        visible: tl.detail !== null && tl.points.length === 0
        text: tl.track ? qsTr("Loading the track…") : qsTr("No timeline: this session's client sent no telemetry")
        color: Theme.text3
        font.family: Theme.family
        font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale)
    }
}
