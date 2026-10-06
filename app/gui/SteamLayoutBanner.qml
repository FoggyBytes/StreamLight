import QtQuick 2.15
import QtQuick.Controls 2.15
import Theme 1.0
import WindowMove 1.0

// "Steam is sending this controller as keyboard and mouse too" (6.5.1, #24).
//
// Steam's Desktop Layout sends a controller's buttons as keys and clicks while SDL reads the
// same pad, so every press arrives twice — in the menus and, worse, in the game on the host.
// Nothing in the app can undo that for good; switching Steam to its Gamepad layout does. So
// the app says so: SdlGamepadKeyNavigation counts the pad/key pairs it matches and reports the
// third, once per launch (AppShell decides when it may be shown).
//
// Deliberately not a dialog: it asks nothing, takes no focus and goes away by itself after
// shownMs. It is not remembered either — it comes back on the next launch for as long as the
// layout is still Desktop, and stops by itself once it is not.
Item {
    id: banner

    readonly property int shownMs: 12000

    // Only the Steam Controller of #24 is known to switch on a long press of Menu.
    property bool menuHoldSwitches: false

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    property bool _shown: false
    readonly property int _fadeMs: Theme.reduceAnimations ? 0 : 300

    function show(menuHold) {
        menuHoldSwitches = menuHold
        drain.restart()
        _shown = true
    }

    width: Math.min(parent ? parent.width - _px(32) : _px(820), _px(820))
    height: card.height
    visible: opacity > 0
    opacity: _shown ? 1 : 0
    enabled: false   // nothing here takes input: the page under it keeps the pad and the focus

    Behavior on opacity { NumberAnimation { duration: banner._fadeMs; easing.type: Easing.OutCubic } }

    Rectangle {
        id: card
        width: parent.width
        height: content.implicitHeight + banner._px(18) * 2
        // Drops in from just above its place as it fades in.
        y: banner._shown ? 0 : -banner._px(16)
        Behavior on y { NumberAnimation { duration: banner._fadeMs; easing.type: Easing.OutCubic } }
        radius: banner._px(10)
        color: Theme.cardHigh
        border.color: Theme.lineHigh
        border.width: 1
        clip: true

        Rectangle {
            width: banner._px(5)
            height: parent.height
            color: Theme.warning
        }

        Row {
            id: content
            x: banner._px(26)
            y: banner._px(18)
            width: parent.width - x - banner._px(22)
            spacing: banner._px(16)

            Image {
                id: icon
                anchors.verticalCenter: parent.verticalCenter
                source: "qrc:/res/notice_pad.svg"
                width: banner._px(40)
                height: banner._px(27)
                sourceSize.width: width * 2
                sourceSize.height: height * 2
                fillMode: Image.PreserveAspectFit
            }

            Column {
                width: content.width - icon.width - content.spacing
                spacing: banner._px(4)

                Label {
                    width: parent.width
                    text: qsTr("Steam is sending this controller as keyboard and mouse too")
                    font.family: Theme.family
                    font.pixelSize: banner._px(Theme.fontTitle)
                    font.bold: true
                    color: Theme.text
                    wrapMode: Text.WordWrap
                }

                // Words and the Menu glyph in one wrapping line: the button is drawn, not named
                // (pad glyph rule), at the size the status bar draws it. "MENU" marks its place.
                Flow {
                    id: remedy
                    width: parent.width
                    spacing: banner._px(5)

                    readonly property var _tokens: banner.menuHoldSwitches
                        ? (qsTr("Switch it to Gamepad (hold") + " MENU "
                           + qsTr("for 3 s), or launch StreamLight from Steam.")).split(" ")
                        : qsTr("Switch it to Gamepad, or launch StreamLight from Steam.").split(" ")

                    Repeater {
                        model: remedy._tokens
                        delegate: Item {
                            id: token
                            required property string modelData
                            readonly property bool _glyph: modelData === "MENU"
                            width: _glyph ? glyph.implicitWidth : word.implicitWidth
                            height: banner._px(24)

                            Label {
                                id: word
                                visible: !token._glyph
                                text: token._glyph ? "" : token.modelData
                                anchors.verticalCenter: parent.verticalCenter
                                font.family: Theme.family
                                font.pixelSize: banner._px(Theme.fontBody)
                                color: Theme.text2
                            }
                            PadGlyph {
                                id: glyph
                                visible: token._glyph
                                anchors.verticalCenter: parent.verticalCenter
                                buttonKey: "START"
                                label: "Menu"
                                size: banner._px(26)
                            }
                        }
                    }
                }
            }
        }

        // What is left of the time on screen.
        Rectangle {
            id: countdown
            anchors.bottom: parent.bottom
            x: banner._px(5)
            height: banner._px(3)
            width: (parent.width - x) * fraction
            color: Theme.warning
            opacity: 0.8
            property real fraction: 1

            NumberAnimation {
                id: drain
                target: countdown
                property: "fraction"
                from: 1
                to: 0
                duration: banner.shownMs
                // It is also the banner's clock. A one-off, but held while the window is dragged
                // like every other animation here — with the basic render loop each frame is
                // presented inside the move loop — and the time on screen is held with it.
                paused: running && WindowMove.moving
                onFinished: banner._shown = false
            }
        }
    }
}
