import QtQuick 2.15

import Theme 1.0

/*
 * The content of a tab strip slides in the direction of the tab (7.0.0): LB moves it in from
 * the left, RB from the right, 40 px and a fade. Give the content `transform: slide.shift`
 * and `opacity: slide.opacity`, and call `slide.to(newIndex)` whenever the tab changes.
 */
QtObject {
    id: slide

    property real distance: 40 * Theme.uiScale
    property int  _last: 0
    property real _p: 1            // 0 → 1 over the slide
    property real _dir: 1

    readonly property real opacity: _p
    property Translate shift: Translate { x: (1 - slide._p) * slide._dir * slide.distance }

    function to(index) {
        var dir = index > _last ? 1 : -1
        // Wrapping round the ends (About → Video on RB) still reads as "next".
        if (Math.abs(index - _last) > 1) dir = -dir
        _last = index
        if (Theme.reduceAnimations) { _p = 1; return }
        _dir = dir
        _anim.restart()
    }

    property NumberAnimation _anim: NumberAnimation {
        target: slide; property: "_p"
        from: 0; to: 1
        duration: 180
        easing.type: Easing.OutCubic
    }
}
