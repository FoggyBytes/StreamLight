import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts
import QtQuick.Window 2.2

import Theme 1.0
import SdlGamepadKeyNavigation 1.0
import SystemProperties 1.0
import StreamingPreferences 1.0
import AppUpdate 1.0

/*
 * ── The host section (StreamLight 8.0) ───────────────────────────────────────────────────
 *
 * Opened by StreamTweak's mark on the host card. Everything here stays ON THE HOST — the data,
 * the work, the service — and this page is the everyday window onto it from the couch.
 * StreamTweak keeps on the host what is set up once. Plan: docs/streamlight-8.0-streamtweak-
 * 10.0-plan.md, in this repository and in StreamTweak's.
 *
 * Tabs: Dashboard, Sessions (SessionsTab.qml), Library (LibraryTab.qml, with Sync and restart),
 * Apps, NVIDIA Sentinel (only on a host with an NVIDIA GPU), Setup.
 *
 * What travels on the bridge (StreamTweak 10.0.0): one read per tab — DASHBOARD, APPS, NVIDIA,
 * plus STATS for the numbers — and three writes, all switches: SETAUDIO, SETAPP <id>,
 * SETSENTINEL. No path and no command line ever leaves this device. A write needs the device's
 * Host management permission on the host; without it every switch here is greyed and the page
 * says where to grant it.
 *
 * Without StreamTweak at work for this host — switched off, not found, older than 10.0, pending,
 * denied, offline — every tab is still there to look at, greyed, with one line on why at the top
 * and the way to Setup; the section opens on Setup then (21/10/2026).
 */
FocusScope {
    id: root
    anchors.fill: parent
    focus: true

    property var    computerModel: null
    property string hostUuid: ""
    property string hostName: ""
    property var    home: null          // HomeScreen: the host's live record and its actions
    property var    appShell: null

    // The host's row, read back from its uuid whenever the list moves (6.3.1).
    readonly property int hostIndex:
        (computerModel && computerModel.layoutRevision >= 0 && hostUuid.length)
            ? computerModel.indexOfUuid(hostUuid) : -1

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }
    // Text a size smaller than the rest of the app, in one proportion (Theme.textScale, 21/10/2026).
    function _fpx(n) { return Math.round(n * _u * Theme.textScale) }

    // StreamTweak's version (CAPS ver=, "x.y.z") is at least `min`. False when unknown.
    function _versionAtLeast(ver, min) {
        if (!ver) return false
        var a = ver.split("."), b = min.split(".")
        for (var i = 0; i < b.length; i++) {
            var x = parseInt(a[i] || "0"), y = parseInt(b[i])
            if (x !== y) return x > y
        }
        return true
    }

    // The whole width, like Settings (21/10/2026): the margins of Settings, and the content laid
    // out in columns so a label and its value stay close — numbers in a row of five, cards in
    // rows of three — fewer of them as the window narrows.
    readonly property int _side: _px(30)
    // ⚠️ In design units (width / _u), not against _px(): everything here grows with the window,
    // so a threshold in scaled pixels grows with it and is never reached — at 4K and 150 % the
    // numbers sat on three columns however wide the screen (21/10/2026).
    readonly property real _designW: width / _u
    readonly property int _metricCols: _designW >= 1000 ? 5 : _designW >= 640 ? 3 : 2

    // ── The host as the card sees it ───────────────────────────────────────────────────
    // Home's own record (auth, link, online…), re-read every second: the section must follow
    // an approval arriving, a link changing, the host going away — exactly as the card does.
    property var host: null
    function _refreshHost() { host = home ? home.hostRecordFor(hostUuid) : null }

    readonly property string _auth: host ? host.auth : ""
    // A server restart asked from the Library tab takes the host off Moonlight's polls for a
    // minute: the section knows why and keeps its tabs, rather than greying itself as offline.
    readonly property bool   _online: (host ? host.online === true || !!host.serverRestart : false) || libraryTab.holdOnline
    readonly property bool   _access: _online && _auth === "authorized"
    // The tabs that read the host need StreamTweak 10.0.0 too; without it they are greyed.
    readonly property bool   _full: _access && !!host && host.hostSection === true && host.stEnabled !== false

    // ── What the host answered ─────────────────────────────────────────────────────────
    property var dash:  ({})
    property var apps:  ({})
    property var nv:    ({})
    property bool _dashLoaded: false
    property bool _appsLoaded: false
    property bool _nvLoaded:   false
    // UPDATESTATE: {"pending": bool}, updates waiting for a restart on the host. Empty when unknown.
    property var upd: ({})

    // Host management for THIS device, as the last reply said. Unknown counts as no: a switch
    // that turns out to be refused is worse than one that waits a second to light up.
    readonly property bool hostManagement:
        dash.host_management === true || apps.host_management === true || nv.host_management === true

    // Writes not yet confirmed by a fresh read, so a switch does not flick back while the host
    // applies it: key → value.
    property var _pending: ({})
    property var _pendingAt: ({})
    function _shown(key, value) { return _pending[key] !== undefined ? _pending[key] : value }

    // The numbers of the Dashboard, last 60 s kept here (nothing is stored on the host).
    property var stats: ({})
    property bool _statsLoaded: false
    // Encoder and network out were here and went (20/10/2026): at rest they sit at zero. System
    // RAM took their place (StreamTweak 10.0.0 sends it in STATS).
    property var _hist: ({ gpu: [], gpu_temp: [], vram_used: [], cpu: [], ram_used: [] })
    property int _histRev: 0

    // ── Tabs ───────────────────────────────────────────────────────────────────────────
    // Every tab is always listed; without StreamTweak at work they are greyed (`dim`) but can be
    // visited, and say why at the top. NVIDIA Sentinel follows the host's answer once there is
    // one, and before that the GPU the pairing reported — kept when that is unknown.
    readonly property bool _nvidiaHost: _dashLoaded ? dash.nvidia === true
        : !(host && host.gpuModel) || String(host.gpuModel).toUpperCase().indexOf("NVIDIA") >= 0
    readonly property var _tabs: {
        var d = !_full
        var t = [ { key: "dashboard", label: qsTr("Dashboard"), dim: d },
                  { key: "sessions",  label: qsTr("Sessions"),  dim: d },
                  { key: "library",   label: qsTr("Library"),   dim: d },
                  { key: "apps",      label: qsTr("Apps"),      dim: d } ]
        if (_nvidiaHost) t.push({ key: "nvidia", label: qsTr("NVIDIA Sentinel"), dim: d })
        t.push({ key: "setup", label: qsTr("Setup") })
        return t
    }
    readonly property string _tab: tabBar.currentIndex >= 0 && tabBar.currentIndex < _tabs.length
                                   ? _tabs[tabBar.currentIndex].key : "dashboard"
    readonly property bool _dimTab: !_full && _tab !== "setup"
    function _setupIndex() { return _tabs.length - 1 }

    // Why the greyed tabs have nothing to show, in one line — the same states as Setup's.
    readonly property string _lockReason: {
        if (!_online)                         return qsTr("%1 is offline.").arg(hostName)
        if (setupPage.presence === "missing") return qsTr("StreamTweak was not found on %1.").arg(hostName)
        if (host && host.stEnabled === false) return qsTr("StreamTweak is off for %1 on this device.").arg(hostName)
        if (_auth === "pending")              return qsTr("Waiting for approval on the host.")
        if (_auth === "denied")               return qsTr("Access denied on the host.")
        if (_auth === "authorized" && host && host.hostSection !== true)
                                              return qsTr("These tabs need StreamTweak 10.0 on the host.")
        if (setupPage.presence === "")        return qsTr("Checking for StreamTweak on %1…").arg(hostName)
        return qsTr("Connecting to StreamTweak…")
    }

    // The section opens on Setup when the other tabs would be greyed: decided once, on the
    // first record of the host (bindings settle first, hence callLater).
    property bool _openingDecided: false
    onHostChanged: if (!_openingDecided && host) {
        _openingDecided = true
        Qt.callLater(function() { if (!root._full) tabBar.currentIndex = root._setupIndex() })
    }
    onHomeChanged: _refreshHost()
    // StreamTweak arriving or leaving while a tab is up moves the line on top in or out: the
    // focus follows, so the pad is never left on something that went away.
    on_DimTabChanged: Qt.callLater(function() { if (root.activeFocus) root.focusFirst() })

    // ── Requests ───────────────────────────────────────────────────────────────────────
    function _ask(verb) {
        if (!computerModel || hostIndex < 0 || !_full) return
        computerModel.requestHostSection(hostIndex, verb)
    }
    function _set(key, command, value) {
        if (!computerModel || hostIndex < 0) return
        var p = Object.assign({}, _pending); p[key] = value; _pending = p
        var t = Object.assign({}, _pendingAt); t[key] = Date.now(); _pendingAt = t
        computerModel.setHostSetting(hostIndex, command)
    }
    // A read clears the writes it can already reflect: those sent more than a moment ago. A
    // poll that crosses a write in flight would otherwise flick the switch back for a second.
    function _clearPending(prefix) {
        var p = {}, now = Date.now()
        for (var k in _pending)
            if (k.indexOf(prefix) !== 0 || now - (_pendingAt[k] || 0) < 1200) p[k] = _pending[k]
        _pending = p
    }

    function _pushHist(key, v) {
        var a = _hist[key]
        a.push(v)
        if (a.length > 60) a.shift()
    }

    Connections {
        target: root.computerModel
        function onHostSectionReceived(idx, verb, data) {
            if (idx !== root.hostIndex) return
            if (verb === "DASHBOARD") {
                root.dash = data; root._dashLoaded = true; root._clearPending("audio")
            } else if (verb === "APPS") {
                root.apps = data; root._appsLoaded = true; root._clearPending("app:")
            } else if (verb === "UPDATESTATE") {
                root.upd = data
            } else if (verb === "NVIDIA") {
                root.nv = data; root._nvLoaded = true; root._clearPending("sentinel")
            } else if (verb === "STATS") {
                root.stats = data
                // The first answer can arrive cold, while the host is still discovering its
                // counters (every field -1): it is waited out, not drawn as "not available".
                if (data.gpu !== undefined && (data.gpu >= 0 || data.cpu >= 0)) {
                    root._statsLoaded = true
                    for (var k in root._hist)
                        root._pushHist(k, data[k] !== undefined ? data[k] : -1)
                    root._histRev++
                }
            }
        }
        function onHostSettingResult(idx, command, reply) {
            if (idx !== root.hostIndex) return
            if (reply !== "OK") {
                root.appShell && root.appShell.notice(root._refusal(reply))
                root._pending = ({})
            }
            // Read the tab back either way: the host is the only source of truth, and a write
            // it applied on its UI thread shows up a moment later.
            settleTimer.verb = command.indexOf("SETAPP") === 0 ? "APPS"
                             : command.indexOf("SETSENTINEL") === 0 ? "NVIDIA" : "DASHBOARD"
            settleTimer.restart()
        }
    }

    function _refusal(reply) {
        if (reply === "ERR_NO_MANAGEMENT")
            return qsTr("This device has no Host management on %1. Turn it on in StreamTweak, under Setup › Clients.").arg(hostName)
        if (reply === "ERR_NO_PROFILE")  return qsTr("Save your profile first: Save current as my profile, in StreamTweak on the host.")
        if (reply === "ERR_UNKNOWN_APP") return qsTr("That app is no longer in the host's list.")
        if (reply === "ERR_UNSUPPORTED") return qsTr("The host cannot do that.")
        if (reply === "")                return qsTr("%1 did not answer.").arg(hostName)
        return qsTr("%1 refused the change.").arg(hostName)
    }

    Timer {
        id: settleTimer
        property string verb: "DASHBOARD"
        interval: 500
        onTriggered: root._ask(verb)
    }

    // The host's record, every second.
    Timer {
        interval: 1000; repeat: true; running: true; triggeredOnStart: true
        onTriggered: root._refreshHost()
    }
    // STATS once a second, only while the Dashboard is on screen (plan: "una richiesta al
    // secondo solo mentre la scheda è visibile").
    Timer {
        interval: 1000; repeat: true; triggeredOnStart: true
        running: root._full && root._tab === "dashboard"
        onTriggered: root._ask("STATS")
    }
    // The rest of the Dashboard every few seconds: streaming state and spatial audio change
    // slowly, and the audio read enumerates devices on the host.
    Timer {
        interval: 5000; repeat: true; triggeredOnStart: true
        running: root._full
        onTriggered: { root._ask("DASHBOARD"); root._ask("UPDATESTATE") }
    }
    Timer {
        interval: 10000; repeat: true; triggeredOnStart: true
        running: root._full && root._tab === "apps"
        onTriggered: root._ask("APPS")
    }
    Timer {
        interval: 10000; repeat: true; triggeredOnStart: true
        running: root._full && root._tab === "nvidia"
        onTriggered: root._ask("NVIDIA")
    }

    // ── Keys ───────────────────────────────────────────────────────────────────────────
    // LB/RB (F16/F17) and PgUp/PgDn switch tabs, wrapping as in Settings. Up/Down walk the
    // controls of the tab in order; A (Return) or Space flips the focused switch. B/Esc is left
    // to the shell, which goes back to the hosts.
    Keys.onPressed: function(event) {
        // S: Settings, as the status bar says (Y on the pad reaches main.qml by itself).
        if (event.key === Qt.Key_S && event.modifiers === Qt.NoModifier) {
            if (root.appShell) root.appShell.openSettings()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_PageUp || event.key === Qt.Key_F16) {
            tabBar.cycle(-1); event.accepted = true
        } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_F17) {
            tabBar.cycle(1); event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            event.accepted = root._moveFocus(1)
        } else if (event.key === Qt.Key_Up) {
            event.accepted = root._moveFocus(-1)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            // Only an Off/On strip (a SegmentedSelector, which has `labels`): a button takes its
            // own Return and Space before they get here.
            var f = Window.activeFocusItem
            if (f && f.labels !== undefined && f.checked !== undefined && f.enabled) {
                f.toggled(!f.checked)
                event.accepted = true
            }
        }
    }

    // The focusable controls of the current tab, in reading order.
    function _focusables() {
        var page = _tab === "apps" ? appsPage : _tab === "nvidia" ? nvPage
                 : _tab === "setup" ? setupPage : dashPage
        var out = _dimTab ? [ lockRow.button ] : []
        function walk(item) {
            if (!item || !item.visible) return
            if (item.activeFocusOnTab === true && item.enabled) { out.push(item); return }
            for (var i = 0; i < item.children.length; i++) walk(item.children[i])
        }
        walk(page)
        return out
    }
    /*
     * One D-pad step. Most of this page is text — the numbers, the saved profile's long list —
     * with a control here and there, so moving only from control to control left everything
     * between them out of reach of the pad, and a press past the last control did nothing
     * (20/10/2026). Now a step goes to the next control when it is on screen or one step away,
     * and otherwise scrolls the page by most of a screen towards it — or towards the end, when
     * there is no control left that way.
     */
    function _scrollBy(dy) {
        var maxY = Math.max(0, contentFlick.contentHeight - contentFlick.height)
        var y = Math.max(0, Math.min(maxY, contentFlick.contentY + dy))
        if (Math.abs(y - contentFlick.contentY) < 1) return false
        contentFlick.contentY = y
        return true
    }
    function _moveFocus(dir) {
        var list = _focusables()
        var cur = Window.activeFocusItem
        var at = -1
        for (var i = 0; i < list.length; i++) {
            var p = cur
            while (p) { if (p === list[i]) { at = i; break } p = p.parent }
            if (at >= 0) break
        }
        var next = at < 0 ? (dir > 0 ? 0 : list.length - 1) : at + dir
        var step = Math.round(contentFlick.height * 0.6)
        if (next >= 0 && next < list.length) {
            var it = list[next]
            var r = it.mapToItem(contentFlick.contentItem, 0, 0, it.width, it.height)
            var top = contentFlick.contentY, bottom = top + contentFlick.height
            var reachable = dir > 0 ? (r.y + r.height <= bottom + step) : (r.y >= top - step)
            if (reachable) { it.forceActiveFocus(); return true }
        }
        _scrollBy(dir * step)
        return true
    }
    function focusFirst() {
        // A greyed tab: its controls are disabled, the way to Setup is the one thing to press.
        if (_dimTab) { lockRow.button.forceActiveFocus(); return }
        // Sessions holds its own cursor (list, timeline): the tab takes the focus as a whole.
        if (_tab === "sessions") { sessionsTab.forceActiveFocus(); return }
        if (_tab === "library") { libraryTab.forceActiveFocus(); return }
        var list = _focusables()
        if (list.length) list[0].forceActiveFocus()
        else contentFlick.forceActiveFocus()
    }

    onActiveFocusChanged: if (activeFocus) Qt.callLater(focusFirst)

    // ── Pieces ─────────────────────────────────────────────────────────────────────────

    // A card: the same surface as the cards of Settings. `icon` names a picture in res/ drawn
    // before the title — "net" gives net.svg in Dark and net_dark.svg in Light; `winMark` draws
    // the Windows mark instead, the one Options and the Power dialog use.
    component Card: Rectangle {
        id: card
        default property alias content: inner.data
        property string title: ""
        property string icon: ""
        property bool winMark: false
        readonly property bool isCard: true
        width: parent ? parent.width : 0
        implicitHeight: inner.implicitHeight + Math.round(32 * Theme.uiScale)
        height: implicitHeight
        radius: Math.round(12 * Theme.uiScale)
        color: Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, 0.75)
        border.color: Theme.line
        border.width: 1
        Column {
            id: inner
            x: Math.round(20 * Theme.uiScale)
            y: Math.round(16 * Theme.uiScale)
            width: parent.width - Math.round(40 * Theme.uiScale)
            spacing: Math.round(4 * Theme.uiScale)
            Row {
                visible: card.title.length > 0
                spacing: Math.round(10 * Theme.uiScale)
                height: Math.round(30 * Theme.uiScale)
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: card.icon.length > 0
                    width: Math.round(20 * Theme.uiScale); height: width
                    source: card.icon.length ? "qrc:/res/" + card.icon + (Theme.light ? "_dark" : "") + ".svg" : ""
                    sourceSize.width: 64; sourceSize.height: 64
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                Grid {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: card.winMark
                    columns: 2
                    spacing: Math.round(2 * Theme.uiScale)
                    Repeater {
                        model: ["#6CD2FE", "#4ACFFF", "#38C0FF", "#20AEFF"]
                        Rectangle {
                            width: Math.round(8 * Theme.uiScale); height: width
                            color: modelData
                        }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: card.title
                    color: Theme.text3
                    font.family: Theme.family
                    font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale * Theme.textScale)
                    font.bold: true
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.8
                }
            }
        }
    }

    // A line of text in a card.
    component Line: Text {
        property bool dim: false
        width: parent ? parent.width : 0
        wrapMode: Text.WordWrap
        color: dim ? Theme.text2 : Theme.text
        font.family: Theme.family
        font.pixelSize: Math.round((dim ? Theme.fontSmall : Theme.fontBody) * Theme.uiScale * Theme.textScale)
    }

    // Settings' row: a title in bold and its hint on the left, the control on the right, 58 tall.
    // SwitchRow's control is an Off/On strip — the focusable item the D-pad walks to. `iconData`
    // is a base64 PNG (an app's icon) drawn before the title.
    component SwitchRow: Item {
        id: sr
        property string title: ""
        property string hint: ""
        property string iconData: ""
        property bool checked: false
        property bool live: true
        signal toggled(bool value)
        width: parent ? parent.width : 0
        implicitHeight: Math.max(texts.implicitHeight, sel.height, Math.round(58 * Theme.uiScale))
        height: implicitHeight
        Image {
            id: appIcon
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            visible: sr.iconData.length > 0
            width: visible ? Math.round(32 * Theme.uiScale) : 0
            height: width
            source: sr.iconData.length ? "data:image/png;base64," + sr.iconData : ""
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
        Column {
            id: texts
            anchors.left: appIcon.right
            anchors.leftMargin: appIcon.visible ? Math.round(14 * Theme.uiScale) : 0
            anchors.right: sel.left
            anchors.rightMargin: Math.round(16 * Theme.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(3 * Theme.uiScale)
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: sr.title
                color: Theme.text
                font.family: Theme.family
                font.pixelSize: Math.round(Theme.fontBody * Theme.uiScale * Theme.textScale)
                font.bold: true
            }
            Text {
                width: parent.width
                visible: text.length > 0
                wrapMode: Text.WordWrap
                text: sr.hint
                color: Theme.text2
                font.family: Theme.family
                font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale * Theme.textScale)
            }
        }
        OnOffSelector {
            id: sel
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            checked: sr.checked
            enabled: sr.live
            opacity: sr.live ? 1.0 : 0.45
            onToggled: function(value) { sr.toggled(value) }
        }
        // ⚠️ No focus ring of our own: SegmentedSelector draws its focus as its own 3 px border,
        // and a second ring here showed as a double outline (20/10/2026).
    }

    // The same row with a small button on the right — Settings' PillButton, not the big dialog
    // button, which looked out of place on a row (21/10/2026).
    component ButtonRow: Item {
        id: br
        property string title: ""
        property string hint: ""
        property string buttonText: ""
        property bool live: true
        readonly property alias button: pill
        signal activated()
        width: parent ? parent.width : 0
        implicitHeight: Math.max(btexts.implicitHeight, pill.height, Math.round(58 * Theme.uiScale))
        height: implicitHeight
        Column {
            id: btexts
            anchors.left: parent.left
            anchors.right: pill.left
            anchors.rightMargin: Math.round(16 * Theme.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(3 * Theme.uiScale)
            Text {
                width: parent.width
                visible: text.length > 0
                wrapMode: Text.WordWrap
                text: br.title
                color: Theme.text
                font.family: Theme.family
                font.pixelSize: Math.round(Theme.fontBody * Theme.uiScale * Theme.textScale)
                font.bold: true
            }
            Text {
                width: parent.width
                visible: text.length > 0
                wrapMode: Text.WordWrap
                text: br.hint
                color: Theme.text2
                font.family: Theme.family
                font.pixelSize: Math.round(Theme.fontSmall * Theme.uiScale * Theme.textScale)
            }
        }
        PillButton {
            id: pill
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: br.buttonText
            enabled: br.live
            opacity: br.live ? 1.0 : 0.45
            onClicked: br.activated()
        }
    }

    // A label and a value on one line — Settings' row with a value where the control would be.
    component Pair: Item {
        property string label: ""
        property string value: ""
        width: parent ? parent.width : 0
        implicitHeight: Math.max(l.implicitHeight, v.implicitHeight, Math.round(44 * Theme.uiScale))
        height: implicitHeight
        Text {
            id: l
            anchors.left: parent.left
            anchors.right: v.left
            anchors.rightMargin: Math.round(12 * Theme.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: parent.label
            color: Theme.text
            font.family: Theme.family
            font.pixelSize: Math.round(Theme.fontBody * Theme.uiScale * Theme.textScale)
            font.bold: true
        }
        Text {
            id: v
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width * 0.6)
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
            text: parent.value
            color: Theme.text2
            font.family: Theme.family
            font.pixelSize: Math.round(Theme.fontBody * Theme.uiScale * Theme.textScale)
        }
    }

    // ── Header ─────────────────────────────────────────────────────────────────────────
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: root._px(24)
        anchors.leftMargin: root._side
        anchors.rightMargin: root._px(30)
        // 56: below the shell's clock, as in Settings.
        height: root._px(56)

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: root._px(14)
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: root._px(38); height: root._px(38)
                source: "qrc:/res/streamtweak_logo.png"
                sourceSize.width: 128; sourceSize.height: 128
                fillMode: Image.PreserveAspectFit
                smooth: true; mipmap: true
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("StreamTweak")
                color: Theme.text
                font.family: Theme.family
                font.pixelSize: root._fpx(Theme.fontH1)
                font.bold: true
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: root._px(3)
                text: root.hostName
                color: Theme.text2
                font.family: Theme.family
                font.pixelSize: root._fpx(Theme.fontTitle)
            }
        }
    }

    // ── Tabs ───────────────────────────────────────────────────────────────────────────
    SectionTabBar {
        id: tabBar
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root._side
        anchors.rightMargin: root._side
        anchors.topMargin: root._px(8)
        height: root._px(48)
        u: root._u
        tabs: root._tabs
        onCurrentIndexChanged: {
            tabSlide.to(currentIndex)
            contentFlick.contentY = 0
            Qt.callLater(root.focusFirst)
        }
    }

    // The line on top of a greyed tab: why it is empty, and the way to Setup. Outside the page,
    // so it stays at full strength while the page under it is dimmed.
    Card {
        id: lockCard
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root._side
        anchors.rightMargin: root._side
        anchors.topMargin: root._px(16)
        visible: root._dimTab
        title: qsTr("Not available")
        ButtonRow {
            id: lockRow
            title: root._lockReason
            buttonText: qsTr("Open Setup")
            onActivated: tabBar.currentIndex = root._setupIndex()
        }
    }

    PadScroll { flick: contentFlick }

    // The Sessions tab: a list and a report side by side that fill the page, so it stands beside
    // the scrolling column of the other tabs rather than inside it.
    SessionsTab {
        id: sessionsTab
        anchors.top: lockCard.visible ? lockCard.bottom : tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root._side
        anchors.rightMargin: root._side
        anchors.topMargin: root._px(16)
        anchors.bottomMargin: root._px(20)
        visible: root._tab === "sessions"
        shown: visible && root._full
        available: root._full
        enabled: root._full
        computerModel: root.computerModel
        hostIndex: root.hostIndex
        hostName: root.hostName
        // 8.2.0: delete a session — Host management, and a host that knows DELETESESSION.
        canDelete: root.hostManagement
                   && root._versionAtLeast(root.host && root.host.hostVersion ? root.host.hostVersion : "", "10.0.6")
        opacity: tabSlide.opacity * (root._full ? 1 : 0.4)
        transform: tabSlide.shift
    }

    // The status bar's prompts while Sessions is up: its own, as the pad does different things
    // in its list, its timeline and its compare view. AppShell reads these (currentPage 3) and
    // hands a click on one of them back here.
    // The Library tab: the server's board and one shelf of covers, the same way beside the column.
    LibraryTab {
        id: libraryTab
        anchors.top: lockCard.visible ? lockCard.bottom : tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root._side
        anchors.rightMargin: root._side
        anchors.topMargin: root._px(16)
        anchors.bottomMargin: root._px(20)
        visible: root._tab === "library"
        shown: visible && root._full
        available: root._full
        enabled: root._full
        computerModel: root.computerModel
        hostIndex: root.hostIndex
        hostName: root.hostName
        opacity: tabSlide.opacity * (root._full ? 1 : 0.4)
        transform: tabSlide.shift
    }

    readonly property var barHints: !_full ? null : _tab === "sessions" ? sessionsTab.barHints
                                  : _tab === "library" ? libraryTab.barHints : null
    function barAction(kind) {
        if (!_full) return
        if (_tab === "sessions") sessionsTab.barAction(kind)
        else if (_tab === "library") libraryTab.barAction(kind)
    }
    TabSlide { id: tabSlide }

    Flickable {
        id: contentFlick
        anchors.top: lockCard.visible ? lockCard.bottom : tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root._side
        anchors.rightMargin: root._side
        anchors.topMargin: root._px(16)
        anchors.bottomMargin: root._px(20)
        visible: root._tab !== "sessions" && root._tab !== "library"
        clip: true
        boundsBehavior: Flickable.OvershootBounds
        contentWidth: width
        contentHeight: (root._tab === "apps" ? appsPage : root._tab === "nvidia" ? nvPage
                       : root._tab === "setup" ? setupPage : dashPage).height
        Keys.forwardTo: [root]

        // Keep the focused control in view as the D-pad walks.
        property Item focusItem: Window.activeFocusItem
        onFocusItemChanged: {
            if (!focusItem) return
            var p = focusItem, inside = false
            while (p) { if (p === contentFlick) { inside = true; break } p = p.parent }
            if (!inside) return
            // The whole card when it fits, so its title is not left cut off above the control;
            // the control alone when the card is taller than the view.
            var target = focusItem, q = focusItem
            while (q) {
                if (q.isCard === true) {
                    if (q.height + root._px(48) <= height) target = q
                    break
                }
                q = q.parent
            }
            var r = target.mapToItem(contentFlick.contentItem, 0, 0, target.width, target.height)
            var m = root._px(24)
            if (r.y - m < contentY) contentY = Math.max(0, r.y - m)
            else if (r.y + r.height + m > contentY + height)
                contentY = Math.min(Math.max(0, contentHeight - height), r.y + r.height + m - height)
        }

        Item {
            width: contentFlick.width
            height: contentFlick.contentHeight
            opacity: tabSlide.opacity
            transform: tabSlide.shift

            // ── Dashboard ──────────────────────────────────────────────────────────────
            // The numbers on one row, then one card per subject stacked like Settings' (21/10/2026).
            Column {
                id: dashPage
                width: parent.width
                spacing: root._px(16)
                visible: root._tab === "dashboard"
                enabled: root._full
                opacity: root._full ? 1 : 0.4

                readonly property var _streaming: root.dash.streaming || ({})
                readonly property var _audio: root.dash.audio || ({})

                Line {
                    text: qsTr("Read-only · Host management is off for this device in StreamTweak")
                    dim: true
                    visible: root._dashLoaded && !root.hostManagement
                }

                /*
                 * Streaming to another device right now.
                 *
                 * 8.2.0: only ANOTHER device. Whoever is looking at this section is not streaming
                 * from here, so "Streaming to this device" only ever showed the stream just ended —
                 * StreamTweak keeps a session open for 30 s after the disconnect, and the card read
                 * it as a stream (28/10/2026, §98.7). StreamTweak 10.0.6 says `active` only while a
                 * stream is live; from an older host an unnamed device is that same grace period
                 * once the telemetry name has aged out, so it is not shown either (a Moonlight
                 * client, which sends no telemetry, then goes unseen there).
                 */
                readonly property bool _liveFlag: root._versionAtLeast(root.host && root.host.hostVersion
                                                                       ? root.host.hostVersion : "", "10.0.6")
                Rectangle {
                    width: parent.width
                    visible: dashPage._streaming.active === true && !streamingText._me
                             && (streamingText._who.length > 0 || dashPage._liveFlag)
                    height: visible ? streamingText.implicitHeight + root._px(24) : 0
                    radius: root._px(10)
                    color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.12)
                    border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.45)
                    Text {
                        id: streamingText
                        x: root._px(16); width: parent.width - root._px(32)
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.WordWrap
                        readonly property string _who: dashPage._streaming.client || ""
                        readonly property bool _me: _who.length > 0 && _who === SystemProperties.clientName()
                        readonly property string _game: dashPage._streaming.game || ""
                        text: (_who.length ? qsTr("Streaming to %1").arg(_who)
                                           : qsTr("Streaming to another device"))
                              + (_game.length ? " · " + _game : "")
                        color: Theme.text
                        font.family: Theme.family
                        font.pixelSize: root._fpx(Theme.fontBody)
                    }
                }

                // The numbers, from STATS, each with its last minute on two labelled axes: the
                // value on the left (0, half, full scale) and the time underneath. The scale is
                // fixed — 100 % or 100 °C, memory to its installed size in whole GB.
                GridLayout {
                    width: parent.width
                    columns: root._metricCols
                    uniformCellWidths: true
                    columnSpacing: root._px(12)
                    rowSpacing: root._px(12)

                    Repeater {
                        model: [
                            { key: "gpu",       label: qsTr("GPU load"),        unit: "%",  max: 100 },
                            { key: "gpu_temp",  label: qsTr("GPU temp"),        unit: "°C", max: 100 },
                            { key: "vram_used", label: qsTr("VRAM"),            unit: "GB", max: 0 },
                            { key: "cpu",       label: qsTr("CPU"),             unit: "%",  max: 100 },
                            { key: "ram_used",  label: qsTr("RAM"),             unit: "GB", max: 0 }
                        ]
                        Rectangle {
                            id: tile
                            Layout.fillWidth: true
                            Layout.preferredHeight: root._px(128)
                            radius: root._px(12)
                            color: Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, 0.75)
                            border.color: Theme.line

                            readonly property string unit: modelData.unit
                            readonly property real _raw: root.stats[modelData.key] !== undefined
                                                         ? root.stats[modelData.key] : -1
                            // Memory's total rides next to its value: vram_total, ram_total (MB).
                            readonly property real _totalMb: modelData.unit === "GB"
                                ? (root.stats[modelData.key.replace("_used", "_total")] || 0) : 0
                            // ⚠️ Installed memory in whole GB: Windows reports a little less than the
                            // modules (31.8 for 32), and the scale names what is installed.
                            readonly property int _totalGb: _totalMb > 0 ? Math.round(_totalMb / 1024) : 0
                            readonly property string _value: {
                                if (!root._statsLoaded) return root._full ? "…" : "—"
                                if (_raw < 0) return "—"
                                if (modelData.unit === "GB")
                                    return (_raw / 1024).toFixed(1) + (_totalGb > 0 ? " / " + _totalGb : "")
                                return Math.round(_raw)
                            }
                            // The top of the value axis, in the unit shown.
                            readonly property real _top: modelData.max > 0 ? modelData.max : _totalGb
                            function _axis(f) {
                                if (_top <= 0) return f === 0 ? "0" : ""
                                var v = _top * f
                                return v % 1 !== 0 ? v.toFixed(1) : String(v)
                            }

                            // Label and value on one line, to leave the height to the chart.
                            Text {
                                x: root._px(12); y: root._px(10)
                                text: modelData.label
                                color: Theme.text2
                                font.family: Theme.family
                                font.pixelSize: root._fpx(Theme.fontCaption)
                            }
                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: root._px(12)
                                y: root._px(6)
                                spacing: root._px(4)
                                Text {
                                    id: valueText
                                    text: tile._value
                                    color: Theme.text
                                    font.family: Theme.family
                                    font.pixelSize: root._fpx(Theme.fontBody)
                                    font.bold: true
                                }
                                Text {
                                    anchors.baseline: valueText.baseline
                                    visible: tile._raw >= 0 && root._statsLoaded
                                    text: modelData.unit
                                    color: Theme.text2
                                    font.family: Theme.family
                                    font.pixelSize: root._fpx(Theme.fontCaption)
                                }
                            }

                            // ── The chart: value axis on the left, time axis underneath ──
                            Item {
                                id: plotArea
                                x: root._px(12) + axisW
                                y: root._px(36)
                                width: parent.width - x - root._px(12)
                                height: parent.height - y - root._px(22)
                                readonly property int axisW: root._px(30)

                                Repeater {
                                    model: [1, 0.5, 0]
                                    Text {
                                        x: -plotArea.axisW
                                        width: plotArea.axisW - root._px(6)
                                        y: plotArea.height * (1 - modelData) - height / 2
                                        horizontalAlignment: Text.AlignRight
                                        text: tile._axis(modelData) + (modelData === 1 && tile._top > 0 ? " " + tile.unit : "")
                                        visible: text.length > 0
                                        color: Theme.text3
                                        font.family: Theme.family
                                        font.pixelSize: root._fpx(Theme.fontCaption - 1)
                                    }
                                }
                                Repeater {
                                    model: [ { f: 0, t: qsTr("60 s") }, { f: 0.5, t: qsTr("30 s") }, { f: 1, t: qsTr("now") } ]
                                    Text {
                                        y: plotArea.height + root._px(3)
                                        x: modelData.f === 0 ? 0
                                         : modelData.f === 1 ? plotArea.width - width
                                         : plotArea.width / 2 - width / 2
                                        text: modelData.t
                                        color: Theme.text3
                                        font.family: Theme.family
                                        font.pixelSize: root._fpx(Theme.fontCaption - 1)
                                    }
                                }

                                Canvas {
                                    anchors.fill: parent
                                    readonly property int _rev: root._histRev
                                    readonly property real _scale: tile._top
                                    on_RevChanged: requestPaint()
                                    on_ScaleChanged: requestPaint()
                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()
                                        ctx.strokeStyle = Theme.line
                                        ctx.lineWidth = 1
                                        for (var g = 0; g <= 2; g++) {
                                            var gy = g === 2 ? height - 0.5 : Math.round(height * g / 2) + 0.5
                                            ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke()
                                        }
                                        var a = root._hist[modelData.key]
                                        if (!a || a.length < 2) return
                                        // The history is in the host's units (MB for memory).
                                        var top = modelData.max > 0 ? modelData.max : tile._totalGb * 1024
                                        if (!(top > 0)) {
                                            top = 1
                                            for (var i = 0; i < a.length; i++) top = Math.max(top, a[i])
                                        }
                                        ctx.strokeStyle = Theme.ink
                                        ctx.lineWidth = Math.max(1, root._px(2))
                                        ctx.beginPath()
                                        var step = width / 59
                                        var x0 = width - step * (a.length - 1)
                                        for (var j = 0; j < a.length; j++) {
                                            var v = Math.min(top, Math.max(0, a[j]))
                                            var y = height - (v / top) * (height - 2) - 1
                                            if (j === 0) ctx.moveTo(x0, y); else ctx.lineTo(x0 + step * j, y)
                                        }
                                        ctx.stroke()
                                    }
                                }
                            }
                        }
                    }
                }

                // Network: the link, both directions, and the reason when it cannot change.
                Card {
                    id: netCard
                    title: qsTr("Network")
                    icon: "net"
                    readonly property var _link: { root.host; return root.home ? root.home.linkActionFor(root.hostUuid) : ({ shown: false }) }

                    Pair {
                        label: qsTr("Current link speed")
                        value: root.host && root.host.linkText ? root.host.linkText : "—"
                    }
                    Pair {
                        label: qsTr("Let clients change the link speed")
                        value: netCard._link.allowsLinkControl ? qsTr("On") : qsTr("Off")
                    }
                    ButtonRow {
                        visible: netCard._link.shown === true
                        title: netCard._link.restoring ? qsTr("Restore link speed") : qsTr("Match link speed")
                        hint: netCard._link.reason ? netCard._link.reason.charAt(0).toUpperCase() + netCard._link.reason.slice(1) : ""
                        buttonText: netCard._link.restoring ? qsTr("Restore now") : qsTr("Match")
                        live: netCard._link.disabled !== true
                        onActivated: if (root.home) root.home.runActionFor(root.hostUuid, "linkSpeed")
                    }
                }

                // Spatial audio: on/off from here, device and format on the host.
                Card {
                    title: qsTr("Spatial audio")
                    icon: "headphones"
                    SwitchRow {
                        title: qsTr("Auto-activate on stream")
                        hint: {
                            var a = dashPage._audio
                            var state = !a.device ? ""
                                      : a.available !== true ? qsTr("not available")
                                      : a.active === true    ? qsTr("active")
                                      :                        qsTr("ready")
                            var fmt = (a.format || "").replace(" for Headphones", "")
                            return (fmt.length ? fmt + " · " + state + " · " : "") + qsTr("from the next session")
                        }
                        checked: root._shown("audio", dashPage._audio.enabled === true)
                        // Off is always allowed; On needs the format to exist on the device.
                        live: root.hostManagement && (dashPage._audio.available === true || checked)
                        onToggled: function(value) { root._set("audio", value ? "SETAUDIO ON" : "SETAUDIO OFF", value) }
                    }
                }

                // Windows Update: the state on the left, the dialog a small button away.
                Card {
                    title: qsTr("Windows Update")
                    winMark: true
                    ButtonRow {
                        title: root.upd.pending === true ? qsTr("Restart pending")
                             : root.upd.pending === false ? qsTr("No restart pending")
                             : qsTr("Updates")
                        buttonText: qsTr("Check for updates")
                        onActivated: if (root.home) root.home.runActionFor(root.hostUuid, "updateHost")
                    }
                }
            }

            // ── Apps ───────────────────────────────────────────────────────────────────
            // One row per app, as in Settings, with the app's own icon.
            Column {
                id: appsPage
                width: parent.width
                spacing: root._px(16)
                visible: root._tab === "apps"
                enabled: root._full
                opacity: root._full ? 1 : 0.4

                readonly property var _list: root.apps.apps || []

                Line {
                    text: qsTr("Read-only · Host management is off for this device in StreamTweak")
                    dim: true
                    visible: root._appsLoaded && !root.hostManagement
                }

                Card {
                    title: qsTr("Managed apps")
                    Line {
                        dim: true
                        visible: !root._appsLoaded
                        text: root._full ? qsTr("Asking the host…") : qsTr("No apps to show.")
                    }
                    Line {
                        dim: true
                        visible: root._appsLoaded && appsPage._list.length === 0
                        text: qsTr("No apps in the list yet.")
                    }
                    Repeater {
                        model: appsPage._list
                        SwitchRow {
                            readonly property string _key: "app:" + modelData.id
                            iconData: modelData.icon || ""
                            title: modelData.name
                            hint: modelData.found === false ? qsTr("Not found on the host")
                                  : checked ? qsTr("Closed when a stream starts, relaunched when it ends")
                                            : qsTr("Not managed")
                            checked: root._shown(_key, modelData.managed === true)
                            live: root.hostManagement
                            onToggled: function(value) {
                                root._set(_key, "SETAPP " + String(modelData.id).toUpperCase()
                                                + (value ? " ON" : " OFF"), value)
                            }
                        }
                    }
                    Line {
                        dim: true
                        text: qsTr("Apps are added and removed in StreamTweak, on the host.")
                    }
                }
            }

            // ── NVIDIA Sentinel ────────────────────────────────────────────────────────
            Column {
                id: nvPage
                width: parent.width
                spacing: root._px(16)
                visible: root._tab === "nvidia"
                enabled: root._full
                opacity: root._full ? 1 : 0.4

                readonly property var _profile: root.nv.profile || ({})
                property bool _showAll: false
                readonly property var _sentinel: root.nv.sentinel || ({})
                readonly property int _shown: 8

                Line {
                    text: qsTr("Read-only · Host management is off for this device in StreamTweak")
                    dim: true
                    visible: root._nvLoaded && !root.hostManagement
                }

                Card {
                    title: qsTr("GPU and driver")
                    icon: "nvidia_eye"
                    Pair {
                        // The host names its own GPU (StreamTweak 10.0.0); the pairing data is the
                        // fallback, and it is often empty.
                        label: root.nv.gpu ? root.nv.gpu
                             : (root.host && root.host.gpuModel ? root.host.gpuModel : qsTr("NVIDIA GPU"))
                        value: (root.nv.driver || "").replace("NVIDIA ", "")
                               + (root.nv.driver_date ? " · " + root.nv.driver_date : "")
                    }
                }

                Card {
                    title: qsTr("Auto-restore")
                    SwitchRow {
                        title: qsTr("Auto-restore my profile")
                        hint: nvPage._profile.saved !== true ? qsTr("No profile saved · Save current as my profile in StreamTweak")
                              : nvPage._sentinel.last_restore
                                ? qsTr("Last auto-restore: %1").arg(nvPage._sentinel.last_restore)
                                : qsTr("Re-applies your saved profile if NVIDIA App resets it")
                        checked: root._shown("sentinel", nvPage._sentinel.enabled === true)
                        // Off is always allowed; On needs a saved profile to put back.
                        live: root.hostManagement && (nvPage._profile.saved === true || checked)
                        onToggled: function(value) { root._set("sentinel", value ? "SETSENTINEL ON" : "SETSENTINEL OFF", value) }
                    }
                    Line {
                        visible: nvPage._sentinel.stuck === true
                        text: qsTr("Auto-restore is not working: the driver refused the profile. Save it again in StreamTweak.")
                        color: Theme.danger
                    }
                }

                Card {
                    title: qsTr("Captured settings")
                    visible: root._nvLoaded
                    Line {
                        dim: true
                        text: nvPage._profile.saved === true
                              // Plural by hand: no translation is loaded, so %n forms do not apply.
                              ? ((nvPage._profile.count || 0) === 1 ? qsTr("1 setting in your profile")
                                                                    : qsTr("%1 settings in your profile").arg(nvPage._profile.count || 0))
                                + (nvPage._profile.saved_at ? " · " + qsTr("saved %1").arg(nvPage._profile.saved_at) : "")
                              : qsTr("No profile saved · Save current as my profile in StreamTweak")
                    }
                    // One under the other: side by side the longer names and values were cut.
                    Repeater {
                        model: {
                            var all = nvPage._profile.settings || []
                            return nvPage._showAll ? all : all.slice(0, nvPage._shown)
                        }
                        Pair { label: modelData.name; value: modelData.value }
                    }
                    ButtonRow {
                        visible: (nvPage._profile.settings || []).length > nvPage._shown
                        title: ""
                        buttonText: nvPage._showAll ? qsTr("Show fewer")
                                                    : qsTr("Show all %1").arg((nvPage._profile.settings || []).length)
                        onActivated: nvPage._showAll = !nvPage._showAll
                    }
                }
            }
            // ── Setup ──────────────────────────────────────────────────────────────────
            // What used to be the StreamTweak tab of Settings (moved 21/10/2026): the integration's
            // switch for THIS host, the two choices of this device that go with it, what StreamTweak
            // adds, and where to get it. Always here — and alone when the host cannot give the other
            // tabs anything (switched off, not found, too old, pending, denied, offline).
            Column {
                id: setupPage
                width: parent.width
                spacing: root._px(16)
                visible: root._tab === "setup"

                // A CAPS while this tab is up, as Settings did: the one question — does this host
                // run StreamTweak — asked even with the integration switched off, which is exactly
                // when it matters. Also brings the host's clipboard sharing ("on", "off", "" before
                // 8.7.0).
                property string presence: ""      // "" checking · found · missing
                property string clip: ""
                function probe() {
                    if (root.computerModel && root.hostIndex >= 0)
                        root.computerModel.probeStreamTweakPresence(root.hostIndex)
                }
                onVisibleChanged: if (visible) probe()
                // As soon as the host's row is known, too: the line on top of a greyed tab says
                // the same thing as this page, and would say "Checking…" for ten seconds.
                readonly property int _idx: root.hostIndex
                on_IdxChanged: probe()
                // Also while the other tabs are greyed.
                Timer {
                    interval: 10000; repeat: true; triggeredOnStart: true
                    running: setupPage.visible || !root._full
                    onTriggered: setupPage.probe()
                }
                Connections {
                    target: root.computerModel
                    function onStreamTweakPresenceReceived(idx, found, clip) {
                        if (idx !== root.hostIndex) return
                        setupPage.presence = found ? "found" : "missing"
                        setupPage.clip = clip
                    }
                }

                readonly property bool _on: root.host ? root.host.stEnabled !== false : false
                readonly property string _pin:
                    (root.home && root.home.stPinHostUuid === root.hostUuid) ? root.home.stPinValue : ""
                readonly property string _version: root.host && root.host.hostVersion ? root.host.hostVersion : ""

                // One line on where things stand with this host.
                readonly property string _status: {
                    if (!root._online)                return qsTr("Offline — can't check right now")
                    if (presence === "missing")       return qsTr("Not found on this host")
                    if (presence === "")              return qsTr("Checking…")
                    if (!_on)                         return qsTr("Found on this host · off on this device")
                    if (root._auth === "pending")     return qsTr("Waiting for approval on the host")
                    if (root._auth === "denied")      return qsTr("Access denied on the host")
                    if (root._auth === "authorized" && root.host && root.host.hostSection !== true)
                        return _version.length ? qsTr("StreamTweak %1 · update it to 10.0 for the other tabs").arg(_version)
                                               : qsTr("Update StreamTweak to 10.0 for the other tabs")
                    if (root._auth === "authorized")  return _version.length ? qsTr("Connected · StreamTweak %1").arg(_version)
                                                                             : qsTr("Connected")
                    return qsTr("Connecting…")
                }

                Card {
                    title: qsTr("StreamTweak on %1").arg(root.hostName)
                    SwitchRow {
                        title: qsTr("Use StreamTweak with this host")
                        hint: setupPage._status
                        checked: setupPage._on
                        // Never greyed, not even offline: the user's decision about a host.
                        onToggled: function(v) {
                            if (root.computerModel && root.hostIndex >= 0)
                                root.computerModel.setStreamTweakEnabled(root.hostIndex, v)
                        }
                    }
                    // Pending: the PIN the host is showing, to compare.
                    Row {
                        visible: setupPage._on && root._auth === "pending" && setupPage._pin.length > 0
                        spacing: root._px(10)
                        Repeater {
                            model: setupPage._pin.split("")
                            Rectangle {
                                width: root._px(40); height: root._px(48)
                                radius: root._px(8)
                                color: "transparent"
                                border.color: Theme.lineHigh
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: Theme.inkText
                                    font.family: Theme.family
                                    font.pixelSize: root._fpx(Theme.fontH2)
                                    font.bold: true
                                }
                            }
                        }
                    }
                    Line {
                        dim: true
                        visible: setupPage._on && root._auth === "pending"
                        text: qsTr("Approve the request with this PIN in StreamTweak, on the host. Streaming is never affected.")
                    }
                    ButtonRow {
                        visible: setupPage._on && root._auth === "denied"
                        title: qsTr("Ask again")
                        hint: qsTr("You can still stream; the host's management waits for the approval")
                        buttonText: qsTr("Request access")
                        onActivated: if (root.home) root.home.runActionFor(root.hostUuid, "requestStAuth")
                    }
                }

                // This device's choices: they apply to every host, and are set here, beside the one
                // they depend on.
                Card {
                    title: qsTr("This device")
                    SwitchRow {
                        // Why the clipboard will not be shared with this host though it is on here.
                        readonly property string _issue:
                            !StreamingPreferences.clipboardSync || !setupPage._on
                                || setupPage.presence !== "found" ? ""
                            : setupPage.clip === "off" ? qsTr(" · off on this host")
                            : setupPage.clip === ""    ? qsTr(" · needs StreamTweak 8.7.0 on this host")
                            : ""
                        title: qsTr("Share clipboard")
                        hint: qsTr("Text, up to 32 KB · every host") + _issue
                        checked: StreamingPreferences.clipboardSync
                        onToggled: function(v) {
                            StreamingPreferences.clipboardSync = v
                            // Persisted on the toggle: an app killed rather than closed never saves.
                            StreamingPreferences.save()
                        }
                    }
                    SwitchRow {
                        // The host's active profile can decide this one; then it is shown, not changed.
                        readonly property bool _locked: !!(root.host && root.host.streamOverride
                                                          && root.host.streamOverride.waitgame !== undefined)
                        title: qsTr("Wait for the game to appear")
                        hint: _locked
                              ? qsTr("Set by the host profile “%1”").arg(root.host.activeProfileName || "")
                              : qsTr("Keep the launch screen up until the game is on screen · every host")
                        checked: _locked ? root.host.streamOverride.waitgame === true
                                         : StreamingPreferences.waitForGameOnScreen
                        live: !_locked && setupPage._on
                        onToggled: function(v) {
                            StreamingPreferences.waitForGameOnScreen = v
                            StreamingPreferences.save()
                        }
                    }
                }

                // What switching StreamTweak on gives this device, as Settings listed it.
                Card {
                    title: qsTr("Features")
                    GridLayout {
                        width: parent.width
                        columns: root._designW >= 900 ? 3 : 1
                        uniformCellWidths: true
                        columnSpacing: root._px(24)
                        rowSpacing: root._px(6)
                        Repeater {
                            model: [
                                [ qsTr("Network"),
                                  qsTr("Host link speed matched to this device"),
                                  qsTr("Speed put back when you stop streaming"),
                                  qsTr("Wait until the game is on screen") ],
                                [ qsTr("Remote"),
                                  qsTr("Sleep, restart or power off the host"),
                                  qsTr("Windows Update: check, install, restart"),
                                  qsTr("Unlock with your PIN after waking it"),
                                  qsTr("Copy on one side, paste on the other") ],
                                [ qsTr("On screen"),
                                  qsTr("Host GPU, CPU and network in the overlay"),
                                  qsTr("Dashboard, sessions, library, apps, Sentinel here"),
                                  qsTr("A game left open after Quit, flagged"),
                                  qsTr("Every session graded, here and on the host") ]
                            ]
                            Column {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                spacing: root._px(4)
                                Text {
                                    text: modelData[0]
                                    color: Theme.text3
                                    font.family: Theme.family
                                    font.pixelSize: root._fpx(Theme.fontCaption)
                                    font.bold: true
                                    font.capitalization: Font.AllUppercase
                                    font.letterSpacing: 1.2
                                }
                                Repeater {
                                    model: modelData.slice(1)
                                    Text {
                                        width: parent.width
                                        wrapMode: Text.WordWrap
                                        text: "▸  " + modelData
                                        color: Theme.text
                                        font.family: Theme.family
                                        font.pixelSize: root._fpx(Theme.fontSmall)
                                    }
                                }
                            }
                        }
                    }
                }

                Card {
                    title: qsTr("StreamTweak")
                    ButtonRow {
                        title: AppUpdate.latestStreamTweak.length > 0
                               ? qsTr("Latest release %1").arg(AppUpdate.latestStreamTweak)
                               : qsTr("A companion app for the host")
                        hint: qsTr("by FoggyBytes · install it on the host to use the tabs of this section")
                        buttonText: qsTr("GitHub releases")
                        onActivated: Qt.openUrlExternally("https://github.com/FoggyBytes/StreamTweak/releases")
                    }
                }
            }
        }
    }
}
