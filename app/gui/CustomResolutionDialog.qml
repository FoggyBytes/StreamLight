import Theme 1.0
import SystemProperties 1.0
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Enter-a-custom-resolution popup. Standalone file: Overlay.modal cannot resolve
// inline. Mirrors AddHostDialog styling/navigation.
// D-pad: Width → Right/Tab → Height → Down → Apply; Apply ⇄ Cancel via Left/Right.
Popup {
    id: pop

    signal accepted(int outWidth, int outHeight)

    property int initWidth:  1920
    property int initHeight: 1080

    readonly property int _minDim: 256
    readonly property int _maxDim: 7680

    /*
     * The shared dialog scale — see SettingsResetDialog for why a dialog cannot take this
     * from the page behind it.
     *
     * ⚠️ This file was missed by 5.3.0's conversion to a single scale and stayed in fixed
     * pixels: on a 1920 handheld (uiScale 1.44) it was drawn at 69% of everything around it.
     * Converted in 5.5.0 alongside its new twin, CustomFrameRateDialog — writing the twin to
     * match a broken original would have doubled the defect instead of ending it.
     */
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    // What this machine's displays report, for the line under the fields. Read when the
    // dialog opens rather than bound: SystemProperties settles at startup and cannot change
    // while a modal is up.
    property string _nativeHint: ""

    function _parse(field) { var n = parseInt(field.text, 10); return isNaN(n) ? 0 : n }
    readonly property int  _w: _parse(wField)
    readonly property int  _h: _parse(hField)
    readonly property bool _valid: _w >= _minDim && _w <= _maxDim
                                && _h >= _minDim && _h <= _maxDim

    modal: true
    Overlay.modal: Item {}
    focus: true
    // Centred but offset higher so a virtual keyboard (handheld) does not cover it.
    x: (Overlay.overlay ? (Overlay.overlay.width  - width)  / 2 : 0)
    y: (Overlay.overlay ? Math.max(pop._px(40), Overlay.overlay.height * 0.12) : pop._px(40))
    closePolicy: Popup.CloseOnEscape
    padding: pop._px(32)

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: pop._px(12)
    }

    // ⚠️ Reads the scale from Theme rather than from pop._px: an inline component is its
    // own scope and cannot see the enclosing component's ids.
    component DimField: TextField {
        readonly property real _fu: Theme.uiScale
        function _fpx(n) { return Math.round(n * _fu) }

        Layout.preferredWidth: _fpx(130)
        implicitHeight: _fpx(48)
        color: Theme.text
        selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
        selectedTextColor: Theme.onAccent
        font.family: Theme.family
        font.pixelSize: _fpx(Theme.fontTitle)
        font.bold: true
        horizontalAlignment: TextInput.AlignHCenter
        inputMethodHints: Qt.ImhDigitsOnly
        validator: IntValidator { bottom: 0; top: 99999 }
        background: Rectangle {
            color: Theme.ground
            // Not parent._fpx(): a Control's background sees its parent as a bare
            // QQuickItem, so the call resolves at runtime and nowhere earlier.
            radius: Math.round(Theme.uiScale * 8)
            border.color: parent.activeFocus ? Theme.accent : Theme.line
            border.width: parent.activeFocus ? 2 : 1
        }
    }

    contentItem: ColumnLayout {
        spacing: pop._px(20)

        Label {
            text: qsTr("CUSTOM RESOLUTION")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
            font.bold: true
            font.letterSpacing: pop._u * 1.6
            color: Theme.text3
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Enter a custom resolution in pixels")
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontTitle)
            color: Theme.text
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: pop._px(520)
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: pop._px(12)

            DimField {
                id: wField
                Keys.onReturnPressed: pop._commit()
                Keys.onEnterPressed:  pop._commit()
                Keys.onRightPressed:  hField.forceActiveFocus()
                Keys.onTabPressed:    hField.forceActiveFocus()
                Keys.onDownPressed:   applyBtn.forceActiveFocus()
            }
            Label {
                text: "×"
                color: Theme.text3
                font.family: Theme.family
                font.pixelSize: pop._px(Theme.fontH2)
                Layout.alignment: Qt.AlignVCenter
            }
            DimField {
                id: hField
                Keys.onReturnPressed: pop._commit()
                Keys.onEnterPressed:  pop._commit()
                Keys.onLeftPressed:   wField.forceActiveFocus()
                Keys.onTabPressed:    applyBtn.forceActiveFocus()
                Keys.onDownPressed:   applyBtn.forceActiveFocus()
            }
        }

        // One line, two jobs: what the display can do while the numbers are sane, what is
        // wrong with them when they are not. They never both apply, so they never both show.
        // Same line, same place, same rules as CustomFrameRateDialog — the two are twins and
        // a dialog that told you about your display while its twin stayed silent would just
        // look like one of them was broken.
        Label {
            text: pop._valid
                  ? pop._nativeHint
                  : qsTr("Width and height must be between %1 and %2 px.")
                    .arg(pop._minDim).arg(pop._maxDim)
            visible: text.length > 0
            color: pop._valid ? Theme.text2 : Theme.danger
            font.family: Theme.family
            font.pixelSize: pop._px(Theme.fontSmall)
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
                Keys.onUpPressed:     wField.forceActiveFocus()

                background: Rectangle {
                    implicitWidth: pop._px(140)
                    implicitHeight: pop._px(42)
                    radius: pop._px(8)
                    color: applyBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : applyBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         Theme.card
                    border.color: applyBtn.activeFocus ? Theme.accent
                                : applyBtn.hovered     ? Theme.lineHigh
                                :                         Theme.line
                    border.width: applyBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: applyBtn.text
                    color: Theme.accent
                    font.family: Theme.family
                    font.pixelSize: pop._px(Theme.fontBody)
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
                Keys.onUpPressed:     hField.forceActiveFocus()

                background: Rectangle {
                    implicitWidth: pop._px(140)
                    implicitHeight: pop._px(42)
                    radius: pop._px(8)
                    color: cancelBtn.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                         : cancelBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         Theme.card
                    border.color: cancelBtn.activeFocus ? Theme.accent
                                : cancelBtn.hovered     ? Theme.lineHigh
                                :                         Theme.line
                    border.width: cancelBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: cancelBtn.text
                    color: Theme.text
                    font.family: Theme.family
                    font.pixelSize: pop._px(Theme.fontBody)
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    function _commit() {
        if (!_valid) return
        // Encoders require even dimensions — round down to the nearest even value.
        var w = _w - (_w % 2)
        var h = _h - (_h % 2)
        pop.accepted(w, h)
        pop.close()
    }

    onOpened: {
        var v = SystemProperties.videoOptions()
        var hint = v.resHint ? v.resHint : ""
        pop._nativeHint = hint.length === 0 ? ""
            : (v.displays > 1 ? qsTr("Your displays are %1.").arg(hint)
                              : qsTr("This display is %1.").arg(hint))

        wField.text = String(initWidth)
        hField.text = String(initHeight)
        wField.forceActiveFocus()
        wField.selectAll()
    }
}
