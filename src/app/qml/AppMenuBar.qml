// SPDX-License-Identifier: GPL-3.0-or-later
// The menu bar (spec 7.1): Illustrator's menus, with the commands that exist
// so far. Shortcuts live here, on the actions.
import QtQuick
import QtQuick.Controls
import Leinwand

MenuBar {
    id: root
    required property var window  // Main.qml: dialogs and panels.
    readonly property var canvas: Session.canvas

    Menu {
        title: qsTr("&File")
        Action { text: qsTr("&New"); shortcut: StandardKey.New; onTriggered: root.window.guard(() => Session.newDocument()) }
        Action { text: qsTr("&Open..."); shortcut: StandardKey.Open; onTriggered: root.window.guard(() => root.window.openDialog.open()) }
        MenuSeparator {}
        Action { text: qsTr("&Save"); shortcut: StandardKey.Save; onTriggered: root.window.saveDocument(null) }
        Action { text: qsTr("Save &As..."); shortcut: "Ctrl+Shift+S"; onTriggered: root.window.saveAsDialog.open() }
        MenuSeparator {}
        Menu {
            title: qsTr("&Export")
            Action { text: qsTr("Export as &SVG..."); onTriggered: root.window.exportSvg() }
            Action { text: qsTr("Export as &PNG..."); shortcut: "Ctrl+Alt+E"; onTriggered: root.window.pngOptions.open() }
        }
        MenuSeparator {}
        Action { text: qsTr("E&xit"); shortcut: StandardKey.Quit; onTriggered: root.window.close() }
    }
    Menu {
        title: qsTr("&Edit")
        Action {
            text: Session.undoAction ? qsTr("&Undo %1").arg(Session.undoAction) : qsTr("&Undo")
            enabled: Session.undoAction !== ""
            shortcut: "Ctrl+Z"
            onTriggered: Session.undo()
        }
        Action {
            text: Session.redoAction ? qsTr("&Redo %1").arg(Session.redoAction) : qsTr("&Redo")
            enabled: Session.redoAction !== ""
            shortcut: "Ctrl+Shift+Z"
            onTriggered: Session.redo()
        }
        MenuSeparator {}
        Action { text: qsTr("&Clear"); onTriggered: Session.deleteSelection() }
    }
    Menu {
        title: qsTr("&Object")
        Menu {
            title: qsTr("&Arrange")
            Action { text: qsTr("Bring to &Front"); shortcut: "Ctrl+Shift+]"; onTriggered: Session.arrange(0) }
            Action { text: qsTr("Bring &Forward"); shortcut: "Ctrl+]"; onTriggered: Session.arrange(1) }
            Action { text: qsTr("Send &Backward"); shortcut: "Ctrl+["; onTriggered: Session.arrange(2) }
            Action { text: qsTr("Send to Bac&k"); shortcut: "Ctrl+Shift+["; onTriggered: Session.arrange(3) }
        }
        MenuSeparator {}
        Action { text: qsTr("&Group"); shortcut: "Ctrl+G"; onTriggered: Session.group() }
        Action { text: qsTr("&Ungroup"); shortcut: "Ctrl+Shift+G"; onTriggered: Session.ungroup() }
        MenuSeparator {}
        Menu {
            title: qsTr("&Path")
            Action { text: qsTr("&Join"); shortcut: "Ctrl+J"; onTriggered: Session.joinEnds() }
        }
    }
    Menu {
        title: qsTr("&Select")
        Action { text: qsTr("&All"); shortcut: "Ctrl+A"; onTriggered: Session.selectAll() }
        Action { text: qsTr("&Deselect"); shortcut: "Ctrl+Shift+A"; onTriggered: Session.deselect() }
    }
    Menu {
        title: qsTr("&View")
        Action {
            text: qsTr("&Outline")
            checkable: true
            checked: root.canvas ? root.canvas.outlineView : false
            shortcut: "Ctrl+Y"
            onTriggered: if (root.canvas) root.canvas.outlineView = !root.canvas.outlineView
        }
        MenuSeparator {}
        Action { text: qsTr("Zoom &In"); shortcut: "Ctrl+="; onTriggered: if (root.canvas) root.canvas.zoomIn() }
        Action { text: qsTr("Zoom &Out"); shortcut: "Ctrl+-"; onTriggered: if (root.canvas) root.canvas.zoomOut() }
        Action { text: qsTr("&Fit Artboard in Window"); shortcut: "Ctrl+0"; onTriggered: if (root.canvas) root.canvas.fitArtboard() }
        Action { text: qsTr("&Actual Size"); shortcut: "Ctrl+1"; onTriggered: if (root.canvas) root.canvas.actualSize() }
        MenuSeparator {}
        Action {
            text: qsTr("&Smart Guides")
            checkable: true
            checked: Session.smartGuides
            shortcut: "Ctrl+U"
            onTriggered: Session.smartGuides = !Session.smartGuides
        }
    }
    Menu {
        id: windowMenu
        title: qsTr("&Window")
        Instantiator {
            model: root.window.panels
            delegate: MenuItem {
                required property var modelData
                text: modelData.title
                onTriggered: {
                    modelData.open();
                    modelData.setAsCurrentTab();
                }
            }
            onObjectAdded: (index, object) => windowMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => windowMenu.removeItem(object)
        }
    }
    Menu {
        title: qsTr("&Help")
        Action { text: qsTr("&About Leinwand"); onTriggered: root.window.aboutDialog.open() }
    }
}
