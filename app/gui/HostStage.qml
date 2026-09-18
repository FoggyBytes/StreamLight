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

    /*
     * ── The backdrop, baked into one ramp when there is no picture ───────────────────────
     *
     * ⚠️ Two stacked ramps used to draw this: the host's colour pair, and a scrim over it for
     * the text. Both horizontal, both dithered — and the card banded anyway. The measurement
     * says why, and it is not the dither: across the LEFT 60% of the card the base ramp offers
     * 48 distinct values and the composite offers EIGHT. The scrim's alpha throws away forty
     * of them, so what looks like a gradient there is really a flat field with six or eight
     * accidental steps in it, each a couple of hundred pixels wide. No dithering can invent
     * levels the 8-bit output of a blend has already discarded — an ordered tile can only
     * feather eight pixels of each step, and 1-D error diffusion was tried and measured
     * (48 px of flat run down to 27) and still did not show.
     *
     * So the two ramps become one, which is possible because both run along x: their composite
     * is a function of x alone and can be written as a single set of stops. And the part that
     * was nearly flat is made EXACTLY flat, because a region spanning six levels over fourteen
     * hundred pixels is not a gradient — it is a flat colour that occasionally jumps, and the
     * jumps are the artefact. One quantisation, one dither, and two fewer effect passes.
     *
     * The stops below follow the old composite's own values, so the card looks like it did
     * apart from the banding: flat at what the blend produced around 30% across, then falling
     * away through the values it produced at 72% and beyond.
     *
     * ⚠️ Only when there is no picture. Over a picture the scrim is veiling an image and has to
     * stay a separate layer — that case is a different problem, and the colour bias inside the
     * provider is what addresses it.
     */
    function _lerp(a, b, f) { return a + (b - a) * f }

    function _baseAt(p) {
        var f = Qt.tint(backdropFrom, Qt.rgba(0, 0, 0, 0))
        var t = Qt.tint(backdropTo,   Qt.rgba(0, 0, 0, 0))
        var e = Qt.tint("#05080a",     Qt.rgba(0, 0, 0, 0))
        if (p <= 0.46) {
            var k = p / 0.46
            return [ _lerp(f.r, t.r, k), _lerp(f.g, t.g, k), _lerp(f.b, t.b, k) ]
        }
        if (p <= 0.90) {
            var k2 = (p - 0.46) / 0.44
            return [ _lerp(t.r, e.r, k2), _lerp(t.g, e.g, k2), _lerp(t.b, e.b, k2) ]
        }
        return [ e.r, e.g, e.b ]
    }

    function _scrimAlphaAt(p) {
        if (p <= 0.30) return _lerp(0xb0 / 255, 0x80 / 255, p / 0.30)
        if (p <= 0.62) return _lerp(0x80 / 255, 0x30 / 255, (p - 0.30) / 0.32)
        return _lerp(0x30 / 255, 0.0, (p - 0.62) / 0.38)
    }

    // What the two layers produced at p, as one colour.
    function _compositeAt(p) {
        var b = _baseAt(p)
        var a = _scrimAlphaAt(p)
        var s = Qt.tint("#05080a", Qt.rgba(0, 0, 0, 0))
        return Qt.rgba(s.r * a + b[0] * (1 - a),
                       s.g * a + b[1] * (1 - a),
                       s.b * a + b[2] * (1 - a), 1)
    }

    // Re-derived whenever the host's colours change, which is what a binding on the two
    // properties gives us — a function result assigned once would freeze on the first host.
    readonly property color _flatColour: _compositeAt(0.30)
    readonly property var _bakedStops: [
        { pos: 0.00, color: _flatColour },
        { pos: 0.55, color: _flatColour },
        { pos: 0.72, color: _compositeAt(0.72) },
        { pos: 0.90, color: _compositeAt(0.92) },
        { pos: 1.00, color: _compositeAt(1.00) }
    ]

    // Optional picture behind the gradient. Drawn sharp and at full saturation: the earlier
    // version blurred and desaturated it on the theory that this is what makes a picture read
    // as a background, and on screen that theory was wrong — it just looked like a bad copy of
    // the picture. What actually keeps the text readable is the scrim below, which is dense
    // exactly where the name and the fields are and gone by the right-hand side, so the
    // picture gets to be itself over most of the card.
    property string backdropImage: ""

    // How opaque the card is, 0..1 (6.0.0). Chosen per host in StageBackgroundDialog and
    // stored with the rest of its backdrop; ComputerModel supplies the default and the floor.
    property real backdropOpacity: 0.90

    // ── Last played (5.7.0) ──────────────────────────────────────────────────
    /*
     * The game this client last streamed on this host, from ComputerModel::lastPlayedFor().
     *
     * ⚠️ Nothing here comes from the host. Until 5.6.1 this panel showed the host's last
     * *session*, fetched over the bridge — which meant it described whatever had streamed,
     * possibly from another client, and vanished entirely without StreamTweak. It now
     * describes what YOU played, from a record kept on this machine, and works against a
     * plain Sunshine host.
     *
     * An empty map means draw nothing at all: never streamed here, record reset, or the game
     * is gone from the host's app list. There is no empty state — the card simply goes back
     * to what it looks like without one.
     */
    property var lastPlayed: ({})

    readonly property bool _hasLastPlayed:
        !addMode && lastPlayed && lastPlayed.name !== undefined && lastPlayed.name !== ""

    // ── Now streaming (6.0.0) ────────────────────────────────────────────────
    /*
     * What the host is streaming right now, from ComputerModel::runningAppFor(): name and
     * artwork, or an empty map.
     *
     * ⚠️ It TAKES the Last played slot rather than joining it. While a session is up, "Play
     * again" on the last game you closed is the wrong offer — the host is holding something
     * else open, and the thing worth one press is getting back into it. Same block, same
     * width, same button position: nothing on the card moves when a session starts or ends,
     * and the layout inherits every scaling rule the Last played block already had.
     */
    property var runningApp: ({})

    readonly property bool _hasRunning:
        !addMode && runningApp && runningApp.name !== undefined && runningApp.name !== ""

    // The right-hand block is on screen: the running session, or failing that the last game.
    readonly property bool _hasPanel: _hasRunning || _hasLastPlayed

    // Neither, yet (6.0.0): one quiet line stands where the block's button will be — see
    // lastPlayedHint. Never in add mode, which has no host to have played anything on. No
    // button with it: _panelActionReachable stays on _hasPanel, so the pad does not stop on
    // an action that has nothing to act on.
    readonly property bool _panelPlaceholder: !addMode && !_hasPanel

    /*
     * ── The card is the LEFT part of the stage (6.0.0) ───────────────────────────
     *
     * It used to fill the stage, with the game block drawn inside it on the right. That left
     * the waves of the app's floor visible only in the margins around one big card, so
     * the card now takes a little over half, and the game block stands OUTSIDE it, directly
     * on the floor — see lastPanel.
     *
     * ⚠️ The game block did not move. It keeps the exact position, size and alignment it had
     * inside the full-width card: its right edge and top are still measured from the stage,
     * which is where the full card's edges were, and its button still sits on the line of the
     * action row. Only its parent changed. The card is what got narrower, and the left-hand
     * content reflows to fit (see nameFlow, subLineRow and fieldRow).
     *
     * 55% and not 50: the left side carries the name, the profile, the next launch's settings
     * and up to five fields, and at half the width they ran out of room too often. What is left
     * still clears the game block (380 wide, 46 from the edge) with a clear gap.
     */
    readonly property int _cardW: Math.round(width * 0.55)

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
    // ⚠️ Without a picture this is now the flat colour the ramp actually paints there, not a
    // blend recomputed here — the two used to be worked out separately and could disagree.
    // With one, the picture is unknowable and "#202020" stands in for it, as before.
    readonly property color _bgOpaque:
        backdropImage !== "" ? Theme.blend("#202020", "#e605080a")
                             : _flatColour

    /*
     * ⚠️ And then laid over the app's FLOOR at the card's opacity (6.0.0). Since the card is
     * translucent, what is behind the text is not the backdrop alone but the backdrop at
     * backdropOpacity over the floor — a darker thing. Judging the text against the opaque
     * backdrop could choose dark text for a light colour and then draw it on something much
     * darker than it was chosen for. The floor is AmbientBackground's ramp around the card's
     * middle height, which is where the name and the fields sit.
     */
    readonly property color _floorUnderCard:
        Qt.tint("#151515", Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.05))
    readonly property color _bgUnderText:
        Theme.blend(_floorUnderCard, Qt.rgba(_bgOpaque.r, _bgOpaque.g, _bgOpaque.b, backdropOpacity))

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

        // (Play again / Resume is NOT in here. It is drawn under the cover on the other side of
        //  the card — see panelActionBtn — because a verb down here had the game it refers to a
        //  card's width away, and read as generic. It is still one stop past the last button
        //  in this row: _panelActionReachable and moveAction below.)

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

    // One stop past the last button is the block's own button — Resume or Play again — when
    // the card is showing the block. Keeping it on the same index line rather than making it
    // a zone of its own is what lets Right walk into it and Left walk back out with no new key
    // handling: it sits to the right of the buttons, which is what Right already means here.
    readonly property bool _panelActionReachable: _hasPanel
    readonly property int _maxActionIndex: actions.length - (_panelActionReachable ? 0 : 1)

    // What that button does, decided once so the pad, the mouse and the label cannot disagree.
    readonly property string _panelActionKind: _hasRunning ? "resume" : "continue"

    /*
     * Shown-and-greyed rather than taken away, which is what the rest of the card does: an
     * unreachable host still shows "Open". The panel this button belongs to — the cover, the
     * title, the figures — stays drawn when the host goes offline, so removing only the
     * button left everything describing the game on screen and nothing to do about it.
     *
     * `linkChanging` is in here for the same reason it is on the action row: launching into a
     * link that is still coming back up is the failure the whole feature exists to avoid.
     */
    readonly property bool _panelActionEnabled:
        _panelActionReachable && online && paired && !statusUnknown && !linkChanging

    // ⚠️ On the LIMIT, not on `actions`. The focus can be sitting on Play again when the
    // button goes away without the button row changing at all — the record is reset from the
    // per-game panel, or the session behind Resume ends with no last game to fall back on —
    // and clamping only on actionsChanged left the focus on something
    // invisible, so no button on the card looked focused at all.
    on_MaxActionIndexChanged: if (actionIndex > _maxActionIndex)
                                  actionIndex = Math.max(0, _maxActionIndex)
    onActionsChanged: if (actionIndex > _maxActionIndex) actionIndex = Math.max(0, _maxActionIndex)

    // Walks the action row. Returns false at the ends so the caller can decide what a
    // further press means (today: nothing — the row does not wrap).
    function moveAction(dir) {
        var next = actionIndex + dir
        if (next < 0 || next > _maxActionIndex) return false
        actionIndex = next
        return true
    }

    // Fires the focused action. Disabled entries are silently ignored rather than skipped
    // during navigation: an unreachable host still shows "Open", greyed, which says more
    // than a row that quietly loses a button.
    function activateFocused() {
        /*
         * ⚠️ The link gate lives HERE, not on the action row, and that is the whole reason
         * this line exists. `enabled: false` up there stops the mouse and nothing else, so
         * the D-pad went on firing Open into an adapter that was still renegotiating — the
         * buttons were grey, dead to the click, and a press of A launched anyway.
         */
        if (linkChanging) return

        // Resume or Play again, one past the buttons.
        if (_panelActionReachable && actionIndex === actions.length) {
            if (!_panelActionEnabled) return
            stage.activated(_panelActionKind)
            return
        }
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
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: stage._cardW
        radius: stage._px(16)

        /*
         * How opaque the card's backdrop is (6.0.0). A shade under 1 so the waves of the
         * app's floor shows through — without it the card covers most of Home and the flow
         * is only visible in the margins. The user sets it per host (stage.backdropOpacity,
         * from StageBackgroundDialog); the floor of that range is ComputerModel's, and the note
         * on StageOpacityMin says why it is not lower.
         *
         * ⚠️ Applied to the backdrop GROUP, flattened — see backdropGroup — never to each
         * layer. The ramp and the picture sit on top of each other: at 0.85 each they would
         * cover 0.98 together, and the card would look almost exactly as opaque as before.
         */
        readonly property real backdropOpacity: stage.backdropOpacity

        // Transparent behind the backdrop, which covers the whole card and is thinned on its
        // own. In add mode there is no backdrop, so the plain fill takes the same opacity.
        color: stage.addMode ? Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, card.backdropOpacity)
                             : "transparent"
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
                // With a picture this is only what shows while the picture loads, so it stays
                // the plain colour pair. Without one it is the whole backdrop, and it carries
                // the scrim baked in — see the note on _bakedStops.
                stops: stage.backdropImage !== ""
                       ? [ { pos: 0.0,  color: stage.backdropFrom },
                           { pos: 0.46, color: stage.backdropTo },
                           { pos: 0.90, color: "#05080a" } ]
                       : stage._bakedStops
            }
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
            /*
             * Cropped, and always from the CENTRE (6.0.0). Since the card became the left part
             * of the stage it is far narrower than the landscape pictures people pick, so the
             * crop now takes a lot off the sides — and the subject of a picture is almost always
             * in the middle. Centre is also Qt's default for PreserveAspectCrop; it is written
             * out because this is now a decision, not a default: an alignment added here later
             * for some other reason would quietly cut the subject in half.
             */
            fillMode: Image.PreserveAspectCrop
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            asynchronous: true
            // Drawn by backdropGroup below; this element is only the texture behind it.
            visible: false
        }

        // ── 3. The scrim ─────────────────────────────────────────────────────
        // Not decoration: without it a light backdrop and white text meet and the text loses.
        // Densest on the left where the name and the fields are, gone by the right edge so the
        // host's colour still reads as a colour. Over a picture it starts denser and clears
        // sooner — reaching fully transparent at the right edge matters, the 19% black that
        // used to sit there is what made artwork look washed out. Its alpha ramp is dithered
        // too: a staircase in alpha bands just as visibly as one in a colour channel.
        // ⚠️ Only over a picture now. Without one the scrim is baked into the ramp above, and
        // drawing it here as well would veil the card twice. Its alpha values are kept here
        // verbatim because _scrimAlphaAt() reproduces them — if these change, that changes.
        Item {
            id: scrimLayer
            anchors.fill: parent
            // ⚠️ Gated as well, not just the effect below. An invisible item with layer.enabled
            // still keeps its FBO — that is exactly how these mask sources work — so leaving it
            // on would go on rendering a scrim nobody samples on every card without a picture,
            // which is most of them.
            layer.enabled: stage.backdropImage !== ""
            visible: false

            DitheredGradient {
                anchors.fill: parent
                orientation: Qt.Horizontal
                stops: [
                    { pos: 0.0,  color: "#e605080a" },
                    { pos: 0.30, color: "#b005080a" },
                    { pos: 0.62, color: "#4005080a" },
                    { pos: 1.0,  color: "#0005080a" }
                ]
            }
        }

        /*
         * ── The three passes, drawn as ONE layer (6.0.0) ─────────────────────
         *
         * The effects that draw the ramp, the picture and the scrim used to sit each beside
         * its source. They are gathered here so card.backdropOpacity can apply to what they
         * make TOGETHER: `layer.enabled` flattens the group first, then the opacity thins the
         * result once. Without the layer, Qt multiplies the opacity into each child and they
         * are blended one over the other at 0.85 apiece — ramp and picture stacked that way
         * are 0.98 opaque, and the flow behind would barely show at all.
         *
         * ⚠️ Only the EFFECTS moved. Their sources (baseRampLayer, backdropPicture, scrimLayer)
         * stay where they were: they are invisible textures, their order means nothing, and
         * backdropPicture in particular must remain sampled directly by its own effect — see
         * the long note above it for the 5.1.0 regression that grouping it caused.
         *
         * The layer costs one texture per card, re-rendered only when the card itself changes;
         * the flow moving behind it is blended against that texture, not redrawn into it.
         */
        Item {
            id: backdropGroup
            anchors.fill: parent
            visible: !stage.addMode
            opacity: card.backdropOpacity
            layer.enabled: card.backdropOpacity < 1

            MultiEffect {
                anchors.fill: parent
                source: baseRampLayer
                autoPaddingEnabled: false
                maskEnabled: true
                maskSource: cardMask
            }

            MultiEffect {
                anchors.fill: parent
                visible: stage.backdropImage !== ""
                source: backdropPicture
                autoPaddingEnabled: false
                maskEnabled: true
                maskSource: cardMask
            }

            MultiEffect {
                anchors.fill: parent
                visible: stage.backdropImage !== ""
                source: scrimLayer
                autoPaddingEnabled: false
                maskEnabled: true
                maskSource: cardMask
            }
        }

        // ── Chips ────────────────────────────────────────────────────────────
        /*
         * ⚠️ Shrunk to fit, never wrapped and never cut (6.0.0).
         *
         * Since the card became the left part of the stage, the full set — state, StreamTweak,
         * Tailscale and a link chip during a wake — can be wider than the card, and the card
         * clips: the last chip was simply cut off at its edge. Which chips appear, and when,
         * is deliberate (Marcello, 18/09/2026: the behaviour stays), so what gives is their
         * SIZE, and only by as much as it has to. `_fit` is 1 whenever the row fits, which is
         * the usual case, and below 1 only for the moments that would otherwise overflow.
         *
         * By `scale` on the whole row, not by re-deriving each font and padding: the chips
         * keep their exact proportions, and the row's implicit width — what `_fit` is measured
         * against — is not affected by its own scale, so there is no binding loop. The box
         * around it takes the SCALED size, so the name below moves up with the row instead of
         * keeping a gap the row no longer fills.
         *
         * Every resolution is covered by the same rule: the chips and the card both grow with
         * _u, so their ratio only changes where _u is clamped, and _fit absorbs exactly that.
         */
        Item {
            id: chipBox
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: stage._px(34)
            anchors.leftMargin: stage._px(38)
            visible: !stage.addMode

            readonly property real _room: card.width - stage._px(38) * 2
            readonly property real _fit: Math.min(1, _room / Math.max(1, chipRow.implicitWidth))
            width: chipRow.implicitWidth * _fit
            height: chipRow.implicitHeight * _fit

        Row {
            id: chipRow
            scale: chipBox._fit
            transformOrigin: Item.TopLeft
            spacing: stage._px(9)

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
                            font.pixelSize: stage._px(Theme.fontBody)
                            font.weight: Font.DemiBold
                            font.letterSpacing: stage._u * 1.1
                        }
                    }
                }
            }
        }
        }

        // ── Name + subtitle ──────────────────────────────────────────────────
        Column {
            id: nameBlock
            anchors.top: stage.addMode ? parent.top : chipBox.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: stage._px(stage.addMode ? 56 : 20)
            anchors.leftMargin: stage._px(38)
            // No room reserved for the game block any more: since 6.0.0 it stands outside the
            // card (see _cardW), so the card's whole width is the text's.
            anchors.rightMargin: stage._px(38)
            spacing: stage._px(8)

            // The name, and the active profile beside it.
            //
            // The profile used to be four words into a grey sentence underneath, which is not
            // where you look — and it is the single setting that changes what every launch
            // from this host will look like. At the name's own height, in the accent, it
            // becomes the second thing read instead of the last.
            //
            // ⚠️ A Flow since 6.0.0, not a Row. In the narrower card a long host name and the
            // profile cluster no longer always fit on one line, and a Row could only elide the
            // NAME to make room — the one word on the card that must never be cut. The Flow
            // keeps them side by side when they fit and puts the profile on the next line when
            // they do not. Each sits in a box of the line's height and centres itself in it,
            // because a Flow places its children by their tops and the two are not equally tall.
            Flow {
                id: nameFlow
                width: parent.width
                spacing: stage._px(18)

                readonly property real _lineH: Math.max(nameText.implicitHeight,
                                                        stage._hasProfiles ? profileCluster.implicitHeight : 0)

                Item {
                    width: nameText.width
                    height: nameFlow._lineH

                Text {
                    id: nameText
                    anchors.verticalCenter: parent.verticalCenter
                    // Its own line if need be, and ellipsis only past the WHOLE card width —
                    // never to make room for the profile, which wraps instead.
                    width: Math.min(implicitWidth, nameFlow.width)
                    text: stage.addMode ? qsTr("Add a host") : stage.hostName
                    color: stage._onBg
                    font.family: Theme.family
                    font.pixelSize: stage._px(68)
                    font.weight: Font.Bold
                    font.letterSpacing: -stage._u * 2.4
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
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
                // The Flow's box for the cluster — see the note on nameFlow. Gated on the same
                // condition as the cluster, NOT on the cluster's own `visible`: that one is
                // inherited from this box, and reading it here would be a binding on itself.
                Item {
                    visible: stage._hasProfiles
                    width: profileCluster.implicitWidth
                    height: nameFlow._lineH

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
                        /*
                         * Dark glass under an accent border and accent text (18/09/2026,
                         * option B of three). It was an accent TINT under accent text — the
                         * same hue on both, so over the card's picture the two met at almost
                         * the same brightness (≈1:1). Raising the tint (0.18 → 0.28 → 0.40)
                         * and darkening the text only moved them together. The page's own
                         * near-black at 80% keeps the text about 9–10:1 on any picture and
                         * any accent, and leaves the accent as the badge's mark rather than
                         * a block of colour competing with the host's name.
                         */
                        color: Qt.rgba(Theme.ground.r, Theme.ground.g, Theme.ground.b, 0.80)
                        border.color: Theme.accent
                        border.width: Math.max(1, stage._px(2))

                        Text {
                            id: profileBadgeText
                            anchors.centerIn: parent
                            text: stage.activeProfileSlot >= 0 && stage.activeProfileName.length > 0
                                      ? stage.activeProfileName : qsTr("Global")
                            // The full accent, on the dark glass above.
                            color: Theme.accent
                            font.family: Theme.family
                            // Below the host name, clearly above everything else: this is the
                            // second-most important thing on the card, not the first.
                            // 34 since 6.0.0 (was 40): in the narrower card it competed with
                            // the name for the same line.
                            font.pixelSize: stage._px(34)
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
            }

            // The status line, and on the same baseline what the next launch would look
            // like: the same badges the host page shows under its Stream heading, in the same
            // shapes and the same words, because this is the screen you are on when you decide
            // whether to press A.
            //
            // No group label in front of them here. On the host page "STREAM" earns its place
            // by separating that half of the line from the Host half; on this card there is
            // nothing to separate it from, and the status line already introduces them.
            //
            // ⚠️ One line, shrunk to fit only when it has to (6.0.0) — the same rule as the
            // chips at the top of the card, and for the same reason: in the narrower card the
            // status and six badges can run past the edge, and which badges show is decided
            // elsewhere and stays. A Flow that wrapped them onto a second line was tried first
            // and turned down: this is one statement, and it reads as one only on one line.
            // Every child is one _rowH tall and centres its own content, so the row needs no
            // anchors — they would not survive the scale anyway.
            Item {
                id: subLineBox
                width: parent.width
                readonly property real _fit: Math.min(1, width / Math.max(1, subLineRow.implicitWidth))
                height: subLineRow.implicitHeight * _fit

            Row {
                id: subLineRow
                scale: subLineBox._fit
                transformOrigin: Item.TopLeft
                spacing: stage._px(14)

                readonly property int _rowH: stage._px(28)

                Text {
                    height: subLineRow._rowH
                    verticalAlignment: Text.AlignVCenter
                    text: stage._subLine
                    color: stage._onBg2
                    font.family: Theme.family
                    font.pixelSize: stage._px(Theme.fontTitle)
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
                            font.pixelSize: stage._px(Theme.fontSmall)
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
            }
        }

        // ── The data quadrant ────────────────────────────────────────────────
        // Fields appear only when they have something to say, so a plain LAN host shows
        // three and a Tailscale host with a pending link change shows five, without either
        // leaving a hole where the other's data would be.
        //
        // ⚠️ A Flow since 6.0.0, for the same reason as the badge line: in the narrower card
        // an address and a GPU name no longer fit side by side, and a Row simply ran past the
        // card's edge. One spacing for both directions — a Flow has only one — at a value
        // between the old horizontal 46 and what reads as a line gap.
        //
        // ⚠️ Clipped above the action row. The worst case — a Tailscale host with a GPU name
        // and a link change pending, five fields — can take three lines, and at that point a
        // field running under the buttons is worse than one cut off: the buttons are what the
        // user is about to press. It does not happen with the usual three fields.
        Flow {
            id: fieldRow
            anchors.top: nameBlock.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: stage._px(32)
            anchors.leftMargin: stage._px(38)
            anchors.rightMargin: stage._px(38)
            height: Math.max(0, actionRow.y - y - stage._px(16))
            clip: true
            spacing: stage._px(32)
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
                        font.pixelSize: stage._px(Theme.fontSmall)
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
                        font.pixelSize: stage._px(Theme.fontH1)
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

            Spinner {
                anchors.verticalCenter: parent.verticalCenter
                bodySize: stage._px(16)
                running: visible
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                // Wraps inside the card: since 6.0.0 the card is narrower than this sentence
                // was ever measured against.
                width: card.width - stage._px(38) * 2 - stage._px(16) - stage._px(14)
                wrapMode: Text.WordWrap
                text: qsTr("Hosts found on the network appear as tabs above.")
                color: stage._onBg3
                font.family: Theme.family
                font.pixelSize: stage._px(Theme.fontBody)
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

                    /*
                     * ── Colour, and only colour (5.7.0) ──────────────────────
                     * The focus is shown by the fill and the border warming into the accent,
                     * cross-faded rather than switched. Walking the row then reads as one
                     * light travelling along it instead of four buttons blinking in turn.
                     *
                     * ⚠️ Fades, not a ring that expands past the button: an outline blooming
                     * outside its own edges reads as an alarm going off, and on a row of four
                     * it fires every time the stick is nudged.
                     */
                    color: !_lit                ? "#14ffffff"
                         : modelData.danger     ? Theme.danger
                         :                        Theme.accent
                    border.width: _focused ? 2 : 1
                    border.color: !_lit            ? Theme.lineHigh
                                : modelData.danger ? Theme.danger
                                :                    Theme.accent

                    Behavior on color {
                        enabled: !Theme.reduceAnimations
                        ColorAnimation { duration: 140; easing.type: Easing.OutCubic }
                    }
                    Behavior on border.color {
                        enabled: !Theme.reduceAnimations
                        ColorAnimation { duration: 140; easing.type: Easing.OutCubic }
                    }

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
                                            : Math.max(stage._px(26), actionBadgeText.implicitWidth + stage._px(14))
                            height: stage._px(26)
                            radius: _padMode ? width / 2 : stage._px(7)
                            color: actionBtn._lit
                                   ? (modelData.danger ? "#1a0505" : Theme.onAccent)
                                   : "#26ffffff"

                            Text {
                                id: actionBadgeText
                                anchors.centerIn: parent
                                text: actionBadge._letter
                                color: actionBtn._lit
                                       ? (modelData.danger ? Theme.danger : Theme.accent)
                                       : stage._onBg
                                font.family: Theme.family
                                font.pixelSize: stage._px(Theme.fontSmall)
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
                            font.pixelSize: stage._px(Theme.fontTitle)
                            font.weight: actionBtn._lit ? Font.Bold : Font.Normal

                            // Crossed over with the fill behind it, or the label would flip
                            // to dark ink a frame before the accent is there to sit on.
                            Behavior on color {
                                enabled: !Theme.reduceAnimations
                                ColorAnimation { duration: 140; easing.type: Easing.OutCubic }
                            }
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

    // ── Last played ──────────────────────────────────────────────────────
    /*
     * The game you last streamed on this host — name, artwork, hours, sessions.
     *
     * ⚠️ Nothing here comes from the host. Until 5.6.1 this panel showed the host's last
     * *session*, fetched over the bridge, which meant it described whatever had streamed
     * — possibly from another client — and vanished entirely without StreamTweak. It now
     * describes what YOU played, from a record kept on this machine, and works against a
     * plain Sunshine host.
     *
     * ⚠️ The layout is GameFacts, the same component the host page's spotlight uses. It
     * was laid out by hand here first — text beside the cover, no shadow, four figures —
     * against the spotlight's cover-above-centred-shadowed-two-figures, and one idea
     * wearing two faces is what that was. Do not re-implement it here.
     *
     * An empty map means draw nothing at all: never streamed here, record reset, or the
     * game is gone from the host's app list. There is no empty state — the card simply
     * goes back to what it looks like without one.
     *
     * ⚠️ 6.0.0: while the host is streaming, the same block shows THAT instead — see
     * runningApp. Every field below picks between the two, and only those fields: the
     * geometry is one, so the running session is sized, scaled and aligned by exactly the
     * rules that were already tuned for Last played.
     *
     * ⚠️ 6.0.0: a child of the STAGE now, not of the card — the card became the left part of
     * the stage (see _cardW) and this block stands on the floor beside it. Every number below
     * is the one it had inside the full-width card, and `parent` still means an item whose
     * right and top edges are where that card's were, so the block sits exactly where it did.
     */
    GameFacts {
        id: lastPanel
        visible: stage._hasPanel
        u: stage._u

        anchors.right: parent.right
        anchors.rightMargin: stage._px(46)
        anchors.top: parent.top
        anchors.topMargin: stage._px(30)
        width: stage._px(380)

        /*
         * ⚠️ It ends where the Play again button begins, and that button is pinned to the
         * action row on the other side of the card. So this is not centred in the card and
         * must not be: the two sides of the bottom edge line up because BOTH are measured
         * from the same row, and centring this block would put its button a few pixels off
         * from the ones beside it — which is exactly the kind of near-alignment the eye
         * reads as a mistake rather than as a choice.
         */
        availableHeight: panelActionBtn.y - y - stage._px(16)

        // "Streaming now" in the words and with the live dot of the host page's STREAMING
        // tag: the badge is where this block says WHY it shows this game, and a session
        // in progress is a different why from the last one you closed. No figure beside
        // it — the host does not say how long the session has run, nor whose it is.
        // ⚠️ When first, the total second and named as a total (6.0.0). This read
        // "LAST PLAYED FOR 18 H · 2 H AGO", and "for" tied the hours to the last session
        // when they are every session on this host added up.
        badgeMain: stage._hasRunning
                   ? qsTr("Streaming now")
                   : (stage.lastPlayed.ago !== undefined && stage.lastPlayed.ago !== ""
                      ? qsTr("Last played %1").arg(stage.lastPlayed.ago)
                      : qsTr("Last played"))
        badgeMuted: stage._hasRunning ? ""
                    : (stage.lastPlayed.total !== undefined && stage.lastPlayed.total !== ""
                       ? qsTr("%1 total").arg(stage.lastPlayed.total) : "")
        badgeDot: stage._hasRunning ? Theme.accent : "transparent"

        title: stage._hasRunning
               ? stage.runningApp.name
               : (stage.lastPlayed.name !== undefined ? stage.lastPlayed.name : "")
        cover: stage._hasRunning
               ? (stage.runningApp.cover !== undefined ? stage.runningApp.cover : "")
               : (stage.lastPlayed.cover !== undefined ? stage.lastPlayed.cover : "")

        // The title box takes only the lines the name needs, so the cover is biggest on a
        // one-line title and stands back on a three-line one. Safe here and nowhere else:
        // this card shows one game until you change host, or until a session starts or
        // ends on it — see titleFitsContent.
        titleFitsContent: true

        /*
         * Deliberately all empty: title and cover, nothing else.
         *
         * The hours are in the badge above the picture and the age beside them, so
         * repeating them under the title would be saying them twice. The store's mark
         * belongs to the host page, where you are choosing between games and the
         * storefront is one of the things you choose by; here there is one game and the
         * room is worth more to the artwork.
         */
        store: ""
        metaExtra: ""
        played: ""
        sessions: 0
    }

    /*
     * ── Play again / Resume ──────────────────────────────────────────────
     *
     * Resume while the host is streaming (6.0.0), the same word the host page puts on
     * the running game; Play again otherwise.
     *
     * Same body as the buttons in the action row and on the same baseline as them, so the
     * card has one row of controls that happens to span both halves rather than two rows
     * at two heights.
     *
     * ⚠️ It is not IN the action row, and that is the point of the whole arrangement: down
     * there it read as a generic verb next to Open and Profiles, with the game it refers
     * to a card's width away. Under the cover and the title, "Play again" has already been
     * told what it plays.
     *
     * The pad reaches it as one stop past the last button — geometrically it is to the
     * right of them, which is what Right already means here. That was true of the cover
     * too, and the cover was still the wrong target: a picture does not look like a
     * control. A button does.
     */
    Rectangle {
        id: panelActionBtn
        // The same condition the focus chain uses, read from one place: written out twice
        // it would eventually be true for the pad and false for the eye.
        visible: stage._panelActionReachable

        anchors.horizontalCenter: lastPanel.horizontalCenter
        // On the line of the action row, as before. It can no longer ANCHOR there: since 6.0.0
        // the row is inside the card and this button is not, and QML only anchors to a parent
        // or a sibling. The card starts at the stage's top, so the row's own y is the stage's.
        y: card.y + actionRow.y + (actionRow.height - height) / 2

        readonly property bool _focused:
            stage.zoneActive && stage.actionIndex === stage.actions.length && !stage.pointerMode
        readonly property bool _hovered: panelActionMouse.containsMouse && stage.pointerMode
        readonly property bool _lit: _focused || _hovered

        height: stage._px(58)
        width: panelActionRow.implicitWidth + stage._px(54)
        radius: stage._px(10)

        // The same pair the action row carries, on the same terms and with the same
        // fade — it is one row of controls that happens to span both halves of the card,
        // so it has to dim as one. Written here rather than inherited because this button
        // is deliberately NOT inside that Row (see the note above).
        opacity: stage._panelActionEnabled ? 1.0 : 0.4
        enabled: stage._panelActionEnabled
        Behavior on opacity {
            enabled: !Theme.reduceAnimations
            NumberAnimation { duration: 160 }
        }

        color: _lit ? Theme.accent : "#14ffffff"
        border.width: _focused ? 2 : 1
        border.color: _lit ? Theme.accent : Theme.lineHigh

        Behavior on color {
            enabled: !Theme.reduceAnimations
            ColorAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
        Behavior on border.color {
            enabled: !Theme.reduceAnimations
            ColorAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            enabled: !Theme.reduceAnimations
            NumberAnimation { duration: 130; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
        }
        scale: _focused && !Theme.reduceAnimations ? 1.04 : 1.0

        Row {
            id: panelActionRow
            anchors.centerIn: parent
            spacing: stage._px(10)

            // The same prompt badge the action row draws, on the same terms: it appears
            // on the focused button and nowhere else, because A activates what has the
            // focus and only that button can honestly claim it.
            Rectangle {
                id: playBadge
                readonly property bool   _padMode: InputHints.padActive
                readonly property string _padSet: SdlGamepadKeyNavigation.controllerType
                readonly property string _padLetter:
                    _padSet === "ps"     ? "✕"
                  : _padSet === "switch" ? "B"
                  :                        "A"
                readonly property string _letter:
                    !panelActionBtn._focused ? "" : (_padMode ? _padLetter : qsTr("Enter"))

                anchors.verticalCenter: parent.verticalCenter
                visible: _letter.length > 0
                width: _padMode ? stage._px(26)
                                : Math.max(stage._px(26), playBadgeText.implicitWidth + stage._px(14))
                height: stage._px(26)
                radius: _padMode ? width / 2 : stage._px(7)
                color: panelActionBtn._lit ? Theme.onAccent : "#26ffffff"

                Text {
                    id: playBadgeText
                    anchors.centerIn: parent
                    text: playBadge._letter
                    // Theme.text, not the card's reading colour (_onBg): this button stands on
                    // the app's floor now, not on the host's backdrop.
                    color: panelActionBtn._lit ? Theme.accent : Theme.text
                    font.family: Theme.family
                    font.pixelSize: stage._px(Theme.fontSmall)
                    font.weight: Font.Bold
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: stage._hasRunning ? qsTr("Resume") : qsTr("Play again")
                color: panelActionBtn._lit ? Theme.onAccent : Theme.text
                font.family: Theme.family
                font.pixelSize: stage._px(Theme.fontTitle)
                font.weight: panelActionBtn._lit ? Font.Bold : Font.Normal

                Behavior on color {
                    enabled: !Theme.reduceAnimations
                    ColorAnimation { duration: 140; easing.type: Easing.OutCubic }
                }
            }
        }

        MouseArea {
            id: panelActionMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                stage.actionIndex = stage.actions.length
                stage.activated(stage._panelActionKind)
            }
        }
    }

    /*
     * ── Before the first game (6.0.0) ───────────────────────────────────────
     *
     * The badge the block will carry, and one line — centred in the space where the block
     * will stand, level with the middle of the card. It sat on the line where Play again will
     * appear at first, and read as a footnote down there (Marcello, 18/09/2026). The rest of
     * the space is left to the waves, which is what the card got narrower for.
     *
     * ⚠️ Deliberately NOT an outline of the block. That was built first — the cover's exact
     * box, dashed, with "Your last game" at title size — and it was the ugliest thing on the
     * screen: the largest object on that side, holding nothing, so the eye went to it and
     * found a hole. Anything that draws the shape of the missing cover repeats that. Also not
     * a stand-in with content of its own (the host's library, pinned games): the first launch
     * replaces it with Last played for good, so it would be a screen seen once.
     */
    Column {
        id: lastPlayedHint
        visible: stage._panelPlaceholder
        anchors.horizontalCenter: lastPanel.horizontalCenter
        // Centred on the card's height: the block it stands in for spans nearly all of it.
        y: card.y + (card.height - height) / 2
        spacing: stage._px(12)

        // The recipe of GameFacts' badge — height, radius, border, type — so it is the same
        // badge the real block will show in this place.
        Rectangle {
            id: hintBadge
            anchors.horizontalCenter: parent.horizontalCenter
            height: stage._px(30)
            width: hintBadgeText.implicitWidth + stage._px(26)
            radius: stage._px(6)
            color: "transparent"
            border.color: Theme.line
            border.width: 1

            Text {
                id: hintBadgeText
                anchors.centerIn: parent
                text: qsTr("Last played").toUpperCase()
                color: Theme.text3
                font.family: Theme.family
                font.pixelSize: stage._px(Theme.fontBody)
                font.weight: Font.DemiBold
                font.letterSpacing: stage._u * 1.1
            }
        }

        Text {
            id: hintText
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Your last game on %1 will show up here").arg(stage.hostName)
            // A long host name wraps within the block's width instead of running off the edge.
            width: Math.min(implicitWidth, lastPanel.width)
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.text2
            font.family: Theme.family
            font.pixelSize: stage._px(Theme.fontBody)
        }
    }
}
