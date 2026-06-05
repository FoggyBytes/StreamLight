import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Power-off chooser for a paired host. Lets the user shut down the host PC
// (via StreamTweak), the local client PC, or both. Standalone Popup (like
// AddHostDialog) so the focus chain is fully controllable for gamepad/keyboard:
//   SegmentedSelector ⇄ Confirm ⇄ Cancel   (Up/Down between rows, Left/Right between buttons)
// Follows the app dialog convention (§22): affirmative on the LEFT, green accent
// reserved for FOCUS only, and the safe/dismissive button (Cancel) focused first.
Popup {
    id: pop

    // ── Public API ────────────────────────────────────────────────────────────
    property int    pcIndex: -1
    property string hostName: ""
    // StreamTweak access state of the host, mirrors HomeScreen's pcCard.streamTweakAuth.
    // Host/Both targets need an approved ("authorized") host; Client is always allowed.
    property string authState: "none"
    readonly property bool hostAllowed: authState === "authorized"

    // Emitted on confirm with one of: "host" | "client" | "both".
    signal confirmed(string target)

    readonly property var _targets: ["host", "client", "both"]

    modal: true
    Overlay.modal: Item {}
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    padding: 32

    background: Rectangle {
        color: "#1a1a1a"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 12
    }

    function _bodyText() {
        switch (selector.currentIndex) {
        case 0:
            return qsTr("Shut down the host PC%1. The streaming session will end.")
                     .arg(pop.hostName.length > 0 ? (" (" + pop.hostName + ")") : "")
        case 1:
            return qsTr("Shut down this PC (the device you're using now).")
        case 2:
            return qsTr("Shut down the host PC%1, then shut down this PC.")
                     .arg(pop.hostName.length > 0 ? (" (" + pop.hostName + ")") : "")
        }
        return ""
    }

    function _commit() {
        var idx = selector.currentIndex
        if (selector.isDisabled(idx))
            return
        pop.confirmed(pop._targets[idx])
        pop.close()
    }

    contentItem: ColumnLayout {
        spacing: 22

        Label {
            text: qsTr("POWER")
            font.family: "DM Sans"
            font.pixelSize: 13
            font.bold: true
            font.letterSpacing: 1.6
            color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }

        SegmentedSelector {
            id: selector
            Layout.alignment: Qt.AlignHCenter
            labels: [qsTr("Host"), qsTr("Client"), qsTr("Both")]
            // Host (0) and Both (2) require an approved host; Client (1) is always on.
            disabledIndices: pop.hostAllowed ? [] : [0, 2]
            currentIndex: 1   // default to Client — always available, safe
            Keys.onDownPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
        }

        Label {
            text: pop._bodyText()
            font.family: "DM Sans"
            font.pixelSize: 18
            color: "#f0f0f0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }

        // Inline hint shown when the host hasn't approved this device yet.
        Label {
            visible: !pop.hostAllowed
            text: qsTr("Host shutdown needs StreamTweak access. Approve this device on the host (Settings → Bridge security) to enable Host and Both.")
            font.family: "DM Sans"
            font.pixelSize: 13
            color: "#a0a0a0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            spacing: 14

            Button {
                id: confirmBtn
                text: qsTr("Confirm")
                activeFocusOnTab: true
                onClicked: pop._commit()
                Keys.onReturnPressed: pop._commit()
                Keys.onEnterPressed:  pop._commit()
                Keys.onSpacePressed:  pop._commit()
                Keys.onRightPressed:  cancelBtn.forceActiveFocus()
                Keys.onUpPressed:     selector.forceActiveFocus()

                background: Rectangle {
                    implicitWidth: 140
                    implicitHeight: 42
                    radius: 8
                    color: confirmBtn.activeFocus ? Qt.rgba(0, 0.9, 0.46, 0.20)
                         : confirmBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                          "#1f1f1f"
                    border.color: confirmBtn.activeFocus ? "#00E676"
                                : confirmBtn.hovered     ? "#3a3a3a"
                                :                          "#2a2a2a"
                    border.width: confirmBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: confirmBtn.text
                    color: "#00E676"
                    font.family: "DM Sans"
                    font.pixelSize: 15
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                id: cancelBtn
                text: qsTr("Cancel")
                activeFocusOnTab: true
                onClicked: pop.close()
                Keys.onReturnPressed: pop.close()
                Keys.onEnterPressed:  pop.close()
                Keys.onSpacePressed:  pop.close()
                Keys.onLeftPressed:   confirmBtn.forceActiveFocus()
                Keys.onUpPressed:     selector.forceActiveFocus()

                background: Rectangle {
                    implicitWidth: 140
                    implicitHeight: 42
                    radius: 8
                    color: cancelBtn.activeFocus ? Qt.rgba(0, 0.9, 0.46, 0.20)
                         : cancelBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         "#1f1f1f"
                    border.color: cancelBtn.activeFocus ? "#00E676"
                                : cancelBtn.hovered     ? "#3a3a3a"
                                :                         "#2a2a2a"
                    border.width: cancelBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: cancelBtn.text
                    color: "#f0f0f0"
                    font.family: "DM Sans"
                    font.pixelSize: 15
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // Reset to the always-safe Client target on every open (clears any stale
    // Host/Both selection from a previous, authorized host), then put focus on
    // the dismissive button (destructive action — §22).
    onOpened: {
        selector.currentIndex = 1
        cancelBtn.forceActiveFocus()
    }
    onClosed: {
        if (typeof stackView !== "undefined" && stackView)
            stackView.forceActiveFocus()
    }
}
