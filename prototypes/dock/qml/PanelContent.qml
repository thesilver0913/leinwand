// SPDX-License-Identifier: GPL-3.0-or-later
// Placeholder panel body: a list of labelled rows.
import QtQuick

Rectangle {
    id: root
    property var rows: []

    anchors.fill: parent
    color: Spectrum.backgroundLayer2

    Column {
        anchors { fill: parent; margins: 12 }
        spacing: 4

        Repeater {
            model: root.rows
            Rectangle {
                required property string modelData
                width: parent.width
                height: Spectrum.componentHeight75
                radius: Spectrum.cornerRadiusSmall
                color: rowArea.containsMouse ? Spectrum.hoverOverlay : "transparent"

                Text {
                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    text: parent.modelData
                    color: Spectrum.content
                    font { family: Spectrum.fontFamily; pixelSize: Spectrum.fontSize75 }
                }
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }
        }
    }
}
