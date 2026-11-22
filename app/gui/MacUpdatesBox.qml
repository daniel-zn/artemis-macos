import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import MacUpdater 1.0

// Settings › General on macOS: the self-update's status and its action (backend/macupdater.*),
// like Settings › About in Mercurius for Mac. The Artemis menu has Check for Updates… /
// Restart to Update too. Only loaded on macOS, by SettingsView.qml.
GroupBox {
    padding: 12
    title: sectionTitle(qsTr("Updates"))
    font.pointSize: settingsPage.bodyPointSize

    RowLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            objectName: "updateStatus"
            Layout.fillWidth: true
            text: MacUpdater.status
            font.pointSize: settingsPage.bodyPointSize
            opacity: 0.7
            wrapMode: Text.Wrap
        }

        BusyIndicator {
            visible: MacUpdater.busy
            running: visible
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
        }

        Button {
            objectName: "updateButton"
            visible: !MacUpdater.busy
            text: MacUpdater.ready ? qsTr("Restart to Update") : qsTr("Check Now")
            highlighted: MacUpdater.ready
            onClicked: MacUpdater.ready ? MacUpdater.restartToUpdate() : MacUpdater.checkNow()
        }
    }
}
