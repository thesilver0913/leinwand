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

    // The template does not draw the placeholder ("—" for mixed values).
    Text {
        x: root.leftPadding
        width: root.width - root.leftPadding - root.rightPadding
        height: root.height
        verticalAlignment: Text.AlignVCenter
        text: root.placeholderText
        color: root.placeholderTextColor
        font: root.font
        elide: Text.ElideRight
        visible: root.length === 0 && root.preeditText === "" && !root.activeFocus
    }

    background: Rectangle {
        radius: Spectrum.cornerRadiusSmallDefault
        color: root.enabled ? Spectrum.gray25 : Spectrum.disabledBackgroundColor
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Spectrum.focusIndicatorColor
                    : root.hovered ? Spectrum.gray500 : Spectrum.gray400
    }
}
