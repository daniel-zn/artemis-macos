import QtQuick 2.0
import QtQuick.Controls 2.5

import NativeChrome 1.0

import "MacSpacing.js" as MacSpacing

Dialog {
    id: dialog
    modal: true
    anchors.centerIn: Overlay.overlay

    // macOS: a rounded sheet-like panel over a dimmed window, as in system alerts.
    // The style's background and footer are replaced, not destroyed: the control hides the
    // old ones, and they go with the dialog. Destroying them here broke the Settings page
    // when it's built in the background (main.qml, at launch): a destroy runs at the next
    // event-loop pass, and when that came before the page was finished, Qt dropped the
    // half-made page ("Object or context destroyed during incubation") - with a handler of
    // it still running, a Qt Fatal (seen 2026-11-09, SettingsView's resolution list).
    Component.onCompleted: {
        if (NativeChrome.enabled) {
            Overlay.modal = macModalDim
            padding = MacSpacing.dialogPadding
            background = macBackgroundComponent.createObject(dialog)

            // The fallback style's button box draws old-looking buttons on a square
            // panel. Dialogs that bring their own (NavigableMessageDialog) keep theirs.
            if (!footer || footer.objectName !== "macButtonBox") {
                footer = macButtonBoxComponent.createObject(dialog)
            }
        }
    }

    Component {
        id: macButtonBoxComponent
        DialogButtonBox {
            objectName: "macButtonBox"
            visible: count > 0
            alignment: Qt.AlignRight
            // Buttons inset like the dialog's text, with the same space under them
            leftPadding: MacSpacing.dialogPadding
            rightPadding: MacSpacing.dialogPadding
            bottomPadding: MacSpacing.dialogPadding
            topPadding: 0
            // The style's own is a square panel that pokes out of the rounded dialog
            background: Item { implicitHeight: 32 }
            delegate: Button {
                // The accepting button is the default (accent) button
                highlighted: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole ||
                             DialogButtonBox.buttonRole === DialogButtonBox.YesRole
            }
        }
    }

    Component {
        id: macBackgroundComponent
        Rectangle {
            radius: 16
            color: dialog.palette.window
            border.width: 1
            border.color: Qt.rgba(dialog.palette.text.r, dialog.palette.text.g, dialog.palette.text.b, 0.12)
        }
    }

    // macOS: a lighter dim, as behind system sheets. Other platforms keep their style's.
    Component {
        id: macModalDim
        Rectangle {
            color: Qt.rgba(0, 0, 0, 0.35)
        }
    }

    // The macOS menu bar stays usable behind modal dialogs, so it counts open ones.
    // A dialog can be destroyed while open (with its page), so that uncounts it too.
    property bool countedOpen: false
    function uncount() {
        if (countedOpen) {
            countedOpen = false
            if (window) {
                window.openDialogs--
            }
        }
    }
    onOpened: {
        if (!countedOpen) {
            countedOpen = true
            window.openDialogs++
        }
    }
    Component.onDestruction: uncount()

    onClosed: {
        uncount()

        // We must force focus back to the last item. If we don't,
        // gamepad and keyboard navigation will break after a
        // dialog appears.
        stackView.forceActiveFocus()
    }
}
