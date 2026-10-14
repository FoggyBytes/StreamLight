import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import PyroWaveTest 1.0
import ComputerManager 1.0

// PyroWave Test (7.0.0), opened from the host's Options. Two steps — the wired network to the
// host, then this device's decoder — and a bitrate for the chosen target, which "Use" writes
// where the host's bitrate lives (its active profile when that sets one, else Settings).
// The pace the test finds is kept for the host and announced by every later PyroWave stream to
// it; that part needs no button.
//
// The target is chosen BEFORE the test, as in Nonary's calibrator: the frame-shaped probe sends
// frames as large as the target implies, so the pace it finds belongs to that target. Moving
// the target after a result turns the popup back into "Start" for the new one.
//
// Pad: ◀ ▶ target · A Start / Use · X Test again · B Cancel / Close. Keyboard: arrows, Enter,
// R, Esc. The status bar sits under the scrim, so the prompts are printed in the footer.
Popup {
    id: dlg

    property string hostUuid: ""
    property string hostName: ""
    // Set by HomeScreen: shows the "bitrate set" notice once the popup has closed.
    property var notify: null

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    transformOrigin: Popup.Center
    enter: PopEnter {}
    exit: PopExit {}
    modal: true
    Overlay.modal: Rectangle { color: Theme.scrim }
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnPressOutside
    padding: dlg._px(32)
    width: dlg._px(760)

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: dlg._px(14)
    }

    // ── state ────────────────────────────────────────────────────────────────────────────────
    property int _target: 1
    property var _settings: ({})
    readonly property int _phase: PyroWaveTest.hostUuid === dlg.hostUuid ? PyroWaveTest.phase : 0
    readonly property var _result: PyroWaveTest.result
    readonly property bool _running: _phase === 1 || _phase === 2
    // A result for another target than the one now selected is not an answer: offer Start.
    readonly property bool _done: _phase === 3 && _result.target === _target
    readonly property bool _idle: !_running && !_done

    readonly property var _targets: [
        { name: qsTr("Minimum"),     what: qsTr("Half of Recommended: the lowest bitrate that still looks clean") },
        { name: qsTr("Recommended"), what: qsTr("A good image, the PyroWave author's 35 dB") },
        { name: qsTr("Moderate"),    what: qsTr("60% of the slower link, room for everything else on the network") },
        { name: qsTr("Maximum"),     what: qsTr("All the network carries without loss, within this device's reach") }
    ]

    function _start() {
        PyroWaveTest.start(ComputerManager, dlg.hostUuid, dlg._target)
    }
    function _use() {
        var where = PyroWaveTest.applyBitrate(dlg.hostUuid, dlg._result.bitrateKbps)
        var text = qsTr("Video bitrate set to %1 Mbps %2").arg(Math.round(dlg._result.bitrateKbps / 1000)).arg(where)
        dlg.close()
        if (dlg.notify) dlg.notify(text)
    }
    function _back() {
        if (dlg._running) PyroWaveTest.cancel()
        else dlg.close()
    }

    onAboutToShow: {
        PyroWaveTest.show(dlg.hostUuid)
        dlg._settings = PyroWaveTest.settingsFor(dlg.hostUuid)
        var saved = PyroWaveTest.savedResult(dlg.hostUuid)
        dlg._target = saved.target !== undefined ? saved.target : 1
    }
    onOpened: keys.forceActiveFocus()
    onClosed: if (dlg._running) PyroWaveTest.cancel()

    // ── pieces ───────────────────────────────────────────────────────────────────────────────
    component Line: Label {
        width: parent ? parent.width : implicitWidth
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.family: Theme.family
        font.pixelSize: dlg._px(Theme.fontSmall)
        color: Theme.text2
    }

    // One step: number or check, name, what is happening, its bar.
    component StepRow: Item {
        id: step
        property int n: 1
        property string name: ""
        property string status: ""
        property real p: 0
        property bool active: false
        property bool done: false
        width: parent ? parent.width : 0
        height: dlg._px(58)

        Rectangle {
            id: dot
            width: dlg._px(28); height: width; radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: step.done ? Qt.rgba(Theme.online.r, Theme.online.g, Theme.online.b, 0.16)
                 : step.active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : "transparent"
            border.color: step.done ? Theme.online : step.active ? Theme.ink : Theme.lineHigh
            border.width: 1.5
            Label {
                anchors.centerIn: parent
                text: step.done ? "✓" : step.n
                font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall); font.bold: true
                color: step.done ? Theme.online : step.active ? Theme.inkText : Theme.text3
            }
        }
        Column {
            anchors.left: dot.right; anchors.leftMargin: dlg._px(14); anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: dlg._px(7)
            Item {
                width: parent.width; height: stepName.implicitHeight
                Label {
                    id: stepName
                    text: step.name
                    font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontBody); font.bold: true
                    color: step.active || step.done ? Theme.text : Theme.text3
                }
                Label {
                    anchors.right: parent.right
                    anchors.left: stepName.right; anchors.leftMargin: dlg._px(16)
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideLeft
                    text: step.status
                    font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall)
                    color: step.active ? Theme.text : Theme.text2
                }
            }
            Rectangle {
                width: parent.width; height: dlg._px(4); radius: height / 2
                color: Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.10)
                Rectangle {
                    height: parent.height; radius: parent.radius
                    width: parent.width * (step.done ? 1 : step.active ? Math.max(0.02, step.p) : 0)
                    color: step.done ? Theme.online : Theme.accentFill
                    Behavior on width { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                }
            }
        }
    }

    component Prompt: Row {
        property string buttonKey: ""
        property string keyLabel: ""
        property string action: ""
        spacing: dlg._px(6)
        ActionHint { anchors.verticalCenter: parent.verticalCenter; buttonKey: parent.buttonKey; keyLabel: parent.keyLabel; size: dlg._px(18) }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: parent.action
            font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption)
            color: Theme.text3
        }
    }

    // ── content ──────────────────────────────────────────────────────────────────────────────
    contentItem: FocusScope {
        id: keys
        focus: true
        implicitHeight: body.implicitHeight

        Keys.onPressed: function(event) {
            var k = event.key
            if (k === Qt.Key_Left || k === Qt.Key_Right) {
                if (!dlg._running) dlg._target = Math.max(0, Math.min(3, dlg._target + (k === Qt.Key_Left ? -1 : 1)))
            } else if (k === Qt.Key_Return || k === Qt.Key_Enter || k === Qt.Key_Space) {
                if (dlg._done && dlg._settings.pyroWave) dlg._use()
                else if (dlg._idle) dlg._start()
            } else if (k === Qt.Key_Menu || k === Qt.Key_R) {
                if (dlg._done) dlg._start()
            } else if (k === Qt.Key_Escape || k === Qt.Key_Back) {
                dlg._back()
            } else {
                return
            }
            event.accepted = true
        }

        Column {
            id: body
            width: parent.width
            spacing: dlg._px(16)

            Column {
                width: parent.width
                spacing: dlg._px(6)
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("PYROWAVE TEST") + (dlg.hostName.length ? "  ·  " + dlg.hostName : "")
                    font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall); font.bold: true; font.letterSpacing: 1.6
                    color: Theme.text3
                }
                Line {
                    text: dlg._idle && dlg._phase !== 4
                          ? qsTr("The wired network to the host, then this device's decoder · about a minute")
                          : qsTr("For %1 · now %2 Mbps").arg(dlg._settings.format || "").arg(Math.round((dlg._settings.bitrateKbps || 0) / 1000))
                }
            }

            // The targets, chosen before the test
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: dlg._px(8)
                Repeater {
                    model: dlg._targets
                    Rectangle {
                        readonly property bool sel: index === dlg._target
                        width: Math.floor((body.width - dlg._px(24)) / 4)
                        height: dlg._px(44)
                        radius: dlg._px(10)
                        opacity: dlg._running && !sel ? 0.45 : 1
                        color: sel ? Theme.accentFill : Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.05)
                        border.color: sel ? Theme.accentFill : Theme.line
                        border.width: 1
                        Label {
                            anchors.centerIn: parent
                            text: modelData.name
                            font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontSmall); font.bold: true
                            color: parent.sel ? Theme.onAccent : Theme.text2
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: !dlg._running
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dlg._target = index
                        }
                    }
                }
            }
            Line { text: dlg._targets[dlg._target].what; color: Theme.text3 }

            StepRow {
                n: 1; name: qsTr("Network")
                active: dlg._phase === 1; done: dlg._done || dlg._phase === 2
                p: dlg._phase === 1 ? PyroWaveTest.progress : 0
                status: dlg._phase === 1 ? PyroWaveTest.status
                      : dlg._done ? qsTr("up to %1 Mbps").arg(dlg._result.budgetMbps)
                                    + (dlg._result.paceMbps > 0 ? qsTr(" · frames at %1 Mbps").arg(dlg._result.paceMbps) : "")
                      : qsTr("this device ↔ %1, wired").arg(dlg.hostName)
            }
            StepRow {
                n: 2; name: qsTr("Decoder")
                active: dlg._phase === 2; done: dlg._done
                p: dlg._phase === 2 ? PyroWaveTest.progress : 0
                status: dlg._phase === 2 ? PyroWaveTest.status
                      : dlg._done ? qsTr("%1 ms per frame").arg(dlg._result.frameMs)
                      : qsTr("this device's GPU")
            }

            // ── the answer ──
            Column {
                visible: dlg._done
                width: parent.width
                spacing: dlg._px(8)
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    topPadding: dlg._px(6)
                    text: qsTr("%1 Mbps").arg(Math.round((dlg._result.bitrateKbps || 0) / 1000))
                    font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontDisplay); font.bold: true
                    color: Theme.text
                }
                Line {
                    text: (dlg._result.quality === "full" ? qsTr("Good image") :
                           dlg._result.quality === "reduced" ? qsTr("Slightly soft image") : qsTr("Soft image"))
                          + qsTr(" · %1 dB").arg(dlg._result.qualityDb)
                          + (dlg._result.deviceLimited ? qsTr(" · lowered so this device keeps up") :
                             dlg._result.linkLimited ? qsTr(" · limited by the network") : "")
                }
                Line {
                    visible: dlg._result.keepsUp === false
                    text: qsTr("This device falls behind even at this bitrate: a lower resolution or frame rate will be smoother")
                    color: Theme.warning
                }
                Line {
                    visible: dlg._result.paceMbps > 0
                    text: qsTr("Every PyroWave stream to %1 now asks for its frames at %2 Mbps%3")
                          .arg(dlg.hostName).arg(dlg._result.paceMbps)
                          .arg(dlg._result.paceQualified ? "" : qsTr(", the best this network managed"))
                    font.pixelSize: dlg._px(Theme.fontCaption)
                    color: Theme.text3
                }
                Line {
                    visible: dlg._settings.pyroWave === false
                    text: qsTr("PyroWave isn't the codec for this host: choose it in Settings → Video to use this")
                    color: Theme.warning
                }
                Line {
                    text: qsTr("Tested %1 · %2").arg(dlg._result.testedDay || "").arg(dlg._result.format || "")
                    font.pixelSize: dlg._px(Theme.fontCaption)
                    color: Theme.text3
                }
            }

            Line {
                visible: dlg._phase === 4 && PyroWaveTest.error.length > 0
                text: PyroWaveTest.error
                color: Theme.danger
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: dlg._px(12)
                DialogButton {
                    visible: dlg._idle
                    affirmative: true
                    text: dlg._phase === 4 ? qsTr("Try again") : qsTr("Start")
                    onActivated: dlg._start()
                }
                DialogButton {
                    visible: dlg._running
                    text: qsTr("Cancel")
                    onActivated: PyroWaveTest.cancel()
                }
                DialogButton {
                    visible: dlg._done && dlg._settings.pyroWave === true
                    affirmative: true
                    text: qsTr("Use %1 Mbps").arg(Math.round((dlg._result.bitrateKbps || 0) / 1000))
                    onActivated: dlg._use()
                }
                DialogButton {
                    visible: dlg._done
                    text: qsTr("Test again")
                    onActivated: dlg._start()
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: dlg._px(22)
                Label {
                    visible: !dlg._running
                    anchors.verticalCenter: parent.verticalCenter
                    text: "◀ ▶  " + qsTr("Target")
                    font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption)
                    color: Theme.text3
                }
                Prompt { visible: dlg._idle; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Start") }
                Prompt { visible: dlg._done && dlg._settings.pyroWave === true; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Use") }
                Prompt { visible: dlg._done; buttonKey: "X"; keyLabel: "R"; action: qsTr("Test again") }
                Prompt { buttonKey: "B"; keyLabel: "Esc"; action: dlg._running ? qsTr("Cancel") : qsTr("Close") }
            }
        }
    }
}
