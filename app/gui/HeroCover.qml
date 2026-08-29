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
// So: the shadow is cast by a black SILHOUETTE of the same geometry drawn behind.
// The picture itself goes through one masked pass with padding off, where there is
// nothing for the rendered rect and the mask to disagree about.
//
// ⚠️ This block used to end by saying the shadow pass could keep autoPaddingEnabled
// because "if its rect ever slips, all that slips is a soft black shape hidden behind
// an opaque cover". THAT WAS WRONG, and it is what shipped the black stripe in 5.3.0:
// a MultiEffect with shadowEnabled draws its SOURCE as well as the shadow, and the
// source here is a hard-edged opaque black rectangle, so a slip shows flat black
// rather than a soft shape. Measured 29/08/2026 — see the block above the silhouette.
//
// Do not merge the two passes back, do not turn autoPadding on for the masked one to
// "let the corners breathe", and do not give the shadow pass any horizontal padding.
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
    //
    // ⚠️ THE SHADOW PASS ASKS MultiEffect FOR NO PADDING AT ALL. The room the blur needs
    // is built into this item's own geometry instead, and that is the fix for the black
    // stripe down the right edge of the cover (issue #11, new in 5.3.0 — before it there
    // was no opaque black shape behind the picture for a seam to expose).
    //
    // MultiEffect's HORIZONTAL padding is computed from a stale width. Measured with a
    // probe driving this very file: at 320x480 the shadow pass reports itemRect
    // (-40,-40,400,568), exactly right. Take the same cover to 691x1036.8 and it reports
    // (-18.5,-40,357,1124.8) — the vertical half is still correct (1036.8+88), the
    // horizontal half is the correct figure multiplied by 320/691: -40*320/691 = -18.5,
    // 771*320/691 = 357. The silhouette texture is then sampled at that wrong horizontal
    // scale, ends up a couple of device pixels wider than the artwork drawn over it, and
    // flat black shows along the right edge. Confirmed by identity, not inference:
    // recolouring the rectangle below red turned the stripe red.
    //
    // It is NOT autoPaddingEnabled specifically — asking for the same room through an
    // explicit paddingRect reproduces the identical broken width. ANY horizontal padding
    // does. And autoPaddingEnabled: false on its own does clear the stripe, but then the
    // blur is clipped to the item and there is no shadow left at all (measured: "NO
    // SHADOW" in every state).
    //
    // So the silhouette is the padded item, the effect matches it 1:1, and the black
    // rectangle sits inset within it. Nothing has to agree about a padding rect any more.
    // ⚠️ ONE number, used as both the blur ceiling and the room reserved for it. They are
    // the same quantity and were written twice as 40 at first; if they ever disagree the
    // blur is clipped again (pad too small) or texture is wasted (pad too large), and
    // nothing would say so.
    readonly property int  _blurMax: 40
    readonly property real _shadowPad: _blurMax

    Item {
        id: silhouette
        x: -root._shadowPad
        y: -root._shadowPad
        width: root.width + root._shadowPad * 2
        // The extra room at the bottom is the shadow's own downward offset.
        height: root.height + root._shadowPad * 2 + root.shadowOffset
        // Tied to root.shadow: with the shadow off (Theme.reduceAnimations on the host
        // page) the pass below is invisible, and a layer kept enabled would still be
        // allocating a texture bigger than the cover for something nobody draws.
        layer.enabled: root.shadow
        visible: false
        Rectangle {
            x: root._shadowPad
            y: root._shadowPad
            width: root.width
            height: root.height
            radius: root.radius
            color: "black"
        }
    }

    MultiEffect {
        // Named so a probe can read itemRect off it without a copy of this file that
        // then drifts. Costs nothing at runtime.
        objectName: "heroShadowPass"
        // Matches the silhouette exactly — same origin, same size — so the texture maps
        // 1:1 and no padding arithmetic happens anywhere.
        x: silhouette.x
        y: silhouette.y
        width: silhouette.width
        height: silhouette.height
        source: silhouette
        visible: root.shadow
        shadowEnabled: root.shadow
        shadowColor: "#000000"
        shadowBlur: 0.9
        shadowVerticalOffset: root.shadowOffset
        shadowOpacity: 0.65
        blurMax: root._blurMax
        autoPaddingEnabled: false
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
        objectName: "heroArtPass"
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
