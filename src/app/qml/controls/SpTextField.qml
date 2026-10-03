// SPDX-License-Identifier: GPL-3.0-or-later
// Spectrum text field at the panel size (component-height-75).
import QtQuick
import QtQuick.Templates as T
import Leinwand

T.TextField {
    id: root
    implicitWidth: 64
    implicitHeight: Spectrum.componentHeight75
    leftPadding: 6
    rightPadding: 6
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    color: enabled ? Spectrum.neutralContentColorDefault : Spectrum.disabledContentColor
    selectionColor: Spectrum.accentBackgroundColorDefault
    selectedTextColor: "#ffffff"
    placeholderTextColor: Spectrum.neutralSubduedContentColorDefault
    font { family: Spectrum.fontFamily; pixelSize: Spectrum.fontSize75 }
    hoverEnabled: true

    background: Rectangle {
        radius: Spectrum.cornerRadiusSmallDefault
        color: root.enabled ? Spectrum.gray25 : Spectrum.disabledBackgroundColor
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Spectrum.focusIndicatorColor
                    : root.hovered ? Spectrum.gray500 : Spectrum.gray400
    }
}
