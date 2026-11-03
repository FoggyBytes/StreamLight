import Theme 1.0
import TvLink 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Settings → TV (8.4.0): pairing an LG TV with this PC. Look for TVs, pick one (A on it pairs),
// then the TV asks — Allow is pressed with the TV's own remote — and the dialog moves on by itself.
// Built as SyncRestartDialog and UpdateDialog are: the same Popup, scrim, eyebrow, centred lines,
// Spinner, SuccessTick and DialogButtons. ⚠️ A popup here is always built on the app's own pieces.
//
// The work is the service's (TvLink); this only follows TvLink's state. Cancel while the TV is
// asking hangs up the pipe, and the service stops waiting.
Popup {
    id: pop

    enter: PopEnter {}
    exit: PopExit {}

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    readonly property string mode: TvLink.searching ? "search"
                                 : TvLink.pairState === TvLink.PairConnecting ? "connecting"
                                 : TvLink.pairState === TvLink.PairPrompt ? "prompt"
                                 : TvLink.pairState === TvLink.PairDone ? "done"
                                 : TvLink.pairState === TvLink.PairFailed ? "failed"
                                 : TvLink.found.length > 0 ? "choose"
                                 : "none"
    readonly property bool _waiting: mode === "search" || mode === "connecting" || mode === "prompt"

    // Two TVs of the same model would read the same: the address tells them apart.
    function _label(tv) {
        var same = 0
        for (var i = 0; i < TvLink.found.length; i++)
            if (TvLink.found[i].name === tv.name) same++
        return same > 1 ? tv.name + " · " + tv.ip.split(".").pop() : tv.name
    }

    function start() {
        TvLink.resetPair()
        TvLink.discover()
        open()
    }

    function _focusForMode() {
        if (mode === "choose" && tvRepeater.count > 0) tvRepeater.itemAt(0).forceActiveFocus()
        else if (mode === "none")                     searchAgainBtn.forceActiveFocus()
        else                                          closeBtn.forceActiveFocus()
    }
    onModeChanged: Qt.callLater(_focusForMode)
    onOpened: Qt.callLater(_focusForMode)

    modal: true
    Overlay.modal: Rectangle { color: Theme.scrim }
    focus: true
    anchors.centerIn: Overlay.overlay
    // B closes it; while pairing, closing is a cancel.
    closePolicy: Popup.CloseOnEscape
    onAboutToHide: if (TvLink.pairState === TvLink.PairConnecting || TvLink.pairState === TvLink.PairPrompt) TvLink.cancelPair()
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
            text: qsTr("SET UP A TV")
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall); font.bold: true; font.letterSpacing: 1.6
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        // ── Looking, connecting, waiting for Allow ────────────────────────────
        ColumnLayout {
            visible: pop._waiting
            Layout.fillWidth: true
            spacing: pop._px(14)
            Spinner {
                running: pop._waiting && pop.visible
                implicitWidth: pop._px(44); implicitHeight: implicitWidth
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: pop.mode === "search" ? qsTr("Looking for LG TVs on this network…")
                    : pop.mode === "connecting" ? qsTr("Connecting to the TV…")
                    : qsTr("The TV is asking. Choose Allow with the TV's remote.")
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontBody); color: Theme.text
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Label {
                visible: pop.mode === "search"
                text: qsTr("LG webOS TVs only — other brands are not supported. Turn the TV on if it is not found.")
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall); color: Theme.text2
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }

        // ── Pick one ──────────────────────────────────────────────────────────
        ColumnLayout {
            visible: pop.mode === "choose"
            Layout.fillWidth: true
            spacing: pop._px(10)
            Label {
                text: TvLink.found.length === 1 ? qsTr("Found one TV. Pair it with this PC?")
                                                : qsTr("Found %1 TVs. Which one is this PC plugged into?").arg(TvLink.found.length)
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontBody); color: Theme.text
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Repeater {
                id: tvRepeater
                model: pop.mode === "choose" ? TvLink.found : []
                DialogButton {
                    required property var modelData
                    required property int index
                    Layout.alignment: Qt.AlignHCenter
                    text: pop._label(modelData)
                    affirmative: true
                    onActivated: TvLink.pair(modelData.ip, modelData.name)
                    Keys.onDownPressed: (index + 1 < tvRepeater.count ? tvRepeater.itemAt(index + 1) : closeBtn).forceActiveFocus()
                    Keys.onUpPressed: if (index > 0) tvRepeater.itemAt(index - 1).forceActiveFocus()
                }
            }
        }

        // ── Nothing found ─────────────────────────────────────────────────────
        Label {
            visible: pop.mode === "none"
            text: qsTr("No LG TV answered. Turn it on, check it is on this network, and look again.")
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontBody); color: Theme.text
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
        }

        // ── Paired / failed ───────────────────────────────────────────────────
        ColumnLayout {
            visible: pop.mode === "done" || pop.mode === "failed"
            Layout.fillWidth: true
            spacing: pop._px(8)
            SuccessTick {
                Layout.alignment: Qt.AlignHCenter
                size: pop._px(30)
                visible: pop.mode === "done"
                onVisibleChanged: if (visible) play()
            }
            Label {
                text: pop.mode === "done"
                      ? (TvLink.input > 0 ? qsTr("Paired. This PC is on HDMI %1 (%2).").arg(TvLink.input).arg(TvLink.inputs[TvLink.input - 1])
                                          : qsTr("Paired. Pick this PC's input in Settings → TV."))
                      : "⚠ " + (TvLink.pairError === "denied on the TV" ? qsTr("The TV said no.") : TvLink.pairError)
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontBody)
                color: pop.mode === "done" ? Theme.inkText : Theme.danger
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }

        // ── Footer ────────────────────────────────────────────────────────────
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: pop._px(4)
            spacing: pop._px(14)

            DialogButton {
                id: searchAgainBtn
                visible: pop.mode === "none" || pop.mode === "failed"
                text: qsTr("Look again")
                affirmative: true
                onActivated: { TvLink.resetPair(); TvLink.discover() }
                Keys.onRightPressed: closeBtn.forceActiveFocus()
            }
            DialogButton {
                id: closeBtn
                text: pop.mode === "done" ? qsTr("Done") : qsTr("Cancel")
                affirmative: pop.mode === "done"
                onActivated: pop.close()
                Keys.onLeftPressed: if (searchAgainBtn.visible) searchAgainBtn.forceActiveFocus()
                Keys.onUpPressed: if (pop.mode === "choose" && tvRepeater.count > 0) tvRepeater.itemAt(tvRepeater.count - 1).forceActiveFocus()
            }
        }
    }
}
