import QtQuick 2.0
import QtQuick.Controls 2.2

import SdlGamepadKeyNavigation 1.0

// Navigation is handled by the parent view (Qt Quick's own arrow-key behaviour).
// The delegate itself must NOT consume Left/Right/Up/Down, or that navigation
// would be bypassed.
//
// ⚠️ The property is a ListView, not a GridView, and the name is historical: the
// library became a vertical list of titles in 5.0.0 and a ListView cannot be
// assigned to a GridView-typed property — they are siblings under Flickable, not
// relatives. Left as GridView it fails the type check at delegate creation, which
// takes the whole row down with it.
ItemDelegate {
    property ListView grid

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
