import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import QtQuick.Controls.Material 2.2

import ComputerManager 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

ApplicationWindow {
    property bool pollingActive: false

    // Debounce after a stream session pops: drops stale gamepad/keyboard
    // events that survive the StreamSegue and would otherwise re-trigger an
    // immediate resume on the focused running app.
    property bool _streamJustEnded: false
    Timer {
        id: _streamEndDebounceTimer
        interval: 3000
        onTriggered: window._streamJustEnded = false
    }
    function markStreamJustEnded() {
        window._streamJustEnded = true
        window._streamLaunching = false
        _streamEndDebounceTimer.restart()
    }

    // Guard against a queued second launch when the user double-taps A:
    // Session::start() in C++ serialises via a semaphore, so the second
    // create+push would auto-fire when the first session ends, looking
    // exactly like an unwanted auto-resume.
    property bool _streamLaunching: false
    Timer {
        id: _streamLaunchTimer
        interval: 5000
        onTriggered: window._streamLaunching = false
    }
    function markStreamLaunching() {
        window._streamLaunching = true
        _streamLaunchTimer.restart()
    }

    id: window
    width: 1280
    height: 720
    minimumWidth: 1280
    minimumHeight: 720
    title: "StreamLight"
    font.family: "DM Sans"

    // ── Embedded UI fonts (matches StreamTweak) ───────────────────────────────
    FontLoader { source: "qrc:/res/fonts/DMSans-Regular.ttf" }
    FontLoader { source: "qrc:/res/fonts/DMSans-Medium.ttf" }
    FontLoader { source: "qrc:/res/fonts/DMSans-SemiBold.ttf" }
    FontLoader { source: "qrc:/res/fonts/JetBrainsMono-Regular.ttf" }
    FontLoader { source: "qrc:/res/fonts/JetBrainsMono-Medium.ttf" }

    // ── Design system palette ─────────────────────────────────────────────────
    readonly property string appDisplayVersion: "3.2.0"

    readonly property color clrBg:      "#0d0d0d"
    readonly property color clrBg1:     "#151515"
    readonly property color clrBg2:     "#1a1a1a"
    readonly property color clrBg3:     "#212121"
    readonly property color clrBgHov:   "#262626"
    readonly property color clrBgPrs:   "#2c2c2c"
    readonly property color clrBorder:  "#2a2a2a"
    readonly property color clrBorderL: "#3a3a3a"
    readonly property color clrBorderS: "#404040"
    readonly property color clrText:    "#f0f0f0"
    readonly property color clrTextDim: "#a0a0a0"
    readonly property color clrTextMut: "#707070"
    readonly property color clrTextDis: "#555555"
    readonly property color clrGreen:   "#00E676"
    readonly property color clrGreenH:  "#00C853"
    readonly property color clrGreenP:  "#00A040"
    readonly property color clrGreenLk: "#00E676"
    readonly property color clrRed:     "#C42B1C"
    readonly property color clrBlue:    "#3a96dd"
    readonly property string monoFont:  "JetBrains Mono"
    // ─────────────────────────────────────────────────────────────────────────

    // This function runs prior to creation of the initial StackView item
    function doEarlyInit() {
        // Force dark background on all Qt versions for the new design
        Material.background = "#151515"

        Material.theme = Material.Dark
        Material.accent = "#00E676"
        Material.primary = "#00E676"

        SdlGamepadKeyNavigation.enable()
    }

    Component.onCompleted: {
        // Honor the GUI mode preference (default on first launch: maximised).
        if (SystemProperties.hasDesktopEnvironment) {
            if (StreamingPreferences.uiDisplayMode === StreamingPreferences.UI_MAXIMIZED) {
                window.showMaximized()
            } else if (StreamingPreferences.uiDisplayMode === StreamingPreferences.UI_FULLSCREEN) {
                window.showFullScreen()
            } else {
                window.show()
            }
        } else {
            window.showFullScreen()
        }

        // Display any modal dialogs for configuration warnings
        if (runConfigChecks) {
            if (SystemProperties.isWow64) {
                wow64Dialog.open()
            }

            // Hardware acceleration and unmapped gamepads are checked asynchronously
            SystemProperties.hasHardwareAccelerationChanged.connect(hasHardwareAccelerationChanged)
            SystemProperties.unmappedGamepadsChanged.connect(hasUnmappedGamepadsChanged)
            SystemProperties.startAsyncLoad()
        }

        // Drive the activeFocus chain all the way down to the gamepad-driven
        // grid AFTER the window is shown and the StackView has its initial
        // item. Using Qt.callLater avoids a race with Loader instantiation.
        Qt.callLater(function() {
            stackView.forceActiveFocus()
            var top = stackView.currentItem
            if (top && top.forceActiveFocus) top.forceActiveFocus()
        })
    }

    function hasHardwareAccelerationChanged() {
        if (!SystemProperties.hasHardwareAcceleration && StreamingPreferences.videoDecoderSelection !== StreamingPreferences.VDS_FORCE_SOFTWARE) {
            if (SystemProperties.isRunningXWayland) {
                xWaylandDialog.open()
            }
            else {
                noHwDecoderDialog.open()
            }
        }
    }

    function hasUnmappedGamepadsChanged() {
        if (SystemProperties.unmappedGamepads) {
            unmappedGamepadDialog.unmappedGamepads = SystemProperties.unmappedGamepads
            unmappedGamepadDialog.open()
        }
    }

    // It would be better to use TextMetrics here, but it always lays out
    // the text slightly more compactly than real Text does in ToolTip,
    // causing unexpected line breaks to be inserted
    Text {
        id: tooltipTextLayoutHelper
        visible: false
        font: ToolTip.toolTip.font
        text: ToolTip.toolTip.text
    }

    // This configures the maximum width of the singleton attached QML ToolTip. If left unconstrained,
    // it will never insert a line break and just extend on forever.
    ToolTip.toolTip.contentWidth: Math.min(tooltipTextLayoutHelper.width, 400)

    function goBack() {
        stackView.pop()
    }

    StackView {
        id: stackView
        anchors.fill: parent
        focus: true

        Component.onCompleted: {
            // Perform our early initialization before constructing the
            // initial view and pushing it to the StackView.
            doEarlyInit()

            // AppShell is always the base; CLI modes layer their segue on top.
            push("qrc:/gui/AppShell.qml")
            if (initialView && initialView.length > 0) {
                push(initialView)
            }
        }

        onCurrentItemChanged: {
            // Ensure focus travels to the next view when going back
            if (currentItem) {
                currentItem.forceActiveFocus()
            }
        }

        Keys.onEscapePressed: {
            if (depth > 1) {
                goBack()
            }
            else {
                quitConfirmationDialog.open()
            }
        }

        Keys.onBackPressed: {
            if (depth > 1) {
                goBack()
            }
            else {
                quitConfirmationDialog.open()
            }
        }

        Keys.onMenuPressed: {
            var item = stackView.currentItem
            if (item && item.openSettings) item.openSettings()
        }

        // This is a keypress we've reserved for letting the
        // SdlGamepadKeyNavigation object tell us to show settings
        // when Menu is consumed by a focused control.
        Keys.onHangupPressed: {
            var item = stackView.currentItem
            if (item && item.openSettings) item.openSettings()
        }
    }

    // This timer keeps us polling for 5 minutes of inactivity
    // to allow the user to work with Moonlight on a second display
    // while dealing with configuration issues. This will ensure
    // machines come online even if the input focus isn't on Moonlight.
    Timer {
        id: inactivityTimer
        interval: 5 * 60000
        onTriggered: {
            if (!active && pollingActive) {
                ComputerManager.stopPollingAsync()
                pollingActive = false
            }
        }
    }

    onVisibleChanged: {
        // When we become invisible while streaming is going on,
        // stop polling immediately.
        if (!visible) {
            inactivityTimer.stop()

            if (pollingActive) {
                ComputerManager.stopPollingAsync()
                pollingActive = false
            }
        }
        else if (active) {
            // When we become visible and active again, start polling
            inactivityTimer.stop()

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling()
                pollingActive = true
            }
        }

        // Poll for gamepad input only when the window is in focus
        SdlGamepadKeyNavigation.notifyWindowFocus(visible && active)
    }

    onActiveChanged: {
        if (active) {
            // Stop the inactivity timer
            inactivityTimer.stop()

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling()
                pollingActive = true
            }
        }
        else {
            // Start the inactivity timer to stop polling
            // if focus does not return within a few minutes.
            inactivityTimer.restart()
        }

        // Poll for gamepad input only when the window is in focus
        SdlGamepadKeyNavigation.notifyWindowFocus(visible && active)
    }

    function navigateTo(url, objectType)
    {
        var existingItem = stackView.find(function(item, index) {
            return item instanceof objectType
        })

        if (existingItem !== null) {
            // Pop to the existing item
            stackView.pop(existingItem)
        }
        else {
            // Create a new item
            stackView.push(url)
        }
    }

    // Keyboard shortcuts preserved from the old toolbar
    Shortcut {
        sequence: StandardKey.Preferences
        onActivated: {
            var item = stackView.currentItem
            if (item && item.openSettings) item.openSettings()
        }
    }

    Shortcut {
        sequence: StandardKey.New
        onActivated: {
            var item = stackView.currentItem
            if (item && item.openAddPc) item.openAddPc()
        }
    }

    Shortcut {
        sequence: StandardKey.HelpContents
        enabled: SystemProperties.hasBrowser
        onActivated: Qt.openUrlExternally("https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide")
    }

    ErrorMessageDialog {
        id: noHwDecoderDialog
        headerText: qsTr("HARDWARE ACCELERATION")
        text: qsTr("No functioning hardware accelerated video decoder was detected by StreamLight." +
                   "Your streaming performance may be severely degraded in this configuration.")
        helpText: qsTr("Click the Help button for more information on solving this problem.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    ErrorMessageDialog {
        id: xWaylandDialog
        headerText: qsTr("DISPLAY SERVER")
        text: qsTr("Hardware acceleration doesn't work on XWayland. Continuing on XWayland may result in poor streaming performance. " +
                   "Try running with QT_QPA_PLATFORM=wayland or switch to X11.")
        helpText: qsTr("Click the Help button for more information.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    NavigableMessageDialog {
        id: wow64Dialog
        headerText: qsTr("WRONG ARCHITECTURE")
        standardButtons: Dialog.Ok | Dialog.Cancel
        text: qsTr("This version of StreamLight isn't optimized for your PC. Please download the '%1' version of StreamLight for the best streaming performance.").arg(SystemProperties.friendlyNativeArchName)
        onAccepted: {
            Qt.openUrlExternally("https://github.com/moonlight-stream/moonlight-qt/releases");
        }
    }

    ErrorMessageDialog {
        id: unmappedGamepadDialog
        headerText: qsTr("UNMAPPED GAMEPAD")
        property string unmappedGamepads : ""
        text: qsTr("StreamLight detected gamepads without a mapping:") + "\n" + unmappedGamepads
        helpTextSeparator: "\n\n"
        helpText: qsTr("Click the Help button for information on how to map your gamepads.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Gamepad-Mapping"
    }

    // This dialog appears when quitting via keyboard or gamepad button
    NavigableMessageDialog {
        id: quitConfirmationDialog
        headerText: qsTr("QUIT STREAMLIGHT")
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Are you sure you want to quit?")
        // For keyboard/gamepad navigation
        onAccepted: Qt.quit()
    }

    // HACK: This belongs in StreamSegue but keeping a dialog around after the parent
    // dies can trigger bugs in Qt 5.12 that cause the app to crash. For now, we will
    // host this dialog in a QML component that is never destroyed.
    //
    // To repro: Start a stream, cut the network connection to trigger the "Connection
    // terminated" dialog, wait until the app grid times out back to the PC grid, then
    // try to dismiss the dialog.
    ErrorMessageDialog {
        id: streamSegueErrorDialog
        headerText: qsTr("STREAM ERROR")

        property bool quitAfter: false

        onClosed: {
            if (quitAfter) {
                Qt.quit()
            }

            // StreamSegue assumes its dialog will be re-created each time we
            // start streaming, so fake it by wiping out the text each time.
            text = ""
        }
    }

}
