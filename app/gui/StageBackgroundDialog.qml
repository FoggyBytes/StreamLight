import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import QtQuick.Dialogs

import Theme 1.0
import SdlGamepadKeyNavigation 1.0

/*
 * A host's background (7.0.0): a picture over the card, and how much of the waves shows
 * through the card. A small popup centred on the right side of Home,
 * beside the card — which stays whole on the left and IS the preview.
 *
 * ⚠️ No colours. Up to 7.0.0's first build a host could have a hue of its own (a swatch, a
 * colour taken from a cover, a pair derived from a picture); that was dropped on 08/10/2026 by
 * decision. The card's colours are the theme's and the accent's.
 *
 *   PICTURE        Choose… (the file dialog) and the last three pictures used, on any host
 *   Card opacity   how opaque the card is over the waves, 70–100%, default 90% — exactly the
 *                  slider of 6.0.0–6.5.1 (Marcello, 09/10/2026, after a few hours as an
 *                  inverted 0–30% "Transparency")
 *
 * ⚠️ Moving the cursor over the pictures only moves the ring (09/10/2026): A picks the one
 * under it — and on Choose… opens the file dialog; A is always "Select". It used to pick as it
 * moved, so walking past a picture on the way back from Choose… put it on the card.
 *
 * ⚠️ B is BACK, and what is on the card stays (Marcello, 09/10/2026: «con B dovrei tornare
 * alla home, non cancellare… eliminando l'immagine»). Every change applies as it is made — a
 * picture picked with A, the slider as it moves — and every way out keeps it. There is no
 * Cancel: X clears the picture. Until then B put back what the popup had opened on, which
 * with the pad read as "the picture I just picked was thrown away".
 *
 * ⚠️ What A, X and B do is said in the app's status bar, which the popup takes over while it is
 * open (AppShell.takeOverBar) — not in a footer of its own: with no scrim the bar stays in
 * sight, and two rows of prompts on screen at once read as two sets of controls.
 */
Popup {
    id: dlg

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    property string hostName: ""
    // What the host is using now, so the current choice reads as selected.
    property string currentImage: ""
    property int currentOpacity: 90
    property int opacityMin: 70
    // Handed in by HomeScreen: where the recent pictures come from.
    property var computerModel: null
    property int hostIndex: -1
    // Handed in by HomeScreen: whose status bar says what A, X and B do while this is open.
    property var appShell: null
    // Handed in by HomeScreen: the host stage, and how far from its left the card ends. The
    // popup is centred in what is left of the stage on the right, both ways.
    property Item dockItem: null
    property real dockLeft: 0

    signal chosen(string imagePath)
    signal opacityChosen(int percent)

    // The picture on the card now.
    property string _selImage: ""
    property var _recent: []

    // The status bar while the popup is open — see the note at the top.
    readonly property var barHints: [
        { btn: "A", key: "Enter", act: qsTr("Select"), kind: "select" },
        { btn: "X", key: "Del",   act: qsTr("Clear"),  kind: "clear" },
        { btn: "B", key: "Esc",   act: qsTr("Back"),   kind: "back" }
    ]
    function barAction(kind) {
        if (kind === "select") { if (pictureRow.activeFocus) pictureRow.accept() }
        else if (kind === "clear") dlg._clear()
        else if (kind === "back") dlg.close()
    }

    modal: true
    Overlay.modal: Item {}
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: dlg._px(24)
    width: dlg._px(420)

    /*
     * Centred in the part of the stage right of the card, horizontally and vertically.
     * ⚠️ It was docked to the window's right edge with its top level with the card's, which
     * left it hanging off the corner; before that, at a fixed 72 px, it covered the date.
     *
     * x and y are set, not bound: the enter and exit transitions animate x, and an animated
     * property loses its binding — the second opening would start off screen. So the place is
     * worked out again whenever the window or the popup changes size while it is open.
     */
    function _parentWidth() { return dlg.parent ? dlg.parent.width : 1280 }
    function _dockRect() {
        if (!dlg.dockItem || !dlg.parent)
            return Qt.rect(dlg._parentWidth() / 2, 0, dlg._parentWidth() / 2, dlg.parent ? dlg.parent.height : 720)
        return dlg.dockItem.mapToItem(dlg.parent, dlg.dockLeft, 0,
                                      dlg.dockItem.width - dlg.dockLeft, dlg.dockItem.height)
    }
    function _dockX() {
        var r = dlg._dockRect()
        return Math.max(0, Math.min(dlg._parentWidth() - width, r.x + (r.width - width) / 2))
    }
    function _dockY() {
        var r = dlg._dockRect()
        var ph = dlg.parent ? dlg.parent.height : 720
        return Math.max(0, Math.min(ph - height, r.y + (r.height - height) / 2))
    }
    function _place() { x = dlg._dockX(); y = dlg._dockY() }
    onAboutToShow: {
        dlg._place()
        if (appShell) appShell.takeOverBar(dlg)
    }
    onAboutToHide: if (appShell) appShell.releaseBar(dlg)
    onWidthChanged: if (dlg.opened) dlg._place()
    onHeightChanged: if (dlg.visible) y = dlg._dockY()
    Connections {
        target: dlg.parent
        function onWidthChanged() { if (dlg.opened) dlg._place() }
        function onHeightChanged() { if (dlg.opened) dlg._place() }
    }

    background: Rectangle {
        color: Theme.card
        border.color: Theme.lineHigh // 7.0.0: no scrim, so the edge has to separate it from the card below
        border.width: 1
        radius: dlg._px(14)
    }

    // Arrives from the right with a small spring, and leaves the same way.
    enter: Transition {
        enabled: !Theme.reduceAnimations
        ParallelAnimation {
            NumberAnimation { property: "x"; from: dlg._parentWidth(); to: dlg._dockX()
                              duration: 300; easing.type: Easing.OutBack; easing.overshoot: 0.9 }
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
        }
    }
    exit: Transition {
        enabled: !Theme.reduceAnimations
        ParallelAnimation {
            NumberAnimation { property: "x"; to: dlg._parentWidth(); duration: 180; easing.type: Easing.InCubic }
            NumberAnimation { property: "opacity"; to: 0; duration: 160 }
        }
    }

    function _preview(image) {
        dlg._selImage = image
        dlg.chosen(image)
    }

    function _cardOpacity() { return Math.round(opacityRow.slider.value) }

    function _clear() { dlg._preview("") }

    onOpened: {
        // The window may have changed size while the popup was sliding in.
        dlg._place()
        dlg._selImage = dlg.currentImage
        dlg._recent = computerModel ? computerModel.recentStagePictures() : []
        // Set, not bound: a binding would break on the first move.
        opacityRow.slider.value = dlg.currentOpacity
        var i = dlg._recent.indexOf(dlg.currentImage)
        pictureRow.cursor = i >= 0 ? i + 1 : 0
        pictureRow.forceActiveFocus()
    }

    // Every way out keeps what is on the card: the last slider value is sent rather than lost
    // in the debounce, and the picture joins the recent ones.
    onClosed: {
        commitTimer.stop()
        dlg.opacityChosen(dlg._cardOpacity())
        if (computerModel && dlg._selImage !== "") computerModel.rememberStagePicture(dlg._selImage)
    }

    // The slider applies as it moves, a beat behind, so a held D-pad does not write the
    // registry on every step.
    Timer {
        id: commitTimer
        interval: 90
        onTriggered: dlg.opacityChosen(dlg._cardOpacity())
    }

    // ── Pieces ───────────────────────────────────────────────────────────────
    component SectionTitle: Label {
        font.family: Theme.family
        font.pixelSize: dlg._px(Theme.fontCaption)
        font.bold: true
        font.letterSpacing: dlg._u * 1.4
        color: Theme.text3
    }

    // One choosable picture. The ring is the text colour, not the accent: the same ring the
    // rest of the popup's cursor uses, readable on any picture.
    component Choice: Item {
        id: choice
        property bool cursor: false
        property bool current: false
        property alias image: pic.source
        signal picked()
        signal hovered()

        implicitHeight: dlg._px(45)
        implicitWidth: Math.round(implicitHeight * 16 / 9)

        Rectangle {
            anchors.fill: parent
            anchors.margins: -dlg._px(5)
            radius: dlg._px(10)
            color: "transparent"
            border.color: Theme.text
            border.width: dlg._px(3)
            visible: choice.cursor
        }
        Rectangle {
            anchors.fill: parent
            radius: dlg._px(7)
            color: Theme.cardHigh
            border.color: Theme.line
            border.width: 1
            clip: true
            scale: choice.cursor && !Theme.reduceAnimations ? 1.06 : 1.0
            Behavior on scale {
                enabled: !Theme.reduceAnimations
                NumberAnimation { duration: 110; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
            }
            Image {
                id: pic
                anchors.fill: parent
                anchors.margins: 1
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 160
            }
            // The tick on the picture in use: white with a dark outline, readable on any picture.
            Label {
                anchors.centerIn: parent
                visible: choice.current
                text: "✓"
                color: "#ffffff"
                style: Text.Outline
                styleColor: Qt.rgba(0, 0, 0, 0.5)
                font.pixelSize: dlg._px(Theme.fontTitle)
                font.bold: true
            }
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: if (SdlGamepadKeyNavigation.inputMode !== "key") choice.hovered()
            onClicked: choice.picked()
        }
    }

    // A labelled percentage slider.
    component PercentRow: RowLayout {
        id: row
        property alias slider: s
        property string label: ""
        signal stepUp()
        signal stepDown()
        spacing: dlg._px(14)
        Layout.fillWidth: true

        ColumnLayout {
            spacing: 0
            Layout.preferredWidth: dlg._px(112)
            Label {
                text: row.label
                font.family: Theme.family
                font.pixelSize: dlg._px(Theme.fontBody)
                font.bold: true
                color: s.enabled ? Theme.text : Theme.text3
            }
        }
        Item {
            Layout.fillWidth: true
            implicitHeight: dlg._px(36)
            Rectangle {
                anchors.fill: parent; anchors.margins: -2
                radius: dlg._px(6); color: "transparent"
                border.color: Theme.ink; border.width: 2
                visible: s.activeFocus
            }
            Slider {
                id: s
                anchors.fill: parent
                stepSize: 1
                snapMode: Slider.SnapAlways
                onMoved: commitTimer.restart()
                Keys.onUpPressed: function(e) { row.stepUp(); e.accepted = true }
                Keys.onDownPressed: function(e) { row.stepDown(); e.accepted = true }
                background: Rectangle {
                    x: s.leftPadding
                    y: s.topPadding + s.availableHeight / 2 - height / 2
                    width: s.availableWidth
                    height: dlg._px(3); radius: dlg._px(2)
                    color: Theme.lineHigh
                    Rectangle {
                        width: s.visualPosition * parent.width
                        height: parent.height; radius: parent.radius
                        color: s.enabled ? Theme.ink : Theme.offline
                    }
                }
                handle: Rectangle {
                    x: s.leftPadding + s.visualPosition * (s.availableWidth - width)
                    y: s.topPadding + s.availableHeight / 2 - height / 2
                    implicitWidth: dlg._px(14); implicitHeight: dlg._px(14); radius: dlg._px(7)
                    color: s.enabled ? Theme.ink : Theme.offline
                }
            }
        }
        Label {
            Layout.preferredWidth: dlg._px(44)
            horizontalAlignment: Text.AlignRight
            text: s.enabled ? Math.round(s.value) + "%" : "—"
            font.family: Theme.family
            font.pixelSize: dlg._px(Theme.fontSmall)
            font.bold: true
            color: s.enabled ? Theme.text : Theme.text3
        }
    }

    contentItem: ColumnLayout {
        spacing: dlg._px(18)

        // X clears the picture, from anywhere in the popup: the rows leave every other key to
        // their parent. Here and not on the Popup, which is not an Item and takes no Keys.
        // B is Esc, handled by closePolicy.
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Menu || event.key === Qt.Key_X || event.key === Qt.Key_Delete) {
                dlg._clear(); event.accepted = true
            }
        }

        Label {
            text: qsTr("BACKGROUND") + (dlg.hostName.length ? "  ·  " + dlg.hostName : "")
            font.family: Theme.family
            font.pixelSize: dlg._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: dlg._u * 1.6
            color: Theme.text3
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        // ── PICTURE ──────────────────────────────────────────────────────────
        ColumnLayout {
            spacing: dlg._px(10)
            SectionTitle { text: qsTr("PICTURE") }
            FocusScope {
                id: pictureRow
                activeFocusOnTab: true
                implicitWidth: picRowItems.implicitWidth
                implicitHeight: picRowItems.implicitHeight
                // 0 is Choose…, then the recent pictures. The cursor only moves the ring.
                property int cursor: 0
                readonly property int count: 1 + dlg._recent.length

                // A: Choose… opens the file dialog; a picture is picked (and stays: see the
                // note at the top).
                function accept() {
                    if (cursor === 0) { fileDialog.open(); return }
                    dlg._preview(dlg._recent[cursor - 1])
                }

                Row {
                    id: picRowItems
                    spacing: dlg._px(10)
                    // Choose… opens the file dialog: the one way in a pad cannot finish.
                    Rectangle {
                        width: dlg._px(80); height: dlg._px(45)
                        radius: dlg._px(7)
                        color: Theme.cardHigh
                        border.color: pictureRow.activeFocus && pictureRow.cursor === 0 ? Theme.ink : Theme.lineHigh
                        border.width: pictureRow.activeFocus && pictureRow.cursor === 0 ? 3 : 1
                        Column {
                            anchors.centerIn: parent
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "+"; color: Theme.text; font.pixelSize: dlg._px(Theme.fontTitle); font.bold: true }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Choose…"); color: Theme.text2; font.family: Theme.family; font.pixelSize: dlg._px(Theme.fontCaption); font.bold: true }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { pictureRow.forceActiveFocus(); pictureRow.cursor = 0; fileDialog.open() }
                        }
                    }
                    Repeater {
                        model: dlg._recent
                        delegate: Choice {
                            image: "file:///" + modelData
                            cursor: pictureRow.activeFocus && pictureRow.cursor === index + 1
                            current: dlg._selImage === modelData
                            onHovered: { pictureRow.forceActiveFocus(); pictureRow.cursor = index + 1 }
                            onPicked:  { pictureRow.forceActiveFocus(); pictureRow.cursor = index + 1; dlg._preview(modelData) }
                        }
                    }
                }
                Keys.onLeftPressed:  function(e) { if (cursor > 0) cursor--; e.accepted = true }
                Keys.onRightPressed: function(e) { if (cursor < count - 1) cursor++; e.accepted = true }
                Keys.onUpPressed:    function(e) { e.accepted = true }
                Keys.onDownPressed:  function(e) { opacityRow.slider.forceActiveFocus(); e.accepted = true }
                Keys.onReturnPressed: accept()
                Keys.onEnterPressed:  accept()
            }
        }

        // ── How much of the waves shows through ──────────────────────────────
        PercentRow {
            id: opacityRow
            Layout.topMargin: dlg._px(6)
            label: qsTr("Card opacity")
            onStepUp: pictureRow.forceActiveFocus()
            onStepDown: {}
        }
    }

    Binding { target: opacityRow.slider; property: "from"; value: dlg.opacityMin }
    Binding { target: opacityRow.slider; property: "to"; value: 100 }

    FileDialog {
        id: fileDialog
        title: qsTr("Pick a background for %1").arg(dlg.hostName)
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.webp)")]
        onAccepted: {
            // The dialog hands back a URL; the stage's Image wants a plain path.
            var p = selectedFile.toString().replace(/^file:\/{3}/, "")
            // Shown at once among the recent ones, so the pad can move off it and back.
            var r = [p]
            for (var i = 0; i < dlg._recent.length && r.length < 3; i++)
                if (dlg._recent[i] !== p) r.push(dlg._recent[i])
            dlg._recent = r
            pictureRow.forceActiveFocus()
            pictureRow.cursor = 1
            // Picked in the file dialog is picked: it goes on the card at once.
            dlg._preview(p)
        }
    }
}
