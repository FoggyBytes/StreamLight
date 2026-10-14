import QtQuick 2.15

import Theme 1.0

// How every popup arrives (7.0.0): it grows from 90% with a small overshoot while it fades in,
// and the overshoot settles in a third of a second. Replaces Material's own grow-and-fade,
// which had no spring and ignored Reduce animations — with it on, the popup simply appears.
Transition {
    // Where it grows from. Smaller for a popup that grows out of the button that opened it
    // (set with the popup's transformOrigin), so the eye follows it from there.
    property real fromScale: 0.9
    enabled: !Theme.reduceAnimations
    ParallelAnimation {
        NumberAnimation { property: "scale"; from: fromScale; to: 1.0; duration: 210; easing.type: Easing.OutBack; easing.overshoot: 1.2 }
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 120; easing.type: Easing.OutCubic }
    }
}
