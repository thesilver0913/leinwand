// SPDX-License-Identifier: GPL-3.0-or-later
// The menu bar (spec 7.1): Illustrator's menus, with the commands that exist
// so far. Shortcuts come from the `Shortcuts` registry (spec 7.3).
import QtQuick
import QtQuick.Controls
import Leinwand

MenuBar {
    id: root
    required property var window  // Main.qml: dialogs and panels.
    readonly property var canvas: Session.canvas
    readonly property bool hasDocument: Session.hasDocument

    function key(id) {
        const sequences = Shortcuts[id];
        return sequences && sequences.length > 0 ? sequences[0] : "";
    }

    Menu {
        title: qsTr("&File")
        Action { text: qsTr("&New..."); shortcut: root.key("fileNew"); onTriggered: root.window.showWelcome() }
        Action { text: qsTr("&Open..."); shortcut: root.key("fileOpen"); onTriggered: root.window.guard(() => root.window.openDialog.open()) }
        Menu {
            id: recentMenu
            title: qsTr("Open &Recent Files")
            enabled: (Preferences.recentFiles ?? []).length > 0
            Instantiator {
                model: Preferences.recentFiles
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.replace(/^.*[\\/]/, "")
                    onTriggered: root.window.guard(() => Session.openPath(modelData))
                }
                onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => recentMenu.removeItem(object)
            }
        }
        MenuSeparator {}
        Action { text: qsTr("&Close"); enabled: root.hasDocument; shortcut: root.key("fileClose"); onTriggered: root.window.guard(() => Session.closeDocument()) }
        Action { text: qsTr("&Save"); enabled: root.hasDocument; shortcut: root.key("fileSave"); onTriggered: root.window.saveDocument(null) }
        Action { text: qsTr("Save &As..."); enabled: root.hasDocument; shortcut: root.key("fileSaveAs"); onTriggered: root.window.saveAsDialog.open() }
        MenuSeparator {}
        Menu {
            title: qsTr("&Export")
            enabled: root.hasDocument
            Action { text: qsTr("Export as &SVG..."); shortcut: root.key("fileExportSvg"); onTriggered: root.window.exportSvg() }
            Action { text: qsTr("Export as &PNG..."); shortcut: root.key("fileExportPng"); onTriggered: root.window.pngOptions.open() }
            Action { text: qsTr("Export as P&DF..."); shortcut: root.key("fileExportPdf"); onTriggered: root.window.pdfOptions.open() }
        }
        Action { text: qsTr("Pre&flight"); enabled: root.hasDocument; onTriggered: root.window.showPanel(root.window.preflight) }
        Action { text: qsTr("&Print..."); enabled: root.hasDocument; shortcut: root.key("filePrint"); onTriggered: root.window.printDialog.open() }
        MenuSeparator {}
        Action { text: qsTr("Co&ver Setup..."); enabled: root.hasDocument && Session.hasCover; onTriggered: root.window.coverDialog.open() }
        MenuSeparator {}
        Action { text: qsTr("E&xit"); shortcut: root.key("fileQuit"); onTriggered: root.window.close() }
    }
    Menu {
        title: qsTr("&Edit")
        Action {
            text: qsTr("&Undo")
            enabled: Session.undoAction !== ""
            shortcut: root.key("editUndo")
            onTriggered: Session.undo()
        }
        Action {
            text: qsTr("&Redo")
            enabled: Session.redoAction !== ""
            shortcut: root.key("editRedo")
            onTriggered: Session.redo()
        }
        MenuSeparator {}
        Action { text: qsTr("C&lear"); enabled: root.hasDocument; shortcut: root.key("editClear"); onTriggered: Session.deleteSelection() }
        MenuSeparator {}
        Action { text: qsTr("&Keyboard Shortcuts..."); shortcut: root.key("editShortcuts"); onTriggered: root.window.shortcutsDialog.show() }
        Action { text: qsTr("Pre&ferences..."); shortcut: root.key("editPreferences"); onTriggered: root.window.preferencesDialog.show() }
    }
    Menu {
        title: qsTr("&Object")
        enabled: root.hasDocument
        Menu {
            title: qsTr("&Arrange")
            Action { text: qsTr("Bring to &Front"); shortcut: root.key("objectBringToFront"); onTriggered: Session.arrange(0) }
            Action { text: qsTr("Bring &Forward"); shortcut: root.key("objectBringForward"); onTriggered: Session.arrange(1) }
            Action { text: qsTr("Send &Backward"); shortcut: root.key("objectSendBackward"); onTriggered: Session.arrange(2) }
            Action { text: qsTr("Send to Bac&k"); shortcut: root.key("objectSendToBack"); onTriggered: Session.arrange(3) }
        }
        MenuSeparator {}
        Action { text: qsTr("&Group"); shortcut: root.key("objectGroup"); onTriggered: Session.group() }
        Action { text: qsTr("&Ungroup"); shortcut: root.key("objectUngroup"); onTriggered: Session.ungroup() }
        MenuSeparator {}
        Menu {
            title: qsTr("&Path")
            Action { text: qsTr("&Join"); shortcut: root.key("objectJoin"); onTriggered: Session.joinEnds() }
            Action { text: qsTr("&Average..."); shortcut: root.key("objectAverage"); onTriggered: root.window.averageDialog.open() }
        }
        Menu {
            title: qsTr("Compound Pat&h")
            Action { text: qsTr("&Make"); shortcut: root.key("objectCompoundMake"); onTriggered: Session.makeCompoundPath() }
            Action { text: qsTr("&Release"); shortcut: root.key("objectCompoundRelease"); onTriggered: Session.releaseCompoundPath() }
        }
        Action { text: qsTr("Create &Trim Marks"); onTriggered: Session.createTrimMarks() }
        Menu {
            title: qsTr("Clipping &Mask")
            Action { text: qsTr("&Make"); shortcut: root.key("objectClipMake"); onTriggered: Session.makeClippingMask() }
            Action { text: qsTr("&Release"); shortcut: root.key("objectClipRelease"); onTriggered: Session.releaseClippingMask() }
        }
    }
    Menu {
        title: qsTr("&Type")
        enabled: root.hasDocument
        Action { text: qsTr("Create &Outlines"); shortcut: root.key("typeCreateOutlines"); onTriggered: Session.createOutlines() }
        Action { text: qsTr("&Revert Outlines to Text"); shortcut: root.key("typeRevertOutlines"); onTriggered: Session.revertOutlines() }
    }
    Menu {
        title: qsTr("&Select")
        enabled: root.hasDocument
        Action { text: qsTr("&All"); shortcut: root.key("selectAll"); onTriggered: Session.selectAll() }
        Action { text: qsTr("&Deselect"); shortcut: root.key("selectDeselect"); onTriggered: Session.deselect() }
    }
    Menu {
        title: qsTr("&View")
        Action {
            text: qsTr("&Outline")
            checkable: true
            checked: root.canvas ? root.canvas.outlineView : false
            shortcut: root.key("viewOutline")
            onTriggered: if (root.canvas) root.canvas.outlineView = !root.canvas.outlineView
        }
        MenuSeparator {}
        Action { text: qsTr("Zoom &In"); shortcut: root.key("viewZoomIn"); onTriggered: if (root.canvas) root.canvas.zoomIn() }
        Action { text: qsTr("Zoom &Out"); shortcut: root.key("viewZoomOut"); onTriggered: if (root.canvas) root.canvas.zoomOut() }
        Action { text: qsTr("&Fit Artboard in Window"); shortcut: root.key("viewFitArtboard"); onTriggered: if (root.canvas) root.canvas.fitArtboard() }
        Action { text: qsTr("Fit A&ll in Window"); shortcut: root.key("viewFitAll"); onTriggered: if (root.canvas) root.canvas.fitAll() }
        Action { text: qsTr("&Actual Size"); shortcut: root.key("viewActualSize"); onTriggered: if (root.canvas) root.canvas.actualSize() }
        MenuSeparator {}
        Action {
            text: qsTr("&Smart Guides")
            checkable: true
            checked: Session.smartGuides
            shortcut: root.key("viewSmartGuides")
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
                onTriggered: root.window.showPanel(modelData)
            }
            onObjectAdded: (index, object) => windowMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => windowMenu.removeItem(object)
        }
    }
    Menu {
        title: qsTr("&Help")
        Action { text: qsTr("&Welcome Screen"); onTriggered: root.window.showWelcome() }
        Action { text: qsTr("&About Leinwand"); onTriggered: root.window.aboutDialog.open() }
    }
}
