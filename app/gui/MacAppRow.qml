import QtQuick
import QtQuick.Controls

import "MacSpacing.js" as MacSpacing

// One game in the macOS game list: a small rounded box art thumbnail, the name,
// and a "Running" badge. The row's buttons and behavior live in AppView.
// Only loaded on macOS (Qt 6).
Item {
    id: row

    property url boxArt
    property string name
    property bool running: false
    property bool selected: false
    property bool hovered: false
    // Leave room on the right for AppView's Resume/Quit buttons
    property real trailingSpace: 0

    // White on the accent color; in an inactive window the selection turns
    // gray, so the text stays dark there, as in Finder
    readonly property bool onAccent: selected && Window.active
    readonly property color textColor: onAccent ? "white" : palette.text

    Rectangle {
        anchors.fill: parent
        radius: MacSpacing.rowRadius
        color: row.selected ? palette.highlight
             : row.hovered ? Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.08)
             : "transparent"
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    MacBoxArt {
        id: thumbnail
        anchors.left: parent.left
        anchors.leftMargin: MacSpacing.rowInset
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height - 12
        width: height * 0.75
        radius: 6
        source: row.boxArt
    }

    Column {
        anchors.left: thumbnail.right
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: MacSpacing.rowInset + row.trailingSpace
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        Label {
            width: parent.width
            text: row.name
            color: row.textColor
            font.pixelSize: 14
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Row {
            spacing: 5
            visible: row.running

            Rectangle {
                width: 7; height: 7; radius: 3.5
                anchors.verticalCenter: parent.verticalCenter
                color: row.onAccent ? "white" : "#30D158"
            }
            Label {
                text: qsTr("Running")
                color: row.textColor
                opacity: 0.75
                font.pixelSize: 11
            }
        }
    }
}
