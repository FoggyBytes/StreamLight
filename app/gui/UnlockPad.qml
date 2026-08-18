// 2.15, not the 2.0 the neighbouring files declare: activeFocusOnTab on a plain Item arrived
// in 2.1, and under 2.0 the whole type fails to load rather than just that line.
import QtQuick 2.15
import QtQuick.Controls 2.5

import SdlGamepadKeyNavigation 1.0
import StreamingPreferences 1.0

/*
 * The PIN pad shown while a hidden session sits on the host's logon screen.
 *
 * Digits are buffered in Session (a byte buffer that gets wiped), never here: QML is told
 * the count and nothing else, so the PIN is not sitting in a QString we can neither pin
 * down nor overwrite. This shows dots for that count and emits what the user asked for.
 *
 * Text is kept to the label of the thing being acted on. What each button does is written
 * on the button, so no line explains it.
 */
Item {
    id: root

    // Digits entered so far, pushed in by the owner from Session::unlockPinLength().
    property int  pinLength : 0
    property int  attempt    : 0
    property int  maxAttempts: 3
    property string hostName : ""

    // Clock and date, laid out like the screen this stands in for, because that is the one
    // thing that says at a glance "this is Windows asking, not StreamLight". It is this
    // device's time, not the host's — the two agree in every case worth drawing.
    property var _now : new Date()
    Timer {
        interval: 1000
        running: root.visible
        repeat: true
        onTriggered: root._now = new Date()
    }

    // "entry" | "checking" | "wrong" | "blocked"
    property string state_ : "entry"

    readonly property bool _busy : state_ === "checking" || state_ === "blocked"

    signal digitPressed(int digit)
    signal backspacePressed()
    signal submitPressed()
    signal cancelPressed()

    focus: true

    // Sized off the window height, like the launch curtain, so one layout holds from a
    // handheld to a TV.
    readonly property real _h : height > 0 ? height : 1080
    // ⚠️ 1330, the same divisor as AppsScreen, HostStage and Theme.uiScale. This was left on
    // 1450 when that reference changed, so the pad came out about 9% smaller than the page it
    // opens from — scaling, but to a scale of its own.
    readonly property real _u : Math.max(0.62, Math.min(1.6, width / 1330))

    function _press(index) {
        if (_busy) return
        if (index === 9)  { backspacePressed(); return }
        if (index === 10) { digitPressed(0);    return }
        if (index === 11) { submitPressed();    return }
        digitPressed(index + 1)
    }

    Keys.onEscapePressed: cancelPressed()
    Keys.onReturnPressed: if (!_busy) submitPressed()
    Keys.onEnterPressed:  if (!_busy) submitPressed()
    Keys.onBackPressed:   if (!_busy) backspacePressed()
    Keys.onPressed: function (event) {
        if (_busy) return
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            digitPressed(event.key - Qt.Key_0)
            event.accepted = true
        }
        else if (event.key === Qt.Key_Backspace) {
            backspacePressed()
            event.accepted = true
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: root._h * 0.022

        // Time and date first, in the proportions Windows uses: this is the host's logon
        // screen, and saying so with its own shape works better than a sentence explaining it.
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            // Follows the same setting as the Home clock: how you read a clock is one preference,
            // not one per screen. The date below does not — this pad is imitating the Windows
            // logon screen, which writes it out in full, and that is what makes it look like
            // Windows asking rather than us.
            text: Qt.formatTime(root._now,
                                StreamingPreferences.clockFormat === StreamingPreferences.CF_12H
                                ? "h:mm AP" : "HH:mm")
            font.pointSize: Math.max(34, Math.round(root._h * 0.075))
            font.weight: Font.Light
            color: "#f2f2f4"
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            // Explicit locale: Qt.formatDate would use the system's, and this app is
            // English-only by design — an Italian weekday next to "Sign in" reads as a bug.
            text: root._now.toLocaleDateString(Qt.locale("en_GB"), "dddd d MMMM")
            font.pointSize: Math.max(11, Math.round(root._h * 0.020))
            color: "#b9c3c8"
            bottomPadding: root._h * 0.020
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.hostName
            font.pointSize: Math.max(14, Math.round(root._h * 0.026))
            font.bold: true
            color: "#f2f2f4"
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Sign in")
            font.pointSize: Math.max(10, Math.round(root._h * 0.017))
            color: "#8fa3aa"
        }

        // The dots grow with what has been typed. No empty placeholders: the client does
        // not know how long the PIN is, and drawing four boxes would claim it does.
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root._h * 0.012
            height: root._h * 0.020

            Repeater {
                model: root.pinLength
                Rectangle {
                    width: root._h * 0.014
                    height: width
                    radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.state_ === "wrong" ? "#f87171" : "#00d3f2"
                }
            }
        }

        Grid {
            id: pad
            anchors.horizontalCenter: parent.horizontalCenter
            columns: 3
            spacing: root._h * 0.011
            opacity: root._busy ? 0.35 : 1.0
            Behavior on opacity { NumberAnimation { duration: 140 } }

            Repeater {
                id: keys
                model: 12
                Rectangle {
                    id: key

                    property bool isAction : index === 9 || index === 11
                    property string glyph  : index === 9  ? "⌫"
                                           : index === 10 ? "0"
                                           : index === 11 ? "✓"
                                           : String(index + 1)

                    width:  root._h * 0.088
                    height: root._h * 0.062
                    radius: root._h * 0.010
                    color: activeFocus ? "#0d2f37" : "#0c1216"
                    border.width: activeFocus ? 2 : 1
                    border.color: activeFocus ? "#00d3f2" : "#223238"
                    scale: activeFocus ? 1.06 : 1.0
                    Behavior on scale { NumberAnimation { duration: 110 } }

                    activeFocusOnTab: true
                    focus: index === 0

                    Label {
                        anchors.centerIn: parent
                        text: key.glyph
                        font.pointSize: Math.max(12, Math.round(root._h * 0.024))
                        color: key.activeFocus ? "#8eecff"
                             : key.isAction    ? "#8fa3aa" : "#cbdde3"
                    }

                    // keys.itemAt(), not pad.children[]: the Repeater is itself a child of the
                    // Grid, so the children list is offset from the delegate index.
                    Keys.onLeftPressed:  if (index % 3 > 0) keys.itemAt(index - 1).forceActiveFocus()
                    Keys.onRightPressed: if (index % 3 < 2 && index + 1 < 12) keys.itemAt(index + 1).forceActiveFocus()
                    Keys.onUpPressed:    if (index >= 3) keys.itemAt(index - 3).forceActiveFocus()
                    Keys.onDownPressed:  if (index + 3 < 12) keys.itemAt(index + 3).forceActiveFocus()

                    Keys.onReturnPressed: root._press(index)
                    Keys.onEnterPressed:  root._press(index)
                    Keys.onSpacePressed:  root._press(index)

                    MouseArea {
                        anchors.fill: parent
                        onClicked: { key.forceActiveFocus(); root._press(index) }
                    }
                }
            }
        }

        // One line, and only when there is something to say.
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: text !== ""
            text: root.state_ === "checking" ? qsTr("Checking…")
                : root.state_ === "wrong"    ? qsTr("Wrong PIN · %1 of %2").arg(root.attempt).arg(root.maxAttempts)
                : root.state_ === "blocked"  ? qsTr("Too many attempts")
                : ""
            font.pointSize: Math.max(10, Math.round(root._h * 0.017))
            color: root.state_ === "checking" ? "#8f8f9c" : "#f5a623"
        }
    }

    // Same footer grammar as the launch curtain: the glyph the user's controller actually
    // shows, and the word for what it does.
    Row {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root._h * 0.035
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root._h * 0.010
        visible: SdlGamepadKeyNavigation.getConnectedGamepads() > 0

        PadGlyph {
            anchors.verticalCenter: parent.verticalCenter
            buttonKey: "B"
            label: "B"
            size: Math.max(22, Math.round(root._h * 0.028))
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Cancel")
            font.pointSize: Math.max(10, Math.round(root._h * 0.016))
            color: "#8f8f9c"
        }
    }

    onVisibleChanged: {
        if (!visible) return
        _now = new Date()          // the pad was built seconds ago; don't open on a stale clock
        var first = keys.itemAt(0)
        if (first) first.forceActiveFocus()
    }
}
