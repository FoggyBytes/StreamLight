import QtQuick
import QtQuick.Window
import QtQuick.Effects

// The large game cover, drawn once for everywhere that shows one at size: the host
// page's spotlight and the launch curtain.
//
// It exists as a component for the reason CoverPalette and CoverAmbient do — two
// copies of a look diverge at the first tweak, and these two are meant to read as
// the same picture on two screens a second apart. Give it a height; the 2:3 box,
// the crop, the rounded corners and the shadow come with it.
//
// ⚠️ The rounding and the shadow are TWO passes with one job each, and that is not
// tidiness. One MultiEffect doing both deforms this cover — it was believed to be the
// cause of the stretch on the way back from a stream (5.1.2) and it is not, or not all
// of it, since the stretch survives the split; but the mismatch below is real and
// measured, so the split stays: a shadow makes MultiEffect widen its own rendered rect so
// the blur is not clipped (autoPaddingEnabled, on by default) while the mask stays
// the size of the item, and the effect resolves the mismatch by stretching. Split,
// the masked pass has no padding to disagree about and the shadow pass has no mask
// to disagree with. Measured with a circle-in-a-box harness: round in, round out,
// for a 2:3 source and a correct crop for anything else.
Item {
    id: root

    property url source
    property real radius: 8
    property bool shadow: true
    property real shadowOffset: 8
    // Lets a caller show a placeholder when there is no artwork or it failed.
    readonly property int status: art.status

    // ═════════════════════════════════════════════════════════════════════════
    // Repair on the way back from a stream — a workaround, and labelled as one
    // ═════════════════════════════════════════════════════════════════════════
    /*
     * Coming back from a stream the cover can return stretched, and it stays that way
     * until the selection moves to another game and back. That is a genuine source
     * reload, and it clears it every time — so this reproduces exactly that, at the
     * moment the window comes back, instead of waiting for the user to do it by hand.
     *
     * ⚠️ This is a REPAIR, not a fix, and it is the second time it has been here: an
     * equivalent lived in AppsScreen through 5.0.0/5.1.1 and was deleted in 5.1.2 when
     * the split-pass mask/shadow above was believed to be the cause. It was not — the
     * deformation survives into 5.2.0 with the split in place, and three attempts at a
     * root cause have failed (CLAUDE.md §55 ⑨ lists what has been excluded by
     * measurement: the effect chain in isolation, the box geometry, fillMode under a
     * layer, the host-side file and /appasset). It is back because the symptom is worth
     * repairing while the cause is still open, not because the cause is understood.
     *
     * The one measurement still missing is the size of the file the client is actually
     * drawing at the moment it looks wrong. If that ever gets taken and the cause turns
     * out to be somewhere else entirely, delete this block rather than leave two
     * mechanisms fighting over the same picture.
     */
    property bool _repairing: false

    // Forces the source through a real reload. Public so a caller can drive it too.
    function regenerate() {
        if (String(root.source) === "") return
        _repairing = true
        // ⚠️ art.source is BOUND to root.source. Clearing it imperatively drops that
        // binding, so it has to go back AS A BINDING — a plain value would freeze this
        // cover on the game that happened to be selected here. (This only touches the
        // component's internal binding; whatever the caller bound to root.source is
        // untouched, which is what makes this safe to live in the shared component.)
        art.source = ""
        art.source = Qt.binding(function() { return root.source })
        repairGuard.restart()
    }

    // The window is hidden for the length of a session and shown again on return, so its
    // own visibility is the signal — nothing has to be threaded down from main.qml. The
    // launch curtain's instance hears it too and does nothing with it: the curtain is
    // gone by the time the window comes back.
    Connections {
        target: Window.window
        function onVisibleChanged() {
            if (Window.window && Window.window.visible)
                repairDelay.restart()
        }
    }

    // The window is back before its scene graph has been rebuilt and presented, and on
    // the full-screen path restoreAfterStream() then recreates the native window a tick
    // later, tearing it down once more. The reload has to land after that. 150 ms is a
    // hedge, not a measurement — if the repair ever misses, this is the number to look
    // at first.
    Timer { id: repairDelay; interval: 150; onTriggered: root.regenerate() }

    // If the reload never resolves, the cover must not stay invisible forever.
    Timer { id: repairGuard; interval: 3000; onTriggered: root._repairing = false }

    // ⚠️ 2:3 by construction, and the caller only sets the height. The box used to
    // take the artwork's own shape, which made the column change width as the
    // selection moved down the list. 2:3 is also what the list rows use and what
    // every cover the host produces is — but NOT what every cover IS: they arrive
    // from seven stores and StreamTweak only enforces a floor on the height, so
    // anything off-ratio is cropped rather than letterboxed. That is the same trade
    // the host card, the last-session strip and StreamTweak itself all take.
    width: Math.round(height * 2 / 3)

    Image {
        id: art
        anchors.fill: parent
        source: root.source
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
        mipmap: true
        visible: false

        // Deferred by a tick on purpose: it gives the two effect passes above one frame
        // to redraw from the new texture, so un-hiding cannot flash the stale one.
        onStatusChanged: {
            if (root._repairing && (status === Image.Ready || status === Image.Error)) {
                Qt.callLater(function() {
                    root._repairing = false
                    repairGuard.stop()
                })
            }
        }
    }

    MultiEffect {
        id: rounded
        anchors.fill: parent
        source: art
        maskEnabled: true
        maskSource: coverMask
        autoPaddingEnabled: false
        // Its own layer, so the pass below can sample it. Drawn by that pass, not here.
        layer.enabled: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: rounded
        // Hidden across a reload, so the blank frame between the two source assignments
        // is not a flash. ⚠️ On this pass and not on root: the launch curtain binds
        // root.opacity to its fade-in, and writing it here would fight that binding.
        opacity: root._repairing ? 0 : 1
        shadowEnabled: root.shadow
        shadowColor: "#000000"
        shadowBlur: 0.9
        shadowVerticalOffset: root.shadowOffset
        shadowOpacity: 0.65
        blurMax: 40
    }

    Item {
        id: coverMask
        anchors.fill: parent
        layer.enabled: true
        visible: false
        Rectangle { anchors.fill: parent; radius: root.radius }
    }
}
