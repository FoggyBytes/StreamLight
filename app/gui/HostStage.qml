import QtQuick 2.12
import QtQuick.Controls 2.2
import QtQuick.Effects

import Theme 1.0
import SdlGamepadKeyNavigation 1.0
import StreamingPreferences 1.0
import InputHints 1.0

/*
 * The stage — one host, filling the screen.
 *
 * This file draws and nothing else. Every value it shows arrives as a property and every
 * button it offers leaves as `activated(kind)`; it owns no timers, no model, no dialogs and
 * never talks to ComputerModel. HomeScreen keeps all of that. The split exists because the
 * old host tile had 593 lines of visuals wound through the machinery that fed them, which
 * made both halves hard to change — and because the stage is now the whole screen, so it
 * would have grown, not shrunk.
 *
 * The one piece of state that does live here is which action has the focus, because the
 * action row is drawn here: HomeScreen says *whether* this zone has the focus, the stage
 * says *where inside it*.
 */
Item {
    id: stage

    // ── The host ─────────────────────────────────────────────────────────────
    property string hostName: ""
    property bool   online: false
    property bool   paired: false
    property bool   statusUnknown: false
    property bool   wakeable: false
    property bool   serverSupported: true
    property string address: ""
    property string tailscaleAddress: ""
    property bool   hasTailscale: false
    property bool   tailscaleActive: false
    property string gpuModel: ""
    property bool   hideAddresses: false

    property int    profileCount: 0
    property int    activeProfileSlot: -1
    property string activeProfileName: ""

    // There is something to cycle only once a profile exists beyond Global. Below that the
    // shoulders and the badge both go: LB/RB would move between one position and itself, and
    // a badge that can only ever read "Global" states the only possibility there is.
    readonly property bool _hasProfiles: !addMode && profileCount >= 1

    // The active profile's override map, empty when none is active. The stream badges show
    // the EFFECTIVE settings — the global preferences with this applied — because that is
    // what the next launch would use. It arrives as a property like everything else here,
    // and the sender re-resolves it whenever the active profile changes, which is what makes
    // the badges follow LB/RB.
    //
    // ⚠️ Reading StreamingPreferences directly would be reading the BOTTOM of the cascade and
    // silently ignoring the profile — the same mistake that made the link-match line lie on
    // this very screen (§40).
    property var streamOverride: ({})

    readonly property int  _effW:       (streamOverride && streamOverride.width   !== undefined) ? streamOverride.width   : StreamingPreferences.width
    readonly property int  _effH:       (streamOverride && streamOverride.height  !== undefined) ? streamOverride.height  : StreamingPreferences.height
    readonly property int  _effFps:     (streamOverride && streamOverride.fps     !== undefined) ? streamOverride.fps     : StreamingPreferences.fps
    readonly property int  _effBitrate: (streamOverride && streamOverride.bitrate !== undefined) ? streamOverride.bitrate : StreamingPreferences.bitrateKbps
    readonly property bool _effHdr:     (streamOverride && streamOverride.hdr     !== undefined) ? streamOverride.hdr     : StreamingPreferences.enableHdr
    readonly property int  _effCodec:   (streamOverride && streamOverride.codec   !== undefined) ? streamOverride.codec   : StreamingPreferences.videoCodecConfig
    readonly property int  _effAudio:   (streamOverride && streamOverride.audio   !== undefined) ? streamOverride.audio   : StreamingPreferences.audioConfig

    // Same wording as the host page, deliberately: a host has to read the same on both.
    function _codecLabel(c) { return c === 1 ? "H.264" : c === 2 ? "HEVC" : c === 4 ? "AV1" : qsTr("Auto codec") }
    function _audioLabel(a) { return a === 1 ? "5.1" : a === 2 ? "7.1" : qsTr("Stereo") }

    function _resLabel() {
        var w = stage._effW, h = stage._effH
        if (w === 3840 && h === 2160) return "4K"
        if (w === 2560 && h === 1440) return "1440p"
        if (w === 1920 && h === 1080) return "1080p"
        if (w === 1280 && h === 720)  return "720p"
        return w + "×" + h
    }

    // "" (unknown) | none | authorized | pending | denied | open
    property string authState: ""

    // Link speed, already formatted by the caller — the stage never parses.
    property string hostLinkText: ""
    property bool   willSwitchLink: false
    property string linkSwitchText: ""
    property bool   cantSwitchLink: false

    // After a remote wake: the host's link is being matched right now, and then briefly that
    // it is done. "Online" alone was never the question — a host mid-renegotiation answers
    // pings and cannot stream — so the card says which of the two it is instead of leaving
    // the user to launch a game into a link that is still coming back up.
    property bool   linkChanging: false
    property string linkChangeText: ""
    // What the change is for. "Matching" while a launch prepares the link, "Restoring" while it
    // is being put back — the two are the same mechanism and look identical from here, so the
    // caller says which one it is rather than this file guessing from a direction.
    property string linkChangeLabel: qsTr("Matching")
    property bool   justReady: false
    // Wording for the green chip that follows. Waking a host ends with "Ready"; a restore ends
    // with the speed it went back to, which is the thing the user was waiting to be told.
    property string justReadyText: qsTr("Ready")

    // The backdrop. Two colours, whatever their origin: a hash of the host name until the
    // user picks something, then the pair CoverPalette derived from their picture or their
    // colour. Keeping it a plain property is what lets the source change without this file
    // knowing anything about it.
    property color backdropFrom: "#1c1c1c"
    property color backdropTo:   "#0d0d0d"

    // Optional picture behind the gradient. Drawn sharp and at full saturation: the earlier
    // version blurred and desaturated it on the theory that this is what makes a picture read
    // as a background, and on screen that theory was wrong — it just looked like a bad copy of
    // the picture. What actually keeps the text readable is the scrim below, which is dense
    // exactly where the name and the fields are and gone by the right-hand side, so the
    // picture gets to be itself over most of the card.
    property string backdropImage: ""

    // ── The host's last session ──────────────────────────────────────────────
    // Straight from StreamTweak over the bridge — see LastSessionReport on the host side.
    // {has:false} or an empty map means there is nothing to show, or the host is older than
    // 8.1.0 and does not know the command; either way the panel is simply absent.
    //
    // ⚠️ This is the HOST's last session, not necessarily one of ours: StreamTweak logs
    // whatever streamed and keeps no record of which client it belonged to. That is why the
    // caption says "last session" and not "your last session".
    property var lastSession: ({})

    readonly property bool _hasLastSession: !addMode && lastSession && lastSession.has === true

    // Reserved so the host name and the field row stop short of the panel instead of running
    // under it. Zero when there is no panel, so a host without one keeps the full width.
    readonly property int _lastPanelW: _hasLastSession ? _px(620) : 0

    // ── "Add a host" mode ────────────────────────────────────────────────────
    // The add panel is the same stage with a different face, not a separate screen: it is
    // reached by selecting the last tab, so adding a host costs exactly what selecting a
    // host costs. The old "+ Add Hosts" tile sat inside the grid and had to be navigated to.
    property bool addMode: false
    property bool discovering: false

    // ── Focus ────────────────────────────────────────────────────────────────
    // zoneActive: the action row is the focused zone (vs. the tab strip above).
    property bool zoneActive: false
    property int  actionIndex: 0
    property bool pointerMode: false

    signal activated(string kind)

    // (The shoulder glyphs used to be resolved here. ActionHint does it now, along with the
    // keyboard alternative, so this file only names the button an action belongs to.)

    // ── Scale ────────────────────────────────────────────────────────────────
    // Every size below is multiplied by this, so the layout keeps its proportions on a
    // handheld and on a TV. Clamped at both ends: unclamped, a narrow window would render an
    // 8px label and a very wide one a host name taller than the card.
    //
    // The reference width is deliberately well below the real one. Sizing 1:1 against 1920
    // was right for a monitor two feet away and wrong for the device this is actually used
    // on: a 7-inch handheld at the same 1920 pixels, where the labels came out physically
    // tiny and half the card sat empty. Dividing by 1330 makes everything about a third
    // larger at any given width — and there was room for it.
    //
    // ⚠️ Raising every size through the divisor, rather than editing them one by one, is the
    // point: the layout's proportions are already tuned and a global factor cannot break
    // them. AppsScreen MUST use the same number — a host and its games drawn at two
    // different scales is the one thing a shared grammar does not survive.
    readonly property real _u: Math.max(0.62, Math.min(1.60, width / 1330))

    function _px(n) { return Math.round(n * _u) }

    // ── Reading against the backdrop ─────────────────────────────────────────
    /*
     * What the left of the card actually is, once the scrim has been laid over the backdrop —
     * and therefore what the text has to be legible against.
     *
     * White was hardcoded here, which held only because every backdrop happened to be dark.
     * It stops holding the moment the user picks a bright colour or a bright picture, and
     * "every backdrop so far" is not something an interface can depend on. The scrim is
     * densest exactly where the name and the fields sit, so this samples the composite at
     * that end rather than at the card's average.
     */
    readonly property color _bgUnderText:
        Theme.blend(backdropImage !== "" ? "#202020" : backdropFrom,
                    backdropImage !== "" ? "#e605080a" : "#b005080a")

    readonly property color _onBg:    Theme.onColor(_bgUnderText)
    readonly property bool  _bgIsLight: _onBg !== Qt.rgba(1, 1, 1, 1)

    // The secondary and tertiary tones have to flip with it, or a dark-on-light card keeps
    // pale grey subtitles that vanish.
    //
    // The tertiary is NOT Theme.text3 on a dark card. That tone was picked to sit on the
    // app's flat near-black, and this card is not that: the backdrop is a colour, and often
    // a fairly dark one, so the same grey ends up with far less contrast against it than it
    // was ever meant to have. The field captions — LOCAL ADDRESS, TAILSCALE, HOST LINK —
    // were the ones that lost, being the smallest text on the card.
    readonly property color _onBg2: _bgIsLight ? Qt.rgba(0, 0, 0, 0.62) : Theme.text2
    readonly property color _onBg3: _bgIsLight ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(1, 1, 1, 0.74)

    // ── The actions ──────────────────────────────────────────────────────────
    // Built from the host's state rather than shown-and-disabled, so the primary button
    // always says the true next step: Open a paired host, Pair one that isn't, Wake one
    // that is asleep. The old tile had one card body that meant all three depending on
    // what you happened to know about the host.
    /*
     * A is deliberately absent from the data, because A is not bound to an action: it
     * activates whatever has the focus. The delegate draws the A badge on the focused button
     * and nowhere else.
     *
     * Stamping A permanently on the first button, which is what this used to do, made the
     * screen contradict itself: with the focus up on the tab strip the status bar said
     * "A Select" while the button underneath still claimed "A Pair", and only one of the two
     * could be right.
     *
     * Shutdown is not here. It is the one action with a face button that works from anywhere
     * (X), so it belongs with the other button prompts in the status bar — where it also
     * stops taking a D-pad stop away from the three actions that have no shortcut at all.
     */
    readonly property var actions: {
        if (addMode) {
            return [
                { kind: "addHost", label: qsTr("Add a host"), danger: false, disabled: false },
                { kind: "refresh", label: qsTr("Refresh"),    danger: false, disabled: false }
            ]
        }

        var list = []
        if (statusUnknown)
            list.push({ kind: "open", label: qsTr("Open"), danger: false, disabled: true })
        else if (online && paired)
            list.push({ kind: "open", label: qsTr("Open"), danger: false, disabled: false })
        else if (online)
            list.push({ kind: "pair", label: qsTr("Pair"), danger: false, disabled: false })
        else if (wakeable)
            list.push({ kind: "wake", label: qsTr("Wake"), danger: false, disabled: false })
        else
            list.push({ kind: "open", label: qsTr("Open"), danger: false, disabled: true })

        list.push({ kind: "profiles", label: qsTr("Profiles"), danger: false, disabled: false })
        list.push({ kind: "options",  label: qsTr("Options"),  danger: false, disabled: false })

        return list
    }

    onActionsChanged: if (actionIndex >= actions.length) actionIndex = Math.max(0, actions.length - 1)

    // Walks the action row. Returns false at the ends so the caller can decide what a
    // further press means (today: nothing — the row does not wrap).
    function moveAction(dir) {
        var next = actionIndex + dir
        if (next < 0 || next >= actions.length) return false
        actionIndex = next
        return true
    }

    // Fires the focused action. Disabled entries are silently ignored rather than skipped
    // during navigation: an unreachable host still shows "Open", greyed, which says more
    // than a row that quietly loses a button.
    function activateFocused() {
        if (actionIndex < 0 || actionIndex >= actions.length) return
        var a = actions[actionIndex]
        if (a.disabled) return
        stage.activated(a.kind)
    }

    // (`activateKind` lived here so the X shortcut could land on Shutdown wherever the focus
    //  was. Shutdown is a status-bar prompt again and HomeScreen calls its own action
    //  directly, so nothing needs to reach into the row by name.)

    // ── Derived text ─────────────────────────────────────────────────────────
    readonly property string _stateLabel:
          statusUnknown          ? qsTr("Checking")
        : online && paired       ? qsTr("Online")
        : online                 ? qsTr("Reachable")
        :                          qsTr("Offline")

    readonly property color _stateColor:
          statusUnknown          ? Theme.text3
        : online && paired       ? Theme.online
        : online                 ? Theme.warning
        :                          Theme.offline

    readonly property string _authLabel:
          authState === "authorized" ? qsTr("StreamTweak authorized")
        : authState === "pending"    ? qsTr("StreamTweak pending")
        : authState === "denied"     ? qsTr("StreamTweak denied")
        :                              ""

    readonly property color _authColor:
          authState === "authorized" ? Theme.accent
        : authState === "pending"    ? Theme.warning
        :                              Theme.danger

    // The one line under the name. It answers "can I press A?" before the eye reaches the
    // buttons. The active profile used to be tacked onto the end of it and is now the badge
    // beside the name — saying it in both places would just be saying it twice.
    readonly property string _subLine: {
        if (addMode)
            return discovering ? qsTr("Searching your local network…")
                               : qsTr("Automatic discovery is off — add the host by address")
        return statusUnknown    ? qsTr("Checking this host…")
             : online && paired ? (serverSupported ? qsTr("Ready to stream")
                                                   : qsTr("This host needs a newer StreamLight"))
             : online           ? qsTr("Not paired yet")
             : wakeable         ? qsTr("Offline · can be woken up")
             :                    qsTr("Offline")
    }

    // ═════════════════════════════════════════════════════════════════════════
    // The card
    // ═════════════════════════════════════════════════════════════════════════
    Rectangle {
        id: card
        anchors.fill: parent
        radius: stage._px(16)
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        clip: true

        // ── Backdrop ─────────────────────────────────────────────────────────
        /*
         * The base ramp, the host's picture over it, and the scrim over that — all inside ONE
         * layer, masked once to the card's shape.
         *
         * Two flat ramps rather than one diagonal: QML gradients are horizontal or vertical
         * only, and horizontal is the axis that matters — the text sits on the left, so that
         * is where the darkening has to be.
         *
         * ⚠️ Grouping them is what keeps the corners right. `clip` is RECTANGULAR: it clips to
         * the bounding box and knows nothing about `radius`, so a square child filling a
         * rounded parent paints straight over the rounded corners. Each layer used to repeat
         * the radius to work around that, which a gradient IMAGE cannot do at all — so they
         * are masked together instead. That is also one effect pass fewer than the two this
         * card used to run.
         *
         * ⚠️ There is no dither tile any more, and its absence is the fix rather than a
         * simplification. A tile of faint noise used to sit over the finished ramps; because
         * it was composited rather than applied at quantisation it was both five times too
         * strong and lopsided — white at alpha 5 over a near-black card adds five levels while
         * black subtracts a third of one — so it read as static grain instead of disappearing.
         * The ramps now carry their own dither, applied as they are rounded to 8 bits. See
         * backend/gradientimage.h.
         */
        Item {
            id: cardMask
            anchors.fill: parent
            layer.enabled: true
            visible: false
            Rectangle {
                anchors.fill: parent
                radius: card.radius
            }
        }

        // ── 1. The base ramp ─────────────────────────────────────────────────
        Item {
            id: baseRampLayer
            anchors.fill: parent
            layer.enabled: true
            visible: false

            DitheredGradient {
                anchors.fill: parent
                orientation: Qt.Horizontal
                stops: [
                    { pos: 0.0,  color: stage.backdropFrom },
                    { pos: 0.46, color: stage.backdropTo },
                    { pos: 0.90, color: "#05080a" }
                ]
            }
        }

        MultiEffect {
            anchors.fill: parent
            visible: !stage.addMode
            source: baseRampLayer
            autoPaddingEnabled: false
            maskEnabled: true
            maskSource: cardMask
        }

        // ── 2. The host's picture, over the ramp it was derived from ─────────
        /*
         * ⚠️ On its own effect, and NOT grouped into a layer with the ramps around it.
         *
         * 5.1.0 first put all three in one layer, masked once — tidier, one pass fewer, and
         * it broke this: the picture stopped appearing and the card showed the ramp under the
         * scrim, reported as "the dark bordeaux gradient". The measurements say the file, the
         * path and the mask are all fine — the same picture renders correctly in an isolated
         * scene — but inside the layer the element never left `status: Loading`, with its
         * painted geometry already computed, so it never produced a node to be rendered into
         * the FBO. The small provider-generated ramps beside it were unaffected, which is why
         * the card looked half-right.
         *
         * An effect sampling this Image directly does not depend on any of that: it asks the
         * texture provider every frame and draws whatever is there. It is the arrangement
         * 5.0.0 shipped and the one that demonstrably works. The cost is one extra pass on a
         * card that is redrawn only when something about the host changes.
         */
        Image {
            id: backdropPicture
            anchors.fill: parent
            source: stage.backdropImage !== "" ? "file:///" + stage.backdropImage : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            // Drawn by the effect below; this element is only the texture behind it.
            visible: false
        }

        MultiEffect {
            anchors.fill: parent
            visible: !stage.addMode && stage.backdropImage !== ""
            source: backdropPicture
            autoPaddingEnabled: false
            maskEnabled: true
            maskSource: cardMask
        }

        // ── 3. The scrim ─────────────────────────────────────────────────────
        // Not decoration: without it a light backdrop and white text meet and the text loses.
        // Densest on the left where the name and the fields are, gone by the right edge so the
        // host's colour still reads as a colour. Over a picture it starts denser and clears
        // sooner — reaching fully transparent at the right edge matters, the 19% black that
        // used to sit there is what made artwork look washed out. Its alpha ramp is dithered
        // too: a staircase in alpha bands just as visibly as one in a colour channel.
        Item {
            id: scrimLayer
            anchors.fill: parent
            layer.enabled: true
            visible: false

            DitheredGradient {
                anchors.fill: parent
                orientation: Qt.Horizontal
                stops: [
                    { pos: 0.0,  color: stage.backdropImage !== "" ? "#e605080a" : "#b005080a" },
                    { pos: 0.30, color: stage.backdropImage !== "" ? "#b005080a" : "#8005080a" },
                    { pos: 0.62, color: stage.backdropImage !== "" ? "#4005080a" : "#3005080a" },
                    { pos: 1.0,  color: "#0005080a" }
                ]
            }
        }

        MultiEffect {
            anchors.fill: parent
            visible: !stage.addMode
            source: scrimLayer
            autoPaddingEnabled: false
            maskEnabled: true
            maskSource: cardMask
        }

        // ── Chips ────────────────────────────────────────────────────────────
        Row {
            id: chipRow
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: stage._px(34)
            anchors.leftMargin: stage._px(38)
            spacing: stage._px(9)
            visible: !stage.addMode

            Repeater {
                model: {
                    var c = [{ text: stage._stateLabel, dot: stage._stateColor }]
                    if (stage._authLabel.length > 0)
                        c.push({ text: stage._authLabel, dot: stage._authColor })
                    // Available is a bright white dot, not the grey the secondary text uses:
                    // it is reporting a route that is there and usable, and at 8px a muted
                    // grey read as "off". Active keeps the accent, which is the stronger
                    // statement of the two — that is the route we are on.
                    if (stage.hasTailscale)
                        c.push({ text: stage.tailscaleActive ? qsTr("Tailscale active")
                                                             : qsTr("Tailscale available"),
                                 dot: stage.tailscaleActive ? Theme.accent : "#ffffff" })
                    // Last, so it reads as the newest thing to have happened, and never both
                    // at once: one is the wait, the other is the end of it.
                    if (stage.linkChanging)
                        c.push({ text: qsTr("Link"), dot: Theme.warning })
                    else if (stage.justReady)
                        c.push({ text: stage.justReadyText, dot: Theme.online })
                    return c
                }

                delegate: Rectangle {
                    height: stage._px(30)
                    width: chipContent.implicitWidth + stage._px(26)
                    radius: stage._px(5)
                    color: "transparent"
                    border.color: Theme.line
                    border.width: 1

                    Row {
                        id: chipContent
                        anchors.centerIn: parent
                        spacing: stage._px(7)

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: stage._px(7); height: width
                            radius: width / 2
                            color: modelData.dot
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.text.toUpperCase()
                            color: stage._onBg2
                            font.family: Theme.family
                            font.pixelSize: stage._px(15)
                            font.weight: Font.DemiBold
                            font.letterSpacing: stage._u * 1.1
                        }
                    }
                }
            }
        }

        // ── Name + subtitle ──────────────────────────────────────────────────
        Column {
            id: nameBlock
            anchors.top: stage.addMode ? parent.top : chipRow.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: stage._px(stage.addMode ? 56 : 20)
            anchors.leftMargin: stage._px(38)
            anchors.rightMargin: stage._px(38) + stage._lastPanelW
            spacing: stage._px(8)

            // The name, and the active profile beside it.
            //
            // The profile used to be four words into a grey sentence underneath, which is not
            // where you look — and it is the single setting that changes what every launch
            // from this host will look like. At the name's own height, in the accent, it
            // becomes the second thing read instead of the last.
            Row {
                width: parent.width
                spacing: stage._px(18)

                Text {
                    id: nameText
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width - (profileCluster.visible ? profileCluster.width + stage._px(18) : 0))
                    text: stage.addMode ? qsTr("Add a host") : stage.hostName
                    color: stage._onBg
                    font.family: Theme.family
                    font.pixelSize: stage._px(68)
                    font.weight: Font.Bold
                    font.letterSpacing: -stage._u * 2.4
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                /*
                 * The profile, with its shoulders on either side of it.
                 *
                 * LB/RB used to sit on their own at the far end of the button row, which made
                 * them one of two floating legends on the card with nothing to anchor to. Here
                 * they are the two ends of the thing they move, so there is no second block to
                 * align and no caption needed — the badge between them says what they change.
                 *
                 * ⚠️ Which is why the badge now shows "Global" instead of disappearing: with
                 * the shoulders attached to it, an empty slot would leave them pointing at
                 * nothing. Global is a real position in the cycle, not the absence of one.
                 * When the host has no profiles at all there is nothing to cycle, and the
                 * whole cluster goes — badge included, since naming the only possible state
                 * tells the user nothing.
                 */
                Row {
                    id: profileCluster
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: stage._px(11)
                    visible: stage._hasProfiles

                    Repeater {
                        model: stage._hasProfiles
                               ? [{ btn: "LB", key: "Q", kind: "prevProfile" }] : []
                        delegate: Item {
                            anchors.verticalCenter: parent.verticalCenter
                            width: stage._px(40); height: stage._px(28)
                            ActionHint {
                                anchors.centerIn: parent
                                buttonKey: modelData.btn
                                keyLabel:  modelData.key
                                size: stage._px(26)
                                opacity: lbMouse.containsMouse ? 1.0 : 0.85
                            }
                            MouseArea {
                                id: lbMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: stage.activated(modelData.kind)
                            }
                        }
                    }

                    Rectangle {
                        id: profileBadge
                        anchors.verticalCenter: parent.verticalCenter
                        width: profileBadgeText.implicitWidth + stage._px(34)
                        height: profileBadgeText.implicitHeight + stage._px(16)
                        radius: stage._px(10)
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                        border.color: Theme.accent
                        border.width: Math.max(1, stage._px(2))

                        Text {
                            id: profileBadgeText
                            anchors.centerIn: parent
                            text: stage.activeProfileSlot >= 0 && stage.activeProfileName.length > 0
                                      ? stage.activeProfileName : qsTr("Global")
                            color: Theme.accent
                            font.family: Theme.family
                            // Below the host name, clearly above everything else: this is the
                            // second-most important thing on the card, not the first.
                            font.pixelSize: stage._px(40)
                            font.weight: Font.DemiBold
                        }
                    }

                    Repeater {
                        model: stage._hasProfiles
                               ? [{ btn: "RB", key: "E", kind: "nextProfile" }] : []
                        delegate: Item {
                            anchors.verticalCenter: parent.verticalCenter
                            width: stage._px(40); height: stage._px(28)
                            ActionHint {
                                anchors.centerIn: parent
                                buttonKey: modelData.btn
                                keyLabel:  modelData.key
                                size: stage._px(26)
                                opacity: rbMouse.containsMouse ? 1.0 : 0.85
                            }
                            MouseArea {
                                id: rbMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: stage.activated(modelData.kind)
                            }
                        }
                    }
                }
            }

            // The status line, and on the same baseline what the next launch would look
            // like: the same badges the host page shows under its Stream heading, in the same
            // shapes and the same words, because this is the screen you are on when you decide
            // whether to press A.
            //
            // No group label in front of them here. On the host page "STREAM" earns its place
            // by separating that half of the line from the Host half; on this card there is
            // nothing to separate it from, and the status line already introduces them.
            Row {
                id: subLineRow
                width: parent.width
                spacing: stage._px(14)

                readonly property int _rowH: stage._px(28)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: stage._subLine
                    color: stage._onBg2
                    font.family: Theme.family
                    font.pixelSize: stage._px(18)
                    maximumLineCount: 1
                }

                Repeater {
                    model: {
                        // Only worth stating for a host that can actually be streamed to —
                        // which is also what keeps this row short, since the long status
                        // lines all belong to states with no settings to show.
                        if (stage.addMode || !stage.online || !stage.paired || !stage.serverSupported)
                            return []
                        var m = [{ text: stage._resLabel() },
                                 { text: stage._effFps + " FPS" },
                                 { text: (stage._effBitrate / 1000).toFixed(0) + " Mbps" }]
                        if (stage._effHdr)
                            m.push({ text: "HDR" })
                        m.push({ text: stage._codecLabel(stage._effCodec) })
                        m.push({ text: stage._audioLabel(stage._effAudio) })
                        return m
                    }

                    delegate: Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        height: subLineRow._rowH
                        width: badgeText.implicitWidth + stage._px(20)
                        radius: stage._px(7)

                        // Derived from the reading colour rather than from Theme, so these
                        // follow the card's own light/dark flip along with the text. A fixed
                        // line colour would vanish on a host carrying a light background.
                        color: Qt.rgba(stage._onBg.r, stage._onBg.g, stage._onBg.b, 0.07)
                        border.width: 1
                        border.color: Qt.rgba(stage._onBg.r, stage._onBg.g, stage._onBg.b, 0.26)

                        Label {
                            id: badgeText
                            anchors.centerIn: parent
                            text: String(modelData.text)
                            color: stage._onBg
                            font.family: Theme.family
                            font.pixelSize: stage._px(14)
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        // ── The data quadrant ────────────────────────────────────────────────
        // Fields appear only when they have something to say, so a plain LAN host shows
        // three and a Tailscale host with a pending link change shows five, without either
        // leaving a hole where the other's data would be.
        Row {
            id: fieldRow
            anchors.top: nameBlock.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: stage._px(32)
            anchors.leftMargin: stage._px(38)
            anchors.rightMargin: stage._px(38) + stage._lastPanelW
            spacing: stage._px(46)
            visible: !stage.addMode

            Repeater {
                model: {
                    var f = []
                    if (stage.addMode) return f

                    if (!stage.hideAddresses && stage.address.length > 0)
                        f.push({ label: stage.hasTailscale ? qsTr("Local address") : qsTr("Address"),
                                 value: stage.address, colour: stage._onBg })

                    if (!stage.hideAddresses && stage.hasTailscale && stage.tailscaleAddress.length > 0)
                        f.push({ label: qsTr("Tailscale"), value: stage.tailscaleAddress, colour: stage._onBg })

                    if (stage.gpuModel.length > 0)
                        f.push({ label: qsTr("GPU"), value: stage.gpuModel, colour: stage._onBg })

                    // Plain, like every other field. It was green, which in this interface
                    // means "live and healthy" — but the link speed is a fact, not a verdict,
                    // and colouring one value out of five made the rest look switched off.
                    // Amber below is different: that one has not happened yet.
                    if (stage.hostLinkText.length > 0)
                        f.push({ label: qsTr("Host link"), value: stage.hostLinkText, colour: stage._onBg })

                    // Happening right now, so it replaces the "on launch" promise rather than
                    // sitting beside it: the switch it was promising is the one under way.
                    if (stage.linkChanging && stage.linkChangeText.length > 0)
                        f.push({ label: stage.linkChangeLabel, value: stage.linkChangeText,
                                 colour: Theme.warning })
                    // Amber, and never green: this has not happened yet.
                    else if (stage.willSwitchLink)
                        f.push({ label: qsTr("On launch"), value: "→ " + stage.linkSwitchText,
                                 colour: Theme.warning })
                    else if (stage.cantSwitchLink)
                        f.push({ label: qsTr("Link match"), value: qsTr("enable it in StreamTweak"),
                                 colour: stage._onBg3 })

                    return f
                }

                delegate: Column {
                    spacing: stage._px(4)

                    Text {
                        text: modelData.label.toUpperCase()
                        color: stage._onBg3
                        font.family: Theme.family
                        font.pixelSize: stage._px(14)
                        font.letterSpacing: stage._u * 1.6
                    }
                    // Full white and semibold, matching the headline numbers in the last-session
                    // panel. These are the card's facts, and they were the only values on it
                    // still drawn at normal weight — which read as secondary next to everything
                    // around them, when they are the opposite.
                    Text {
                        text: modelData.value
                        color: modelData.colour
                        font.family: Theme.family
                        font.pixelSize: stage._px(26)
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        // ── Last session ─────────────────────────────────────────────────────
        /*
         * What StreamTweak's own Dashboard says about the last session, said again here — the
         * one place where knowing it changes a decision, because this is the screen you are on
         * when deciding whether to stream again.
         *
         * No frame and no fill: it is part of the card, not a box resting on it. A bordered
         * panel made the same information read as a second, foreign surface — which is exactly
         * how it looked, because the design it came from is StreamTweak's, not this one's.
         * Sitting directly on the backdrop it becomes the right-hand half of one card.
         *
         * It follows the card's own grid — top aligned with the chip row, right margin the
         * same as the field row — and the card's own reading tones (_onBg / _onBg2 / _onBg3),
         * so it flips with a light backdrop like everything else instead of staying
         * near-white and vanishing.
         *
         * ⚠️ The scrim is at its thinnest on this side, deliberately, so a host picture gets
         * to be a picture. With no fill of its own the panel now leans on that: if a bright
         * photo ever makes it hard to read, the fix is the scrim's right-hand stop, not a box
         * around this.
         */
        Item {
            id: lastPanel
            visible: stage._hasLastSession
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: stage._px(34)
            anchors.rightMargin: stage._px(38)
            width: stage._px(560)
            height: lastCol.implicitHeight

            readonly property var _s: stage.lastSession

            // -1 is the host saying "never measured", which is not the same as zero — a client
            // that printed 0 ms would be inventing a result it was explicitly not given.
            function _num(v, decimals) {
                return (v === undefined || v === null || v < 0)
                       ? "—" : Number(v).toFixed(decimals)
            }

            Column {
                id: lastCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: stage._px(18)

                // A badge, built to the same recipe as the state chips on the left: same
                // height, radius, border and typography. That is what makes the two sides read
                // as one row of the card rather than two blocks that happen to start at the
                // same height — and it makes them line up by construction, instead of by
                // matching a bare caption's baseline to a box's centre.
                //
                // No status dot: the chips carry one because they report a state that can be
                // good or bad. This reports when, and a coloured dot would imply a verdict the
                // grade below is already giving.
                Rectangle {
                    // Everything in this panel hangs off the card's right edge: it is the
                    // right-hand block of the card, and ragged-left is what makes it read as
                    // one column instead of three rows that happen to be over here.
                    anchors.right: parent.right
                    height: stage._px(30)
                    width: agoRow.implicitWidth + stage._px(26)
                    radius: stage._px(6)
                    color: "transparent"
                    border.color: Theme.line
                    border.width: 1

                    Row {
                        id: agoRow
                        anchors.centerIn: parent
                        spacing: stage._px(9)

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("LAST SESSION")
                            color: stage._onBg2
                            font.family: Theme.family
                            font.pixelSize: stage._px(15)
                            font.weight: Font.DemiBold
                            font.letterSpacing: stage._u * 1.1
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: text.length > 0
                            text: lastPanel._s.ago !== undefined
                                  ? String(lastPanel._s.ago).toUpperCase() : ""
                            color: stage._onBg3
                            font.family: Theme.family
                            font.pixelSize: stage._px(15)
                            font.weight: Font.DemiBold
                            font.letterSpacing: stage._u * 1.1
                        }
                    }
                }

                // Grade and duration, as two pills — the grade in its own colour, which is the
                // one thing on this panel worth reading from across the room.
                Row {
                    anchors.right: parent.right
                    spacing: stage._px(10)

                    // The grade keeps its tinted pill: it is a verdict, and the colour is the
                    // whole point of it. The duration does not — a bordered box around a plain
                    // fact was the second frame this panel did not need.
                    Rectangle {
                        visible: lastPanel._s.hasGrade === true
                                 && lastPanel._s.grade !== undefined && lastPanel._s.grade !== ""
                        width: gradeText.implicitWidth + stage._px(30)
                        height: stage._px(42)
                        radius: stage._px(8)
                        color: Qt.rgba(gradeText.color.r, gradeText.color.g, gradeText.color.b, 0.16)

                        Text {
                            id: gradeText
                            anchors.centerIn: parent
                            text: lastPanel._s.grade !== undefined ? lastPanel._s.grade : ""
                            color: (lastPanel._s.gradeColor !== undefined
                                    && lastPanel._s.gradeColor !== "") ? lastPanel._s.gradeColor
                                                                       : stage._onBg
                            font.family: Theme.family
                            font.pixelSize: stage._px(21)
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: lastPanel._s.duration !== undefined && lastPanel._s.duration !== ""
                        text: lastPanel._s.duration !== undefined ? lastPanel._s.duration : ""
                        color: stage._onBg2
                        font.family: Theme.family
                        font.pixelSize: stage._px(21)
                        leftPadding: stage._px(4)
                    }
                }

                // The three headline numbers.
                Row {
                    id: metricRow
                    anchors.right: parent.right
                    spacing: stage._px(34)

                    Repeater {
                        model: [
                            { value: lastPanel._num(lastPanel._s.rttMs, 0),     unit: "ms",
                              caption: lastPanel._num(lastPanel._s.rttPeakMs, 0) === "—"
                                       ? qsTr("RTT") : lastPanel._num(lastPanel._s.rttPeakMs, 0) + " " + qsTr("peak") },
                            { value: lastPanel._num(lastPanel._s.hostLatMs, 1), unit: "ms",
                              caption: qsTr("Host lat.") },
                            { value: lastPanel._num(lastPanel._s.dropsPct, 1),  unit: "%",
                              caption: qsTr("Drops") }
                        ]

                        delegate: Column {
                            spacing: stage._px(4)

                            Row {
                                spacing: stage._px(4)
                                // Full white and semibold, the same treatment the address
                                // and link values now get: these are the card's numbers,
                                // and they should read as one family.
                                Text {
                                    anchors.bottom: parent.bottom
                                    text: modelData.value
                                    color: stage._onBg
                                    font.family: Theme.family
                                    font.pixelSize: stage._px(46)
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: stage._px(7)
                                    visible: modelData.value !== "—"
                                    text: modelData.unit
                                    color: stage._onBg3
                                    font.family: Theme.family
                                    font.pixelSize: stage._px(18)
                                }
                            }
                            Text {
                                text: modelData.caption.toUpperCase()
                                color: stage._onBg3
                                font.family: Theme.family
                                font.pixelSize: stage._px(14)
                                font.letterSpacing: stage._u * 1.6
                            }
                        }
                    }
                }

                // The covers, on a row of their own under the figures.
                //
                // They used to sit beside the numbers, which meant three of them only fitted by
                // shrinking — and a cover that changes size depending on how many there are is a
                // cover you cannot compare across sessions. Given the full width of the panel
                // they all draw at the same size whether there is one or three, and the block
                // still ends on the same right edge as everything above it.
                Row {
                    id: coverStrip
                    anchors.right: parent.right
                    spacing: stage._px(8)
                    height: stage._px(150)
                    visible: _games.length > 0

                    // All three the host is willing to send.
                    readonly property var _games:
                        (lastPanel._s.games !== undefined) ? lastPanel._s.games.slice(0, 3) : []

                    Repeater {
                        model: coverStrip._games

                            delegate: Item {
                                width: stage._px(100)
                                height: stage._px(150)

                                // Usually the full-size artwork already cached for this host
                                // (600x900), resolved by name in ComputerModel; the host's
                                // inline thumbnail only when there is nothing cached for it.
                                //
                                // ⚠️ mipmap, because the usual case is now MINIFYING rather
                                // than magnifying: 900 down to somewhere between 144 and 288
                                // physical pixels depending on the panel. Plain bilinear
                                // aliases at that ratio, and mipmap is what stops it.
                                Image {
                                    id: cover
                                    anchors.fill: parent
                                    source: modelData.cover !== undefined ? modelData.cover : ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    smooth: true
                                    mipmap: true
                                    visible: false
                                }

                                MultiEffect {
                                    anchors.fill: parent
                                    visible: cover.status === Image.Ready
                                    source: cover
                                    maskEnabled: true
                                    maskSource: coverMask
                                }

                                Item {
                                    id: coverMask
                                    anchors.fill: parent
                                    layer.enabled: true
                                    visible: false
                                    Rectangle { anchors.fill: parent; radius: stage._px(8) }
                                }

                                // A game with no cover still has a name, and a blank rectangle
                                // would say less than the name does.
                                Rectangle {
                                    anchors.fill: parent
                                    visible: cover.status !== Image.Ready
                                    radius: stage._px(8)
                                    color: "#18ffffff"
                                    border.color: Theme.line
                                    border.width: 1

                                    Text {
                                        anchors.fill: parent
                                        anchors.margins: stage._px(8)
                                        text: modelData.name !== undefined ? modelData.name : ""
                                        color: stage._onBg3
                                        font.family: Theme.family
                                        font.pixelSize: stage._px(14)
                                        wrapMode: Text.Wrap
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }

                        // "+2" when the session credited more games than the strip shows.
                        // Deliberately quiet and outside the tiles: a footnote saying something
                        // was left out, not a fourth game. The count comes from the host —
                        // `games` is the capped list, so it could never say this by itself.
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: text.length > 0
                            text: {
                                var total = lastPanel._s.gamesTotal !== undefined
                                            ? lastPanel._s.gamesTotal : 0
                                var extra = total - coverStrip._games.length
                                return extra > 0 ? "+" + extra : ""
                            }
                            color: stage._onBg3
                            font.family: Theme.family
                            font.pixelSize: stage._px(16)
                            font.weight: Font.DemiBold
                        }
                    }
            }
        }

        // ── The add panel's body ─────────────────────────────────────────────
        Row {
            anchors.top: nameBlock.bottom
            anchors.left: parent.left
            anchors.topMargin: stage._px(30)
            anchors.leftMargin: stage._px(38)
            spacing: stage._px(14)
            visible: stage.addMode && stage.discovering

            BusyIndicator {
                anchors.verticalCenter: parent.verticalCenter
                width: stage._px(26); height: width
                running: visible
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Hosts found on the network appear as tabs above.")
                color: stage._onBg3
                font.family: Theme.family
                font.pixelSize: stage._px(16)
            }
        }

        // ── The action row ───────────────────────────────────────────────────
        Row {
            id: actionRow
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: stage._px(38)
            anchors.bottomMargin: stage._px(30)
            spacing: stage._px(12)

            // Dimmed while the adapter renegotiates: launching into a link that is still
            // coming back up is exactly the failure this whole feature exists to avoid.
            opacity: stage.linkChanging ? 0.4 : 1.0
            enabled: !stage.linkChanging
            Behavior on opacity {
                enabled: !Theme.reduceAnimations
                NumberAnimation { duration: 160 }
            }

            Repeater {
                model: stage.actions

                delegate: Rectangle {
                    id: actionBtn

                    // Focus is drawn only when this zone owns it AND the user is not driving
                    // with the mouse — otherwise the pad ring and the hover ring light up two
                    // different buttons at once, which is the bug the old grid had to guard
                    // against in five places.
                    readonly property bool _focused:
                        stage.zoneActive && index === stage.actionIndex && !stage.pointerMode
                    readonly property bool _hovered: actionMouse.containsMouse && stage.pointerMode
                    readonly property bool _lit: _focused || _hovered

                    height: stage._px(58)
                    width: actionContent.implicitWidth + stage._px(54)
                    radius: stage._px(10)
                    opacity: modelData.disabled ? 0.4 : 1.0

                    color: !_lit                ? "#14ffffff"
                         : modelData.danger     ? Theme.danger
                         :                        Theme.accent
                    border.width: _focused ? 2 : 1
                    border.color: !_lit            ? Theme.lineHigh
                                : modelData.danger ? Theme.danger
                                :                    Theme.accent

                    // Costs nothing and is the difference between the focus moving and the
                    // focus teleporting. Skipped when the user asked for less movement.
                    Behavior on scale {
                        enabled: !Theme.reduceAnimations
                        NumberAnimation { duration: 130; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
                    }
                    scale: _focused && !Theme.reduceAnimations ? 1.04 : 1.0

                    Row {
                        id: actionContent
                        anchors.centerIn: parent
                        spacing: stage._px(10)

                        // The A badge lives here and only here — A was taken out of the status
                        // bar so the mapping is stated once. It follows the focus rather than
                        // sitting on the first button, because A activates whatever is
                        // focused, and only the focused button can honestly claim it.
                        Rectangle {
                            id: actionBadge
                            // ⚠️ Follows the device in hand like every other prompt: "Enter"
                            // once the keyboard or mouse is used, the controller's own button
                            // otherwise. It used to be the literal letter "A" always, so
                            // coming back from Settings with the keyboard still offered a
                            // controller button — and on a DualSense it named the wrong one.
                            // Deliberately NOT a PadGlyph: the vendor SVGs carry their own
                            // colours (the Xbox A is green) and this badge is meant to sit in
                            // the button's own palette — a dark disc with the accent letter on
                            // the lit one. It still follows the device in hand, which is the
                            // part that was broken: "Enter" once the keyboard is in use.
                            readonly property bool   _padMode: InputHints.padActive
                            // ⚠️ Which character this is depends on the vendor, and it is
                            // resolved by SDL POSITION exactly like PadGlyph maps buttonKey
                            // "A": the south face button is ✕ on PlayStation and B on
                            // Nintendo, which swaps A/B against Xbox. `controllerType`
                            // already returns the set forced in Settings → Shortcuts when
                            // there is one, so the preference is not read a second time.
                            // Direct ternary rather than a helper call, for the same reason
                            // as PadGlyph._resolved: a function-call binding can fail to
                            // register the dependency under compiled QML and go stale on a
                            // vendor switch.
                            readonly property string _padSet: SdlGamepadKeyNavigation.controllerType
                            readonly property string _padLetter:
                                _padSet === "ps"     ? "✕"
                              : _padSet === "switch" ? "B"
                              :                        "A"
                            readonly property string _letter:
                                !actionBtn._focused ? ""
                                                    : (_padMode ? _padLetter : qsTr("Enter"))

                            anchors.verticalCenter: parent.verticalCenter
                            visible: _letter.length > 0
                            width: _padMode ? stage._px(26)
                                            : Math.max(stage._px(26), badgeText.implicitWidth + stage._px(14))
                            height: stage._px(26)
                            radius: _padMode ? width / 2 : stage._px(7)
                            color: actionBtn._lit
                                   ? (modelData.danger ? "#1a0505" : Theme.onAccent)
                                   : "#26ffffff"

                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text: actionBadge._letter
                                color: actionBtn._lit
                                       ? (modelData.danger ? Theme.danger : Theme.accent)
                                       : stage._onBg
                                font.family: Theme.family
                                font.pixelSize: stage._px(actionBadge._padMode ? 14 : 13)
                                font.weight: Font.Bold
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: actionBtn._lit
                                   ? (modelData.danger ? "#1a0505" : Theme.onAccent)
                                   : Theme.text
                            font.family: Theme.family
                            font.pixelSize: stage._px(19)
                            font.weight: actionBtn._lit ? Font.Bold : Font.Normal
                        }
                    }

                    MouseArea {
                        id: actionMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: modelData.disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.disabled) return
                            stage.actionIndex = index
                            stage.activated(modelData.kind)
                        }
                    }
                }
            }
        }

    }
}
