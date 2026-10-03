// SPDX-License-Identifier: GPL-3.0-or-later
// The settings of a book cover (spec 7.5): trim size, page count and paper
// thickness (the spine follows), or the spine given directly, and the
// bleed. Used by the welcome screen and by File > Cover Setup.
import QtQuick
import QtQuick.Layouts
import Leinwand

GridLayout {
    id: root
    readonly property real mm: 72 / 25.4
    readonly property var sizes: [
        { name: qsTr("A5"), w: 148, h: 210 }, { name: qsTr("B5"), w: 182, h: 257 },
        { name: qsTr("A4"), w: 210, h: 297 }, { name: qsTr("B6"), w: 128, h: 182 },
        { name: qsTr("Bunko (A6)"), w: 105, h: 148 }, { name: qsTr("Shinsho"), w: 103, h: 182 },
        { name: qsTr("Shiroku-ban"), w: 127, h: 188 }
    ]
    property alias pages: pagesField.value
    property alias thickness: thicknessField.value
    property alias bleed: bleedField.value
    property bool spineAuto: true
    readonly property real spineMm: spineAuto ? Math.max(0, pages) / 2 * thickness : spineField.value

    // Fills the form from Session.cover() (points).
    function load(cover) {
        if (!cover || cover.width === undefined)
            return;
        const w = cover.width / mm, h = cover.height / mm;
        let index = sizes.findIndex(s => Math.abs(s.w - w) < 0.5 && Math.abs(s.h - h) < 0.5);
        sizePicker.currentIndex = Math.max(0, index);
        pagesField.value = cover.pages;
        thicknessField.value = Math.round(cover.thickness / mm * 1000) / 1000;
        bleedField.value = Math.round(cover.bleed / mm * 100) / 100;
        spineAuto = cover.spine === undefined || cover.spine === null;
        if (!spineAuto)
            spineField.value = cover.spine / mm;
    }
    // The values for Session, in points (NaN spine: worked out).
    function values() {
        const s = sizes[sizePicker.currentIndex];
        return { width: s.w * mm, height: s.h * mm, pages: Math.round(pages),
                 thickness: thickness * mm, spine: spineAuto ? NaN : spineField.value * mm,
                 bleed: bleed * mm };
    }

    columns: 2
    columnSpacing: 8
    rowSpacing: 6

    SpLabel { text: qsTr("Size") }
    SpPicker {
        id: sizePicker
        Layout.preferredWidth: 160
        model: root.sizes.map(s => s.name + " (" + s.w + " × " + s.h + " mm)")
    }
    SpLabel { text: qsTr("Pages") }
    SpNumberField { id: pagesField; unit: ""; decimals: 0; minimum: 4; maximum: 2000; value: 36
                    Layout.preferredWidth: 90; onCommitted: v => value = v }
    SpLabel { text: qsTr("Paper thickness") }
    RowLayout {
        SpNumberField { id: thicknessField; unit: ""; decimals: 3; minimum: 0.01; maximum: 2; value: 0.1
                        Layout.preferredWidth: 90; onCommitted: v => value = v }
        SpLabel { text: qsTr("mm per sheet") }
    }
    SpLabel { text: qsTr("Spine width") }
    RowLayout {
        SpCheckBox { text: qsTr("From pages"); checked: root.spineAuto; onClicked: root.spineAuto = checked }
        SpNumberField { id: spineField; unit: ""; decimals: 2; minimum: 0; maximum: 200; value: 2
                        visible: !root.spineAuto; Layout.preferredWidth: 90; onCommitted: v => value = v }
        SpLabel { visible: root.spineAuto; text: root.spineMm.toFixed(2) + " mm" }
    }
    SpLabel { text: qsTr("Bleed") }
    RowLayout {
        SpNumberField { id: bleedField; unit: ""; decimals: 2; minimum: 0; maximum: 20; value: 3
                        Layout.preferredWidth: 90; onCommitted: v => value = v }
        SpLabel { text: "mm" }
    }
}
