import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import Theme 1.0
import WindowChrome 1.0

/*
 * Minimize, maximize and close at the top right, on the app's own floor (8.3.0, #48).
 *
 * There is no title bar under them: WindowChrome gives Windows' caption to the client area and
 * tells Windows where these three are, so dragging, the window menu and Windows 11's snap
 * layouts still work. The mouse over them is non-client and QML never sees it — hover and press
 * come from WindowChrome. Transparent until hovered, as in WinUI apps; close turns red.
 *
 * In the overlay layer, above the pages and the popups' dim, the way Windows' own buttons sit
 * above anything an app draws. Hidden in full screen (inset 0).
 */
Item {
    id: root

    // Moved there once it is in the window: as a binding, Overlay.overlay is null while the item
    // has no window yet, and with a null parent it never gets one.
    Component.onCompleted: if (Overlay.overlay) parent = Overlay.overlay
    z: 10000
    anchors.top: parent ? parent.top : undefined
    anchors.right: parent ? parent.right : undefined
    width: WindowChrome.buttonWidth * 3
    height: WindowChrome.inset
    visible: WindowChrome.inset > 0

    // Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets — same code points — on Windows 10.
    readonly property string _iconFont: Qt.fontFamilies().indexOf("Segoe Fluent Icons") >= 0
                                        ? "Segoe Fluent Icons" : "Segoe MDL2 Assets"
    readonly property bool _maximized: Window.window && Window.window.visibility === Window.Maximized

    Row {
        anchors.fill: parent

        Repeater {
            // In the order they stand; the numbers are WindowChrome's.
            model: [
                { button: WindowChrome.Minimize, glyph: "\uE921" },   // ChromeMinimize
                { button: WindowChrome.Maximize, glyph: "" },
                { button: WindowChrome.Close,    glyph: "\uE8BB" }    // ChromeClose
            ]

            Rectangle {
                readonly property bool _close: modelData.button === WindowChrome.Close
                readonly property bool _hot: WindowChrome.hovered === modelData.button
                readonly property bool _down: _hot && WindowChrome.pressed === modelData.button

                width: WindowChrome.buttonWidth
                height: root.height
                color: _close && _hot ? (_down ? Qt.rgba(0.77, 0.17, 0.11, 0.9) : "#C42B1C")
                     : _down          ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                     : _hot           ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.09)
                     :                  "transparent"

                Text {
                    anchors.centerIn: parent
                    // ChromeRestore while maximized, ChromeMaximize otherwise.
                    text: modelData.button === WindowChrome.Maximize
                          ? (root._maximized ? "\uE923" : "\uE922")
                          : modelData.glyph
                    font.family: root._iconFont
                    // 10 DIP, as Windows draws them: the same ratio to the button as 10 to 46.
                    font.pixelSize: Math.round(WindowChrome.buttonWidth * 10 / 46)
                    color: parent._close && parent._hot ? "white" : Theme.text
                    // Quieter while the window is in the background, as Windows' own are.
                    opacity: parent._down ? 0.7
                           : (Window.active || parent._hot) ? 1
                           : 0.45
                }
            }
        }
    }
}
