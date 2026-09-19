import Theme 1.0
import SystemProperties 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// POWER chooser (6.2.0, StreamTweak issue #10). One row per machine — the host and this
// device — and each row picks what THAT machine does: Keep on, Sleep, Restart, Shut down.
// "Both" is no longer a target of its own: it is simply two rows that are not Keep on.
//
// Each row shows only the modes its machine really has. The host reports its own over the
// bridge (POWERCAPS, StreamTweak 8.6.0); this device reads them locally
// (SystemProperties.clientPowerModes). Nothing here knows which hardware is on either end —
// a host with Modern Standby, a machine without standby, an account without the right to
// shut down all come out of those two lists.
//
// ⚠️ No Hibernate (19/09/2026). StreamTweak 8.6.0 still lists "hibernate" for a host that
// has it; it simply has no key in _keys, so it is never drawn. A hibernating host woke by
// itself ~30 s later with this client silent — the wake came from elsewhere (§77).
//
// A host older than 8.6.0 cannot answer POWERCAPS, so its row offers Shut down only — the
// power-off it always understood — and the caller sends it the old SHUTDOWN.
//
// Standalone Popup, so the focus chain is ours for pad and keyboard:
//   host row ⇅ host updates ⇅ device row ⇅ device updates ⇅ Confirm ⇄ Cancel
// Dialog convention (§22): affirmative on the LEFT, accent reserved for focus, and the
// dismissive button (Cancel) focused first.
Popup {
    id: pop

    // Shared dialog measurements — see Theme.uiScale.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    // ── Public API ────────────────────────────────────────────────────────────
    property int    pcIndex: -1
    property string hostName: ""
    // StreamTweak access state of the host, mirrors HomeScreen's currentHost.auth.
    property string authState: "none"
    readonly property bool hostAllowed: authState === "authorized"

    // Client-only mode: no reachable/paired host (offline host, the "+ Add" tile, no hosts).
    // The host row is not drawn at all. Set by the caller before open().
    property bool clientOnly: false

    // What the host row can offer, set by HomeScreen:
    //   "checking" — POWERCAPS asked, not answered yet
    //   "ready"    — hostModes holds the host's own list
    //   "legacy"   — a host older than StreamTweak 8.6.0: Shut down only
    property string hostCaps: "checking"
    property var    hostModes: []
    // Whether a magic packet can bring the host back, for the warning under its row.
    property bool   hostWakeLan: true    // the host's NIC is armed for wake (POWERCAPS)
    property bool   hostWakeable: true   // we know its MAC
    property bool   hostAway: false      // reached through Tailscale: WOL does not get there

    // This device's modes; HomeScreen reads them on every open.
    property var    clientModes: []

    // Windows-update status of each side, set by HomeScreen when the dialog opens.
    //   host:   "checking" | "pending" | "none" | "unavailable"
    //   client: "pending" | "none"
    property string hostUpdateState: "unavailable"
    property string clientUpdateState: "none"

    // Emitted on confirm. A mode is one of _keys; "keep" means leave that machine alone.
    // The update flags are only ever true for restart/shutdown on a side with updates pending.
    signal confirmed(string hostMode, bool hostUpdates, string clientMode, bool clientUpdates)

    // ── Modes ─────────────────────────────────────────────────────────────────
    // One fixed list for both rows, so an index means the same mode everywhere; what a
    // machine lacks is hidden, not removed.
    readonly property var _keys:   ["keep", "sleep", "restart", "shutdown"]
    readonly property var _labels: [qsTr("Keep on"), qsTr("Sleep"), qsTr("Restart"), qsTr("Shut down")]

    function _hiddenFor(modes) {
        var hidden = []
        for (var i = 1; i < _keys.length; ++i)
            if (modes.indexOf(_keys[i]) < 0) hidden.push(i)
        return hidden
    }

    readonly property bool _hostRow: !clientOnly
    readonly property var  _hostOffer: hostCaps === "legacy" ? ["shutdown"]
                                     : hostCaps === "ready"  ? hostModes : []
    readonly property bool _hostSelectable: _hostRow && hostAllowed && hostCaps !== "checking"
                                            && _hostOffer.length > 0

    readonly property string hostMode:   _hostSelectable ? _keys[hostSel.currentIndex] : "keep"
    readonly property string clientMode: clientModes.length > 0 ? _keys[clientSel.currentIndex] : "keep"

    function _canUpdate(mode) { return mode === "restart" || mode === "shutdown" }
    readonly property bool _hostUpdOffered:   pop._canUpdate(hostMode)   && hostUpdateState === "pending"
    readonly property bool _clientUpdOffered: pop._canUpdate(clientMode) && clientUpdateState === "pending"

    readonly property bool _nothingToDo: hostMode === "keep" && clientMode === "keep"

    // Under the host row: only when the choice would leave it where we cannot reach it.
    readonly property string _hostWarning: {
        if (hostMode === "keep" || hostMode === "restart") return ""
        if (hostAway) return qsTr("Away from home: can’t wake it")
        if (hostMode === "sleep" && (!hostWakeLan || !hostWakeable))
            return qsTr("Can’t be woken remotely")
        return ""
    }

    function _phrase(name, mode, updates) {
        var verb = mode === "sleep"     ? qsTr("sleeps")
                 : mode === "restart"   ? (updates ? qsTr("updates and restarts") : qsTr("restarts"))
                 :                        (updates ? qsTr("updates and shuts down") : qsTr("shuts down"))
        return "<b>" + name + "</b> " + verb
    }
    readonly property string _clientLabel: {
        var n = SystemProperties.clientName()
        return n.length > 0 ? n : qsTr("This device")
    }
    readonly property string _summary: {
        var parts = []
        if (hostMode !== "keep")
            parts.push(_phrase(hostName.length > 0 ? hostName : qsTr("Host"), hostMode, _hostUpdOffered && hostUpd.checked))
        if (clientMode !== "keep")
            parts.push(_phrase(_clientLabel, clientMode, _clientUpdOffered && clientUpd.checked))
        return parts.length > 0 ? parts.join(qsTr(" · then ")) : qsTr("Nothing to do")
    }

    function _commit() {
        if (pop._nothingToDo)
            return
        pop.confirmed(pop.hostMode,   pop._hostUpdOffered && hostUpd.checked,
                      pop.clientMode, pop._clientUpdOffered && clientUpd.checked)
        pop.close()
    }

    // ── Focus chain ───────────────────────────────────────────────────────────
    function _chain() {
        var c = []
        if (hostSel.visible)   c.push(hostSel)
        if (hostUpd.visible)   c.push(hostUpd)
        if (clientSel.visible) c.push(clientSel)
        if (clientUpd.visible) c.push(clientUpd)
        c.push(cancelBtn)
        return c
    }
    function _step(from, delta) {
        var c = _chain()
        var i = c.indexOf(from === confirmBtn ? cancelBtn : from)
        var next = c[Math.max(0, Math.min(c.length - 1, i + delta))]
        next.forceActiveFocus()
    }

    function _updateChip(state) {
        switch (state) {
        case "pending":  return qsTr("Updates pending")
        case "none":     return qsTr("Up to date")
        case "checking": return qsTr("Checking…")
        }
        return ""
    }

    modal: true
    Overlay.modal: Rectangle { color: "#cc000000" }
    focus: true
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    padding: pop._px(32)

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: pop._px(12)
    }

    // One machine's heading: role caption, name, update status.
    component RowHead: RowLayout {
        id: head
        property string role: ""
        property string name: ""
        property string updates: ""
        Layout.alignment: Qt.AlignHCenter
        spacing: pop._px(10)
        Label {
            text: head.role
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontCaption)
            font.bold: true; font.letterSpacing: 1.4
            color: Theme.text3
            Layout.alignment: Qt.AlignBaseline
        }
        Label {
            text: head.name
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontBody); font.bold: true
            color: Theme.text
            elide: Text.ElideRight
            Layout.maximumWidth: pop._px(300)
            Layout.alignment: Qt.AlignBaseline
        }
        // Windows mark + status, so "up to date" reads as the OS and not as StreamLight.
        WinMark {
            visible: head.updates.length > 0 && head.updates !== "unavailable"
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: pop._px(4)
        }
        Label {
            visible: text.length > 0
            text: pop._updateChip(head.updates)
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall)
            color: head.updates === "pending" ? Theme.warning : Theme.text2
            Layout.alignment: Qt.AlignBaseline
        }
    }

    // The Windows mark, drawn: four squares, lighter at the top left and deeper towards the
    // bottom right (Grid fills row by row: TL, TR, BL, BR). Stands for "Windows" next to
    // update status, where the word took more room than the fact.
    component WinMark: Grid {
        columns: 2
        spacing: Math.max(1, pop._px(1.5))
        Repeater {
            model: ["#6CD2FE", "#4ACFFF", "#38C0FF", "#20AEFF"]
            Rectangle {
                required property string modelData
                width: pop._px(6); height: pop._px(6)
                color: modelData
            }
        }
    }

    // "Install updates" with its Off/On pills, under a row that restarts or shuts down.
    // ⚠️ OnOffSelector never writes `checked` itself (it is meant to be bound to a setting);
    // here the pills ARE the state, so onToggled stores it.
    component UpdateRow: RowLayout {
        property alias selector: pills
        Layout.alignment: Qt.AlignHCenter
        spacing: pop._px(12)
        WinMark { Layout.alignment: Qt.AlignVCenter; Layout.rightMargin: -pop._px(4) }
        Label {
            text: qsTr("Install updates")
            font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall)
            color: Theme.text2
            Layout.alignment: Qt.AlignVCenter
        }
        OnOffSelector {
            id: pills
            onToggled: function(value) { pills.checked = value }
            Keys.onUpPressed:   function(event) { pop._step(pills, -1); event.accepted = true }
            Keys.onDownPressed: function(event) { pop._step(pills,  1); event.accepted = true }
        }
    }

    contentItem: ColumnLayout {
        spacing: pop._px(18)

        Label {
            text: qsTr("POWER")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: 1.6
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        // ── Host ──────────────────────────────────────────────────────────────
        ColumnLayout {
            visible: pop._hostRow
            Layout.fillWidth: true
            Layout.preferredWidth: pop._px(560)
            spacing: pop._px(8)

            RowHead {
                role: qsTr("HOST")
                name: pop.hostName
                updates: pop.hostAllowed ? pop.hostUpdateState : ""
            }

            SegmentedSelector {
                id: hostSel
                visible: pop._hostSelectable
                labels: pop._labels
                Layout.alignment: Qt.AlignHCenter
                hiddenIndices: pop._hiddenFor(pop._hostOffer)
                Keys.onUpPressed:   function(event) { pop._step(hostSel, -1); event.accepted = true }
                Keys.onDownPressed: function(event) { pop._step(hostSel,  1); event.accepted = true }
            }

            // In place of the selector when there is nothing to choose from.
            Label {
                visible: pop._hostRow && !pop._hostSelectable
                text: !pop.hostAllowed          ? qsTr("Needs StreamTweak access")
                    : pop.hostCaps === "checking" ? qsTr("Checking…")
                    :                               qsTr("Not available")
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall)
                color: Theme.text3
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                visible: pop._hostWarning.length > 0
                text: "▲  " + pop._hostWarning
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall)
                color: Theme.warning
                Layout.alignment: Qt.AlignHCenter
            }

            UpdateRow {
                id: hostUpdRow
                visible: pop._hostUpdOffered
            }
        }

        // ── This device ───────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: pop._px(560)
            spacing: pop._px(8)

            RowHead {
                role: qsTr("THIS DEVICE")
                name: pop._clientLabel
                updates: pop.clientUpdateState
            }

            SegmentedSelector {
                id: clientSel
                visible: pop.clientModes.length > 0
                labels: pop._labels
                Layout.alignment: Qt.AlignHCenter
                hiddenIndices: pop._hiddenFor(pop.clientModes)
                Keys.onUpPressed:   function(event) { pop._step(clientSel, -1); event.accepted = true }
                Keys.onDownPressed: function(event) { pop._step(clientSel,  1); event.accepted = true }
            }

            Label {
                visible: pop.clientModes.length === 0
                text: qsTr("Not available")
                font.family: Theme.family; font.pixelSize: pop._px(Theme.fontSmall)
                color: Theme.text3
                Layout.alignment: Qt.AlignHCenter
            }

            UpdateRow {
                id: clientUpdRow
                visible: pop._clientUpdOffered
            }
        }

        Label {
            text: pop._summary
            textFormat: Text.StyledText
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontTitle)
            color: pop._nothingToDo ? Theme.text3 : Theme.text
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Layout.preferredWidth: pop._px(560)
            Layout.topMargin: pop._px(4)
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: pop._px(4)
            spacing: pop._px(14)

            Button {
                id: confirmBtn
                text: qsTr("Confirm")
                enabled: !pop._nothingToDo
                activeFocusOnTab: true
                onClicked: pop._commit()
                Keys.onReturnPressed: pop._commit()
                Keys.onEnterPressed:  pop._commit()
                Keys.onSpacePressed:  pop._commit()
                Keys.onRightPressed:  cancelBtn.forceActiveFocus()
                Keys.onUpPressed:     pop._step(confirmBtn, -1)

                background: Rectangle {
                    implicitWidth: pop._px(140)
                    implicitHeight: pop._px(42)
                    radius: pop._px(8)
                    opacity: confirmBtn.enabled ? 1.0 : 0.4
                    color: confirmBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : confirmBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                          Theme.card
                    border.color: confirmBtn.activeFocus ? Theme.accent
                                : confirmBtn.hovered     ? Theme.lineHigh
                                :                          Theme.line
                    border.width: confirmBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: confirmBtn.text
                    color: Theme.accent
                    opacity: confirmBtn.enabled ? 1.0 : 0.4
                    font.family: Theme.family
                    font.pixelSize: pop._px(Theme.fontBody)
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
                Keys.onLeftPressed:   if (confirmBtn.enabled) confirmBtn.forceActiveFocus()
                Keys.onUpPressed:     pop._step(cancelBtn, -1)

                background: Rectangle {
                    implicitWidth: pop._px(140)
                    implicitHeight: pop._px(42)
                    radius: pop._px(8)
                    color: cancelBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : cancelBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         Theme.card
                    border.color: cancelBtn.activeFocus ? Theme.accent
                                : cancelBtn.hovered     ? Theme.lineHigh
                                :                         Theme.line
                    border.width: cancelBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: cancelBtn.text
                    color: Theme.text
                    font.family: Theme.family
                    font.pixelSize: pop._px(Theme.fontBody)
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // The two update selectors, named for the focus chain and _commit(). Read out of the
    // UpdateRow instances, so the rows stay one component.
    readonly property var hostUpd:   hostUpdRow.selector
    readonly property var clientUpd: clientUpdRow.selector

    // Every open starts from the same safe state: the host kept on, this device shut down
    // (the old default target, Client), no updates, focus on Cancel (§22).
    onOpened: {
        hostSel.currentIndex = 0
        var off = pop._keys.indexOf("shutdown")
        clientSel.currentIndex = pop.clientModes.indexOf("shutdown") >= 0 ? off : 0
        hostUpd.checked = false
        clientUpd.checked = false
        cancelBtn.forceActiveFocus()
    }
    onClosed: {
        if (typeof stackView !== "undefined" && stackView)
            stackView.forceActiveFocus()
    }
}
