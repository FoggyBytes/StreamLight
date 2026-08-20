import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.2
import QtQuick.Window 2.2

import StreamingPreferences 1.0
import ComputerManager 1.0
import SdlGamepadKeyNavigation 1.0
import SystemProperties 1.0
import ShortcutManager 1.0

// SettingsScreen — Xbox-style flat settings panel.
// 6 tabs: Video, Audio, Input, Decoder, Network, Session.
// Each tab body is a Column of "sections"; each section is a header label
// (uppercase, dim) followed by a panel of rows. Each row has a label on the
// left and a control on the right.
// Root is FocusScope (not Item) — required for activeFocus propagation from
// the Loader above to the controls inside (TabBar, switches, dropdowns).
FocusScope {
    id: settingsScreen
    anchors.fill: parent
    focus: true

    readonly property color _bg2:       "#1a1a1a"
    readonly property color _bg3:       "#202020"
    readonly property color _border:    "#2a2a2a"
    readonly property color _borderS:   "#3a3a3a"
    readonly property color _text:      "#f0f0f0"
    readonly property color _textDim:   "#a0a0a0"
    readonly property color _textMut:   "#707070"
    // Bright-green accent used everywhere (_green/_greenLk kept as aliases).
    readonly property color _green:     Theme.accent
    readonly property color _greenLk:   Theme.accent
    readonly property color _focus:     Theme.accent
    readonly property int   _focusBd:   3
    readonly property int   _rowHeight: 58
    readonly property int   _rowHeightTall: 76

    // ── Host link speed (4.6.0) ───────────────────────────────────────────────
    // Filled from SystemProperties.localLinkInfo() when the screen opens. Read once:
    // the cable doesn't change while you're reading a settings page, and re-probing on
    // a timer would be motion for its own sake.
    property string _linkDetail: qsTr("Checking this device's connection…")
    property string _linkPill:   qsTr("Checking…")
    property bool   _linkUsable: false

    function _refreshLocalLink() {
        var info = SystemProperties.localLinkInfo()
        _linkUsable = info.usable === true

        if (_linkUsable) {
            var speed = info.mbps >= 1000 ? (info.mbps / 1000) + " Gbps" : info.mbps + " Mbps"
            _linkPill = speed
            _linkDetail = StreamingPreferences.matchHostLinkSpeed
                ? qsTr("Reaching the network over %1 at %2.").arg(info.adapter).arg(speed)
                : qsTr("Reaching the network over %1 at %2. Hosts are left as they are.").arg(info.adapter).arg(speed)
            return
        }

        // The reason comes from the probe itself, so the wording always matches the
        // actual cause rather than a guess made here.
        _linkPill = qsTr("Not available")
        _linkDetail = info.reason && info.reason.length > 0
            ? info.reason.charAt(0).toUpperCase() + info.reason.slice(1) + "."
            : qsTr("This device's wired connection could not be identified.")
    }
    readonly property int   _gapY:      24

    // Read by AppShell to show the "X · Default" status-bar prompt.
    property bool bitrateNonDefault: false

    // Active host-profile context (set by AppShell when Settings is opened from a
    // host with an active profile). Rows whose key the profile overrides are shown
    // greyed + disabled, since editing them here wouldn't affect that host.
    // Direct per-key bindings (not a function) so they react when the map is set.
    property var    activeProfileOverride: ({})
    property string activeProfileName: ""

    // One lock per setting, not one for the pair: a profile that pins only the frame
    // rate used to grey out the global resolution as well, which said something untrue
    // about what the profile was actually holding.
    readonly property bool _lockRes:         activeProfileOverride && activeProfileOverride.width !== undefined
    readonly property bool _lockFps:         activeProfileOverride && activeProfileOverride.fps !== undefined
    readonly property bool _lockBitrate:     activeProfileOverride && activeProfileOverride.bitrate !== undefined
    readonly property bool _lockHdr:         activeProfileOverride && activeProfileOverride.hdr !== undefined
    readonly property bool _lockCodec:       activeProfileOverride && activeProfileOverride.codec !== undefined
    readonly property bool _lockFramePacing: activeProfileOverride && activeProfileOverride.framepacing !== undefined
    readonly property bool _lockAudio:       activeProfileOverride && activeProfileOverride.audio !== undefined
    readonly property bool _lockHue:         activeProfileOverride && activeProfileOverride.hue !== undefined
    readonly property bool _lockMatchLink:   activeProfileOverride && activeProfileOverride.matchlink !== undefined
    readonly property bool _lockWaitForGame: activeProfileOverride && activeProfileOverride.waitgame !== undefined
    readonly property bool _lockRefreshRate: activeProfileOverride && activeProfileOverride.refreshrate !== undefined
    readonly property bool _lockDisplayMode: activeProfileOverride && activeProfileOverride.displaymode !== undefined
    readonly property bool _lockVsync:       activeProfileOverride && activeProfileOverride.vsync !== undefined

    // Latest-release tags fetched once per Settings open from the GitHub API.
    property string streamLightLatest: ""
    property string streamTweakLatest: ""

    function _fetchLatestTag(repo, callback) {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", "https://api.github.com/repos/FoggyBytes/" + repo + "/releases/latest")
        xhr.setRequestHeader("Accept", "application/vnd.github+json")
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            if (xhr.status !== 200) { callback(""); return }
            try {
                var data = JSON.parse(xhr.responseText)
                callback(data.tag_name || "")
            } catch (e) { callback("") }
        }
        xhr.send()
    }

    function resetBitrateToDefault() {
        if (!bitrateSlider) return
        var def = StreamingPreferences.getDefaultBitrate(
                      StreamingPreferences.width, StreamingPreferences.height,
                      StreamingPreferences.fps, StreamingPreferences.enableYUV444)
        StreamingPreferences.bitrateKbps       = def
        StreamingPreferences.autoAdjustBitrate = true
        bitrateSlider.value                    = def
        // Explicit: if the bitrate was already at the default, only autoAdjustBitrate moved and
        // the slider's debounce would never be armed.
        StreamingPreferences.save()
    }

    // LB/RB cycle tabs; X (Menu) resets the bitrate when non-default.
    Keys.onPressed: function(event) {
        // Two pairs, one meaning here: the shoulders now send their own F16/F17 (they used
        // to send PageUp/PageDown, which collided with the host cycle on Home — see
        // sdlgamepadkeynavigation.cpp), while PgUp/PgDn stay for the keyboard and for the
        // status-bar tab arrows, which drive this through simulateKey.
        if (event.key === Qt.Key_PageUp || event.key === Qt.Key_F16) {
            if (tabBar.currentIndex > 0) tabBar.currentIndex--
            event.accepted = true
        } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_F17) {
            if (tabBar.currentIndex < tabBar.count - 1) tabBar.currentIndex++
            event.accepted = true
        } else if ((event.key === Qt.Key_Menu || event.key === Qt.Key_D)
                   && settingsScreen.bitrateNonDefault) {
            // D is the keyboard half of the same prompt. The status bar was already offering
            // it while nothing listened for it — a prompt naming a key that did nothing.
            settingsScreen.resetBitrateToDefault()
            event.accepted = true
        }
    }

    // Focus the first control of the given tab (so D-pad starts inside the body).
    function focusFirstControl(idx) {
        switch (idx) {
            case 0: if (resolutionSelector)    resolutionSelector.forceActiveFocus();    break
            case 1: if (audioConfigSelector)   audioConfigSelector.forceActiveFocus();   break
            case 2: if (absMouseSwitch)        absMouseSwitch.forceActiveFocus();        break
            case 3: if (decoderSelector)       decoderSelector.forceActiveFocus();       break
            case 4: if (mdnsSwitch)            mdnsSwitch.forceActiveFocus();            break
            case 5: if (gameOptSwitch)         gameOptSwitch.forceActiveFocus();         break
            case 6: if (perfOverlaySwitch)     perfOverlaySwitch.forceActiveFocus();     break
            case 7: if (glyphSetSelector)      glyphSetSelector.forceActiveFocus();      break
            case 8: if (aboutSlGithubBtn)      aboutSlGithubBtn.forceActiveFocus();      break
        }
    }

    // Re-focus on every activation (the Loader transfers focus AFTER ctor).
    onActiveFocusChanged: {
        if (activeFocus) {
            Qt.callLater(function() { focusFirstControl(tabBar.currentIndex) })
        }
    }

    Component.onCompleted: {
        SdlGamepadKeyNavigation.setUiNavMode(true)
        Qt.callLater(function() { focusFirstControl(tabBar.currentIndex) })
        _fetchLatestTag("StreamLight",  function(t) { settingsScreen.streamLightLatest  = t })
        _fetchLatestTag("StreamTweak",  function(t) { settingsScreen.streamTweakLatest  = t })
        _refreshLocalLink()
    }

    /*
     * ⚠️ This save() is a backstop, not the mechanism — the selectors and switches persist on
     * the change itself.
     *
     * It does run more often than "at exit" would suggest: AppShell's settingsLoader is
     * `active: currentPage === 2`, so leaving this page destroys the screen and saves. What it
     * cannot cover is the app being killed while still standing on it, and that is not a corner
     * case — it is how "matchHostLinkSpeed doesn't stick" was found, in a release where the app
     * dying was a normal way for it to end.
     *
     * save() writes the whole preference set, so calling it from one control persists any other
     * pending change too. That is why it is cheap to call everywhere and why nothing here needs
     * to be selective about it.
     */
    Component.onDestruction: {
        SdlGamepadKeyNavigation.setUiNavMode(false)
        StreamingPreferences.save()
    }

    // Manual resolution entry — opened by the "Custom" pill in the Video tab.
    CustomResolutionDialog {
        id: customResDialog
        onAccepted: function(w, h) {
            if (StreamingPreferences.width !== w || StreamingPreferences.height !== h) {
                StreamingPreferences.width  = w
                StreamingPreferences.height = h
                if (StreamingPreferences.autoAdjustBitrate) {
                    StreamingPreferences.bitrateKbps = StreamingPreferences.getDefaultBitrate(
                        w, h, StreamingPreferences.fps, StreamingPreferences.enableYUV444)
                    bitrateSlider.value = StreamingPreferences.bitrateKbps
                }
                StreamingPreferences.save()
            }
        }
    }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        /*
         * The title alone. It used to carry a line under it describing what the screen is
         * for, which is what the tabs below already say, one word each.
         *
         * ⚠️ 56 is a floor, not a spare-looking number, and the title is centred in it rather
         * than sitting at the top with the removed line's space left under it. The clock is
         * 60px tall and pinned at y22 by the shell, so anything full-width has to start below
         * y82 — at 40 the tab bar rose to y72 and "About" ran under the date. At 56 the tabs
         * start at 88, and centring puts the title's own centre on 52, which is the clock's:
         * the two ends of the band read as one line instead of two things at two heights.
         */
        height: 56

        Label {
            id: headerTitle
            text: qsTr("Settings")
            font.family: "DM Sans"
            font.pixelSize: 28
            font.bold: true
            color: settingsScreen._text
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
        }

        // (The clock is the shell's — see StatusCluster in AppShell.)
    }

    TabBar {
        id: tabBar
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        anchors.topMargin: 8
        height: 48
        // Tab switching is LB/RB only — never via D-pad focus traversal.
        focusPolicy: Qt.NoFocus
        onCurrentIndexChanged: settingsScreen.focusFirstControl(currentIndex)

        background: Rectangle {
            color: "transparent"
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: settingsScreen._border
            }
        }

        TabButton {
            text: qsTr("Video")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 0 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 0 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Audio")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 1 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 1 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Input")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 2 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 2 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Decoder")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 3 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 3 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Network")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 4 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 4 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Session")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 5 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 5 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Overlay")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 6 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 6 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("Shortcuts")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 7 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 7 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        TabButton {
            text: qsTr("About")
            focusPolicy: Qt.NoFocus
            font.family: "DM Sans"
            font.pixelSize: 15
            font.bold: true
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    color: tabBar.currentIndex === 8 ? settingsScreen._green : "transparent"
                }
            }
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 8 ? settingsScreen._text : settingsScreen._textDim
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // Green outline shown around the active control when it has focus.
    // Hidden in pointer mode so the keyboard/gamepad focus ring does not
    // linger on the last-focused control while the user is driving with
    // the mouse (would otherwise highlight two controls at once).
    component FocusFrame: Rectangle {
        property Item target
        color: "transparent"
        radius: 6
        border.color: settingsScreen._focus
        border.width: settingsScreen._focusBd
        visible: target && target.activeFocus && SdlGamepadKeyNavigation.inputMode !== "pointer"
        z: 1
    }

    // Notice placed at the top of any settings sub-block that contains rows locked
    // by the active host profile. Collapses to zero height when not active.
    component ProfileLockNotice: Item {
        property bool active: false
        width: parent ? parent.width : 0
        visible: active && settingsScreen.activeProfileName.length > 0
        height: visible ? settingsScreen._rowHeight : 0
        Row {
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Label {
                text: "🔒"; font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                width: parent.width - 28
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Greyed settings are controlled by the active host profile “%1”.")
                      .arg(settingsScreen.activeProfileName)
                font.family: "DM Sans"; font.pixelSize: 13
                color: settingsScreen._textDim
                wrapMode: Text.WordWrap
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            x: 16; width: parent.width - 32; height: 1; color: settingsScreen._border
        }
    }

    Flickable {
        id: contentFlick
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        anchors.topMargin: 16
        anchors.bottomMargin: 20

        boundsBehavior: Flickable.OvershootBounds
        clip: true
        contentWidth: tabContent.width
        contentHeight: tabContent.implicitHeight

        // LB/RB must reach the tab switcher even if focus is inside the Flickable.
        Keys.forwardTo: [settingsScreen]

        // Auto-scroll: keep the focused row in view as D-pad navigation moves
        // through the tab body. Otherwise focus would land on off-screen rows.
        // focus to rows that the user can't see until they mouse-scroll.
        property Item activeFocusItem: Window.activeFocusItem
        onActiveFocusItemChanged: {
            if (!activeFocusItem) return
            // Walk up to verify the focused item belongs to this Flickable.
            var p = activeFocusItem
            var inside = false
            while (p) {
                if (p === contentFlick) { inside = true; break }
                p = p.parent
            }
            if (!inside) return

            var margin = 24

            // A row may ask for its container to be revealed instead of itself — the overlay
            // preview does, because one of its lines on screen says nothing about the block.
            //
            // ⚠️ Only when the container fits the viewport. Without that check every keypress
            // inside a taller-than-the-screen block would scroll back to its top, making its
            // lower rows unreachable. Reading an undeclared property returns undefined here,
            // so rows that do not opt in need no changes.
            var target = activeFocusItem.revealTarget
            var ref = (target && target.height <= height - margin * 2)
                      ? target : activeFocusItem

            var pos    = ref.mapToItem(tabContent, 0, 0)
            var top    = pos.y
            var bottom = pos.y + ref.height

            if (top < contentY + margin) {
                contentY = Math.max(0, top - margin)
            } else if (bottom > contentY + height - margin) {
                contentY = Math.min(
                    Math.max(0, contentHeight - height),
                    bottom - height + margin
                )
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: contentFlick.contentHeight > contentFlick.height
                    ? ScrollBar.AsNeeded
                    : ScrollBar.AlwaysOff
        }

        Item {
            id: tabContent
            width: contentFlick.width - 16
            // Use only the active tab's column height; childrenRect would
            // include invisible sibling tabs and oversize the scrollbar.
            implicitHeight: {
                switch (tabBar.currentIndex) {
                    case 0: return videoTab.implicitHeight
                    case 1: return audioTab.implicitHeight
                    case 2: return inputTab.implicitHeight
                    case 3: return decoderTab.implicitHeight
                    case 4: return networkTab.implicitHeight
                    case 5: return sessionTab.implicitHeight
                    case 6: return overlayTab.implicitHeight
                    case 7: return shortcutsTab.implicitHeight
                    case 8: return aboutTab.implicitHeight
                }
                return 0
            }

            // ──────────────────────────────────────────────────────────────────
            //                              VIDEO TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: videoTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 0
                spacing: 16

                // ── Section: VIDEO ────────────────────────────────────────────
                Label {
                    text: qsTr("Video")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: videoCol.implicitHeight + 8

                    Column {
                        id: videoCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        // Active-profile notice — shown only when this VIDEO block
                        // actually has a setting locked by the active profile.
                        // ⚠️ One notice per card, not per row: the Video tab is a single card,
                        // so every lockable row in it reports here. A second notice further
                        // down would read as a second warning about something else.
                        ProfileLockNotice {
                            active: settingsScreen._lockRes
                                    || settingsScreen._lockFps
                                    || settingsScreen._lockBitrate
                                    || settingsScreen._lockDisplayMode
                                    || settingsScreen._lockRefreshRate
                                    || settingsScreen._lockVsync
                                    || settingsScreen._lockFramePacing
                        }

                        // ── Resolution / Frame rate ───────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Resolution / Frame rate")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Row {
                                id: resFpsRow
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 12

                                SegmentedSelector {
                                    id: resolutionSelector
                                    anchors.verticalCenter: parent.verticalCenter
                                    enabled: !settingsScreen._lockRes
                                    opacity: enabled ? 1.0 : 0.4

                                    property var _widths:  [1280, 1920, 2560, 3840]
                                    property var _heights: [720,  1080, 1440, 2160]
                                    labels: ["720p", "1080p", "1440p", "4K"]

                                    // Highlight the matching preset, or -1 (none) when the
                                    // current resolution is a custom one — the Custom pill
                                    // then shows the actual value instead.
                                    function _resync() {
                                        for (var i = 0; i < _widths.length; i++) {
                                            if (_widths[i] === StreamingPreferences.width
                                             && _heights[i] === StreamingPreferences.height) {
                                                currentIndex = i
                                                return
                                            }
                                        }
                                        currentIndex = -1
                                    }

                                    Component.onCompleted: _resync()

                                    // width/height share the displayModeChanged NOTIFY signal.
                                    Connections {
                                        target: StreamingPreferences
                                        function onDisplayModeChanged() { resolutionSelector._resync() }
                                    }

                                    onActivated: function(idx) {
                                        var w = _widths[idx], h = _heights[idx]
                                        if (StreamingPreferences.width !== w || StreamingPreferences.height !== h) {
                                            StreamingPreferences.width  = w
                                            StreamingPreferences.height = h
                                            if (StreamingPreferences.autoAdjustBitrate) {
                                                StreamingPreferences.bitrateKbps = StreamingPreferences.getDefaultBitrate(
                                                    w, h, StreamingPreferences.fps, StreamingPreferences.enableYUV444)
                                                bitrateSlider.value = StreamingPreferences.bitrateKbps
                                            }
                                            StreamingPreferences.save()
                                        }
                                    }

                                    KeyNavigation.right: customResBtn
                                }

                                // Standalone "Custom" pill — opens the manual-entry dialog.
                                // Selected (and shows the value) when no preset matches.
                                PillButton {
                                    id: customResBtn
                                    anchors.verticalCenter: parent.verticalCenter
                                    enabled: !settingsScreen._lockRes
                                    opacity: enabled ? 1.0 : 0.4
                                    selected: resolutionSelector.currentIndex < 0
                                    text: selected ? (StreamingPreferences.width + "×" + StreamingPreferences.height)
                                                   : qsTr("Custom")
                                    onClicked: {
                                        customResDialog.initWidth  = StreamingPreferences.width
                                        customResDialog.initHeight = StreamingPreferences.height
                                        customResDialog.open()
                                    }
                                    KeyNavigation.left:  resolutionSelector
                                    KeyNavigation.right: fpsSelector
                                }

                                // Visual separator between the resolution and frame-rate blocks.
                                Rectangle {
                                    width: 1; height: 24
                                    color: "#3a3a3a"
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                SegmentedSelector {
                                    id: fpsSelector
                                    anchors.verticalCenter: parent.verticalCenter
                                    enabled: !settingsScreen._lockFps
                                    opacity: enabled ? 1.0 : 0.4

                                    property var _fps: [30, 60, 90, 120]
                                    property var _presetLabels: ["30", "60", "90", "120"]

                                    function _resync() {
                                        for (var i = 0; i < _fps.length; i++) {
                                            if (_fps[i] === StreamingPreferences.fps) {
                                                currentIndex = i
                                                return
                                            }
                                        }
                                        currentIndex = -1
                                    }

                                    Component.onCompleted: {
                                        for (var i = 0; i < _fps.length; i++) {
                                            if (_fps[i] === StreamingPreferences.fps) {
                                                labels = _presetLabels
                                                currentIndex = i
                                                return
                                            }
                                        }
                                        labels = [String(StreamingPreferences.fps)].concat(_presetLabels)
                                        _fps   = [StreamingPreferences.fps].concat(_fps)
                                        currentIndex = 0
                                    }

                                    Connections {
                                        target: StreamingPreferences
                                        function onFpsChanged() { fpsSelector._resync() }
                                    }

                                    onActivated: function(idx) {
                                        var f = _fps[idx]
                                        if (StreamingPreferences.fps !== f) {
                                            StreamingPreferences.fps = f
                                            if (StreamingPreferences.autoAdjustBitrate) {
                                                StreamingPreferences.bitrateKbps = StreamingPreferences.getDefaultBitrate(
                                                    StreamingPreferences.width, StreamingPreferences.height,
                                                    f, StreamingPreferences.enableYUV444)
                                                bitrateSlider.value = StreamingPreferences.bitrateKbps
                                            }
                                            StreamingPreferences.save()
                                        }
                                    }

                                    KeyNavigation.left: customResBtn
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Video bitrate ─────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: !settingsScreen._lockBitrate
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Video bitrate")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Raise for higher quality on fast connections")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: bitrateSlider; anchors.margins: -6; target: bitrateSlider }

                            // Bridges current vs recommended → AppShell status-bar X·Default hint.
                            Binding {
                                target: settingsScreen
                                property: "bitrateNonDefault"
                                value: StreamingPreferences.bitrateKbps !== StreamingPreferences.getDefaultBitrate(
                                            StreamingPreferences.width, StreamingPreferences.height,
                                            StreamingPreferences.fps, StreamingPreferences.enableYUV444)
                            }

                            Slider {
                                id: bitrateSlider
                                anchors.right: bitrateValueLabel.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                width: 240

                                from: 500
                                to: StreamingPreferences.unlockBitrate ? 500000 : 150000
                                stepSize: 500
                                snapMode: Slider.SnapAlways
                                value: StreamingPreferences.bitrateKbps

                                onValueChanged: {
                                    // Equal on the binding's first evaluation, so opening the page
                                    // writes nothing.
                                    if (StreamingPreferences.bitrateKbps !== value) {
                                        StreamingPreferences.bitrateKbps = value
                                        bitrateSaveTimer.restart()
                                    }
                                }
                                onMoved:        { StreamingPreferences.autoAdjustBitrate = false }

                                // A slider reports every step of a drag, so saving on each one
                                // would write the whole preference set dozens of times to cross
                                // the track. The write waits for the movement to stop instead.
                                //
                                // ⚠️ It has to hang off valueChanged and not moved(): the
                                // keyboard/pad path below sets `value` directly, and moved() only
                                // fires for mouse, wheel and the control's own key handling.
                                Timer {
                                    id: bitrateSaveTimer
                                    interval: 600
                                    onTriggered: StreamingPreferences.save()
                                }

                                // Hold-to-accelerate on ◀/▶: tap = ±0.5 Mbps, hold ramps 1×→20×.
                                property int  _accelDir: 0       // -1 / 0 / +1
                                property int  _accelTicks: 0

                                Timer {
                                    id: bitrateAccelTimer
                                    interval: 60
                                    repeat: true
                                    onTriggered: {
                                        if (bitrateSlider._accelDir === 0) { stop(); return }
                                        bitrateSlider._accelTicks++
                                        var mult = Math.min(20, 1 + Math.floor(bitrateSlider._accelTicks / 4))
                                        var delta = bitrateSlider._accelDir * bitrateSlider.stepSize * mult
                                        var v = Math.max(bitrateSlider.from,
                                                Math.min(bitrateSlider.to, bitrateSlider.value + delta))
                                        if (v !== bitrateSlider.value) {
                                            bitrateSlider.value = v
                                            StreamingPreferences.autoAdjustBitrate = false
                                        }
                                    }
                                }

                                function _startAccel(dir) {
                                    var v = Math.max(from, Math.min(to, value + dir * stepSize))
                                    if (v !== value) { value = v; StreamingPreferences.autoAdjustBitrate = false }
                                    _accelDir   = dir
                                    _accelTicks = 0
                                    bitrateAccelTimer.start()
                                }
                                function _stopAccel() {
                                    _accelDir = 0
                                    _accelTicks = 0
                                    bitrateAccelTimer.stop()
                                }

                                Keys.onPressed: {
                                    if (event.isAutoRepeat) { event.accepted = true; return }
                                    if (event.key === Qt.Key_Left)  { _startAccel(-1); event.accepted = true }
                                    if (event.key === Qt.Key_Right) { _startAccel(+1); event.accepted = true }
                                }
                                Keys.onReleased: {
                                    if (event.isAutoRepeat) { event.accepted = true; return }
                                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                                        _stopAccel()
                                        event.accepted = true
                                    }
                                }

                                background: Rectangle {
                                    x: bitrateSlider.leftPadding
                                    y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                    width: bitrateSlider.availableWidth
                                    height: 3
                                    radius: 2
                                    color: "#f0f0f0"
                                }
                                handle: Rectangle {
                                    x: bitrateSlider.leftPadding + bitrateSlider.visualPosition * (bitrateSlider.availableWidth - width)
                                    y: bitrateSlider.topPadding + bitrateSlider.availableHeight / 2 - height / 2
                                    implicitWidth: 14
                                    implicitHeight: 14
                                    radius: 7
                                    color: bitrateSlider.pressed ? Qt.lighter(settingsScreen._green, 1.2)
                                         : bitrateSlider.hovered ? Qt.lighter(settingsScreen._green, 1.1)
                                         :                         settingsScreen._green
                                    border.color: settingsScreen._green
                                    border.width: 1
                                }
                            }

                            Label {
                                id: bitrateValueLabel
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: (StreamingPreferences.bitrateKbps / 1000).toFixed(0) + " Mbps"
                                font.family: "DM Sans"
                                font.pixelSize: 13
                                font.bold: true
                                color: settingsScreen._green
                                horizontalAlignment: Text.AlignRight
                                width: 80
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Display mode ──────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: !settingsScreen._lockDisplayMode
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Display mode")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Fullscreen has the best performance. Borderless windowed allows Alt+Tab, screenshots and overlays.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            SegmentedSelector {
                                id: displayModeSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Fullscreen"), qsTr("Borderless"), qsTr("Windowed")]
                                property var _values: [
                                    StreamingPreferences.WM_FULLSCREEN,
                                    StreamingPreferences.WM_FULLSCREEN_DESKTOP,
                                    StreamingPreferences.WM_WINDOWED
                                ]

                                // Reactive: re-evaluates whenever the underlying preference
                                // changes (here or elsewhere), keeping the highlighted pill
                                // in sync without an explicit setter call.
                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.windowMode
                                        for (var i = 0; i < displayModeSelector._values.length; i++) {
                                            if (displayModeSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.windowMode = _values[idx]; StreamingPreferences.save() }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Refresh rate ──────────────────────────────────────
                        Item {
                            width: parent.width
                            height: Math.max(settingsScreen._rowHeightTall, rrCol.implicitHeight + 16)
                            // Only exclusive fullscreen sets a display mode at all — in
                            // Borderless and Windowed the desktop mode is used as it is,
                            // so there is nothing here to decide. Lock the control rather
                            // than let it show a choice that is silently ignored, the same
                            // way Frame Pacing locks when V-Sync is off.
                            enabled: !settingsScreen._lockRefreshRate
                                     && StreamingPreferences.windowMode === StreamingPreferences.WM_FULLSCREEN
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                id: rrCol
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: refreshRateSelector.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Refresh rate switching")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: StreamingPreferences.windowMode === StreamingPreferences.WM_FULLSCREEN
                                          ? qsTr("What your screen's refresh rate does while streaming. Feed a 120 Hz screen a 60 FPS stream and Highest keeps it at 120 Hz, Match frame rate takes it down to 60 Hz. Automatic picks whichever suits your frame pacing setting.")
                                          : qsTr("Only applies to Fullscreen — Borderless and Windowed use the desktop's own refresh rate.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            SegmentedSelector {
                                id: refreshRateSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Off"), qsTr("Automatic"), qsTr("Highest"), qsTr("Match frame rate")]
                                property var _values: [
                                    StreamingPreferences.RR_OFF,
                                    StreamingPreferences.RR_AUTO,
                                    StreamingPreferences.RR_HIGHEST,
                                    StreamingPreferences.RR_MATCH_FPS
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.refreshRateMode
                                        for (var i = 0; i < refreshRateSelector._values.length; i++) {
                                            if (refreshRateSelector._values[i] === v) return i
                                        }
                                        // Fall back to Highest, the default — index 0 is Off,
                                        // and landing on it would misreport a stored value we
                                        // simply didn't recognise.
                                        return 2
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.refreshRateMode = _values[idx]; StreamingPreferences.save() }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── V-Sync ────────────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: !settingsScreen._lockVsync
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("V-Sync")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Disabling reduces latency but may cause visible tearing")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: vsyncSwitch; anchors.margins: -3; target: vsyncSwitch }
                            STSwitch {
                                id: vsyncSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.enableVsync
                                // ⚠️ The guard is not tidiness: onCheckedChanged also fires when the
                                // binding above first evaluates, so an unguarded save() here would
                                // write the whole preference set once per switch every time this
                                // page opens. Every switch below follows the same shape.
                                onCheckedChanged: { if (StreamingPreferences.enableVsync !== checked) { StreamingPreferences.enableVsync = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Frame Pacing ──────────────────────────────────────
                        Item {
                            id: fpRow
                            width: parent.width
                            height: Math.max(settingsScreen._rowHeightTall, fpCol.implicitHeight + 16)
                            // Frame pacing has no effect without V-Sync: Session passes
                            // FP_OFF to the decoder whenever V-Sync is disabled, and both
                            // pacing paths (DXGI sync interval, software Pacer) are gated
                            // on it. Lock the control instead of letting it show a value
                            // that is silently ignored. The stored mode is deliberately
                            // left untouched, so re-enabling V-Sync restores the choice.
                            enabled: !settingsScreen._lockFramePacing
                                     && StreamingPreferences.enableVsync
                            opacity: enabled ? 1.0 : 0.4

                            // The 2:2 arrangement is not a setting anyone can see, so work out
                            // whether these choices actually land on it and say so only then.
                            // Mirrors the gate in D3D11VARenderer::initialize(): Automatic or
                            // Hardware, V-Sync on, and a panel running a whole multiple of 2 to 4
                            // times the stream rate, within a Hz of exact.
                            //
                            // ⚠️ getRefreshRate() is a Q_INVOKABLE, so a binding that read it
                            // inline would never be told to re-evaluate. It is captured once
                            // instead - the panel does not change rate while this page is open,
                            // and everything else here is a property that tracks. Display 0: on a
                            // multi-monitor desk the stream may open elsewhere, which is why this
                            // line is advisory and gates nothing.
                            property int _panelHz: 0
                            Component.onCompleted: _panelHz = SystemProperties.getRefreshRate(0)

                            readonly property int _cadence: {
                                if (!StreamingPreferences.enableVsync) return 0
                                if (StreamingPreferences.framePacingMode !== StreamingPreferences.FP_AUTO
                                        && StreamingPreferences.framePacingMode !== StreamingPreferences.FP_MULTIPLE) return 0
                                // Match frame rate puts the panel on the stream's own rate, so
                                // there is no multiple left to warn about - but only in exclusive
                                // fullscreen, the one place a mode switch actually happens.
                                if (StreamingPreferences.refreshRateMode === StreamingPreferences.RR_MATCH_FPS
                                        && StreamingPreferences.windowMode === StreamingPreferences.WM_FULLSCREEN) return 0
                                var fps = StreamingPreferences.fps
                                if (fps <= 0 || fpRow._panelHz < fps * 2) return 0
                                var n = Math.round(fpRow._panelHz / fps)
                                if (n < 2 || n > 4 || Math.abs(fpRow._panelHz - n * fps) > 1) return 0
                                return n
                            }

                            Column {
                                id: fpCol
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: framePacingSelector.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Frame Pacing")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: StreamingPreferences.enableVsync
                                          ? qsTr("Removes judder on high-refresh displays. Software paces every frame itself. Hardware hands the job to the GPU and needs a screen running a whole multiple of the stream, holding each frame for two refreshes on a 120 Hz screen at 60 FPS, or four on a 240 Hz one.")
                                          : qsTr("Requires V-Sync — with it off the stream renders as fast as it can, so nothing is paced. Your saved mode is kept and comes back as soon as you re-enable V-Sync.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                                Label {
                                    visible: fpRow._cadence !== 0
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("At %1 FPS on a %2 Hz screen each frame is held for %3 refreshes, and how much delay that adds is settled when the stream starts. Match frame rate avoids the arrangement instead of managing it.")
                                          .arg(StreamingPreferences.fps)
                                          .arg(fpRow._panelHz)
                                          .arg(["", "", qsTr("two"), qsTr("three"), qsTr("four")][fpRow._cadence])
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: Theme.warning
                                }
                            }

                            SegmentedSelector {
                                id: framePacingSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Off"), qsTr("Automatic"), qsTr("Software"), qsTr("Hardware")]
                                property var _values: [
                                    StreamingPreferences.FP_OFF,
                                    StreamingPreferences.FP_AUTO,
                                    StreamingPreferences.FP_MATCHED,
                                    StreamingPreferences.FP_MULTIPLE
                                ]

                                Binding on currentIndex {
                                    value: {
                                        // With V-Sync off nothing is pacing, so show Off (index 0)
                                        // rather than the saved mode: the user reads the selector as
                                        // "what is happening", and a greyed "Automatic" just invites
                                        // the question "why isn't it?".
                                        //
                                        // This changes the display ONLY — the stored preference is
                                        // untouched and reappears here as soon as V-Sync is back on.
                                        // Safe because onActivated fires on user interaction, not on
                                        // this binding, and the row is disabled in that state.
                                        if (!StreamingPreferences.enableVsync) return 0
                                        var v = StreamingPreferences.framePacingMode
                                        for (var i = 0; i < framePacingSelector._values.length; i++) {
                                            if (framePacingSelector._values[i] === v) return i
                                        }
                                        return 0
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.framePacingMode = _values[idx]; StreamingPreferences.save() }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                              AUDIO TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: audioTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 1
                spacing: 16

                Label {
                    text: qsTr("Audio")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: audioCol.implicitHeight + 8

                    Column {
                        id: audioCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        ProfileLockNotice { active: settingsScreen._lockAudio }

                        // ── Audio configuration ───────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight
                            enabled: !settingsScreen._lockAudio
                            opacity: enabled ? 1.0 : 0.4

                            Label {
                                text: qsTr("Audio configuration")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: audioConfigSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Stereo"), qsTr("5.1"), qsTr("7.1")]
                                property var _values: [
                                    StreamingPreferences.AC_STEREO,
                                    StreamingPreferences.AC_51_SURROUND,
                                    StreamingPreferences.AC_71_SURROUND
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.audioConfig
                                        for (var i = 0; i < audioConfigSelector._values.length; i++) {
                                            if (audioConfigSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.audioConfig = _values[idx]; StreamingPreferences.save() }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Mute host PC speakers ─────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Mute host PC speakers during streaming")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Restart any in-progress game for this to take effect")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: muteHostSwitch; anchors.margins: -3; target: muteHostSwitch }
                            STSwitch {
                                id: muteHostSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !StreamingPreferences.playAudioOnHost
                                // Inverted switch: the stored value is the opposite of the checkbox,
                                // so the guard has to compare against that, not against `checked`.
                                onCheckedChanged: { var v = !checked; if (StreamingPreferences.playAudioOnHost !== v) { StreamingPreferences.playAudioOnHost = v; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Mute when window not focused ──────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Mute audio when window is not focused")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Mutes when you Alt+Tab away or click on another window")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: muteFocusSwitch; anchors.margins: -3; target: muteFocusSwitch }
                            STSwitch {
                                id: muteFocusSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.muteOnFocusLoss
                                onCheckedChanged: { if (StreamingPreferences.muteOnFocusLoss !== checked) { StreamingPreferences.muteOnFocusLoss = checked; StreamingPreferences.save() } }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                              INPUT TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: inputTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 2
                spacing: 16

                // ── MOUSE & KEYBOARD section ──────────────────────────────────
                Label {
                    text: qsTr("Mouse & Keyboard")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: mkCol.implicitHeight + 8

                    Column {
                        id: mkCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        // ── Optimize mouse for remote desktop ─────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Optimize mouse for remote desktop")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Seamless cursor without capture. Toggle live with Ctrl+Alt+Shift+M.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: absMouseSwitch; anchors.margins: -3; target: absMouseSwitch }
                            STSwitch {
                                id: absMouseSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.absoluteMouseMode
                                onCheckedChanged: { if (StreamingPreferences.absoluteMouseMode !== checked) { StreamingPreferences.absoluteMouseMode = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Capture system keyboard shortcuts ─────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Capture system keyboard shortcuts")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Forwards shortcuts like Alt+Tab to the host. Ctrl+Alt+Del cannot be intercepted.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            SegmentedSelector {
                                id: captureSysKeysSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Never"), qsTr("In Game"), qsTr("Always")]
                                property var _values: [
                                    StreamingPreferences.CSK_OFF,
                                    StreamingPreferences.CSK_FULLSCREEN,
                                    StreamingPreferences.CSK_ALWAYS
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.captureSysKeysMode
                                        for (var i = 0; i < captureSysKeysSelector._values.length; i++) {
                                            if (captureSysKeysSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.captureSysKeysMode = _values[idx]; StreamingPreferences.save() }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Use touchscreen as virtual trackpad ───────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Use touchscreen as virtual trackpad")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("On: behaves like a trackpad.  Off: directly controls the pointer.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: touchSwitch; anchors.margins: -3; target: touchSwitch }
                            STSwitch {
                                id: touchSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !StreamingPreferences.absoluteTouchMode
                                onCheckedChanged: { var v = !checked; if (StreamingPreferences.absoluteTouchMode !== v) { StreamingPreferences.absoluteTouchMode = v; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Swap mouse buttons ────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Swap left and right mouse buttons")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: swapMouseSwitch; anchors.margins: -3; target: swapMouseSwitch }
                            STSwitch {
                                id: swapMouseSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.swapMouseButtons
                                onCheckedChanged: { if (StreamingPreferences.swapMouseButtons !== checked) { StreamingPreferences.swapMouseButtons = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Reverse scroll direction ──────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Reverse scroll direction")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: revScrollSwitch; anchors.margins: -3; target: revScrollSwitch }
                            STSwitch {
                                id: revScrollSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.reverseScrollDirection
                                onCheckedChanged: { if (StreamingPreferences.reverseScrollDirection !== checked) { StreamingPreferences.reverseScrollDirection = checked; StreamingPreferences.save() } }
                            }
                        }
                    }
                }

                // ── GAMEPAD section ───────────────────────────────────────────
                Label {
                    text: qsTr("Controller")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: gpCol.implicitHeight + 8

                    Column {
                        id: gpCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Swap A/B and X/Y controller buttons")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Nintendo-style button layout")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: swapFaceSwitch; anchors.margins: -3; target: swapFaceSwitch }
                            STSwitch {
                                id: swapFaceSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.swapFaceButtons
                                onCheckedChanged: { if (StreamingPreferences.swapFaceButtons !== checked) { StreamingPreferences.swapFaceButtons = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Force controller #1 always connected")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Keeps a virtual pad on the host. Enable only for games that don't support hot-plug.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: singleCtrlSwitch; anchors.margins: -3; target: singleCtrlSwitch }
                            STSwitch {
                                id: singleCtrlSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !StreamingPreferences.multiController
                                onCheckedChanged: { var v = !checked; if (StreamingPreferences.multiController !== v) { StreamingPreferences.multiController = v; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Mouse control with controller (Start)")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: gamepadMouseSwitch; anchors.margins: -3; target: gamepadMouseSwitch }
                            STSwitch {
                                id: gamepadMouseSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.gamepadMouse
                                onCheckedChanged: { if (StreamingPreferences.gamepadMouse !== checked) { StreamingPreferences.gamepadMouse = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Process controller input in background")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Captures controller input even when the window is not focused")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: bgGamepadSwitch; anchors.margins: -3; target: bgGamepadSwitch }
                            STSwitch {
                                id: bgGamepadSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.backgroundGamepad
                                onCheckedChanged: { if (StreamingPreferences.backgroundGamepad !== checked) { StreamingPreferences.backgroundGamepad = checked; StreamingPreferences.save() } }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                            DECODER TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: decoderTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 3
                spacing: 16

                Label {
                    text: qsTr("Video decoder & codec")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: decCol.implicitHeight + 8

                    Column {
                        id: decCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        ProfileLockNotice {
                            active: settingsScreen._lockCodec || settingsScreen._lockHdr
                        }

                        // ── Video decoder ─────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Video decoder")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: decoderSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Auto"), qsTr("Software"), qsTr("Hardware")]
                                property var _values: [
                                    StreamingPreferences.VDS_AUTO,
                                    StreamingPreferences.VDS_FORCE_SOFTWARE,
                                    StreamingPreferences.VDS_FORCE_HARDWARE
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.videoDecoderSelection
                                        for (var i = 0; i < decoderSelector._values.length; i++) {
                                            if (decoderSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.videoDecoderSelection = _values[idx]; StreamingPreferences.save() }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Video codec ───────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight
                            enabled: !settingsScreen._lockCodec
                            opacity: enabled ? 1.0 : 0.4

                            Label {
                                text: qsTr("Video codec")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: codecSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Auto"), qsTr("H.264"), qsTr("HEVC"), qsTr("AV1")]
                                property var _values: [
                                    StreamingPreferences.VCC_AUTO,
                                    StreamingPreferences.VCC_FORCE_H264,
                                    StreamingPreferences.VCC_FORCE_HEVC,
                                    StreamingPreferences.VCC_FORCE_AV1
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.videoCodecConfig
                                        for (var i = 0; i < codecSelector._values.length; i++) {
                                            if (codecSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.videoCodecConfig = _values[idx]; StreamingPreferences.save() }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Enable HDR ────────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: !settingsScreen._lockHdr
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Enable HDR")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Some games require an HDR monitor on the host to enable HDR")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: hdrSwitch; anchors.margins: -3; target: hdrSwitch }
                            STSwitch {
                                id: hdrSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.enableHdr
                                onCheckedChanged: { if (StreamingPreferences.enableHdr !== checked) { StreamingPreferences.enableHdr = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Enable YUV 4:4:4 ──────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Enable YUV 4:4:4 (experimental)")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Better for desktop and text-heavy games. Not recommended for fast-paced action.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: yuv444Switch; anchors.margins: -3; target: yuv444Switch }
                            STSwitch {
                                id: yuv444Switch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.enableYUV444
                                onCheckedChanged: {
                                    if (StreamingPreferences.enableYUV444 !== checked) {
                                        StreamingPreferences.enableYUV444 = checked
                                        if (StreamingPreferences.autoAdjustBitrate) {
                                            StreamingPreferences.bitrateKbps = StreamingPreferences.getDefaultBitrate(
                                                StreamingPreferences.width, StreamingPreferences.height,
                                                StreamingPreferences.fps, StreamingPreferences.enableYUV444)
                                            bitrateSlider.value = StreamingPreferences.bitrateKbps
                                        }
                                        StreamingPreferences.save()
                                    }
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Unlock bitrate limit ──────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Unlock bitrate limit (experimental)")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Allows very high bitrates with Sunshine hosts. Use only over wired LAN.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: vbrSwitch; anchors.margins: -3; target: vbrSwitch }
                            STSwitch {
                                id: vbrSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.unlockBitrate
                                onCheckedChanged: {
                                    // Only the save is guarded here, not the body: the clamp has to
                                    // run on the binding's first evaluation too, or a bitrate stored
                                    // above the locked ceiling would stay there.
                                    var changed = (StreamingPreferences.unlockBitrate !== checked)
                                    StreamingPreferences.unlockBitrate = checked
                                    StreamingPreferences.bitrateKbps = Math.min(StreamingPreferences.bitrateKbps, bitrateSlider.to)
                                    bitrateSlider.value = StreamingPreferences.bitrateKbps
                                    if (changed) { StreamingPreferences.save() }
                                }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                            NETWORK TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: networkTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 4
                spacing: 16

                Label {
                    text: qsTr("Network")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: netCol.implicitHeight + 8

                    Column {
                        id: netCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Automatically discover PCs on local network")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: mdnsSwitch; anchors.margins: -3; target: mdnsSwitch }
                            STSwitch {
                                id: mdnsSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.enableMdns
                                onCheckedChanged: {
                                    if (StreamingPreferences.enableMdns !== checked) {
                                        StreamingPreferences.enableMdns = checked
                                        StreamingPreferences.save()
                                    }
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Automatically detect blocked connections")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: blockDetectSwitch; anchors.margins: -3; target: blockDetectSwitch }
                            STSwitch {
                                id: blockDetectSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.detectNetworkBlocking
                                onCheckedChanged: { if (StreamingPreferences.detectNetworkBlocking !== checked) { StreamingPreferences.detectNetworkBlocking = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Auto-reconnect on no video ────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: autoReconnectSwitch.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Automatically reconnect if the host is slow to start")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("If the host doesn't send video right away (e.g. a virtual display or HDR/AV1 encoder still warming up), StreamLight quietly retries once instead of showing an error.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                }
                            }

                            FocusFrame { anchors.fill: autoReconnectSwitch; anchors.margins: -3; target: autoReconnectSwitch }
                            // STSwitch like every other row in this screen. It was the one plain
                            // Switch left, so it rendered in Material's default look while its
                            // twenty-six neighbours used the app's.
                            STSwitch {
                                id: autoReconnectSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.autoReconnectNoVideo
                                onCheckedChanged: { if (StreamingPreferences.autoReconnectNoVideo !== checked) { StreamingPreferences.autoReconnectNoVideo = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // Auto-start Tailscale at StreamLight launch. When ON, Tailscale
                        // is started in the background on every StreamLight boot (only
                        // if it is not already running), so remote hosts reachable via
                        // their Tailscale IP can be discovered and streamed. The OFF
                        // toggle does NOT kill the running Tailscale instance — it only
                        // prevents the next launch. Requires the official installer
                        // from https://tailscale.com/download (Microsoft Store package
                        // is not supported).
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Auto-start Tailscale on launch")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Launches Tailscale in the background so remote hosts can be reached via Tailscale IP.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: tailscaleSwitch; anchors.margins: -3; target: tailscaleSwitch }
                            STSwitch {
                                id: tailscaleSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.tailscaleAutoStart
                                onCheckedChanged: {
                                    if (StreamingPreferences.tailscaleAutoStart !== checked) {
                                        StreamingPreferences.tailscaleAutoStart = checked
                                        // Persist immediately so the new value survives
                                        // the imminent restart (Yes) or any later quit.
                                        StreamingPreferences.save()
                                        if (checked) {
                                            tailscaleRestartDialog.open()
                                        } else {
                                            tailscaleStopNoticeDialog.open()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── HOST LINK SPEED ───────────────────────────────────────────
                Label {
                    text: qsTr("Host link speed")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: linkCol.implicitHeight + 8

                    Column {
                        id: linkCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        ProfileLockNotice { active: settingsScreen._lockMatchLink }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight + 18
                            enabled: !settingsScreen._lockMatchLink
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: linkSwitch.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Match host link speed to this device")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Before connecting, ask the host to run its wired link at this device's speed. Fixes the packet loss caused by a faster host link feeding a slower one.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                    wrapMode: Text.Wrap
                                    width: parent.width
                                }
                                // The host half of the feature lives in StreamTweak: without it
                                // there is nothing on the other end to ask, and the client simply
                                // connects as it always did.
                                Label {
                                    text: qsTr("Requires StreamTweak 8.1.0 or later on the host, with client control allowed. Hosts without it are unaffected.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                    wrapMode: Text.Wrap
                                    width: parent.width
                                }
                            }

                            FocusFrame { anchors.fill: linkSwitch; anchors.margins: -3; target: linkSwitch }
                            STSwitch {
                                id: linkSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.matchHostLinkSpeed
                                onCheckedChanged: {
                                    if (StreamingPreferences.matchHostLinkSpeed !== checked) {
                                        StreamingPreferences.matchHostLinkSpeed = checked
                                        // Persist now rather than leaving it to this screen's
                                        // Component.onDestruction: a session that ends by killing
                                        // the app never runs that, and the setting silently
                                        // reverts to its default on the next launch.
                                        StreamingPreferences.save()
                                    }
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // Never inert in silence: when the feature can't act, this row says
                        // why — Wi-Fi, a tunnel, or an adapter that reports no rate. Reads the
                        // default route, which answers "what is this device's wired link";
                        // a specific host may still be reached another way, and the host tile
                        // shows that case.
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight + 8

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: linkPill.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("This device")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: settingsScreen._linkDetail
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                    wrapMode: Text.Wrap
                                    width: parent.width
                                }
                            }

                            Rectangle {
                                id: linkPill
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: linkPillText.implicitWidth + 20
                                height: 26
                                radius: 5
                                // Matches a selected SegmentedSelector pill: solid accent with dark
                                // text, so a live value reads the same everywhere in Settings.
                                color: settingsScreen._linkUsable ? Theme.accent : "#1AA0A0A0"
                                border.width: settingsScreen._linkUsable ? 0 : 1
                                border.color: "#3a3a3a"

                                Label {
                                    id: linkPillText
                                    anchors.centerIn: parent
                                    text: settingsScreen._linkPill
                                    font.family: "DM Sans"
                                    font.pixelSize: 12
                                    font.bold: true
                                    color: settingsScreen._linkUsable ? Theme.onAccent : settingsScreen._textMut
                                }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                              SESSION TAB
            //   Two sections: HOST (host-side behaviour) and INTERFACE
            //   (StreamLight client UI/UX preferences).
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: sessionTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 5
                spacing: 16

                // ── HOST section ──────────────────────────────────────────────
                Label {
                    text: qsTr("Host")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: hostCol.implicitHeight + 8

                    Column {
                        id: hostCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        // ── Optimize game settings for streaming ──────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Optimize game settings for streaming")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: gameOptSwitch; anchors.margins: -3; target: gameOptSwitch }
                            STSwitch {
                                id: gameOptSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.gameOptimizations
                                onCheckedChanged: { if (StreamingPreferences.gameOptimizations !== checked) { StreamingPreferences.gameOptimizations = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Quit app on host after closing the stream ─────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Quit app on host after closing the stream")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Closes the game when the stream ends. Unsaved progress will be lost.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: quitAppSwitch; anchors.margins: -3; target: quitAppSwitch }
                            STSwitch {
                                id: quitAppSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.quitAppAfter
                                onCheckedChanged: { if (StreamingPreferences.quitAppAfter !== checked) { StreamingPreferences.quitAppAfter = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Wait for the game to appear ───────────────────────
                        // Here rather than under Network, where it first landed next to
                        // auto-reconnect because the two answer the same moment. By subject it
                        // belongs with the row above: both are about what happens around a
                        // session rather than about the network. It is the bottom of its own
                        // cascade — a host profile, then a per-game override, replace it.
                        ProfileLockNotice { active: settingsScreen._lockWaitForGame }

                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: !settingsScreen._lockWaitForGame
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: waitForGameSwitch.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Wait for the game to appear")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Keep the launch screen up until the host reports the game is on screen, instead of showing the stream as soon as it starts. Games that open their own launcher never get there — turn it off for those in their per-game settings.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                }
                            }

                            FocusFrame { anchors.fill: waitForGameSwitch; anchors.margins: -3; target: waitForGameSwitch }
                            STSwitch {
                                id: waitForGameSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.waitForGameOnScreen
                                onCheckedChanged: {
                                    if (StreamingPreferences.waitForGameOnScreen !== checked) {
                                        StreamingPreferences.waitForGameOnScreen = checked
                                        // Persisted on the toggle, not at this screen's
                                        // Component.onDestruction: an app killed rather than
                                        // closed never runs that, and the setting would revert.
                                        StreamingPreferences.save()
                                    }
                                }
                            }
                        }
                    }
                }

                // ── INTERFACE section ─────────────────────────────────────────
                Label {
                    text: qsTr("Interface")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: ifaceCol.implicitHeight + 8

                    Column {
                        id: ifaceCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        ProfileLockNotice { active: settingsScreen._lockHue }

                        // ── GUI mode (dropdown) ───────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("GUI mode")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: uiModeSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Windowed"), qsTr("Maximized"), qsTr("Fullscreen")]
                                property var _values: [
                                    StreamingPreferences.UI_WINDOWED,
                                    StreamingPreferences.UI_MAXIMIZED,
                                    StreamingPreferences.UI_FULLSCREEN
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.uiDisplayMode
                                        for (var i = 0; i < uiModeSelector._values.length; i++) {
                                            if (uiModeSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) { StreamingPreferences.uiDisplayMode = _values[idx]; StreamingPreferences.save() }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Show connection quality warnings ──────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Show connection quality warnings")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: connWarnSwitch; anchors.margins: -3; target: connWarnSwitch }
                            STSwitch {
                                id: connWarnSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.connectionWarnings
                                onCheckedChanged: { if (StreamingPreferences.connectionWarnings !== checked) { StreamingPreferences.connectionWarnings = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Show configuration warnings ───────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Show configuration warnings")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            FocusFrame { anchors.fill: configWarnSwitch; anchors.margins: -3; target: configWarnSwitch }
                            STSwitch {
                                id: configWarnSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.configurationWarnings
                                onCheckedChanged: { if (StreamingPreferences.configurationWarnings !== checked) { StreamingPreferences.configurationWarnings = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Discord Rich Presence ─────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Discord Rich Presence")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Shows the streamed game in your Discord status")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: discordSwitch; anchors.margins: -3; target: discordSwitch }
                            STSwitch {
                                id: discordSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.richPresence
                                onCheckedChanged: { if (StreamingPreferences.richPresence !== checked) { StreamingPreferences.richPresence = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Keep display awake while streaming ────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Keep display awake while streaming")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Prevents screensaver and display sleep during streaming")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: keepAwakeSwitch; anchors.margins: -3; target: keepAwakeSwitch }
                            STSwitch {
                                id: keepAwakeSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.keepAwake
                                onCheckedChanged: { if (StreamingPreferences.keepAwake !== checked) { StreamingPreferences.keepAwake = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Launch Philips Hue Sync during streaming ──────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: !settingsScreen._lockHue
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Launch Philips Hue Sync during streaming")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Auto-launches Hue Sync at stream start and closes it at end")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: hueSyncSwitch; anchors.margins: -3; target: hueSyncSwitch }
                            STSwitch {
                                id: hueSyncSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.hueSyncIntegration
                                onCheckedChanged: { if (StreamingPreferences.hueSyncIntegration !== checked) { StreamingPreferences.hueSyncIntegration = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Hide host IP addresses ────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Hide host IP addresses")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Masks host IPs across the app for privacy (e.g. screenshots)")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: hideIpsSwitch; anchors.margins: -3; target: hideIpsSwitch }
                            STSwitch {
                                id: hideIpsSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.hideHostIps
                                onCheckedChanged: { if (StreamingPreferences.hideHostIps !== checked) { StreamingPreferences.hideHostIps = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Clock format ──────────────────────────────────────
                        // Here, with the rows about what the app puts on screen, rather than with
                        // the accent below: those two are styling, these two are content. Both
                        // drive the clock in the corner of Home — a setting and not the system
                        // locale, because the app is English-only and the locale would otherwise
                        // pick the order of the date fields on the user's behalf.
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Clock format")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: clockFormatSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("24-hour"), qsTr("AM/PM")]
                                property var _values: [
                                    StreamingPreferences.CF_24H,
                                    StreamingPreferences.CF_12H
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.clockFormat
                                        for (var i = 0; i < clockFormatSelector._values.length; i++) {
                                            if (clockFormatSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                // Persisted on the change, not at this screen's
                                // Component.onDestruction: an app killed rather than closed never
                                // runs that, and the setting would revert.
                                onActivated: function(idx) {
                                    StreamingPreferences.clockFormat = _values[idx]
                                    StreamingPreferences.save()
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Date format ───────────────────────────────────────
                        // The patterns are the labels: naming the orders instead ("day first")
                        // would be a word for a thing that is already legible as itself.
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Date format")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: dateFormatSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: ["DD/MM/YYYY", "MM/DD/YYYY", "YYYY/MM/DD"]
                                property var _values: [
                                    StreamingPreferences.DF_DMY,
                                    StreamingPreferences.DF_MDY,
                                    StreamingPreferences.DF_YMD
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.dateFormat
                                        for (var i = 0; i < dateFormatSelector._values.length; i++) {
                                            if (dateFormatSelector._values[i] === v) return i
                                        }
                                        return -1
                                    }
                                }
                                onActivated: function(idx) {
                                    StreamingPreferences.dateFormat = _values[idx]
                                    StreamingPreferences.save()
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Accent colour ─────────────────────────────────────
                        // One colour drives the whole interface: the focus ring, the primary
                        // button, the active tab, the mark in the header. Semantic colours are
                        // deliberately NOT included — online stays green, a warning stays amber —
                        // or a red accent would make "online" read as an alarm.
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Accent colour")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Used wherever the interface highlights something — status colours don't change")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            // Presets and a free field, side by side. The presets are for the
                            // pad — typing a hex code with a thumbstick is not a thing anyone
                            // should have to do — and the field is for everyone who already
                            // knows the colour they want.
                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10

                                SegmentedSelector {
                                    id: accentSelector
                                    anchors.verticalCenter: parent.verticalCenter

                                    // Ion first, because it is the default (see theme.cpp) and the
                                    // first pill is where anyone looks for it. Signal — the green
                                    // the app wore until 5.0.0 — sits next to it.
                                    //
                                    // ⚠️ The two arrays are parallel and indexed together by both
                                    // the Binding below and onActivated: reordering one without
                                    // the other silently assigns the wrong colour to every name.
                                    labels: [qsTr("Ion"), qsTr("Signal"), qsTr("Ember"), qsTr("Amber"), qsTr("Ultra")]
                                    readonly property var _hexes: ["#00d3f2", "#00e676", "#ff6a3d", "#ffb300", "#c060ff"]

                                    // -1, not 0, when the colour matches no preset: a typed
                                    // colour must leave every pill unlit rather than light up
                                    // "Signal" and claim the accent is green when it isn't.
                                    Binding on currentIndex {
                                        value: {
                                            var cur = Theme.accent.toString().toLowerCase()
                                            for (var i = 0; i < accentSelector._hexes.length; i++) {
                                                if (accentSelector._hexes[i] === cur) return i
                                            }
                                            return -1
                                        }
                                    }
                                    onActivated: function(idx) { Theme.accent = _hexes[idx] }
                                    // At its right edge the selector leaves the key unhandled
                                    // on purpose, so this hands the focus on to the field.
                                    KeyNavigation.right: accentHexField
                                }

                                // Live swatch. It is the answer to "is this the colour I meant"
                                // before the rest of the screen repaints.
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 28; height: 28
                                    radius: 6
                                    color: Theme.accent
                                    border.color: Theme.lineHigh
                                    border.width: 1
                                }

                                TextField {
                                    id: accentHexField
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 110
                                    implicitHeight: 36
                                    activeFocusOnTab: true
                                    KeyNavigation.left: accentSelector

                                    // Six hex digits with an optional leading #. Anything the
                                    // validator rejects never reaches Theme, so there is no
                                    // path from this field to an unreadable interface.
                                    validator: RegularExpressionValidator {
                                        regularExpression: /#?[0-9A-Fa-f]{0,6}/
                                    }
                                    maximumLength: 7
                                    horizontalAlignment: TextInput.AlignHCenter
                                    color: Theme.text
                                    selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
                                    selectedTextColor: Theme.onAccent
                                    font.family: Theme.family
                                    font.pixelSize: 14
                                    font.bold: true

                                    background: Rectangle {
                                        color: "#0f0f0f"
                                        radius: 8
                                        border.color: accentHexField.activeFocus ? Theme.accent : Theme.line
                                        border.width: accentHexField.activeFocus ? 2 : 1
                                    }

                                    // Follows Theme while the user is elsewhere, so picking a
                                    // preset updates the field — but never while it has the
                                    // focus, or every keystroke would be overwritten mid-typing.
                                    Binding on text {
                                        when: !accentHexField.activeFocus
                                        value: Theme.accent.toString()
                                    }

                                    function commit() {
                                        var t = text.trim()
                                        if (t.length > 0 && t.charAt(0) !== "#") t = "#" + t
                                        // Six digits exactly: Qt would happily accept "#abc"
                                        // and expand it, which is a different colour from the
                                        // one a half-typed code was heading towards.
                                        if (/^#[0-9A-Fa-f]{6}$/.test(t)) Theme.accent = t
                                        text = Theme.accent.toString()
                                    }

                                    onEditingFinished: commit()
                                    Keys.onReturnPressed: { commit(); event.accepted = true }
                                    Keys.onEnterPressed:  { commit(); event.accepted = true }
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Reduce animations ─────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Reduce animations")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    text: qsTr("Turns off movement and glow outside the stream — worth it on battery")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: reduceAnimSwitch; anchors.margins: -3; target: reduceAnimSwitch }
                            STSwitch {
                                id: reduceAnimSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: Theme.reduceAnimations
                                onCheckedChanged: { Theme.reduceAnimations = checked }
                            }
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                              OVERLAY TAB
            //   Three cards: how it looks, what it will look like, and what goes on
            //   it. The old Off/Minimal/Default/Full profiles are gone — they were
            //   three fixed answers to a question only the user can answer, and the
            //   one that made the overlay small enough for a 1080p handheld did not
            //   exist. From issue #9 (@Soladus).
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: overlayTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 6
                spacing: 16

                // The real overlay face, so the preview is not merely "something like it".
                FontLoader { id: overlayFace; source: "qrc:/data/RobotoMono.ttf" }

                // Read through properties rather than calling the invokables straight from a
                // binding: a binding that only calls a function registers no dependency, so the
                // preview would keep the colours it happened to be built with. Same trap as
                // PadGlyph's resolver — see its comment.
                readonly property int   _itemMask: StreamingPreferences.overlayItems
                readonly property int   _colorSel: StreamingPreferences.overlayTextColor
                readonly property int   _transpSel: StreamingPreferences.overlayTransparency
                readonly property int   _sizeSel:  StreamingPreferences.overlayFontSize
                readonly property color _txtColor: overlayTab._colorSel >= 0
                                                   ? StreamingPreferences.overlayTextColorValue() : "white"
                readonly property color _boxColor: overlayTab._transpSel >= 0
                                                   ? StreamingPreferences.overlayBoxColorValue() : "#f018181a"
                readonly property int   _fontPx:   overlayTab._sizeSel >= 0
                                                   ? StreamingPreferences.overlayFontPixelSize() : 20

                // Which corner the stats card takes; the settings panel gets the other one,
                // exactly as OverlayManager::getOverlayOriginX flips it in the stream.
                readonly property bool _statsRight:
                    StreamingPreferences.overlayPosition === StreamingPreferences.OVP_TOP_RIGHT

                // Advance width of one character per pixel of font size. The face is
                // monospace, so every string's width is a multiplication — which is how the
                // settings panel below sizes its columns without a measuring pass per row.
                readonly property string _mono: overlayFace.status === FontLoader.Ready
                                                ? overlayFace.name : "monospace"
                TextMetrics {
                    id: monoUnit
                    font.family: overlayTab._mono
                    font.pixelSize: 100
                    text: "0"
                }
                readonly property real _adv: monoUnit.advanceWidth / 100

                /*
                 * One list drives both the switches below and the preview above, so the two
                 * cannot drift apart. `lines` is what stringifyVideoStats writes for that item,
                 * with illustrative numbers — if a line changes there, it changes here.
                 */
                readonly property var itemDefs: [
                    { bit: StreamingPreferences.OI_VIDEO,
                      name: qsTr("Video stream"),
                      desc: qsTr("Resolution, frame rate and codec"),
                      host: false, sub: false,
                      lines: ["Video stream: 1920x1080 60.00 FPS (Codec: HEVC)"] },
                    { bit: StreamingPreferences.OI_BITRATE,
                      name: qsTr("Bitrate"),
                      desc: qsTr("How much video is actually arriving"),
                      host: false, sub: false,
                      lines: ["Bitrate: 41.2 Mbps"] },
                    // ⚠️ OI_BITRATE_PEAK is deliberately NOT here. Every entry in this list is a
                    // line of the overlay; the peak is not one — it changes what the bitrate
                    // line says. As a row of its own it was the only thing in the box that
                    // matched nothing visible, and it needed a special "greyed when bitrate is
                    // off" case to stand up at all. It lives with the style settings instead.
                    { bit: StreamingPreferences.OI_FRAMERATES,
                      name: qsTr("Frame rate breakdown"),
                      desc: qsTr("Frames arriving, decoded and drawn, as three separate figures"),
                      host: false, sub: false,
                      lines: ["Incoming frame rate from network: 60.00 FPS",
                              "Decoding frame rate: 60.00 FPS",
                              "Rendering frame rate: 60.00 FPS"] },
                    { bit: StreamingPreferences.OI_HOST_LATENCY,
                      name: qsTr("Host processing latency"),
                      desc: qsTr("How long the host takes to capture and encode each frame"),
                      host: false, sub: false,
                      lines: ["Host processing latency min/max/average: 1.2/4.8/2.1 ms"] },
                    { bit: StreamingPreferences.OI_NET_DROPS,
                      name: qsTr("Network frame drops"),
                      desc: qsTr("Frames the connection lost on the way here"),
                      host: false, sub: false,
                      lines: ["Frames dropped by your network connection: 0.10%"] },
                    { bit: StreamingPreferences.OI_JITTER_DROPS,
                      name: qsTr("Jitter frame drops"),
                      desc: qsTr("Frames thrown away because they arrived too late to be useful"),
                      host: false, sub: false,
                      lines: ["Frames dropped due to network jitter: 0.02%"] },
                    { bit: StreamingPreferences.OI_LATENCY,
                      name: qsTr("Network latency"),
                      desc: qsTr("Round trip to the host, and how much it wanders"),
                      host: false, sub: false,
                      lines: ["Average network latency: 11 ms (variance: 2 ms)"] },
                    { bit: StreamingPreferences.OI_DECODE_TIME,
                      name: qsTr("Decoding time"),
                      desc: qsTr("How long this device takes to decode a frame"),
                      host: false, sub: false,
                      lines: ["Average decoding time: 3.21 ms"] },
                    { bit: StreamingPreferences.OI_QUEUE_DELAY,
                      name: qsTr("Frame queue delay"),
                      desc: qsTr("How long decoded frames wait before being drawn"),
                      host: false, sub: false,
                      lines: ["Average frame queue delay: 0.40 ms"] },
                    { bit: StreamingPreferences.OI_RENDER_TIME,
                      name: qsTr("Rendering time"),
                      desc: qsTr("Drawing, including the wait for the monitor's V-sync"),
                      host: false, sub: false,
                      lines: ["Average rendering time (including monitor V-sync latency): 1.10 ms"] },
                    { bit: StreamingPreferences.OI_PACING,
                      name: qsTr("Frame pacing"),
                      desc: qsTr("Which pacing mechanism is really in effect"),
                      host: false, sub: false,
                      lines: ["Frame pacing: Hardware (2:2 cadence)"] },
                    // ⚠️ The one switch here that does more than choose what to print: it
                    // turns on the renderer's per-present measurement, and with it the
                    // [pacing] lines in the log. That is the point — it is what a bug
                    // report needs, and it used to require setting an environment variable
                    // from a shell.
                    { bit: StreamingPreferences.OI_CADENCE,
                      name: qsTr("Cadence (measured)"),
                      desc: qsTr("What the display did with the cadence above — how long each frame was really held, how deep the queue sat, how long a frame waited to be handed over. Also records it in the log, which is what to switch on before reporting a stutter"),
                      host: false, sub: false,
                      lines: ["Cadence: 2.00 v/f (2-2), queue 0.0, wait 0.4 ms"] },
                    { bit: StreamingPreferences.OI_HOST_METRICS,
                      name: qsTr("Host metrics"),
                      desc: qsTr("GPU, encoder, temperature, VRAM, CPU and outbound network — needs StreamTweak on the host"),
                      host: true, sub: false,
                      lines: ["GPU: 47% | Enc: 12% | Temp: 62C | VRAM: 4096 / 8192 MB",
                              "CPU: 18% | Net TX: 41 Mbps"] }
                ]

                // The three ready-made sets. Anything else is Custom, which is reported
                // beside the group rather than offered inside it — see the Preset row.
                readonly property var presetMasks: [
                    StreamingPreferences.OI_VIDEO | StreamingPreferences.OI_BITRATE |
                    StreamingPreferences.OI_LATENCY | StreamingPreferences.OI_NET_DROPS,

                    StreamingPreferences.OI_VIDEO | StreamingPreferences.OI_BITRATE |
                    StreamingPreferences.OI_NET_DROPS | StreamingPreferences.OI_JITTER_DROPS |
                    StreamingPreferences.OI_LATENCY | StreamingPreferences.OI_DECODE_TIME |
                    StreamingPreferences.OI_PACING | StreamingPreferences.OI_HOST_METRICS,

                    StreamingPreferences.OI_ALL
                ]

                // -1 means Custom: no preset matches, so nothing in the group is highlighted.
                readonly property int presetIndex: {
                    var m = overlayTab._itemMask
                    for (var i = 0; i < overlayTab.presetMasks.length; i++) {
                        if (overlayTab.presetMasks[i] === m) return i
                    }
                    return -1
                }

                /*
                 * Every row the box shows, in order: each item, plus the two section headings
                 * where the renderer would put them.
                 *
                 * The headings follow the same rule as stringifyVideoStats — they appear only
                 * as a pair, because a heading over the only section there is is just the
                 * widest line in the box. That means they come and go as items are switched,
                 * which is correct: the box has to look like what will actually be drawn.
                 */
                readonly property var rowModel: {
                    var mask = overlayTab._itemMask
                    var defs = overlayTab.itemDefs
                    var client = [], host = []
                    var clientOn = false, hostOn = false

                    for (var i = 0; i < defs.length; i++) {
                        var on = (mask & defs[i].bit) !== 0
                        if (defs[i].host) {
                            host.push({ kind: "item", defIndex: i })
                            if (on) hostOn = true
                        }
                        else {
                            client.push({ kind: "item", defIndex: i })
                            if (on) clientOn = true
                        }
                    }

                    var out = []
                    if (clientOn && hostOn) {
                        out.push({ kind: "head", text: "--- Client Metrics (StreamLight) ---" })
                    }
                    out = out.concat(client)
                    if (clientOn && hostOn) {
                        out.push({ kind: "head", text: "--- Host Metrics (StreamTweak) ---" })
                    }
                    return out.concat(host)
                }

                // The mono lines one item contributes. Only the bitrate varies, because its
                // peak is a modifier of that line rather than a line of its own.
                function linesFor(defIndex) {
                    var d = overlayTab.itemDefs[defIndex]
                    if (d.bit === StreamingPreferences.OI_BITRATE) {
                        return [(overlayTab._itemMask & StreamingPreferences.OI_BITRATE_PEAK)
                                ? "Bitrate: 41.2 Mbps, Peak (5s): 58.4"
                                : "Bitrate: 41.2 Mbps"]
                    }
                    return d.lines.length > 0 ? d.lines : [d.name]
                }

                // What the status line under the box is saying, driven by whatever has focus.
                property string focusedDesc: ""

                // ── Section: OVERLAY ──────────────────────────────────────────
                Label {
                    text: qsTr("Overlay")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: overlayCol.implicitHeight + 8

                    Column {
                        id: overlayCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        spacing: 0

                        // ── Master switch ─────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: perfOverlaySwitch.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Performance overlay")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("Real-time stats while streaming. The hotkey shows and hides it — set the keyboard and controller combos in Settings → Shortcuts.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: perfOverlaySwitch; anchors.margins: -3; target: perfOverlaySwitch }
                            STSwitch {
                                id: perfOverlaySwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: StreamingPreferences.showPerfOverlay
                                onCheckedChanged: { if (StreamingPreferences.showPerfOverlay !== checked) { StreamingPreferences.showPerfOverlay = checked; StreamingPreferences.save() } }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Preset ────────────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                // ⚠️ presetRow, not overlayPresetSelector: the selector lives
                                // INSIDE that Row now, so it is no longer a sibling of this
                                // Column, and QML drops an anchor to a non-sibling in silence.
                                // The result is a column with no right edge — the description
                                // wraps to nothing and the whole card folds over itself.
                                anchors.right: presetRow.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Preset")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("A starting point for the switches below. Change any of them and this reads Custom.")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            Row {
                                id: presetRow
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10

                                /*
                                 * Custom stands outside the group on purpose. It is not a fourth
                                 * choice — it is what the selection is called once it stops
                                 * matching any of the three — so putting it in the block would
                                 * have made it look pickable, and it isn't. Out here it appears
                                 * only when it is true, and the block simply shows nothing
                                 * selected, which is the honest state.
                                 */
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: overlayTab.presetIndex < 0
                                    width: customLabel.implicitWidth + 22
                                    height: 30
                                    radius: 5
                                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                                    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55)
                                    border.width: 1

                                    Label {
                                        id: customLabel
                                        anchors.centerIn: parent
                                        text: qsTr("Custom")
                                        font.family: "DM Sans"
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: Theme.accent
                                    }
                                }

                                SegmentedSelector {
                                    id: overlayPresetSelector
                                    anchors.verticalCenter: parent.verticalCenter

                                    labels: [qsTr("Minimal"), qsTr("Default"), qsTr("Full")]

                                    // -1 when the items match no preset: no pill is highlighted,
                                    // and the bubble beside it says why.
                                    Binding on currentIndex { value: overlayTab.presetIndex }

                                    onActivated: function(idx) {
                                        StreamingPreferences.overlayItems = overlayTab.presetMasks[idx]
                                        StreamingPreferences.save()
                                    }
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Position ──────────────────────────────────────────
                        // Top edge only. The bottom is where games put their own HUD and where
                        // subtitles live, so a corner down there is the one place a stats box
                        // is guaranteed to be in the way.
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: overlayPositionSelector.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Position")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("The in-stream settings panel takes the other corner")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            SegmentedSelector {
                                id: overlayPositionSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Top left"), qsTr("Top right")]
                                property var _values: [
                                    StreamingPreferences.OVP_TOP_LEFT,
                                    StreamingPreferences.OVP_TOP_RIGHT
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.overlayPosition
                                        for (var i = 0; i < overlayPositionSelector._values.length; i++) {
                                            if (overlayPositionSelector._values[i] === v) return i
                                        }
                                        return 0
                                    }
                                }
                                onActivated: function(idx) {
                                    StreamingPreferences.overlayPosition = _values[idx]
                                    StreamingPreferences.save()
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Text colour ───────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Text colour")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: overlayColorSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("White"), qsTr("Green"), qsTr("Yellow"), qsTr("Cyan"), qsTr("Orange")]
                                property var _values: [
                                    StreamingPreferences.OTC_WHITE,
                                    StreamingPreferences.OTC_GREEN,
                                    StreamingPreferences.OTC_YELLOW,
                                    StreamingPreferences.OTC_CYAN,
                                    StreamingPreferences.OTC_ORANGE
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.overlayTextColor
                                        for (var i = 0; i < overlayColorSelector._values.length; i++) {
                                            if (overlayColorSelector._values[i] === v) return i
                                        }
                                        return 0
                                    }
                                }
                                onActivated: function(idx) {
                                    StreamingPreferences.overlayTextColor = _values[idx]
                                    StreamingPreferences.save()
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Font size ─────────────────────────────────────────
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeight

                            Label {
                                text: qsTr("Font size")
                                font.family: "DM Sans"
                                font.pixelSize: 16
                                font.bold: true
                                color: settingsScreen._text
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            SegmentedSelector {
                                id: overlaySizeSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: [qsTr("Small"), qsTr("Medium"), qsTr("Large")]
                                property var _values: [
                                    StreamingPreferences.OFS_SMALL,
                                    StreamingPreferences.OFS_MEDIUM,
                                    StreamingPreferences.OFS_LARGE
                                ]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.overlayFontSize
                                        for (var i = 0; i < overlaySizeSelector._values.length; i++) {
                                            if (overlaySizeSelector._values[i] === v) return i
                                        }
                                        return 1
                                    }
                                }
                                onActivated: function(idx) {
                                    StreamingPreferences.overlayFontSize = _values[idx]
                                    StreamingPreferences.save()
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Bitrate peak ──────────────────────────────────────
                        // Here and not in the box below, because it is not a line: it appends
                        // the windowed peak to the bitrate line. Greyed when that line is off,
                        // since there is then nothing for it to be the peak of.
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall
                            enabled: (overlayTab._itemMask & StreamingPreferences.OI_BITRATE) !== 0
                            opacity: enabled ? 1.0 : 0.4

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: bitratePeakSwitch.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Bitrate peak")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("Adds the highest of the last few seconds to the bitrate line")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            FocusFrame { anchors.fill: bitratePeakSwitch; anchors.margins: -3; target: bitratePeakSwitch }
                            STSwitch {
                                id: bitratePeakSwitch
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                checked: (overlayTab._itemMask & StreamingPreferences.OI_BITRATE_PEAK) !== 0
                                onCheckedChanged: {
                                    var on = (StreamingPreferences.overlayItems & StreamingPreferences.OI_BITRATE_PEAK) !== 0
                                    if (on === checked) return
                                    StreamingPreferences.overlayItems = checked
                                            ? (StreamingPreferences.overlayItems | StreamingPreferences.OI_BITRATE_PEAK)
                                            : (StreamingPreferences.overlayItems & ~StreamingPreferences.OI_BITRATE_PEAK)
                                    StreamingPreferences.save()
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: settingsScreen._border; x: 16 }

                        // ── Transparency ──────────────────────────────────────
                        // Five steps rather than a slider: a slider is the one control on this
                        // page that needs its own hold-to-accelerate code to be usable with a
                        // thumbstick, and nobody is hunting for 37% here.
                        Item {
                            width: parent.width
                            height: settingsScreen._rowHeightTall

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: overlayTranspSelector.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Label {
                                    text: qsTr("Transparency")
                                    font.family: "DM Sans"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: settingsScreen._text
                                }
                                Label {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("How much of the game shows through the box behind the text")
                                    font.family: "DM Sans"
                                    font.pixelSize: 13
                                    color: settingsScreen._textDim
                                }
                            }

                            SegmentedSelector {
                                id: overlayTranspSelector
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter

                                labels: ["0%", "15%", "30%", "45%", "60%"]
                                property var _values: [0, 15, 30, 45, 60]

                                Binding on currentIndex {
                                    value: {
                                        var v = StreamingPreferences.overlayTransparency
                                        for (var i = 0; i < overlayTranspSelector._values.length; i++) {
                                            if (overlayTranspSelector._values[i] === v) return i
                                        }
                                        return 0
                                    }
                                }
                                onActivated: function(idx) {
                                    StreamingPreferences.overlayTransparency = _values[idx]
                                    StreamingPreferences.save()
                                }
                            }
                        }

                    }
                }



                // ── LINES ─────────────────────────────────────────────────────
                Label {
                    text: qsTr("Lines")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.4
                    font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut
                    leftPadding: 14
                }

                // What the row under the cursor is — where the per-item descriptions went when
                // the switches did. Above the box and not below it: it is read on the way in,
                // and a caption under a tall box is the thing nobody scrolls back up from.
                Label {
                    width: parent.width
                    leftPadding: 14
                    wrapMode: Text.WordWrap
                    text: overlayTab.focusedDesc.length > 0
                          ? overlayTab.focusedDesc
                          : qsTr("Select a line to switch it on or off")
                    font.family: "DM Sans"
                    font.pixelSize: 13
                    color: settingsScreen._textDim
                }

                /*
                 * The list IS the preview.
                 *
                 * There used to be two objects here: thirteen switches with descriptions, and a
                 * separate preview further down. That arrangement asked the eye to correlate the
                 * row you were touching with a box somewhere else on the page — and with a
                 * controller it was worse than that, because the preview held nothing focusable,
                 * so the pad could not reach it at all and you were toggling blind.
                 *
                 * So the box became the control. Each line is focusable and A turns it on or off,
                 * in the real face, colours and transparency it will have while streaming. Lines
                 * that are off stay where they are, dimmed: nothing jumps as you work, and you
                 * turn one back on where you left it.
                 *
                 * It also happens to be the only version that fits: thirteen rows of 20px mono is
                 * about a fifth of the height thirteen described switch rows took, which is what
                 * made the old one unusable at 1080p.
                 */
                Rectangle {
                    id: itemsStrip
                    width: parent.width
                    radius: 8
                    color: "transparent"
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: Math.round(
                        Math.max(itemsCol.implicitHeight + 20, panelCard.height) * _scale) + 26

                    readonly property int _inset: 13

                    /*
                     * ⚠️ The scale is deliberately NOT tied to the stream resolution any more,
                     * and it must not be tied to it again. It used to be `strip width / stream
                     * width`, on the argument that the overlay is composited into the stream
                     * 1:1 (d3d11va draws the card at its literal pixel size, and SDL_ttf point
                     * sizes are pixels at 72 DPI), so a 25pt line really does take 25 of the
                     * stream's pixels — one line out of 1080, or out of 2160 at 4K.
                     *
                     * That ratio only looks honest. The strip is not a scale model of the
                     * screen: it is ~1770 wide and ~300 tall against a 3840x2160 stream, so
                     * scaling by width while the height is arbitrary conveys nothing the eye
                     * can check. What it did convey, reliably, is that the higher your
                     * resolution the less usable this page becomes — at 4K it put Large at
                     * 11px and Small at 7px, i.e. the control's whole range below legibility.
                     *
                     * ⚠️ The reference is the natural width at LARGE, whatever is selected.
                     * Measuring the current size instead would blow Small up to fill the strip
                     * and make all three look identical — the same self-inverting preview the
                     * old font-size floor produced, from the other side. Against a fixed
                     * reference the three keep their 16:20:25 relation, which is the only
                     * thing the setting actually means.
                     *
                     * Both cards take this one scale: the stats card's size is the user's, the
                     * settings panel's is fixed at 20 in OverlayManager, so drawing them
                     * together is also how you see one shrink against the other.
                     */
                    readonly property real _statsNaturalW:
                        itemsCol.implicitWidth * 25 / Math.max(1, overlayTab._fontPx) + 22

                    readonly property real _scale:
                        Math.min(1, (width - _inset * 2 - 40)
                                    / Math.max(1, _statsNaturalW + panelCard.width))

                    // A surface, not a scene: transparency is the one setting that does not
                    // exist on its own — over a panel the box's own colour it cannot be judged.
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: parent.radius - 1
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#2f4152" }
                            GradientStop { position: 0.5; color: "#3e5568" }
                            GradientStop { position: 1.0; color: "#2f4152" }
                        }
                    }

                    // Everything the overlay is made of, scaled as one: box, text, padding. Two
                    // separately-scaled things would drift, and the padding is part of the look.
                    //
                    // ⚠️ transformOrigin follows the corner it is pinned to. An item scaled
                    // about TopRight shrinks towards x + width, so that edge stays put and x is
                    // set from the UNSCALED width — subtracting the scaled width here would
                    // push it inwards twice.
                    Item {
                        x: overlayTab._statsRight
                           ? itemsStrip.width - itemsStrip._inset - width
                           : itemsStrip._inset
                        y: itemsStrip._inset
                        width: itemsCol.implicitWidth + 22
                        height: itemsCol.implicitHeight + 20
                        transformOrigin: overlayTab._statsRight ? Item.TopRight : Item.TopLeft
                        scale: itemsStrip._scale

                        // The box itself, drawn with the user's own colour and transparency.
                        Rectangle {
                            anchors.fill: parent
                            radius: 9
                            color: overlayTab._boxColor
                        }

                    Column {
                        id: itemsCol
                        x: 11
                        y: 10
                        spacing: 0

                        Repeater {
                            model: overlayTab.rowModel

                            delegate: Item {
                                id: lineRow
                                width: lineCol.implicitWidth + 14
                                height: lineCol.implicitHeight + 4

                                // Headings are derived, not chosen: they appear only when there
                                // are two sections to tell apart. Making them focusable would
                                // offer a switch that decides nothing.
                                // Landing on any line should bring the whole box into view, not
                                // just the one line: a row is 30px of a 13-row block, so
                                // entering from above used to leave everything below the fold —
                                // and the point of the block is that you see the shape change
                                // as you switch lines on and off. See contentFlick's handler.
                                readonly property Item revealTarget: itemsStrip

                                readonly property bool _pickable: modelData.kind === "item"
                                readonly property var _def: _pickable ? overlayTab.itemDefs[modelData.defIndex] : null
                                readonly property bool _on: _pickable
                                    && (overlayTab._itemMask & _def.bit) !== 0
                                // The bitrate peak has nothing to be the peak of on its own.
                                readonly property bool _usable: _pickable && (!_def.sub
                                    || (overlayTab._itemMask & StreamingPreferences.OI_BITRATE) !== 0)

                                activeFocusOnTab: _pickable && _usable
                                onActiveFocusChanged: if (activeFocus && _def) overlayTab.focusedDesc = _def.desc

                                function _toggle() {
                                    if (!_usable) return
                                    StreamingPreferences.overlayItems = _on
                                        ? (StreamingPreferences.overlayItems & ~_def.bit)
                                        : (StreamingPreferences.overlayItems | _def.bit)
                                    StreamingPreferences.save()
                                }

                                Keys.onReturnPressed: function(event) { _toggle(); event.accepted = true }
                                Keys.onEnterPressed:  function(event) { _toggle(); event.accepted = true }
                                Keys.onSpacePressed:  function(event) { _toggle(); event.accepted = true }

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: -2
                                    radius: 4
                                    visible: lineRow.activeFocus
                                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                                    border.color: Theme.accent
                                    border.width: 1
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: lineRow._pickable && lineRow._usable
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { lineRow.forceActiveFocus(); lineRow._toggle() }
                                }

                                Column {
                                    id: lineCol
                                    x: 7
                                    y: 2
                                    spacing: 0

                                    Repeater {
                                        model: modelData.kind === "head"
                                               ? [modelData.text]
                                               : overlayTab.linesFor(modelData.defIndex)
                                        delegate: Text {
                                            text: modelData
                                            // Headings keep the overlay's own grey; a line that
                                            // is off is the chosen colour, faded, so it still
                                            // reads as the thing it will be once switched on.
                                            color: lineRow._pickable ? overlayTab._txtColor : "#8a8a8a"
                                            opacity: !lineRow._pickable ? 1.0
                                                   : !lineRow._usable ? 0.18
                                                   : lineRow._on ? 1.0 : 0.28
                                            font.family: overlayFace.status === FontLoader.Ready
                                                         ? overlayFace.name : "monospace"
                                            font.pixelSize: overlayTab._fontPx
                                        }
                                    }
                                }
                            }
                        }
                    }
                    }

                    /*
                     * The in-stream Stream Settings panel, in the corner the stats card is not
                     * in.
                     *
                     * ⚠️ Two positions and not three is load-bearing — it is what lets this
                     * panel take the opposite corner by itself (OverlayManager::getOverlayOriginX
                     * flips `onRight` for OverlayStreamSettings) — and this is where that shows.
                     * So the half of the strip the stats card does not fill is not waste: it is
                     * the preview of the Position setting, which nothing else displayed.
                     *
                     * A replica, not a control. Resolution, frame rate, bitrate, HDR and pacing
                     * belong to Video and Network; they are only read here, so the picture is
                     * the user's own. The rows are deliberately not focusable — in this strip
                     * focus means "this line is a switch", and none of these are.
                     *
                     * Metrics mirror buildPanelSurface: main font 20 — fixed there, NOT the
                     * user's size, which is why picking Small visibly shrinks the stats card
                     * against this one — small font main-5, 16 padding, 18 column gap.
                     */
                    Item {
                        id: panelCard
                        x: overlayTab._statsRight
                           ? itemsStrip._inset
                           : itemsStrip.width - itemsStrip._inset - width
                        y: itemsStrip._inset
                        transformOrigin: overlayTab._statsRight ? Item.TopLeft : Item.TopRight
                        scale: itemsStrip._scale

                        readonly property int _main:   20
                        readonly property int _small:  15
                        readonly property int _pad:    16
                        readonly property int _colGap: 18
                        readonly property int _gap:     9
                        readonly property int _mainH:  Math.round(_main  * 1.32)
                        readonly property int _smallH: Math.round(_small * 1.32)
                        readonly property int _rowH:   _mainH + 10
                        readonly property int _arrowW: 6
                        readonly property int _arrowsW: (_arrowW + _gap) * 2

                        function _w(s, px) { return s.length * overlayTab._adv * px }

                        readonly property string _pacing:
                              !StreamingPreferences.enableVsync ? "Off"
                            : StreamingPreferences.framePacingMode === StreamingPreferences.FP_OFF     ? "Off"
                            : StreamingPreferences.framePacingMode === StreamingPreferences.FP_AUTO    ? "Automatic"
                            : StreamingPreferences.framePacingMode === StreamingPreferences.FP_MATCHED ? "Software"
                            :                                                                            "Hardware"

                        // The same rows the panel builds, in the same order, from the same
                        // preferences. Frame pacing is read-only without V-Sync there, so it is
                        // read-only here: no arrows, muted, and never drawn as the selection.
                        readonly property var _rows: [
                            { label: "Resolution",
                              value: StreamingPreferences.width + "x" + StreamingPreferences.height,
                              hint: "", locked: false },
                            { label: "Frame rate",
                              value: StreamingPreferences.fps + " fps",
                              hint: "", locked: false },
                            { label: "Bitrate",
                              value: Math.round(StreamingPreferences.bitrateKbps / 1000) + " Mbps",
                              hint: "", locked: false },
                            { label: "HDR",
                              value: StreamingPreferences.enableHdr ? "On" : "Off",
                              hint: "", locked: false },
                            { label: "Frame pacing",
                              value: panelCard._pacing,
                              hint: StreamingPreferences.enableVsync ? "" : "V-Sync off",
                              locked: !StreamingPreferences.enableVsync }
                        ]

                        readonly property string _status: "No changes"
                        readonly property var _footer: ["Apply", "Cancel", "Save"]

                        // Widest of rows, header and footer — the panel measures all three, and
                        // a monospace face makes each one a multiplication rather than a layout
                        // pass, so nothing here can feed back into the width.
                        readonly property real _contentW: {
                            var m = 0, i
                            for (i = 0; i < _rows.length; i++) {
                                var r = _rows[i]
                                var cluster = _w(r.value, _main)
                                            + (r.locked ? 0 : _arrowsW)
                                            + (r.hint === "" ? 0 : _w(r.hint, _small) + _gap)
                                m = Math.max(m, _w(r.label, _main) + _colGap + cluster)
                            }
                            m = Math.max(m, _w("STREAM SETTINGS", _small) + 28 + _w("Global", _small))
                            m = Math.max(m, _w(_status, _small))
                            var f = 0
                            for (i = 0; i < _footer.length; i++) {
                                f += (_mainH + 2) + 7 + _w(_footer[i], _main)
                                if (i + 1 < _footer.length) f += _colGap
                            }
                            return Math.max(m, f)
                        }

                        width:  Math.ceil(_contentW) + _pad * 2
                        height: panelBody.y + panelBody.implicitHeight + 14

                        Rectangle {
                            anchors.fill: parent
                            radius: 12
                            color: "#F2161619"
                        }

                        Column {
                            id: panelBody
                            y: 12
                            width: parent.width
                            spacing: 0

                            // Header: title left, save target chip right.
                            Item {
                                width: parent.width
                                height: panelCard._smallH + 12
                                Text {
                                    x: panelCard._pad
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    text: "STREAM SETTINGS"
                                    color: "#B8BCC2"
                                    font.family: overlayTab._mono
                                    font.pixelSize: panelCard._small
                                }
                                Text {
                                    x: parent.width - panelCard._pad - width
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    // Grey is the Global tint; a game override is amber and a
                                    // host profile cyan, but only a live session knows which.
                                    text: "Global"
                                    color: "#9AA0A6"
                                    font.family: overlayTab._mono
                                    font.pixelSize: panelCard._small
                                }
                            }
                            Rectangle {
                                x: panelCard._pad
                                width: panelCard.width - panelCard._pad * 2
                                height: 1
                                color: "#333339"
                            }
                            Item { width: 1; height: 8 }

                            Repeater {
                                model: panelCard._rows

                                delegate: Item {
                                    id: psRow
                                    width: panelBody.width
                                    height: panelCard._rowH

                                    // Only the first row is drawn as the selection: it is what
                                    // shows the green bar and label the panel uses for focus.
                                    readonly property bool _sel: index === 0 && !modelData.locked

                                    Rectangle {
                                        visible: psRow._sel
                                        x: 5; y: 2
                                        width: panelCard.width - 10
                                        height: psRow.height - 4
                                        color: "#F2212F26"
                                    }
                                    Rectangle {
                                        visible: psRow._sel
                                        x: 8; y: 5
                                        width: 3
                                        height: psRow.height - 10
                                        radius: 1
                                        color: "#22C55E"
                                    }

                                    Text {
                                        x: panelCard._pad
                                        height: parent.height
                                        verticalAlignment: Text.AlignVCenter
                                        text: modelData.label
                                        color: modelData.locked ? "#767A80"
                                             : psRow._sel       ? "#4ADE80"
                                             :                    "#CED2D6"
                                        font.family: overlayTab._mono
                                        font.pixelSize: panelCard._main
                                    }

                                    // Right cluster, laid inward from the edge: hint ‹ value ›.
                                    Row {
                                        x: parent.width - panelCard._pad - width
                                        height: parent.height
                                        spacing: panelCard._gap

                                        Text {
                                            visible: modelData.hint !== ""
                                            height: parent.height
                                            verticalAlignment: Text.AlignVCenter
                                            text: modelData.hint
                                            color: "#767A80"
                                            font.family: overlayTab._mono
                                            font.pixelSize: panelCard._small
                                        }
                                        Canvas {
                                            visible: !modelData.locked
                                            width: panelCard._arrowW
                                            height: parent.height
                                            property color tint: psRow._sel ? "#C2C6CC" : "#666A70"
                                            onTintChanged: requestPaint()
                                            onPaint: {
                                                var c = getContext("2d")
                                                c.reset()
                                                var h = Math.max(8, panelCard._mainH / 2)
                                                var t = (height - h) / 2
                                                c.fillStyle = tint
                                                c.beginPath()
                                                c.moveTo(width, t)
                                                c.lineTo(width, t + h)
                                                c.lineTo(0, t + h / 2)
                                                c.closePath()
                                                c.fill()
                                            }
                                        }
                                        Text {
                                            height: parent.height
                                            verticalAlignment: Text.AlignVCenter
                                            text: modelData.value
                                            color: modelData.locked ? "#767A80" : "#F0F0F0"
                                            font.family: overlayTab._mono
                                            font.pixelSize: panelCard._main
                                        }
                                        Canvas {
                                            visible: !modelData.locked
                                            width: panelCard._arrowW
                                            height: parent.height
                                            property color tint: psRow._sel ? "#C2C6CC" : "#666A70"
                                            onTintChanged: requestPaint()
                                            onPaint: {
                                                var c = getContext("2d")
                                                c.reset()
                                                var h = Math.max(8, panelCard._mainH / 2)
                                                var t = (height - h) / 2
                                                c.fillStyle = tint
                                                c.beginPath()
                                                c.moveTo(0, t)
                                                c.lineTo(0, t + h)
                                                c.lineTo(width, t + h / 2)
                                                c.closePath()
                                                c.fill()
                                            }
                                        }
                                    }
                                }
                            }

                            Item { width: 1; height: 9 }
                            Rectangle {
                                x: panelCard._pad
                                width: panelCard.width - panelCard._pad * 2
                                height: 1
                                color: "#333339"
                            }
                            Item { width: 1; height: 9 }

                            Item {
                                width: parent.width
                                height: panelCard._smallH + 8
                                Text {
                                    x: panelCard._pad
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    text: panelCard._status
                                    color: "#808080"
                                    font.family: overlayTab._mono
                                    font.pixelSize: panelCard._small
                                }
                            }

                            // Footer: the vendor button glyphs the panel rasterises for real.
                            // ⚠️ PadGlyph follows the detected pad when the glyph set is Auto,
                            // where the panel's own padIcon() falls back to Xbox — the two
                            // differ only in that case, and padIcon() is the odd one out.
                            Item {
                                width: parent.width
                                height: panelCard._mainH + 12
                                Row {
                                    x: panelCard._pad
                                    height: parent.height
                                    spacing: panelCard._colGap

                                    Repeater {
                                        model: [
                                            { key: "A", label: panelCard._footer[0] },
                                            { key: "B", label: panelCard._footer[1] },
                                            { key: "Y", label: panelCard._footer[2] }
                                        ]
                                        delegate: Row {
                                            height: parent.height
                                            spacing: 7
                                            PadGlyph {
                                                anchors.verticalCenter: parent.verticalCenter
                                                buttonKey: modelData.key
                                                size: panelCard._mainH + 2
                                            }
                                            Text {
                                                height: parent.height
                                                verticalAlignment: Text.AlignVCenter
                                                text: modelData.label
                                                color: "#CED2D6"
                                                font.family: overlayTab._mono
                                                font.pixelSize: panelCard._main
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

            }
            // ──────────────────────────────────────────────────────────────────
            //                              ABOUT TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: aboutTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 8
                spacing: 16

                // StreamLight card — title + version + author on the left,
                // action buttons aligned to the right edge.
                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: 76

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 18
                        spacing: 12
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "StreamLight"
                            font.family: "DM Sans"
                            font.pixelSize: 22
                            font.bold: true
                            color: settingsScreen._text
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: settingsScreen.streamLightLatest.length > 0
                                  ? settingsScreen.streamLightLatest
                                  : qsTr("checking…")
                            font.family: "DM Sans"
                            font.pixelSize: 14
                            color: settingsScreen._greenLk
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("by FoggyBytes")
                            font.family: "DM Sans"
                            font.pixelSize: 13
                            color: settingsScreen._textDim
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 16
                        spacing: 10
                        AboutLinkButton {
                            id: aboutSlGithubBtn
                            label: qsTr("GitHub releases")
                            url:   "https://github.com/FoggyBytes/StreamLight/releases"
                            KeyNavigation.right: aboutSlGplBtn
                            KeyNavigation.down:  aboutStGithubBtn
                        }
                        AboutLinkButton {
                            id: aboutSlGplBtn
                            label: qsTr("GPL v3")
                            url:   "https://www.gnu.org/licenses/gpl-3.0.html"
                            KeyNavigation.left:  aboutSlGithubBtn
                            KeyNavigation.right: aboutSlDonateBtn
                            KeyNavigation.down:  aboutStGithubBtn
                        }
                        AboutLinkButton {
                            id: aboutSlDonateBtn
                            label: qsTr("Donate")
                            url:   "https://paypal.me/foggypunk"
                            KeyNavigation.left: aboutSlGplBtn
                            KeyNavigation.down: aboutStGithubBtn
                        }
                    }
                }

                // StreamTweak card — same single-row layout.
                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: 76

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 18
                        spacing: 12
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "StreamTweak"
                            font.family: "DM Sans"
                            font.pixelSize: 22
                            font.bold: true
                            color: settingsScreen._text
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: settingsScreen.streamTweakLatest.length > 0
                                  ? settingsScreen.streamTweakLatest
                                  : qsTr("checking…")
                            font.family: "DM Sans"
                            font.pixelSize: 14
                            color: settingsScreen._greenLk
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("by FoggyBytes")
                            font.family: "DM Sans"
                            font.pixelSize: 13
                            color: settingsScreen._textDim
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 16
                        AboutLinkButton {
                            id: aboutStGithubBtn
                            label: qsTr("GitHub releases")
                            url:   "https://github.com/FoggyBytes/StreamTweak/releases"
                            KeyNavigation.up: aboutSlGithubBtn
                        }
                    }
                }
            }

            // ──────────────────────────────────────────────────────────────────
            //                            SHORTCUTS TAB
            // ──────────────────────────────────────────────────────────────────
            Column {
                id: shortcutsTab
                anchors.left: parent.left
                anchors.right: parent.right
                visible: tabBar.currentIndex === 7
                spacing: 16

                property var kbModel: ShortcutManager.keyboardModel()
                property var padModel: ShortcutManager.gamepadModel()

                Connections {
                    target: ShortcutManager
                    function onShortcutsChanged() {
                        shortcutsTab.kbModel = ShortcutManager.keyboardModel()
                        shortcutsTab.padModel = ShortcutManager.gamepadModel()
                    }
                }

                component KeyCap: Rectangle {
                    property string text: ""
                    width: kcl.implicitWidth + 16
                    height: 28
                    radius: 6
                    color: "#23262e"
                    border.color: "#3a3f4a"
                    border.width: 1
                    Label {
                        id: kcl
                        anchors.centerIn: parent
                        text: parent.text
                        color: "#dfe2e8"
                        font.family: "DM Sans"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }

                // Styled to match the standalone "Custom" pill button (PillButton):
                // same container, geometry and colours.
                component MiniButton: Button {
                    property string label: ""
                    signal triggered()
                    activeFocusOnTab: true
                    implicitHeight: 36
                    onClicked: triggered()
                    Keys.onReturnPressed: triggered()
                    Keys.onEnterPressed:  triggered()
                    Keys.onSpacePressed:  triggered()
                    background: Rectangle {
                        radius: 8
                        color: Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07))
                        border.color: parent.activeFocus ? Theme.accent : "#2a2a2a"
                        border.width: parent.activeFocus ? 3 : 1
                    }
                    contentItem: Label {
                        text: parent.label
                        color: "#a0a0a0"
                        font.family: "DM Sans"
                        font.pixelSize: 13
                        leftPadding: 16
                        rightPadding: 16
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // ── CONTROLLER GLYPHS ─────────────────────────────────────────
                Label {
                    text: qsTr("Controller glyphs")
                    font.family: "DM Sans"; font.pixelSize: 13; font.bold: true
                    font.letterSpacing: 1.4; font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut; leftPadding: 14
                }
                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: settingsScreen._rowHeightTall
                    Item {
                        width: parent.width
                        height: settingsScreen._rowHeightTall
                        Column {
                            anchors.left: parent.left; anchors.leftMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Label {
                                text: qsTr("Button icon set")
                                font.family: "DM Sans"; font.pixelSize: 16; font.bold: true
                                color: settingsScreen._text
                            }
                            Label {
                                text: qsTr("Auto follows the connected pad. Force a vendor for generic controllers.")
                                font.family: "DM Sans"; font.pixelSize: 13
                                color: settingsScreen._textDim
                            }
                        }
                        SegmentedSelector {
                            id: glyphSetSelector
                            anchors.right: parent.right; anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            labels: [qsTr("Auto"), qsTr("Xbox"), qsTr("PlayStation"), qsTr("Nintendo")]
                            Binding on currentIndex { value: StreamingPreferences.glyphSet }
                            onActivated: function(idx) {
                                StreamingPreferences.glyphSet = idx
                                StreamingPreferences.save()
                                SdlGamepadKeyNavigation.refreshGlyphPreference()
                            }
                        }
                    }
                }

                // ── GAMEPAD ───────────────────────────────────────────────────
                Label {
                    text: qsTr("Controller")
                    font.family: "DM Sans"; font.pixelSize: 13; font.bold: true
                    font.letterSpacing: 1.4; font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut; leftPadding: 14
                }
                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: padCol.implicitHeight + 8
                    Column {
                        id: padCol
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.top: parent.top; anchors.topMargin: 4
                        spacing: 0
                        // Bind rules for gamepad combos.
                        Item {
                            width: padCol.width
                            implicitHeight: padRules.implicitHeight + 20
                            Label {
                                id: padRules
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.leftMargin: 16; anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Hold the listed buttons together. A combo must use at least 3 buttons, one of them Start / Select / LB / RB, so it can't fire during normal play.")
                                wrapMode: Text.WordWrap
                                font.family: "DM Sans"; font.pixelSize: 13
                                color: settingsScreen._textDim
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                x: 16; width: parent.width - 32; height: 1
                                color: settingsScreen._border
                            }
                        }
                        Repeater {
                            model: shortcutsTab.padModel
                            delegate: Item {
                                width: padCol.width
                                height: 56
                                property var rd: modelData
                                Label {
                                    anchors.left: parent.left; anchors.leftMargin: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width * 0.40
                                    text: rd.name
                                    elide: Text.ElideRight
                                    font.family: "DM Sans"; font.pixelSize: 15
                                    color: settingsScreen._text
                                }
                                Row {
                                    anchors.right: parent.right; anchors.rightMargin: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 6
                                    Repeater {
                                        model: rd.buttons
                                        // 26 = the status bar's face-button size, so a combo
                                        // shown here and a prompt shown there are the same button
                                        // at the same size.
                                        delegate: PadGlyph {
                                            buttonKey: modelData.key
                                            label: modelData.label
                                            size: 26
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                    Item { width: 8; height: 1 }
                                    MiniButton {
                                        label: qsTr("Rebind")
                                        onTriggered: padCaptureDialog.openFor(rd.action, rd.name, rd.mask)
                                    }
                                    MiniButton {
                                        label: qsTr("Reset")
                                        onTriggered: ShortcutManager.resetGamepad(rd.action)
                                    }
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    x: 16; width: parent.width - 32; height: 1
                                    color: settingsScreen._border
                                    visible: index < shortcutsTab.padModel.length - 1
                                }
                            }
                        }
                    }
                }

                // ── KEYBOARD ──────────────────────────────────────────────────
                Label {
                    text: qsTr("Keyboard")
                    font.family: "DM Sans"; font.pixelSize: 13; font.bold: true
                    font.letterSpacing: 1.4; font.capitalization: Font.AllUppercase
                    color: settingsScreen._textMut; leftPadding: 14
                }
                Rectangle {
                    width: parent.width
                    color: settingsScreen._bg2
                    radius: 8
                    border.color: settingsScreen._border
                    border.width: 1
                    implicitHeight: kbCol.implicitHeight + 8
                    Column {
                        id: kbCol
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.top: parent.top; anchors.topMargin: 4
                        spacing: 0
                        // Bind rules for keyboard combos.
                        Item {
                            width: kbCol.width
                            implicitHeight: kbRules.implicitHeight + 20
                            Label {
                                id: kbRules
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.leftMargin: 16; anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Use at least two of Ctrl / Alt / Shift plus one key. Heavier combos are less likely to clash with software running on the host.")
                                wrapMode: Text.WordWrap
                                font.family: "DM Sans"; font.pixelSize: 13
                                color: settingsScreen._textDim
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                x: 16; width: parent.width - 32; height: 1
                                color: settingsScreen._border
                            }
                        }
                        Repeater {
                            model: shortcutsTab.kbModel
                            delegate: Item {
                                width: kbCol.width
                                height: 56
                                property var rd: modelData
                                Label {
                                    anchors.left: parent.left; anchors.leftMargin: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width * 0.42
                                    text: rd.name
                                    elide: Text.ElideRight
                                    font.family: "DM Sans"; font.pixelSize: 15
                                    color: settingsScreen._text
                                }
                                Row {
                                    anchors.right: parent.right; anchors.rightMargin: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 7
                                    Repeater {
                                        model: ShortcutManager.modifierLabel(rd.modifiers).split(" + ").concat([rd.label])
                                        delegate: KeyCap { text: modelData }
                                    }
                                    Item { width: 8; height: 1 }
                                    MiniButton {
                                        label: qsTr("Rebind")
                                        onTriggered: {
                                            kbCaptureDialog.action = rd.action
                                            kbCaptureDialog.actionName = rd.name
                                            kbCaptureDialog.open()
                                        }
                                    }
                                    MiniButton {
                                        label: qsTr("Reset")
                                        onTriggered: ShortcutManager.resetKeyboard(rd.action)
                                    }
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    x: 16; width: parent.width - 32; height: 1
                                    color: settingsScreen._border
                                    visible: index < shortcutsTab.kbModel.length - 1
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Shown after the user enables "Auto-start Tailscale". A restart is required
    // because Tailscale is launched once at StreamLight startup; toggling the
    // preference at runtime does not retroactively spawn it. Yes → restart now;
    // No → preference is still saved, takes effect at the next manual launch.
    NavigableMessageDialog {
        id: tailscaleRestartDialog
        headerText: qsTr("RESTART REQUIRED")
        text: qsTr("StreamLight needs to restart to start Tailscale in the background. Restart now?")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: SystemProperties.restartApplication()
    }

    // Shown after the user disables "Auto-start Tailscale". The currently
    // running Tailscale instance is intentionally NOT killed — if the user
    // started it manually or in a previous StreamLight session, killing it
    // would be surprising. The toggle only affects future StreamLight launches.
    NavigableMessageDialog {
        id: tailscaleStopNoticeDialog
        headerText: qsTr("TAILSCALE")
        text: qsTr("Tailscale will keep running until you close it manually or reboot. StreamLight will no longer start it automatically on future launches.")
        standardButtons: Dialog.Ok
    }

    // Capture dialogs for the Shortcuts tab. Persist via ShortcutManager, which
    // emits shortcutsChanged so the rows refresh immediately.
    ShortcutCaptureDialog {
        id: kbCaptureDialog
        onCaptured: function(action, modifiers, sdlKey, sdlScan, label) {
            ShortcutManager.setKeyboardBinding(action, modifiers, sdlKey, sdlScan, label)
        }
    }
    GamepadCaptureDialog {
        id: padCaptureDialog
        onCaptured: function(action, mask) {
            ShortcutManager.setGamepadBinding(action, mask)
        }
    }

    // Reusable styled link button — opens `url` in the system browser / shell.
    component AboutLinkButton: Button {
        id: btn
        property string label: ""
        property string url: ""
        text: label
        activeFocusOnTab: true
        onClicked: Qt.openUrlExternally(url)
        Keys.onReturnPressed: Qt.openUrlExternally(url)
        Keys.onEnterPressed:  Qt.openUrlExternally(url)
        Keys.onSpacePressed:  Qt.openUrlExternally(url)

        // Suppress focus highlight in pointer mode, hover highlight in key mode,
        // so the two never light up two different buttons at the same time.
        readonly property bool _keyFocused: activeFocus && SdlGamepadKeyNavigation.inputMode !== "pointer"
        readonly property bool _hoverVisible: hovered && SdlGamepadKeyNavigation.inputMode !== "key"

        background: Rectangle {
            implicitWidth:  170
            implicitHeight: 36
            radius: 6
            color: btn._keyFocused   ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                 : btn._hoverVisible ? "#262626"
                 :                     "#1f1f1f"
            border.color: (btn._keyFocused || btn._hoverVisible)
                          ? settingsScreen._greenLk
                          : settingsScreen._border
            border.width: btn._keyFocused ? 2 : 1
        }
        contentItem: Label {
            text: btn.label
            color: settingsScreen._text
            font.family: "DM Sans"
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
