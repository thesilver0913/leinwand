// SPDX-FileCopyrightText: 2019 Klarälvdalens Datakonsult AB, a KDAB Group company
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
// Derived from KDDockWidgets examples/qtquick/customtabbar/MyTabBar.qml; modified for Leinwand.
// Panel tabs in the style of Illustrator's panel groups, with Spectrum tokens.
import QtQuick
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

KDDW.TabBarBase {
    id: root

    implicitHeight: Spectrum.componentHeight75

    function getTabAtIndex(index) {
        return tabRow.children[index];
    }

    function getTabIndexAtPosition(globalPoint) {
        for (let i = 0; i < tabRow.children.length; ++i) {
            const tab = tabRow.children[i];
            if (tab.contains(tab.mapFromGlobal(globalPoint.x, globalPoint.y)))
                return i;
        }
        return -1;
    }

    Rectangle {
        anchors.fill: parent
        color: Spectrum.backgroundLayer1
    }

    Row {
        id: tabRow
        z: root.mouseAreaZ + 1
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }

        property int hoveredIndex: -1

        Repeater {
            model: root.groupCpp ? root.groupCpp.tabBar.dockWidgetModel : 0

            Rectangle {
                id: tab
                required property int index
                required property string title
                readonly property int tabIndex: index
                readonly property string text: title  // Read by KDDockWidgets.
                readonly property bool isCurrent: root.groupCpp && index === root.groupCpp.currentIndex
                readonly property bool isHovered: tabRow.hoveredIndex === index

                height: parent.height
                width: label.implicitWidth + 24
                color: isCurrent ? Spectrum.backgroundLayer2 : "transparent"

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: tab.title
                    color: tab.isCurrent || tab.isHovered ? Spectrum.contentHover
                                                          : Spectrum.contentSubdued
                    font {
                        family: Spectrum.fontFamily
                        pixelSize: Spectrum.fontSize75
                        bold: tab.isCurrent
                    }
                }
            }
        }

        Connections {
            target: root.tabBarCpp
            function onHoveredTabIndexChanged(index) {
                tabRow.hoveredIndex = index;
            }
        }
    }
}
