import Theme 1.0
import QtQuick 2.0
import QtQuick.Controls 2.5

Dialog {
    id: navDialog

    modal: true
    anchors.centerIn: Overlay.overlay

    // Theme.scrim over the page, like every dialog opened on one (6.3.1, §80.9). This used to
    // be `Item {}` — no dim at all — so a confirmation or an error sat on a fully lit page while
    // Power, Update and Options darkened theirs. The rule now (option A, chosen by Marcello on
    // 24/09/2026): a scrim when a dialog opens on a page; none on the small editors that open
    // over another dialog or over Settings — the profile name, the custom resolution and frame
    // rate, the rebinding dialogs — so nothing is ever darkened twice. Every message and
    // confirmation built on this opens on a page.
    Overlay.modal: Rectangle { color: Theme.scrim }
    Overlay.modeless: Item {}

    // When true the affirmative button (Ok / Yes) is rendered in danger red
    // instead of accent green. Inherited by NavigableMessageDialog.
    property bool affirmativeIsDanger: false

    // Help URL opened when DialogButtonBox emits helpRequested (Dialog.Help in
    // standardButtons). Empty by default; subclasses or call sites set it.
    property string helpUrl: ""

    // The window scale every other dialog is drawn at (6.3.1). This one — and with it every
    // error and confirmation in the app, which are all built on it — was still in fixed
    // pixels, so on a large screen it opened visibly smaller than the page behind it.
    // Hairlines stay 1 px, as everywhere else.
    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    padding: _px(32)

    background: Rectangle {
        color: Theme.card
        border.color: Theme.line
        border.width: 1
        radius: navDialog._px(12)
    }

    footer: DialogButtonBox {
        id: dialogButtonBox
        standardButtons: navDialog.standardButtons
        // Affirmative (Yes / OK) on the LEFT, dismissive (No / Cancel) on the RIGHT,
        // to match StreamTweak's dialogs. The Qt default here is the opposite order.
        buttonLayout: DialogButtonBox.WinLayout
        alignment: Qt.AlignHCenter
        spacing: navDialog._px(14)
        topPadding: navDialog._px(8)
        background: Rectangle { color: "transparent" }

        // Not DialogButton, although it draws the same thing: DialogButtonBox answers the
        // buttons' clicked(), and DialogButton turns Return into activated() instead.
        delegate: Button {
            id: btn
            readonly property int role: DialogButtonBox.buttonRole
            readonly property bool isAffirmative: role === DialogButtonBox.AcceptRole
                                                  || role === DialogButtonBox.YesRole
            readonly property bool isDanger: isAffirmative && navDialog.affirmativeIsDanger
            readonly property color accentBase:  isDanger ? Theme.danger : Theme.accent
            // The tint derived from the same colour as the border and the label. The danger
            // one used to be a literal — #EF4444 at 20% — under a Theme.danger (#F87171)
            // border: two reds on one button.
            readonly property color accentHover: Qt.rgba(accentBase.r, accentBase.g, accentBase.b, 0.20)

            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed:  clicked()
            Keys.onRightPressed:  nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocus)
            Keys.onLeftPressed:   nextItemInFocusChain(false).forceActiveFocus(Qt.TabFocus)

            background: Rectangle {
                implicitWidth: navDialog._px(140)
                implicitHeight: navDialog._px(42)
                radius: navDialog._px(8)
                // Option A: the accent (green, or red for a danger action) is reserved
                // for FOCUS only — matching the app-wide "green border = focused" rule.
                // Unfocused buttons are flat with a grey border; the affirmative role is
                // shown via text colour, so it no longer looks focused at rest.
                color: btn.activeFocus ? btn.accentHover
                     : btn.hovered     ? Qt.rgba(1, 1, 1, 0.05)
                     :                   Theme.card
                border.color: btn.activeFocus ? btn.accentBase
                            : btn.hovered     ? Theme.lineHigh
                            :                   Theme.line
                border.width: btn.activeFocus ? 2 : 1
            }
            contentItem: Label {
                text: btn.text
                color: btn.isAffirmative ? btn.accentBase : Theme.text
                font.family: Theme.family
                font.pixelSize: navDialog._px(Theme.fontBody)
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
