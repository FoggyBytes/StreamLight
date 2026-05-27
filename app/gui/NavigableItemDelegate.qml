import QtQuick 2.0
import QtQuick.Controls 2.2

import SdlGamepadKeyNavigation 1.0

// Navigation is handled by the parent GridView (Qt Quick's default arrow-key
// behavior or, in StreamLight 3.0, AppsScreen's sub-focus handler). The
// delegate itself must NOT consume Left/Right/Up/Down or sub-focus would be
// bypassed and the cursor would always jump cell-to-cell.
ItemDelegate {
    property GridView grid

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
