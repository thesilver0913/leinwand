// SPDX-License-Identifier: GPL-3.0-or-later
// Spectrum checkbox (small).
import QtQuick
import QtQuick.Templates as T
import Leinwand

T.CheckBox {
    id: root
    implicitWidth: indicator.width + (text ? spacing + label.implicitWidth : 0)
    implicitHeight: Spectrum.componentHeight75
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    spacing: 6

    indicator: Rectangle {
        y: (root.height - height) / 2
        width: Spectrum.checkboxControlSizeSmall
        height: width
        radius: 2
        color: root.checked ? (root.hovered ? Spectrum.neutralContentColorHover
                                            : Spectrum.neutralContentColorDefault)
                            : Spectrum.gray25
        border.width: root.checked ? 0 : 2
        border.color: root.hovered ? Spectrum.gray700 : Spectrum.gray600
        SpIcon {
            anchors.centerIn: parent
            visible: root.checked
            name: "Checkmark"
            size: 12
            color: Spectrum.gray25
        }
    }
    contentItem: SpLabel {
        id: label
        leftPadding: root.indicator.width + root.spacing
        text: root.text
        subdued: false
    }
}
