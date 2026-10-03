// SPDX-FileCopyrightText: 2019 Klarälvdalens Datakonsult AB, a KDAB Group company
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
// Derived from KDDockWidgets Group.qml; modified for Leinwand.
// A panel group: optional title bar, tab strip, then the current panel.
// Simplified from KDDockWidgets' Group.qml (no MDI support).
import QtQuick
import Leinwand

Rectangle {
    id: root

    property QtObject groupCpp  // Set by KDDockWidgets.
    readonly property QtObject titleBarCpp: groupCpp ? groupCpp.titleBar : null
    // Read by KDDockWidgets to size the group around its contents.
    readonly property int nonContentsHeight: (titleBar.item ? titleBar.item.height : 0) + tabBar.height
    readonly property bool hasCustomMouseEventRedirector: false

    anchors.fill: parent
    color: Spectrum.backgroundLayer2Color

    onGroupCppChanged: {
        if (groupCpp)
            groupCpp.setStackLayout(stackLayout);
    }
    onNonContentsHeightChanged: {
        if (groupCpp)
            groupCpp.geometryUpdated();
    }

    Loader {
        id: titleBar
        readonly property QtObject titleBarCpp: root.titleBarCpp
        source: root.groupCpp ? "qrc:/dock/qml/SpectrumTitleBar.qml" : ""
        anchors { top: parent.top; left: parent.left; right: parent.right }
    }

    Loader {
        id: tabBar
        readonly property QtObject groupCpp: root.groupCpp
        readonly property bool hasCustomMouseEventRedirector: root.hasCustomMouseEventRedirector
        source: root.groupCpp ? "qrc:/dock/qml/SpectrumTabBar.qml" : ""
        height: item ? item.height : 0
        anchors {
            top: titleBar.item && titleBar.item.visible ? titleBar.bottom : parent.top
            left: parent.left
            right: parent.right
        }
    }

    Item {
        id: stackLayout
        anchors {
            top: tabBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
    }
}
