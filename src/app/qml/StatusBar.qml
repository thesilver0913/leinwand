// SPDX-License-Identifier: GPL-3.0-or-later
// The status bar (spec 7.1): zoom, the current tool, the pointer position,
// and until the preferences arrive (M7), the theme switch.
import QtQuick
import QtQuick.Layouts
import Leinwand

Rectangle {
    id: root
    property var canvas  // The CanvasItem, once the dock layout made it.
    implicitHeight: 24
    color: Spectrum.backgroundLayer1Color

    readonly property var toolNames: [
        qsTr("Selection"), qsTr("Rectangle"), qsTr("Ellipse"), qsTr("Polygon"), qsTr("Star"),
        qsTr("Line Segment"), qsTr("Pen"), qsTr("Add Anchor Point"), qsTr("Delete Anchor Point"),
        qsTr("Anchor Point"), qsTr("Direct Selection"), qsTr("Eyedropper"), qsTr("Hand"), qsTr("Zoom")
    ]

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: Spectrum.gray300
    }

    RowLayout {
        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
        spacing: 16

        SpLabel {
            text: root.canvas ? (root.canvas.zoom * 100).toFixed(2) + "%" : ""
            Layout.preferredWidth: 64
        }
        SpLabel {
            text: root.toolNames[Session.tool] ?? ""
            Layout.preferredWidth: 120
        }
        SpLabel {
            text: root.canvas ? "X %1  Y %2".arg(root.canvas.pointer.x.toFixed(1))
                                                .arg(root.canvas.pointer.y.toFixed(1)) : ""
            Layout.preferredWidth: 140
        }
        SpLabel {
            Layout.fillWidth: true
            text: root.canvas && root.canvas.error !== ""
                  ? qsTr("Canvas error: %1").arg(root.canvas.error)
                  : qsTr("%1 objects, %2 selected").arg(Session.objectCount).arg(Session.selectionCount)
        }
        SpLabel {
            text: root.canvas && root.canvas.fps > 0 ? qsTr("%1 fps").arg(root.canvas.fps.toFixed(0)) : ""
        }
        SpActionButton {
            implicitHeight: 20
            iconName: "Contrast"
            iconSize: 14
            text: Spectrum.dark ? qsTr("Dark") : qsTr("Light")
            tip: qsTr("Switch between the light and dark themes")
            onClicked: Spectrum.dark = !Spectrum.dark
        }
    }
}
