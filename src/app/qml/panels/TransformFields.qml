// SPDX-License-Identifier: GPL-3.0-or-later
// Position, size and angle of the selection, and a live shape's parameters
// (spec 4.1, 7.2). Shared by the Transform and Properties panels. The
// reference point is the top left until the reference point selector comes.
import QtQuick
import QtQuick.Layouts
import Leinwand

GridLayout {
    id: root
    readonly property var info: Session.selectionInfo
    readonly property string shape: info.shape ?? ""
    columns: 4
    columnSpacing: 6
    rowSpacing: 6
    enabled: info.valid

    SpLabel { text: "X" }
    SpNumberField {
        Layout.fillWidth: true
        value: root.info.x ?? 0
        onCommitted: v => Session.setBounds(v, root.info.y, root.info.width, root.info.height)
    }
    SpLabel { text: qsTr("W") }
    SpNumberField {
        Layout.fillWidth: true
        value: root.info.width ?? 0
        minimum: 0.001
        onCommitted: v => Session.setBounds(root.info.x, root.info.y, v, root.info.height)
    }
    SpLabel { text: "Y" }
    SpNumberField {
        Layout.fillWidth: true
        value: root.info.y ?? 0
        onCommitted: v => Session.setBounds(root.info.x, v, root.info.width, root.info.height)
    }
    SpLabel { text: qsTr("H") }
    SpNumberField {
        Layout.fillWidth: true
        value: root.info.height ?? 0
        minimum: 0.001
        onCommitted: v => Session.setBounds(root.info.x, root.info.y, root.info.width, v)
    }
    SpIcon { name: "RotateCCW"; size: 14 }
    SpNumberField {
        Layout.fillWidth: true
        value: root.info.rotation ?? 0
        unit: "°"
        onCommitted: v => Session.setRotation(v)
    }
    Item { Layout.columnSpan: 2 }

    // Live shape properties.
    SpLabel {
        Layout.columnSpan: 4
        Layout.topMargin: 6
        visible: root.shape !== ""
        heading: true
        text: ({ rectangle: qsTr("Rectangle Properties"), ellipse: qsTr("Ellipse Properties"),
                 polygon: qsTr("Polygon Properties"), star: qsTr("Star Properties"),
                 line: qsTr("Line Properties") })[root.shape] ?? ""
    }
    Repeater {
        model: ({
            rectangle: [["shapeWidth", "width", qsTr("W"), "pt"], ["shapeHeight", "height", qsTr("H"), "pt"],
                        ["cornerRadius", "cornerRadius", qsTr("Corner"), "pt"]],
            ellipse: [["shapeWidth", "width", qsTr("W"), "pt"], ["shapeHeight", "height", qsTr("H"), "pt"],
                      ["pieStart", "pieStart", qsTr("Pie start"), "°"], ["pieEnd", "pieEnd", qsTr("Pie end"), "°"]],
            polygon: [["sides", "sides", qsTr("Sides"), ""], ["radius", "radius", qsTr("Radius"), "pt"],
                      ["polygonCornerRadius", "polygonCornerRadius", qsTr("Corner"), "pt"]],
            star: [["points", "points", qsTr("Points"), ""], ["outerRadius", "outerRadius", qsTr("Radius 1"), "pt"],
                   ["innerRadius", "innerRadius", qsTr("Radius 2"), "pt"]],
            line: [["length", "length", qsTr("Length"), "pt"]]
        })[root.shape] ?? []
        delegate: RowLayout {
            required property var modelData
            Layout.columnSpan: 2
            Layout.fillWidth: true
            spacing: 6
            SpLabel {
                text: modelData[2]
                Layout.preferredWidth: 44
            }
            SpNumberField {
                Layout.fillWidth: true
                value: root.info[modelData[0]] ?? 0
                unit: modelData[3]
                decimals: modelData[3] === "" ? 0 : 2
                onCommitted: v => Session.setShapeValue(modelData[1], v)
            }
        }
    }
    SpLabel {
        visible: root.shape === "rectangle"
        text: qsTr("Corner type")
    }
    SpPicker {
        Layout.columnSpan: 3
        Layout.fillWidth: true
        visible: root.shape === "rectangle"
        model: [qsTr("Round"), qsTr("Inverted Round"), qsTr("Chamfer")]
        currentIndex: root.info.cornerKind ?? 0
        onActivated: index => Session.setShapeValue("cornerKind", index)
    }
}
