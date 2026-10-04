// SPDX-License-Identifier: GPL-3.0-or-later
// Print (spec 7.4): printer, paper, range, scaling, position on the paper,
// trim marks and copies, with a preview of the first sheet on the left.
// Printer-specific settings open the OS's own dialog. Tiling comes later.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Leinwand

Dialog {
    id: root
    title: qsTr("Print")
    modal: true
    anchors.centerIn: parent
    standardButtons: Dialog.Cancel
    property var preview: ({})
    property var printers: []

    readonly property var papers: ["printer", "A4", "A3", "B4", "B5", "Letter", "Legal"]
    function settings() {
        return {
            printer: printer.currentIndex >= 0 ? root.printers[printer.currentIndex] : "",
            paper: root.papers[paper.currentIndex],
            orientation: orientation.current,
            range: range.currentIndex,
            scaling: scaling.currentIndex,
            percent: percent.value,
            position: root.position,
            marks: marks.currentIndex,
            copies: Math.round(copies.value),
            collate: collate.checked
        };
    }
    property int position: 4
    function refresh() { preview = Session.printPreview(settings()); }

    onAboutToShow: {
        printers = Session.printers();
        printer.currentIndex = Math.max(0, printers.indexOf(Session.defaultPrinter()));
        refresh();
    }

    RowLayout {
        spacing: 20

        // The sheet with the artwork on it.
        Rectangle {
            id: sheetArea
            Layout.preferredWidth: 260
            Layout.preferredHeight: 260
            color: Spectrum.gray200
            readonly property real fit: root.preview.paperWidth > 0
                ? Math.min((width - 20) / root.preview.paperWidth, (height - 20) / root.preview.paperHeight) : 1
            Rectangle {
                id: paperRect
                anchors.centerIn: parent
                width: (root.preview.paperWidth ?? 0) * sheetArea.fit
                height: (root.preview.paperHeight ?? 0) * sheetArea.fit
                color: "white"
                border.color: Spectrum.gray500
                clip: true
                Image {
                    x: (root.preview.x ?? 0) * sheetArea.fit
                    y: (root.preview.y ?? 0) * sheetArea.fit
                    width: (root.preview.width ?? 0) * sheetArea.fit
                    height: (root.preview.height ?? 0) * sheetArea.fit
                    source: root.preview.image ?? ""
                    smooth: true
                }
            }
            SpLabel {
                anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 2 }
                text: qsTr("%n page(s)", "", root.preview.pages ?? 0)
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: 10
            rowSpacing: 8

            SpLabel { text: qsTr("Printer") }
            RowLayout {
                SpPicker {
                    id: printer
                    Layout.preferredWidth: 220
                    model: root.printers
                    onActivated: root.refresh()
                }
                SpActionButton {
                    quiet: false
                    text: qsTr("Setup...")
                    tip: qsTr("The printer's own settings")
                    onClicked: {
                        Session.printerSetup(root.printers[printer.currentIndex] ?? "");
                        root.refresh();
                    }
                }
            }

            SpLabel { text: qsTr("Paper") }
            SpPicker {
                id: paper
                Layout.preferredWidth: 220
                model: [qsTr("Printer's setting"), "A4", "A3", qsTr("B4 (JIS)"), qsTr("B5 (JIS)"), "Letter", "Legal"]
                currentIndex: 0
                onActivated: root.refresh()
            }

            SpLabel { text: qsTr("Orientation") }
            SpSegmented {
                id: orientation
                current: 0
                options: [{ text: qsTr("Auto") }, { text: qsTr("Portrait") }, { text: qsTr("Landscape") }]
                onChosen: i => { current = i; root.refresh(); }
            }

            SpLabel { text: qsTr("Range") }
            SpPicker {
                id: range
                Layout.preferredWidth: 220
                model: [qsTr("All artboards"), qsTr("Active artboard"), qsTr("Ignore artboards")]
                currentIndex: 0
                onActivated: root.refresh()
            }

            SpLabel { text: qsTr("Scaling") }
            RowLayout {
                SpPicker {
                    id: scaling
                    Layout.preferredWidth: 150
                    model: [qsTr("Actual size"), qsTr("Fit to paper"), qsTr("Custom")]
                    currentIndex: 0
                    onActivated: root.refresh()
                }
                SpNumberField {
                    id: percent
                    Layout.preferredWidth: 70
                    visible: scaling.currentIndex === 2
                    value: 100
                    unit: "%"
                    decimals: 0
                    minimum: 1
                    maximum: 1000
                    onCommitted: v => { value = v; root.refresh(); }
                }
            }

            SpLabel { text: qsTr("Position") }
            Grid {
                columns: 3
                spacing: 2
                Repeater {
                    model: 9
                    SpActionButton {
                        required property int index
                        implicitWidth: 18
                        implicitHeight: 18
                        quiet: false
                        checkable: true
                        checked: root.position === index
                        tip: qsTr("Place on the paper")
                        onClicked: { root.position = index; root.refresh(); }
                    }
                }
            }

            SpLabel { text: qsTr("Marks and Bleed") }
            SpPicker {
                id: marks
                Layout.preferredWidth: 220
                model: [qsTr("None"), qsTr("Japanese trim marks"), qsTr("Western trim marks")]
                currentIndex: 0
                onActivated: root.refresh()
            }

            SpLabel { text: qsTr("Copies") }
            RowLayout {
                SpNumberField {
                    id: copies
                    Layout.preferredWidth: 70
                    value: 1
                    unit: ""
                    decimals: 0
                    minimum: 1
                    maximum: 999
                    onCommitted: v => value = v
                }
                SpCheckBox {
                    id: collate
                    text: qsTr("Collate")
                    checked: true
                }
            }

            Item { implicitHeight: 1 }
            SpActionButton {
                Layout.alignment: Qt.AlignRight
                quiet: false
                text: qsTr("Print")
                enabled: root.printers.length > 0 && (root.preview.pages ?? 0) > 0
                onClicked: {
                    if (Session.print(root.settings()))
                        root.close();
                }
            }
        }
    }
}
