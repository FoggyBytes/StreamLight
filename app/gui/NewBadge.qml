import QtQuick 2.15

import Theme 1.0
import SystemProperties 1.0

/*
 * "NEW" beside something the installed release added (7.0.0).
 *
 * `since` is the version that added the thing. The badge shows only while that is the version
 * running, so it goes away by itself with the next release — nothing to remember to remove,
 * and nothing stored: a user who updates twice in a row never sees a stale one.
 *
 * On its first appearance it gets one pass of light across it and then stays still, so it is
 * noticed without ever moving again while the row is in view.
 */
Rectangle {
    id: badge

    property string since: ""
    // Drawn over a picture (the host card's): the card's light palette instead of the theme's,
    // or in Light the word and its outline go dark on a dark picture (21/10/2026).
    property bool onPicture: false

    readonly property real _u: Theme.uiScale
    function _px(n) { return Math.round(n * _u) }

    visible: since.length > 0 && SystemProperties.versionString === since

    implicitWidth: label.implicitWidth + _px(10)
    implicitHeight: label.implicitHeight + _px(2)
    radius: _px(4)
    color: "transparent"
    border.color: onPicture ? Theme.stageLineHigh : Theme.lineHigh
    border.width: 1
    clip: true

    Text {
        id: label
        anchors.centerIn: parent
        text: qsTr("NEW")
        color: badge.onPicture ? Theme.accent : Theme.inkText
        font.family: Theme.family
        font.pixelSize: badge._px(10)
        font.bold: true
        font.letterSpacing: badge._u * 1.2
    }

    // The one pass of light. A band a little wider than half the badge, swept once from left
    // to right when the badge first becomes visible.
    Rectangle {
        id: shine
        width: badge.width * 0.6
        height: badge.height
        x: -width
        opacity: 0.55
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.light ? Qt.rgba(1, 1, 1, 0.9) : Qt.rgba(1, 1, 1, 0.7) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
    NumberAnimation {
        id: sweep
        target: shine; property: "x"
        from: -shine.width; to: badge.width
        duration: 700; easing.type: Easing.InOutQuad
    }
    property bool _shone: false
    onVisibleChanged: _maybeShine()
    Component.onCompleted: _maybeShine()
    function _maybeShine() {
        if (!visible || _shone || Theme.reduceAnimations) return
        _shone = true
        sweep.start()
    }
}
