import QtQuick 2.15
import Theme 1.0
import SdlGamepadKeyNavigation 1.0
import "sessionfmt.js" as Fmt

/*
 * ── The Library tab of the StreamTweak section (8.0.0) ─────────────────────────────────────
 *
 * What the streaming server lists, read from the couch, and the one action a client can ask
 * for there: Sync and restart. Layout C «Board» of docs/mockups/libreria, chosen on 21/10/2026:
 * the server on top, as the section's other cards, with the action and a count per store; the
 * games below, two centred rows that are only looked at — Sync and restart is the one thing to
 * press here (Marcello, 21/10/2026). Its window is SyncRestartDialog, built as UpdateDialog.
 * On the bridge (StreamTweak 10.0.0):
 *   LIBRARY          the list, the server's state, the job in progress
 *   LIBSYNC          start the job — no argument; signature + Host management (SECURITY.md)
 *
 * The covers are StreamLight's own: the box art the host page shows, from this client's cache or
 * fetched from the server (ComputerModel.libraryCover), never a picture from StreamTweak — its
 * thumbnails went grainy at shelf size (21/10/2026). A game the server does not list has none.
 *
 * ⚠️ The store badges ON A COVER are always the white ones, in both themes: they stand on a dark
 * plate over the picture. Only the badges on the theme's own surfaces take the `_dark` twin.
 */
FocusScope {
    id: tab
    property var computerModel: null
    property int hostIndex: -1
    property bool shown: false
    property bool available: true
    property string hostName: ""

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }
    function _fpx(n) { return Math.round(n * _u * Theme.textScale) }

    // ── What the host answered ───────────────────────────────────────────────────────
    property var lib: ({})
    property bool loaded: false
    property bool failed: false
    property int coverRevision: 0      // bumped when a cover lands, so the shelf asks again

    readonly property var games: lib.games || []
    readonly property var job: lib.job || ({})
    readonly property string phase: job.phase || "idle"
    readonly property string server: lib.server || qsTr("the streaming server")
    readonly property bool hostManagement: lib.host_management === true
    readonly property bool jobRunning: ["scanning", "writing", "stopping", "starting", "waiting"].indexOf(phase) >= 0
    // While the server restarts the host does not answer Moonlight's polls: the section reads
    // this so it does not grey itself out as if the host had gone away.
    readonly property bool restarting: ["stopping", "starting", "waiting"].indexOf(phase) >= 0
    // …and for a little after: Home's poll of the host takes a few seconds to see it back.
    readonly property bool holdOnline: restarting || backGrace.running
    onRestartingChanged: if (!restarting) backGrace.restart()
    Timer { id: backGrace; interval: 15000 }

    // The window: closed · open (ask, refusal, progress or result follow from the job)
    property bool dialogOpen: false
    property string refusal: ""             // the host's ERR_* to LIBSYNC, until the window closes
    property bool _sent: false              // LIBSYNC asked, its first state not back yet
    // A finished job's result is shown once: closing it, or finding it already there when the
    // tab first reads the host, marks it seen. The key is its phase and its length.
    property string _seenEnd: ""
    readonly property string _endKey: phase === "done" || phase === "failed" ? phase + ":" + (job.elapsed_ms || 0) : ""

    readonly property var stores: {
        var m = {}, order = []
        for (var i = 0; i < games.length; i++) {
            if (games[i].state === "off") continue
            var s = games[i].store || qsTr("Other")
            if (m[s] === undefined) { m[s] = 0; order.push(s) }
            m[s]++
        }
        order.sort(function(a, b) { return m[b] - m[a] })
        return order.map(function(s) { return { store: s, count: m[s] } })
    }
    readonly property int listedCount: games.filter(function(g) { return g.state !== "off" }).length
    function _count(st) { return games.filter(function(g) { return g.state === st }).length }

    function storeKey(s) {
        return s === "Steam" ? "steam" : s === "Battle.net" ? "battlenet" : s === "GOG" ? "gog" : s === "Xbox" ? "xbox"
             : s === "Epic Games" ? "epic" : s === "Ubisoft Connect" ? "ubisoft" : s === "EA App" ? "ea" : ""
    }
    // `onPicture`: the badge sits on a cover — always white (see the header).
    function badge(s, onPicture) {
        var k = storeKey(s)
        return k ? "qrc:/res/store_" + k + (Theme.light && !onPicture ? "_dark" : "") + ".svg" : ""
    }
    // "off" = excluded on the host: in its list, kept out of the server at every sync (StreamTweak 10.0).
    function stateLabel(st) { return st === "manual" ? qsTr("Added by hand") : st === "off" ? qsTr("Excluded") : "" }
    function _lastSync() {
        if (!lib.last_sync) return qsTr("never synced")
        var d = new Date(lib.last_sync), now = new Date()
        var today = d.toDateString() === now.toDateString()
        var y = new Date(now.getTime() - 86400000)
        return qsTr("last sync %1").arg(today ? qsTr("today %1").arg(Fmt.time(lib.last_sync))
                                        : d.toDateString() === y.toDateString() ? qsTr("yesterday %1").arg(Fmt.time(lib.last_sync))
                                        : Fmt.day(lib.last_sync) + " " + Fmt.time(lib.last_sync))
    }

    // ── Requests ─────────────────────────────────────────────────────────────────────
    function _ask(cmd) {
        if (!computerModel || hostIndex < 0 || !available) return
        computerModel.requestHostSection(hostIndex, cmd)
    }

    // Every 5 s on screen; every second while something moves — a job, or the window open.
    Timer {
        interval: tab.jobRunning || tab.dialogOpen || tab._sent ? 1000 : 5000
        repeat: true; triggeredOnStart: true
        running: tab.shown && tab.available
        onTriggered: tab._ask("LIBRARY")
    }
    onShownChanged: if (shown) { refusal = ""; failed = false }

    Connections {
        target: tab.computerModel
        function onHostSectionReceived(idx, verb, data) {
            if (idx !== tab.hostIndex) return
            if (verb === "LIBRARY") {
                if (data.v === undefined) { tab.failed = !tab.loaded; return }
                if (!tab.loaded) {
                    var j = data.job || {}
                    if (j.phase === "done" || j.phase === "failed") tab._seenEnd = j.phase + ":" + (j.elapsed_ms || 0)
                }
                tab.lib = data
                tab.loaded = true
                tab.failed = false
                tab._sent = false
            }
        }
        function onLibraryCoverReady(idx) { if (idx === tab.hostIndex) tab.coverRevision++ }
        function onHostSettingResult(idx, command, reply) {
            if (idx !== tab.hostIndex || command !== "LIBSYNC") return
            if (reply === "OK") { tab._sent = true; tab._ask("LIBRARY") }
            else { tab._sent = false; tab.refusal = reply === "" ? "ERR_NO_ANSWER" : reply }
        }
    }

    // ── The window's words ───────────────────────────────────────────────────────────
    // Why the sync cannot start, read from what the host said before asking — or from its refusal.
    readonly property string blockReason: {
        var r = refusal
        if (r === "ERR_NO_MANAGEMENT" || (!r && loaded && !hostManagement))
            return qsTr("This device has no Host management on %1. Turn it on in StreamTweak, under Setup › Clients.").arg(hostName)
        if (r === "ERR_GAME_RUNNING" || (!r && (lib.game_running || "") !== ""))
            return (lib.game_running ? qsTr("%1 is still running on %2.").arg(lib.game_running).arg(hostName)
                                     : qsTr("A game is still running on %1.").arg(hostName))
                   + " " + qsTr("Close it first: restarting %1 would close it.").arg(server)
        if (r === "ERR_STREAMING" || (!r && lib.streaming === true))
            return qsTr("A device is streaming from %1. Try again when the stream has ended.").arg(hostName)
        if (r === "ERR_NO_SERVER" || (!r && loaded && lib.server_state === "missing"))
            return qsTr("StreamTweak found no streaming server it can restart on %1.").arg(hostName)
        if (r === "ERR_BUSY" || (!r && (job.cooldown_s || 0) > 0 && !jobRunning))
            return qsTr("A sync just ran. Try again in %1 s.").arg(Math.max(1, job.cooldown_s || 0))
        if (r === "ERR_NO_ANSWER") return qsTr("%1 did not answer.").arg(hostName)
        if (r) return qsTr("%1 refused the sync.").arg(hostName)
        return ""
    }
    readonly property var steps: [ qsTr("Scanning the host's stores"), qsTr("Writing %1's list").arg(server),
                                   qsTr("Stopping %1").arg(server), qsTr("Starting %1").arg(server),
                                   qsTr("Waiting for %1 to answer").arg(server) ]
    readonly property int stepIndex: ["scanning", "writing", "stopping", "starting", "waiting", "done"].indexOf(phase)
    readonly property string windowMode: refusal !== "" ? "blocked"
                                       : jobRunning || _sent ? "progress"
                                       : _endKey !== "" && _endKey !== _seenEnd ? phase
                                       : blockReason !== "" ? "blocked" : "ask"
    function _resultText() {
        var s = Math.round((job.elapsed_ms || 0) / 1000)
        if (phase === "done") {
            var a = job.added || [], r = job.removed || []
            var parts = []
            parts.push(a.length ? qsTr("%1 added: %2").arg(a.length).arg(a.join(", ")) : qsTr("none added"))
            parts.push(r.length ? qsTr("%1 removed: %2").arg(r.length).arg(r.join(", ")) : qsTr("none removed"))
            return qsTr("Done in %1 s").arg(s) + " · " + parts.join(" · ")
        }
        var e = job.error || ""
        if (e === "streaming") return qsTr("A stream started meanwhile, so %1 was not restarted: the new list shows after its next restart.").arg(server)
        if (e === "no answer") return qsTr("%1 did not answer within a minute. Look at %2: it may need starting by hand.").arg(server).arg(hostName)
        if (e === "start failed") return qsTr("%1 could not be started again. Start it on %2.").arg(server).arg(hostName)
        if (e === "no service") return qsTr("StreamTweak's service is not running on %1.").arg(hostName)
        if (e === "no server" || e === "not allowed") return qsTr("StreamTweak could not restart %1: its service did not pass the checks.").arg(server)
        return e
    }

    function openSync() {
        if (!hostManagement || !loaded) return
        refusal = ""
        dialogOpen = true
        syncDialog.open()
    }
    function confirmSync() {
        if (windowMode !== "ask" || !computerModel || hostIndex < 0) return
        _sent = true
        computerModel.setHostSetting(hostIndex, "LIBSYNC")
    }

    // ── The status bar's prompts (HostSectionScreen.barHints) ────────────────────────
    // While the window is up the scrim covers the bar and the window's own buttons speak.
    readonly property var barHints: {
        var l = []
        if (hostManagement) l.push({ btn: "X", key: "X", act: qsTr("Sync and restart"), kind: "section:sync" })
        if (_shelfScrolls) l.push({ btn: "RSCROLLX", key: "", act: qsTr("Scroll"), kind: "none" })
        l.push({ btn: "LB", key: "PgUp", act: qsTr("Prev tab"), kind: "prevTab" },
               { btn: "RB", key: "PgDn", act: qsTr("Next tab"), kind: "nextTab" },
               { btn: "Y", key: "S", act: qsTr("Settings"), kind: "settings" },
               { btn: "B", key: "Esc", act: qsTr("Hosts"), kind: "back" })
        return l
    }
    function barAction(kind) { if (kind === "sync") openSync() }

    // ── Keys ─────────────────────────────────────────────────────────────────────────
    // One thing to press: the button holds the focus (and takes A itself); X reaches it from
    // anywhere in the tab. The covers are only looked at, so the D-pad has nothing to move here
    // and its keys go on to the section.
    Keys.onPressed: function(e) {
        if ((e.key === Qt.Key_Menu || e.key === Qt.Key_X) && hostManagement) { openSync(); e.accepted = true }
    }

    // ── The server, as the section's other cards ─────────────────────────────────────
    // Same surface, title and rows as Dashboard's cards and Settings: a caption, a row with its
    // title and hint on the left and Settings' pill button on the right, then a line of counts.
    Rectangle {
        id: board
        width: parent.width
        height: boardCol.implicitHeight + tab._px(32)
        radius: tab._px(12)
        color: Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, 0.75)
        border.color: Theme.line
        border.width: 1

        Column {
            id: boardCol
            x: tab._px(20); y: tab._px(16)
            width: parent.width - tab._px(40)
            spacing: tab._px(4)

            Text {
                height: tab._px(30)
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Streaming server")
                color: Theme.text3
                font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall); font.bold: true
                font.capitalization: Font.AllUppercase; font.letterSpacing: 0.8
            }

            Item {
                width: parent.width
                implicitHeight: Math.max(serverTexts.implicitHeight, syncBtn.height, tab._px(58))
                height: implicitHeight
                Column {
                    id: serverTexts
                    anchors.left: parent.left
                    anchors.right: syncBtn.left; anchors.rightMargin: tab._px(16)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: tab._px(3)
                    Text {
                        width: parent.width; elide: Text.ElideRight
                        text: tab.loaded ? (tab.lib.server || qsTr("Not found")) : (tab.failed ? qsTr("No answer") : "…")
                        color: Theme.text
                        font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontBody); font.bold: true
                    }
                    // The state in words, as the Dashboard says things — coloured only when it is
                    // not the normal one.
                    Text {
                        readonly property string _s: tab.restarting ? "restarting"
                                                   : tab.phase === "failed" && (tab.job.error === "no answer" || tab.job.error === "start failed") ? "down"
                                                   : tab.lib.server_state || ""
                        width: parent.width; wrapMode: Text.WordWrap
                        visible: tab.loaded
                        text: (_s === "restarting" ? qsTr("Restarting…") : _s === "stopped" ? qsTr("Stopped")
                               : _s === "down" ? qsTr("Not answering") : _s === "missing" ? qsTr("Not found on this host")
                               : qsTr("Running"))
                              + "  ·  " + (tab.listedCount === 1 ? qsTr("1 game listed") : qsTr("%1 games listed").arg(tab.listedCount))
                              + "  ·  " + tab._lastSync()
                        color: _s === "restarting" ? Theme.warning : _s === "stopped" || _s === "down" ? Theme.danger : Theme.text2
                        font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall)
                    }
                }
                PillButton {
                    id: syncBtn
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    focus: true                 // the tab's one control: the scope hands it the focus
                    text: tab.jobRunning ? (tab.restarting ? qsTr("Restarting…") : qsTr("Syncing…")) : qsTr("Sync and restart")
                    enabled: tab.hostManagement && tab.loaded
                    opacity: enabled ? 1.0 : 0.45
                    onClicked: tab.openSync()
                }
            }

            // The counts: a store's badge and number each, then what is not listed — one line.
            Flow {
                width: parent.width
                spacing: tab._px(18)
                visible: tab.loaded && (tab.stores.length > 0 || countsExtra.text.length > 0)
                Repeater {
                    model: tab.stores
                    Row {
                        spacing: tab._px(6)
                        height: tab._px(30)
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: source !== ""
                            source: tab.badge(modelData.store, false)
                            width: tab._px(18); height: width
                            sourceSize.width: 64; sourceSize.height: 64
                            fillMode: Image.PreserveAspectFit
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.store + "  " + modelData.count
                            color: Theme.text2
                            font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall)
                        }
                    }
                }
                Text {
                    id: countsExtra
                    height: tab._px(30)
                    verticalAlignment: Text.AlignVCenter
                    readonly property var _parts: {
                        var p = []
                        // The host counts every game kept out — the excluded ones in its list and
                        // those removed before StreamTweak 10.0, which have no name to show.
                        var ex = tab.lib.excluded || 0, man = tab._count("manual")
                        if (ex > 0)  p.push(qsTr("%1 excluded").arg(ex))
                        if (man > 0) p.push(qsTr("%1 added by hand").arg(man))
                        return p
                    }
                    visible: text.length > 0
                    text: _parts.join("  ·  ")
                    color: Theme.text3
                    font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall)
                }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                visible: tab.loaded && !tab.hostManagement
                text: qsTr("Read-only · Host management is off for this device in StreamTweak")
                color: Theme.text2
                font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall)
            }
        }
    }

    // ── The shelf: two rows of covers, each centred, scrolling sideways ──────────────
    // The first half of the list on top, the rest below (11 → 6 over 5), and each row centred
    // on its own, so an odd count sits staggered by half a cover. When the rows are wider than the
    // tab the shelf scrolls with the right stick (or the mouse); the covers take no focus.
    readonly property int _topCount: Math.ceil(games.length / 2)
    readonly property int _bottomCount: games.length - _topCount
    function _rowOf(i) { return i < _topCount ? 0 : 1 }
    function _posOf(i) { return i < _topCount ? i : i - _topCount }

    Flickable {
        id: shelf
        anchors.top: board.bottom; anchors.topMargin: tab._px(18)
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width

        readonly property int _gap: tab._px(12)
        // Never bigger than the host page's own cover (GameFacts' maxCoverHeight there, 340):
        // on a tall window the rows stay that size and the room goes above and below them.
        readonly property real coverH: Math.floor(Math.min((height - _gap) / 2, tab._px(340)))
        readonly property real coverW: Math.floor(coverH / 1.5)
        readonly property real cellW: coverW + _gap
        readonly property real _rowsW: Math.max(0, tab._topCount * cellW - _gap)
        // The bottom row's own start, inside the rows' width: half a cover in when it has one less.
        readonly property real bottomX: (_rowsW - Math.max(0, tab._bottomCount * cellW - _gap)) / 2
        // Centred in the tab while it fits; flush with its edges once it scrolls.
        readonly property real rowsX: _rowsW < width ? (width - _rowsW) / 2 : 0
        readonly property real rowsY: Math.max(0, (height - coverH * 2 - _gap) / 2)
        contentWidth: Math.max(width, _rowsW)
        contentHeight: height

        // The cover's left edge in the content, by index.
        function coverX(i) { return rowsX + (tab._rowOf(i) === 0 ? 0 : bottomX) + tab._posOf(i) * cellW }

        Repeater {
            model: tab.games
            delegate: Item {
                id: coverItem
                // The host page's picture (see the header); coverRevision makes it ask again
                // once a download has landed.
                readonly property string _src: tab.coverRevision >= 0 && tab.computerModel && tab.hostIndex >= 0
                                               ? tab.computerModel.libraryCover(tab.hostIndex, modelData.name) : ""
                x: shelf.coverX(index)
                y: shelf.rowsY + (tab._rowOf(index) === 0 ? 0 : shelf.coverH + shelf._gap)
                width: shelf.coverW
                height: shelf.coverH
                readonly property real _r: tab._px(8)
                // No cover yet, or none on the host: a plain tile with the name.
                Rectangle {
                    anchors.fill: parent
                    radius: coverItem._r
                    color: Theme.cardHigh
                    visible: coverItem._src.length === 0
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - tab._px(24)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: modelData.name
                        color: Theme.text2
                        font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontSmall); font.bold: true
                    }
                }
                // The picture as everywhere else in the app: HeroCover's crop and rounded mask, no
                // shadow (a dozen shadow passes for a shelf would cost more than they show).
                HeroCover {
                    anchors.centerIn: parent
                    height: parent.height
                    radius: coverItem._r
                    shadow: false
                    visible: coverItem._src.length > 0
                    source: coverItem._src
                    opacity: modelData.state === "off" ? 0.35 : 1
                }
                // The store, white on a dark plate whatever the theme, in the lower right corner —
                // above the state band when there is one.
                Rectangle {
                    visible: tab.storeKey(modelData.store) !== ""
                    anchors.right: parent.right; anchors.rightMargin: tab._px(6)
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: tab._px(6) + (modelData.state !== "listed" ? tab._px(24) : 0)
                    width: tab._px(24); height: width; radius: tab._px(5)
                    color: Qt.rgba(0, 0, 0, 0.55)
                    Image {
                        anchors.centerIn: parent
                        source: tab.badge(modelData.store, true)
                        width: tab._px(15); height: width
                        sourceSize.width: 64; sourceSize.height: 64
                        fillMode: Image.PreserveAspectFit
                    }
                }
                // Not plainly listed: a band along the foot that follows the cover's lower corners
                // (a taller rounded plate, cut at the band's top).
                Item {
                    visible: modelData.state !== "listed"
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: tab._px(24)
                    clip: true
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: parent.height + coverItem._r
                        radius: coverItem._r
                        color: Qt.rgba(0, 0, 0, 0.6)
                    }
                    Text {
                        anchors.centerIn: parent
                        text: tab.stateLabel(modelData.state)
                        color: "#ffffff"
                        font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontCaption); font.bold: true
                    }
                }
            }
        }
    }
    // The right stick, sideways, scrolls the shelf — as PadScroll does
    // up and down elsewhere. A Timer, not a FrameAnimation: this file imports QtQuick 2.15.
    readonly property bool _shelfScrolls: shelf.contentWidth > shelf.width + 2
    Timer {
        interval: 16
        repeat: true
        running: tab.visible && tab.activeFocus && !tab.dialogOpen && tab._shelfScrolls
                 && SdlGamepadKeyNavigation.scrollVelocityX !== 0
        property double _last: 0
        onRunningChanged: _last = Date.now()
        onTriggered: {
            var now = Date.now()
            var dt = Math.min(0.05, (now - _last) / 1000)
            _last = now
            shelf.contentX = Math.max(0, Math.min(shelf.contentWidth - shelf.width,
                shelf.contentX + SdlGamepadKeyNavigation.scrollVelocityX * dt))
        }
    }
    // Nothing to show: why, in one line.
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: shelf.verticalCenter
        width: parent.width - tab._px(40)
        visible: tab.games.length === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: !tab.available ? qsTr("No games to show.")
            : tab.failed ? qsTr("The host did not answer.")
            : !tab.loaded ? qsTr("Loading the library…")
            : qsTr("No games in the library yet. Sync and restart looks for them in the host's stores.")
        color: Theme.text3
        font.family: Theme.family; font.pixelSize: tab._fpx(Theme.fontBody)
    }

    // ── Sync and restart ─────────────────────────────────────────────────────────────
    SyncRestartDialog {
        id: syncDialog
        hostName: tab.hostName
        server: tab.server
        mode: tab.windowMode
        reason: tab.blockReason
        steamWarning: tab.lib.steam_sync === true
        stepText: tab.stepIndex >= 0 && tab.stepIndex < tab.steps.length ? tab.steps[tab.stepIndex] + "…" : tab.steps[0] + "…"
        stepNumber: tab.stepIndex + 1
        elapsedS: Math.round((tab.job.elapsed_ms || 0) / 1000)
        resultText: tab._resultText()
        onConfirmed: tab.confirmSync()
        // Hide, Cancel, Close and B all end here. The focus goes back to the button in onClosed,
        // after Qt's own hand-over at the end of the exit (§93.13).
        onClosed: {
            if (tab.windowMode === "done" || tab.windowMode === "failed") tab._seenEnd = tab._endKey
            tab.dialogOpen = false
            tab.refusal = ""
            syncBtn.forceActiveFocus()
        }
    }
}
