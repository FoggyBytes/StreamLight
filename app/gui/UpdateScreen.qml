// ⚠️ Unversioned, like AmbientWaves: FrameAnimation and friends need a recent QtQuick.
import QtQuick
import QtQuick.Window

import Theme 1.0
import AppUpdate 1.0

/*
 * The update screen (8.6.0, mockup A "Apertura", docs/mockups/aggiornamento/): the opening
 * animation's last frame — icon and STREAMLIGHT on the waves — with a bar under the name.
 *
 * It is the first third of one screen that three programs draw in turn:
 *
 *   1. here, while StreamLightUpdater.exe checks the release and its copy of the installer
 *      (PhaseCheck), or before the Windows prompt when there is no task (PhaseAsk);
 *   2. the updater, which redraws THIS frame from look.json (writeLook) over this window
 *      before the app closes, and moves the bar with Setup's progress;
 *   3. StreamLight again, reopened by the updater, with "Updated to …" after its opening.
 *
 * ⚠️ So the frame handed over must be one the updater can draw exactly: at PhaseClosing the
 * waves stop (AmbientBackground.frozen), the indeterminate segment goes, and nothing animates —
 * the updater draws the same floor, waves at the same `flowTime`, icon, letters, line and bar
 * (updater/screen.cpp). Every position it needs is measured here, never assumed there.
 */
FocusScope {
    id: screen

    readonly property int phase: AppUpdate.installPhase
    readonly property bool shown: phase !== AppUpdate.PhaseNone
    readonly property real _u: Theme.uiScale

    visible: shown
    enabled: shown
    focus: shown

    property Item _focusBefore: null
    onShownChanged: {
        if (shown) {
            _focusBefore = Window.activeFocusItem
            forceActiveFocus()
        } else if (_focusBefore) {
            _focusBefore.forceActiveFocus()
            _focusBefore = null
        }
    }

    // A, B and nothing else; the pages under it must not see a key while it is up.
    Keys.onPressed: function (event) {
        event.accepted = true
        const a = event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space
        const b = event.key === Qt.Key_Escape || event.key === Qt.Key_Back
        if (a) {
            if (phase === AppUpdate.PhaseAsk) AppUpdate.confirmPermission()
            else if (phase === AppUpdate.PhaseFailed || phase === AppUpdate.PhaseCancelled) AppUpdate.closeInstallScreen()
        } else if (b) {
            if (phase === AppUpdate.PhaseAsk || phase === AppUpdate.PhaseCheck) AppUpdate.cancelInstall()
            else if (phase === AppUpdate.PhaseFailed || phase === AppUpdate.PhaseCancelled) AppUpdate.closeInstallScreen()
        }
    }
    // The mouse ends here too.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true }

    readonly property bool _closing: phase === AppUpdate.PhaseClosing
    readonly property string _status: phase === AppUpdate.PhaseAsk ? qsTr("Windows will ask for permission")
                                    : phase === AppUpdate.PhaseCheck ? qsTr("Checking the update")
                                    : phase === AppUpdate.PhaseClosing ? qsTr("Closing StreamLight")
                                    : phase === AppUpdate.PhaseFailed ? qsTr("The update could not be verified")
                                    : phase === AppUpdate.PhaseCancelled ? qsTr("Update cancelled")
                                    : ""
    readonly property string _detail: phase === AppUpdate.PhaseAsk ? qsTr("A keyboard or mouse is needed to answer it.")
                                    : phase === AppUpdate.PhaseFailed ? AppUpdate.installMessage + qsTr(". Nothing was installed.")
                                    : phase === AppUpdate.PhaseCancelled ? qsTr("Nothing was installed.")
                                    : ""
    readonly property bool _barShown: phase === AppUpdate.PhaseCheck || phase === AppUpdate.PhaseClosing

    AmbientBackground {
        id: floor
        anchors.fill: parent
        frozen: screen._closing
    }

    Column {
        id: brand
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(screen.height * 0.20)
        spacing: Math.round(24 * screen._u)

        Image {
            id: icon
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.round(150 * screen._u)
            height: width
            source: "qrc:/streamlight.ico"
            sourceSize.width: width * Screen.devicePixelRatio
            sourceSize.height: height * Screen.devicePixelRatio
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        // A letter at a time, as the opening animation spells it: the updater places each one
        // where this row put it.
        Row {
            id: word
            anchors.horizontalCenter: parent.horizontalCenter
            readonly property int fontPx: Math.round(36 * screen._u)
            spacing: Math.round(fontPx * 0.32)
            Repeater {
                id: letters
                model: "STREAMLIGHT".length
                delegate: Text {
                    text: "STREAMLIGHT".charAt(index)
                    color: Theme.text
                    font.family: Theme.family
                    font.pixelSize: word.fontPx
                    font.bold: true
                }
            }
        }
    }

    Column {
        id: block
        readonly property real w: Math.round(480 * screen._u)
        width: w
        anchors.horizontalCenter: parent.horizontalCenter
        y: brand.y + brand.height + Math.round(64 * screen._u)
        spacing: Math.round(12 * screen._u)

        Item {
            width: block.w
            height: status.implicitHeight
            Text {
                id: status
                x: screen._barShown ? 0 : (block.w - width) / 2
                text: screen._status
                color: screen.phase === AppUpdate.PhaseFailed ? Theme.danger : Theme.text
                font.family: Theme.family
                font.pixelSize: Math.round(21 * screen._u)
                font.weight: Font.Medium
            }
            // Empty until Setup runs: the percentage is the updater's to draw (same place).
            Text {
                id: percent
                anchors.right: parent.right
                anchors.baseline: status.baseline
                text: ""
                color: Theme.text2
                font.family: Theme.monoFamily
                font.pixelSize: Math.round(17 * screen._u)
            }
        }

        Rectangle {
            id: bar
            visible: screen._barShown
            width: block.w
            height: Math.max(3, Math.round(5 * screen._u))
            radius: height / 2
            color: Theme.line
            clip: true
            // While the updater checks: a segment going across. Gone once it is ready — the
            // updater starts from an empty track and fills it with Setup's progress.
            Rectangle {
                id: segment
                visible: screen.phase === AppUpdate.PhaseCheck
                height: parent.height
                radius: parent.radius
                width: parent.width * 0.28
                color: Theme.ink
                property real p: 0
                x: (parent.width + width) * p - width
                NumberAnimation on p {
                    running: segment.visible && !Theme.reduceAnimations
                    from: 0; to: 1; duration: 1400; loops: Animation.Infinite
                }
            }
        }

        Text {
            width: block.w
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: screen._detail
            color: Theme.text2
            font.family: Theme.family
            font.pixelSize: Math.round(17 * screen._u)
        }

        Item {
            width: block.w
            height: version.implicitHeight
            visible: screen._barShown
            Text {
                id: version
                anchors.horizontalCenter: parent.horizontalCenter
                text: AppUpdate.installFrom + "  →  " + AppUpdate.installTo
                color: Theme.text3
                font.family: Theme.monoFamily
                font.pixelSize: Math.round(15 * screen._u)
            }
        }
    }

    // The prompts of the moment: only where there is still something to do.
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(28 * screen._u)
        spacing: Math.round(32 * screen._u)
        Repeater {
            model: screen.phase === AppUpdate.PhaseAsk ? [["A", qsTr("Continue")], ["B", qsTr("Cancel")]]
                 : screen.phase === AppUpdate.PhaseCheck ? [["B", qsTr("Cancel")]]
                 : screen.phase === AppUpdate.PhaseFailed || screen.phase === AppUpdate.PhaseCancelled ? [["A", qsTr("OK")]]
                 : []
            delegate: Row {
                spacing: Math.round(8 * screen._u)
                PadGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    buttonKey: modelData[0]
                    size: Math.round(26 * screen._u)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData[1]
                    color: Theme.text2
                    font.family: Theme.family
                    font.pixelSize: Math.round(16 * screen._u)
                }
            }
        }
    }

    // ---- the hand-over ---------------------------------------------------------------------
    function _pt(item, x, y) { return item.mapToItem(null, x, y) }
    function _rect(item) {
        const p = _pt(item, 0, 0)
        return [p.x, p.y, item.width, item.height]
    }
    function _baseline(item) { return _pt(item, 0, item.baselineOffset).y }

    function _look() {
        const xs = []
        for (let i = 0; i < letters.count; ++i) xs.push(_pt(letters.itemAt(i), 0, 0).x)
        const first = letters.itemAt(0)
        return {
            u: screen._u,
            t: floor.flowTime,
            waves: true,
            colors: {
                top: floor.topColor, mid: floor.midColor, foot: floor.footColor, ink: floor.waveColor,
                text: Theme.text, text2: Theme.text2, text3: Theme.text3, line: Theme.line
            },
            icon: _rect(icon),
            word: { px: word.fontPx, baseline: _baseline(first), x: xs },
            status: { px: status.font.pixelSize, x: _pt(status.parent, 0, 0).x, baseline: _baseline(status) },
            percent: { px: percent.font.pixelSize, right: _pt(percent.parent, percent.parent.width, 0).x,
                       baseline: _baseline(status) },
            bar: _rect(bar),
            version: { px: version.font.pixelSize, cx: _pt(version.parent, version.parent.width / 2, 0).x,
                       baseline: _baseline(version) }
        }
    }

    // Once the frame has stopped: one frame later, so the frozen waves and the empty bar are
    // what is on screen when it is measured.
    // phase, not _closing: a handler on phase may run before the bindings that read it.
    onPhaseChanged: if (phase === AppUpdate.PhaseClosing) handOver.restart()
    Timer {
        id: handOver
        interval: 50
        // ⚠️ screen.Window, not Window: on a Timer, which is no Item, the attached property is
        // null — and the first collaudo (07/11) handed over without a look.json for exactly that.
        onTriggered: AppUpdate.writeLook(screen.Window.window, screen._look())
    }
}
