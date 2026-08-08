import Theme 1.0
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
    // StreamTweak access state of the host, mirrors HomeScreen's currentHost.auth.
    // Host/Both targets need an approved ("authorized") host; Client is always allowed.
    property string authState: "none"
    readonly property bool hostAllowed: authState === "authorized"

    // Client-only mode: opened from the X shortcut when there's no reachable/paired
    // host (offline host, the "+ Add" tile, or no hosts at all). Host & Both stay
    // disabled and the access hint is suppressed — the user can still power off THIS
    // PC. Set by the caller before open(); reset to false for the full host chooser.
    property bool clientOnly: false

    // Windows-update status of each side, set by HomeScreen when the dialog opens.
    //   host:   "checking" | "pending" | "none" | "unavailable" (not authorized)
    //   client: "pending" | "none"  (always determinable locally)
    // Fast reboot-pending check (registry / UPDATESTATE) — for a full scan the user has the
    // dedicated "Windows Update" flow. Drives the evidence rows and gates the checkbox.
    property string hostUpdateState: "unavailable"
    property string clientUpdateState: "none"

    readonly property bool _targetIncludesHost:   selector.currentIndex === 0 || selector.currentIndex === 2
    readonly property bool _targetIncludesClient: selector.currentIndex === 1 || selector.currentIndex === 2
    readonly property bool _targetHasPendingUpdates:
           (_targetIncludesHost   && pop.hostUpdateState   === "pending")
        || (_targetIncludesClient && pop.clientUpdateState === "pending")
    readonly property bool _targetChecking:
        _targetIncludesHost && pop.hostUpdateState === "checking"

    // Emitted on confirm with target ("host"|"client"|"both") and whether to install
    // pending Windows updates before powering off.
    signal confirmed(string target, bool installUpdates)

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
        // Only ever send installUpdates when the option is enabled (target has updates).
        pop.confirmed(pop._targets[idx], pop._targetHasPendingUpdates && updatesCheck.checked)
        pop.close()
    }

    function _pillText(state) {
        switch (state) {
        case "pending":     return "🟠  " + qsTr("Updates pending")
        case "none":        return "✓  " + qsTr("Up to date")
        case "checking":    return "⏳  " + qsTr("Checking…")
        case "unavailable": return "—  " + qsTr("needs StreamTweak access")
        }
        return ""
    }
    function _pillColor(state) {
        return state === "pending" ? "#f59e0b" : state === "none" ? Theme.accent : "#909090"
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
            Keys.onDownPressed: { (updatesCheck.enabled ? updatesCheck : cancelBtn).forceActiveFocus(); event.accepted = true }
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

        // Inline hint shown when the host hasn't approved this device yet. Suppressed
        // in client-only mode (no host context — the hint would be misleading).
        Label {
            visible: !pop.hostAllowed && !pop.clientOnly
            text: qsTr("Host shutdown needs StreamTweak access. Approve this device on the host (Settings → Bridge security) to enable Host and Both.")
            font.family: "DM Sans"
            font.pixelSize: 13
            color: "#a0a0a0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }

        // ── Evidence: Windows-update status per side (rows shown for the target) ──
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
            spacing: 4

            Label {
                text: qsTr("WINDOWS UPDATES")
                font.family: "DM Sans"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.2
                color: "#606060"
                Layout.alignment: Qt.AlignHCenter
            }
            RowLayout {
                visible: pop._targetIncludesHost
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                Label {
                    text: "🖥  " + qsTr("Host") + (pop.hostName.length ? " (" + pop.hostName + ")" : "")
                    font.family: "DM Sans"; font.pixelSize: 13; color: "#c0c0c0"
                }
                Label {
                    text: pop._pillText(pop.hostUpdateState)
                    font.family: "DM Sans"; font.pixelSize: 13; color: pop._pillColor(pop.hostUpdateState)
                }
            }
            RowLayout {
                visible: pop._targetIncludesClient
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                Label {
                    text: "💻  " + qsTr("This PC")
                    font.family: "DM Sans"; font.pixelSize: 13; color: "#c0c0c0"
                }
                Label {
                    text: pop._pillText(pop.clientUpdateState)
                    font.family: "DM Sans"; font.pixelSize: 13; color: pop._pillColor(pop.clientUpdateState)
                }
            }
        }

        // "Update and shut down": install pending Windows updates before power-off.
        // Default OFF (opt-in) and ENABLED only when the selected target has updates
        // pending — installing can make shutdown take much longer.
        CheckBox {
            id: updatesCheck
            Layout.alignment: Qt.AlignHCenter
            checked: false
            enabled: pop._targetHasPendingUpdates
            opacity: enabled ? 1.0 : 0.4
            activeFocusOnTab: true
            text: qsTr("Install pending updates before shutting down")
            onEnabledChanged: if (!enabled) checked = false

            Keys.onUpPressed:     selector.forceActiveFocus()
            Keys.onDownPressed:   cancelBtn.forceActiveFocus()
            Keys.onReturnPressed: updatesCheck.toggle()
            Keys.onEnterPressed:  updatesCheck.toggle()
            Keys.onSpacePressed:  updatesCheck.toggle()

            indicator: Rectangle {
                implicitWidth: 20
                implicitHeight: 20
                radius: 4
                y: updatesCheck.height / 2 - height / 2
                color: updatesCheck.checked ? Theme.accent : "transparent"
                border.color: (updatesCheck.activeFocus || updatesCheck.checked) ? Theme.accent : "#3a3a3a"
                border.width: updatesCheck.activeFocus ? 2 : 1
                Label {
                    anchors.centerIn: parent
                    visible: updatesCheck.checked
                    text: "✓"
                    color: "#0d0d0d"
                    font.pixelSize: 14
                    font.bold: true
                }
            }
            contentItem: Label {
                text: updatesCheck.text
                font.family: "DM Sans"
                font.pixelSize: 14
                color: "#d0d0d0"
                verticalAlignment: Text.AlignVCenter
                leftPadding: updatesCheck.indicator.width + 10
            }
        }

        // Note shown when the checkbox is disabled: either still checking, or nothing
        // pending (with a pointer to the full Windows Update flow).
        Label {
            visible: !pop._targetHasPendingUpdates
            text: pop._targetChecking
                  ? qsTr("Checking for updates…")
                  : qsTr("Nothing pending for the selected device. For a full check, use \"Windows Update\".")
            font.family: "DM Sans"
            font.pixelSize: 13
            color: "#909090"
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
                Keys.onUpPressed:     (updatesCheck.enabled ? updatesCheck : selector).forceActiveFocus()

                background: Rectangle {
                    implicitWidth: 140
                    implicitHeight: 42
                    radius: 8
                    color: confirmBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : confirmBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                          "#1f1f1f"
                    border.color: confirmBtn.activeFocus ? Theme.accent
                                : confirmBtn.hovered     ? "#3a3a3a"
                                :                          "#2a2a2a"
                    border.width: confirmBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: confirmBtn.text
                    color: Theme.accent
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
                Keys.onUpPressed:     (updatesCheck.enabled ? updatesCheck : selector).forceActiveFocus()

                background: Rectangle {
                    implicitWidth: 140
                    implicitHeight: 42
                    radius: 8
                    color: cancelBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : cancelBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         "#1f1f1f"
                    border.color: cancelBtn.activeFocus ? Theme.accent
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
        updatesCheck.checked = false   // explicit opt-in every time (§ default OFF)
        cancelBtn.forceActiveFocus()
    }
    onClosed: {
        if (typeof stackView !== "undefined" && stackView)
            stackView.forceActiveFocus()
    }
}
