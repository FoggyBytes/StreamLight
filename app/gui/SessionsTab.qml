import QtQuick 2.15
import QtQuick.Controls 2.15
import Theme 1.0
import SdlGamepadKeyNavigation 1.0
import "sessionfmt.js" as Fmt

/*
 * ── The Sessions tab of the StreamTweak section (8.0.0) ────────────────────────────────────
 *
 * The host's session history, read from the couch: layout A of docs/mockups/sessioni (chosen on
 * 21/10/2026) — the list on the left, the selected session's report beside it, a compare view.
 * Everything stays on the host; it travels on the bridge a piece at a time (StreamTweak 10.0.0):
 *   SESSIONS <offset> <count> ALL|MINE   a page of the list (50 at a time, more as the cursor nears the end)
 *   SESSION <id>                         one report, asked when the cursor rests on a row
 *   SERIES <id> <track>                  one track of the timeline, asked when it is shown — with
 *                                        Host compute / Host power also its subtracks (8.0.1)
 *   COVER <id> <n>                       a cover thumbnail, asked when its row is drawn
 *   DELETESESSION <id>                   (8.2.0, StreamTweak 10.0.6) one session out of the host's
 *                                        history, after a confirmation; needs Host management
 * Sessions are named by id, never by a game: the bridge upper-cases the line.
 *
 * 8.2.0: the list follows the host by itself. A session appears in the history only once
 * StreamTweak has closed it — 30 s after the client left, the grace period in which a reconnect
 * still joins it — and the list used to be asked once, so the session just played was missing
 * until the tab was left and opened again. Now, while the tab is shown, the first row is asked
 * every 5 s (SESSIONS 0 1, a few hundred bytes), and the page is read again only when the totals
 * or the first id have moved, the cursor staying on the session it was on.
 *
 * Where the pad is (`zone`):
 *   filter   This device / All devices      ◂ ▸ choose · ▾ back to the list
 *   list     the sessions                   ▴ ▾ move · A the timeline · X mark · Start compare
 *   report   the timeline                   LB/RB track · ◂ ▸ cursor · LT/RT zoom · B the list
 *   compare  two marked sessions            B the list
 * What it does not use goes on to the section: LB/RB (tabs) and B (hosts) from the list, Y
 * (Settings) everywhere.
 */
FocusScope {
    id: tab
    property var computerModel: null
    property int hostIndex: -1
    property bool shown: false
    // False while the host cannot answer (StreamTweak off, missing, too old…): the tab is shown
    // greyed by the section and asks nothing.
    property bool available: true
    // 8.2.0: this device may delete sessions on the host — Host management, StreamTweak 10.0.6+.
    property bool canDelete: false
    property string hostName: ""

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }
    function _fpx(n) { return Math.round(n * _u * Theme.textScale) }
    function _gradeColor(g) { return g === 1 ? Theme.online : g === 2 ? Theme.gradeGood : g === 3 ? Theme.danger : Theme.text3 }
    // The dot and the badge: Good's mark is brighter than its word (Theme.gradeGoodMark).
    function _gradeMark(g) { return g === 2 ? Theme.gradeGoodMark : _gradeColor(g) }

    // The tracks of the timeline, in the order StreamTweak's lanes come. A session shows the
    // ones its host recorded (SESSION's `tracks`).
    readonly property var allTracks: [
        { key: "HL",    name: qsTr("Host frame latency"), unit: "ms",     dec: 1 },
        { key: "RTT",   name: qsTr("RTT"),                unit: "ms",     dec: 1 },
        { key: "DROPS", name: qsTr("Frame drops"),        unit: qsTr("frames"), dec: 0, bars: true },
        { key: "BR",    name: qsTr("Bitrate"),            unit: "Mbps",   dec: 0 },
        { key: "DEC",   name: qsTr("Decode"),             unit: "ms",     dec: 2 },
        { key: "GPU",   name: qsTr("Host compute"),       unit: "%",      dec: 0, max: 100 },
        { key: "POWER", name: qsTr("Host power"),         unit: "W",      dec: 0 }
    ]

    // ── State ────────────────────────────────────────────────────────────────────────
    property string filter: ""          // "" until the first answer, then mine · all
    property var items: []
    property int total: 0
    property int totalAll: 0
    property int totalMine: 0
    property bool loading: false
    property bool failed: false
    property int cur: 0
    readonly property var sel: items.length ? items[Math.min(cur, items.length - 1)] : null

    // What the host sent, by id (and track). Replaced, never mutated, so bindings follow.
    property var details: ({})
    property var covers: ({})
    property var series: ({})
    property var _pending: ({})         // not a binding source: only stops asking twice

    property var marked: []             // ids, in the order they were marked
    property string zone: "list"
    property int trackIndex: 0
    property real cursor: 0.5
    property int zoom: 1

    readonly property var detail: sel && details[sel.id] !== undefined ? details[sel.id] : null
    readonly property var tracks: detail && detail.tracks
                                  ? allTracks.filter(function(t) { return detail.tracks.indexOf(t.key) >= 0 }) : []
    readonly property var track: tracks.length ? tracks[Math.min(trackIndex, tracks.length - 1)] : null
    readonly property var points: sel && track && series[sel.id + "/" + track.key] !== undefined
                                  ? series[sel.id + "/" + track.key] : []
    // The other series of Host compute and Host power, drawn as strips (8.0.1): the ones the host
    // lists in SESSION's `subtracks` (StreamTweak 10.0.4 — an older host has none).
    readonly property var _partKeys: {
        if (!detail || !track || !detail.subtracks) return []
        var want = track.key === "GPU" ? ["ENC", "CPU"] : track.key === "POWER" ? ["PLIMIT", "TEMP", "PCAP"] : []
        return want.filter(function(k) { return detail.subtracks.indexOf(k) >= 0 })
    }
    readonly property var parts: {
        var m = {}
        for (var i = 0; i < _partKeys.length; i++) {
            var k = sel ? sel.id + "/" + _partKeys[i] : ""
            if (series[k] !== undefined) m[_partKeys[i]] = series[k]
        }
        return m
    }

    // ── Requests ─────────────────────────────────────────────────────────────────────
    function _ask(cmd) {
        if (!computerModel || hostIndex < 0) return
        if (_pending[cmd]) return
        _pending[cmd] = true
        computerModel.requestHostSection(hostIndex, cmd)
    }
    function _done(cmd) { delete _pending[cmd] }
    function _set(mapName, key, value) {
        var m = Object.assign({}, tab[mapName]); m[key] = value; tab[mapName] = m
    }
    function _listCmd(offset) { return "SESSIONS " + offset + " 50 " + (filter === "mine" ? "MINE" : "ALL") }

    function reload() {
        items = []; total = 0; cur = 0; failed = false
        loading = true
        _ask(_listCmd(0))
    }

    // ── Following the host (8.2.0) ───────────────────────────────────────────────────
    // The id the cursor should land on again when the first page comes back ("" = none).
    property string _keepId: ""
    function _probe() {
        if (!available || filter === "" || loading) return
        _ask("SESSIONS 0 1 " + (filter === "mine" ? "MINE" : "ALL"))
    }
    // The page again, in place: no emptying, the cursor kept on its session.
    function _refresh() {
        if (loading) return
        _keepId = sel ? sel.id : ""
        loading = true
        _ask(_listCmd(0))
    }
    Timer {
        id: probeTimer
        interval: 5000
        repeat: true
        running: tab.shown && tab.available && tab.filter !== "" && Qt.application.state === Qt.ApplicationActive
        onTriggered: tab._probe()
    }
    function _more() {
        if (loading || items.length >= total) return
        loading = true
        _ask(_listCmd(items.length))
    }
    function needCover(id) { if (covers[id] === undefined) _ask("COVER " + id + " 0") }
    function _needDetail(id) { if (id && details[id] === undefined) _ask("SESSION " + id) }
    function _needSeries() {
        if (!sel || !detail || !track) return
        if (series[sel.id + "/" + track.key] === undefined) _ask("SERIES " + sel.id + " " + track.key)
        for (var i = 0; i < _partKeys.length; i++)
            if (series[sel.id + "/" + _partKeys[i]] === undefined) _ask("SERIES " + sel.id + " " + _partKeys[i])
    }

    // First shown: the whole history, then "This device" if this device has any. Shown again:
    // a look at whether anything moved meanwhile (8.2.0).
    onShownChanged: {
        if (!shown) return
        if (filter === "" && !loading) reload()
        else _probe()
    }
    onFilterChanged: if (filter !== "" && !_firstAnswer) reload()
    property bool _firstAnswer: false

    // The cursor rests on a row a moment before its report is asked: the D-pad held down the
    // list should not ask for every report it passes.
    onSelChanged: { cursor = 0.5; zoom = 1; detailTimer.restart() }
    Timer { id: detailTimer; interval: 150; onTriggered: if (tab.sel) tab._needDetail(tab.sel.id) }

    // The right stick, sideways, moves the timeline's cursor (21/10/2026): by speed, not by
    // step, so a nudge creeps from one sample to the next and a full push crosses the window in
    // under two seconds. Measured in windows per second, so the zoom makes it finer still. The
    // stick's speed (250-2500 px/s, already quadratic) is read as a 0-1 push. A Timer and not a
    // FrameAnimation: this file imports QtQuick 2.15.
    Timer {
        id: cursorStick
        interval: 16
        repeat: true
        running: tab.zone === "report" && tab.activeFocus && tab.points.length > 0
                 && SdlGamepadKeyNavigation.scrollVelocityX !== 0
        property double _last: 0
        onRunningChanged: _last = Date.now()
        onTriggered: {
            var now = Date.now()
            var dt = Math.min(0.05, (now - _last) / 1000)
            _last = now
            var v = SdlGamepadKeyNavigation.scrollVelocityX
            var push = Math.max(0, Math.min(1, (Math.abs(v) - 250) / 2250))
            var windowsPerSec = 0.02 + 0.58 * push
            tab.cursor = Math.max(0, Math.min(1, tab.cursor + (v > 0 ? 1 : -1) * windowsPerSec / tab.zoom * dt))
        }
    }
    // Asked once every binding has caught up: `_partKeys` is a binding on the same track, and Qt
    // does not say whether it or this handler runs first — read here directly, it could still hold
    // the previous track's keys, so the strips were asked only on the way out (28/10/2026).
    onDetailChanged: Qt.callLater(_needSeries)
    onTrackChanged: Qt.callLater(_needSeries)
    onTracksChanged: if (trackIndex >= tracks.length) trackIndex = 0

    Connections {
        target: tab.computerModel
        function onHostSectionReceived(idx, verb, data) {
            if (idx !== tab.hostIndex) return
            var p = verb.split(" ")
            if (p[0] === "SESSIONS" && p[2] === "1") {
                // The 5 s look (8.2.0): read the page again only if something moved.
                tab._done(verb)
                if (data.items === undefined || tab.loading) return
                if (p[3] !== (tab.filter === "mine" ? "MINE" : "ALL")) return
                var first = data.items.length ? data.items[0].id : ""
                var have = tab.items.length ? tab.items[0].id : ""
                if (data.total_all !== tab.totalAll || data.total_mine !== tab.totalMine
                        || data.total !== tab.total || first !== have)
                    tab._refresh()
                return
            }
            if (p[0] === "SESSIONS") {
                tab._done(verb)
                tab.loading = false
                if (data.items === undefined) { tab.failed = tab.items.length === 0; return }
                // An answer for a filter since left is not this list.
                if (p[3] !== (tab.filter === "mine" ? "MINE" : "ALL") && tab.filter !== "") return
                tab.totalAll = data.total_all || 0
                tab.totalMine = data.total_mine || 0
                if (tab.filter === "") {
                    // The first answer is the whole history; "This device" is the default
                    // once this device has sessions of its own.
                    tab._firstAnswer = true
                    tab.filter = tab.totalMine > 0 ? "mine" : "all"
                    tab._firstAnswer = false
                    if (tab.filter === "mine") { tab.reload(); return }
                }
                var offset = parseInt(p[1])
                if (offset === 0) tab.items = data.items
                else if (offset === tab.items.length) tab.items = tab.items.concat(data.items)
                tab.total = data.total || 0
                if (offset === 0 && tab._keepId !== "") {
                    // A refresh: back on the session the cursor was on, wherever it moved to.
                    var at = -1
                    for (var i = 0; i < tab.items.length; i++)
                        if (tab.items[i].id === tab._keepId) { at = i; break }
                    tab.cur = at >= 0 ? at : Math.min(tab.cur, Math.max(0, tab.items.length - 1))
                    tab._keepId = ""
                }
                if (tab.sel) detailTimer.restart()
            } else if (p[0] === "SESSION") {
                tab._done(verb)
                // A missing answer is left unset, so the next rest on the row asks again.
                if (data.id !== undefined) tab._set("details", p[1], data)
            } else if (p[0] === "SERIES") {
                tab._done(verb)
                if (data.points !== undefined) tab._set("series", p[1] + "/" + p[2], data.points)
            } else if (p[0] === "COVER") {
                tab._done(verb)
                if (data.cover !== undefined) tab._set("covers", p[1], data.cover)
            }
        }
    }

    Connections {
        target: tab.computerModel
        function onHostSettingResult(idx, command, reply) {
            if (idx !== tab.hostIndex || command.indexOf("DELETESESSION ") !== 0) return
            var id = command.substring(14)
            if (reply === "OK" || reply === "ERR_UNKNOWN_SESSION") { tab._dropLocally(id); tab._refresh(); return }
            deleteFailed.text = reply === "ERR_NO_MANAGEMENT"
                ? qsTr("This device has no Host management on %1. Turn it on in StreamTweak, under Setup › Clients.").arg(tab.hostName)
                : reply === "" ? qsTr("%1 did not answer.").arg(tab.hostName)
                               : qsTr("%1 could not delete the session.").arg(tab.hostName)
            deleteFailed.open()
        }
    }

    // ── Delete (8.2.0) ───────────────────────────────────────────────────────────────
    function askDelete() {
        if (!canDelete || !sel || (zone !== "list" && zone !== "report")) return
        deleteConfirm.sessionId = sel.id
        deleteConfirm.text = qsTr("Delete the session of %1, %2 at %3? It is deleted from StreamTweak on %4 as well.")
                                .arg(Fmt.games(sel)).arg(Fmt.day(sel.start)).arg(Fmt.time(sel.start)).arg(tab.hostName)
        deleteConfirm.open()
    }
    // Gone from the host: out of the list at once, without waiting for the page.
    function _dropLocally(id) {
        var at = -1
        for (var i = 0; i < items.length; i++) if (items[i].id === id) { at = i; break }
        if (at < 0) return
        var it = items.slice(); var mine = it[at].mine === true
        it.splice(at, 1)
        items = it
        total = Math.max(0, total - 1)
        totalAll = Math.max(0, totalAll - 1)
        if (mine) totalMine = Math.max(0, totalMine - 1)
        marked = marked.filter(function(m) { return m !== id })
        if (cur >= items.length) cur = Math.max(0, items.length - 1)
        if (!items.length && zone === "report") zone = "list"
    }

    NavigableMessageDialog {
        id: deleteConfirm
        parent: Overlay.overlay
        property string sessionId: ""
        headerText: qsTr("DELETE SESSION")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: if (sessionId !== "") tab.computerModel.setHostSetting(tab.hostIndex, "DELETESESSION " + sessionId)
    }
    NavigableMessageDialog {
        id: deleteFailed
        parent: Overlay.overlay
        headerText: qsTr("DELETE SESSION")
        standardButtons: Dialog.Ok
    }

    // ── Actions ──────────────────────────────────────────────────────────────────────
    function markIndex(id) { return id ? marked.indexOf(id) : -1 }
    function toggleMark() {
        if (!sel) return
        var m = marked.slice(), i = m.indexOf(sel.id)
        if (i >= 0) m.splice(i, 1)
        else { m.push(sel.id); if (m.length > 2) m.shift() }
        marked = m
    }
    function openCompare() {
        if (marked.length !== 2) return
        _needDetail(marked[0]); _needDetail(marked[1])
        needCover(marked[0]); needCover(marked[1])
        zone = "compare"
    }
    function _cycleTrack(d) { if (tracks.length) trackIndex = (trackIndex + d + tracks.length) % tracks.length }

    // The status bar's prompts for where the pad is — lent through HostSectionScreen.barHints.
    readonly property var barHints: {
        var two = marked.length === 2
        var markAct = sel && markIndex(sel.id) >= 0 ? qsTr("Unmark") : qsTr("Mark to compare")
        if (zone === "filter")
            return [ { btn: "DPADX", key: "← →", act: qsTr("Filter"), kind: "section:filter" },
                     { btn: "B", key: "Esc", act: qsTr("Hosts"), kind: "back" } ]
        if (zone === "compare")
            return [ { btn: "B", key: "Esc", act: qsTr("Sessions"), kind: "section:back" } ]
        if (zone === "report") {
            var r = [ { btn: "LB", key: "Q", act: "", kind: "section:prevTrack" },
                      { btn: "RB", key: "E", act: qsTr("Track"), kind: "section:nextTrack" },
                      { btn: "RSCROLLX", key: "← →", act: qsTr("Cursor"), kind: "none" },
                      { btn: "LT", key: "−", act: "", kind: "section:zoomOut" },
                      { btn: "RT", key: "+", act: qsTr("Zoom"), kind: "section:zoomIn" },
                      { btn: "X", key: "X", act: markAct, kind: "section:mark" } ]
            if (canDelete) r.push({ btn: "RS", key: "Del", act: qsTr("Delete"), kind: "section:delete" })
            if (two) r.push({ btn: "START", key: "C", act: qsTr("Compare"), kind: "section:compare" })
            r.push({ btn: "B", key: "Esc", act: qsTr("List"), kind: "section:back" })
            return r
        }
        var l = []
        if (sel) l.push({ btn: "A", key: "Enter", act: qsTr("Timeline"), kind: "section:open" },
                        { btn: "X", key: "X", act: markAct, kind: "section:mark" })
        if (sel && canDelete) l.push({ btn: "RS", key: "Del", act: qsTr("Delete"), kind: "section:delete" })
        if (two) l.push({ btn: "START", key: "C", act: qsTr("Compare"), kind: "section:compare" })
        l.push({ btn: "LB", key: "PgUp", act: qsTr("Prev tab"), kind: "prevTab" },
               { btn: "RB", key: "PgDn", act: qsTr("Next tab"), kind: "nextTab" },
               { btn: "Y", key: "S", act: qsTr("Settings"), kind: "settings" },
               { btn: "B", key: "Esc", act: qsTr("Hosts"), kind: "back" })
        return l
    }
    function barAction(kind) {
        switch (kind) {
        case "open":      if (sel) zone = "report"; break
        case "mark":      toggleMark(); break
        case "delete":    askDelete(); break
        case "compare":   openCompare(); break
        case "prevTrack": _cycleTrack(-1); break
        case "nextTrack": _cycleTrack(1); break
        case "zoomOut":   zoom = Math.max(1, zoom / 2); break
        case "zoomIn":    zoom = Math.min(8, zoom * 2); break
        case "filter":    filter = filter === "mine" ? "all" : "mine"; break
        case "back":      zone = "list"; break
        }
    }

    // ── Keys ─────────────────────────────────────────────────────────────────────────
    Keys.onPressed: function(e) {
        var k = e.key
        var A = k === Qt.Key_Return || k === Qt.Key_Enter || k === Qt.Key_Space
        var B = k === Qt.Key_Escape
        var X = k === Qt.Key_Menu || k === Qt.Key_X
        var START = k === Qt.Key_F18 || k === Qt.Key_C
        var LB = k === Qt.Key_F16 || k === Qt.Key_Q
        var RB = k === Qt.Key_F17 || k === Qt.Key_E
        var LT = k === Qt.Key_F14 || k === Qt.Key_Minus
        var RT = k === Qt.Key_F15 || k === Qt.Key_Plus || k === Qt.Key_Equal
        var DEL = k === Qt.Key_F19 || k === Qt.Key_Delete

        if (zone === "compare") {
            if (B) { zone = "list"; e.accepted = true }
            return
        }
        if (START && marked.length === 2) { openCompare(); e.accepted = true; return }
        if (X && (zone === "list" || zone === "report")) { toggleMark(); e.accepted = true; return }
        if (DEL && (zone === "list" || zone === "report")) { askDelete(); e.accepted = true; return }

        if (zone === "filter") {
            if (k === Qt.Key_Left || k === Qt.Key_Right) { filter = filter === "mine" ? "all" : "mine"; e.accepted = true }
            else if (k === Qt.Key_Down || k === Qt.Key_Up || A) { if (k !== Qt.Key_Up) zone = "list"; e.accepted = true }
            return
        }
        if (zone === "list") {
            if (k === Qt.Key_Down) {
                if (cur < items.length - 1) cur++
                if (cur >= items.length - 8) _more()
                e.accepted = true
            } else if (k === Qt.Key_Up) {
                if (cur > 0) cur--; else zone = "filter"
                e.accepted = true
            } else if (A && sel) {
                zone = "report"; e.accepted = true
            }
            return
        }
        if (zone === "report") {
            var step = 1 / zoom / 40
            if (k === Qt.Key_Left)       { cursor = Math.max(0, cursor - step); e.accepted = true }
            else if (k === Qt.Key_Right) { cursor = Math.min(1, cursor + step); e.accepted = true }
            else if (LB) { _cycleTrack(-1); e.accepted = true }
            else if (RB) { _cycleTrack(1); e.accepted = true }
            else if (LT) { zoom = Math.max(1, zoom / 2); e.accepted = true }
            else if (RT) { zoom = Math.min(8, zoom * 2); e.accepted = true }
            else if (B)  { zone = "list"; e.accepted = true }
            else if (k === Qt.Key_Up || k === Qt.Key_Down || A) e.accepted = true
        }
    }

    // ── Filter ───────────────────────────────────────────────────────────────────────
    Row {
        id: filterRow
        visible: tab.zone !== "compare"
        spacing: tab._px(8)
        Repeater {
            model: [ ["mine", qsTr("This device"), tab.totalMine], ["all", qsTr("All devices"), tab.totalAll] ]
            Rectangle {
                readonly property bool _on: tab.filter === modelData[0]
                width: fText.implicitWidth + tab._px(30); height: tab._px(36); radius: tab._px(8)
                color: _on ? Theme.accentFill : Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, 0.75)
                border.color: tab.zone === "filter" && _on && tab.activeFocus ? Theme.ink : Theme.line
                border.width: tab.zone === "filter" && _on && tab.activeFocus ? 3 : 1
                Text { id: fText; anchors.centerIn: parent; text: modelData[1] + "  " + modelData[2]
                       color: parent._on ? Theme.onAccent : Theme.text
                       font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall); font.bold: parent._on }
                MouseArea { anchors.fill: parent; onClicked: { tab.filter = modelData[0]; tab.forceActiveFocus() } }
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: tab._px(16)
            text: tab.marked.length === 0 ? ""
                : tab.marked.length === 1 ? qsTr("1 marked · mark one more to compare")
                : qsTr("2 marked · Start compares them")
            color: Theme.inkText
            font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall); font.bold: true
        }
    }

    // ── List ─────────────────────────────────────────────────────────────────────────
    component MarkBadge: Rectangle {
        property int n: -1
        visible: n >= 0
        width: Math.round(22 * Theme.uiScale); height: width; radius: width / 2
        color: Theme.accentFill
        Text { anchors.centerIn: parent; text: parent.n + 1; color: Theme.onAccent
               font.family: Theme.family; font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale * Theme.textScale); font.bold: true }
    }

    ListView {
        id: list
        anchors.top: filterRow.bottom
        anchors.topMargin: tab._px(14)
        anchors.bottom: parent.bottom
        width: Math.round(parent.width * 0.36)
        visible: tab.zone !== "compare"
        clip: true
        model: tab.items
        currentIndex: tab.cur
        spacing: tab._px(6)
        highlightMoveDuration: Theme.reduceAnimations ? 0 : 120
        boundsBehavior: Flickable.StopAtBounds
        onAtYEndChanged: if (atYEnd) tab._more()
        delegate: Rectangle {
            readonly property var s: modelData
            readonly property bool current: index === tab.cur
            width: list.width
            height: tab._px(64)
            radius: tab._px(10)
            color: current ? Qt.rgba(Theme.cardHigh.r, Theme.cardHigh.g, Theme.cardHigh.b, 0.95)
                           : Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, 0.75)
            border.color: current && tab.zone === "list" && tab.activeFocus ? Theme.ink : Theme.line
            border.width: current && tab.zone === "list" && tab.activeFocus ? 3 : 1
            Component.onCompleted: tab.needCover(s.id)
            Rectangle {
                id: rc
                x: tab._px(10); anchors.verticalCenter: parent.verticalCenter
                width: tab._px(36); height: tab._px(48); radius: tab._px(4); color: Theme.cardHigh; clip: true
                Image {
                    anchors.fill: parent
                    readonly property string _b64: tab.covers[s.id] !== undefined ? tab.covers[s.id] : ""
                    visible: _b64.length > 0
                    source: _b64.length ? "data:image/png;base64," + _b64 : ""
                    fillMode: Image.PreserveAspectCrop
                }
            }
            Column {
                anchors.left: rc.right; anchors.leftMargin: tab._px(12)
                anchors.right: gradeTag.left; anchors.rightMargin: tab._px(10)
                anchors.verticalCenter: parent.verticalCenter
                spacing: tab._px(2)
                Text { width: parent.width; elide: Text.ElideRight; text: Fmt.games(s); color: Theme.text
                       font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontBody); font.bold: true }
                Text { width: parent.width; elide: Text.ElideRight
                       text: Fmt.day(s.start) + " · " + Fmt.time(s.start) + " · " + Fmt.duration(s.start, s.end)
                             + (tab.filter === "all" ? " · " + (s.client || qsTr("Unknown client")) : "")
                       color: Theme.text2; font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall) }
            }
            Row {
                id: gradeTag
                anchors.right: parent.right; anchors.rightMargin: tab._px(14)
                anchors.verticalCenter: parent.verticalCenter
                spacing: tab._px(6)
                Rectangle { width: tab._px(9); height: width; radius: width / 2; color: tab._gradeMark(s.grade)
                            anchors.verticalCenter: parent.verticalCenter }
                Text { text: Fmt.gradeName(s.grade); color: tab._gradeColor(s.grade)
                       font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall); font.bold: true }
            }
            MarkBadge { n: tab.markIndex(s.id); x: tab._px(4); y: tab._px(4) }
            MouseArea {
                anchors.fill: parent
                onClicked: { tab.cur = index; tab.zone = "list"; tab.forceActiveFocus() }
                onDoubleClicked: { tab.cur = index; tab.zone = "report"; tab.forceActiveFocus() }
            }
        }
    }

    // Nothing to list: why, in one line.
    Text {
        anchors.centerIn: list
        width: list.width - tab._px(40)
        visible: tab.zone !== "compare" && tab.items.length === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: !tab.available ? qsTr("No sessions to show.")
            : tab.failed ? qsTr("The host did not answer.")
            : tab.loading || tab.filter === "" ? qsTr("Loading the sessions…")
            : tab.filter === "mine" ? qsTr("No sessions from this device yet.")
            : qsTr("No sessions recorded on this host yet.")
        color: Theme.text3
        font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontBody)
    }

    // ── Report ───────────────────────────────────────────────────────────────────────
    SessionReport {
        anchors.top: filterRow.bottom
        anchors.topMargin: tab._px(14)
        anchors.bottom: parent.bottom
        x: list.width + tab._px(24)
        width: parent.width - x
        visible: tab.zone !== "compare" && tab.sel !== null
        detail: tab.detail
        cover: tab.sel && tab.covers[tab.sel.id] !== undefined ? tab.covers[tab.sel.id] : ""
        tracks: tab.tracks
        trackIndex: tab.trackIndex
        points: tab.points
        parts: tab.parts
        cursor: tab.cursor
        zoom: tab.zoom
        active: tab.zone === "report" && tab.activeFocus
        marked: tab.sel !== null && tab.markIndex(tab.sel.id) >= 0
    }

    // ── Compare ──────────────────────────────────────────────────────────────────────
    SessionCompare {
        anchors.fill: parent
        visible: tab.zone === "compare"
        a: tab.marked.length === 2 && tab.details[tab.marked[0]] !== undefined ? tab.details[tab.marked[0]] : null
        b: tab.marked.length === 2 && tab.details[tab.marked[1]] !== undefined ? tab.details[tab.marked[1]] : null
        coverA: tab.marked.length === 2 && tab.covers[tab.marked[0]] !== undefined ? tab.covers[tab.marked[0]] : ""
        coverB: tab.marked.length === 2 && tab.covers[tab.marked[1]] !== undefined ? tab.covers[tab.marked[1]] : ""
    }
}
