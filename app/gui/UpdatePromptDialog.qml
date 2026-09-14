import QtQuick 2.15
import QtQuick.Controls 2.15
import Theme 1.0

// "A new version is out" — offered at startup, once per launch, when AppUpdate finds a newer
// release with an installer to update with (AppUpdate::shouldPrompt). Opened by AppShell.
//
// It only points the way: Yes goes to Settings → About, where Update now is, and nothing is
// downloaded or installed from here. The box stops the prompt for this version (and older);
// a newer release is offered again.
Popup {
    id: dlg

    property string latestVersion: ""

    signal accepted(bool dontRemind)
    signal declined(bool dontRemind)

    // The shared dialog measurements, all of them multiplied by the window scale — see
    // Theme.uiScale for why a dialog cannot take this from the page it is covering.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: _px(520)
    padding: _px(28)

    Overlay.modal: Rectangle { color: "#cc000000" }

    background: Rectangle {
        color: Theme.card
        radius: dlg._px(14)
        border.color: Theme.line
        border.width: 1
    }

    // Escape is a No, but it must not tick anything on the way out.
    onClosed: dontRemindCheck.checked = false

    // Focus on Yes: neither answer changes anything on its own — Yes only opens a page.
    onOpened: yesBtn.forceActiveFocus()

    // Centred, because this dialog asks a question (same rule as LinkRestoreDialog).
    contentItem: Column {
        spacing: 0

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("UPDATE")
            font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall)
            font.bold: true; font.letterSpacing: dlg._u * 1.6
            color: Theme.text3
        }

        Item { width: 1; height: dlg._px(12) }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("StreamLight %1 is available").arg(dlg.latestVersion)
            font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontH2); font.bold: true
            color: Theme.text
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: dlg._px(10) }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Go to Settings to update?")
            font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontBody)
            color: Theme.text2
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: dlg._px(18) }

        // Drawn like the check box in PowerDialog, the only other one in the app.
        CheckBox {
            id: dontRemindCheck
            anchors.horizontalCenter: parent.horizontalCenter
            checked: false
            activeFocusOnTab: true
            text: qsTr("Don't remind me about this version")

            KeyNavigation.down: yesBtn
            Keys.onReturnPressed: dontRemindCheck.toggle()
            Keys.onEnterPressed:  dontRemindCheck.toggle()
            Keys.onSpacePressed:  dontRemindCheck.toggle()

            indicator: Rectangle {
                implicitWidth: dlg._px(20)
                implicitHeight: dlg._px(20)
                radius: dlg._px(4)
                y: dontRemindCheck.height / 2 - height / 2
                color: dontRemindCheck.checked ? Theme.accent : "transparent"
                border.color: (dontRemindCheck.activeFocus || dontRemindCheck.checked) ? Theme.accent : Theme.lineHigh
                border.width: dontRemindCheck.activeFocus ? 2 : 1
                Label {
                    anchors.centerIn: parent
                    visible: dontRemindCheck.checked
                    text: "✓"
                    color: Theme.ground
                    font.pixelSize: dlg._px(Theme.fontSmall)
                    font.bold: true
                }
            }
            contentItem: Label {
                text: dontRemindCheck.text
                font.family: Theme.family
                font.pixelSize: dlg._px(Theme.fontSmall)
                color: Theme.text2
                verticalAlignment: Text.AlignVCenter
                leftPadding: dontRemindCheck.indicator.width + dlg._px(10)
            }
        }

        Item { width: 1; height: dlg._px(22) }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: dlg._px(10)

            // No width or height here: DialogButton carries the shared size and scales it.
            DialogButton {
                id: yesBtn
                text: qsTr("Yes")
                affirmative: true
                onActivated: {
                    var skip = dontRemindCheck.checked
                    dlg.close()
                    dlg.accepted(skip)
                }
                KeyNavigation.right: noBtn
                KeyNavigation.up: dontRemindCheck
            }
            DialogButton {
                id: noBtn
                text: qsTr("No")
                onActivated: {
                    var skip = dontRemindCheck.checked
                    dlg.close()
                    dlg.declined(skip)
                }
                KeyNavigation.left: yesBtn
                KeyNavigation.up: dontRemindCheck
            }
        }
    }
}
