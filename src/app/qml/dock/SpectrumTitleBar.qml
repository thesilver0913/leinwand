// SPDX-License-Identifier: GPL-3.0-or-later
// Shown on floating windows and on groups whose tabs are hidden.
import QtQuick
import Leinwand
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

KDDW.TitleBarBase {
    id: root

    readonly property QtObject closeButton: closeButton
    readonly property QtObject floatButton: null

    color: Spectrum.backgroundLayer1Color
    heightWhenVisible: Spectrum.componentHeight75

    Text {
        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
        text: root.title
        color: root.isFocused ? Spectrum.neutralContentColorDefault : Spectrum.neutralSubduedContentColorDefault
        font { family: Spectrum.fontFamily; pixelSize: Spectrum.fontSize75 }
    }

    Rectangle {
        id: closeButton
        anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
        width: 20
        height: 20
        radius: Spectrum.cornerRadiusSmallDefault
        color: closeArea.containsMouse ? Spectrum.hoverOverlay : "transparent"
        opacity: root.closeButtonEnabled ? 1 : 0.4

        Image {
            anchors.centerIn: parent
            source: "image://icon/Close/" + Spectrum.neutralContentColorDefault.toString().substring(1)
            sourceSize { width: 14; height: 14 }
        }
        MouseArea {
            id: closeArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: root.closeButtonEnabled
            onClicked: root.closeButtonClicked()
        }
    }
}
