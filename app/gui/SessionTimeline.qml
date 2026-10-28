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
 *
 * Host compute and Host power (8.0.1, variant C of docs/mockups/sessioni/HostLanesMockup.qml):
 * every series the host recorded, one strip each under the same clock and cursor — GPU · Enc ·
 * CPU, and Power (with the limit in force dashed and the At-the-limit bands) · Temp. Each strip
 * has its own scale and its name and value on the left. Needs StreamTweak 10.0.4, which lists
 * the extra series in SESSION's `subtracks`; from an older host the two tracks stay one line.
 */
Item {
    id: tl
    property var detail: null        // SESSION reply
    property var points: []          // SERIES reply's points
    property var parts: ({})         // the track's subtracks' points, by key (ENC, CPU, PLIMIT, TEMP, PCAP)
    property var track: null         // { key, name, unit, dec, bars, max }
    property real cursor: 0.5
    property int zoom: 1
    property bool active: false

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    // ── The two host tracks as strips ────────────────────────────────────────────────
    // StreamTweak's roles for the colours: the first series in the accent like every track,
    // Enc orange, CPU and Temp green, the limit and its bands amber.
    readonly property color _cEnc:   Theme.light ? "#c2410c" : "#fb923c"
    readonly property color _cGreen: Theme.light ? "#15803d" : "#4ade80"
    readonly property bool _stacked: detail !== null && track !== null && detail.subtracks !== undefined
                                     && (track.key === "GPU" || track.key === "POWER")
    readonly property var _strips: {
        if (!_stacked) return []
        var has = function(k) { return detail.subtracks.indexOf(k) >= 0 }
        if (track.key === "GPU") {
            var c = [ { key: "GPU", name: qsTr("GPU"), unit: "%", color: Theme.ink, max: 100, fill: true } ]
            if (has("ENC")) c.push({ key: "ENC", name: qsTr("Enc"), unit: "%", color: _cEnc, max: 100,
                                     na: detail.enc_bypass_all === true })
            if (has("CPU")) c.push({ key: "CPU", name: qsTr("CPU"), unit: "%", color: _cGreen, max: 100 })
            return c
        }
        var p = [ { key: "POWER", name: qsTr("Power"), unit: "W", color: Theme.ink, fill: true,
                    limit: has("PLIMIT"), cap: has("PCAP") } ]
        if (has("TEMP")) p.push({ key: "TEMP", name: qsTr("Temp"), unit: "°C", color: _cGreen, max: 100 })
        return p
    }
    // The streams whose encoder Enc cannot see (PyroWave, software), as fractions of the clock.
    readonly property var _bypass: {
        var r = []
        if (!detail || !detail.enc_bypass) return r
        for (var i = 0; i < detail.enc_bypass.length; i++) {
            var b = detail.enc_bypass[i]
            r.push({ f0: (new Date(b.start).getTime() - _t0) / (_t1 - _t0),
                     f1: (new Date(b.end).getTime() - _t0) / (_t1 - _t0), label: b.label })
        }
        return r
    }
    function _bypassAt(f) {
        for (var i = 0; i < _bypass.length; i++) if (f >= _bypass[i].f0 && f <= _bypass[i].f1) return _bypass[i]
        return null
    }
    function _pts(key) { return track && key === track.key ? points : (parts[key] || []) }
    function _at(key, f) {
        var d = _pts(key), n = d.length
        if (n === 0) return -1
        return d[Math.max(0, Math.min(n - 1, Math.round(f * (n - 1))))]
    }
    function _visibleMax(key) {
        var d = _pts(key), n = d.length, m = 0
        var i0 = Math.max(0, Math.floor(_from * (n - 1))), i1 = Math.ceil((_from + _span) * (n - 1))
        for (var i = i0; i <= i1 && i < n; i++) m = Math.max(m, d[i])
        return m
    }
    function _round(m) {
        if (m <= 0) return 1
        var p = Math.pow(10, Math.floor(Math.log(m) / Math.LN10))
        var steps = [1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10]
        for (var s = 0; s < steps.length; s++) if (steps[s] * p >= m) return steps[s] * p
        return 10 * p
    }
    readonly property int _labelW: _px(140)

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
        return _round(m)
    }

    readonly property int _gutter: _px(46)
    readonly property int _axisH: _px(22)
    readonly property int _gameH: _px(26)
    readonly property int _top: _px(30)

    onDetailChanged: cv.requestPaint()
    onPointsChanged: cv.requestPaint()
    onPartsChanged: cv.requestPaint()
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

        function rgba(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
        function font(size, bold) { return (bold ? "bold " : "") + Math.round(size * tl._u) + "px '" + Theme.family + "'" }

        // The clock under the plot and the games along the bottom.
        function clockAndGames(ctx, x0, x1, fx, y1) {
            var u = tl._u
            ctx.font = font(Theme.fontCaption, false)
            ctx.fillStyle = Theme.text3; ctx.textBaseline = "top"
            for (var c = 0; c <= 4; c++) {
                var f = tl._from + tl._span * c / 4
                ctx.textAlign = c === 0 ? "left" : c === 4 ? "right" : "center"
                ctx.fillText(Fmt.clock(tl._t0 + f * (tl._t1 - tl._t0)), fx(f), y1 + 6 * u)
            }
            var gy = y1 + tl._axisH + 2 * u
            var spans = tl.detail.game_spans || []
            for (var s = 0; s < spans.length; s++) {
                var a = (new Date(spans[s].start).getTime() - tl._t0) / (tl._t1 - tl._t0)
                var e = (new Date(spans[s].end).getTime() - tl._t0) / (tl._t1 - tl._t0)
                var gx0 = Math.max(x0, fx(a)), gx1 = Math.min(x1, fx(e))
                if (gx1 - gx0 < 2) continue
                ctx.fillStyle = rgba(Theme.accent, 0.16)
                ctx.fillRect(gx0, gy, gx1 - gx0, tl._gameH - 6 * u)
                ctx.fillStyle = Theme.text2; ctx.textAlign = "left"; ctx.textBaseline = "middle"
                ctx.save(); ctx.beginPath(); ctx.rect(gx0, gy, gx1 - gx0, tl._gameH); ctx.clip()
                ctx.fillText(spans[s].name, gx0 + 6 * u, gy + (tl._gameH - 6 * u) / 2)
                ctx.restore()
            }
        }

        // One series as a line, optionally filled down to yBase and broken where skip(f) says.
        function line(ctx, key, fx, fy, col, lw, dashed, fillA, yBase, skip) {
            var d = tl._pts(key), n = d.length
            if (n < 2) return
            var i0 = Math.max(0, Math.floor(tl._from * (n - 1)) - 1)
            var i1 = Math.min(n - 1, Math.ceil((tl._from + tl._span) * (n - 1)) + 1)
            var runs = [], run = null
            for (var j = i0; j <= i1; j++) {
                if (skip && skip(j / (n - 1))) { run = null; continue }
                if (!run) { run = []; runs.push(run) }
                run.push(j)
            }
            ctx.setLineDash(dashed ? [5, 4] : [])
            for (var r = 0; r < runs.length; r++) {
                var rr = runs[r]
                ctx.beginPath()
                for (var k = 0; k < rr.length; k++) {
                    var px = fx(rr[k] / (n - 1)), py = fy(d[rr[k]])
                    if (k === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                }
                ctx.strokeStyle = col; ctx.lineWidth = lw; ctx.stroke()
                if (fillA > 0 && rr.length > 1) {
                    ctx.lineTo(fx(rr[rr.length - 1] / (n - 1)), yBase); ctx.lineTo(fx(rr[0] / (n - 1)), yBase); ctx.closePath()
                    ctx.fillStyle = rgba(col, fillA); ctx.fill()
                }
            }
            ctx.setLineDash([])
        }

        // At the limit: each point is the share of the host's readings in that slice the driver
        // flagged as SW Power Cap (§88) — an amber band, stronger the larger the share.
        function capBands(ctx, fx, yTop, yBot) {
            var d = tl._pts("PCAP"), n = d.length
            if (n < 2) return
            var half = 0.5 / (n - 1)
            for (var j = 0; j < n; j++) {
                if (d[j] <= 0) continue
                var a = fx(j / (n - 1) - half), b = fx(j / (n - 1) + half)
                if (b - a < 3 * tl._u) { var m = (a + b) / 2; a = m - 1.5 * tl._u; b = m + 1.5 * tl._u }
                ctx.fillStyle = rgba(Theme.warning, 0.16 + 0.5 * d[j])
                ctx.fillRect(a, yTop, b - a, yBot - yTop)
            }
        }

        function swatch(ctx, x, y, col, kind) {
            var u = tl._u
            if (kind === "band") { ctx.fillStyle = rgba(Theme.warning, 0.45); ctx.fillRect(x, y - 6 * u, 14 * u, 12 * u); return }
            ctx.setLineDash(kind === "dashed" ? [3, 3] : [])
            ctx.strokeStyle = col; ctx.lineWidth = 2.5 * u
            ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + 14 * u, y); ctx.stroke()
            ctx.setLineDash([])
        }

        // Host compute / Host power: a strip per series (variant C).
        function paintStrips(ctx) {
            var u = tl._u
            var x0 = tl._labelW, x1 = width - tl._px(8), w = x1 - x0
            var top = tl._top, bot = height - tl._axisH - tl._gameH
            if (w <= 0 || bot <= top) return
            var fx = function(f) { return x0 + (f - tl._from) / tl._span * w }
            var cx = Math.round(fx(tl.cursor)) + 0.5
            var strips = tl._strips
            // Enc on a session streamed only with PyroWave has nothing to plot: one row, no strip.
            var naH = tl._px(26), gap = tl._px(12), weight = 0, fixed = 0
            for (var i = 0; i < strips.length; i++) {
                if (strips[i].na) fixed += naH
                else weight += strips[i].key === "POWER" ? 1.6 : 1
            }
            var avail = bot - top - gap * (strips.length - 1) - fixed
            var y = top

            for (var k = 0; k < strips.length; k++) {
                var st = strips[k]
                var sh = st.na ? naH : avail * (st.key === "POWER" ? 1.6 : 1) / weight
                var sy0 = y, sy1 = y + sh
                y = sy1 + gap

                // The name, the value under the cursor, and the Power strip's extras.
                ctx.textAlign = "left"; ctx.textBaseline = "top"
                swatch(ctx, 0, sy0 + 8 * u, st.na ? Theme.text3 : st.color, st.na ? "dashed" : "line")
                ctx.font = font(Theme.fontSmall, false); ctx.fillStyle = Theme.text2
                ctx.fillText(st.name, tl._px(20), sy0)
                var bypassHere = st.key === "ENC" ? tl._bypassAt(tl.cursor) : null
                if (st.na) {
                    var naText = qsTr("n/a on %1").arg(tl._bypass.length ? tl._bypass[0].label : "PyroWave")
                    ctx.font = font(Theme.fontSmall, true); ctx.fillStyle = Theme.text3
                    ctx.fillText(naText, tl._px(20) + ctx.measureText(st.name).width + tl._px(16), sy0)
                    ctx.setLineDash([3, 4]); ctx.strokeStyle = rgba(Theme.text, 0.18); ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(x0, sy0 + 9 * u); ctx.lineTo(x1, sy0 + 9 * u); ctx.stroke()
                    ctx.setLineDash([])
                    continue
                }
                var v = tl._at(st.key, tl.cursor)
                ctx.font = font(Theme.fontBody, true)
                ctx.fillStyle = bypassHere ? Theme.text3 : Theme.text
                ctx.fillText(bypassHere ? qsTr("n/a · %1").arg(bypassHere.label)
                                        : v < 0 ? "—" : Fmt.num(v, 0) + " " + st.unit, tl._px(20), sy0 + tl._px(18))
                var ly = sy0 + tl._px(44)
                var extras = []
                if (st.limit) {
                    var lim = tl._at("PLIMIT", tl.cursor)
                    extras.push({ name: qsTr("Limit"), kind: "dashed", col: Theme.warning,
                                  text: lim < 0 ? "—" : Fmt.num(lim, 0) + " W", hot: false })
                }
                if (st.cap) {
                    var cap = tl._at("PCAP", tl.cursor)
                    extras.push({ name: qsTr("At the limit"), kind: "band", col: Theme.warning,
                                  text: cap > 0 ? Fmt.num(cap * 100, 0) + " %" : qsTr("no"), hot: cap > 0 })
                }
                for (var e = 0; e < extras.length; e++) {
                    if (ly + tl._px(16) > sy1) break
                    var ex = extras[e]
                    swatch(ctx, 0, ly + 8 * u, ex.col, ex.kind)
                    ctx.font = font(Theme.fontCaption, false); ctx.fillStyle = Theme.text2; ctx.textBaseline = "top"
                    ctx.fillText(ex.name + "  ", tl._px(20), ly)
                    var nw = ctx.measureText(ex.name + "  ").width
                    ctx.font = font(Theme.fontCaption, true); ctx.fillStyle = ex.hot ? Theme.warning : Theme.text
                    ctx.fillText(ex.text, tl._px(20) + nw, ly)
                    ly += tl._px(18)
                }

                // The strip: its own scale, the base and the top line, the top value on the left.
                var max = st.max ? st.max
                        : tl._round(Math.max(tl._visibleMax(st.key), st.limit ? tl._visibleMax("PLIMIT") * 1.04 : 0))
                var fy = (function(m, a, b) { return function(val) { return b - Math.min(1, Math.max(0, val / m)) * (b - a) } })(max, sy0 + 4 * u, sy1)
                ctx.lineWidth = 1
                ctx.strokeStyle = rgba(Theme.text, 0.25)
                ctx.beginPath(); ctx.moveTo(x0, Math.round(sy1) + 0.5); ctx.lineTo(x1, Math.round(sy1) + 0.5); ctx.stroke()
                ctx.strokeStyle = rgba(Theme.text, 0.08)
                ctx.beginPath(); ctx.moveTo(x0, Math.round(fy(max)) + 0.5); ctx.lineTo(x1, Math.round(fy(max)) + 0.5); ctx.stroke()
                ctx.font = font(Theme.fontCaption, false); ctx.fillStyle = Theme.text3
                ctx.textAlign = "right"; ctx.textBaseline = "middle"
                ctx.fillText(Fmt.num(max, 0) + " " + st.unit, x0 - 6 * u, fy(max))

                ctx.save()
                ctx.beginPath(); ctx.rect(x0, sy0, w, sh + 1); ctx.clip()
                if (st.cap) capBands(ctx, fx, sy0, sy1)
                if (st.limit) line(ctx, "PLIMIT", fx, fy, Theme.warning, 1.5 * u, true, 0, sy1, null)
                var skip = st.key === "ENC" && tl._bypass.length ? function(f) { return tl._bypassAt(f) !== null } : null
                line(ctx, st.key, fx, fy, st.color, (st.fill ? 1.8 : 1.5) * u, false, st.fill ? 0.10 : 0, sy1, skip)
                // Over the streams Enc cannot see, a dashed rule where the line would be.
                if (skip) {
                    ctx.setLineDash([3, 4]); ctx.strokeStyle = rgba(Theme.text, 0.3)
                    for (var b = 0; b < tl._bypass.length; b++) {
                        var bx0 = Math.max(x0, fx(tl._bypass[b].f0)), bx1 = Math.min(x1, fx(tl._bypass[b].f1))
                        if (bx1 <= bx0) continue
                        ctx.beginPath(); ctx.moveTo(bx0, sy1 - 2 * u); ctx.lineTo(bx1, sy1 - 2 * u); ctx.stroke()
                    }
                    ctx.setLineDash([])
                }
                ctx.restore()

                ctx.strokeStyle = tl.active ? Theme.text : Theme.text3; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(cx, sy0); ctx.lineTo(cx, sy1); ctx.stroke()
                if (v >= 0 && !bypassHere) {
                    ctx.fillStyle = st.color
                    ctx.beginPath(); ctx.arc(cx, fy(v), 4 * u, 0, Math.PI * 2); ctx.fill()
                }
            }
            clockAndGames(ctx, x0, x1, fx, bot)
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            if (!tl.detail || !tl.track || tl.points.length === 0) return
            if (tl._stacked) { paintStrips(ctx); return }
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

            // The clock under it, the games along the bottom.
            clockAndGames(ctx, x0, x1, fx, y1)

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
        // The strips carry their own values on the left: here only the time.
        text: tl.detail && tl.points.length ? Fmt.clockSec(tl._t0 + tl.cursor * (tl._t1 - tl._t0))
              + (tl._stacked ? "" : "   "
              + (tl._cursorValue < 0 ? "—" : Fmt.num(tl._cursorValue, tl.track ? tl.track.dec : 1) + " " + (tl.track ? tl.track.unit : "")))
              : ""
        color: Theme.text
        font.family: Theme.family
        font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale)
        font.bold: true
    }
    Text {
        x: tl._stacked ? tl._labelW : tl._gutter
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
