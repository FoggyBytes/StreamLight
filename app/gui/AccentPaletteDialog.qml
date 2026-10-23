import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import SdlGamepadKeyNavigation 1.0

/*
 * The accent palette (7.0.0, issue #33 — variant A of the mockup).
 *
 * Twelve hues in two rows, vivid over pale, from Theme.accentPalette. The five presets the
 * Settings row used to show are all in the vivid row, so nobody loses the colour they had.
 *
 * ⚠️ The cursor IS the preview. Every move calls Theme.previewAccent(), which repaints the whole
 * app without saving — and there is no scrim, so the Settings screen behind this popup is the
 * preview, at full size. A confirms (Theme.accent, which saves); B, Esc or a click outside put
 * back the colour the popup opened on. That is why closing is handled in onClosed and not in
 * the buttons: every way out has to pass through the restore unless a choice was confirmed.
 *
 * The hex field stays, last, for keyboards: a pad cannot type, so it is not where focus lands.
 *
 * ⚠️ What A and B do is said in the app's status bar, which the popup takes over while it is
 * open (AppShell.takeOverBar) — not in a footer of its own: with no scrim the bar stays in
 * sight, and two rows of prompts at once read as two sets of controls. Same as Background.
 *
 * D-pad: the grid is ONE focusable element with a cursor of its own, as in ProfileNameDialog —
 * twenty-four focus stops wired through a Repeater would not see each other.
 */
Popup {
    id: pop

    // 7.0.0: the shared spring in and quick out (PopEnter/PopExit), off with Reduce animations.
    enter: PopEnter {}
    exit: PopExit {}

    readonly property var _palette: Theme.accentPalette
    readonly property int _cols: 12

    // The shared dialog scale — see SettingsResetDialog for why a dialog cannot take this
    // from the page behind it.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    // What was on screen when the popup opened: the colour B goes back to, and the one the
    // tick marks as current.
    property color _original: Theme.accent
    property bool  _committed: false

    // Set by SettingsScreen: whose status bar says what A and B do while this is open.
    property var appShell: null
    readonly property var barHints: [
        { btn: "A", key: "Enter", act: qsTr("Apply"),  kind: "apply" },
        { btn: "B", key: "Esc",   act: qsTr("Cancel"), kind: "cancel" }
    ]
    function barAction(kind) {
        if (kind === "apply") {
            var c = hexField.activeFocus ? hexField._full() : pop._palette[grid.cursor].color
            if (c !== "") pop._commit(c)
        } else if (kind === "cancel") {
            pop.close()
        }
    }
    onAboutToShow: if (appShell) appShell.takeOverBar(pop)
    onAboutToHide: if (appShell) appShell.releaseBar(pop)

    modal: true
    // No scrim, on purpose: the page behind is the preview. Settings opens its small editors
    // the same way (§80.9).
    Overlay.modal: Item {}
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: pop._px(28)

    background: Rectangle {
        color: Theme.card
        border.color: Theme.lineHigh // 7.0.0: no scrim, so the edge has to separate it from the card below
        border.width: 1
        radius: pop._px(12)
    }

    function _same(a, b) { return Qt.colorEqual(a, b) }

    function _indexOf(c) {
        for (var i = 0; i < _palette.length; i++)
            if (_same(_palette[i].color, c)) return i
        return -1
    }

    function _commit(c) {
        pop._committed = true
        Theme.accent = c
        pop.close()
    }

    VirtualKeyboard {
        id: hexKeyboard
        target: hexField
        mode: "hex"
        // What is typed shows at once, as it does from a keyboard (textEdited does not fire).
        onEdited: { var c = hexField._full(); if (c.length > 0) Theme.previewAccent(c) }
        onAccepted: { var c = hexField._full(); if (c.length > 0) { close(); pop._commit(c) } }
    }

    onOpened: {
        pop._original = Theme.accent
        pop._committed = false
        var i = pop._indexOf(pop._original)
        grid.cursor = i >= 0 ? i : 0
        hexField.text = pop._original.toString()
        grid.forceActiveFocus()
    }

    // Every way out that was not a confirmation puts the colour back.
    onClosed: {
        if (!pop._committed) Theme.previewAccent(pop._original)
    }

    contentItem: ColumnLayout {
        spacing: pop._px(20)

        Label {
            text: qsTr("ACCENT COLOUR")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: pop._u * 1.6
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        // ── The palette ─────────────────────────────────────────────────────────
        FocusScope {
            id: grid
            activeFocusOnTab: true
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: swatches.implicitWidth
            implicitHeight: swatches.implicitHeight

            property int cursor: 0
            // Only a MOVE previews. Gaining the focus does not: opening the popup on a colour
            // typed by hand puts the cursor on the first swatch, and that must not repaint the
            // app before the user has touched anything.
            onCursorChanged: if (grid.activeFocus) Theme.previewAccent(pop._palette[grid.cursor].color)

            Grid {
                id: swatches
                columns: pop._cols
                spacing: pop._px(12)

                Repeater {
                    model: pop._palette
                    delegate: Item {
                        id: cell
                        width: pop._px(44); height: width

                        readonly property bool _cursor: grid.activeFocus && grid.cursor === index
                        readonly property bool _current: pop._same(modelData.color, pop._original)

                        // The focus ring is drawn in the text colour, not the accent: the
                        // accent IS the swatch under the cursor, so a ring in it would vanish.
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width + pop._px(10); height: width
                            radius: width / 2
                            color: "transparent"
                            border.color: Theme.text
                            border.width: pop._px(3)
                            visible: cell._cursor
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: modelData.color
                            border.color: Theme.line
                            border.width: 1
                            scale: cell._cursor && !Theme.reduceAnimations ? 1.08 : 1.0
                            Behavior on scale {
                                enabled: !Theme.reduceAnimations
                                NumberAnimation { duration: 130; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: cell._current
                                text: "✓"
                                color: Theme.onColor(modelData.color)
                                font.pixelSize: pop._px(Theme.fontTitle)
                                font.bold: true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            // Hover moves the cursor only for a mouse user: with the pad pinned
                            // (Button prompts = Controller, 6.4.1) a pointer resting on a swatch
                            // must not keep repainting the app under the D-pad.
                            onEntered: if (SdlGamepadKeyNavigation.inputMode !== "key") {
                                grid.cursor = index
                                grid.forceActiveFocus()
                            }
                            onClicked: pop._commit(modelData.color)
                        }
                    }
                }
            }

            Keys.onLeftPressed: function(event) {
                if (grid.cursor % pop._cols > 0) grid.cursor--
                event.accepted = true
            }
            Keys.onRightPressed: function(event) {
                if (grid.cursor % pop._cols < pop._cols - 1) grid.cursor++
                event.accepted = true
            }
            function _up() { if (grid.cursor >= pop._cols) grid.cursor -= pop._cols }
            function _down() {
                // Down leaves the grid only from its last row — inside it, it is a move.
                if (grid.cursor + pop._cols < pop._palette.length) grid.cursor += pop._cols
                else { hexField.forceActiveFocus(); hexField.selectAll() }
            }
            Keys.onUpPressed:   function(event) { grid._up();   event.accepted = true }
            Keys.onDownPressed: function(event) { grid._down(); event.accepted = true }
            // ⚠️ Opened from Settings, the D-pad's up and down arrive as Shift+Tab and Tab
            // (SdlGamepadKeyNavigation's UI nav mode), so the arrows above never fired there and
            // down went straight to Custom: only the vivid row could be reached (7.0.0).
            // ⚠️⚠️ And the pad's Shift+Tab is Key_Tab with Shift, not Key_Backtab (that is what a
            // real keyboard sends): it reaches onTabPressed, so "up" from the pale row went DOWN,
            // into the hex field (09/10/2026). The modifier decides.
            Keys.onTabPressed:     function(event) { if ((event.modifiers & Qt.ShiftModifier)) grid._up(); else grid._down(); event.accepted = true }
            Keys.onBacktabPressed: function(event) { grid._up();   event.accepted = true }
            Keys.onReturnPressed: pop._commit(pop._palette[grid.cursor].color)
            Keys.onEnterPressed:  pop._commit(pop._palette[grid.cursor].color)
            Keys.onSpacePressed:  pop._commit(pop._palette[grid.cursor].color)
        }

        // ── Or type one. Last, because a pad cannot use it ──────────────────────
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: pop._px(12)

            Label {
                text: qsTr("Custom")
                color: Theme.text2
                font.family: Theme.family
                font.pixelSize: pop._px(Theme.fontSmall)
                Layout.alignment: Qt.AlignVCenter
            }

            TextField {
                id: hexField
                Layout.preferredWidth: pop._px(130)
                implicitHeight: pop._px(38)
                // Six hex digits with an optional leading #. Anything the validator rejects
                // never reaches Theme.
                validator: RegularExpressionValidator { regularExpression: /#?[0-9A-Fa-f]{0,6}/ }
                maximumLength: 7
                horizontalAlignment: TextInput.AlignHCenter
                color: Theme.text
                selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
                selectedTextColor: Theme.text
                font.family: Theme.family
                font.pixelSize: pop._px(Theme.fontSmall)
                font.bold: true
                background: Rectangle {
                    color: Theme.cardHigh
                    radius: pop._px(6)
                    border.color: hexField.activeFocus ? Theme.ink : Theme.line
                    border.width: hexField.activeFocus ? 2 : 1
                }

                function _full() {
                    var t = text.trim()
                    if (t.length > 0 && t.charAt(0) !== "#") t = "#" + t
                    // Six digits exactly: Qt would happily expand "#abc", which is a different
                    // colour from the one a half-typed code was heading towards.
                    return /^#[0-9A-Fa-f]{6}$/.test(t) ? t : ""
                }

                // Follows the cursor while the user is elsewhere, so the field always shows
                // the code of the colour on screen.
                Binding on text {
                    // Nor while the on-screen keyboard is typing into it: the focus is there then.
                    when: !hexField.activeFocus && !hexKeyboard.visible
                    value: Theme.accent.toString()
                }
                onTextEdited: { var c = _full(); if (c.length > 0) Theme.previewAccent(c) }
                // The pad's A (Space, in Settings) opens the on-screen keyboard (8.0.0).
                Keys.onReturnPressed: function(event) { if (hexKeyboard.offer(event)) return; var c = _full(); if (c.length > 0) pop._commit(c); event.accepted = true }
                Keys.onEnterPressed:  function(event) { if (hexKeyboard.offer(event)) return; var c = _full(); if (c.length > 0) pop._commit(c); event.accepted = true }
                Keys.onSpacePressed:  function(event) { event.accepted = hexKeyboard.offer(event) }
                // Back on the grid, the cursor's colour is what A would choose: show it.
                function _backToGrid() {
                    grid.forceActiveFocus()
                    Theme.previewAccent(pop._palette[grid.cursor].color)
                }
                Keys.onUpPressed:      function(event) { _backToGrid(); event.accepted = true }
                Keys.onBacktabPressed: function(event) { _backToGrid(); event.accepted = true }
                // The last stop: Tab (the D-pad's down in Settings) stays here; the pad's up is
                // Tab with Shift, and goes back to the grid.
                Keys.onTabPressed:     function(event) { if ((event.modifiers & Qt.ShiftModifier)) _backToGrid(); event.accepted = true }
            }

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: pop._px(28); implicitHeight: pop._px(28)
                radius: pop._px(6)
                color: Theme.accent
                border.color: Theme.lineHigh
                border.width: 1
            }
        }
    }
}
