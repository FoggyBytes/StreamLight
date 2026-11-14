import QtQuick 2.15
import QtQuick.Controls 2.15

import Theme 1.0
import StreamingPreferences 1.0
import SdlGamepadKeyNavigation 1.0

/*
 * The profile list (8.7.1, issue #53) — variant A «Tendina» of docs/mockups/profili.
 *
 * With ten profiles and more, stepping through them one at a time with LT/RT (Q/E) skipped
 * past the one being looked for. This drops from the profile badge on the Home card and lists
 * Global and every profile with what it changes, in the words of the badges under the host's
 * name, which is what tells one dock from another — in the order set in Profiles (8.8.0).
 *
 * Opened by Start / W and by a click on the badge, on Home only. ⚠️ 8.7.1 also opened it by
 * holding LT/RT, on Home and on the host page; 8.8.0 dropped both (Marcello, 13/11/2026). The
 * D-pad (↑↓) moves, A chooses, B closes without changing anything; LT/RT do nothing while it
 * is open.
 *
 * Built like Options (HostOptionsDialog): a Popup on the overlay with the dialogs' card, line,
 * radius and scrim, the shared spring in and out. Its own file because HomeScreen imports
 * Controls 2.2, which has no Overlay.
 *
 * ⚠️ The host is held by uuid, never by row: the list stays open for as long as the user
 * likes, and a host renamed, found or removed meanwhile moves every row (§80.2).
 */
Popup {
    id: pop

    parent: Overlay.overlay
    modal: true
    focus: true
    Overlay.modal: Rectangle { color: Theme.scrim }
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Grows out of the badge: from its top edge when it opens below it, from its bottom when
    // there was no room below and it opens above.
    transformOrigin: _above ? Popup.BottomLeft : Popup.TopLeft
    enter: PopEnter { fromScale: 0.92 }
    exit: PopExit {}

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    property var    computerModel: null
    property string hostUuid: ""
    property string hostName: ""
    // What it drops from: the profile badge. Placed under it, or over it near the bottom.
    property Item   anchorItem: null

    // Emitted after the choice has been applied, with the slot (-1 = Global).
    signal chosen(int slot)

    property var  _entries: []
    property int  _activeSlot: -1          // read once per opening, in _load()
    property bool _above: false

    readonly property int _rowH:    _px(50)
    readonly property int _rows:    Math.min(7, Math.max(1, _entries.length))
    readonly property int _headH:   _px(48)
    readonly property int _listH:   _rows * _rowH + _px(8)
    readonly property int _footH:   _px(54)

    // 8.8.3 (#54): as wide as its longest row. It was 480 fixed, and the name gave way to the
    // settings beside it — with a resolution that has no short name ("3440×1440" against "4K")
    // a name well inside the limit was already cut. The name is never cut now: the list grows to
    // hold name and settings in full, and only at the window's edge do the settings give way.
    // Measured in _load(), once per opening, in the heavier weight the cursor's row takes.
    FontMetrics {
        id: nameMetrics
        font.family: Theme.family
        font.pixelSize: pop._px(Theme.fontBody + 1)
        font.weight: Font.DemiBold
    }
    FontMetrics {
        id: sumMetrics
        font.family: Theme.family
        font.pixelSize: pop._px(Theme.fontSmall + 1)
    }
    property real _rowTextW: 0
    readonly property int _nameX:  _px(30)       // the name, after the dot of the profile in use
    readonly property int _gap:    _px(28)       // between the name and its settings
    readonly property int _sumPad: _px(16)       // from the settings to the row's edge
    // Around the two texts: the list's margins (10 + 10), the room kept for the scroll bar (10)
    // and the row's own inset (3 + 3).
    readonly property int _chrome: _nameX + _gap + _sumPad + _px(36)

    padding: 0
    width: Math.max(_px(480),
                    Math.min(Math.ceil(_rowTextW) + _chrome + _px(4),
                             parent ? parent.width - 2 * _px(16) : _px(480)))
    height: _headH + _listH + _footH

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: pop._px(12)
    }

    function _index() {
        return computerModel ? computerModel.indexOfUuid(hostUuid) : -1
    }

    // The same words as the badges under the host's name (HostStage, the host page's
    // configuration line): the global preferences with the profile's own values on top.
    // Audio is left out — it is the same on almost every profile and the row is short.
    function _summary(ov) {
        function pick(k, d) { return (ov && ov[k] !== undefined) ? ov[k] : d }
        var w = pick("width", StreamingPreferences.width)
        var h = pick("height", StreamingPreferences.height)
        var res = (w === 3840 && h === 2160) ? "4K"
                : (w === 2560 && h === 1440) ? "1440p"
                : (w === 1920 && h === 1080) ? "1080p"
                : (w === 1280 && h === 720)  ? "720p"
                : w + "×" + h
        var parts = [res,
                     pick("fps", StreamingPreferences.fps) + " FPS",
                     (pick("bitrate", StreamingPreferences.bitrateKbps) / 1000).toFixed(0) + " Mbps"]
        if (pick("hdr", StreamingPreferences.enableHdr))
            parts.push("HDR")
        var c = pick("codec", StreamingPreferences.videoCodecConfig)
        if (c === 1) parts.push("H.264")
        else if (c === 2) parts.push("HEVC")
        else if (c === 4) parts.push("AV1")
        else if (c === 5) parts.push("PyroWave")
        return parts.join(" · ")
    }

    function _load() {
        var idx = _index()
        if (idx < 0) { _entries = []; return }
        // 8.8.0: in the order chosen with Sort in Profiles (A–Z, Created or Custom).
        var order = computerModel.hostProfileOrder(idx)
        var e = [{ slot: -1, name: qsTr("Global"), sum: _summary({}) }]
        for (var i = 0; i < order.length; i++) {
            var s = order[i]
            e.push({ slot: s, name: computerModel.hostProfileName(idx, s),
                     sum: _summary(computerModel.hostProfileSettings(idx, s)) })
        }
        _activeSlot = computerModel.hostActiveProfile(idx)
        var widest = 0
        for (var k = 0; k < e.length; k++)
            widest = Math.max(widest, nameMetrics.advanceWidth(e[k].name) + sumMetrics.advanceWidth(e[k].sum))
        _rowTextW = widest
        _entries = e
        var at = 0
        for (var j = 0; j < e.length; j++) if (e[j].slot === _activeSlot) at = j
        list.currentIndex = at
    }

    function _place() {
        if (!anchorItem || !parent) return
        var p = anchorItem.mapToItem(parent, 0, 0)
        var gap = _px(10), margin = _px(16)
        var below = p.y + anchorItem.height + gap
        _above = below + height > parent.height - margin
        y = _above ? Math.max(margin, p.y - height - gap) : below
        x = Math.max(margin, Math.min(p.x - _px(12), parent.width - width - margin))
    }

    function _choose(i) {
        if (i < 0 || i >= _entries.length) return
        var idx = _index()
        if (idx >= 0) {
            computerModel.setHostActiveProfile(idx, _entries[i].slot)
            chosen(_entries[i].slot)
        }
        close()
    }

    // The focus goes back where it was, as after every NavigableDialog — in onClosed, a beat
    // later, after Qt's own hand-over at the end of the exit.
    property Item _focusBefore: null
    onAboutToShow: {
        _focusBefore = (typeof window !== "undefined" && window) ? window.activeFocusItem : null
        _load()
        _place()
    }
    onOpened: {
        list.positionViewAtIndex(list.currentIndex, ListView.Center)
        list.forceActiveFocus()
    }
    onClosed: {
        var f = _focusBefore
        _focusBefore = null
        Qt.callLater(function() {
            if (f && f.visible && f.enabled) f.forceActiveFocus()
            else if (typeof stackView !== "undefined" && stackView) stackView.forceActiveFocus()
        })
    }

    contentItem: Item {
        // ── Header: whose profiles, and how many ──
        Label {
            x: pop._px(22)
            y: Math.round((pop._headH - height) / 2) + pop._px(2)
            width: parent.width - x - countLabel.width - pop._px(30)
            elide: Text.ElideRight
            text: (qsTr("Profile") + (pop.hostName.length ? "  ·  " + pop.hostName : "")).toUpperCase()
            color: Theme.text3
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: 1.6
        }
        Label {
            id: countLabel
            anchors.right: parent.right
            anchors.rightMargin: pop._px(22)
            y: Math.round((pop._headH - height) / 2) + pop._px(2)
            // Plural by hand: no translation is loaded (§86).
            text: (pop._entries.length - 1) === 1 ? qsTr("1 profile")
                                                  : qsTr("%1 profiles").arg(Math.max(0, pop._entries.length - 1))
            color: Theme.text3
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
        }

        // ── The list ──
        ListView {
            id: list
            x: pop._px(10)
            y: pop._headH
            width: parent.width - 2 * x
            height: pop._listH - pop._px(8)
            clip: true
            model: pop._entries
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            keyNavigationWraps: false
            highlightFollowsCurrentItem: false
            // Keeps the cursor in view as it moves (the highlight does not, it is drawn by the row).
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            Keys.onPressed: function(event) {
                // The list moves with the D-pad (↑↓) only (12/11/2026, Marcello). LT/RT and Q/E
                // do nothing while it is open — taken here, or they would reach the page behind
                // and step the profile under the list.
                if (event.key === Qt.Key_F14 || event.key === Qt.Key_Q
                        || event.key === Qt.Key_F15 || event.key === Qt.Key_E) {
                    event.accepted = true
                } else if (event.key === Qt.Key_F18 || event.key === Qt.Key_W) {
                    // Start / W opened it on Home: the same key puts it away.
                    pop.close(); event.accepted = true
                }
            }
            Keys.onReturnPressed: function(event) { pop._choose(currentIndex); event.accepted = true }
            Keys.onEnterPressed:  function(event) { pop._choose(currentIndex); event.accepted = true }
            Keys.onSpacePressed:  function(event) { pop._choose(currentIndex); event.accepted = true }

            ScrollBar.vertical: ThemedScrollBar {}

            delegate: Item {
                id: row
                width: list.width - pop._px(10)
                height: pop._rowH

                readonly property bool _cursor: ListView.isCurrentItem
                readonly property bool _active: pop._activeSlot === modelData.slot

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: pop._px(3)
                    radius: pop._px(8)
                    // Glass, as every button since 8.5.0 (variant A of docs/mockups/pulsanti):
                    // the cursor tinted 22% under a 2 px border, the profile in use 14% under a
                    // hairline at 55%, both in the accent's fill colour.
                    color: row._cursor ? Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.22)
                         : row._active ? Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.14)
                         : rowMouse.containsMouse ? Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, 0.06)
                         : "transparent"
                    border.width: row._cursor ? 2 : row._active ? 1 : 0
                    border.color: row._cursor ? Theme.accentFill
                                              : Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.55)
                    Behavior on color {
                        enabled: !Theme.reduceAnimations
                        ColorAnimation { duration: 120 }
                    }

                    // The profile in use: a dot in the accent, as a mark.
                    Rectangle {
                        x: pop._px(14)
                        anchors.verticalCenter: parent.verticalCenter
                        width: pop._px(8); height: width; radius: width / 2
                        color: Theme.ink
                        visible: row._active
                    }
                    Label {
                        id: nameLabel
                        x: pop._nameX
                        anchors.verticalCenter: parent.verticalCenter
                        // In full (8.8.3): only a window narrower than the name itself cuts it.
                        width: Math.min(implicitWidth, parent.width - x - pop._sumPad)
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: modelData.name
                        color: Theme.text
                        font.family: Theme.family
                        font.pixelSize: pop._px(Theme.fontBody + 1)
                        font.weight: (row._cursor || row._active) ? Font.DemiBold : Font.Normal
                    }
                    Label {
                        id: sumLabel
                        anchors.right: parent.right
                        anchors.rightMargin: pop._sumPad
                        anchors.verticalCenter: parent.verticalCenter
                        // What gives way when the window is too narrow for both.
                        width: Math.min(implicitWidth,
                                        Math.max(0, parent.width - nameLabel.x - nameLabel.width
                                                    - pop._gap - pop._sumPad))
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: modelData.sum
                        color: Theme.text2
                        font.family: Theme.family
                        font.pixelSize: pop._px(Theme.fontSmall + 1)
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // Hover moves the cursor only with the mouse in hand, as in Options: a
                    // pointer parked over the list must not move the pad's row (#24).
                    onEntered: if (SdlGamepadKeyNavigation.inputMode !== "key") list.currentIndex = index
                    onClicked: pop._choose(index)
                }
            }
        }

        // ── Footer: the buttons, printed where the user is looking ──
        Rectangle {
            y: pop._headH + pop._listH
            width: parent.width
            height: 1
            color: Theme.line
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            y: pop._headH + pop._listH + Math.round((pop._footH - height) / 2)
            spacing: pop._px(22)

            Repeater {
                model: [
                    { keys: [{ btn: "A", key: "Enter" }],                act: qsTr("Use"),   kind: "use" },
                    { keys: [{ btn: "B", key: "Esc" }],                  act: qsTr("Close"), kind: "close" }
                ]
                delegate: Item {
                    implicitWidth: promptRow.implicitWidth
                    implicitHeight: promptRow.implicitHeight
                    Row {
                        id: promptRow
                        spacing: pop._px(8)
                        Repeater {
                            model: modelData.keys
                            delegate: ActionHint {
                                anchors.verticalCenter: parent.verticalCenter
                                buttonKey: modelData.btn
                                keyLabel: modelData.key
                                size: pop._px(24)
                            }
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.act
                            color: Theme.text
                            font.family: Theme.family
                            font.pixelSize: pop._px(Theme.fontBody)
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: modelData.kind === "use" ? pop._choose(list.currentIndex) : pop.close()
                    }
                }
            }
        }
    }
}
