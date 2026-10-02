// SPDX-FileCopyrightText: 2019 Klarälvdalens Datakonsult AB, a KDAB Group company
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
// Derived from KDDockWidgets Separator.qml; modified for Leinwand.
import QtQuick
import Leinwand

Rectangle {
    id: root
    anchors.fill: parent
    color: area.containsMouse || area.pressed ? Spectrum.accentBackgroundColorDefault : Spectrum.backgroundBaseColor

    readonly property QtObject kddwSeparator: parent

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.kddwSeparator && root.kddwSeparator.isVertical ? Qt.SizeVerCursor
                                                                         : Qt.SizeHorCursor
        onPressed: root.kddwSeparator.onMousePressed()
        onReleased: root.kddwSeparator.onMouseReleased()
        onPositionChanged: mouse => root.kddwSeparator.onMouseMoved(Qt.point(mouse.x, mouse.y))
        onDoubleClicked: root.kddwSeparator.onMouseDoubleClicked()
    }
}
