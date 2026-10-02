// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import com.kdab.dockwidgets 2.0 as KDDW

Window {
    id: window
    width: 1440
    height: 900
    visible: true
    color: Spectrum.backgroundBase
    title: qsTr("Leinwand - docking prototype")

    KDDW.DockingArea {
        id: dockingArea
        anchors.fill: parent
        uniqueName: "MainLayout"
        options: KDDW.KDDockWidgets.MainWindowOption_HasCentralWidget
        persistentCentralItemFileName: ":/dock/qml/CentralCanvas.qml"

        KDDW.DockWidget {
            id: properties
            uniqueName: "properties"
            title: qsTr("Properties")
            PanelContent { rows: [qsTr("Transform"), qsTr("Appearance"), qsTr("Quick Actions")] }
        }
        KDDW.DockWidget {
            id: layers
            uniqueName: "layers"
            title: qsTr("Layers")
            PanelContent { rows: [qsTr("Layer 1"), qsTr("Layer 2"), qsTr("Background")] }
        }
        KDDW.DockWidget {
            id: colorPanel
            uniqueName: "color"
            title: qsTr("Color")
            PanelContent { rows: ["R", "G", "B"] }
        }
        KDDW.DockWidget {
            id: swatches
            uniqueName: "swatches"
            title: qsTr("Swatches")
            PanelContent { rows: [qsTr("None"), qsTr("Registration"), qsTr("White"), qsTr("Black")] }
        }
        KDDW.DockWidget {
            id: stroke
            uniqueName: "stroke"
            title: qsTr("Stroke")
            PanelContent { rows: [qsTr("Weight"), qsTr("Cap"), qsTr("Corner")] }
        }

        Component.onCompleted: {
            addDockWidget(properties, KDDW.KDDockWidgets.Location_OnRight, null,
                          Qt.size(Spectrum.panelWidth, 0));
            properties.addDockWidgetAsTab(layers);
            addDockWidget(colorPanel, KDDW.KDDockWidgets.Location_OnBottom, properties);
            colorPanel.addDockWidgetAsTab(swatches);
            colorPanel.addDockWidgetAsTab(stroke);
            properties.setAsCurrentTab();
            colorPanel.setAsCurrentTab();
        }
    }

    // Workspaces (spec 7.1): check that a layout survives save and restore.
    KDDW.LayoutSaver {
        id: layoutSaver
    }
    Shortcut {
        sequence: "Ctrl+Shift+S"
        onActivated: layoutSaver.saveToFile("dock-layout.json")
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        onActivated: layoutSaver.restoreFromFile("dock-layout.json")
    }
}
