// SPDX-License-Identifier: GPL-3.0-or-later
// One drop target. KDDockWidgets finds these by their type name, so this file
// must stay named ClassicIndicator.qml.
import QtQuick
import Leinwand
import com.kdab.dockwidgets 2.0

Rectangle {
    id: root

    required property ClassicDropIndicatorOverlay overlayWindow
    property int indicatorType: KDDockWidgets.DropLocation_None
    readonly property bool isHovered: overlayWindow && overlayWindow.currentDropLocation === indicatorType

    readonly property bool isLeft: indicatorType === KDDockWidgets.DropLocation_Left
                                   || indicatorType === KDDockWidgets.DropLocation_OutterLeft
    readonly property bool isRight: indicatorType === KDDockWidgets.DropLocation_Right
                                    || indicatorType === KDDockWidgets.DropLocation_OutterRight
    readonly property bool isTop: indicatorType === KDDockWidgets.DropLocation_Top
                                  || indicatorType === KDDockWidgets.DropLocation_OutterTop
    readonly property bool isBottom: indicatorType === KDDockWidgets.DropLocation_Bottom
                                     || indicatorType === KDDockWidgets.DropLocation_OutterBottom

    width: 36
    height: 36
    radius: Spectrum.cornerRadiusSmallDefault
    color: isHovered ? Spectrum.accentBackgroundColorDefault : Spectrum.gray200
    border { color: isHovered ? Spectrum.accentContentColorDefault : Spectrum.gray400; width: 1 }

    // A small pane, with the part the panel would take filled in.
    Rectangle {
        id: pane
        anchors.centerIn: parent
        width: 20
        height: 20
        radius: 2
        color: "transparent"
        border { color: Spectrum.neutralContentColorDefault; width: 1 }

        Rectangle {
            color: Spectrum.neutralContentColorDefault
            radius: 1
            x: root.isRight ? parent.width / 2 : 0
            y: root.isBottom ? parent.height / 2 : 0
            width: root.isLeft || root.isRight ? parent.width / 2 : parent.width
            height: root.isTop || root.isBottom ? parent.height / 2 : parent.height
            opacity: 0.7
        }
    }
}
