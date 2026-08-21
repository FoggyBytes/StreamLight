import QtQuick
import QtQuick.Shapes
import Theme 1.0

// The app's busy spinner, drawn here instead of using Material's BusyIndicator.
//
// ⚠️ Why we do not use the stock control: its arc is built as triangle geometry with
// flat ends and no track, so the silhouette is never a circle and the two ends read as
// square corners hanging off the ring. It is not a sizing artefact - it was compared
// side by side at seven sizes including the control's own implicit one, and every one
// of them showed it. Do not "fix" this by going back to BusyIndicator with a different
// height; that is the road this came from.
//
// Two properties of this one are deliberate and load-bearing:
//   - the dim track is a full 360° arc, so the outline is a true circle at every
//     instant regardless of where the moving arc happens to be;
//   - both caps are round, so neither end can produce a corner.
//
// It also ends the sizing fragility that produced a speck at small sizes (§41): the
// stroke is a fraction of the box, so it stays proportionate from 24px to 120px.
Item {
    id: root

    // Follows the user's accent by default, like everything else that is drawn rather
    // than themed. Read from Theme and not Material.accent: attached properties assigned
    // from JS do not track, which is the trap documented in §40.
    property color color: Theme.accent
    property bool running: true
    property real thickness: Math.max(1.5, Math.min(width, height) * 0.085)

    // ⚠️ The ratio lives here and not at the call sites. Ten spinners sized by hand is
    // how this ended up with rings running from 1.3 to 3.1 times the text they sat next
    // to, and with one of them not scaling with its dialog at all. Set `bodySize` to the
    // pixelSize of the text the spinner shares a line with and the size follows.
    //
    // ⚠️ Give it the FONT SIZE, never the label's height: a Label height is the whole
    // line box with the leading in it, roughly 1.2x the body, which is how a nominal 1.4
    // once came out at 1.69 and dominated the line.
    //
    // Left at 0 for a spinner that is a block of its own, stacked above text rather than
    // beside a word - there the ratio has no meaning and the size is set explicitly.
    readonly property real ringToBody: 1.30
    property real bodySize: 0

    implicitWidth: bodySize > 0 ? Math.round(bodySize * ringToBody) : 48
    implicitHeight: implicitWidth

    // Mirrors BusyIndicator's behaviour so call sites that use `running` as a visibility
    // switch keep working unchanged.
    opacity: running ? 1 : 0
    Behavior on opacity { OpacityAnimator { duration: 250 } }

    // Clamped at zero: for the frame or two before a layout has given this a size, the
    // radius would otherwise go negative and PathAngleArc would be asked to draw an arc
    // that cannot exist.
    readonly property real _r: Math.max(0, (Math.min(width, height) - thickness) / 2)

    Shape {
        anchors.fill: parent
        antialiasing: true
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Qt.rgba(root.color.r, root.color.g, root.color.b, 0.20)
            strokeWidth: root.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: root._r
                radiusY: root._r
                startAngle: 0
                sweepAngle: 360
            }
        }
    }

    Shape {
        anchors.fill: parent
        antialiasing: true
        preferredRendererType: Shape.CurveRenderer
        transformOrigin: Item.Center

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: root._r
                radiusY: root._r
                startAngle: -90
                sweepAngle: 100
            }
        }

        // Stopped when not running or not on screen: a rotation animator left going
        // behind a closed dialog is a wakeup per frame for something nobody can see.
        RotationAnimator on rotation {
            running: root.running && root.visible
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
        }
    }
}
