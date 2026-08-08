import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Dialogs

import Theme 1.0

/*
 * What sits behind a host on the Home screen.
 *
 * Three ways in, in the order most people will want them: a colour, a picture, or nothing.
 * All three end in the same place — CoverPalette turns whichever one you chose into the same
 * kind of two-colour pair — which is why a picked colour and a picked picture look like the
 * same feature rather than two different ones.
 *
 * It is stored on the host, not on a profile: "docked" and "handheld" are two ways of using
 * the same machine and must look the same.
 *
 * Emits chosen(imagePath, seedColor); both empty means "clear it".
 */
Popup {
    id: dlg

    property string hostName: ""
    // What the host is using now, so the current choice reads as selected.
    property string currentImage: ""
    property string currentSeed: ""

    signal chosen(string imagePath, string seedColor)

    // Six hues far enough apart to tell a strip of hosts apart at a glance. They are seeds,
    // not final colours — CoverPalette normalises each one, so a badly chosen value cannot
    // produce an unreadable stage.
    readonly property var _seeds: [
        "#3a7bd5", "#8e44ad", "#c0392b", "#27ae60", "#d68910", "#16a085"
    ]

    modal: true
    dim: true
    focus: true
    Overlay.modal: Rectangle { color: "#cc000000" }
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 28

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: 12
    }

    // Focus lands on the swatch row: picking a colour is the common case, and the file
    // dialog is a detour most users will never take.
    onOpened: swatchRow.forceActiveFocus()

    contentItem: Column {
        spacing: 20

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("BACKGROUND") + (dlg.hostName.length ? "  ·  " + dlg.hostName : "")
            font.family: Theme.family
            font.pixelSize: 13
            font.bold: true
            font.letterSpacing: 1.6
            color: Theme.text3
        }

        // ── Colours ──────────────────────────────────────────────────────────
        FocusScope {
            id: swatchRow
            width: swatches.implicitWidth
            height: 64
            anchors.horizontalCenter: parent.horizontalCenter
            activeFocusOnTab: true

            property int index: 0

            // Qualified deliberately: `index` alone would resolve here, but the same name is
            // the Repeater's delegate index a few lines below, and the two meaning different
            // things in one file is exactly how a working handler starts reading the wrong one.
            Keys.onLeftPressed:  function(event) { if (swatchRow.index > 0) swatchRow.index--; event.accepted = true }
            Keys.onRightPressed: function(event) { if (swatchRow.index < dlg._seeds.length - 1) swatchRow.index++; event.accepted = true }
            Keys.onDownPressed:  function(event) { pickBtn.forceActiveFocus(); event.accepted = true }
            Keys.onReturnPressed: function(event) { dlg.chosen("", dlg._seeds[swatchRow.index]); event.accepted = true }
            Keys.onEnterPressed:  function(event) { dlg.chosen("", dlg._seeds[swatchRow.index]); event.accepted = true }
            Keys.onSpacePressed:  function(event) { dlg.chosen("", dlg._seeds[swatchRow.index]); event.accepted = true }

            Row {
                id: swatches
                anchors.centerIn: parent
                spacing: 12

                Repeater {
                    model: dlg._seeds

                    delegate: Rectangle {
                        readonly property bool _focused: swatchRow.activeFocus && swatchRow.index === index
                        readonly property bool _current: dlg.currentSeed === modelData && dlg.currentImage === ""

                        width: 52; height: 52
                        radius: 10
                        color: modelData
                        border.width: _focused ? 3 : (_current ? 2 : 0)
                        border.color: _focused ? Theme.accent : Theme.text

                        Behavior on scale {
                            enabled: !Theme.reduceAnimations
                            NumberAnimation { duration: 130; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
                        }
                        scale: _focused && !Theme.reduceAnimations ? 1.08 : 1.0

                        // A tick on the one in use, because six coloured squares with a thin
                        // outline on one of them is not a readable "current" state.
                        Label {
                            anchors.centerIn: parent
                            visible: parent._current
                            text: "✓"
                            color: "#ffffff"
                            font.pixelSize: 22
                            font.bold: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                swatchRow.index = index
                                swatchRow.forceActiveFocus()
                                dlg.chosen("", modelData)
                            }
                        }
                    }
                }
            }
        }

        // ── Picture / clear ──────────────────────────────────────────────────
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12

            DialogButton {
                id: pickBtn
                text: qsTr("Choose a picture…")
                implicitWidth: 190
                KeyNavigation.right: clearBtn
                KeyNavigation.up: swatchRow
                onActivated: fileDialog.open()
            }

            DialogButton {
                id: clearBtn
                text: qsTr("Clear")
                implicitWidth: 130
                KeyNavigation.left: pickBtn
                KeyNavigation.right: closeBtn
                KeyNavigation.up: swatchRow
                onActivated: dlg.chosen("", "")
            }

            DialogButton {
                id: closeBtn
                text: qsTr("Done")
                affirmative: true
                implicitWidth: 130
                KeyNavigation.left: clearBtn
                KeyNavigation.up: swatchRow
                onActivated: dlg.close()
            }
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 460
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("The picture is darkened behind the host's name and details, and left alone everywhere else. Its colours also become the gradient, so the two never clash.")
            color: Theme.text3
            font.family: Theme.family
            font.pixelSize: 12
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Pick a background for %1").arg(dlg.hostName)
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.webp)")]
        onAccepted: {
            // The dialog hands back a URL; everything downstream — QImage in CoverPalette,
            // and the Image element on the stage — wants a plain local path.
            dlg.chosen(selectedFile.toString().replace(/^file:\/{3}/, ""), "")
        }
    }
}
