// SPDX-License-Identifier: GPL-3.0-or-later
// Keyboard shortcuts (spec 7.3): every command with its keys, grouped. Click
// a key field and press the new keys; Backspace clears it. Sets can be
// exported and imported as files.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Leinwand

Window {
    id: root
    width: 640
    height: 560
    minimumWidth: 520
    minimumHeight: 400
    title: qsTr("Keyboard Shortcuts")
    color: Spectrum.backgroundLayer2Color
    property string message

    readonly property var groups: ({
        file: qsTr("File"), edit: qsTr("Edit"), object: qsTr("Object"), select: qsTr("Select"),
        view: qsTr("View"), window: qsTr("Window"), tools: qsTr("Tools"),
        paint: qsTr("Fill and Stroke")
    })
    readonly property var names: ({
        fileNew: qsTr("New"), fileOpen: qsTr("Open"), fileSave: qsTr("Save"),
        fileSaveAs: qsTr("Save As"), fileExportSvg: qsTr("Export as SVG"),
        fileExportPng: qsTr("Export as PNG"), fileClose: qsTr("Close"), fileQuit: qsTr("Exit"),
        editUndo: qsTr("Undo"), editRedo: qsTr("Redo"), editClear: qsTr("Clear"),
        editPreferences: qsTr("Preferences"), editShortcuts: qsTr("Keyboard Shortcuts"),
        objectBringToFront: qsTr("Bring to Front"), objectBringForward: qsTr("Bring Forward"),
        objectSendBackward: qsTr("Send Backward"), objectSendToBack: qsTr("Send to Back"),
        objectGroup: qsTr("Group"), objectUngroup: qsTr("Ungroup"), objectJoin: qsTr("Join"),
        objectCompoundMake: qsTr("Make Compound Path"), objectCompoundRelease: qsTr("Release Compound Path"),
        windowPathfinder: qsTr("Pathfinder"), windowGradient: qsTr("Gradient"), windowTransparency: qsTr("Transparency"), objectClipMake: qsTr("Make Clipping Mask"), objectClipRelease: qsTr("Release Clipping Mask"), toolGradient: qsTr("Gradient Tool"), windowArtboards: qsTr("Artboards"),
        viewFitAll: qsTr("Fit All in Window"), toolArtboard: qsTr("Artboard Tool"), windowAlign: qsTr("Align"), objectAverage: qsTr("Average"),
        selectAll: qsTr("All"), selectDeselect: qsTr("Deselect"),
        viewOutline: qsTr("Outline"), viewZoomIn: qsTr("Zoom In"), viewZoomOut: qsTr("Zoom Out"),
        viewFitArtboard: qsTr("Fit Artboard in Window"), viewActualSize: qsTr("Actual Size"),
        viewSmartGuides: qsTr("Smart Guides"),
        toolSelection: qsTr("Selection Tool"), toolDirectSelection: qsTr("Direct Selection Tool"),
        toolPen: qsTr("Pen Tool"), toolAddAnchor: qsTr("Add Anchor Point Tool"),
        toolDeleteAnchor: qsTr("Delete Anchor Point Tool"), toolAnchorPoint: qsTr("Anchor Point Tool"),
        toolLine: qsTr("Line Segment Tool"), toolRectangle: qsTr("Rectangle Tool"),
        toolEllipse: qsTr("Ellipse Tool"), toolPolygon: qsTr("Polygon Tool"), toolStar: qsTr("Star Tool"),
        toolEyedropper: qsTr("Eyedropper Tool"), toolScissors: qsTr("Scissors Tool"), toolHand: qsTr("Hand Tool"), toolZoom: qsTr("Zoom Tool"),
        paintToggle: qsTr("Toggle Fill and Stroke"), paintSwap: qsTr("Swap Fill and Stroke"),
        paintDefault: qsTr("Default Fill and Stroke"), paintNone: qsTr("None")
    })

    ColumnLayout {
        anchors { fill: parent; margins: 16 }
        spacing: 10

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Shortcuts.commands
            section.property: "group"
            section.delegate: SpLabel {
                required property string section
                text: root.groups[section] ?? section
                heading: true
                topPadding: 10
                bottomPadding: 4
            }
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: command
                required property var modelData
                width: ListView.view.width - 12
                height: 30
                SpLabel {
                    Layout.fillWidth: true
                    text: root.names[command.modelData.id] ?? command.modelData.id
                    subdued: false
                }
                Rectangle {
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: Spectrum.componentHeight75
                    radius: Spectrum.cornerRadiusSmallDefault
                    color: Spectrum.gray25
                    border.width: keyField.activeFocus ? 2 : 1
                    border.color: keyField.activeFocus ? Spectrum.focusIndicatorColor : Spectrum.gray400
                    SpLabel {
                        anchors { fill: parent; leftMargin: 6 }
                        subdued: false
                        text: keyField.activeFocus ? qsTr("Press keys...") : command.modelData.sequences.join(", ")
                    }
                    Item {
                        id: keyField
                        anchors.fill: parent
                        Keys.onPressed: event => {
                            event.accepted = true;
                            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                                Shortcuts.set(command.modelData.id, []);
                                focus = false;
                                return;
                            }
                            if (event.key === Qt.Key_Escape) {
                                focus = false;
                                return;
                            }
                            const sequence = Shortcuts.sequenceFor(event.key, event.modifiers);
                            if (sequence === "")
                                return;
                            const conflicts = Shortcuts.set(command.modelData.id, [sequence]);
                            root.message = conflicts.length > 0
                                ? qsTr("%1 is also used by: %2").arg(sequence).arg(conflicts.map(c => root.names[c] ?? c).join(", "))
                                : "";
                            focus = false;
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: keyField.forceActiveFocus() }
                }
            }
        }

        SpLabel {
            Layout.fillWidth: true
            visible: root.message !== ""
            text: root.message
            color: "#e34850"
            wrapMode: Text.WordWrap
        }
        RowLayout {
            SpActionButton { quiet: false; text: qsTr("Reset All"); onClicked: { Shortcuts.resetAll(); root.message = ""; } }
            SpActionButton { quiet: false; text: qsTr("Export..."); onClicked: exportDialog.open() }
            SpActionButton { quiet: false; text: qsTr("Import..."); onClicked: importDialog.open() }
            Item { Layout.fillWidth: true }
            SpActionButton { quiet: false; text: qsTr("Close"); onClicked: root.close() }
        }
    }

    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("Shortcut sets (*.json)")]
        onAccepted: Shortcuts.exportSet(selectedFile)
    }
    FileDialog {
        id: importDialog
        nameFilters: [qsTr("Shortcut sets (*.json)")]
        onAccepted: if (!Shortcuts.importSet(selectedFile)) root.message = qsTr("This file is not a shortcut set.")
    }
}
