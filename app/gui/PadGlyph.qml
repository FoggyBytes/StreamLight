import QtQuick 2.15
import SdlGamepadKeyNavigation 1.0

// Renders a single gamepad button as the correct vendor glyph for the active
// glyph set. Face buttons are resolved by SDL POSITION (Nintendo swaps A/B and
// X/Y relative to Xbox), matching AppShell's status-bar logic. Every bindable
// button has a glyph, but a labelled-pill fallback is kept for safety.
Item {
    id: glyph

    property string buttonKey: ""   // "A","B","X","Y","LB","RB","START","SELECT"
    property string label: ""        // text fallback
    property string glyphSet: SdlGamepadKeyNavigation.controllerType // "xbox"/"ps"/"switch"/...
    property int size: 22

    implicitWidth: size
    implicitHeight: size

    readonly property bool _ps: glyphSet === "ps"
    readonly property bool _sw: glyphSet === "switch"

    // Direct ternary binding (no helper function) so it re-resolves reactively
    // when the glyph set changes — a function-call binding can fail to register
    // the dependency under compiled QML, which left these stale on a vendor
    // switch while the status bar updated correctly.
    readonly property string _resolved:
          buttonKey === "A"      ? (_ps ? "qrc:/res/pad_ps_cross.svg"    : _sw ? "qrc:/res/pad_switch_b.svg"     : "qrc:/res/pad_xbox_a.svg")
        : buttonKey === "B"      ? (_ps ? "qrc:/res/pad_ps_circle.svg"   : _sw ? "qrc:/res/pad_switch_a.svg"     : "qrc:/res/pad_xbox_b.svg")
        : buttonKey === "X"      ? (_ps ? "qrc:/res/pad_ps_square.svg"   : _sw ? "qrc:/res/pad_switch_y.svg"     : "qrc:/res/pad_xbox_x.svg")
        : buttonKey === "Y"      ? (_ps ? "qrc:/res/pad_ps_triangle.svg" : _sw ? "qrc:/res/pad_switch_x.svg"     : "qrc:/res/pad_xbox_y.svg")
        : buttonKey === "LB"     ? (_ps ? "qrc:/res/pad_ps_l1.svg"       : _sw ? "qrc:/res/pad_switch_l.svg"     : "qrc:/res/pad_xbox_lb.svg")
        : buttonKey === "RB"     ? (_ps ? "qrc:/res/pad_ps_r1.svg"       : _sw ? "qrc:/res/pad_switch_r.svg"     : "qrc:/res/pad_xbox_rb.svg")
        : buttonKey === "SELECT" ? (_ps ? "qrc:/res/pad_ps_create.svg"   : _sw ? "qrc:/res/pad_switch_minus.svg" : "qrc:/res/pad_xbox_view.svg")
        : buttonKey === "START"  ? (_ps ? "qrc:/res/pad_ps_options.svg"  : _sw ? "qrc:/res/pad_switch_plus.svg"  : "qrc:/res/pad_xbox_start.svg")
        : ""

    Image {
        visible: glyph._resolved !== ""
        anchors.centerIn: parent
        width: glyph.size
        height: glyph.size
        source: glyph._resolved
        sourceSize.width: glyph.size
        sourceSize.height: glyph.size
        fillMode: Image.PreserveAspectFit
        mipmap: true
    }

    Rectangle {
        visible: glyph._resolved === ""
        anchors.fill: parent
        radius: 5
        color: "#1e2129"
        border.color: "#3a3f4a"
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: glyph.label
            color: "#dfe2e8"
            font.family: "DM Sans"
            font.pixelSize: Math.max(11, glyph.size - 11)
            font.bold: true
        }
    }
}
