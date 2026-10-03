// SPDX-License-Identifier: GPL-3.0-or-later
// The welcome screen (spec 9): a window of its own with new document
// presets and a custom size, opening a file, and recent files with
// thumbnails (grid or list).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Leinwand

Window {
    id: root
    // Main.qml: runs an action after asking about unsaved changes.
    property var guard: action => action()
    signal openRequested()

    width: 980
    height: 640
    minimumWidth: 760
    minimumHeight: 520
    title: qsTr("Welcome to Leinwand")
    color: Spectrum.backgroundLayer1Color
    modality: Qt.NonModal

    // Points per unit for the custom size.
    readonly property var units: [
        { name: "mm", points: 72 / 25.4 }, { name: "pt", points: 1 },
        { name: "px", points: 1 }, { name: "in", points: 72 }
    ]
    readonly property var presets: [
        { group: qsTr("Print"), name: qsTr("A4"), w: 210, h: 297, unit: 0, bleed: 3 },
        { group: qsTr("Print"), name: qsTr("A3"), w: 297, h: 420, unit: 0, bleed: 3 },
        { group: qsTr("Print"), name: qsTr("B5"), w: 182, h: 257, unit: 0, bleed: 3 },
        { group: qsTr("Print"), name: qsTr("Postcard"), w: 100, h: 148, unit: 0, bleed: 3 },
        { group: qsTr("Print"), name: qsTr("Business card"), w: 91, h: 55, unit: 0, bleed: 3 },
        { group: qsTr("Web"), name: "1920 × 1080", w: 1920, h: 1080, unit: 2, bleed: 0 },
        { group: qsTr("Web"), name: "1280 × 720", w: 1280, h: 720, unit: 2, bleed: 0 },
        { group: qsTr("Icon"), name: "256 × 256", w: 256, h: 256, unit: 2, bleed: 0 },
        { group: qsTr("Icon"), name: "1024 × 1024", w: 1024, h: 1024, unit: 2, bleed: 0 }
    ]
    property int selectedPreset: 0
    property bool listView: false

    function applyPreset(i) {
        selectedPreset = i;
        const p = presets[i];
        unitPicker.currentIndex = p.unit;
        widthField.value = p.w;
        heightField.value = p.h;
        bleedField.value = p.bleed;
    }
    function create() {
        const k = units[unitPicker.currentIndex].points;
        const w = widthField.value * k, h = heightField.value * k, b = bleedField.value * k;
        guard(() => {
            Session.newDocument(w, h, b);
            root.close();
        });
    }
    Component.onCompleted: applyPreset(0)

    RowLayout {
        anchors { fill: parent; margins: 24 }
        spacing: 24

        // New document.
        ColumnLayout {
            Layout.preferredWidth: 420
            Layout.fillHeight: true
            spacing: 10

            SpLabel { text: qsTr("New Document"); heading: true; font.pixelSize: Spectrum.fontSize100 }
            GridView {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                clip: true
                cellWidth: 136
                cellHeight: 96
                model: root.presets
                delegate: Rectangle {
                    id: preset
                    required property int index
                    required property var modelData
                    width: 128
                    height: 88
                    radius: Spectrum.cornerRadiusMediumDefault
                    color: root.selectedPreset === index ? Spectrum.gray300
                         : presetArea.containsMouse ? Spectrum.gray200 : Spectrum.gray100
                    border.width: root.selectedPreset === index ? 2 : 0
                    border.color: Spectrum.accentBackgroundColorDefault
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Rectangle {  // The page's proportions.
                            anchors.horizontalCenter: parent.horizontalCenter
                            readonly property real ratio: preset.modelData.w / preset.modelData.h
                            width: ratio >= 1 ? 40 : 40 * ratio
                            height: ratio >= 1 ? 40 / ratio : 40
                            color: "white"
                            border.color: Spectrum.gray500
                        }
                        SpLabel { anchors.horizontalCenter: parent.horizontalCenter; text: preset.modelData.name; subdued: false }
                        SpLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: preset.modelData.group + " · " + preset.modelData.w + " × " + preset.modelData.h + " "
                                  + root.units[preset.modelData.unit].name
                            font.pixelSize: Spectrum.fontSize50
                        }
                    }
                    MouseArea {
                        id: presetArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.applyPreset(preset.index)
                        onDoubleClicked: { root.applyPreset(preset.index); root.create(); }
                    }
                }
            }
            GridLayout {
                columns: 4
                columnSpacing: 8
                rowSpacing: 6
                SpLabel { text: qsTr("Width") }
                SpNumberField { id: widthField; unit: ""; decimals: 2; minimum: 1; Layout.preferredWidth: 90; onCommitted: v => value = v }
                SpLabel { text: qsTr("Height") }
                SpNumberField { id: heightField; unit: ""; decimals: 2; minimum: 1; Layout.preferredWidth: 90; onCommitted: v => value = v }
                SpLabel { text: qsTr("Units") }
                SpPicker { id: unitPicker; model: root.units.map(u => u.name); Layout.preferredWidth: 90 }
                SpLabel { text: qsTr("Bleed") }
                SpNumberField { id: bleedField; unit: ""; decimals: 2; minimum: 0; Layout.preferredWidth: 90; onCommitted: v => value = v }
                SpLabel { text: qsTr("Color mode") }
                SpPicker {
                    // Phases 1-3 create RGB documents only (spec 3).
                    model: ["RGB"]
                    enabled: false
                    Layout.preferredWidth: 90
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                SpActionButton { quiet: false; text: qsTr("Open..."); iconName: "FolderOpen"; onClicked: root.openRequested() }
                Item { Layout.fillWidth: true }
                SpCheckBox {
                    text: qsTr("Show at startup")
                    checked: Preferences.showWelcome
                    onClicked: Preferences.showWelcome = checked
                }
                SpActionButton { quiet: false; text: qsTr("Create"); onClicked: root.create() }
            }
        }

        Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Spectrum.gray300 }

        // Recent files.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            RowLayout {
                SpLabel { text: qsTr("Recent Files"); heading: true; font.pixelSize: Spectrum.fontSize100; Layout.fillWidth: true }
                SpSegmented {
                    current: root.listView ? 1 : 0
                    options: [{ text: qsTr("Grid") }, { text: qsTr("List") }]
                    onChosen: i => root.listView = i === 1
                }
            }
            SpLabel {
                visible: (Preferences.recentFiles ?? []).length === 0
                text: qsTr("Files you open or save appear here.")
            }
            GridView {
                id: recent
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Preferences.recentFiles
                cellWidth: root.listView ? width : 150
                cellHeight: root.listView ? 40 : 150
                delegate: Item {
                    id: file
                    required property var modelData
                    readonly property string name: modelData.replace(/^.*[\\/]/, "")
                    width: recent.cellWidth - 8
                    height: recent.cellHeight - 8
                    Rectangle {
                        anchors.fill: parent
                        radius: Spectrum.cornerRadiusSmallDefault
                        color: fileArea.containsMouse ? Spectrum.gray200 : "transparent"
                    }
                    Image {
                        id: thumb
                        x: 6
                        y: 6
                        width: root.listView ? 28 : parent.width - 12
                        height: root.listView ? 28 : parent.height - 32
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        sourceSize: Qt.size(256, 256)
                        source: "image://thumbnail/" + encodeURIComponent(file.modelData)
                    }
                    SpIcon {
                        anchors.centerIn: thumb
                        visible: thumb.status !== Image.Ready
                        name: "Path"
                        size: root.listView ? 18 : 40
                    }
                    SpLabel {
                        x: root.listView ? 44 : 6
                        y: root.listView ? 0 : parent.height - 24
                        height: root.listView ? parent.height : 20
                        width: parent.width - x - 6
                        text: root.listView ? file.modelData : file.name
                        subdued: false
                        horizontalAlignment: root.listView ? Text.AlignLeft : Text.AlignHCenter
                    }
                    MouseArea {
                        id: fileArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.guard(() => {
                            if (Session.openPath(file.modelData))
                                root.close();
                        })
                    }
                }
            }
        }
    }
}
