// SPDX-License-Identifier: GPL-3.0-or-later
// A color chip: a color, "none" (white with a red slash), or "mixed" (?).
import QtQuick
import Leinwand

Rectangle {
    id: root
    property color swatchColor: "white"
    property bool none: false
    property bool mixed: false
    // A stroke chip: a frame in the color around a hole.
    property bool stroke: false
    property bool selected: false

    implicitWidth: 20
    implicitHeight: 20
    radius: 2
    color: stroke || none || mixed ? Spectrum.gray25 : swatchColor
    border.width: selected ? 2 : 1
    border.color: selected ? Spectrum.neutralContentColorDefault : Spectrum.gray500
    clip: true

    Rectangle {
        visible: root.stroke && !root.none && !root.mixed
        anchors.fill: parent
        anchors.margins: 1
        color: "transparent"
        border.width: Math.max(3, root.width / 5)
        border.color: root.swatchColor
    }
    Rectangle {  // None: a red diagonal.
        visible: root.none
        anchors.centerIn: parent
        width: parent.width * 1.3
        height: 2
        rotation: -45
        color: "#e34850"
    }
    SpLabel {
        visible: root.mixed
        anchors.centerIn: parent
        text: "?"
        subdued: false
    }
}
