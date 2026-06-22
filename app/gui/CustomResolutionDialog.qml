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
    y: (Overlay.overlay ? Math.max(40, Overlay.overlay.height * 0.12) : 40)
    closePolicy: Popup.CloseOnEscape
    padding: 32

    background: Rectangle {
        color: "#1a1a1a"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 12
    }

    component DimField: TextField {
        Layout.preferredWidth: 130
        implicitHeight: 48
        color: "#f0f0f0"
        selectionColor: Qt.rgba(0, 0.9, 0.46, 0.30)
        selectedTextColor: "#0d1410"
        font.family: "JetBrains Mono"
        font.pixelSize: 20
        font.bold: true
        horizontalAlignment: TextInput.AlignHCenter
        inputMethodHints: Qt.ImhDigitsOnly
        validator: IntValidator { bottom: 0; top: 99999 }
        background: Rectangle {
            color: "#0f0f0f"
            radius: 8
            border.color: parent.activeFocus ? "#00E676" : "#2a2a2a"
            border.width: parent.activeFocus ? 2 : 1
        }
    }

    contentItem: ColumnLayout {
        spacing: 20

        Label {
            text: qsTr("CUSTOM RESOLUTION")
            font.family: "DM Sans"
            font.pixelSize: 13
            font.bold: true
            font.letterSpacing: 1.6
            color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Enter a custom resolution in pixels")
            font.family: "DM Sans"
            font.pixelSize: 18
            color: "#f0f0f0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12

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
                color: "#707070"
                font.family: "DM Sans"
                font.pixelSize: 22
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

        Label {
            text: qsTr("Width and height must be between %1 and %2 px.")
                  .arg(pop._minDim).arg(pop._maxDim)
            visible: !pop._valid
            color: "#ff6b6b"
            font.family: "DM Sans"
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 360
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            spacing: 14

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
                    implicitWidth: 140
                    implicitHeight: 42
                    radius: 8
                    color: applyBtn.activeFocus ? Qt.rgba(0, 0.9, 0.46, 0.20)
                         : applyBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         "#1f1f1f"
                    border.color: applyBtn.activeFocus ? "#00E676"
                                : applyBtn.hovered     ? "#3a3a3a"
                                :                         "#2a2a2a"
                    border.width: applyBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: applyBtn.text
                    color: "#00E676"
                    font.family: "DM Sans"
                    font.pixelSize: 15
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
                    implicitWidth: 140
                    implicitHeight: 42
                    radius: 8
                    color: cancelBtn.activeFocus ? Qt.rgba(0, 0.9, 0.46, 0.20)
                         : cancelBtn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                         :                         "#1f1f1f"
                    border.color: cancelBtn.activeFocus ? "#00E676"
                                : cancelBtn.hovered     ? "#3a3a3a"
                                :                         "#2a2a2a"
                    border.width: cancelBtn.activeFocus ? 2 : 1
                }
                contentItem: Label {
                    text: cancelBtn.text
                    color: "#f0f0f0"
                    font.family: "DM Sans"
                    font.pixelSize: 15
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
        wField.text = String(initWidth)
        hField.text = String(initHeight)
        wField.forceActiveFocus()
        wField.selectAll()
    }
}
