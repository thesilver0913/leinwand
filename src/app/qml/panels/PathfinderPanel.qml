// SPDX-License-Identifier: GPL-3.0-or-later
// Pathfinder panel (spec 7.2, 4.3): the four shape modes and six
// pathfinders on the selected objects. Numbers follow geometry::Pathfinder.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property bool usable: Session.selectionCount >= 2

    component Buttons: Row {
        id: row
        property var entries: []
        spacing: 2
        Repeater {
            model: row.entries
            SpActionButton {
                required property var modelData
                implicitWidth: 32
                implicitHeight: 32
                iconSize: 20
                iconName: modelData.icon
                tip: modelData.tip
                enabled: root.usable
                onClicked: Session.pathfinder(modelData.op)
            }
        }
    }

    ColumnLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 6

        SpLabel { text: qsTr("Shape Modes:") }
        Buttons {
            entries: [
                { op: 0, icon: "PathfinderUnite", tip: qsTr("Unite") },
                { op: 1, icon: "PathfinderMinusFront", tip: qsTr("Minus Front") },
                { op: 2, icon: "PathfinderIntersect", tip: qsTr("Intersect") },
                { op: 3, icon: "PathfinderExclude", tip: qsTr("Exclude") }
            ]
        }

        SpLabel { text: qsTr("Pathfinders:"); Layout.topMargin: 4 }
        Buttons {
            entries: [
                { op: 4, icon: "PathfinderDivide", tip: qsTr("Divide") },
                { op: 5, icon: "PathfinderTrim", tip: qsTr("Trim") },
                { op: 6, icon: "PathfinderMerge", tip: qsTr("Merge") },
                { op: 7, icon: "PathfinderCrop", tip: qsTr("Crop") },
                { op: 8, icon: "PathfinderOutline", tip: qsTr("Outline") },
                { op: 9, icon: "PathfinderMinusBack", tip: qsTr("Minus Back") }
            ]
        }
    }
}
