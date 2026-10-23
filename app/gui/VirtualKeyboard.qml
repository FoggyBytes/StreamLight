import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import SdlGamepadKeyNavigation 1.0
import InputHints 1.0

/*
 * ── The on-screen keyboard (8.0.0) ─────────────────────────────────────────────────────────
 *
 * For the few places that need typing — a profile's name, a host's name or address, the accent
 * colour, a custom frame rate or resolution — when the hand is on a pad. Variant B of the
 * mockup in docs/mockups/tastiera (a card at the bottom, centred, that the right stick moves
 * out of the way), chosen on 21/10/2026.
 *
 * Every field that uses it declares one beside itself and hands it the key that would act on
 * the field:
 *
 *     VirtualKeyboard { id: vk; target: nameField; mode: "text"; onAccepted: pop._commit(...) }
 *     TextField { id: nameField
 *         Keys.onReturnPressed: function(event) { if (!vk.offer(event)) pop._commit(...) }
 *         Keys.onSpacePressed:  function(event) { event.accepted = vk.offer(event) } }
 *
 * offer() opens it only for a key the PAD sent (no scan code: SdlGamepadKeyNavigation builds
 * its events by hand) — Enter on a real keyboard still does what it always did. Space is there
 * for a field on the Settings page itself, where the pad's A arrives as Space (in a popup it is
 * Return since 8.0.0).
 *
 * ⚠️ It opens ONLY on A (21/10). It used to open by itself as the field took the focus, which
 * put it up whenever the D-pad merely crossed the field. And it is MODAL: the focus stays on
 * its keys until B closes it — non-modal, the dialog underneath kept the keys.
 *
 * The pad, while it is open:
 *   D-pad ←↑→↓ (Tab / Shift+Tab in Settings)  move between the keys
 *   A (Return, Space in Settings)            type the key under the cursor
 *   B (Escape)                               close — the text stays as it is. Handled by the
 *                                            popup's CloseOnEscape: as the topmost popup it
 *                                            takes Escape before the dialog below can
 *   X (Key_Menu)        delete               Y (Key_Hangup)  space
 *   LB / RB (F16/F17)   move the text cursor LT (F14)        shift
 *   RT (F15)            symbols              Start (F18)     Done = accepted()
 *   right stick         move the keyboard (SdlGamepadKeyNavigation.scrollVelocity / X)
 *
 * `mode`: text (letters, digits, common symbols), address (no symbols page: . : - _ /),
 * hex (0-9 A-F #) and number (a keypad).
 */
Popup {
    id: vk

    // ── What it edits and how ─────────────────────────────────────────────────────────────
    property Item target: null                  // a TextField
    property string mode: "text"                // text · address · hex · number
    signal accepted()
    // After every change it makes to the text — insert() and remove() do not raise the
    // field's textEdited, and a field that previews what is typed needs to know.
    signal edited()

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }
    function _fpx(n) { return Math.round(n * _u * Theme.textScale) }

    parent: Overlay.overlay
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    background: null

    // ── Open / close ─────────────────────────────────────────────────────────────────────
    // The key the pad sent on the field: open instead of letting it act. Returns whether it did.
    function offer(event) {
        // A real key, with a scan code — unless Steam's Desktop Layout is about (8.0.0): its
        // keyboard copy of A can arrive before the pad's own, and the pad's is then dropped as an
        // echo. With that layout seen and the pad in hand, its Return counts as the pad's A.
        if (event.nativeScanCode !== 0
                && !(SdlGamepadKeyNavigation.desktopLayoutEchoes && InputHints.padActive)) return false
        if (!opened) open()
        event.accepted = true
        return true
    }

    Connections {
        target: vk.target
        // The dialog went away under it (it lives in the overlay, not in the dialog).
        function onVisibleChanged() { if (!vk.target.visible) vk.close() }
    }

    property bool _keptSelection: false
    // Closed by Done: the owner's accept handler places the focus, never this keyboard.
    property bool _accepted: false
    onAboutToShow: {
        _accepted = false
        _page = "main"; _shift = false
        _row = Math.min(1, _rows.length - 1)
        _col = 0
        offX = 0; offY = 0
        // The selection (a dialog opens with the name selected) has to survive the focus moving
        // here, so the first key replaces it as it would on a real keyboard.
        if (target) { _keptSelection = target.persistentSelection; target.persistentSelection = true }
    }
    onOpened: {
        // The keys, not the field: every pad button belongs to the keyboard while it is up.
        contentItem.forceActiveFocus()
    }
    /*
     * ⚠️⚠️ Where the focus goes after Done (21/10/2026, read in Qt 6.12's qquickpopup.cpp).
     * A popup that had the active focus when its close began hands it, at the END of its exit,
     * to the topmost popup still open — but only to one that does not have it already; if that
     * one has it, Qt gives it to the PAGE (contentItem). The keyboard used to close holding the
     * focus, so after Done on a profile name — the name dialog gone, the profile dialog already
     * refocused by its owner — Qt put the focus on Home under the still open profile dialog.
     *
     * So the field takes the focus BEFORE accepted() is emitted: the keyboard then closes
     * without it and its exit moves nothing, and what happens next is the owner's (its dialog
     * closing, or staying open on an invalid value). If the owner did not close the keyboard,
     * the focus comes back to the keys. After B the focus goes back to the field as before.
     */
    function _done() {
        _accepted = true
        if (target && target.visible) target.forceActiveFocus()
        accepted()
        if (opened) {
            // Not closed by the owner (a value it refuses): the keys keep the pad.
            _accepted = false
            contentItem.forceActiveFocus()
        }
    }
    onClosed: {
        if (!target) return
        target.persistentSelection = _keptSelection
        if (_accepted) { _accepted = false; return }
        if (target.visible) target.forceActiveFocus()
    }

    // ── Keys ─────────────────────────────────────────────────────────────────────────────
    property string _page: "main"       // main · sym
    property bool _shift: false
    property int _row: 1
    property int _col: 0

    // A key: { k: what it types or does, l: label, w: width in units }. Rows add up to 10 units
    // (5 for hex, 3 for number).
    function _K(k, w, l) { return { k: k, w: w || 1, l: l !== undefined ? l : k } }
    function _letters(s) { var r = []; for (var i = 0; i < s.length; i++) r.push(_K(s[i])); return r }

    readonly property var _rows: {
        if (mode === "number")
            return [ _letters("789"), _letters("456"), _letters("123"),
                     [ _K("bksp", 1, "⌫"), _K("0"), _K("done", 1, qsTr("Done")) ] ]
        if (mode === "hex")
            return [ _letters("789AB"), _letters("456CD"), _letters("123EF"),
                     [ _K("#"), _K("0"), _K("bksp", 1, "⌫"), _K("done", 2, qsTr("Done")) ] ]
        var digits = _letters("1234567890")
        if (_page === "sym")
            return [ digits,
                     _letters("!@#$%&*()?"),
                     _letters("-_=+/\\:;\"'"),
                     [ _K("shift", 1.5, "⇧") ].concat(_letters("[]<>~|"), [ _K("bksp", 2.5, "⌫") ]),
                     _bottom(true) ]
        return [ digits,
                 _letters("qwertyuiop"),
                 _letters("asdfghjkl").concat([ _K("'") ]),
                 [ _K("shift", 1.5, "⇧") ].concat(_letters("zxcvbnm"), [ _K("bksp", 1.5, "⌫") ]),
                 _bottom(false) ]
    }
    function _bottom(sym) {
        // Address: the characters an address uses, no symbols page.
        if (mode === "address")
            return [ _K("."), _K(":"), _K("-"), _K("_"), _K("/"), _K("space", 3, qsTr("space")), _K("done", 2, qsTr("Done")) ]
        return [ _K("sym", 1.5, sym ? "abc" : "#+="), _K(","), _K("space", 4.5, qsTr("space")), _K("."), _K("done", 2, qsTr("Done")) ]
    }

    // Where a key starts, in units, along its row.
    function _unitX(r, i) { var x = 0; var row = _rows[r]; for (var j = 0; j < i; j++) x += row[j].w; return x }
    readonly property int _units: mode === "number" ? 3 : mode === "hex" ? 5 : 10

    function moveH(d) {
        var n = _rows[_row].length
        _col = (_col + d + n) % n
    }
    function moveV(d) {
        var r = _row + d
        if (r < 0 || r >= _rows.length) return
        // The key in the next row whose centre is nearest this one's.
        var c = _unitX(_row, _col) + _rows[_row][_col].w / 2
        var best = 0, bestD = 1e9
        for (var i = 0; i < _rows[r].length; i++) {
            var m = _unitX(r, i) + _rows[r][i].w / 2
            if (Math.abs(m - c) < bestD) { bestD = Math.abs(m - c); best = i }
        }
        _row = r; _col = best
    }
    on_RowsChanged: { _row = Math.min(_row, _rows.length - 1); _col = Math.min(_col, _rows[_row].length - 1) }

    property int _flashRow: -1
    property int _flashCol: -1
    Timer { id: flashTimer; interval: 120; onTriggered: { vk._flashRow = -1; vk._flashCol = -1 } }

    function pressCurrent() {
        _flashRow = _row; _flashCol = _col; flashTimer.restart()
        act(_rows[_row][_col].k)
    }

    // One key's effect on the field.
    function act(k) {
        if (k === "shift") { _shift = !_shift; return }
        if (k === "sym")   { _page = _page === "sym" ? "main" : "sym"; return }
        if (k === "done")  { _done(); return }
        if (!target) return
        var p = target.cursorPosition
        var s0 = target.selectionStart, s1 = target.selectionEnd
        if (k === "left")  { target.deselect(); target.cursorPosition = Math.max(0, p - 1); return }
        if (k === "right") { target.deselect(); target.cursorPosition = Math.min(target.length, p + 1); return }
        if (k === "bksp") {
            if (s1 > s0) { target.remove(s0, s1); target.cursorPosition = s0 }
            else if (p > 0) { target.remove(p - 1, p); target.cursorPosition = p - 1 }
            edited()
            return
        }
        var s = k === "space" ? " " : k
        if (_shift && s.length === 1) s = s.toUpperCase()
        if (mode === "hex") s = s.toUpperCase()
        // A selection is replaced, as on a real keyboard.
        if (s1 > s0) { target.remove(s0, s1); p = s0 }
        if (target.length >= target.maximumLength) { edited(); return }
        // insert() goes through the field's validator: a key it refuses changes nothing.
        var before = target.length
        target.insert(p, s)
        target.cursorPosition = p + (target.length - before)
        edited()
    }

    // The keys the pad sends, as listed at the top. Returns whether it took the key.
    function handleKey(key, modifiers) {
        switch (key) {
        case Qt.Key_Left:    moveH(-1); return true
        case Qt.Key_Right:   moveH(1);  return true
        case Qt.Key_Up:      moveV(-1); return true
        case Qt.Key_Down:    moveV(1);  return true
        case Qt.Key_Backtab: moveV(-1); return true
        case Qt.Key_Tab:     moveV((modifiers & Qt.ShiftModifier) ? -1 : 1); return true
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:   pressCurrent(); return true
        // The pad's B is sent straight to the window, past the shortcut map, so it arrives
        // here as a key and not as the popup's CloseOnEscape.
        case Qt.Key_Escape:  close(); return true
        case Qt.Key_Menu:    act("bksp");  return true
        case Qt.Key_Hangup:  act("space"); return true
        case Qt.Key_F16:     act("left");  return true
        case Qt.Key_F17:     act("right"); return true
        case Qt.Key_F14:     act("shift"); return true
        case Qt.Key_F15:     if (mode === "text") act("sym"); return true
        case Qt.Key_F18:     act("done");  return true
        }
        return false
    }

    // ── Moving it with the right stick ─────────────────────────────────────────────────────
    property real offX: 0
    property real offY: 0
    Timer {
        interval: 16; repeat: true
        running: vk.opened && (SdlGamepadKeyNavigation.scrollVelocity !== 0
                               || SdlGamepadKeyNavigation.scrollVelocityX !== 0)
        property double _last: 0
        onRunningChanged: _last = Date.now()
        onTriggered: {
            var now = Date.now(), dt = Math.min(0.05, (now - _last) / 1000); _last = now
            vk.offX += SdlGamepadKeyNavigation.scrollVelocityX * dt
            vk.offY += SdlGamepadKeyNavigation.scrollVelocity * dt
            vk._clamp()
        }
    }
    function _clamp() {
        var W = parent ? parent.width : 0
        offX = Math.max(-_baseX, Math.min(W - width - _baseX, offX))
        offY = Math.max(-_baseY, Math.min(0, offY))
    }

    // ── Geometry ─────────────────────────────────────────────────────────────────────────
    readonly property int _gap: _px(6)                                  // between keys
    readonly property int _pad: _px(14)                                 // inside the panel
    readonly property int _keyH: _px(46)
    readonly property int _promptH: _px(34)

    readonly property real _areaW: parent ? parent.width : 0
    readonly property real _areaH: parent ? parent.height : 0
    // The width of the block of keys.
    readonly property int _blockW: mode === "number" ? _px(330)
                                 : mode === "hex"    ? _px(470)
                                 : Math.min(_areaW * 0.62, _px(860)) - _pad * 2
    readonly property real _unitW: (_blockW - _gap * (_units - 1)) / _units
    function _keyX(r, i) { return _unitX(r, i) * (_unitW + _gap) }
    function _keyW(r, i) { var w = _rows[r][i].w; return w * _unitW + (w - 1) * _gap }

    readonly property real _baseX: (_areaW - width) / 2
    readonly property real _baseY: _areaH - height - _px(24)
    // As wide as the keys or the prompts underneath, whichever is wider: the hex pad and the
    // keypad are narrower than their own prompt line, which used to run past both edges.
    width: Math.max(_blockW, promptRow.implicitWidth) + _pad * 2
    height: _rows.length * (_keyH + _gap) - _gap + _pad * 2 + _promptH
    x: _baseX + offX
    y: _baseY + offY + _rise * _px(60)

    // In and out: up from a little below, fading — none of it with Reduce animations.
    property real _rise: 0
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.reduceAnimations ? 0 : 200; easing.type: Easing.OutCubic }
        NumberAnimation { property: "_rise"; from: 1; to: 0; duration: Theme.reduceAnimations ? 0 : 200; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.reduceAnimations ? 0 : 140; easing.type: Easing.InCubic }
        NumberAnimation { property: "_rise"; from: 0; to: 1; duration: Theme.reduceAnimations ? 0 : 140; easing.type: Easing.InCubic }
    }

    // ── Drawing ──────────────────────────────────────────────────────────────────────────
    contentItem: Item {
        focus: true

        /*
         * The field's text cursor, drawn by the keyboard (21/10/2026). Material draws a
         * TextField's cursor only while the FIELD has the active focus (its CursorDelegate:
         * `visible: parent.activeFocus && …`), and with this keyboard modal the focus is here:
         * LB/RB moved the cursor and typing went where it was, but nothing on screen moved.
         * `cursorVisible` on the field does not help — Material's delegate does not read it.
         * Same look as Material's: 2 px, blinking, steady again on every move; hidden over a
         * selection, as there.
         */
        Rectangle {
            id: fieldCursor
            readonly property Item _t: vk.target
            // Where the field's cursor is, in this item's coordinates. vk.x / vk.y are read so
            // the mapping follows the keyboard when the right stick moves it.
            readonly property rect _r: {
                vk.x; vk.y
                if (!_t) return Qt.rect(0, 0, 0, 0)
                var c = _t.cursorRectangle
                var p = _t.mapToItem(fieldCursor.parent, c.x, c.y)
                return Qt.rect(p.x, p.y, c.width, c.height)
            }
            visible: vk.opened && _t !== null && _t.visible && !_t.activeFocus
                     && _t.selectionStart === _t.selectionEnd
            x: Math.round(_r.x)
            y: Math.round(_r.y)
            width: 2
            height: _r.height
            color: Theme.ink
            Timer {
                id: blink
                interval: Qt.styleHints.cursorFlashTime > 0 ? Qt.styleHints.cursorFlashTime / 2 : 0
                running: fieldCursor.visible && interval > 0
                repeat: true
                onTriggered: fieldCursor.opacity = fieldCursor.opacity > 0 ? 0 : 1
                onRunningChanged: fieldCursor.opacity = 1
            }
            Connections {
                target: fieldCursor._t
                function onCursorPositionChanged() { fieldCursor.opacity = 1; blink.restart() }
            }
        }

        Keys.onPressed: function(event) {
            if (event.nativeScanCode === 0) { event.accepted = vk.handleKey(event.key, event.modifiers); return }
            // A real keyboard: it types, it does not steer the cursor around the keys.
            if (event.key === Qt.Key_Backspace) { vk.act("bksp"); event.accepted = true; return }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { vk.act("done"); event.accepted = true; return }
            if (event.key === Qt.Key_Left)  { vk.act("left");  event.accepted = true; return }
            if (event.key === Qt.Key_Right) { vk.act("right"); event.accepted = true; return }
            if (event.text.length === 1 && event.text >= " ") {
                var s = vk.mode === "hex" ? event.text.toUpperCase() : event.text
                var shift = vk._shift; vk._shift = false
                vk.act(s === " " ? "space" : s)
                vk._shift = shift
                event.accepted = true
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: vk._px(14)
            color: Theme.card
            border.color: Theme.lineHigh
            border.width: 1
        }

        // The keys.
        Repeater {
            model: vk._rows.length
            Item {
                id: rowItem
                readonly property int r: index
                x: (vk.width - vk._blockW) / 2
                y: vk._pad + r * (vk._keyH + vk._gap)
                width: vk._blockW; height: vk._keyH
                Repeater {
                    model: vk._rows[rowItem.r]
                    Rectangle {
                        readonly property bool _cur: rowItem.r === vk._row && index === vk._col
                        readonly property bool _flash: rowItem.r === vk._flashRow && index === vk._flashCol
                        readonly property bool _special: modelData.k.length > 1
                        readonly property bool _lit: (modelData.k === "shift" && vk._shift)
                                                     || (modelData.k === "sym" && vk._page === "sym")
                        x: vk._keyX(rowItem.r, index)
                        width: vk._keyW(rowItem.r, index)
                        height: vk._keyH
                        radius: vk._px(8)
                        color: _flash ? Theme.accentFill
                             : _lit   ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                             : modelData.k === "done" ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14)
                             : _special ? Theme.cardHigh
                             : Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.06))
                        border.color: _cur ? Theme.ink : Theme.line
                        border.width: _cur ? 3 : 1
                        scale: _flash && !Theme.reduceAnimations ? 0.94 : 1
                        Behavior on scale { NumberAnimation { duration: 90 } }
                        Text {
                            anchors.centerIn: parent
                            text: {
                                var l = modelData.l
                                if (l.length === 1 && vk._shift) return l.toUpperCase()
                                if (vk.mode === "hex") return l.toUpperCase()
                                return l
                            }
                            color: parent._flash ? Theme.onAccent
                                 : modelData.k === "done" ? Theme.inkText : Theme.text
                            font.family: Theme.family
                            font.pixelSize: vk._fpx(modelData.l.length > 1 ? Theme.fontSmall : Theme.fontTitle)
                            font.bold: modelData.k === "done"
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { vk._row = rowItem.r; vk._col = index; vk.pressCurrent() }
                        }
                    }
                }
            }
        }

        // What the pad's buttons do, along the bottom.
        Row {
            id: promptRow
            // Shift only where there are letters to shift, Symbols only on the text keyboard.
            readonly property var _items: [ ["A", qsTr("Type")], ["X", qsTr("Delete")], ["Y", qsTr("Space")],
                                            ["LB", ""], ["RB", qsTr("Cursor")] ]
                .concat(vk.mode === "text" || vk.mode === "address" ? [ ["LT", qsTr("Shift")] ] : [],
                        vk.mode === "text" ? [ ["RT", qsTr("Symbols")] ] : [],
                        [ ["START", qsTr("Done")], ["B", qsTr("Close")], ["RSMOVE", qsTr("Move")] ])
            x: (parent.width - width) / 2
            y: parent.height - vk._promptH + vk._px(4)
            height: vk._promptH - vk._px(8)
            spacing: vk._px(14)
            Repeater {
                model: parent._items
                Row {
                    spacing: vk._px(5)
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    PadGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        buttonKey: modelData[0]
                        size: vk._px(20)
                    }
                    Text {
                        visible: text.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData[1]
                        color: Theme.text2
                        font.family: Theme.family
                        font.pixelSize: vk._fpx(Theme.fontCaption)
                    }
                }
            }
        }
    }
}
