// SPDX-License-Identifier: GPL-3.0-or-later
// The control bar (spec 7.1): the main settings for the selection and the
// tool: fill, stroke, weight, opacity, anchor commands with direct selection,
// and position and size.
import QtQuick
import QtQuick.Layouts
import Leinwand

Rectangle {
    id: root
    readonly property var style: Session.style
    readonly property var info: Session.selectionInfo
    readonly property int count: Session.selectionCount
    implicitHeight: 36
    color: Spectrum.backgroundLayer1Color

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Spectrum.gray300
    }

    RowLayout {
        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
        spacing: 8

        SpLabel {
            text: Session.anchorCount > 0 ? qsTr("Anchor Point")
                : root.count === 0 ? qsTr("No Selection")
                : root.count === 1 ? (({ rectangle: qsTr("Rectangle"), ellipse: qsTr("Ellipse"),
                                         polygon: qsTr("Polygon"), star: qsTr("Star"), line: qsTr("Line")
                                       })[root.info.shape ?? ""] ?? qsTr("Path"))
                : qsTr("Mixed Objects")
            subdued: false
            Layout.preferredWidth: 96
        }

        SpLabel { text: qsTr("Fill") }
        SpSwatch {
            swatchColor: root.style.fill ?? "white"
            none: root.style.fillNone ?? false
            mixed: root.style.fillMixed ?? false
            selected: Session.fillActive
            MouseArea { anchors.fill: parent; onClicked: Session.fillActive = true }
        }
        SpLabel { text: qsTr("Stroke") }
        SpSwatch {
            stroke: true
            swatchColor: root.style.stroke ?? "black"
            none: root.style.strokeNone ?? false
            mixed: root.style.strokeMixed ?? false
            selected: !Session.fillActive
            MouseArea { anchors.fill: parent; onClicked: Session.fillActive = false }
        }
        SpNumberField {
            Layout.preferredWidth: 64
            enabled: root.style.hasStroke ?? false
            value: root.style.strokeWidth ?? 0
            minimum: 0
            onCommitted: v => Session.setStrokeValue("width", v)
        }

        Rectangle { implicitWidth: 1; implicitHeight: 20; color: Spectrum.gray300 }

        SpLabel { text: qsTr("Opacity") }
        SpNumberField {
            Layout.preferredWidth: 56
            enabled: root.count > 0
            value: Math.round((root.style.opacity ?? 1) * 100)
            mixed: root.style.opacityMixed ?? false
            unit: "%"
            decimals: 0
            minimum: 0
            maximum: 100
            onCommitted: v => Session.setOpacity(v / 100)
        }

        // Anchor commands (spec 4.2).
        RowLayout {
            visible: Session.anchorCount > 0
            spacing: 2
            Rectangle { implicitWidth: 1; implicitHeight: 20; color: Spectrum.gray300 }
            SpLabel { text: qsTr("Convert:"); leftPadding: 6 }
            SpActionButton { text: qsTr("Corner"); onClicked: Session.convertAnchors(false) }
            SpActionButton { text: qsTr("Smooth"); onClicked: Session.convertAnchors(true) }
            SpActionButton { iconName: "Delete"; iconSize: 16; tip: qsTr("Remove Selected Anchor Points"); onClicked: Session.removeAnchors() }
            SpActionButton { iconName: "Cut"; iconSize: 16; tip: qsTr("Cut Path at Selected Anchor Points"); enabled: Session.anchorCount === 1; onClicked: Session.cutAtAnchor() }
            SpActionButton { iconName: "Link"; iconSize: 16; tip: qsTr("Connect Selected End Points (Ctrl+J)"); enabled: Session.anchorCount === 2; onClicked: Session.joinEnds() }
        }

        Item { Layout.fillWidth: true }

        RowLayout {
            visible: root.info.valid ?? false
            spacing: 4
            SpLabel { text: "X" }
            SpNumberField {
                Layout.preferredWidth: 72
                value: root.info.x ?? 0
                onCommitted: v => Session.setBounds(v, root.info.y, root.info.width, root.info.height)
            }
            SpLabel { text: "Y" }
            SpNumberField {
                Layout.preferredWidth: 72
                value: root.info.y ?? 0
                onCommitted: v => Session.setBounds(root.info.x, v, root.info.width, root.info.height)
            }
            SpLabel { text: qsTr("W") }
            SpNumberField {
                Layout.preferredWidth: 72
                value: root.info.width ?? 0
                minimum: 0.001
                onCommitted: v => Session.setBounds(root.info.x, root.info.y, v, root.info.height)
            }
            SpLabel { text: qsTr("H") }
            SpNumberField {
                Layout.preferredWidth: 72
                value: root.info.height ?? 0
                minimum: 0.001
                onCommitted: v => Session.setBounds(root.info.x, root.info.y, root.info.width, v)
            }
        }
    }
}
