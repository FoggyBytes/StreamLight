import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Remote "Update host" dialog. Drives the host's Windows Update via StreamTweak:
// scan → classified list → scope choice → install → reboot. State is fed in live by
// HomeScreen (which polls the host); this is just the view + the scope/confirm controls.
//
// "Hide" backgrounds the dialog (the host keeps working; a status-bar chip stays and RB
// reopens it). "Cancel"/"Close" clears the job. There is deliberately NO cancel once the
// install has started — aborting Windows Update mid-way is what corrupts it.
Popup {
    id: pop

    // ── Live state (bound from HomeScreen's update job) ───────────────────────
    property string hostName: ""
    property string phase: "IDLE"     // CHECKING|CHECK_READY|NO_UPDATES|DOWNLOADING|INSTALLING|REBOOTING|DONE|ERROR
    property int    percent: -1
    property string message: ""
    property string errorText: ""
    property var    updates: []       // [{title,kb,category,sizeMb,selectable}]
    property var    counts: ({})      // {security,defender,optional,upgrade}

    // ── Signals to HomeScreen ─────────────────────────────────────────────────
    signal install(string scope)      // scope: "SEC" | "ALL"
    signal hideRequested()            // background (keep job running)
    signal dismissed()                // clear the job

    readonly property bool _picking: phase === "CHECK_READY"
    readonly property bool _working: phase === "DOWNLOADING" || phase === "INSTALLING" || phase === "REBOOTING"
    readonly property bool _terminal: phase === "DONE" || phase === "NO_UPDATES" || phase === "ERROR"

    // Keep the gamepad/keyboard cursor inside the dialog: focus the control that is
    // actually visible for the current phase. Without this the modal popup never grabs
    // focus and the pad keeps moving the host cards behind it. Deferred via callLater so
    // the per-phase `visible` bindings have settled before we focus.
    function _focusForPhase() {
        if (_working)       hideBtn.forceActiveFocus()
        else if (_picking)  scopeSel.forceActiveFocus()
        else                cancelBtn.forceActiveFocus()   // CHECKING + terminal (Cancel/Close)
    }
    onPhaseChanged: Qt.callLater(_focusForPhase)

    modal: true
    Overlay.modal: Item {}
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    padding: 28
    implicitWidth: Math.min(1040, (Overlay.overlay ? Overlay.overlay.width : 1040) - 96)

    background: Rectangle {
        color: "#1a1a1a"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 12
    }

    function _fmtSize(mb) {
        if (mb <= 0) return ""
        return mb >= 1024 ? (mb / 1024).toFixed(1) + " GB" : mb + " MB"
    }
    function _n(key) { return (pop.counts && pop.counts[key] !== undefined) ? pop.counts[key] : 0 }

    contentItem: ColumnLayout {
        spacing: 18

        // Eyebrow
        Label {
            text: qsTr("WINDOWS UPDATE") + (pop.hostName.length ? "  ·  " + pop.hostName : "")
            font.family: "DM Sans"; font.pixelSize: 13; font.bold: true; font.letterSpacing: 1.6
            color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }

        // ── Checking ──────────────────────────────────────────────────────────
        ColumnLayout {
            visible: pop.phase === "CHECKING"
            Layout.alignment: Qt.AlignHCenter
            spacing: 14
            BusyIndicator { running: pop.phase === "CHECKING"; Layout.alignment: Qt.AlignHCenter }
            Label {
                text: qsTr("Checking for updates on the host…")
                font.family: "DM Sans"; font.pixelSize: 17; color: "#f0f0f0"
                Layout.alignment: Qt.AlignHCenter
            }
        }

        // ── Classified list + scope choice ────────────────────────────────────
        ColumnLayout {
            visible: pop._picking
            Layout.fillWidth: true
            spacing: 14

            // Category summary chips
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 18
                Repeater {
                    model: [
                        { k: "security", icon: "🔒", label: qsTr("Security & critical") },
                        { k: "defender", icon: "🛡️", label: qsTr("Defender") },
                        { k: "optional", icon: "⚙️", label: qsTr("Optional") }
                    ]
                    delegate: Label {
                        text: modelData.icon + " " + pop._n(modelData.k) + "  " + modelData.label
                        font.family: "DM Sans"; font.pixelSize: 13
                        color: pop._n(modelData.k) > 0 ? "#f0f0f0" : "#606060"
                    }
                }
            }

            // Scrollable list of updates (upgrades shown separately, non-selectable)
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, updatesCol.implicitHeight)
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    id: updatesCol
                    width: pop.width - 2 * pop.padding - 20
                    spacing: 6
                    Repeater {
                        model: pop.updates
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: row.implicitHeight + 20
                            radius: 6
                            color: modelData.category === "upgrade" ? "#231a0e" : "#202020"
                            border.color: modelData.category === "upgrade" ? "#5a4423" : "#2a2a2a"
                            border.width: 1
                            RowLayout {
                                id: row
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10
                                Label {
                                    Layout.alignment: Qt.AlignTop
                                    text: modelData.category === "upgrade" ? "⬆️"
                                        : modelData.category === "defender" ? "🛡️"
                                        : modelData.category === "optional" ? "⚙️" : "🔒"
                                    font.pixelSize: 14
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    // Full title — wraps instead of truncating.
                                    Label {
                                        text: modelData.title
                                        font.family: "DM Sans"; font.pixelSize: 13
                                        color: modelData.category === "upgrade" ? "#f0c890" : "#e8e8e8"
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                    }
                                    // KB number · download size, below the title. The size
                                    // is hidden for Defender/definition updates: WUA reports
                                    // the full-base worst case there (often >1 GB), which
                                    // wildly over-states the small delta that actually downloads.
                                    Label {
                                        text: {
                                            var kb = (modelData.kb && modelData.kb.length) ? modelData.kb : ""
                                            var sz = modelData.category === "defender" ? "" : pop._fmtSize(modelData.sizeMb)
                                            return (kb && sz) ? (kb + "  ·  " + sz) : (kb || sz)
                                        }
                                        visible: text.length > 0
                                        font.family: "DM Sans"; font.pixelSize: 11; color: "#909090"
                                    }
                                    Label {
                                        visible: modelData.category === "upgrade"
                                        text: qsTr("Feature update — not installed remotely. Run it on the host.")
                                        font.family: "DM Sans"; font.pixelSize: 11; color: "#b08850"
                                        wrapMode: Text.Wrap; Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Label {
                text: qsTr("The host restarts automatically if required.")
                font.family: "DM Sans"; font.pixelSize: 12; color: "#909090"
                Layout.alignment: Qt.AlignHCenter
            }

            SegmentedSelector {
                id: scopeSel
                Layout.alignment: Qt.AlignHCenter
                labels: [qsTr("Security + Defender"), qsTr("All updates")]
                currentIndex: 0
                Keys.onDownPressed: { installBtn.forceActiveFocus(); event.accepted = true }
            }
        }

        // ── Working (download / install / reboot) ─────────────────────────────
        ColumnLayout {
            visible: pop._working
            Layout.fillWidth: true
            spacing: 12
            Label {
                text: pop.message
                font.family: "DM Sans"; font.pixelSize: 17; color: "#f0f0f0"
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            ProgressBar {
                Layout.fillWidth: true
                // DOWNLOADING gives a real %; INSTALLING/REBOOTING are indeterminate.
                indeterminate: pop.percent < 0
                from: 0; to: 100
                value: pop.percent < 0 ? 0 : pop.percent
            }
            Label {
                visible: pop.percent >= 0
                text: pop.percent + "%"
                font.family: "DM Sans"; font.pixelSize: 13; color: "#909090"
                Layout.alignment: Qt.AlignHCenter
            }
        }

        // ── Terminal (done / none / error) ────────────────────────────────────
        ColumnLayout {
            visible: pop._terminal
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: pop.phase === "DONE" ? "✓ " + pop.message
                    : pop.phase === "NO_UPDATES" ? "✓ " + pop.message
                    : "⚠ " + pop.message
                font.family: "DM Sans"; font.pixelSize: 17
                color: pop.phase === "ERROR" ? "#ef4444" : Theme.accent
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Label {
                visible: pop.phase === "ERROR" && pop.errorText.length > 0
                text: pop.errorText
                font.family: "DM Sans"; font.pixelSize: 12; color: "#a0a0a0"
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }

        // ── Footer buttons (depend on phase) ──────────────────────────────────
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            spacing: 14

            // Install (only while picking)
            DialogButton {
                id: installBtn
                visible: pop._picking
                text: qsTr("Install")
                affirmative: true
                onActivated: pop.install(scopeSel.currentIndex === 1 ? "ALL" : "SEC")
                Keys.onRightPressed: cancelBtn.forceActiveFocus()
                Keys.onUpPressed:    scopeSel.forceActiveFocus()
            }
            // Hide (while working)
            DialogButton {
                id: hideBtn
                visible: pop._working
                text: qsTr("Hide")
                affirmative: true
                onActivated: pop.hideRequested()
            }
            // Cancel (picking/checking) or Close (terminal)
            DialogButton {
                id: cancelBtn
                visible: !pop._working
                text: pop._terminal ? qsTr("Close") : qsTr("Cancel")
                affirmative: pop._terminal
                onActivated: pop.dismissed()
                Keys.onLeftPressed: if (pop._picking) installBtn.forceActiveFocus()
                Keys.onUpPressed:   if (pop._picking) scopeSel.forceActiveFocus()
            }
        }
    }

    onOpened: Qt.callLater(_focusForPhase)
    onClosed: {
        if (typeof stackView !== "undefined" && stackView)
            stackView.forceActiveFocus()
    }
}
