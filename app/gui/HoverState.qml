import QtQuick 2.15
import SdlGamepadKeyNavigation 1.0

/*
 * The mouse-hover state, in one place. Draws nothing.
 *
 * Declare it inside the control and render `active` on whatever channel that control already
 * uses to say something about itself:
 *
 *     Button {
 *         HoverState { id: hov }
 *         background: Rectangle {
 *             border.color: activeFocus ? Theme.accent
 *                         : hov.active  ? Theme.lineHigh
 *                         :               Theme.line
 *         }
 *     }
 *
 * A bordered control brightens its border; a pill inside a container that already has the
 * border lightens its fill instead; a bare glyph raises its opacity. One rule, three
 * renderings — and two of the three were already shipping.
 *
 * ⚠️ Never the accent. In this app the accent on an outline means "the keyboard or the pad is
 * here". Hover is the neutral `Theme.lineHigh`, and it changes colour ONLY — never width — so
 * it cannot produce a jump in geometry.
 *
 * That the outline can carry both is not a coincidence: it is an exclusive channel that is
 * already time-shared by input mode. `FocusFrame` is `visible: … && inputMode !== "pointer"`
 * and AboutLinkButton's `_keyFocused` reads the same way, so while the mouse is in hand the
 * app draws no focus ring at all and the outline is free. Focus still wins on the same
 * control, for the case where a stale focus and a live pointer meet.
 *
 * The two guards are load-bearing:
 *
 *   inputMode !== "key" — without it, on a handheld the control that happens to sit under the
 *   last mouse position stays lit for as long as the user drives with the pad, next to a focus
 *   ring that is somewhere else entirely.
 *
 *   target.enabled — a lit border and a pointing hand on a disabled control promise a click
 *   that is not there. ⚠️ It is only as good as the habit of actually setting `enabled` on
 *   what gets disabled: SegmentedSelector used to say it with an opacity on the fill plus a
 *   flag on the MouseArea alone, which this could not see.
 */
Item {
    id: hov

    /** The control being described. Read for `enabled`; nothing is written to it. */
    property Item target: parent

    readonly property bool armed: target !== null && target.enabled
                                  && SdlGamepadKeyNavigation.inputMode !== "key"

    readonly property bool active: armed && hh.hovered

    /**
     * The other half of the same rule: the control has the focus AND the focus should be
     * drawn at all. They live together because they are complementary — while the mouse is in
     * hand the focus ring is suppressed and the hover wash is shown, and the moment a key or a
     * pad button arrives they swap. Split across two files they would eventually disagree, and
     * the failure looks like two controls lit at once.
     *
     * ⚠️ Only for controls that have a hover state to take over. Suppressing the ring on
     * something with no pointer feedback of its own leaves the user with nothing at all — and
     * NEVER on a text field, where the ring says where the typing is going and the user very
     * probably clicked there with the mouse to put it there.
     */
    readonly property bool keyFocused: target !== null && target.activeFocus
                                       && SdlGamepadKeyNavigation.inputMode !== "pointer"

    anchors.fill: parent

    HoverHandler {
        id: hh
        // A HoverHandler observes and never accepts a press, so this cannot swallow the click
        // meant for the control underneath — which is what lets it sit on top of one.
        //
        // Arrow rather than "no cursor at all" when disarmed: cursorShape has no unset value
        // at runtime, and disabling the handler instead would mean hover does not come back
        // until the SECOND mouse move after the mode flips, the first one being what flips it.
        cursorShape: hov.armed ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
