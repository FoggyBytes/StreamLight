import QtQuick 2.0
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

NavigableDialog {
    id: dialog

    // ── Public API (preserved from legacy version) ────────────────────────────
    property string text: ""
    property alias  showSpinner: spinner.visible
    property string imageSrc: ""              // kept for backwards-compat — no longer rendered
    property string helpText
    property string helpTextSeparator: " "

    // Default Troubleshooting URL; ErrorMessageDialog override per-case via the
    // helpUrl property inherited from NavigableDialog.
    helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Troubleshooting"

    // ── New API ───────────────────────────────────────────────────────────────
    // Eyebrow uppercase label above the body. Named `headerText` (not `header`)
    // to avoid colliding with Dialog's built-in `header: Item` slot.
    property string headerText: ""

    onOpened: {
        // Move keyboard focus onto the last button so keyboard / gamepad
        // navigation works as it used to with the legacy implementation.
        if (dialog.footer && dialog.footer.count > 0) {
            dialog.footer.itemAt(dialog.footer.count - 1).forceActiveFocus(Qt.TabFocus)
        }
    }

    contentItem: ColumnLayout {
        spacing: 22

        Label {
            visible: dialog.headerText.length > 0
            text: dialog.headerText
            font.family: "DM Sans"
            font.pixelSize: 13
            font.bold: true
            font.letterSpacing: 1.6
            color: "#707070"
            Layout.alignment: Qt.AlignHCenter
        }

        BusyIndicator {
            id: spinner
            visible: false
            running: visible
            implicitWidth: 56
            implicitHeight: 56
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            id: bodyLabel
            text: dialog.text + ((dialog.helpText && (dialog.standardButtons & Dialog.Help))
                                  ? (dialog.helpTextSeparator + dialog.helpText)
                                  : "")
            font.family: "DM Sans"
            font.pixelSize: 18
            color: "#f0f0f0"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 520
        }
    }
}
