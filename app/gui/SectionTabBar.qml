import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15

import Theme 1.0

/*
 * ── The tab strip of Settings, as a component (6.0.0) ────────────────────────────────────
 *
 * One strip for the places that split settings into sections: the Settings screen and the
 * StreamTweak section. Written once so they are identical by construction — the same labels,
 * the same accent underline, the same LB/RB ring. (The host profile and per-game dialogs used
 * it too until 8.8.0, which gave them one list in sections instead.)
 *
 * `tabs` is a list of { label } or { icon }, with `dim: true` for a tab that can be visited but
 * has nothing to show yet (8.0.0, the host section). The counts on the tabs and the LB / RB
 * prompts at the two ends, which only the profile dialogs used, went with them in 8.8.0.
 *
 * ⚠️ The ring WRAPS — see cycle(). And the strip never takes focus: sections are switched with
 * LB/RB or the mouse, never by walking onto the tabs with the D-pad.
 */
Item {
    id: root

    property var tabs: []
    property alias currentIndex: bar.currentIndex
    readonly property alias count: bar.count

    // The caller's own scale, so the strip matches the rows under it.
    property real u: Theme.uiScale
    function _px(n) { return Math.round(n * u) }

    function cycle(dir) {
        if (bar.count <= 0) return
        bar.currentIndex = (bar.currentIndex + dir + bar.count) % bar.count
    }

    implicitHeight: _px(48)

    TabBar {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        focusPolicy: Qt.NoFocus
        /*
         * ⚠️ The buttons are as tall as the strip, and it is set HERE, on the bar. TabBar
         * sizes its buttons to contentHeight and lays them from the top; left to itself that
         * is the tallest button's implicit height, so on a strip taller than that the labels
         * sat high while the LB/RB prompts — centred on the strip — sat lower (§73.28 on).
         *
         * Setting the height on each TabButton instead was tried and is wrong: the buttons
         * grew, but the Material style's animated underline — the ListView highlight in its
         * TabBar template — did not follow, and the strip showed TWO accent lines a few
         * pixels apart. Sizing through the bar moves buttons and highlight together.
         */
        contentHeight: bar.availableHeight

        /*
         * The sliding underline stays Material's own highlight, animated as before — only
         * quieter (05/11/2026, Marcello, with the glass buttons of docs/mockups/pulsanti): the
         * accent at 55%, the hairline a chosen value wears, instead of the full accent. The
         * highlight is drawn in Material.accentColor, so the colour is set here and the
         * template is not touched. A glass pill on the current tab was tried and turned down:
         * the underline and its slide are what Marcello wants on tabs.
         */
        Material.accent: Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.55)

        background: Rectangle {
            color: "transparent"
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.line
            }
        }

        Repeater {
            model: root.tabs

            TabButton {
                id: tabButton

                readonly property bool _current: bar.currentIndex === index

                text: modelData.label !== undefined ? modelData.label : ""
                focusPolicy: Qt.NoFocus
                font.family: Theme.family
                font.pixelSize: root._px(Theme.fontSmall * Theme.textScale)
                font.bold: true
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8

                // ⚠️ No underline of our own. The Material TabBar already draws the current
                // tab's line, animated, as its ListView highlight; this background used to draw
                // a second, static one at the same spot. The two hid each other while they
                // coincided and showed as a double line the moment they did not.
                background: Item {}

                /*
                 * ⚠️ The implicit size is NOT optional, and leaving it out is what crammed every
                 * tab into the left end of the bar for one build (§69).
                 *
                 * A Control takes its own implicitWidth from its contentItem's, and a bare Item
                 * has an implicitWidth of ZERO — so every TabButton reported itself as nothing
                 * but padding and TabBar packed them into a couple of hundred pixels. The wrapper
                 * has to answer the question a Text would, for whichever child is drawn.
                 */
                contentItem: Item {
                    implicitWidth:  (modelData.icon !== undefined ? tabIcon.width : tabLabel.implicitWidth)
                    implicitHeight: modelData.icon !== undefined ? tabIcon.height : tabLabel.implicitHeight

                    Row {
                        anchors.centerIn: parent
                        spacing: root._px(6)
                        // A tab with nothing to show yet (`dim`): greyed, still reachable.
                        opacity: modelData.dim === true ? (tabButton._current ? 0.6 : 0.4) : 1

                        Text {
                            id: tabLabel
                            anchors.verticalCenter: parent.verticalCenter
                            visible: modelData.icon === undefined
                            text: tabButton.text
                            font: tabButton.font
                            color: tabButton._current ? Theme.text : Theme.text2
                        }
                        Image {
                            id: tabIcon
                            anchors.verticalCenter: parent.verticalCenter
                            visible: modelData.icon !== undefined
                            // A shade taller than the labels beside it, not a badge: a mark has
                            // to carry at a glance where a word carries by shape. The source is
                            // far larger than 26 px even at 4K and 200 %, hence `smooth`.
                            height: root._px(26)
                            width: root._px(26)
                            source: modelData.icon !== undefined ? modelData.icon : ""
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                            opacity: tabButton._current ? 1.0 : 0.55
                        }
                    }
                }
            }
        }
    }
}
