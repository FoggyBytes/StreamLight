import Theme 1.0
import SystemProperties 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Enter-a-custom-frame-rate popup (5.5.0). Twin of CustomResolutionDialog: same popup,
// same field, same navigation, one number instead of two.
//
// It exists because the frame-rate strip is a closed list, and until 5.4.0 nobody noticed:
// StreamLight read Moonlight's settings store, so a rate Moonlight offered — the client
// display's own — arrived already chosen and the strip merely displayed it. With our own
// store there is nothing to inherit, and a 165 Hz panel had no way to ask for 165 (issue #13).
//
// The display's own rates are in the strip next door, so this is for everything else: a
// deliberate cap, or a rate no display reports.
//
// D-pad: Value → Down → Apply; Apply ⇄ Cancel via Left/Right.
Popup {
    id: pop

    signal accepted(int outFps)

    property int initFps: 60

    /*
     * ⚠️ Our own bounds, not the protocol's: nothing in moonlight-common-c rejects a frame
     * rate. Below 24 the stream stops being watchable and above 480 there is no hardware,
     * so these are the edges of "the user meant this" rather than of what would work.
     */
    readonly property int _minFps: 24
    readonly property int _maxFps: 480

    // What this machine's displays report, for the line under the field. Read when the
    // dialog opens rather than bound: SystemProperties settles at startup and cannot
    // change while a modal is up.
    property string _nativeHint: ""

    readonly property int  _fps: { var n = parseInt(fpsField.text, 10); return isNaN(n) ? 0 : n }
    readonly property bool _valid: _fps >= _minFps && _fps <= _maxFps

    // The shared dialog scale — see SettingsResetDialog for why a dialog cannot take this
    // from the page behind it.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    modal: true
    Overlay.modal: Item {}
    focus: true
    // Centred but offset higher so a virtual keyboard (handheld) does not cover it.
    x: (Overlay.overlay ? (Overlay.overlay.width  - width)  / 2 : 0)
    y: (Overlay.overlay ? Math.max(pop._px(40), Overlay.overlay.height * 0.12) : pop._px(40))
    closePolicy: Popup.CloseOnEscape
    padding: pop._px(32)

    background: Rectangle {
        color: "#1a1a1a"
        border.color: "#2a2a2a"
        border.width: 1
        radius: pop._px(12)
    }

    contentItem: ColumnLayout {
        spacing: pop._px(20)

        Label {
            text: qsTr("CUSTOM FRAME RATE")
            font.family: "DM Sans"
            font.pixelSize: pop._px(13)
            font.bold: true
            font.letterSpacing: pop._u * 1.6
            color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Enter a frame rate in frames per second")
            font.family: "DM Sans"
            font.pixelSize: pop._px(18)
            color: "#f0f0f0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: pop._px(520)
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: pop._px(12)

            TextField {
                id: fpsField
                Layout.preferredWidth: pop._px(130)
                implicitHeight: pop._px(48)
                color: "#f0f0f0"
                selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
                selectedTextColor: Theme.onAccent
                font.family: "DM Sans"
                font.pixelSize: pop._px(20)
                font.bold: true
                horizontalAlignment: TextInput.AlignHCenter
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0; top: 9999 }
                background: Rectangle {
                    color: "#0f0f0f"
                    radius: pop._px(8)
                    border.color: parent.activeFocus ? Theme.accent : "#2a2a2a"
                    border.width: parent.activeFocus ? 2 : 1
                }
                Keys.onReturnPressed: pop._commit()
                Keys.onEnterPressed:  pop._commit()
                Keys.onTabPressed:    applyBtn.forceActiveFocus()
                Keys.onDownPressed:   applyBtn.forceActiveFocus()
            }
            Label {
                text: qsTr("fps")
                color: "#707070"
                font.family: "DM Sans"
                font.pixelSize: pop._px(16)
                Layout.alignment: Qt.AlignVCenter
            }
        }

        // One line, two jobs: what the display can do while the number is sane, what is
        // wrong with it when it is not. They never both apply, so they never both show.
        Label {
            text: pop._valid
                  ? pop._nativeHint
                  : qsTr("Enter a value between %1 and %2.").arg(pop._minFps).arg(pop._maxFps)
            visible: text.length > 0
            color: pop._valid ? "#a0a0a0" : "#ff6b6b"
            font.family: "DM Sans"
            font.pixelSize: pop._px(13)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: pop._px(360)
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: pop._px(4)
            spacing: pop._px(14)

            Button {
                id: applyBtn
                text: qsTr("Apply")
                enabled: pop._valid
                opacity: enabled ? 1.0 : 0.4
                activeFocusOnTab: true
                onClicked: pop._commit()
                Keys.onReturnPressed: pop._commit()
                Keys.onEnterPressed:  pop._commit()
                Keys.onSpacePressed:  pop._commit()
                Keys.onRightPressed:  cancelBtn.forceActiveFocus()
                Keys.onUpPressed:     fpsField.forceActiveFocus()

                background: Rectangle {
                    implicitWidth: pop._px(140)
                    implicitHeight: pop._px(42)
                    radius: pop._px(8)
                    color: applyBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : applyBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         "#1f1f1f"
                    border.color: applyBtn.activeFocus ? Theme.accent
                                : applyBtn.hovered     ? "#3a3a3a"
                                :                         "#2a2a2a"
                    border.width: applyBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: applyBtn.text
                    color: Theme.accent
                    font.family: "DM Sans"
                    font.pixelSize: pop._px(15)
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                id: cancelBtn
                text: qsTr("Cancel")
                activeFocusOnTab: true
                onClicked: pop.close()
                Keys.onReturnPressed: pop.close()
                Keys.onEnterPressed:  pop.close()
                Keys.onSpacePressed:  pop.close()
                Keys.onLeftPressed:   applyBtn.forceActiveFocus()
                Keys.onUpPressed:     fpsField.forceActiveFocus()

                background: Rectangle {
                    implicitWidth: pop._px(140)
                    implicitHeight: pop._px(42)
                    radius: pop._px(8)
                    color: cancelBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : cancelBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         "#1f1f1f"
                    border.color: cancelBtn.activeFocus ? Theme.accent
                                : cancelBtn.hovered     ? "#3a3a3a"
                                :                         "#2a2a2a"
                    border.width: cancelBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: cancelBtn.text
                    color: "#f0f0f0"
                    font.family: "DM Sans"
                    font.pixelSize: pop._px(15)
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    function _commit() {
        if (!_valid) return
        pop.accepted(_fps)
        pop.close()
    }

    onOpened: {
        var v = SystemProperties.videoOptions()
        var hint = v.fpsHint ? v.fpsHint : ""
        pop._nativeHint = hint.length === 0 ? ""
            : (v.displays > 1 ? qsTr("Your displays run at %1.").arg(hint)
                              : qsTr("This display runs at %1.").arg(hint))

        fpsField.text = String(initFps)
        fpsField.forceActiveFocus()
        fpsField.selectAll()
    }
}
