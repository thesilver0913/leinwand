// SPDX-License-Identifier: GPL-3.0-or-later
// Preferences (spec 7.3): one window, categories on the left. Changes apply
// at once and are saved; "Reset Preferences" restores the defaults. The
// categories for text, units, guides and grids, and fonts arrive with those
// features.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Leinwand

Window {
    id: root
    width: 720
    height: 480
    minimumWidth: 660
    minimumHeight: 400
    title: qsTr("Preferences")
    color: Spectrum.backgroundLayer2Color
    property int category: 0

    readonly property var categories: [
        qsTr("General"), qsTr("Selection & Anchor Display"), qsTr("Smart Guides"),
        qsTr("User Interface"), qsTr("Performance"), qsTr("File Handling")
    ]

    // A labelled row. Long labels wrap rather than being cut off.
    component Row2: RowLayout {
        property alias label: label.text
        default property alias content: holder.data
        spacing: 12
        Layout.fillWidth: true
        SpLabel {
            id: label
            Layout.preferredWidth: 200
            Layout.alignment: Qt.AlignVCenter
            subdued: false
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
        RowLayout { id: holder; spacing: 6; Layout.fillWidth: true }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 180
            color: Spectrum.backgroundLayer1Color
            ListView {
                anchors { fill: parent; margins: 8 }
                model: root.categories
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: ListView.view.width
                    height: 30
                    radius: Spectrum.cornerRadiusSmallDefault
                    color: root.category === index ? Spectrum.gray300
                         : area.containsMouse ? Spectrum.hoverOverlay : "transparent"
                    SpLabel {
                        anchors { fill: parent; leftMargin: 10 }
                        text: modelData
                        subdued: root.category !== index
                    }
                    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.category = index }
                }
            }
        }

        ColumnLayout {
            // Takes the width there is; the pages' own widths do not push
            // the window's contents (and the buttons) out of sight.
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Layout.margins: 20
            spacing: 12

            SpLabel { text: root.categories[root.category]; heading: true; font.pixelSize: Spectrum.fontSize100 }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.category

                ColumnLayout {  // General.
                    spacing: 10
                    Row2 {
                        label: qsTr("Keyboard increment")
                        SpNumberField {
                            Layout.preferredWidth: 90
                            value: Preferences.keyboardIncrement
                            minimum: 0.001
                            onCommitted: v => Preferences.keyboardIncrement = v
                        }
                    }
                    Row2 {
                        label: qsTr("Pen rubber band")
                        SpCheckBox { checked: Preferences.rubberBand; onClicked: Preferences.rubberBand = checked }
                    }
                    Row2 {
                        label: qsTr("Show the welcome screen at startup")
                        SpCheckBox { checked: Preferences.showWelcome; onClicked: Preferences.showWelcome = checked }
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {  // Selection & anchor display.
                    spacing: 10
                    Row2 {
                        label: qsTr("Selection tolerance")
                        SpNumberField {
                            Layout.preferredWidth: 90
                            value: Preferences.pickTolerance
                            unit: " px"
                            minimum: 1
                            maximum: 20
                            onCommitted: v => Preferences.pickTolerance = v
                        }
                    }
                    Row2 {
                        label: qsTr("Anchor size")
                        SpNumberField {
                            Layout.preferredWidth: 90
                            value: Preferences.anchorSize
                            unit: " px"
                            minimum: 3
                            maximum: 16
                            onCommitted: v => Preferences.anchorSize = v
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {  // Smart guides.
                    spacing: 10
                    Row2 {
                        label: qsTr("Smart guides")
                        SpCheckBox { checked: Session.smartGuides; onClicked: Session.smartGuides = checked }
                    }
                    Row2 {
                        label: qsTr("Snapping tolerance")
                        SpNumberField {
                            Layout.preferredWidth: 90
                            value: Preferences.snapTolerance
                            unit: " px"
                            minimum: 1
                            maximum: 20
                            onCommitted: v => Preferences.snapTolerance = v
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {  // User interface.
                    spacing: 10
                    Row2 {
                        label: qsTr("Theme")
                        SpPicker {
                            Layout.preferredWidth: 180
                            readonly property var values: ["dark", "light", "system"]
                            model: [qsTr("Dark"), qsTr("Light"), qsTr("Match the system")]
                            currentIndex: Math.max(0, values.indexOf(Preferences.theme))
                            onActivated: index => Preferences.theme = values[index]
                        }
                    }
                    Row2 {
                        label: qsTr("Language")
                        SpPicker {
                            Layout.preferredWidth: 180
                            readonly property var values: ["system", "ja", "en"]
                            model: [qsTr("Match the system"), "日本語", "English"]
                            currentIndex: Math.max(0, values.indexOf(Preferences.language))
                            onActivated: index => Preferences.language = values[index]
                        }
                    }
                    Row2 {
                        label: qsTr("UI scaling")
                        SpNumberField {
                            Layout.preferredWidth: 90
                            value: Preferences.uiScale
                            unit: "%"
                            decimals: 0
                            minimum: 50
                            maximum: 300
                            onCommitted: v => Preferences.uiScale = v
                        }
                        SpLabel { text: qsTr("(after a restart)") }
                    }
                    Row2 {
                        label: qsTr("Canvas color")
                        SpTextField {
                            Layout.preferredWidth: 90
                            text: Preferences.canvasColor
                            onEditingFinished: Preferences.canvasColor = text
                        }
                        SpSwatch { swatchColor: Preferences.canvasColor }
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {  // Performance.
                    spacing: 10
                    Row2 {
                        label: qsTr("Undo levels (0: unlimited)")
                        SpNumberField {
                            Layout.preferredWidth: 90
                            value: Preferences.undoLimit
                            unit: ""
                            decimals: 0
                            minimum: 0
                            maximum: 10000
                            onCommitted: v => Preferences.undoLimit = v
                        }
                    }
                    SpLabel {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        text: qsTr("Drawing always uses the GPU (Vulkan, or Metal on macOS) in this version.")
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {  // File handling.
                    spacing: 10
                    Row2 {
                        label: qsTr("Autosave every (0: off)")
                        SpNumberField {
                            Layout.preferredWidth: 90
                            value: Preferences.autosaveMinutes
                            unit: qsTr(" min")
                            decimals: 0
                            minimum: 0
                            maximum: 120
                            onCommitted: v => Preferences.autosaveMinutes = v
                        }
                    }
                    Row2 {
                        label: qsTr("Recovery data folder")
                        SpTextField {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 120
                            text: Preferences.recoveryFolder
                            placeholderText: qsTr("Default")
                            onEditingFinished: Preferences.recoveryFolder = text
                        }
                        SpActionButton { quiet: false; text: qsTr("Choose..."); onClicked: folderDialog.open() }
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            RowLayout {
                SpActionButton { quiet: false; text: qsTr("Reset Preferences"); onClicked: Preferences.reset() }
                Item { Layout.fillWidth: true }
                SpActionButton { quiet: false; text: qsTr("Close"); onClicked: root.close() }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        onAccepted: Preferences.recoveryFolder = selectedFolder.toString().replace(/^file:\/{2,3}/, "")
    }
}
