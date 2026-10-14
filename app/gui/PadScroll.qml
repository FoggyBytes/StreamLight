// Unversioned: FrameAnimation is Qt 6.4, and a versioned "2.15" import cannot see it.
import QtQuick
import QtQuick.Window

import Theme 1.0
import SdlGamepadKeyNavigation 1.0

/*
 * The right stick scrolls this view (7.0.0).
 *
 * Put one inside a page or dialog and point `flick` at its Flickable (or ListView). It moves
 * only when the focused element is inside that view — so with a dialog open over a page, the
 * dialog scrolls and the page does not — and only if there is something to scroll.
 *
 * ⚠️ The stick arrives as a SPEED (SdlGamepadKeyNavigation.scrollVelocity, px/s), and the view
 * is moved by it on every frame here. It used to arrive as a step per 20 Hz poll, written
 * straight into contentY: up to 125 px at a time, twenty times a second, which read as a
 * judder whether the stick was nudged or held. The speed is also eased in and out over a few
 * frames, so a push starts and stops like a scroll and not like a switch.
 *
 * The focus never moves while scrolling: that is the point, it lets a pad read what no
 * control reaches (captions, the changelog in About). The first D-pad or left-stick step
 * afterwards brings the focused element back into view and goes no further; the next one
 * navigates as usual. The mouse wheel needs none of this — Flickable scrolls with it already.
 */
Item {
    id: padScroll

    property Flickable flick: null
    // Room kept above and below the focused element when it is brought back.
    property real margin: 24

    visible: false

    // The speed actually applied, chasing the stick's. Eased, except with Reduce animations.
    property real _v: 0

    // The view has room to scroll and holds the focus: the stick would move it now. Told to
    // SdlGamepadKeyNavigation, which the status bar asks before drawing its "Scroll" prompt.
    readonly property bool available: {
        if (!flick || !flick.visible || flick.contentHeight - flick.height < 2) return false
        for (var p = padScroll.Window.activeFocusItem; p; p = p.parent)
            if (p === flick) return true
        return false
    }
    onAvailableChanged: SdlGamepadKeyNavigation.setScrollAvailable(padScroll, available)
    Component.onDestruction: SdlGamepadKeyNavigation.setScrollAvailable(padScroll, false)

    function _focusInside() {
        var w = padScroll.Window.window
        var f = w ? w.activeFocusItem : null
        for (var p = f; p; p = p.parent)
            if (p === flick) return f
        return null
    }

    // Moves the view by dy px; false when there is nothing to move (no focus here, an end).
    function _scrollBy(dy) {
        if (!flick || !flick.visible || !padScroll._focusInside()) return false
        var top = flick.originY
        var bottom = flick.originY + Math.max(0, flick.contentHeight - flick.height)
        if (bottom <= top) return false
        var y = Math.max(top, Math.min(bottom, flick.contentY + dy))
        if (Math.abs(y - flick.contentY) < 0.01) return false
        refocusAnim.stop()
        flick.contentY = y
        SdlGamepadKeyNavigation.noteScrolled()
        return true
    }

    FrameAnimation {
        running: padScroll.flick !== null
                 && (SdlGamepadKeyNavigation.scrollVelocity !== 0 || padScroll._v !== 0)
        onTriggered: {
            var target = SdlGamepadKeyNavigation.scrollVelocity
            // Clamped: the first frame after a pause reports the whole time it was away.
            var dt = Math.min(frameTime, 0.05)
            if (Theme.reduceAnimations) {
                padScroll._v = target
            } else {
                // ~70 ms to reach the stick's speed, and as long to come to rest.
                padScroll._v += (target - padScroll._v) * (1 - Math.exp(-dt / 0.07))
                if (target === 0 && Math.abs(padScroll._v) < 8) padScroll._v = 0
            }
            if (padScroll._v !== 0 && !padScroll._scrollBy(padScroll._v * dt) && target === 0)
                padScroll._v = 0
        }
    }

    Connections {
        target: SdlGamepadKeyNavigation

        function onRefocusRequested() {
            var f = padScroll._focusInside()
            if (!f) return
            padScroll._v = 0
            var r = f.mapToItem(flick.contentItem, 0, 0, f.width, f.height)
            var top = flick.originY
            var bottom = flick.originY + Math.max(0, flick.contentHeight - flick.height)
            var y = flick.contentY
            if (r.y - padScroll.margin < y) y = r.y - padScroll.margin
            else if (r.y + r.height + padScroll.margin > y + flick.height) y = r.y + r.height + padScroll.margin - flick.height
            y = Math.max(top, Math.min(bottom, y))
            if (Theme.reduceAnimations) { flick.contentY = y; return }
            refocusAnim.to = y
            refocusAnim.restart()
        }
    }

    NumberAnimation {
        id: refocusAnim
        target: padScroll.flick
        property: "contentY"
        duration: 260
        easing.type: Easing.OutCubic
    }
}
