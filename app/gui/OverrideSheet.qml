import QtQuick 2.15
import QtQuick.Controls 2.15

import Theme 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0

/*
 * The right-hand sheet of Host profiles and of Per-game settings (8.8.0, #53 — the variant A
 * «Elenco» of docs/mockups/profili/ProfilesAMockup.qml, with the scheme «Valore con A»).
 *
 * One vertical list in sections — Stream, Display, Audio, Network, StreamTweak, titled like
 * Settings' cards — instead of the tabs it replaces: most of those tabs held one row. Each row is a
 * label, a caption and ONE badge with the value it streams with (ValueBadge). A opens the badge
 * into every choice, ◀▶ move, A confirms, B puts it back; on Bitrate A opens a slider instead.
 *
 * It works on two maps keyed like an override (width, height, fps, bitrate, hdr, …):
 *   · `base`: what this level inherits — Global for a profile, Global + the host's profile for a
 *     game (ComputerModel.globalValues / AppModel.inheritedValues);
 *   · `own`: what this level holds itself. With `isBase` the sheet edits the base itself (Global,
 *     from the host dialog): every row is then the level's own.
 * It never writes: `change(key, value)` asks the dialog, value undefined = back to the base.
 * Choosing the base's own value on a profile or a game is also "back to the base", as the inherit
 * pill of 6.0.0 was.
 *
 * ⚠️ The sheet does not take the focus. The dialog keeps one key handler and hands the keys here
 * with handleKey() while the cursor is on this side: a single owner is what keeps the pad from
 * landing in a control two levels down, which is what the old tabbed dialog kept doing.
 */
Item {
    id: sheet

    property var    sections: []        // [{ title, keys: [...] }]
    property var    base: ({})
    property var    own: ({})
    property bool   isBase: false
    property string baseName: qsTr("Global")
    // Captions of the base level, by key (Host profiles: "2 profiles change it").
    property var    baseCaptions: ({})
    // The cursor is on this side.
    property bool   active: false

    property int    idx: 0
    property bool   editing: false
    readonly property string currentKey: _rows.length > 0 ? _rows[Math.max(0, Math.min(idx, _rows.length - 1))].key : ""
    // The row under the cursor holds a value of its own that Y can give back.
    readonly property bool currentOver: !isBase && currentKey.length > 0 && _own(currentKey)
    readonly property bool currentEnabled: currentKey.length > 0 && rowEnabled(currentKey)
    readonly property bool currentIsSlider: currentKey === "bitrate"
    readonly property int  changes: _countOwn()

    signal change(string key, var value)
    signal customRequested(string key)
    // A click on a row: the dialog moves its cursor to this side.
    signal pointerEntered()

    // ── Measures: Settings' (rows 58, label 16 / caption 13 at Settings' text scale 0.88) ──
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }
    function _fpx(n) { return Math.round(n * _u * 0.88) }
    readonly property int _rowH: _px(58)
    readonly property int _secH: _px(44)
    readonly property int _gap: _px(16)

    // ── The rows, laid out once per section list ──
    readonly property var _layout: {
        var out = [], y = 0, i = 0
        for (var s = 0; s < sections.length; s++) {
            out.push({ type: "sec", title: sections[s].title, y: y, h: _secH }); y += _secH
            var ks = sections[s].keys
            for (var r = 0; r < ks.length; r++) {
                out.push({ type: "row", key: ks[r], y: y, h: _rowH, idx: i++, last: r === ks.length - 1 })
                y += _rowH
            }
            y += _gap
        }
        return out
    }
    readonly property var _rows: _layout.filter(function(e) { return e.type === "row" })
    readonly property real _bodyH: _layout.length ? _layout[_layout.length - 1].y + _layout[_layout.length - 1].h + _px(30) : 0

    // ── Values ──
    readonly property var _video: SystemProperties.videoOptions()
    readonly property var _vrrRec: SystemProperties.vrrRecommendation()

    function _get(map, key) {
        if (!map) return undefined
        if (key === "resolution")
            return map.width !== undefined ? { w: map.width, h: map.height } : undefined
        return map[key]
    }
    function _own(key) { return isBase || _get(own, key) !== undefined }
    function eff(key) { var v = isBase ? undefined : _get(own, key); return v !== undefined ? v : _get(base, key) }
    function _same(key, a, b) {
        if (a === undefined || b === undefined) return a === b
        if (key === "resolution") return a.w === b.w && a.h === b.h
        return a === b
    }
    function _countOwn() {
        if (isBase) return 0
        var n = 0
        for (var i = 0; i < _rows.length; i++) if (_get(own, _rows[i].key) !== undefined) n++
        return n
    }

    // What the other rows depend on, resolved on this level.
    readonly property bool effVsync:  !!eff("vsync")
    readonly property bool effPacing: effVsync && eff("framepacing") === StreamingPreferences.FP_ON
    readonly property int  effFps:    eff("fps") || 0
    readonly property bool vrrOn:     !!eff("vrr")
    // Above the display's refresh Session refuses VRR — same exception as Settings' _vrrActive.
    readonly property bool vrrOverRate: effFps > (_vrrRec.refreshHz || 100000)
    readonly property bool effVrr:    effVsync && vrrOn && !vrrOverRate
    readonly property int  effCodec:  eff("codec") !== undefined ? eff("codec") : StreamingPreferences.VCC_AUTO
    readonly property bool effPyroWave: effCodec === StreamingPreferences.VCC_FORCE_PYROWAVE

    function rowEnabled(key) {
        switch (key) {
        case "displaymode":     return !effVrr
        case "vsync":           return !effVrr
        case "framepacing":     return effVsync && !effVrr
        case "fractionalvsync": return effPacing && !effVrr
        case "vrr":             return effVsync
        }
        return true
    }
    function rowDetail(key) {
        switch (key) {
        case "displaymode":     return effVrr ? qsTr("Borderless with VRR") : ""
        case "vsync":           return effVrr ? qsTr("On with VRR") : ""
        case "framepacing":     return effVrr ? qsTr("On with VRR") : (!effVsync ? qsTr("Needs V-Sync") : "")
        case "fractionalvsync": return effVrr ? qsTr("Off with VRR")
                                     : (!effVsync ? qsTr("Needs V-Sync") : (!effPacing ? qsTr("Needs Frame pacing") : ""))
        case "vrr":             return !effVsync ? qsTr("Needs V-Sync")
                                     : (vrrOn && vrrOverRate
                                        ? qsTr("Inactive at %1 FPS on this %2 Hz display").arg(effFps).arg(_vrrRec.refreshHz)
                                        : "")
        }
        return ""
    }

    readonly property var labels: ({
        resolution: qsTr("Resolution"), fps: qsTr("Frame rate"), codec: qsTr("Video codec"),
        hdr: qsTr("HDR"), yuv444: qsTr("YUV 4:4:4"), bitrate: qsTr("Bitrate"),
        displaymode: qsTr("Display mode"), vsync: qsTr("V-Sync"), framepacing: qsTr("Frame pacing"),
        fractionalvsync: qsTr("Fractional V-Sync"), vrr: qsTr("VRR"), audio: qsTr("Audio"),
        matchlink: qsTr("Match host link speed"), waitgame: qsTr("Wait for the game to appear")
    })

    // The choices of a row: labels, values, and which one opens a dialog (-1 = none).
    function choices(key) {
        var L = [], V = [], c = -1, i
        switch (key) {
        case "resolution":
            for (i = 0; i < _video.res.length; i++) { L.push(_video.res[i].label); V.push({ w: _video.res[i].width, h: _video.res[i].height }) }
            c = L.length; L.push(qsTr("Custom")); V.push(null)
            break
        case "fps":
            for (i = 0; i < _video.fps.length; i++) { L.push(_video.fps[i].label); V.push(_video.fps[i].value) }
            // With VRR on, the recommended rate is the last pill ("116 VRR"), as in Settings.
            var rec = _vrrRec.fps || 0
            if (vrrOn && rec > 0 && V.indexOf(rec) < 0) { L.push(qsTr("%1 VRR").arg(rec)); V.push(rec) }
            c = L.length; L.push(qsTr("Custom")); V.push(null)
            break
        case "codec":
            L = [qsTr("Auto"), "H.264", "HEVC", "AV1"]
            V = [StreamingPreferences.VCC_AUTO, StreamingPreferences.VCC_FORCE_H264,
                 StreamingPreferences.VCC_FORCE_HEVC, StreamingPreferences.VCC_FORCE_AV1]
            // PyroWave (6.4.0) only where this build can decode it, as in Settings.
            if (SystemProperties.hasPyroWave) { L.push("PyroWave"); V.push(StreamingPreferences.VCC_FORCE_PYROWAVE) }
            break
        case "framepacing":
            L = [qsTr("Off"), qsTr("On")]; V = [StreamingPreferences.FP_OFF, StreamingPreferences.FP_ON]
            break
        case "audio":
            L = [qsTr("Stereo"), "5.1", "7.1"]
            V = [StreamingPreferences.AC_STEREO, StreamingPreferences.AC_51_SURROUND, StreamingPreferences.AC_71_SURROUND]
            break
        case "displaymode":
            L = [qsTr("Fullscreen"), qsTr("Borderless"), qsTr("Windowed")]
            V = [StreamingPreferences.WM_FULLSCREEN, StreamingPreferences.WM_FULLSCREEN_DESKTOP, StreamingPreferences.WM_WINDOWED]
            break
        default:   // On/Off, in Settings' order
            L = [qsTr("Off"), qsTr("On")]; V = [false, true]
        }
        return { labels: L, values: V, custom: c }
    }
    function indexOf(key, v, ch) {
        if (v === undefined || v === null) return -1
        for (var i = 0; i < ch.values.length; i++)
            if (ch.values[i] !== null && _same(key, ch.values[i], v)) return i
        return ch.custom
    }
    function valueText(key, v) {
        if (v === undefined || v === null) return ""
        if (key === "bitrate") return _mbps(v)
        if (key === "resolution") {
            for (var i = 0; i < _video.res.length; i++)
                if (_video.res[i].width === v.w && _video.res[i].height === v.h) return _video.res[i].label
            return v.w + "×" + v.h
        }
        if (key === "fps") return String(v)
        var ch = choices(key), j = indexOf(key, v, ch)
        return j >= 0 ? ch.labels[j] : String(v)
    }
    // The labels on show: a custom value names itself on the Custom pill.
    function shownLabels(key) {
        var ch = choices(key)
        if (ch.custom < 0) return ch.labels
        var L = ch.labels.slice(), v = eff(key)
        if (v !== undefined && indexOf(key, v, ch) === ch.custom) L[ch.custom] = valueText(key, v)
        return L
    }
    function _mbps(kbps) {
        var m = kbps / 1000
        return (Math.round(m) === m ? m.toFixed(0) : m.toFixed(1)) + " " + qsTr("Mbps")
    }
    function caption(key) {
        var d = rowDetail(key)
        if (d.length > 0) return d
        if (isBase) return baseCaptions[key] || ""
        return _own(key) ? baseName + ": " + valueText(key, _get(base, key))
                         : qsTr("From %1").arg(baseName)
    }

    // ── Bitrate: the scale of the codec this level streams with (StreamingPreferences.getMaxBitrate)
    readonly property int brFrom: effPyroWave ? 5000 : 500
    readonly property int brStep: effPyroWave ? 5000 : 500
    readonly property int brTo:   StreamingPreferences.getMaxBitrate(effCodec)

    // ── Editing ──
    property int  _editSel: -1
    property real _editBr: 0
    property real _lastStep: 0
    property int  _ticks: 0

    function startEdit() {
        if (!currentEnabled) return
        if (currentIsSlider) {
            _editBr = Math.max(brFrom, Math.min(brTo, eff("bitrate") || brFrom))
        } else {
            var ch = choices(currentKey)
            _editSel = indexOf(currentKey, eff(currentKey), ch)
            if (_editSel < 0) _editSel = 0
        }
        _ticks = 0
        editing = true
    }
    function cancelEdit() { editing = false }
    function commitEdit() {
        var key = currentKey
        editing = false
        if (key === "bitrate") { _set(key, Math.round(_editBr)); return }
        var ch = choices(key)
        if (_editSel === ch.custom) { customRequested(key); return }
        _set(key, ch.values[_editSel])
    }
    // Choosing on a row, from the pad or the mouse.
    function pick(key, i) {
        var ch = choices(key)
        if (i === ch.custom) { customRequested(key); return }
        _set(key, ch.values[i])
    }
    function _set(key, v) {
        if (!isBase && _same(key, v, _get(base, key))) { change(key, undefined); return }
        if (!isBase && _same(key, v, _get(own, key))) return
        if (isBase && _same(key, v, _get(base, key))) return
        change(key, v)
    }
    function _step(dir) {
        if (currentIsSlider) {
            var now = Date.now()
            _ticks = (now - _lastStep < 200) ? _ticks + 1 : 0
            _lastStep = now
            // Tap = one step; held, it ramps up to 20 steps at a time, as in Settings.
            var mult = Math.min(20, 1 + Math.floor(_ticks / 4))
            _editBr = Math.max(brFrom, Math.min(brTo, _editBr + dir * brStep * mult))
        } else {
            var n = choices(currentKey).labels.length
            _editSel = Math.max(0, Math.min(n - 1, _editSel + dir))
        }
    }

    // The dialog's keys, while the cursor is here. Returns true when the key was used.
    function handleKey(e) {
        var k = e.key
        var A = k === Qt.Key_Return || k === Qt.Key_Enter || k === Qt.Key_Space
        if (editing) {
            if (k === Qt.Key_Left)       _step(-1)
            else if (k === Qt.Key_Right) _step(1)
            else if (A)                  commitEdit()
            else if (k === Qt.Key_Escape) cancelEdit()
            else if (k === Qt.Key_Up || k === Qt.Key_Down || k === Qt.Key_Tab || k === Qt.Key_Backtab) {}
            else return false
            return true
        }
        if (k === Qt.Key_Up || (k === Qt.Key_Tab && (e.modifiers & Qt.ShiftModifier)) || k === Qt.Key_Backtab)
            idx = Math.max(0, idx - 1)
        else if (k === Qt.Key_Down || k === Qt.Key_Tab)
            idx = Math.min(_rows.length - 1, idx + 1)
        else if (A) startEdit()
        else if ((k === Qt.Key_Hangup || k === Qt.Key_Y) && currentOver) change(currentKey, undefined)
        else return false
        return true
    }

    onIdxChanged: { editing = false; _follow() }
    onActiveChanged: if (!active) editing = false
    onSectionsChanged: idx = 0

    function _follow() {
        if (_rows.length === 0) return
        var r = _rows[Math.max(0, Math.min(idx, _rows.length - 1))], m = _px(80)
        var y = body.contentY
        if (r.y - m < y) y = Math.max(0, r.y - m)
        else if (r.y + r.h + m > y + body.height) y = Math.min(Math.max(0, body.contentHeight - body.height), r.y + r.h + m - body.height)
        if (idx === 0) y = 0
        body.contentY = y
    }
    function toTop() { body.contentY = 0 }

    Flickable {
        id: body
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: sheet._bodyH
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        Behavior on contentY {
            enabled: !Theme.reduceAnimations && !body.moving
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }
        ScrollBar.vertical: ThemedScrollBar {}

        // The cursor: one frame that glides, FocusFrame's radius and border.
        Rectangle {
            visible: sheet.active && sheet._rows.length > 0
            readonly property var r: sheet._rows[Math.max(0, Math.min(sheet.idx, sheet._rows.length - 1))]
            x: sheet._px(4); width: body.width - sheet._px(8)
            y: r ? r.y : 0; height: sheet._rowH
            radius: sheet._px(6)
            color: Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, sheet.editing ? 0.14 : 0.10)
            border.width: sheet._px(2); border.color: Theme.accentFill
            Behavior on y { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            Behavior on color { enabled: !Theme.reduceAnimations; ColorAnimation { duration: 140 } }
        }

        Repeater {
            model: sheet._layout
            delegate: Item {
                id: entry
                required property var modelData
                x: sheet._px(4); y: modelData.y
                width: body.width - sheet._px(8); height: modelData.h

                // A section title: Settings' card titles.
                Text {
                    visible: entry.modelData.type === "sec"
                    anchors.bottom: parent.bottom; anchors.bottomMargin: sheet._px(10)
                    leftPadding: sheet._px(14)
                    text: entry.modelData.type === "sec" ? entry.modelData.title : ""
                    color: Theme.text3
                    font.family: Theme.family
                    font.pixelSize: sheet._fpx(Theme.fontSmall)
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                }

                Item {
                    id: row
                    visible: entry.modelData.type === "row"
                    anchors.fill: parent
                    readonly property string key: entry.modelData.type === "row" ? entry.modelData.key : ""
                    readonly property bool cur: sheet.active && sheet.idx === entry.modelData.idx
                    readonly property bool open: cur && sheet.editing
                    readonly property bool on: key.length > 0 && sheet.rowEnabled(key)
                    readonly property bool over: key.length > 0 && sheet._own(key)
                    readonly property var ch: key.length > 0 ? sheet.choices(key) : ({ labels: [], values: [], custom: -1 })

                    Column {
                        anchors.left: parent.left; anchors.leftMargin: sheet._px(16)
                        anchors.right: badge.left; anchors.rightMargin: sheet._px(16)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: sheet._px(3)
                        opacity: row.on ? 1 : 0.4
                        Text {
                            width: parent.width
                            text: row.key.length > 0 ? sheet.labels[row.key] : ""
                            color: Theme.text
                            elide: Text.ElideRight
                            font.family: Theme.family
                            font.pixelSize: sheet._fpx(Theme.fontBody)
                            font.bold: true
                        }
                        Text {
                            width: parent.width
                            readonly property string t: row.key.length > 0 ? sheet.caption(row.key) : ""
                            visible: t.length > 0
                            text: t
                            elide: Text.ElideRight
                            color: !sheet.isBase && row.over && sheet.rowDetail(row.key).length === 0 ? Theme.inkText : Theme.text3
                            font.family: Theme.family
                            font.pixelSize: sheet._fpx(Theme.fontSmall)
                        }
                    }

                    // The value: a badge, or the bitrate slider while it is being changed.
                    ValueBadge {
                        id: badge
                        visible: !(row.open && row.key === "bitrate")
                        anchors.right: parent.right; anchors.rightMargin: sheet._px(16)
                        anchors.verticalCenter: parent.verticalCenter
                        opacity: row.on ? 1 : 0.4
                        opts: row.key === "bitrate" ? [] : sheet.shownLabels(row.key)
                        sel: row.key === "bitrate" ? -1
                             : row.open ? sheet._editSel
                             : sheet.indexOf(row.key, sheet.eff(row.key), row.ch)
                        text: row.key === "bitrate" ? sheet._mbps(sheet.eff("bitrate") || 0)
                                                    : sheet.valueText(row.key, sheet.eff(row.key))
                        over: sheet.isBase ? true
                              : row.open ? !sheet._same(row.key, row.ch.values[sheet._editSel], sheet._get(sheet.base, row.key))
                              : row.over
                        open: row.open
                        onPicked: function(i) { sheet.editing = false; sheet.pick(row.key, i) }
                    }

                    Row {
                        visible: row.open && row.key === "bitrate"
                        anchors.right: parent.right; anchors.rightMargin: sheet._px(16)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: sheet._px(12)
                        ThemedSlider {
                            id: brSlider
                            anchors.verticalCenter: parent.verticalCenter
                            width: sheet._px(300)
                            from: sheet.brFrom; to: sheet.brTo; stepSize: sheet.brStep
                            snapMode: Slider.SnapAlways
                            focusPolicy: Qt.NoFocus
                            value: sheet._editBr
                            onMoved: sheet._editBr = value
                            onPressedChanged: if (!pressed && row.open) sheet.commitEdit()
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: sheet._px(86)
                            horizontalAlignment: Text.AlignRight
                            text: sheet._mbps(sheet._editBr)
                            color: Theme.text
                            font.family: Theme.family
                            font.pixelSize: sheet._fpx(Theme.fontBody)
                            font.bold: true
                        }
                    }

                    Rectangle {
                        visible: !entry.modelData.last
                        anchors.bottom: parent.bottom
                        x: sheet._px(16); width: parent.width - sheet._px(32); height: 1
                        color: Theme.line
                    }

                    // A click on the row opens it; a click on a pill of an open badge chooses
                    // (ValueBadge's own areas sit above this one).
                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        enabled: row.visible
                        cursorShape: row.on ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            sheet.pointerEntered()
                            if (sheet.idx !== entry.modelData.idx) sheet.idx = entry.modelData.idx
                            if (row.open) sheet.cancelEdit()
                            else sheet.startEdit()
                        }
                    }
                }
            }
        }
    }

    // The right stick scrolls the list, as everywhere else in the app.
    PadScroll { flick: body }
}
