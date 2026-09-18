import QtQuick 2.15
import QtQuick.Controls 2.15

import Theme 1.0

/*
 * ── The tab strip of Settings, as a component (6.0.0) ────────────────────────────────────
 *
 * One strip for the three places that split settings into sections: the Settings screen,
 * the host profile dialog and the per-game dialog. Written once so the three are identical
 * by construction — the same labels, the same accent underline, the same LB/RB ring —
 * rather than by three copies kept in step by hand, which is how a UI starts to disagree
 * with itself.
 *
 * `tabs` is a list of { label } or { icon }. `counts` is a parallel list of integers: a number
 * above zero draws a small accent badge after that label. The dialogs use it for "this section
 * holds this many values of its own", because splitting the rows into tabs hides where a
 * profile's overrides are.
 *
 * ⚠️ It was a dot — a boolean — until 6.0.0, and a dot answers the wrong question. Inside a
 * dialog the user is deciding what to keep and what to give back, and "this tab has something"
 * leaves them opening five tabs to find two rows. The number costs the same badge.
 *
 * ⚠️ Counts are NOT fields of `tabs`, on purpose. A binding that rebuilt the tab list whenever a
 * value changed would make the Repeater recreate every TabButton, and a TabBar that loses its
 * items resets its current index — the section would jump back to the first tab mid-edit.
 *
 * `shoulders` draws the LB / RB prompts at the two ends of the strip (PgUp / PgDn once the
 * keyboard is in use — ActionHint decides). Settings leaves them off: there the status bar
 * already carries them. Clicking a prompt cycles like the button would.
 *
 * ⚠️ The ring WRAPS — see cycle(). And the strip never takes focus: sections are switched with
 * LB/RB or the mouse, never by walking onto the tabs with the D-pad.
 */
Item {
    id: root

    property var tabs: []
    property var counts: []
    property alias currentIndex: bar.currentIndex
    readonly property alias count: bar.count

    // The caller's own scale, so the strip matches the rows under it.
    property real u: Theme.uiScale
    function _px(n) { return Math.round(n * u) }

    property bool shoulders: false

    function cycle(dir) {
        if (bar.count <= 0) return
        bar.currentIndex = (bar.currentIndex + dir + bar.count) % bar.count
    }

    implicitHeight: _px(48)

    ActionHint {
        id: lbHint
        visible: root.shoulders
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        buttonKey: "LB"
        keyLabel: "PgUp"
        size: root._px(26)

        MouseArea {
            anchors.fill: parent
            anchors.margins: -root._px(6)
            cursorShape: Qt.PointingHandCursor
            onClicked: root.cycle(-1)
        }
    }

    ActionHint {
        id: rbHint
        visible: root.shoulders
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        buttonKey: "RB"
        keyLabel: "PgDn"
        size: root._px(26)

        MouseArea {
            anchors.fill: parent
            anchors.margins: -root._px(6)
            cursorShape: Qt.PointingHandCursor
            onClicked: root.cycle(1)
        }
    }

    TabBar {
        id: bar
        anchors.left: root.shoulders ? lbHint.right : parent.left
        anchors.right: root.shoulders ? rbHint.left : parent.right
        anchors.leftMargin: root.shoulders ? root._px(14) : 0
        anchors.rightMargin: root.shoulders ? root._px(14) : 0
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
                readonly property int  _count: (root.counts !== undefined && root.counts[index] > 0)
                                               ? root.counts[index] : 0

                text: modelData.label !== undefined ? modelData.label : ""
                focusPolicy: Qt.NoFocus
                font.family: Theme.family
                font.pixelSize: root._px(Theme.fontSmall)
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
                 * has to answer the question a Text would, for whichever child is drawn, dot
                 * included.
                 */
                contentItem: Item {
                    readonly property int _badgeSpace: tabButton._count > 0
                                                       ? root._px(18) + root._px(6) : 0
                    implicitWidth:  (modelData.icon !== undefined ? tabIcon.width : tabLabel.implicitWidth) + _badgeSpace
                    implicitHeight: Math.max(modelData.icon !== undefined ? tabIcon.height : tabLabel.implicitHeight,
                                             root._px(18))

                    Row {
                        anchors.centerIn: parent
                        spacing: root._px(6)

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
                        // The badge. Its own text decides its width, so a two-digit count
                        // does not clip — a profile can hold ten overrides in one section.
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: tabButton._count > 0
                            width: Math.max(root._px(18), countText.implicitWidth + root._px(10))
                            height: root._px(18)
                            radius: height / 2
                            color: Theme.accent

                            Text {
                                id: countText
                                anchors.centerIn: parent
                                text: tabButton._count
                                color: Theme.onAccent
                                font.family: Theme.family
                                font.pixelSize: root._px(Theme.fontCaption)
                                font.bold: true
                            }
                        }
                    }
                }
            }
        }
    }
}
