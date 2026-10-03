// SPDX-License-Identifier: GPL-3.0-or-later
// Object > Path > Average (Alt+Ctrl+J): moves the selected anchors to their
// mean position along one axis or both.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Leinwand

Dialog {
    id: root
    property int axis: 2  // 0 horizontal, 1 vertical, 2 both.

    title: qsTr("Average")
    modal: true
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.Ok | Dialog.Cancel
    background: Rectangle {
        color: Spectrum.backgroundLayer2Color
        radius: Spectrum.cornerRadiusMediumDefault
        border.width: 1
        border.color: Spectrum.gray300
    }

    ColumnLayout {
        spacing: 8
        SpLabel { text: qsTr("Axis") }
        SpSegmented {
            current: root.axis
            options: [{ text: qsTr("Horizontal"), tip: qsTr("Onto one horizontal line") },
                      { text: qsTr("Vertical"), tip: qsTr("Onto one vertical line") },
                      { text: qsTr("Both"), tip: qsTr("Onto one point") }]
            onChosen: i => root.axis = i
        }
    }
    onAccepted: Session.average(axis)
}
