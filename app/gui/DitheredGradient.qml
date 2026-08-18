import QtQuick 2.15

/*
 * A gradient that does not band. Drop-in for a Rectangle's `gradient:` — see
 * backend/gradientimage.h for why laying one of these over a plain Rectangle gradient was
 * never going to work.
 *
 * Usage:
 *     DitheredGradient {
 *         anchors.fill: parent
 *         orientation: Qt.Horizontal
 *         stops: [ { pos: 0.0, color: "#151515" },
 *                  { pos: 1.0, color: Theme.accent } ]
 *     }
 *
 * ⚠️ It has no radius of its own. Inside a rounded card it needs the same mask treatment as
 * any other full-bleed layer — `clip` is rectangular and will not do it.
 */
Item {
    id: root

    /** Qt.Vertical runs top to bottom, Qt.Horizontal left to right. */
    property int orientation: Qt.Vertical

    /** [{ pos: 0..1, color: <color or string> }, …], in ascending pos order. */
    property var stops: []

    readonly property bool _vertical: orientation === Qt.Vertical

    // Qt.tint with a fully transparent overlay is the identity, and the cheapest way to
    // accept "#151515" and a real colour on equal terms: a plain string has no .r/.g/.b.
    function _hex(c) {
        var col = Qt.tint(c, Qt.rgba(0, 0, 0, 0))
        function h(v) {
            var s = Math.round(v * 255).toString(16)
            return s.length < 2 ? "0" + s : s
        }
        return h(col.a) + h(col.r) + h(col.g) + h(col.b)
    }

    readonly property string _id: {
        var parts = []
        for (var i = 0; i < root.stops.length; i++) {
            parts.push(root.stops[i].pos + ":" + root._hex(root.stops[i].color))
        }
        return (root._vertical ? "v" : "h") + "/" + parts.join(",")
    }

    Image {
        anchors.fill: parent
        source: root.stops.length > 0 ? "image://gradient/" + root._id : ""

        // The provider returns a strip: full length along the ramp, one 8px Bayer tile
        // across it. Ceil, not round — a source one pixel short of the item would tile a
        // sliver of the ramp's start back in at the far edge.
        sourceSize: root._vertical
                    ? Qt.size(8, Math.max(1, Math.ceil(root.height)))
                    : Qt.size(Math.max(1, Math.ceil(root.width)), 8)

        fillMode: Image.Tile

        // ⚠️ Both are load-bearing. Smoothing would interpolate neighbouring pixels and
        // average the dither straight back out — undoing the whole thing — and caching a
        // fresh image for every size crossed during a window drag is a lot of pixmaps for
        // something this cheap to rebuild.
        smooth: false
        cache: false
        asynchronous: false
    }
}
