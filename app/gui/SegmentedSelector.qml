import Theme 1.0
import SdlGamepadKeyNavigation 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5

// Row of pill buttons; one selected at a time. D-pad ◀/▶ cycle, ▲/▼ propagate.
// Flat replacement for ComboBox dropdowns in SettingsScreen.
FocusScope {
    id: selector

    property var labels: []
    property int currentIndex: 0
    signal activated(int index)

    // Indices that are shown greyed-out and cannot be selected (skipped by ◀/▶
    // navigation and by clicks). Empty by default.
    property var disabledIndices: []

    /*
     * Indices that are not drawn at all, and that navigation steps over as if they were
     * not in the list. Empty by default.
     *
     * ⚠️ Hidden, not removed from `labels`: the override panels keep parallel arrays of
     * labels and values, and every one of their lookups is by index. Taking an entry out
     * of the list would shift the mapping under all of them; taking it out of the drawing
     * leaves it exactly where it was.
     */
    property var hiddenIndices: []

    /*
     * Indices carrying a small accent dot: the values that came from this machine's own
     * display rather than from our preset list (5.5.0). Empty by default.
     *
     * A marker and not a different label, because the pill has to stay a pill — "165" is
     * the value, and the dot is why it is on offer here and not on someone else's screen.
     * Selection and navigation ignore this entirely.
     */
    property var nativeIndices: []

    /*
     * ── The inherited state (6.0.0) ─────────────────────────────────────────────────────
     *
     * An override strip's index 0 means "no override": the value comes from Global, or from
     * the host profile in the per-game panel. Until now that was a pill of its own reading
     * "Global · 4K", drawn selected — in the accent, like a value the user had chosen. Every
     * row of an untouched profile therefore looked modified, which is the one thing the panel
     * exists to tell apart. So: the accent now means "this profile changes it", and inherited
     * is drawn NEUTRAL.
     *
     * `inheritStrip` turns that on. `inheritIndex` says which of the real options equals the
     * inherited value — the caller knows it, this control cannot: it sees labels, not the
     * settings behind them. When it is set:
     *
     *   · index 0 is not drawn and navigation steps over it, exactly like a hidden index;
     *   · with currentIndex 0 the inheritIndex pill carries a neutral highlight, so the strip
     *     still shows where the value sits;
     *   · with an override, the chosen pill takes the accent and the inherited one keeps a
     *     small dot — where you would land if you gave the value back;
     *   · CHOOSING the inherited option resets the row instead of storing the same value
     *     again. That is the whole reason index 0 could stop being a pill: "same as Global"
     *     and "Global" are one state, and it used to be two.
     *
     * ⚠️ inheritIndex may legitimately be -1: the inherited value need not be on the strip
     * (a custom resolution, a frame rate no display reports). Then nothing is highlighted
     * while the row is inherited — the row's own caption says what Global holds, and Y on the
     * row is what gives the value back.
     */
    property bool inheritStrip: false
    property int  inheritIndex: -1

    // Where the selection is DRAWN. Not currentIndex: index 0 is the inherit slot, and what
    // it means on screen is "the option that happens to equal Global".
    readonly property int _visualIndex: (inheritStrip && currentIndex === 0) ? inheritIndex
                                                                            : currentIndex
    readonly property bool _inheritedSel: inheritStrip && currentIndex === 0

    // Where ◀ stops. With an inherit strip index 0 is not a place the cursor can be.
    readonly property int _firstIndex: inheritStrip ? 1 : 0

    /*
     * One place decides what a pill press means, because there are two of them — the mouse
     * and ◀/▶ — and they must not disagree about the reset.
     */
    function _choose(i) {
        var target = (inheritStrip && i === inheritIndex) ? 0 : i
        if (selector.currentIndex === target && selector.currentIndex !== -1) return
        selector.currentIndex = target
        selector.activated(target)
    }

    function isNative(i) {
        for (var k = 0; k < nativeIndices.length; ++k)
            if (nativeIndices[k] === i)
                return true
        return false
    }

    function isDisabled(i) {
        for (var k = 0; k < disabledIndices.length; ++k)
            if (disabledIndices[k] === i)
                return true
        return false
    }

    function isHidden(i) {
        // The inherit slot is never drawn: it has no value of its own to show.
        if (inheritStrip && i === 0) return true
        for (var k = 0; k < hiddenIndices.length; ++k)
            if (hiddenIndices[k] === i)
                return true
        return false
    }

    // What ◀/▶ must step over: a greyed option cannot be chosen, and one that is not on
    // screen must not be either — landing on an invisible pill would look like the focus
    // had vanished.
    function isSkipped(i) { return isDisabled(i) || isHidden(i) }

    readonly property color _accent:    Theme.accent
    // The container carries a trace of the accent, the same way the page background does, so
    // the control belongs to the palette instead of sitting on it as a grey box.
    readonly property color _bgPill:    Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                                    Theme.accent.b, 0.07))
    readonly property color _border:    Theme.line
    // Was a hardcoded near-black green. That was only ever legible because the accent was
    // green too: on an amber or lime accent it is dark green on yellow, and on a deep blue one
    // it is black on navy. Theme decides this from the accent's luminance, which is the whole
    // reason a user-chosen accent can be allowed at all.
    readonly property color _textOn:    Theme.onAccent
    readonly property color _textOff:   Theme.text2

    /*
     * The interface scale, read here rather than passed in: this control appears 40 times
     * across five files, and a `scaled:` property on the instance is a knob that would be
     * set in 39 places and forgotten in the fortieth.
     *
     * ⚠️ Until 5.3.0 every number below was a literal, so this was the one control that did
     * not grow with the window. On a 1920 handheld — uiScale 1.44 — a 52 px settings row was
     * drawn 75 px tall with its label at 22 px, and this selector stayed 36 px tall with 13 px
     * text inside it: 48% of the row's height where at 1280 it had been 72%.
     *
     * What stays literal: border.width. A hairline is a hairline at every scale, and a focus
     * ring drawn 5 px thick on a large screen is a box, not a ring — same rule AppsScreen and
     * the dialogs already follow, and `_px(1)` appears nowhere in this repo.
     */
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    readonly property int   _pillPadX:  _px(14)

    activeFocusOnTab: true

    // The focus half of HoverState's rule, written out here because the HoverStates in this
    // control sit one level down, on the pills, and this border belongs to the container.
    // Same predicate, so the two cannot drift: while the mouse is in hand the ring is off and
    // the pill under the pointer washes instead.
    readonly property bool _keyFocused: selector.activeFocus
                                        && SdlGamepadKeyNavigation.inputMode !== "pointer"

    implicitWidth: row.implicitWidth + selector._px(8)
    implicitHeight: selector._px(36)

    Rectangle {
        anchors.fill: parent
        radius: selector._px(8)
        color: selector._bgPill
        border.color: selector._keyFocused ? selector._accent : selector._border
        border.width: selector._keyFocused ? 3 : 1
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 0

        Repeater {
            model: selector.labels
            delegate: Item {
                id: pill
                width: pillLabel.implicitWidth + selector._pillPadX * 2
                height: selector._px(30)

                readonly property bool _selected: selector.currentIndex >= 0
                                                  && selector._visualIndex === index
                // Drawn selected, but by inheritance rather than by choice.
                readonly property bool _inherited: pill._selected && selector._inheritedSel
                // The value this row would go back to, while it is holding another one.
                readonly property bool _inheritMark: selector.inheritStrip
                                                     && selector.currentIndex !== 0
                                                     && index === selector.inheritIndex
                readonly property bool _disabled: selector.isDisabled(index)

                // A Row leaves out what is not visible, so the strip closes up on its own and
                // nothing has to know which index went missing.
                visible: !selector.isHidden(index)

                // ⚠️ Disabled used to be an opacity on the fill plus `enabled: false` on the
                // MouseArea alone, which left this Item enabled — so HoverState's guard would
                // have seen nothing wrong with a greyed pill. Say it on the Item.
                enabled: !pill._disabled && pill.visible

                HoverState { id: hov }

                // The selection. Accent when this profile chose the value, a raised neutral
                // when the value is merely the one being inherited — see inheritStrip.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: selector._px(2)
                    radius: selector._px(5)
                    color: !pill._selected  ? "transparent"
                         : pill._inherited  ? Theme.cardHigh
                         :                    selector._accent
                    border.color: Theme.lineHigh
                    border.width: pill._inherited ? 1 : 0
                    opacity: pill._disabled ? 0.4 : 1.0
                }
                // Where the value would go back to. Bottom-centre, so it never meets the
                // native marker in the opposite corner, and neutral: this is not a choice.
                Rectangle {
                    visible: pill._inheritMark
                    width: selector._px(4); height: width
                    radius: width / 2
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: selector._px(4)
                    color: Theme.text3
                    opacity: pill._disabled ? 0.4 : 1.0
                }
                // The native marker. Inside the pill's own rounded corner rather than
                // floating over the strip, so it moves and disappears with its pill.
                // On the selected pill it is drawn in the text colour: an accent dot on
                // an accent fill would be invisible.
                Rectangle {
                    visible: selector.isNative(index)
                    width: selector._px(4); height: width
                    radius: width / 2
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: selector._px(5)
                    anchors.rightMargin: selector._px(6)
                    color: (pill._selected && !pill._inherited) ? selector._textOn
                                                                : selector._accent
                    opacity: pill._disabled ? 0.4
                           : (pill._selected && !pill._inherited) ? 0.5 : 1.0
                }
                Label {
                    id: pillLabel
                    anchors.centerIn: parent
                    text: modelData
                    color: pill._disabled  ? Theme.offline
                         : pill._inherited ? Theme.text
                         : pill._selected  ? selector._textOn
                         :                   selector._textOff
                    font.family: Theme.family
                    font.pixelSize: selector._px(Theme.fontSmall)
                    font.bold: pill._selected
                }
                // The one control in the app whose hover is a wash rather than a border: the
                // border belongs to the container around these, and a border inside a border
                // two pixels away is noise. A pill is a filled shape, so it lightens.
                //
                // ⚠️ The SELECTED pill does not light up. Hovering the option you already have
                // chosen has nothing to say, and five percent of white over a full accent
                // moves red from 0 to 13 and leaves green and blue where they are — it would
                // have been a promise the pixels could not keep. The cursor still says it is
                // clickable.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: selector._px(2)
                    radius: selector._px(5)
                    color: Qt.rgba(1, 1, 1, 0.05)
                    opacity: (hov.active && !pill._selected) ? 1 : 0

                    Behavior on opacity {
                        enabled: !Theme.reduceAnimations
                        NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    // enabled / cursorShape / hoverEnabled are HoverState's job now.
                    onClicked: {
                        selector.forceActiveFocus()
                        selector._choose(index)
                    }
                }
            }
        }
    }

    // At a boundary we leave the event UNaccepted so KeyNavigation.left/right
    // (if set on the instance) can move focus to a neighbour — e.g. Right past
    // the last profile tab focuses the "+ Add" button.
    //
    // ⚠️ Both walk from _visualIndex, not from currentIndex: on an inherit strip those two
    // differ by exactly the case that matters — currentIndex 0 is drawn on the inheritIndex
    // pill, and stepping from 0 would jump the cursor to the front of the strip instead of
    // moving one pill from where the eye sees it. When nothing is selected (a custom value,
    // currentIndex -1) the inherited pill is the anchor: it is where the strip would land.
    function _cursorIndex() {
        if (selector._visualIndex >= 0) return selector._visualIndex
        return selector.inheritIndex >= 0 ? selector.inheritIndex : selector._firstIndex
    }

    /*
     * Nothing is drawn as selected: a custom value is in force (currentIndex -1) and the
     * inherited one is not on the strip either. Then the first press has nowhere to step
     * FROM, so it selects the anchor instead of stepping past it — otherwise ▶ would appear
     * to skip a pill and ◀ would walk the focus out of the control.
     */
    readonly property bool _nothingDrawn: selector._visualIndex < 0
    Keys.onLeftPressed: function(event) {
        if (selector._nothingDrawn) {
            selector._choose(selector._firstIndex)
            event.accepted = true
            return
        }
        var i = selector._cursorIndex() - 1
        while (i >= selector._firstIndex && selector.isSkipped(i)) i--
        if (i >= selector._firstIndex) {
            selector._choose(i)
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
    Keys.onRightPressed: function(event) {
        if (selector._nothingDrawn) {
            selector._choose(selector._firstIndex)
            event.accepted = true
            return
        }
        var i = selector._cursorIndex() + 1
        while (i < selector.labels.length && selector.isSkipped(i)) i++
        if (i < selector.labels.length) {
            selector._choose(i)
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
}
