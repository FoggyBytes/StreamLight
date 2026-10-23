import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// The Library tab's Sync and restart (8.0.0): ask, then follow the host through its steps, then
// say how it went. Built as UpdateDialog is — the same Popup, scrim, eyebrow, centred lines,
// indeterminate bar, SuccessTick and DialogButtons — because it is the same kind of job: the host
// works, the client watches. ⚠️ A popup here is always built on the app's own pieces, never drawn
// by hand (21/10/2026: a hand-made one was judged out of place with the rest of the UI).
//
// The state comes in live from LibraryTab, which polls the host; this is only the view. "Hide"
// leaves the job running on the host; there is no cancel once it has started — a server stopped
// half-way is worse than one restarted.
Popup {
    id: pop

    enter: PopEnter {}
    exit: PopExit {}

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    // ── Live state (bound from LibraryTab) ────────────────────────────────────
    property string hostName: ""
    property string server: ""
    property string mode: "ask"       // ask · blocked · progress · done · failed
    property string reason: ""        // blocked: why it cannot start
    property bool   steamWarning: false
    property string stepText: ""      // progress: the step under way
    property int    stepNumber: 0     // progress: 1-5
    property int    elapsedS: 0
    property string resultText: ""    // done / failed

    signal confirmed()

    readonly property bool _terminal: mode === "done" || mode === "failed" || mode === "blocked"

    function _focusForMode() {
        if (mode === "ask")           confirmBtn.forceActiveFocus()
        else if (mode === "progress") hideBtn.forceActiveFocus()
        else                          closeBtn.forceActiveFocus()
    }
    onModeChanged: Qt.callLater(_focusForMode)

    modal: true
    Overlay.modal: Rectangle { color: Theme.scrim }
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    padding: pop._px(28)
    implicitWidth: Math.min(pop._px(640), (Overlay.overlay ? Overlay.overlay.width : pop._px(640)) - pop._px(96))

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: pop._px(12)
    }

    contentItem: ColumnLayout {
        spacing: pop._px(18)

        // Eyebrow
        Label {
            text: qsTr("SYNC AND RESTART") + (pop.hostName.length ? "  ·  " + pop.hostName : "")
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall); font.bold: true; font.letterSpacing: 1.6
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        // ── Ask ───────────────────────────────────────────────────────────────
        ColumnLayout {
            visible: pop.mode === "ask"
            Layout.fillWidth: true
            spacing: pop._px(12)
            Label {
                text: qsTr("%1 looks for games in its stores and updates %2's list; excluded games stay out.")
                          .arg(pop.hostName).arg(pop.server)
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontBody); color: Theme.text
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Label {
                text: qsTr("%1 then restarts to show it. It takes about a minute, and %2 is offline meanwhile.")
                          .arg(pop.server).arg(pop.hostName)
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall); color: Theme.text2
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Label {
                visible: pop.steamWarning
                text: "⚠ " + qsTr("%1 syncs Steam too, so Steam games would be listed twice. Turn one of the two off.").arg(pop.server)
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall); color: Theme.warning
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }

        // ── Working ───────────────────────────────────────────────────────────
        ColumnLayout {
            visible: pop.mode === "progress"
            Layout.fillWidth: true
            spacing: pop._px(12)
            Label {
                text: pop.stepText
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontBody); color: Theme.text
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            // Indeterminate, as Windows Update's: the steps take what they take (a scan is seconds
            // or a minute), and a bar that pretends to know would stall and lie.
            ProgressBar {
                Layout.fillWidth: true
                indeterminate: true
            }
            Label {
                text: qsTr("Step %1 of 5 · %2 s").arg(Math.max(1, pop.stepNumber)).arg(pop.elapsedS)
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontCaption); color: Theme.text2
                Layout.alignment: Qt.AlignHCenter
            }
        }

        // ── Terminal (done / failed / cannot start) ───────────────────────────
        ColumnLayout {
            visible: pop._terminal
            Layout.fillWidth: true
            spacing: pop._px(8)
            SuccessTick {
                Layout.alignment: Qt.AlignHCenter
                size: pop._px(30)
                visible: pop.mode === "done"
                onVisibleChanged: if (visible) play()
            }
            Label {
                text: pop.mode === "done" ? pop.resultText
                    : "⚠ " + (pop.mode === "blocked" ? pop.reason : pop.resultText)
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontBody)
                color: pop.mode === "done" ? Theme.inkText : Theme.danger
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }

        // ── Footer buttons ────────────────────────────────────────────────────
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: pop._px(4)
            spacing: pop._px(14)

            DialogButton {
                id: confirmBtn
                visible: pop.mode === "ask"
                text: qsTr("Sync and restart")
                affirmative: true
                onActivated: pop.confirmed()
                Keys.onRightPressed: closeBtn.forceActiveFocus()
            }
            DialogButton {
                id: hideBtn
                visible: pop.mode === "progress"
                text: qsTr("Hide")
                affirmative: true
                onActivated: pop.close()
            }
            DialogButton {
                id: closeBtn
                visible: pop.mode !== "progress"
                text: pop.mode === "ask" ? qsTr("Cancel") : qsTr("Close")
                affirmative: pop._terminal
                onActivated: pop.close()
                Keys.onLeftPressed: if (pop.mode === "ask") confirmBtn.forceActiveFocus()
            }
        }
    }

    onOpened: Qt.callLater(_focusForMode)
}
