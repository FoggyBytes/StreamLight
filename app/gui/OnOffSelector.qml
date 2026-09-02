import QtQuick 2.15

/*
 * A two-pill Off/On strip. Replaces the Material switch that every boolean row in
 * Settings used until 5.6.0.
 *
 * Why it exists as a component rather than as `SegmentedSelector { labels: ["Off", "On"] }`
 * written out 33 times: the label pair, the index↔bool mapping and the binding rule below
 * are one decision each, and 33 copies of a decision is 33 chances for one of them to be
 * made differently. It also means the day Off/On wants a different shape, it changes here.
 *
 * ⚠️ Why the switches went. They were the only control on the Settings screen that did not
 * grow with the interface scale — a Material Switch is sized by the style, not by our
 * `_px()` — so on a handheld at uiScale 1.44 every row grew around a control that stayed
 * put. The pills are a SegmentedSelector, which reads Theme.uiScale itself. The second
 * reason is that the host-profile and per-game panels had already been Off/On strips since
 * 4.0.0, so a boolean was two different controls depending on which screen you were on.
 */
SegmentedSelector {
    id: sel

    // The value shown. Bind it to the setting; never assign it.
    property bool checked: false

    // Emitted only when a person changes it — a click, or ◀/▶ on the pad. Never on a
    // binding's first evaluation.
    //
    // ⚠️ This is the behavioural difference from the switches, and it is the reason the
    // guards went with them. `onCheckedChanged` fired when the binding to the preference
    // was first evaluated, so every handler had to compare against the stored value before
    // writing, or opening this page would have written the whole preference set once per
    // switch. Nothing here fires at load, so `onToggled` can write and save unconditionally.
    //
    // ⚠️ One row depended on that first evaluation rather than merely tolerating it — the
    // bitrate unlock, which re-clamped a stored bitrate above the locked ceiling. It now
    // does that from its own Component.onCompleted, where it is visible as the load-time
    // step it always was.
    signal toggled(bool value)

    labels: [qsTr("Off"), qsTr("On")]

    /*
     * ⚠️ A `Binding on`, not `currentIndex: sel.checked ? 1 : 0`.
     *
     * SegmentedSelector assigns `currentIndex` imperatively when a pill is clicked or the
     * d-pad moves, which destroys a plain binding: the strip would follow the first click
     * and then stop following the setting, so anything that changes the preference from
     * elsewhere — a reset, a profile, the other end of a dependency — would leave the pills
     * showing a value that is no longer true. A `Binding on` survives the imperative write
     * and re-asserts itself the moment `checked` changes.
     *
     * The same shape is already used by the Frame Pacing selector on this screen and by the
     * override panels, for the same reason.
     */
    Binding on currentIndex {
        value: sel.checked ? 1 : 0
    }

    onActivated: function(index) { sel.toggled(index === 1) }
}
