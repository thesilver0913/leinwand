// SPDX-FileCopyrightText: 2019 Klarälvdalens Datakonsult AB, a KDAB Group company
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
// Derived from KDDockWidgets FloatingWindow.qml; modified for Leinwand.
import QtQuick
import Leinwand
import com.kdab.dockwidgets 2.0
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

Rectangle {
    id: root
    readonly property FloatingWindowView floatingWindowCpp: parent // qmllint disable incompatible-type
    readonly property TitleBarView titleBarCpp: floatingWindowCpp ? floatingWindowCpp.titleBar : null
    readonly property DropAreaView dropAreaCpp: floatingWindowCpp ? floatingWindowCpp.dropArea : null
    readonly property int titleBarHeight: titleBar.heightWhenVisible
    property int margins: 1  // Read by KDDockWidgets.

    anchors.fill: parent
    color: Spectrum.backgroundLayer2Color
    border { color: Spectrum.gray400; width: 1 }

    // While a panel is dragged, the drop indicators are drawn inside the
    // window underneath, so let them show through. Per-pixel alpha does not
    // work with Vulkan on Windows, but whole-window opacity is applied by the
    // OS compositor.
    Binding {
        target: root.Window.window
        property: "opacity"
        value: Singletons.helpers.isDragging ? 0.6 : 1.0
    }

    onTitleBarHeightChanged: {
        if (floatingWindowCpp)
            floatingWindowCpp.geometryUpdated();
    }

    Loader {
        id: titleBar
        readonly property TitleBarView titleBarCpp: root.titleBarCpp
        readonly property int heightWhenVisible: item ? item.heightWhenVisible : 0 // qmllint disable missing-property
        source: Singletons.widgetFactory.titleBarFilename()
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: root.margins }
    }

    KDDW.DropArea {
        id: dropArea
        dropAreaCpp: root.dropAreaCpp
        anchors {
            left: parent.left
            right: parent.right
            top: titleBar.bottom
            bottom: parent.bottom
            leftMargin: root.margins
            rightMargin: root.margins
            bottomMargin: root.margins
        }
    }

    onDropAreaCppChanged: {
        if (dropAreaCpp) {
            dropAreaCpp.parent = dropArea;
            dropAreaCpp.anchors.fill = dropArea;
        }
    }
}
