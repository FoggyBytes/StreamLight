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
     * ⚠️ Hidden, not removed from `labels`: callers keep parallel arrays of
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

    // One place decides what a pill press means: the mouse and ◀/▶ both come through here.
    function _choose(i) {
        if (selector.currentIndex === i && selector.currentIndex !== -1) return
        selector.currentIndex = i
        selector.activated(i)
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
        for (var k = 0; k < hiddenIndices.length; ++k)
            if (hiddenIndices[k] === i)
                return true
        return false
    }

    // What ◀/▶ must step over: a greyed option cannot be chosen, and one that is not on
    // screen must not be either — landing on an invisible pill would look like the focus
    // had vanished.
    function isSkipped(i) { return isDisabled(i) || isHidden(i) }

    readonly property color _accent:    Theme.accentFill
    /*
     * ⚠️ Glass since 05/11/2026 (Marcello, variant A of docs/mockups/pulsanti): the strip is a
     * wash of the reading colour under Theme.lineHigh, the hairline of the host card's buttons,
     * and the selection the same glass tinted by the accent (18%) under a 55% accent hairline,
     * words in Theme.text. Until then the strip was a solid card-coloured pill (the card tinted
     * 7% by the accent, 50% opaque) and the selection a solid accent with dark words.
     */
    readonly property color _bgPill:    Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.05)
    readonly property color _border:    Theme.lineHigh
    // (`_textOn`, the dark words on the solid selection, went with it on 05/11/2026: on glass
    // the selected words are Theme.text.)
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
        // Focused: the strip's border turns to the accent (2 px, like every focused button) and
        // the selection inside it deepens — see the slider.
        color: selector._keyFocused ? Qt.rgba(selector._accent.r, selector._accent.g, selector._accent.b, 0.06)
                                    : selector._bgPill
        border.color: selector._keyFocused ? selector._accent : selector._border
        border.width: selector._keyFocused ? 2 : 1
    }

    /*
     * The selection, ONE shape that slides from pill to pill (7.0.0) — the iOS segmented
     * control. It used to be a fill inside each pill, switched off in one and on in the other.
     * Glass tinted by the accent.
     * It does not animate the first time it is placed, only when the choice moves.
     */
    // pills.count is read so this re-evaluates once the Repeater has built its items.
    readonly property Item _selItem: (pills.count > 0 && selector.currentIndex >= 0)
                                     ? pills.itemAt(selector.currentIndex) : null
    property bool _slideReady: false
    Component.onCompleted: Qt.callLater(function() { selector._slideReady = true })

    Rectangle {
        id: slider
        visible: selector._selItem !== null && selector._selItem.visible
        x: selector._selItem ? row.x + selector._selItem.x + selector._px(2) : 0
        y: row.y + selector._px(2)
        width: selector._selItem ? selector._selItem.width - selector._px(4) : 0
        height: selector._px(30) - selector._px(4)
        radius: selector._px(5)
        // Glass tinted by the accent.
        color: Qt.rgba(selector._accent.r, selector._accent.g, selector._accent.b,
                         selector._keyFocused ? 0.26 : 0.18)
        border.color: Qt.rgba(selector._accent.r, selector._accent.g, selector._accent.b,
                              selector._keyFocused ? 1.0 : 0.55)
        border.width: 1
        opacity: (selector._selItem && selector.isDisabled(selector.currentIndex)) ? 0.4 : 1.0
        Behavior on x {
            enabled: selector._slideReady && !Theme.reduceAnimations
            NumberAnimation { duration: 190; easing.type: Easing.OutBack; easing.overshoot: 1.05 }
        }
        Behavior on width {
            enabled: selector._slideReady && !Theme.reduceAnimations
            NumberAnimation { duration: 190; easing.type: Easing.OutBack; easing.overshoot: 1.05 }
        }
        Behavior on color {
            enabled: selector._slideReady && !Theme.reduceAnimations
            ColorAnimation { duration: 160 }
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 0

        Repeater {
            id: pills
            model: selector.labels
            delegate: Item {
                id: pill
                width: pillLabel.implicitWidth + selector._pillPadX * 2
                height: selector._px(30)

                readonly property bool _selected: selector.currentIndex >= 0
                                                  && selector.currentIndex === index
                readonly property bool _disabled: selector.isDisabled(index)

                // A Row leaves out what is not visible, so the strip closes up on its own and
                // nothing has to know which index went missing.
                visible: !selector.isHidden(index)

                // ⚠️ Disabled used to be an opacity on the fill plus `enabled: false` on the
                // MouseArea alone, which left this Item enabled — so HoverState's guard would
                // have seen nothing wrong with a greyed pill. Say it on the Item.
                enabled: !pill._disabled && pill.visible

                HoverState { id: hov }

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
                    // The selection is glass now (05/11/2026), so the accent dot shows on it.
                    color: Theme.ink
                    opacity: pill._disabled ? 0.4 : 1.0
                }
                Label {
                    id: pillLabel
                    anchors.centerIn: parent
                    text: modelData
                    color: pill._disabled  ? Theme.offline
                         : pill._selected  ? Theme.text
                         :                   selector._textOff
                    font.family: Theme.family
                    font.pixelSize: selector._px(Theme.fontSmall * Theme.textScale)
                    font.bold: pill._selected
                }
                // The one control in the app whose hover is a wash rather than a border: the
                // border belongs to the container around these, and a border inside a border
                // two pixels away is noise. A pill is a filled shape, so it lightens.
                //
                // ⚠️ The SELECTED pill does not light up. Hovering the option you already have
                // chosen has nothing to say, and the selection is already a tinted glass the wash
                // would only muddy. The cursor still says it is clickable.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: selector._px(2)
                    radius: selector._px(5)
                    color: Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.08)
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
    function _cursorIndex() { return selector.currentIndex >= 0 ? selector.currentIndex : 0 }

    /*
     * Nothing is drawn as selected: a custom value is in force (currentIndex -1). Then the
     * first press has nowhere to step FROM, so it selects the first pill instead — otherwise
     * ▶ would appear to skip a pill and ◀ would walk the focus out of the control.
     */
    readonly property bool _nothingDrawn: selector.currentIndex < 0
    Keys.onLeftPressed: function(event) {
        if (selector._nothingDrawn) {
            selector._choose(0)
            event.accepted = true
            return
        }
        var i = selector._cursorIndex() - 1
        while (i >= 0 && selector.isSkipped(i)) i--
        if (i >= 0) {
            selector._choose(i)
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
    Keys.onRightPressed: function(event) {
        if (selector._nothingDrawn) {
            selector._choose(0)
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
