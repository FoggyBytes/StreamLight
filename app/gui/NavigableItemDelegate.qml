import QtQuick 2.0
import QtQuick.Controls 2.2

import SdlGamepadKeyNavigation 1.0

// Navigation is handled by the parent view (Qt Quick's own arrow-key behaviour).
// The delegate itself must NOT consume Left/Right/Up/Down, or that navigation
// would be bypassed.
//
// Both library layouts use this delegate. ListView and GridView are siblings,
// so accept either view rather than restricting this property to one layout.
ItemDelegate {
    property var grid

    readonly property bool keyboardFocused: grid.activeFocus && grid.currentItem === this
    readonly property bool pointerFocused: hovered

    // Visivamente "in focus": il highlight viene mostrato solo se l'ultimo
    // input è gamepad/tastiera, l'hover solo se l'ultimo input è il mouse.
    readonly property bool inputFocused:
        SdlGamepadKeyNavigation.inputMode === "key" ? keyboardFocused : pointerFocused

    highlighted: keyboardFocused

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed:  clicked()
}
