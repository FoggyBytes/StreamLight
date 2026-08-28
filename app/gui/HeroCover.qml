import QtQuick
import QtQuick.Effects

// The large game cover, drawn once for everywhere that shows one at size: the host
// page's spotlight and the launch curtain.
//
// It exists as a component for the reason CoverPalette and CoverAmbient do — two
// copies of a look diverge at the first tweak, and these two are meant to read as
// the same picture on two screens a second apart. Give it a height; the 2:3 box,
// the crop, the rounded corners and the shadow come with it.
//
// ═════════════════════════════════════════════════════════════════════════════
// ⚠️ THE ARTWORK NEVER GOES THROUGH A PASS THAT PADS. That is the whole shape of
// this file, and it is what finally ended the deformation that had been chased
// since 5.0.0 (the cover coming back from a stream stretched, or shoved sideways
// and clipped — issue #10).
//
// A MultiEffect with a shadow widens its own rendered rect so the blur is not
// clipped (autoPaddingEnabled, true by default) and the docs are explicit that
// this "causes a resize of the item". When the item then changes size — which it
// does on every return from a stream, because restoreAfterStream() goes
// showNormal() → showFullScreen() and Theme.uiScale is width/1330, so the cover
// shrinks and grows back — the padded rect and the content stop agreeing, and the
// picture is drawn offset and/or scaled inside a box that is still the right shape.
//
// 5.1.2 split the single mask+shadow MultiEffect in two on this reasoning and it
// was not enough: the shadow pass still padded on its own, so the fault survived
// into 5.2.0 with the split in place, and the 150 ms source-reload repair added
// on top of it never cleared it either.
//
// Measured 28/08/2026 with a circle-in-a-box harness driving the real sequence
// (visible=false → visible=true → showNormal → showFullScreen), grabbing the item
// and measuring the circle against the box:
//
//   as shipped, after the cycle   circle 325x243, centre 164 px right of the box
//   as shipped, DPR forced to 1   circle round, centre 205 px right of the box
//   with shadow: false            round and centred
//   plain Image, no effects       round and centred
//   this file                     round and centred, DPR 1 and 2, two cycles
//
// So: the shadow is cast by a black SILHOUETTE of the same geometry drawn behind,
// which is the only pass that keeps autoPaddingEnabled — it needs the room, and if
// its rect ever slips, all that slips is a soft black shape hidden behind an opaque
// cover. The picture itself goes through one masked pass with padding off, where
// there is nothing for the rendered rect and the mask to disagree about.
//
// Do not merge the two passes back, and do not turn autoPadding on for the masked
// one to "let the corners breathe".
// ═════════════════════════════════════════════════════════════════════════════
Item {
    id: root

    property url source
    property real radius: 8
    property bool shadow: true
    property real shadowOffset: 8
    // Lets a caller show a placeholder when there is no artwork or it failed.
    readonly property int status: art.status

    // ⚠️ 2:3 by construction, and the caller only sets the height. The box used to
    // take the artwork's own shape, which made the column change width as the
    // selection moved down the list. 2:3 is also what the list rows use and what
    // every cover the host produces is — but NOT what every cover IS: they arrive
    // from seven stores and StreamTweak only enforces a floor on the height, so
    // anything off-ratio is cropped rather than letterboxed. That is the same trade
    // the host card, the last-session strip and StreamTweak itself all take.
    width: Math.round(height * 2 / 3)

    // ── The shadow, cast by a silhouette rather than by the picture ───────────
    Item {
        id: silhouette
        anchors.fill: parent
        layer.enabled: true
        visible: false
        Rectangle { anchors.fill: parent; radius: root.radius; color: "black" }
    }

    MultiEffect {
        anchors.fill: parent
        source: silhouette
        visible: root.shadow
        shadowEnabled: root.shadow
        shadowColor: "#000000"
        shadowBlur: 0.9
        shadowVerticalOffset: root.shadowOffset
        shadowOpacity: 0.65
        blurMax: 40
    }

    // ── The picture ──────────────────────────────────────────────────────────
    Image {
        id: art
        anchors.fill: parent
        source: root.source
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
        mipmap: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: art
        maskEnabled: true
        maskSource: coverMask
        autoPaddingEnabled: false
    }

    Item {
        id: coverMask
        anchors.fill: parent
        layer.enabled: true
        visible: false
        Rectangle { anchors.fill: parent; radius: root.radius }
    }
}
