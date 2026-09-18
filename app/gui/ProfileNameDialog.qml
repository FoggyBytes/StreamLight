import Theme 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

/*
 * Naming a host profile (6.0.0).
 *
 * It exists because of one thing the app cannot do: type. StreamLight is driven from a
 * handheld with a controller in hand, and Windows only offers its on-screen keyboard outside
 * our window. Until now the name lived in a TextField inside the profile dialog, which meant
 * the one row a pad could reach but not use was sitting in the middle of the list, above the
 * settings it was renaming.
 *
 * So the field is still here — with a keyboard it is the fastest way — but it is the LAST
 * thing in the dialog, and above it are eight names chosen with the D-pad. A pad can now name
 * a profile; a keyboard can still call it whatever it likes.
 *
 * ⚠️ The presets are situations, not picture qualities: "Docked", "TV", "Handheld". A profile
 * is chosen by where you are, and that is what the name has to say from the host card, where
 * it is the only word on screen. Keep them inside HostProfilesDialog._maxNameLen (14).
 *
 * D-pad: the grid is ONE focusable element with a cursor of its own, like the profile chips —
 * eight separate focus stops would take eight D-pad presses to cross and would need their
 * KeyNavigation wired in a Repeater, which cannot see its own siblings.
 */
Popup {
    id: pop

    signal accepted(string outName)

    property string initName: ""
    property int    maxLength: 14

    readonly property var _presets: ["Docked", "Portable", "Handheld", "TV",
                                     "Living room", "Desk", "4K", "1080p"]
    readonly property int _cols: 4

    // The shared dialog scale — see SettingsResetDialog for why a dialog cannot take this
    // from the page behind it.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    readonly property string _typed: nameField.text.trim()
    readonly property bool   _valid: _typed.length > 0

    modal: true
    Overlay.modal: Item {}
    focus: true
    // Centred but offset higher, like the other entry popups: on a handheld a virtual
    // keyboard takes the bottom of the screen.
    x: (Overlay.overlay ? (Overlay.overlay.width - width) / 2 : 0)
    y: (Overlay.overlay ? Math.max(pop._px(40), Overlay.overlay.height * 0.12) : pop._px(40))
    closePolicy: Popup.CloseOnEscape
    padding: pop._px(32)

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: pop._px(12)
    }

    function _commit(name) {
        var n = name.trim().substring(0, pop.maxLength)
        if (n.length === 0) return
        pop.accepted(n)
        pop.close()
    }

    contentItem: ColumnLayout {
        spacing: pop._px(18)

        Label {
            text: qsTr("PROFILE NAME")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: pop._u * 1.6
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Name it after where you play")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontTitle)
            color: Theme.text
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }

        // ── The eight names, one focusable element with its own cursor ───────────
        FocusScope {
            id: nameGrid
            focus: true
            activeFocusOnTab: true
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: grid.implicitWidth
            implicitHeight: grid.implicitHeight

            property int cursor: 0

            Grid {
                id: grid
                columns: pop._cols
                spacing: pop._px(8)

                Repeater {
                    model: pop._presets
                    delegate: Rectangle {
                        id: chip
                        width: chipLabel.implicitWidth + pop._px(34)
                        height: pop._px(42)
                        radius: pop._px(8)
                        readonly property bool _cursor: nameGrid.activeFocus
                                                        && nameGrid.cursor === index
                        readonly property bool _isCurrent: modelData === pop.initName
                        color: Qt.tint(Theme.card, Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                           Theme.accent.b, 0.07))
                        border.color: chip._cursor ? Theme.accent : Theme.line
                        border.width: chip._cursor ? 3 : 1

                        Rectangle {
                            anchors.fill: parent; anchors.margins: pop._px(3)
                            radius: pop._px(5)
                            color: chip._isCurrent ? Theme.cardHigh : "transparent"
                            border.color: Theme.lineHigh
                            border.width: chip._isCurrent ? 1 : 0
                        }
                        Label {
                            id: chipLabel
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.text
                            font.family: Theme.family
                            font.pixelSize: pop._px(Theme.fontSmall)
                            font.bold: chip._isCurrent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                nameGrid.forceActiveFocus()
                                nameGrid.cursor = index
                                pop._commit(modelData)
                            }
                        }
                    }
                }
            }

            function _apply() { pop._commit(pop._presets[nameGrid.cursor]) }

            Keys.onLeftPressed: function(event) {
                if (nameGrid.cursor % pop._cols > 0) { nameGrid.cursor--; event.accepted = true }
                else event.accepted = false
            }
            Keys.onRightPressed: function(event) {
                if (nameGrid.cursor % pop._cols < pop._cols - 1
                        && nameGrid.cursor + 1 < pop._presets.length) {
                    nameGrid.cursor++
                    event.accepted = true
                } else event.accepted = false
            }
            Keys.onUpPressed: function(event) {
                if (nameGrid.cursor >= pop._cols) { nameGrid.cursor -= pop._cols; event.accepted = true }
                else event.accepted = false
            }
            Keys.onDownPressed: function(event) {
                // Down leaves the grid only from its last row — inside it, it is a move.
                if (nameGrid.cursor + pop._cols < pop._presets.length) {
                    nameGrid.cursor += pop._cols
                } else {
                    nameField.forceActiveFocus()
                    nameField.selectAll()
                }
                event.accepted = true
            }
            Keys.onReturnPressed: nameGrid._apply()
            Keys.onEnterPressed:  nameGrid._apply()
            Keys.onSpacePressed:  nameGrid._apply()
        }

        // ── Or type one. Last, because this is the half a pad cannot use ─────────
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: pop._px(2)
            spacing: pop._px(12)

            TextField {
                id: nameField
                Layout.preferredWidth: pop._px(300)
                implicitHeight: pop._px(46)
                maximumLength: pop.maxLength
                color: Theme.text
                selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
                selectedTextColor: Theme.onAccent
                font.family: Theme.family
                font.pixelSize: pop._px(Theme.fontBody)
                selectByMouse: true
                background: Rectangle {
                    color: Theme.ground
                    radius: pop._px(8)
                    border.color: nameField.activeFocus ? Theme.accent : Theme.line
                    border.width: nameField.activeFocus ? 2 : 1
                }
                Keys.onReturnPressed: pop._commit(nameField.text)
                Keys.onEnterPressed:  pop._commit(nameField.text)
                Keys.onUpPressed:     nameGrid.forceActiveFocus()
                Keys.onDownPressed:   applyBtn.forceActiveFocus()
                Keys.onTabPressed:    applyBtn.forceActiveFocus()
            }
            Label {
                text: qsTr("or type one")
                color: Theme.text3
                font.family: Theme.family
                font.pixelSize: pop._px(Theme.fontSmall)
                Layout.alignment: Qt.AlignVCenter
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: pop._px(14)

            DialogButton {
                id: applyBtn
                text: qsTr("Apply")
                affirmative: true
                enabled: pop._valid
                opacity: enabled ? 1.0 : 0.4
                onActivated: pop._commit(nameField.text)
                KeyNavigation.up: nameField
                KeyNavigation.right: cancelBtn
            }
            DialogButton {
                id: cancelBtn
                text: qsTr("Cancel")
                onActivated: pop.close()
                KeyNavigation.up: nameField
                KeyNavigation.left: applyBtn
            }
        }
    }

    onOpened: {
        nameField.text = pop.initName
        var i = pop._presets.indexOf(pop.initName)
        nameGrid.cursor = i >= 0 ? i : 0
        // The grid, not the field: a pad opens this dialog and a pad has to be able to use
        // the first thing it lands on.
        nameGrid.forceActiveFocus()
    }
}
