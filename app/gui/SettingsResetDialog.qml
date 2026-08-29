import QtQuick 2.15
import QtQuick.Controls 2.15
import Theme 1.0

// Shown once, on the first launch after upgrading from a build that shared Moonlight's
// settings store (5.3.0 and earlier). From 5.4.0 StreamLight has its own store and
// nothing carries over, so the app comes up with no hosts and default settings.
//
// This exists because that is indistinguishable from a fault. The app never shows its
// changelog at runtime, so README and release notes reach nobody at the moment the
// empty host list appears — this is the only channel that does. See storereset.h.
//
// It states what happened, what to do about it, and that nothing was destroyed. The
// reasoning belongs in the changelog, not on screen.
Popup {
    id: dlg

    // The shared dialog measurements, all of them multiplied by the window scale — see
    // Theme.uiScale for why a dialog cannot take this from the page it is covering.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    modal: true
    dim: true
    focus: true
    // No CloseOnEscape: dismissing is what marks the notice as seen, and escaping past
    // it would leave the user with an empty host list and no explanation for it.
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: _px(560)
    padding: _px(28)

    Overlay.modal: Rectangle { color: "#cc000000" }

    background: Rectangle {
        color: Theme.card
        radius: dlg._px(14)
        border.color: Theme.line
        border.width: 1
    }

    onOpened: okBtn.forceActiveFocus()

    contentItem: Column {
        spacing: 0

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("SETTINGS")
            font.family: Theme.family; font.pixelSize: dlg._px(13)
            font.bold: true; font.letterSpacing: dlg._u * 1.6
            color: Theme.text3
        }

        Item { width: 1; height: dlg._px(12) }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Your settings have been reset")
            font.family: Theme.family; font.pixelSize: dlg._px(22); font.bold: true
            color: Theme.text
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: dlg._px(10) }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            // ⚠️ Three steps, not two. The third is the one that reads as a fault:
            // everything else works and the bridge just stays silent, because a
            // freshly discovered host record starts with the integration off
            // (nvcomputer.cpp) — the same default any new host gets.
            text: qsTr("StreamLight no longer shares its settings with Moonlight, so this version starts from scratch. Pair your hosts again; then, if you use StreamTweak, approve this device on the host and switch the integration back on for each host in Settings.")
            font.family: Theme.family; font.pixelSize: dlg._px(15)
            color: Theme.text2
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: dlg._px(10) }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Nothing was deleted — the old settings are still where they were.")
            font.family: Theme.family; font.pixelSize: dlg._px(15)
            color: Theme.text3
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: dlg._px(22) }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter

            // No width or height here: DialogButton carries the shared size and scales it.
            DialogButton {
                id: okBtn
                text: qsTr("Got it")
                affirmative: true
                onActivated: dlg.close()
            }
        }
    }
}
