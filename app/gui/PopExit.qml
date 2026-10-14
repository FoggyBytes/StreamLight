import QtQuick 2.15

import Theme 1.0

// How every popup leaves (7.0.0): faster than it came, shrinking only a little — leaving is
// not an event, it should get out of the way.
Transition {
    enabled: !Theme.reduceAnimations
    ParallelAnimation {
        NumberAnimation { property: "scale"; from: 1.0; to: 0.96; duration: 110; easing.type: Easing.InCubic }
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 110; easing.type: Easing.InCubic }
    }
}
