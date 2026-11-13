import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.15

import SdlGamepadKeyNavigation 1.0
import InputHints 1.0

/*
 * Per-game settings (StreamLight 4.0.0; redone in 8.8.0 with the structure of Host profiles —
 * docs/mockups/profili/ProfilesAMockup.qml).
 *
 * Almost the whole window. On the left the game — its cover, its name, what its rows inherit
 * from — and the two things that act on all of it, Reset all and Reset stats, walked with the
 * D-pad. On the right the same sheet as Host profiles (OverrideSheet): one list in sections, one
 * badge per row that A opens. The rows inherit from the host's active profile when there is one,
 * else from Global (AppModel.inheritedValues), and choosing the inherited value gives the row back.
 *
 * ⚠️ One key handler for the whole dialog (`root`), as in HostProfilesDialog — see the note there.
 */
Popup {
    id: dlg

    enter: PopEnter {}
    exit: PopExit {}

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }
    function _fpx(n) { return Math.round(n * _u * 0.88) }

    property var appModel: null
    property int appIndex: -1
    property string appName: ""
    // The game's box art. Empty for an app without one: the column then shows the settings glyph.
    property string cover: ""
    // The host's active profile, empty when none: what the rows inherit from is named after it.
    property string activeProfileName: ""

    // Emitted when the dialog closes, so the opener can restore gamepad focus to the grid.
    signal closedByUser()

    readonly property string _inheritLabel: activeProfileName.length > 0 ? activeProfileName : qsTr("Global")

    property var _base: ({})
    property var _own: ({})
    // This game's play record: an empty map when there is nothing to clear (never streamed, or
    // an entry that is never tracked) — the Reset stats row greys itself on exactly that.
    property var _playtime: ({})
    readonly property bool _hasPlaytime: _playtime.total !== undefined

    readonly property var _sections: [
        { title: qsTr("Stream"),      keys: ["resolution", "fps", "codec", "hdr", "yuv444", "bitrate"] },
        { title: qsTr("Display"),     keys: ["framepacing"] },
        { title: qsTr("Audio"),       keys: ["audio"] },
        { title: qsTr("StreamTweak"), keys: ["waitgame"] }
    ]

    // ── The cursor: the sheet first; on the left 0 Reset all · 1 Reset stats ──
    property string zone: "sheet"
    property int sideIdx: 0
    readonly property bool _sideOn: sideIdx === 0 ? sheet.changes > 0 : _hasPlaytime

    function reload() {
        if (!appModel || appIndex < 0) return
        _base = appModel.inheritedValues()
        _own = appModel.getAppOverride(appIndex)
        _playtime = appModel.playtimeFor(appIndex)
    }

    function applyChange(key, v) {
        var m = {}
        for (var k in _own) m[k] = _own[k]
        if (key === "resolution") {
            if (v === undefined) { delete m.width; delete m.height }
            else { m.width = v.w; m.height = v.h }
        } else if (v === undefined) delete m[key]
        else m[key] = v
        appModel.setAppOverride(appIndex, m)
        reload()
    }
    function resetAll() {
        if (appModel && appIndex >= 0) appModel.clearAppOverride(appIndex)
        reload()
    }
    function resetStats() {
        if (appModel && appIndex >= 0) appModel.resetPlaytime(appIndex)
        // Re-read rather than assume: this is what greys the row out.
        _playtime = appModel ? appModel.playtimeFor(appIndex) : ({})
    }
    function activateSide() {
        if (!_sideOn) return
        if (sideIdx === 0) resetConfirm.open()
        else statsConfirm.open()
    }

    modal: true
    dim: true
    focus: true
    // B is the dialog's own key; Qt would take Esc as a shortcut before the dialog saw it.
    closePolicy: sheet.editing ? Popup.NoAutoClose : Popup.CloseOnPressOutside
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent ? parent.width - _px(64) : _px(1200)
    height: parent ? parent.height - _px(56) : _px(800)
    padding: 0

    Overlay.modal: Rectangle { color: Theme.scrim }
    background: Rectangle {
        color: Theme.card
        radius: dlg._px(14)
        border.color: Theme.line
        border.width: 1
    }

    onAboutToShow: {
        reload()
        zone = "sheet"
        sideIdx = 0
        sheet.idx = 0
        sheet.editing = false
        sheet.toTop()
    }
    onOpened: root.forceActiveFocus()
    onClosed: dlg.closedByUser()

    component Prompt: Item {
        id: prompt
        property string buttonKey: ""
        property string keyLabel: ""
        property string action: ""
        property bool clickable: false
        signal clicked()
        implicitWidth: pr.implicitWidth
        // As tall as the key chip even when there is none ("◀▶ Choose"): the footer Row lines its
        // items up by the top, so a prompt of text alone sat higher than its neighbours.
        implicitHeight: Math.max(pr.implicitHeight, dlg._px(26))
        Row {
            id: pr
            anchors.verticalCenter: parent.verticalCenter
            spacing: dlg._px(9)
            ActionHint {
                visible: prompt.buttonKey.length > 0 || prompt.keyLabel.length > 0
                anchors.verticalCenter: parent.verticalCenter
                buttonKey: prompt.buttonKey
                keyLabel: prompt.keyLabel
                size: dlg._px(26)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: prompt.action
                color: Theme.text
                font.family: Theme.family
                font.pixelSize: dlg._px(16)
            }
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -dlg._px(6)
            enabled: prompt.clickable
            cursorShape: Qt.PointingHandCursor
            onClicked: prompt.clicked()
        }
    }

    // A row of the left column that acts on the whole game.
    component SideAction: Item {
        id: sa
        property int index: 0
        property string label: ""
        property string hint: ""
        property bool danger: false
        property bool on: true
        width: parent ? parent.width : 0
        height: dlg._px(58)
        opacity: on ? 1 : 0.4
        Column {
            x: dlg._px(16); anchors.verticalCenter: parent.verticalCenter; spacing: dlg._px(2)
            Text { text: sa.label; color: sa.danger ? Theme.danger : Theme.text
                   font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontBody); font.bold: true }
            Text { text: sa.hint; color: Theme.text3
                   font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontSmall) }
        }
        MouseArea {
            anchors.fill: parent
            enabled: sa.on
            cursorShape: Qt.PointingHandCursor
            onClicked: { dlg.zone = "side"; dlg.sideIdx = sa.index; dlg.activateSide() }
        }
    }

    contentItem: Item {
        id: root
        focus: true

        Keys.onPressed: function(e) {
            var k = e.key
            var A = k === Qt.Key_Return || k === Qt.Key_Enter || k === Qt.Key_Space
            var B = k === Qt.Key_Escape
            var up = k === Qt.Key_Up || k === Qt.Key_Backtab || (k === Qt.Key_Tab && (e.modifiers & Qt.ShiftModifier))
            var down = !up && (k === Qt.Key_Down || k === Qt.Key_Tab)
            // Everything is taken: a Y that got through would reach main.qml, which opens Settings.
            e.accepted = true

            if (dlg.zone === "sheet") {
                if (sheet.handleKey(e)) return
                if (k === Qt.Key_Left) dlg.zone = "side"
                else if ((k === Qt.Key_Menu || k === Qt.Key_X) && sheet.changes > 0) resetConfirm.open()
                else if (B) dlg.close()
                return
            }
            if (up) dlg.sideIdx = 0
            else if (down) dlg.sideIdx = 1
            else if (k === Qt.Key_Right) dlg.zone = "sheet"
            else if (A) dlg.activateSide()
            else if (B) dlg.close()
        }

        // ── Header ──
        Item {
            id: head
            width: parent.width; height: dlg._px(58)
            Row {
                x: dlg._px(28); anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(12)
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    source: Theme.light ? "qrc:/res/tune_dark.svg" : "qrc:/res/tune.svg"
                    sourceSize.width: 22; sourceSize.height: 22
                    width: dlg._px(22); height: dlg._px(22)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Per-game settings")
                    color: Theme.text
                    font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontTitle); font.bold: true
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
        }

        // ── The game ──
        Item {
            id: side
            y: head.height
            width: dlg._px(340); height: parent.height - y - foot.height
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }

            Column {
                id: gameCol
                x: dlg._px(28); y: dlg._px(24)
                width: parent.width - dlg._px(56)
                spacing: dlg._px(14)

                HeroCover {
                    visible: dlg.cover !== ""
                    width: dlg._px(140); height: Math.round(width * 3 / 2)
                    source: dlg.cover
                    radius: dlg._px(8)
                    shadow: false
                }
                Column {
                    width: parent.width
                    spacing: dlg._px(4)
                    Text {
                        width: parent.width
                        text: dlg.appName
                        color: Theme.text
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontH2); font.bold: true
                    }
                    Text {
                        width: parent.width; elide: Text.ElideRight
                        text: qsTr("Inherits from %1").arg(dlg._inheritLabel)
                        color: Theme.text2
                        font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontSmall)
                    }
                    Text {
                        readonly property int n: sheet.changes
                        text: n === 0 ? qsTr("No changes") : n === 1 ? qsTr("1 change") : qsTr("%1 changes").arg(n)
                        color: n > 0 ? Theme.inkText : Theme.text3
                        font.family: Theme.family; font.pixelSize: dlg._fpx(Theme.fontSmall)
                    }
                }
            }

            Item {
                id: actions
                x: dlg._px(10)
                anchors.bottom: parent.bottom; anchors.bottomMargin: dlg._px(16)
                width: parent.width - dlg._px(20); height: dlg._px(58) * 2

                Rectangle {
                    x: dlg._px(2); width: parent.width - dlg._px(4)
                    y: dlg.sideIdx * dlg._px(58); height: dlg._px(58)
                    radius: dlg._px(6)
                    readonly property bool here: dlg.zone === "side"
                    color: here ? Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.22) : "transparent"
                    border.width: here ? dlg._px(2) : 0
                    border.color: Theme.accentFill
                    visible: here
                    Behavior on y { enabled: !Theme.reduceAnimations; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                }
                Column {
                    width: parent.width
                    SideAction { index: 0; label: qsTr("Reset all"); hint: qsTr("Every row back to %1").arg(dlg._inheritLabel)
                                 on: sheet.changes > 0 }
                    SideAction { index: 1; label: qsTr("Reset stats"); hint: qsTr("Play time and sessions"); danger: true
                                 on: dlg._hasPlaytime }
                }
            }
        }

        // ── The sheet ──
        OverrideSheet {
            id: sheet
            x: side.width + dlg._px(20); y: head.height + dlg._px(12)
            width: parent.width - side.width - dlg._px(40)
            height: parent.height - y - foot.height
            sections: dlg._sections
            base: dlg._base
            own: dlg._own
            baseName: dlg._inheritLabel
            active: dlg.zone === "sheet"
            onChange: function(key, value) { dlg.applyChange(key, value) }
            onCustomRequested: function(key) {
                if (key === "resolution") {
                    var r = sheet.eff("resolution")
                    resCustomDialog.initWidth = r ? r.w : 1920
                    resCustomDialog.initHeight = r ? r.h : 1080
                    resCustomDialog.open()
                } else {
                    customFpsDialog.initFps = sheet.eff("fps") || 60
                    customFpsDialog.open()
                }
            }
            onPointerEntered: dlg.zone = "sheet"
        }

        // ── Footer ──
        Item {
            id: foot
            anchors.bottom: parent.bottom
            width: parent.width; height: dlg._px(60)
            Rectangle { width: parent.width; height: 1; color: Theme.line }
            Row {
                id: prompts
                x: dlg._px(24); anchors.verticalCenter: parent.verticalCenter
                spacing: dlg._px(20)
                readonly property bool idleSheet: dlg.zone === "sheet" && !sheet.editing

                Prompt { visible: dlg.zone === "side" && dlg._sideOn; buttonKey: "A"; keyLabel: "Enter"
                         action: dlg.sideIdx === 0 ? qsTr("Reset all") : qsTr("Reset stats") }

                Prompt { visible: prompts.idleSheet && sheet.currentEnabled; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Change") }
                Prompt { visible: prompts.idleSheet && sheet.currentOver; buttonKey: "Y"; keyLabel: "Y"
                         action: qsTr("Use %1").arg(dlg._inheritLabel)
                         clickable: true; onClicked: dlg.applyChange(sheet.currentKey, undefined) }
                Prompt { visible: prompts.idleSheet && sheet.changes > 0; buttonKey: "X"; keyLabel: "X"; action: qsTr("Reset all")
                         clickable: true; onClicked: resetConfirm.open() }
                Prompt { visible: dlg.zone === "sheet" && SdlGamepadKeyNavigation.scrollAvailable && InputHints.padActive
                         buttonKey: "RSCROLL"; action: qsTr("Scroll") }

                Prompt { visible: sheet.editing; action: "◀▶  " + (sheet.currentIsSlider ? qsTr("Adjust") : qsTr("Choose")) }
                Prompt { visible: sheet.editing; buttonKey: "A"; keyLabel: "Enter"; action: qsTr("Confirm")
                         clickable: true; onClicked: sheet.commitEdit() }

                Prompt {
                    buttonKey: "B"; keyLabel: "Esc"
                    action: sheet.editing ? qsTr("Cancel") : qsTr("Close")
                    clickable: true
                    onClicked: { if (sheet.editing) sheet.cancelEdit(); else dlg.close() }
                }
            }
            DialogButton {
                anchors.right: parent.right; anchors.rightMargin: dlg._px(24)
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Done")
                affirmative: true
                fontSize: 13
                focusPolicy: Qt.NoFocus
                width: dlg._px(96); height: dlg._px(38)
                onActivated: dlg.close()
            }
        }
    }

    CustomResolutionDialog {
        id: resCustomDialog
        onAccepted: function(w, h) { sheet._set("resolution", { w: w, h: h }) }
        onClosed: if (dlg.opened) root.forceActiveFocus()
    }
    CustomFrameRateDialog {
        id: customFpsDialog
        onAccepted: function(fps) { sheet._set("fps", fps) }
        onClosed: if (dlg.opened) root.forceActiveFocus()
    }
    NavigableMessageDialog {
        id: resetConfirm
        parent: Overlay.overlay
        headerText: qsTr("RESET ALL")
        text: qsTr("Give every setting of this game back to %1?").arg(dlg._inheritLabel)
        standardButtons: Dialog.Yes | Dialog.No
        restoreFocus: false
        onAccepted: dlg.resetAll()
        onClosed: if (dlg.opened) root.forceActiveFocus()
    }
    NavigableMessageDialog {
        id: statsConfirm
        parent: Overlay.overlay
        headerText: qsTr("RESET STATS")
        text: qsTr("Clear the play time and sessions of %1?").arg(dlg.appName)
        standardButtons: Dialog.Yes | Dialog.No
        restoreFocus: false
        onAccepted: dlg.resetStats()
        onClosed: if (dlg.opened) root.forceActiveFocus()
    }
}
