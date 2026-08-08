import Theme 1.0
import QtQuick 2.15

/*
 * The app's floor: charcoal at the top, washed with the accent towards the bottom.
 *
 * Extracted from AppShell so the screens that cover it whole — quitting, the PIN pad — stand
 * on the same ground instead of on a flat rectangle. The wash is derived rather than fixed:
 * a hardcoded green would have made the accent a lie the moment the user chose anything else.
 */
Rectangle {
    anchors.fill: parent

    gradient: Gradient {
        GradientStop { position: 0.0; color: "#151515" }
        GradientStop {
            position: 0.7
            color: Qt.tint("#151515", Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07))
        }
        GradientStop {
            position: 1.0
            color: Qt.tint("#151515", Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20))
        }
    }
}
