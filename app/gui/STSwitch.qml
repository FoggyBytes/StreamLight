import QtQuick
import QtQuick.Controls.Material
import QtQuick.Controls.Material.impl
import QtQuick.Templates as T

// A Material Switch whose hover/focus/press halo (the grey-or-green circle behind
// the handle) is half the stock size. The Material style hardcodes that Ripple at
// 28x28 in QtQuick/Controls/Material/Switch.qml; everything else here mirrors the
// stock indicator verbatim so the track and handle look identical — only the
// Ripple's diameter is reduced to 14x14.
T.Switch {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 8
    spacing: 8

    icon.width: 16
    icon.height: 16
    icon.color: checked
        ? (Material.theme === Material.Light
           ? enabled ? Qt.darker(Material.switchCheckedTrackColor, 1.8) : Material.switchDisabledCheckedIconColor
           : enabled ? Material.primaryTextColor : Material.switchDisabledCheckedIconColor)
        : enabled ? Material.switchUncheckedTrackColor : Material.switchDisabledUncheckedIconColor

    indicator: SwitchIndicator {
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        control: control

        Ripple {
            x: parent.handle.x + parent.handle.width / 2 - width / 2
            y: parent.handle.y + parent.handle.height / 2 - height / 2
            width: 14
            height: 14
            pressed: control.pressed
            active: control.enabled && (control.down || control.visualFocus || control.hovered)
            color: control.checked ? control.Material.highlightedRippleColor : control.Material.rippleColor
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0

        text: control.text
        font: control.font
        color: control.enabled ? control.Material.foreground : control.Material.hintTextColor
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
