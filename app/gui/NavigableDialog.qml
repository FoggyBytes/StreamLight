import Theme 1.0
import QtQuick 2.0
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

Dialog {
    id: navDialog

    modal: true
    anchors.centerIn: Overlay.overlay

    // Disable the default semi-transparent dim layer that Qt Quick Controls
    // paints behind a modal dialog. Matches AddHostDialog / PairDialog.
    Overlay.modal: Item {}
    Overlay.modeless: Item {}

    // When true the affirmative button (Ok / Yes) is rendered in danger red
    // instead of accent green. Inherited by NavigableMessageDialog.
    property bool affirmativeIsDanger: false

    // Help URL opened when DialogButtonBox emits helpRequested (Dialog.Help in
    // standardButtons). Empty by default; subclasses or call sites set it.
    property string helpUrl: ""

    padding: 32

    background: Rectangle {
        color: "#1a1a1a"
        border.color: "#2a2a2a"
        border.width: 1
        radius: 12
    }

    footer: DialogButtonBox {
        id: dialogButtonBox
        standardButtons: navDialog.standardButtons
        // Affirmative (Yes / OK) on the LEFT, dismissive (No / Cancel) on the RIGHT,
        // to match StreamTweak's dialogs. The Qt default here is the opposite order.
        buttonLayout: DialogButtonBox.WinLayout
        alignment: Qt.AlignHCenter
        spacing: 14
        topPadding: 8
        background: Rectangle { color: "transparent" }

        delegate: Button {
            id: btn
            readonly property int role: DialogButtonBox.buttonRole
            readonly property bool isAffirmative: role === DialogButtonBox.AcceptRole
                                                  || role === DialogButtonBox.YesRole
            readonly property bool isDanger: isAffirmative && navDialog.affirmativeIsDanger
            readonly property color accentBase:  isDanger ? "#ef4444"                            : Theme.accent
            readonly property color accentHover: isDanger ? Qt.rgba(0.937, 0.267, 0.267, 0.20)   : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
            readonly property color accentIdle:  isDanger ? Qt.rgba(0.937, 0.267, 0.267, 0.12)   : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)

            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed:  clicked()
            Keys.onRightPressed:  nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocus)
            Keys.onLeftPressed:   nextItemInFocusChain(false).forceActiveFocus(Qt.TabFocus)

            background: Rectangle {
                implicitWidth: 140
                implicitHeight: 42
                radius: 8
                // Option A: the accent (green, or red for a danger action) is reserved
                // for FOCUS only — matching the app-wide "green border = focused" rule.
                // Unfocused buttons are flat with a grey border; the affirmative role is
                // shown via text colour, so it no longer looks focused at rest.
                color: btn.activeFocus ? btn.accentHover
                     : btn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                     :                   "#1f1f1f"
                border.color: btn.activeFocus ? btn.accentBase
                            : btn.hovered     ? "#3a3a3a"
                            :                   "#2a2a2a"
                border.width: btn.activeFocus ? 2 : 1
            }
            contentItem: Label {
                text: btn.text
                color: btn.isAffirmative ? btn.accentBase : "#f0f0f0"
                font.family: "DM Sans"
                font.pixelSize: 15
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        onHelpRequested: {
            if (navDialog.helpUrl) {
                Qt.openUrlExternally(navDialog.helpUrl)
            }
            close()
        }
    }

    onClosed: {
        // Restore focus to the stack so gamepad / keyboard navigation keeps
        // working after the dialog disappears.
        if (typeof stackView !== "undefined" && stackView) {
            stackView.forceActiveFocus()
        }
    }
}
