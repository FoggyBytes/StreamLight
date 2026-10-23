.pragma library

// The Sessions tab's words and numbers (8.0.0): dates, lengths, grades — shared by its pieces.

function pad2(n) { return (n < 10 ? "0" : "") + n }

var _days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
var _months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

function day(iso) {
    var d = new Date(iso)
    return _days[d.getDay()] + " " + d.getDate() + " " + _months[d.getMonth()]
}
function time(iso) {
    var d = new Date(iso)
    return pad2(d.getHours()) + ":" + pad2(d.getMinutes())
}
function clock(ms) {
    var d = new Date(ms)
    return pad2(d.getHours()) + ":" + pad2(d.getMinutes())
}
function clockSec(ms) {
    var d = new Date(ms)
    return pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds())
}
function duration(a, b) {
    var m = Math.round((new Date(b) - new Date(a)) / 60000)
    if (m < 60) return m + " min"
    return Math.floor(m / 60) + " h " + pad2(m % 60)
}
// Names as StreamTweak's Sessions page gives them.
function gradeName(g) { return g === 1 ? "Excellent" : g === 2 ? "Good" : g === 3 ? "Poor" : "No data" }
// A figure the host never measured comes as -1: a dash, never a zero.
function num(v, dec) { return (v === undefined || v === null || v < 0) ? "—" : Number(v).toFixed(dec) }
function games(s) { return s && s.games && s.games.length ? s.games.join(" · ") : "Desktop" }
