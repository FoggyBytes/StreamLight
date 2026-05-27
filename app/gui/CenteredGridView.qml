import QtQuick 2.9
import QtQuick.Controls 2.2

GridView {
    property int minMargin: 10
    // Use our own width, not parent.width — the grid may be nested inside an
    // Item with its own anchors margins, in which case parent.width would
    // overestimate the space available and the columns would be misaligned.
    property real availableWidth: (width - 2 * minMargin)
    // How many columns physically fit in the available space.
    property int itemsPerRow: Math.max(1, Math.floor(availableWidth / cellWidth))
    // How many columns we will actually render in the top row (clamped by
    // total item count). This lets us centre the whole block even when the
    // grid has very few items.
    property int activeColumns: count > 0 ? Math.min(itemsPerRow, count) : itemsPerRow
    // Always centre: split the leftover horizontal space equally on both
    // sides, regardless of whether the grid is full or partially filled.
    property real horizontalMargin: availableWidth >= cellWidth
        ? (width - activeColumns * cellWidth) / 2
        : minMargin

    function updateMargins() {
        leftMargin = horizontalMargin
        rightMargin = horizontalMargin
    }

    onHorizontalMarginChanged: {
        updateMargins()
    }

    Component.onCompleted: {
        updateMargins()
    }

    boundsBehavior: Flickable.OvershootBounds
}
