// SPDX-FileCopyrightText: 2019 Klarälvdalens Datakonsult AB, a KDAB Group company
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
// Derived from KDDockWidgets examples/qtquick/customtabbar/MyTabBar.qml; modified for Leinwand.
// Panel tabs in the style of Illustrator's panel groups, with Spectrum tokens.
// When the tabs do not fit, the current one keeps its width and the others
// share what is left, their titles elided.
import QtQuick
import Leinwand
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

KDDW.TabBarBase {
    id: root

    implicitHeight: Spectrum.componentHeight75

    readonly property real minimumTabWidth: 36
    // The tabs' widths with room enough, in total and for the current tab.
    readonly property real naturalSum: {
        let sum = 0;
        for (let i = 0; i < tabs.count; ++i) {
            const tab = tabs.itemAt(i);
            if (tab)
                sum += tab.natural;
        }
        return sum;
    }
    readonly property real currentNatural: {
        const tab = root.groupCpp ? tabs.itemAt(root.groupCpp.currentIndex) : null;
        return tab ? tab.natural : 0;
    }
    function widthOf(tab) {
        if (naturalSum <= width || tabs.count <= 1)
            return tab.natural;
        if (tab.isCurrent)
            return Math.min(tab.natural, Math.max(minimumTabWidth, width - minimumTabWidth * (tabs.count - 1)));
        const share = (width - Math.min(currentNatural, width / 2)) / (tabs.count - 1);
        return Math.min(tab.natural, Math.max(minimumTabWidth, share));
    }

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
        color: Spectrum.backgroundLayer1Color
    }

    Row {
        id: tabRow
        z: root.mouseAreaZ + 1
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }

        property int hoveredIndex: -1

        Repeater {
            id: tabs
            model: root.groupCpp ? root.groupCpp.tabBar.dockWidgetModel : 0

            Rectangle {
                id: tab
                required property int index
                required property string title
                readonly property int tabIndex: index
                readonly property string text: title  // Read by KDDockWidgets.
                readonly property bool isCurrent: root.groupCpp && index === root.groupCpp.currentIndex
                readonly property bool isHovered: tabRow.hoveredIndex === index

                readonly property real natural: metrics.advanceWidth + 24
                height: parent.height
                width: root.widthOf(tab)
                color: isCurrent ? Spectrum.backgroundLayer2Color : "transparent"
                clip: true

                TextMetrics {
                    id: metrics
                    text: tab.title
                    font: label.font
                }
                Text {
                    id: label
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, tab.width - 12)
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    text: tab.title
                    color: tab.isCurrent || tab.isHovered ? Spectrum.neutralContentColorHover
                                                          : Spectrum.neutralSubduedContentColorDefault
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
