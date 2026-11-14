import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.15

import StreamingPreferences 1.0
import InputHints 1.0
import SdlGamepadKeyNavigation 1.0

/*
 * Host profiles (StreamLight 4.0.0; redone in 8.8.0 for #53 — variant A «Elenco» of
 * docs/mockups/profili/ProfilesAMockup.qml, with the scheme «Valore con A»).
 *
 * Almost the whole window. On the left a sidebar the D-pad walks up and down:
 *   · Sort — Alphabetical, Created or Custom; in Custom, R3 (M) picks a profile up, ▲▼ carry it,
 *     A puts it down, B puts it back where it was. The mouse drags it;
 *   · Global — the settings of Settings, editable from here too;
 *   · the profiles, in that order (the same order the triggers cycle and Home's list shows);
 *   · + Add a profile.
 * A on a row makes it the one this host streams with, and the right side shows it: one list in
 * sections (OverrideSheet), ▶ to go there, ◀ to come back.
 *
 * ⚠️ One key handler for the whole dialog (`root`), with the cursor kept here as a zone and an
 * index — no focus chain. The old dialog's chips, tabs and rows each took the focus, and every
 * child popup that closed had a chance to hand it to the wrong one.
 */
Popup {
    id: dlg

    // 7.0.0: the shared spring in and quick out (PopEnter/PopExit), off with Reduce animations.
    enter: PopEnter {}
    exit: PopExit {}

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }
    // Settings' text scale (Theme.textScale 0.88 there), whatever the page behind the dialog uses.
    function _fpx(n) { return Math.round(n * _u * 0.88) }

    property var computerModel: null
    property int pcIndex: -1
    property string hostName: ""

    // 20 since 8.8.3 (#54), was 14 — the same figure as HostProfileManager::kMaxNameLen.
    readonly property int _maxNameLen: 20

    // ── What the host holds ──
    property int  profileCount: 0
    property int  sortMode: 1                 // HostProfileManager::SortMode — 0 A–Z, 1 Created, 2 Custom
    property int  activeSlot: -1              // -1 = Global
    property var  _info: ({})                 // created → { slot, name, n }
    property var  _orderCreated: []           // the profiles as shown, by creation number
    property var  _base: ({})                 // Global, as values (ComputerModel.globalValues)
    property var  _own: ({})                  // the active profile's own values
    property var  _counts: ({})               // key → how many profiles change it
    readonly property int _activeCreated: {
        for (var c in _info) if (_info[c].slot === activeSlot) return parseInt(c)
        return -1
    }
    ListModel { id: profModel }               // one entry per profile, keyed by creation: it survives a re-sort

    readonly property var _sortNames: [qsTr("Alphabetical"), qsTr("Created"), qsTr("Custom")]
    readonly property var _sortHints: [qsTr("A to Z"), qsTr("Oldest first"), qsTr("Your order")]

    readonly property var _sections: [
        { title: qsTr("Stream"),      keys: ["resolution", "fps", "codec", "hdr", "yuv444", "bitrate"] },
        { title: qsTr("Display"),     keys: ["displaymode", "vsync", "framepacing", "fractionalvsync", "vrr"] },
        { title: qsTr("Audio"),       keys: ["audio"] },
        { title: qsTr("Network"),     keys: ["matchlink"] },
        { title: qsTr("StreamTweak"), keys: ["waitgame"] }
    ]

    // ── The cursor ──
    property string zone: "side"              // side · sheet · sort
    property int  sideIdx: 1                  // 0 Sort · 1 Global · 2.. profiles · last + Add
    property int  sortIdx: 1
    property int  _movingCreated: -1
    property int  _movingFrom: -1
    readonly property bool _moving: _movingCreated >= 0
    property string _toast: ""

    readonly property int _addIdx: profileCount + 2
    readonly property bool _onProfile: sideIdx >= 2 && sideIdx < _addIdx
    function _createdAt(i) { return (i >= 2 && i < _addIdx) ? _orderCreated[i - 2] : -1 }
    function _slotAt(i) { var c = _createdAt(i); return c >= 0 && _info[c] ? _info[c].slot : -1 }

    // ── Layout of the sidebar ──
    readonly property int _rowH: _px(58)
    readonly property int _btnH: _px(40)
    readonly property int _listTop: _px(132)
    function _sideY(i) {
        return i === 0 ? 0 : i === 1 ? _px(58)
             : i === _addIdx ? _listTop + profileCount * _rowH + _px(14)
             : _listTop + (i - 2) * _rowH
    }
    function _sideH(i) { return (i === 0 || i === _addIdx) ? _btnH : _rowH }

    function _changes(m) { var n = 0; for (var k in m) if (k !== "height") n++; return n }
    function _say(t) { _toast = t; toastTimer.restart() }

    // ── Reading the host ──
    function reload() {
        if (!computerModel || pcIndex < 0) return
        profileCount = computerModel.hostProfileCount(pcIndex)
        sortMode = computerModel.hostProfileSortMode(pcIndex)
        activeSlot = computerModel.hostActiveProfile(pcIndex)
        var info = {}, counts = {}
        for (var s = 0; s < profileCount; s++) {
            var c = computerModel.hostProfileCreated(pcIndex, s)
            var m = computerModel.hostProfileSettings(pcIndex, s)
            info[c] = { slot: s, name: computerModel.hostProfileName(pcIndex, s), n: _changes(m) }
            for (var k in m) {
                var key = (k === "width" || k === "height") ? "resolution" : k
                if (k === "height") continue
                counts[key] = (counts[key] || 0) + 1
            }
        }
        var ord = computerModel.hostProfileOrder(pcIndex), oc = []
        for (var i = 0; i < ord.length; i++) oc.push(computerModel.hostProfileCreated(pcIndex, ord[i]))
        _info = info
        _orderCreated = oc
        _counts = counts
        // The model keeps the delegates of profiles that are still there, so a new order moves them.
        for (var j = profModel.count - 1; j >= 0; j--)
            if (info[profModel.get(j).created] === undefined) profModel.remove(j)
        for (var cc in info) {
            var found = false
            for (var q = 0; q < profModel.count; q++) if (profModel.get(q).created === parseInt(cc)) { found = true; break }
            if (!found) profModel.append({ created: parseInt(cc) })
        }
        _base = computerModel.globalValues()
        _own = activeSlot >= 0 ? computerModel.hostProfileSettings(pcIndex, activeSlot) : ({})
    }
    function _cursorTo(created) {
        var p = _orderCreated.indexOf(created)
        sideIdx = p >= 0 ? p + 2 : (activeSlot < 0 ? 1 : sideIdx)
    }

    // ── The sidebar's actions ──
    function activateSide() {
        if (sideIdx === 0) { openSort(); return }
        if (sideIdx === 1) { selectGlobal(); return }
        if (sideIdx === _addIdx) { addProfile(); return }
        selectSlot(_slotAt(sideIdx))
    }
    function selectSlot(slot) {
        if (slot < 0) return
        computerModel.setHostActiveProfile(pcIndex, slot)
        reload()
        sheet.idx = 0; sheet.toTop()
    }
    function selectGlobal() {
        computerModel.setHostActiveProfile(pcIndex, -1)
        reload()
        sheet.idx = 0; sheet.toTop()
    }
    function addProfile() {
        var slot = computerModel.addHostProfile(pcIndex)
        if (slot < 0) return
        computerModel.setHostActiveProfile(pcIndex, slot)
        var c = computerModel.hostProfileCreated(pcIndex, slot)
        reload()
        _cursorTo(c)
        sheet.idx = 0; sheet.toTop()
    }
    function renameProfile(slot) {
        if (slot < 0) return
        nameDialog.slot = slot
        nameDialog.created = computerModel.hostProfileCreated(pcIndex, slot)
        nameDialog.initName = computerModel.hostProfileName(pcIndex, slot)
        nameDialog.maxLength = _maxNameLen
        nameDialog.open()
    }
    function askRemoveProfile(slot) {
        if (slot < 0) return
        removeConfirm.slot = slot
        removeConfirm.pcName = computerModel.hostProfileName(pcIndex, slot)
        removeConfirm.open()
    }
    function removeProfile(slot) {
        var pos = sideIdx
        computerModel.removeHostProfile(pcIndex, slot)
        reload()
        sideIdx = Math.max(1, Math.min(pos, _addIdx - (profileCount > 0 ? 1 : 0)))
        if (profileCount === 0) sideIdx = _addIdx
    }

    function openSort() { sortIdx = sortMode; zone = "sort" }
    function chooseSort(m) {
        var keep = _createdAt(sideIdx)
        computerModel.setHostProfileSortMode(pcIndex, m)
        reload()
        zone = "side"
        if (keep >= 0) _cursorTo(keep)
    }

    // Custom order: the profile is moved for real at each step, so the sidebar shows exactly what
    // B would undo; B carries it back to where it was picked up.
    function pick() {
        var c = _createdAt(sideIdx)
        if (c < 0) return
        if (profileCount < 2) return
        if (sortMode !== 2) { _say(qsTr("Sort: Custom to move a profile")); return }
        _movingCreated = c
        _movingFrom = _orderCreated.indexOf(c)
    }
    function _moveTo(to) {
        var from = _orderCreated.indexOf(_movingCreated)
        to = Math.max(0, Math.min(profileCount - 1, to))
        if (from < 0 || to === from) return
        // In Custom the order IS the slot order (HostProfileManager::order).
        computerModel.moveHostProfile(pcIndex, from, to)
        reload()
        sideIdx = to + 2
    }
    function drop(keep) {
        if (!_moving) return
        if (!keep) _moveTo(_movingFrom)
        var c = _movingCreated
        _movingCreated = -1
        _cursorTo(c)
    }

    // ── Writing what the sheet asks ──
    function _defaultBitrate(w, h, fps) {
        return StreamingPreferences.videoCodecConfig === StreamingPreferences.VCC_FORCE_PYROWAVE
            ? StreamingPreferences.getDefaultPyroWaveBitrate(w, h, fps, StreamingPreferences.enableYUV444,
                                                             StreamingPreferences.enableHdr)
            : StreamingPreferences.getDefaultBitrate(w, h, fps, StreamingPreferences.enableYUV444)
    }
    function _autoBitrate() {
        if (StreamingPreferences.autoAdjustBitrate)
            StreamingPreferences.bitrateKbps = _defaultBitrate(StreamingPreferences.width, StreamingPreferences.height,
                                                               StreamingPreferences.fps)
    }
    function _setGlobalFps(f) {
        if (!f || StreamingPreferences.fps === f) return
        StreamingPreferences.fps = f
        _autoBitrate()
    }

    // Global: exactly what the rows of Settings do, side effects included (the bitrate that
    // follows on automatic, the VRR rate, the codec's ceiling) — see SettingsScreen.qml.
    function _writeGlobal(key, v) {
        var P = StreamingPreferences
        switch (key) {
        case "resolution": P.width = v.w; P.height = v.h; _autoBitrate(); break
        case "fps": _setGlobalFps(v); break
        case "codec":
            var wasPyro = P.videoCodecConfig === P.VCC_FORCE_PYROWAVE
            P.videoCodecConfig = v
            var isPyro = v === P.VCC_FORCE_PYROWAVE, maxKbps = P.getMaxBitrate(v)
            if (isPyro !== wasPyro && P.autoAdjustBitrate) _autoBitrate()
            else if (P.bitrateKbps > maxKbps) P.bitrateKbps = maxKbps
            break
        case "hdr": P.enableHdr = v; if (P.videoCodecConfig === P.VCC_FORCE_PYROWAVE) _autoBitrate(); break
        case "yuv444": P.enableYUV444 = v; _autoBitrate(); break
        case "bitrate": P.bitrateKbps = v; P.autoAdjustBitrate = false; break
        case "displaymode": P.windowMode = v; break
        case "vsync": P.enableVsync = v; break
        case "framepacing": P.framePacingMode = v; break
        case "fractionalvsync": P.fractionalVsync = v; break
        case "vrr":
            P.enableVrr = v
            var rec = sheet._vrrRec.fps || 0
            if (rec > 0) {
                if (v) {
                    if (P.fps !== rec) { P.fpsBeforeVrr = P.fps; _setGlobalFps(rec) }
                } else {
                    if (P.fps === rec && P.fpsBeforeVrr > 0) _setGlobalFps(P.fpsBeforeVrr)
                    P.fpsBeforeVrr = 0
                }
            }
            break
        case "audio": P.audioConfig = v; break
        case "matchlink": P.matchHostLinkSpeed = v; break
        case "waitgame": P.waitForGameOnScreen = v; break
        }
        P.save()
    }

    // A profile: its own map, rewritten whole. The VRR rate rides along as in Settings — on, the
    // recommended rate is chosen and the one it replaced remembered (by the dialog only); off, it
    // comes back if the recommended one is still in use.
    property var _fpsBeforeVrr: null
    function _writeProfile(key, v) {
        var m = {}
        for (var k in _own) m[k] = _own[k]
        if (key === "resolution") {
            if (v === undefined) { delete m.width; delete m.height }
            else { m.width = v.w; m.height = v.h }
        } else if (v === undefined) delete m[key]
        else m[key] = v

        if (key === "vrr") {
            var rec = sheet._vrrRec.fps || 0
            var wasOn = sheet.vrrOn
            var on = v !== undefined ? v : !!_base.vrr
            var fps = m.fps !== undefined ? m.fps : _base.fps
            if (rec > 0 && on && !wasOn && fps !== rec) {
                _fpsBeforeVrr = { had: m.fps !== undefined, v: m.fps }
                if (rec === _base.fps) delete m.fps; else m.fps = rec
            } else if (rec > 0 && !on && wasOn && fps === rec) {
                if (_fpsBeforeVrr && _fpsBeforeVrr.had) m.fps = _fpsBeforeVrr.v; else delete m.fps
                _fpsBeforeVrr = null
            }
        }
        computerModel.setHostProfileSettings(pcIndex, activeSlot, m)
    }
    function applyChange(key, v) {
        if (activeSlot < 0) _writeGlobal(key, v)
        else _writeProfile(key, v)
        reload()
    }
    function resetAll() {
        if (activeSlot < 0) return
        computerModel.setHostProfileSettings(pcIndex, activeSlot, {})
        _fpsBeforeVrr = null
        reload()
    }

    // ── The popup ──
    modal: true
    dim: true
    focus: true
    // B is the dialog's own key (it closes, cancels or goes back, by zone), so no CloseOnEscape:
    // Qt would take Esc as a shortcut before the dialog saw it.
    closePolicy: (_moving || zone === "sort" || sheet.editing) ? Popup.NoAutoClose : Popup.CloseOnPressOutside
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent ? parent.width - _px(64) : _px(1200)
    height: parent ? parent.height - _px(56) : _px(800)
    padding: 0

    Overlay.modal: Rectangle { color: Theme.scrim }
    background: Rectangle {
        color: Theme.card
        radius: dlg._px(14)
        border.color: Theme.line
        border.width: 1
    }

    onAboutToShow: {
        reload()
        zone = "side"
        _movingCreated = -1
        _fpsBeforeVrr = null
        sideIdx = activeSlot >= 0 ? Math.max(2, _orderCreated.indexOf(_activeCreated) + 2) : 1
        sheet.idx = 0
        sheet.editing = false
        sheet.toTop()
    }
    onOpened: root.forceActiveFocus()
    onClosed: { drop(true); zone = "side" }

    Timer { id: toastTimer; interval: 1800; onTriggered: dlg._toast = "" }

    // One prompt, as in the status bar: the button and what it does here.
    component Prompt: Item {
        id: prompt
        property string buttonKey: ""
        property string keyLabel: ""
        property string action: ""
        property bool clickable: false
        signal clicked()
        implicitWidth: pr.implicitWidth
        // As tall as the key chip even when there is none ("▲▼ Move"): the footer Row lines its
        // items up by the top, so a prompt of text alone sat higher than its neighbours.
        implicitHeight: Math.max(pr.implicitHeight, dlg._px(26))
        Row {
            id: pr
            anchors.verticalCenter: parent.verticalCenter
            spacing: dlg._px(9)
            ActionHint {
                visible: prompt.buttonKey.length > 0 || prompt.keyLabel.length > 0
                anchors.verticalCenter: parent.verticalCenter
                buttonKey: prompt.buttonKey
                keyLabel: prompt.keyLabel
                size: dlg._px(26)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: prompt.action
                color: Theme.text
                font.family: Theme.family
                font.pixelSize: dlg._px(16)
            }
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -dlg._px(6)
            enabled: prompt.clickable
            cursorShape: Qt.PointingHandCursor
            onClicked: prompt.clicked()
        }
    }

    contentItem: Item {
        id: root
        focus: true

        Keys.onPressed: function(e) {
            var k = e.key
            var A = k === Qt.Key_Return || k === Qt.Key_Enter || k === Qt.Key_Space
            var B = k === Qt.Key_Escape
            var up = k === Qt.Key_Up || k === Qt.Key_Backtab || (k === Qt.Key_Tab && (e.modifiers & Qt.ShiftModifier))
            var down = !up && (k === Qt.Key_Down || k === Qt.Key_Tab)
            // Everything is taken: the dialog is modal, and a Y that got through would reach
            // main.qml, which opens Settings on it.
            e.accepted = true

            if (dlg.zone === "sort") {
                if (up) dlg.sortIdx = Math.max(0, dlg.sortIdx - 1)
                else if (down) dlg.sortIdx = Math.min(2, dlg.sortIdx + 1)
                else if (A) dlg.chooseSort(dlg.sortIdx)
                else if (B) dlg.zone = "side"
                return
            }
            if (dlg._moving) {
                var pos = dlg._orderCreated.indexOf(dlg._movingCreated)
                if (up) dlg._moveTo(pos - 1)
                else if (down) dlg._moveTo(pos + 1)
                else if (A || k === Qt.Key_F19 || k === Qt.Key_M) dlg.drop(true)
                else if (B) dlg.drop(false)
                return
            }
            if (dlg.zone === "sheet") {
                if (sheet.handleKey(e)) return
                if (k === Qt.Key_Left) dlg.zone = "side"
                else if ((k === Qt.Key_Menu || k === Qt.Key_X) && dlg.activeSlot >= 0 && sheet.changes > 0) resetConfirm.open()
                else if (B) dlg.close()
                return
            }
            // The sidebar.
            if (up) dlg.sideIdx = Math.max(0, dlg.sideIdx - 1)
            else if (down) dlg.sideIdx = Math.min(dlg._addIdx, dlg.sideIdx + 1)
            else if (k === Qt.Key_Right) dlg.zone = "sheet"
            else if (A) dlg.activateSide()
            else if (k === Qt.Key_F19 || k === Qt.Key_M) dlg.pick()
            else if (k === Qt.Key_F18 || k === Qt.Key_W) { dlg.sideIdx = 0; dlg.openSort() }
            else if ((k === Qt.Key_Menu || k === Qt.Key_X) && dlg._onProfile) dlg.renameProfile(dlg._slotAt(dlg.sideIdx))
            else if ((k === Qt.Key_Hangup || k === Qt.Key_Y) && dlg._onProfile) dlg.askRemoveProfile(dlg._slotAt(dlg.sideIdx))
            else if (B) dlg.close()
        }

        // ── Header ──
        Item {
            id: head
            width: parent.width; height: dlg._px(58)
            Row {
                x: dlg._px(28); anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(12)
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    source: Theme.light ? "qrc:/res/tune_dark.svg" : "qrc:/res/tune.svg"
                    sourceSize.width: 22; sourceSize.height: 22
                    width: dlg._px(22); height: dlg._px(22)
                }
                Text {
                    id: htitle
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Host profiles")
                    color: Theme.text
                    font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontTitle); font.bold: true
                }
                Text {
                    anchors.baseline: htitle.baseline
                    text: dlg.hostName
                    color: Theme.text2
                    font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontSmall)
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
        }

        // ── Sidebar ──
        Item {
            id: side
            y: head.height
            width: dlg._px(340); height: parent.height - y - foot.height
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }

            Flickable {
                id: sideFlick
                x: dlg._px(10); y: dlg._px(16)
                width: parent.width - dlg._px(20); height: parent.height - dlg._px(32)
                clip: true
                contentHeight: dlg._sideY(dlg._addIdx) + dlg._btnH + dlg._px(8)
                boundsBehavior: Flickable.StopAtBounds
                Behavior on contentY { enabled: !Theme.reduceAnimations && !sideFlick.moving
                                       NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                function follow() {
                    var top = dlg._sideY(dlg.sideIdx) - dlg._px(12)
                    var bot = dlg._sideY(dlg.sideIdx) + dlg._sideH(dlg.sideIdx) + dlg._px(12)
                    if (top < contentY) contentY = Math.max(0, top)
                    else if (bot > contentY + height) contentY = Math.min(Math.max(0, contentHeight - height), bot - height)
                }
                Connections { target: dlg; function onSideIdxChanged() { sideFlick.follow() } }

                Item {
                    id: sideContent
                    width: sideFlick.width
                    height: sideFlick.contentHeight

                    // With the cursor on the sheet the frame below stays as a quiet mark of where it
                    // was, but only on an entry that draws nothing of its own: on Sort, + Add a profile
                    // or the profile in use it sat 4 px outside their frame, two borders side by side.
                    readonly property bool _sideOwnFrame: dlg.sideIdx === 0 || dlg.sideIdx === dlg._addIdx
                                                         || (dlg.sideIdx === 1 && dlg.activeSlot < 0)
                                                         || (dlg._onProfile && dlg.activeSlot >= 0 && dlg._slotAt(dlg.sideIdx) === dlg.activeSlot)

                    // The cursor: one frame that glides (FocusFrame's radius and border).
                    Rectangle {
                        visible: !dlg._moving && (here || !sideContent._sideOwnFrame)
                        x: dlg._px(2); width: parent.width - dlg._px(4)
                        y: dlg._sideY(dlg.sideIdx); height: dlg._sideH(dlg.sideIdx)
                        radius: dlg._px(6)
                        readonly property bool here: dlg.zone === "side" || dlg.zone === "sort"
                        color: here ? Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.22)
                                    : Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.05)
                        border.width: here ? dlg._px(2) : 1
                        border.color: here ? Theme.accentFill : Theme.lineHigh
                        Behavior on y { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                        Behavior on height { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                        Behavior on color { enabled: !Theme.reduceAnimations; ColorAnimation { duration: 180 } }
                    }

                    // Sort
                    Item {
                        x: dlg._px(6); width: parent.width - dlg._px(12); height: dlg._btnH; z: 1
                        Rectangle {
                            anchors.fill: parent; radius: dlg._px(8)
                            color: Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.06)
                            border.width: 1; border.color: Theme.lineHigh
                            visible: !(dlg.zone !== "sheet" && dlg.sideIdx === 0)
                        }
                        Row {
                            x: dlg._px(14); anchors.verticalCenter: parent.verticalCenter; spacing: dlg._px(8)
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "⇅"; color: Theme.text2
                                   font.pixelSize: dlg._px(15) }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Sort"); color: Theme.text2
                                   font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontBody) }
                            Text {
                                id: sortName
                                anchors.verticalCenter: parent.verticalCenter
                                text: dlg._sortNames[dlg.sortMode]
                                color: Theme.text
                                font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontBody); font.bold: true
                                Behavior on text {
                                    enabled: !Theme.reduceAnimations
                                    SequentialAnimation {
                                        NumberAnimation { target: sortName; property: "opacity"; to: 0; duration: 90 }
                                        PropertyAction {}
                                        NumberAnimation { target: sortName; property: "opacity"; to: 1; duration: 140 }
                                    }
                                }
                            }
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: dlg._px(14)
                            anchors.verticalCenter: parent.verticalCenter
                            text: "▾"; color: Theme.text2; font.pixelSize: dlg._px(13)
                            rotation: dlg.zone === "sort" ? 180 : 0
                            Behavior on rotation { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 160 } }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { if (dlg._moving) return; dlg.sideIdx = 0; dlg.openSort() }
                        }
                    }

                    // Global
                    Item {
                        id: gRow
                        x: dlg._px(6); y: dlg._px(58); width: parent.width - dlg._px(12); height: dlg._rowH; z: 1
                        readonly property bool act: dlg.activeSlot < 0
                        Rectangle {
                            anchors.fill: parent; radius: dlg._px(6)
                            visible: gRow.act && !(dlg.zone !== "sheet" && dlg.sideIdx === 1)
                            color: Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.14)
                            border.width: 1
                            border.color: Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.55)
                        }
                        Rectangle {
                            visible: gRow.act
                            x: dlg._px(14); anchors.verticalCenter: parent.verticalCenter
                            width: dlg._px(7); height: width; radius: width / 2; color: Theme.ink
                        }
                        Column {
                            x: dlg._px(30); anchors.verticalCenter: parent.verticalCenter; spacing: dlg._px(2)
                            Text { text: qsTr("Global"); color: Theme.text
                                   font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontBody); font.bold: true }
                            Text { text: qsTr("Your settings"); color: Theme.text3
                                   font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontSmall) }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { if (dlg._moving) return; dlg.zone = "side"; dlg.sideIdx = 1; dlg.selectGlobal() }
                        }
                    }
                    Rectangle { y: dlg._px(124); x: dlg._px(16); width: parent.width - dlg._px(32); height: 1; color: Theme.line }

                    // The profiles. Keyed by creation, so a new order glides them instead of redrawing.
                    Repeater {
                        model: profModel
                        delegate: Item {
                            id: pd
                            required property int created
                            readonly property var inf: dlg._info[created] || ({ slot: -1, name: "", n: 0 })
                            readonly property int pos: dlg._orderCreated.indexOf(created)
                            readonly property bool foc: dlg.zone !== "sheet" && !dlg._moving && dlg.sideIdx === pos + 2
                            readonly property bool act: dlg.activeSlot === inf.slot
                            readonly property bool lifted: dlg._movingCreated === created
                            x: dlg._px(6); width: sideContent.width - dlg._px(12); height: dlg._rowH
                            y: dlg._listTop + Math.max(0, pos) * dlg._rowH
                            z: lifted ? 3 : 1
                            Behavior on y {
                                enabled: !Theme.reduceAnimations
                                SequentialAnimation {
                                    // A new sort cascades from the top; a profile being carried just moves.
                                    PauseAnimation { duration: dlg._moving ? 0 : Math.min(6, Math.max(0, pd.pos)) * 28 }
                                    NumberAnimation { duration: dlg._moving ? 150 : 300; easing.type: Easing.OutCubic }
                                }
                            }
                            opacity: 0
                            scale: 0.96
                            Component.onCompleted: { opacity = 1; scale = 1 }
                            Behavior on opacity { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 220 } }
                            Behavior on scale { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 260; easing.type: Easing.OutBack } }

                            Rectangle {
                                anchors.fill: parent; radius: dlg._px(6)
                                visible: (pd.act && !pd.foc) || pd.lifted
                                color: pd.lifted ? Theme.cardHigh
                                                 : Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.14)
                                border.width: pd.lifted ? dlg._px(2) : 1
                                border.color: pd.lifted ? Theme.accentFill
                                                        : Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.55)
                                scale: pd.lifted ? 1.04 : 1
                                Behavior on scale { enabled: !Theme.reduceAnimations
                                                    NumberAnimation { duration: 180; easing.type: Easing.OutBack; easing.overshoot: 2.2 } }
                                Rectangle {
                                    visible: pd.lifted; z: -1
                                    anchors.fill: parent; anchors.topMargin: dlg._px(6); anchors.bottomMargin: -dlg._px(6)
                                    radius: dlg._px(6); color: "#45000000"
                                }
                            }
                            Text {
                                visible: pd.lifted
                                x: dlg._px(8); anchors.verticalCenter: parent.verticalCenter
                                text: "⋮⋮"; color: Theme.inkText; font.pixelSize: dlg._px(18); font.letterSpacing: -6
                            }
                            Rectangle {
                                visible: pd.act && !pd.lifted
                                x: dlg._px(14); anchors.verticalCenter: parent.verticalCenter
                                width: dlg._px(7); height: width; radius: width / 2; color: Theme.ink
                            }
                            Column {
                                x: dlg._px(30); anchors.verticalCenter: parent.verticalCenter; spacing: dlg._px(2)
                                width: parent.width - x - dlg._px(36)
                                Text {
                                    width: parent.width
                                    text: pd.inf.name; color: Theme.text; elide: Text.ElideRight
                                    font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontBody); font.bold: true
                                }
                                Text {
                                    text: pd.inf.n === 0 ? qsTr("No changes") : pd.inf.n === 1 ? qsTr("1 change") : qsTr("%1 changes").arg(pd.inf.n)
                                    color: pd.inf.n > 0 ? Theme.inkText : Theme.text3
                                    font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontSmall)
                                }
                            }
                            Column {
                                visible: pd.lifted
                                anchors.right: parent.right; anchors.rightMargin: dlg._px(14)
                                anchors.verticalCenter: parent.verticalCenter
                                Text { text: "▲"; font.pixelSize: dlg._px(9)
                                       color: pd.pos > 0 ? Theme.inkText : Qt.rgba(Theme.text3.r, Theme.text3.g, Theme.text3.b, 0.4) }
                                Text { text: "▼"; font.pixelSize: dlg._px(9)
                                       color: pd.pos < dlg.profileCount - 1 ? Theme.inkText : Qt.rgba(Theme.text3.r, Theme.text3.g, Theme.text3.b, 0.4) }
                            }

                            // Click = use it. In Custom, a drag carries it (one profile at a time,
                            // moved for real as it crosses a row, like ▲▼ with R3).
                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                cursorShape: dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                property real pressY: 0
                                property bool dragging: false
                                // A drag ends in a release that Qt also reports as a click: kept until the next press.
                                property bool dragged: false
                                onPressed: function(m) { pressY = m.y; dragging = false; dragged = false }
                                onPositionChanged: function(m) {
                                    if (!pressed) return
                                    if (!dragging && dlg.sortMode === 2 && dlg.profileCount > 1
                                            && Math.abs(m.y - pressY) > dlg._px(8)) {
                                        dragging = true
                                        dragged = true
                                        dlg.zone = "side"
                                        dlg.sideIdx = pd.pos + 2
                                        dlg.pick()
                                    }
                                    if (dragging) {
                                        var p = mapToItem(sideContent, m.x, m.y)
                                        dlg._moveTo(Math.floor((p.y - dlg._listTop) / dlg._rowH))
                                    }
                                }
                                onReleased: { if (dragging) { dragging = false; dlg.drop(true) } }
                                onCanceled: { if (dragging) { dragging = false; dlg.drop(true) } }
                                onClicked: {
                                    if (dragged || dragging || dlg._moving) return
                                    dlg.zone = "side"
                                    dlg.sideIdx = pd.pos + 2
                                    dlg.selectSlot(pd.inf.slot)
                                }
                            }
                        }
                    }

                    // + Add a profile
                    Item {
                        x: dlg._px(6); y: dlg._sideY(dlg._addIdx); width: parent.width - dlg._px(12); height: dlg._btnH; z: 1
                        Behavior on y { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                        Rectangle {
                            anchors.fill: parent; radius: dlg._px(8)
                            color: Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.06)
                            border.width: 1; border.color: Theme.lineHigh
                            visible: !(dlg.zone !== "sheet" && dlg.sideIdx === dlg._addIdx)
                        }
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("+ Add a profile"); color: Theme.inkText
                            font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontBody); font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { if (dlg._moving) return; dlg.zone = "side"; dlg.sideIdx = dlg._addIdx; dlg.addProfile() }
                        }
                    }
                }
            }

            // The Sort menu, dropped from the button.
            Rectangle {
                id: sortMenu
                x: sideFlick.x + dlg._px(6); y: sideFlick.y + dlg._px(46) - sideFlick.contentY
                width: sideFlick.width - dlg._px(12); height: 3 * dlg._rowH + dlg._px(12)
                radius: dlg._px(8); z: 10
                color: Theme.cardHigh
                border.width: 1; border.color: Theme.lineHigh
                transformOrigin: Item.Top
                opacity: dlg.zone === "sort" ? 1 : 0
                scale: dlg.zone === "sort" ? 1 : 0.9
                visible: opacity > 0
                Behavior on opacity { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 140 } }
                Behavior on scale { enabled: !Theme.reduceAnimations
                                    NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.4 } }
                Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: dlg._px(10); anchors.bottomMargin: -dlg._px(10)
                            radius: dlg._px(8); color: "#55000000" }
                Rectangle {
                    x: dlg._px(6); width: parent.width - dlg._px(12); height: dlg._rowH; radius: dlg._px(6)
                    y: dlg._px(6) + dlg.sortIdx * dlg._rowH
                    color: Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.22)
                    border.width: dlg._px(2); border.color: Theme.accentFill
                    Behavior on y { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                }
                Column {
                    x: dlg._px(6); y: dlg._px(6); width: parent.width - dlg._px(12)
                    Repeater {
                        model: 3
                        Item {
                            required property int index
                            width: parent.width; height: dlg._rowH
                            Text {
                                x: dlg._px(12); anchors.verticalCenter: parent.verticalCenter
                                text: parent.index === dlg.sortMode ? "✓" : ""
                                color: Theme.inkText; font.pixelSize: dlg._px(14)
                            }
                            Column {
                                x: dlg._px(34); anchors.verticalCenter: parent.verticalCenter; spacing: dlg._px(2)
                                Text { text: dlg._sortNames[parent.parent.index]; color: Theme.text
                                       font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontBody); font.bold: true }
                                Text { text: dlg._sortHints[parent.parent.index]; color: Theme.text3
                                       font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontSmall) }
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: dlg.zone === "sort"
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onEntered: dlg.sortIdx = parent.index
                                onClicked: dlg.chooseSort(parent.index)
                            }
                        }
                    }
                }
            }
            // A click anywhere else closes the menu.
            MouseArea {
                anchors.fill: parent
                z: 9
                enabled: dlg.zone === "sort"
                onClicked: dlg.zone = "side"
            }
        }

        // ── The right side: the level this host streams with ──
        Item {
            id: panel
            x: side.width; y: side.y
            width: parent.width - side.width; height: side.height

            Item {
                id: profHead
                x: dlg._px(36); width: parent.width - dlg._px(72); height: dlg._px(76)
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    spacing: dlg._px(3)
                    Text {
                        id: pname
                        width: parent.width; elide: Text.ElideRight
                        text: dlg.activeSlot >= 0 && dlg._info[dlg._activeCreated] ? dlg._info[dlg._activeCreated].name : qsTr("Global")
                        color: Theme.text
                        font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontH2); font.bold: true
                        Behavior on text {
                            enabled: !Theme.reduceAnimations
                            SequentialAnimation {
                                NumberAnimation { target: pname; property: "opacity"; to: 0; duration: 80 }
                                PropertyAction {}
                                NumberAnimation { target: pname; property: "opacity"; to: 1; duration: 160 }
                            }
                        }
                    }
                    Text {
                        width: parent.width; elide: Text.ElideRight
                        readonly property int n: sheet.changes
                        text: dlg.activeSlot < 0
                              ? qsTr("Your settings, the ones in Settings — every profile starts from them")
                              : (n === 0 ? qsTr("No changes from Global") : n === 1 ? qsTr("1 change from Global")
                                         : qsTr("%1 changes from Global").arg(n))
                        color: Theme.text3
                        font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontSmall)
                    }
                }
            }

            OverrideSheet {
                id: sheet
                x: dlg._px(20); y: profHead.height
                width: parent.width - dlg._px(40); height: parent.height - y
                sections: dlg._sections
                base: dlg._base
                own: dlg._own
                isBase: dlg.activeSlot < 0
                baseName: qsTr("Global")
                baseCaptions: {
                    var out = {}
                    for (var k in dlg._counts) {
                        var n = dlg._counts[k]
                        out[k] = n === 1 ? qsTr("1 profile changes it") : qsTr("%1 profiles change it").arg(n)
                    }
                    return out
                }
                active: dlg.zone === "sheet"
                onChange: function(key, value) { dlg.applyChange(key, value) }
                onCustomRequested: function(key) {
                    if (key === "resolution") {
                        var r = sheet.eff("resolution")
                        resCustomDialog.initWidth = r ? r.w : 1920
                        resCustomDialog.initHeight = r ? r.h : 1080
                        resCustomDialog.open()
                    } else {
                        customFpsDialog.initFps = sheet.eff("fps") || 60
                        customFpsDialog.open()
                    }
                }
                onPointerEntered: { if (!dlg._moving) dlg.zone = "sheet" }
            }
        }

        // A short notice above the footer.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: foot.top; anchors.bottomMargin: dlg._px(14)
            width: toastText.implicitWidth + dlg._px(32); height: toastText.implicitHeight + dlg._px(18)
            radius: dlg._px(8)
            color: Theme.cardHigh; border.width: 1; border.color: Theme.lineHigh
            opacity: dlg._toast.length ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 160 } }
            Text { id: toastText; anchors.centerIn: parent; text: dlg._toast; color: Theme.text
                   font.family: Theme.family; font.pixelSize: dlg._px(16) }
        }

        // ── Footer: the buttons where the cursor is, as in the status bar ──
        Item {
            id: foot
            anchors.bottom: parent.bottom
            width: parent.width; height: dlg._px(60)
            Rectangle { width: parent.width; height: 1; color: Theme.line }
            Row {
                id: prompts
                x: dlg._px(24); anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(20)
                readonly property bool idleSide: dlg.zone === "side" && !dlg._moving
                readonly property bool idleSheet: dlg.zone === "sheet" && !sheet.editing

                Prompt { visible: prompts.idleSide && (dlg._onProfile || dlg.sideIdx === 1)
                         buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Use") }
                Prompt { visible: prompts.idleSide && dlg.sideIdx === 0; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Sort") }
                Prompt { visible: prompts.idleSide && dlg.sideIdx === dlg._addIdx; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Add") }
                Prompt { visible: prompts.idleSide && dlg._onProfile && dlg.sortMode === 2 && dlg.profileCount > 1
                         buttonKey: "RS"; keyLabel: "M"; action: qsTr("Move")
                         clickable: true; onClicked: dlg.pick() }
                Prompt { visible: prompts.idleSide && dlg._onProfile; buttonKey: "X"; keyLabel: "X"; action: qsTr("Rename")
                         clickable: true; onClicked: dlg.renameProfile(dlg._slotAt(dlg.sideIdx)) }
                Prompt { visible: prompts.idleSide && dlg._onProfile; buttonKey: "Y"; keyLabel: "Y"; action: qsTr("Remove")
                         clickable: true; onClicked: dlg.askRemoveProfile(dlg._slotAt(dlg.sideIdx)) }

                Prompt { visible: dlg._moving; action: "▲▼  " + qsTr("Move") }
                Prompt { visible: dlg._moving; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Place")
                         clickable: true; onClicked: dlg.drop(true) }

                Prompt { visible: dlg.zone === "sort"; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Select") }

                Prompt { visible: prompts.idleSheet && sheet.currentEnabled; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Change") }
                Prompt { visible: prompts.idleSheet && sheet.currentOver; buttonKey: "Y"; keyLabel: "Y"; action: qsTr("Use Global")
                         clickable: true; onClicked: dlg.applyChange(sheet.currentKey, undefined) }
                Prompt { visible: prompts.idleSheet && dlg.activeSlot >= 0 && sheet.changes > 0
                         buttonKey: "X"; keyLabel: "X"; action: qsTr("Reset all")
                         clickable: true; onClicked: resetConfirm.open() }
                Prompt { visible: dlg.zone === "sheet" && SdlGamepadKeyNavigation.scrollAvailable && InputHints.padActive
                         buttonKey: "RSCROLL"; action: qsTr("Scroll") }

                Prompt { visible: sheet.editing; action: "◀▶  " + (sheet.currentIsSlider ? qsTr("Adjust") : qsTr("Choose")) }
                Prompt { visible: sheet.editing; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Confirm")
                         clickable: true; onClicked: sheet.commitEdit() }

                Prompt {
                    buttonKey: "B"; keyLabel: "Esc"
                    action: (dlg._moving || sheet.editing) ? qsTr("Cancel") : dlg.zone === "sort" ? qsTr("Back") : qsTr("Close")
                    clickable: true
                    onClicked: {
                        if (dlg._moving) dlg.drop(false)
                        else if (sheet.editing) sheet.cancelEdit()
                        else if (dlg.zone === "sort") dlg.zone = "side"
                        else dlg.close()
                    }
                }
            }
            DialogButton {
                anchors.right: parent.right; anchors.rightMargin: dlg._px(24)
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Done")
                affirmative: true
                fontSize: 13
                focusPolicy: Qt.NoFocus
                width: dlg._px(96); height: dlg._px(38)
                onActivated: dlg.close()
            }
        }
    }

    // ── Child popups. Each gives the keys back to the dialog in onClosed, the last word after
    //    Qt's own focus hand-over at the end of its exit. ──
    CustomResolutionDialog {
        id: resCustomDialog
        onAccepted: function(w, h) { sheet._set("resolution", { w: w, h: h }) }
        onClosed: if (dlg.opened) root.forceActiveFocus()
    }
    CustomFrameRateDialog {
        id: customFpsDialog
        onAccepted: function(fps) { sheet._set("fps", fps) }
        onClosed: if (dlg.opened) root.forceActiveFocus()
    }
    ProfileNameDialog {
        id: nameDialog
        property int slot: -1
        property int created: -1
        onAccepted: function(name) {
            dlg.computerModel.setHostProfileName(dlg.pcIndex, slot, name)
            dlg.reload()
            // Alphabetical moves a renamed profile: the cursor goes with it.
            dlg._cursorTo(created)
        }
        onClosed: if (dlg.opened) root.forceActiveFocus()
    }
    NavigableMessageDialog {
        id: removeConfirm
        parent: Overlay.overlay
        property int slot: -1
        property string pcName: ""
        headerText: qsTr("REMOVE PROFILE")
        text: qsTr("Remove the profile '%1'?").arg(pcName)
        standardButtons: Dialog.Yes | Dialog.No
        restoreFocus: false
        onAccepted: dlg.removeProfile(slot)
        onClosed: if (dlg.opened) root.forceActiveFocus()
    }
    NavigableMessageDialog {
        id: resetConfirm
        parent: Overlay.overlay
        headerText: qsTr("RESET ALL")
        text: qsTr("Give every setting of this profile back to Global?")
        standardButtons: Dialog.Yes | Dialog.No
        restoreFocus: false
        onAccepted: dlg.resetAll()
        onClosed: if (dlg.opened) root.forceActiveFocus()
    }
}
