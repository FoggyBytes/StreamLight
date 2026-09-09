import QtQuick
import QtQuick.Window

import Theme 1.0

/*
 * ── One game, presented (5.7.0) ──────────────────────────────────────────────────────────
 *
 * Cover, title, one meta line, and the two figures that matter: hours and sessions.
 *
 * ⚠️ It exists because BOTH screens show this and they had drifted. Home laid the text out
 * beside the cover with no shadow and four figures; the host page put it underneath, centred
 * and shadowed, with two. One idea wearing two faces — the eye has to re-learn the screen
 * every time it moves between them, and every future change has to be made twice, which is
 * how they diverged in the first place. One component, two callers, and they cannot disagree
 * again.
 *
 * The picture is HeroCover, so the shadow here and the shadow in the spotlight are not
 * "matched": they are the same code.
 *
 * The caller composes `meta` and gives `availableHeight`; this works out the cover from what
 * is left. See coverHeight.
 */
Item {
    id: root

    /*
     * Optional badge above the picture, in two tones: the statement, then the quieter figure
     * beside it — "LAST PLAYED FOR 18 H 42 M" · "2 H AGO".
     *
     * Home needs one; without it the card simply has a game on it and never says why. The
     * spotlight does not: the library beside it already establishes that this is the selected
     * game.
     *
     * ⚠️ Built to the recipe of the state chips on the host card — same height, radius, border
     * and typography — so the two sides of the card read as one row rather than as a caption
     * that happens to sit nearby. That is also why the hours live in here rather than under
     * the title: said once, in the place that is already saying when.
     */
    property string badgeMain: ""
    property string badgeMuted: ""
    property string title: ""
    property url    cover: ""

    // The storefront, by name — "Steam", "Epic Games", … Its mark is resolved here, so the
    // map lives in ONE place and both screens get the same badge.
    //
    // ⚠️ It used to be a copy of this table in AppsScreen, used by the spotlight only, and
    // rebuilding the spotlight out of this component quietly dropped the icon. A second copy
    // for Home would have been the third.
    property string store: ""
    // Everything else on that line, already joined: "2 h ago", "running now · custom
    // settings". The store is kept apart because it is the half with a picture.
    property string metaExtra: ""
    property color  metaColor: Theme.text2

    function storeIconSource(s) {
        if (s === "Steam")           return "qrc:/res/store_steam.svg"
        if (s === "Epic Games")      return "qrc:/res/store_epic.svg"
        if (s === "GOG")             return "qrc:/res/store_gog.svg"
        if (s === "Ubisoft Connect") return "qrc:/res/store_ubisoft.svg"
        if (s === "Xbox")            return "qrc:/res/store_xbox.svg"
        if (s === "Battle.net")      return "qrc:/res/store_battlenet.svg"
        if (s === "EA App")          return "qrc:/res/store_ea.svg"
        return ""
    }
    property string played: ""      // already formatted — PlaytimeManager::formatDuration
    property int    sessions: 0

    property bool showShadow: true

    property real u: 1.0
    function _px(n) { return Math.round(n * u) }

    /*
     * ── How big the cover gets ───────────────────────────────────────────────────────────
     *
     * Three limits, smallest wins:
     *
     *   1. what is left once the text has taken its room — MEASURED, not estimated, so a
     *      title that wraps to three lines shortens the picture instead of pushing the
     *      figures off the bottom of the card;
     *   2. a ceiling the caller may set, when the layout wants it smaller than the room;
     *   3. ⚠️ 900 PHYSICAL pixels. Box art tops out at 600x900 (Steam's library capsule) and
     *      BoxArtManager rejects anything under 600 tall, so beyond that we would be scaling
     *      up pixels that do not exist and the biggest picture on screen would be the softest.
     *      Same ceiling, same reason, as the spotlight's own 340.
     *
     * ⚠️ NOT `_px(constant)`. The interface scale is clamped at 1.60, so on a 4K panel the
     * card grows far more than the scale does and a fixed size would leave the bottom of it
     * empty.
     */
    property int availableHeight: 400
    property int maxCoverHeight: 100000

    /*
     * ── The title box: fixed, or as tall as the words need ───────────────────────────────
     *
     * `false` (the spotlight): the box is a constant, so the picture keeps one size while you
     * walk the library. ⚠️ Do not turn it on there — the title changes with every row, and a
     * cover that resized on each press of the stick would be far worse than a title box with
     * some air in it.
     *
     * `true` (Home): the box takes exactly the room the title needs, so the cover grows when
     * a name fits on one line and gives way when it takes three. The card shows one game
     * until you change host, so nothing is jumping.
     */
    property bool titleFitsContent: false

    /*
     * ⚠️ The text column's width comes from `root.width` — which the CALLER sets — and not
     * from the cover, and that is what keeps the sizing acyclic. With titleFitsContent on,
     * the chain is: probe width → probe height → title box → text column height → cover
     * height → cover width. Sizing the column off the cover (it used to be `art.width * 1.7`)
     * closes that loop and QML reports a binding loop, or worse settles on a size that
     * depends on which property happened to be evaluated first.
     */
    readonly property int _textW: Math.min(width, _px(430))

    // Measures the title at FULL size, wrapped to the real column width, so we know how many
    // lines it wants before deciding how much room to leave it. maximumLineCount caps the
    // answer at three; past that Text.Fit shrinks the type to fit those three.
    Text {
        id: titleProbe
        visible: false
        width: root._textW
        text: root.title
        font.family: Theme.family
        font.pixelSize: root._px(Theme.fontDisplay)
        font.weight: Font.Bold
        font.letterSpacing: -root.u * 0.7
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideNone
    }

    readonly property int _titleBoxH:
        titleFitsContent ? Math.max(_px(40), Math.ceil(titleProbe.contentHeight) + _px(6))
                         : _px(96)

    readonly property int coverHeight: {
        var byRoom  = availableHeight - textCol.implicitHeight - _px(18)
                      - (badgeMain.length > 0 ? _px(48) : 0)
        var bySharp = Math.round(900 / Math.max(1, Screen.devicePixelRatio))
        // ⚠️ The picture is 2:3, so its height also decides its WIDTH — and nothing else here
        // was stopping that width from running past the block it lives in. At 4K with no
        // scaling the sharpness ceiling alone came within a few pixels of the panel's edge,
        // which is not a margin, it is a coincidence waiting to stop holding.
        var byWidth = Math.round(width * 3 / 2)
        return Math.max(_px(90), Math.min(byRoom, maxCoverHeight, bySharp, byWidth))
    }

    implicitWidth: Math.max(art.width, textCol.width)
    implicitHeight: col.implicitHeight

    Column {
        id: col
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root._px(18)

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.badgeMain.length > 0
            height: visible ? root._px(30) : 0
            width: badgeRow.implicitWidth + root._px(26)
            radius: root._px(6)
            color: "transparent"
            border.color: Theme.line
            border.width: 1

            Row {
                id: badgeRow
                anchors.centerIn: parent
                spacing: root._px(9)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.badgeMain.toUpperCase()
                    color: Theme.text2
                    font.family: Theme.family
                    font.pixelSize: root._px(Theme.fontBody)
                    font.weight: Font.DemiBold
                    font.letterSpacing: root.u * 1.1
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: text.length > 0
                    text: root.badgeMuted.toUpperCase()
                    color: Theme.text3
                    font.family: Theme.family
                    font.pixelSize: root._px(Theme.fontBody)
                    font.weight: Font.DemiBold
                    font.letterSpacing: root.u * 1.1
                }
            }
        }

        HeroCover {
            id: art
            anchors.horizontalCenter: parent.horizontalCenter
            height: root.coverHeight
            source: root.cover
            radius: root._px(10)
            shadow: root.showShadow && !Theme.reduceAnimations
            shadowOffset: root._px(10)

            // The artwork fades in rather than appearing: covers load asynchronously off
            // disk, and a picture that pops is the one thing on the screen big enough for
            // the pop to register.
            //
            // ⚠️ Opacity only. Do NOT animate width or height here — resizing this item is
            // what deformed the cover for three releases; the block at the top of
            // HeroCover.qml has the measurements.
            opacity: Theme.reduceAnimations ? 1 : 0
            Component.onCompleted: opacity = 1
            Behavior on opacity {
                enabled: !Theme.reduceAnimations
                NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
            }
        }

        Column {
            id: textCol
            anchors.horizontalCenter: parent.horizontalCenter
            width: root._textW
            spacing: root._px(4)

            /*
             * ── The title, which is never cut ────────────────────────────────────────
             *
             * ⚠️ The height is explicit and that is not decoration: Qt defines Text.Fit as
             * the largest size fitting the width AND the height, so without one there is no
             * vertical bound to shrink against. ElideNone is what actually switches
             * truncation off — Qt elides "as per the elide property" when the text will not
             * fit even at minimumPixelSize. Text.Wrap rather than WordWrap so a single word
             * longer than the box breaks instead of running past it.
             *
             * Fixing the height also stops the figures below jumping as the selection moves
             * between a one-line title and a three-line one.
             */
            Text {
                id: titleText
                width: parent.width
                height: root._titleBoxH
                text: root.title
                color: Theme.text
                font.family: Theme.family
                font.pixelSize: root._px(Theme.fontDisplay)
                font.weight: Font.Bold
                font.letterSpacing: -root.u * 0.7
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                fontSizeMode: Text.Fit
                minimumPixelSize: root._px(16)
                wrapMode: Text.Wrap
                elide: Text.ElideNone

                /*
                 * ⚠️ NO maximumLineCount here, and that is the whole reason a title stopped
                 * being cut. Measured: "METAL GEAR SOLID 4: Guns of the Patriots – Master
                 * Collection Version" came out as three full-size lines with the last word
                 * missing.
                 *
                 * Text.Fit and maximumLineCount do not cooperate. Fit shrinks the type until
                 * the text fits the width and the height — but with a line cap the text is
                 * already truncated to three lines, so it DOES fit, Fit sees nothing to do,
                 * and the words past the cap are simply gone. Fit was doing its job on a
                 * string that had been cut before it ever saw it.
                 *
                 * Without the cap, Fit shrinks until the WHOLE title fits the box, taking a
                 * fourth line at a smaller size if that is what it needs. The three-line
                 * ceiling still exists — it is titleProbe's, where it belongs: it caps how
                 * tall the box is allowed to grow, not how much of the name is shown.
                 */

                // Walking the library replaces this text many times a second. A cross-fade
                // says "this was replaced" without anything moving — the title box holds
                // still, only its contents change.
                opacity: 1
                onTextChanged: if (!Theme.reduceAnimations) titleSwap.restart()
                SequentialAnimation {
                    id: titleSwap
                    NumberAnimation { target: titleText; property: "opacity"; to: 0.0; duration: 70 }
                    NumberAnimation { target: titleText; property: "opacity"; to: 1.0
                                      duration: 190; easing.type: Easing.OutCubic }
                }
            }

            // The store's mark and the line about it. The mark earns its place by being
            // recognisable before the word beside it is read — which is the whole reason
            // storefronts have one.
            Row {
                id: metaRow
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: root._px(8)
                visible: metaText.text.length > 0

                Image {
                    id: storeIcon
                    anchors.verticalCenter: parent.verticalCenter
                    visible: source != ""
                    source: root.storeIconSource(root.store)
                    width: root._px(19); height: width
                    // Fixed at twice the design size rather than scaled with `u`: an SVG
                    // rasterises once at sourceSize, and binding that to a live value would
                    // re-rasterise on every window resize.
                    sourceSize.width: 38; sourceSize.height: 38
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                Text {
                    id: metaText
                    anchors.verticalCenter: parent.verticalCenter
                    // Its natural width, capped by what the column leaves once the mark has
                    // taken its share — so the row stays centred on short text and elides
                    // instead of running past the column on long text.
                    width: Math.min(implicitWidth,
                                    textCol.width - (storeIcon.visible
                                                     ? storeIcon.width + metaRow.spacing : 0))
                    text: {
                        var p = []
                        if (root.store.length > 0)      p.push(root.store)
                        if (root.metaExtra.length > 0)  p.push(root.metaExtra)
                        return p.join("  ·  ")
                    }
                    color: root.metaColor
                    font.family: Theme.family
                    font.pixelSize: root._px(Theme.fontBody)
                    elide: Text.ElideRight
                    maximumLineCount: 1

                    Behavior on color {
                        enabled: !Theme.reduceAnimations
                        ColorAnimation { duration: 190 }
                    }
                }
            }

            // Follows the figures it separates. A Column skips invisible children, so an
            // unconditional spacer left twelve points of nothing under the title on the Home
            // card, where there are no figures — and that is height the cover could have had.
            Item {
                width: 1
                height: root._px(12)
                visible: figures.visible
            }

            /*
             * ── Two figures, and only two ────────────────────────────────────────────
             * Hours and sessions — what this game is to you. Delivered frame rate and drop
             * rate used to be here: they answer how the network behaved in the last half
             * hour, not what this game is, and at this size they made the card read as a
             * diagnostics panel. Both are kept in full in the per-game panel.
             */
            Row {
                id: figures
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: root._px(30)
                visible: root.played.length > 0 || root.sessions > 0

                Repeater {
                    model: [
                        { v: root.played.length > 0 ? root.played : "—", c: qsTr("PLAYED") },
                        { v: root.sessions > 0 ? String(root.sessions) : "—",
                          c: root.sessions === 1 ? qsTr("SESSION") : qsTr("SESSIONS") }
                    ]

                    delegate: Column {
                        spacing: root._px(2)
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.v
                            color: Theme.text
                            font.family: Theme.family
                            font.pixelSize: root._px(Theme.fontH2)
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.c
                            color: Theme.text3
                            font.family: Theme.family
                            font.pixelSize: root._px(Theme.fontCaption)
                            font.letterSpacing: root.u * 1.5
                        }
                    }
                }
            }
        }
    }
}
