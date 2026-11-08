import QtQuick 2.9
import QtQuick.Controls 2.2

import "MacSpacing.js" as MacSpacing

GridView {
    property int minMargin: 10
    property real availableWidth: (parent.width - 2 * minMargin)
    property int itemsPerRow: availableWidth / cellWidth
    // macOS: a single-column list, inset from the window's edges and centered when wide
    // (MacSpacing). Set cellWidth to the view's width and the rows to MacSpacing.rowWidth().
    property bool macList: false
    property real horizontalMargin: macList ? MacSpacing.sideMargin(width) :
                                    itemsPerRow < count && availableWidth >= cellWidth ?
                                        (availableWidth % cellWidth) / 2 : minMargin

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
