import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5

/*
 * Shown while an offline host is being woken, up to the moment the PIN pad takes over or
 * the host turns out not to need one.
 *
 * Steps rather than a bare spinner: a wake takes the better part of a minute, and knowing
 * which part is slow is the difference between waiting and wondering. Each row is the name
 * of a thing that has to happen — no sentences.
 */
Popup {
    id: dialog

    property string hostName : ""
    // Which row is being waited on: 0 the packet, 1 the host, 2 StreamTweak. (3 is the link
    // match, which happens after this dialog has closed — the card shows that one.)
    property int step : 0
    property string detail : ""

    /*
     * ── Each phase's own clock ───────────────────────────────────────────────
     *
     * A wake is the better part of a minute of nothing visible happening, and "which part is
     * slow" used to be answerable only by noticing which row had the spinner — never by how
     * long it had been there. So the rows that are actually waited on — the host appearing and
     * StreamTweak coming up — carry their own elapsed time while they run and keep the figure
     * once they are settled.
     *
     * The first row has no clock: the packet leaves in a fraction of a second, so its figure
     * was a number next to a tick that said nothing the tick did not. HomeScreen still times
     * it — only the display is gone.
     *
     * Three parallel arrays rather than three objects, because the Repeater below already
     * walks these by the same index it uses for the labels.
     */
    property var  stepStart  : [0, 0, 0]        // ms since epoch; 0 = not started
    property var  stepMs     : [-1, -1, -1]     // settled duration; -1 = not settled
    property var  stepResult : ["", "", ""]     // "" pending/running · "ok" · "fail"
    property real nowMs      : 0                // ticked by the owner so a running row counts up

    // One failure ends the wake: nothing below it will ever start, so nothing below it spins.
    readonly property bool failed: stepResult.indexOf("fail") >= 0

    readonly property var _steps: waitForStreamTweak
           ? [qsTr("Wake signal sent"),
              qsTr("Host on the network"),
              qsTr("StreamTweak ready")]
           : [qsTr("Wake signal sent"),
              qsTr("Host on the network")]

    // Seconds and tenths, running or settled — the same format both ways, so a row does not
    // change shape at the moment it finishes. Past a minute, m:ss.t.
    function _secText(ms) {
        if (ms < 0) return ""
        var t = Math.floor(ms / 100)            // whole tenths, truncated: a clock never rounds up
        var tenths = t % 10
        var s = Math.floor(t / 10)
        if (s >= 60) return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2) + "." + tenths
        return s + "." + tenths + "s"
    }
    function _rowTime(i, running) {
        if (i === 0) return ""
        if (running) return stepStart[i] ? _secText(Math.max(0, nowMs - stepStart[i])) : ""
        return _secText(stepMs[i] !== undefined ? stepMs[i] : -1)
    }

    // Whether this host's StreamTweak integration is on. With it off there is no third step
    // to wait for — the wake is over the moment the host answers — so the row is not drawn
    // at all rather than drawn and skipped. A step that can never complete is worse than an
    // absent one: the old dialog spun under the words "StreamTweak ready" for a full minute
    // on hosts that would never have it.
    property bool waitForStreamTweak : true

    signal cancelled()

    // Shared dialog measurements — see Theme.uiScale.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    /*
     * ── Column widths for the centred block ──────────────────────────────────
     *
     * The steps are centred as a block, and a block only has a width if its rows agree on
     * one. Every row is glyph · label · time with the label and time columns sized to the
     * widest thing they will ever hold, so the ticks line up down one edge and the times down
     * the other — the two things a centred-per-row layout used to break.
     *
     * The time column is sized for "8:88.8", not for the current figure: sized to the figure,
     * the whole block would re-centre and shift sideways every tenth of a second.
     *
     * The `.height` reads are there only to make these bindings depend on the font: the
     * method calls alone are not tracked, and a scale change would leave stale widths.
     */
    readonly property FontMetrics _bodyFm: FontMetrics {
        font.family: Theme.family
        font.pixelSize: dialog._px(Theme.fontBody)
    }
    readonly property FontMetrics _smallFm: FontMetrics {
        font.family: Theme.family
        font.pixelSize: dialog._px(Theme.fontSmall)
    }
    readonly property real _glyphW: _px(26)
    readonly property real _gap: _px(10)
    readonly property real _timeW: _smallFm.height >= 0
                                   ? Math.ceil(_smallFm.advanceWidth("8:88.8")) + _px(2) : 0
    readonly property real _labelW: {
        var w = 0
        if (_bodyFm.height >= 0) {
            for (var i = 0; i < _steps.length; i++)
                w = Math.max(w, _bodyFm.advanceWidth(_steps[i]))
        }
        // Never wider than the dialog leaves room for: past that the label elides instead.
        return Math.max(0, Math.min(Math.ceil(w) + _px(2),
                                    availableWidth - _glyphW - _timeW - 2 * _gap))
    }

    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: Math.min(_px(520), parent ? parent.width * 0.8 : _px(520))
    padding: _px(28)

    background: Rectangle {
        // Theme.card, not Theme.ground: ground is the colour of the page underneath, so a
        // panel painted with it does not lift off what it is covering.
        color: Theme.card
        radius: dialog._px(14)
        border.color: Theme.line
        border.width: 1
    }

    Overlay.modal: Rectangle { color: "#cc000000" }

    contentItem: Column {
        spacing: dialog._px(16)
        width: dialog.availableWidth

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("WAKE")
            font.family: Theme.family
            // The grey, not the accent: in this interface the accent means "the focus is
            // here", and an eyebrow is never focusable.
            font.pixelSize: dialog._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: dialog._u * 1.6
            color: Theme.text3
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: dialog.hostName
            font.family: Theme.family
            font.pixelSize: dialog._px(Theme.fontH2)
            font.bold: true
            color: Theme.text
        }

        // Centred as a block, like everything else in the dialog. Every row has the same
        // width (see the column widths above), so centring the block keeps both the ticks and
        // the times in straight columns.
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: dialog._px(9)

            Repeater {
                // Three, and it ends here: the link match that follows is shown on the host
                // card, which is where the user will be looking by then.
                model: dialog._steps

                Row {
                    id: stepRow
                    height: dialog._px(26)
                    spacing: dialog._gap

                    readonly property string _result: dialog.stepResult[index] || ""
                    readonly property bool   _done:   _result === "ok"
                    readonly property bool   _failed: _result === "fail"
                    // The one row with a clock running — and nothing runs once a step has
                    // failed, because a spinner under a dead wake is a promise it will finish.
                    readonly property bool   _running:
                        !_done && !_failed && index === dialog.step && !dialog.failed

                    // Done · failed · in progress · not yet, in one glyph column so the
                    // labels line up. 26px, not 14: that dates from Material BusyIndicator,
                    // which shrank to a speck at label height. Spinner scales properly, so
                    // the size is now a layout choice rather than a workaround.
                    Item {
                        width: dialog._glyphW
                        height: dialog._px(26)
                        anchors.verticalCenter: parent.verticalCenter

                        Label {
                            anchors.centerIn: parent
                            visible: stepRow._done || stepRow._failed
                            text: stepRow._done ? "✓" : "✗"
                            font.family: Theme.family
                            font.pixelSize: dialog._px(Theme.fontBody)
                            color: stepRow._done ? Theme.online : Theme.danger
                        }
                        Spinner {
                            anchors.centerIn: parent
                            bodySize: dialog._px(15)
                            visible: stepRow._running
                            running: visible
                        }
                    }

                    Label {
                        width: dialog._labelW
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        text: modelData
                        font.family: Theme.family
                        font.pixelSize: dialog._px(Theme.fontBody)
                        color: stepRow._failed  ? Theme.danger
                             : stepRow._done    ? Theme.online
                             : stepRow._running ? Theme.text
                             :                    Theme.text3
                    }

                    // The figure this row is worth: counting up while it is the one being
                    // waited on, then frozen at what it took. Nothing at all before it starts
                    // — a "0.0s" on a step that has not begun claims a measurement — and
                    // nothing ever on the first row (see the clock note above). The column
                    // keeps its width either way, or that row would not line up.
                    Label {
                        width: dialog._timeW
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: dialog._rowTime(index, stepRow._running)
                        font.family: Theme.family
                        font.pixelSize: dialog._px(Theme.fontSmall)
                        color: stepRow._failed ? Theme.danger
                             : stepRow._done   ? Theme.online
                             :                   Theme.text3
                    }
                }
            }
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: dialog.detail !== ""
            text: dialog.detail
            font.family: Theme.family
            font.pixelSize: dialog._px(Theme.fontSmall)
            color: Theme.text3
        }

        DialogButton {
            id: cancelBtn
            anchors.horizontalCenter: parent.horizontalCenter
            // "Cancel" while there is something of ours to cancel — the wait for StreamTweak
            // to come up. With the integration off there is nothing running on our side: the
            // host boots either way and this dialog closes itself the moment it answers, so
            // the button only dismisses it early. Calling that "Cancel" would claim it aborts
            // something.
            // …and "Close" again once a step has failed: there is nothing left running to
            // cancel, the dialog is only still up so the ✗ and its reason can be read.
            text: (dialog.waitForStreamTweak && !dialog.failed) ? qsTr("Cancel") : qsTr("Close")
            onActivated: dialog.cancelled()
            Keys.onEscapePressed: dialog.cancelled()
        }
    }

    onOpened: cancelBtn.forceActiveFocus()
}
