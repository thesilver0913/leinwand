// SPDX-License-Identifier: GPL-3.0-or-later
// The main window (spec 7.1): control bar on top, toolbar on the left, the
// canvas in the middle of a docking layout with the panels on the right, and
// the status bar at the bottom.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import com.kdab.dockwidgets 2.0 as KDDW
import Leinwand

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    title: "Leinwand"

    readonly property var canvas: Session.canvas

    // Spectrum colors for the few Qt Quick Controls still in use (scroll
    // bars, tooltips) on the Fusion style.
    color: Spectrum.backgroundBaseColor
    font.family: Spectrum.fontFamily
    font.pixelSize: Spectrum.fontSize75
    palette {
        window: Spectrum.backgroundLayer1Color
        windowText: Spectrum.neutralContentColorDefault
        base: Spectrum.gray25
        text: Spectrum.neutralContentColorDefault
        button: Spectrum.gray200
        buttonText: Spectrum.neutralContentColorDefault
        highlight: Spectrum.accentBackgroundColorDefault
        highlightedText: "#ffffff"
        mid: Spectrum.gray400
        dark: Spectrum.gray500
        toolTipBase: Spectrum.backgroundElevatedColor
        toolTipText: Spectrum.neutralContentColorDefault
    }

    // --paths=N: show N generated blobs instead of the showcase document.
    // --bench: animate zoom and pan over the blobs for 10 s, print the
    // averages, then quit.
    readonly property bool bench: Qt.application.arguments.indexOf("--bench") >= 0
    readonly property int pathsArg: {
        const arg = Qt.application.arguments.find(a => a.startsWith("--paths="));
        return arg ? parseInt(arg.substring(8)) : (bench ? 10000 : 0);
    }
    property var samples: []
    // Development aids for checking the UI: --light, --select-all, and
    // --tabs=layers,swatches to bring panels to the front.
    function argValue(name) {
        const arg = Qt.application.arguments.find(a => a.startsWith("--" + name + "="));
        return arg ? arg.substring(name.length + 3) : "";
    }
    Component.onCompleted: {
        if (pathsArg > 0)
            Session.loadTestDocument(pathsArg);
        if (Qt.application.arguments.indexOf("--light") >= 0)
            Spectrum.dark = false;
        if (Qt.application.arguments.indexOf("--select-all") >= 0)
            Session.selectAll();
    }

    header: ControlBar {}
    footer: StatusBar { canvas: window.canvas }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Toolbar {
            Layout.fillHeight: true
        }
        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 1
            color: Spectrum.gray300
        }

        KDDW.DockingArea {
            id: dockingArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            uniqueName: "MainLayout"
            options: KDDW.KDDockWidgets.MainWindowOption_HasCentralWidget
            persistentCentralItemFileName: ":/dock/qml/CentralCanvas.qml"

            KDDW.DockWidget {
                id: properties
                uniqueName: "properties"
                title: qsTr("Properties")
                SpPanel { PropertiesPanel { anchors.fill: parent } }
            }
            KDDW.DockWidget {
                id: layers
                uniqueName: "layers"
                title: qsTr("Layers")
                SpPanel { LayersPanel { anchors.fill: parent } }
            }
            KDDW.DockWidget {
                id: transform
                uniqueName: "transform"
                title: qsTr("Transform")
                SpPanel { TransformPanel { anchors.fill: parent } }
            }
            KDDW.DockWidget {
                id: colorPanel
                uniqueName: "color"
                title: qsTr("Color")
                SpPanel { ColorPanel { anchors.fill: parent } }
            }
            KDDW.DockWidget {
                id: swatches
                uniqueName: "swatches"
                title: qsTr("Swatches")
                SpPanel { SwatchesPanel { anchors.fill: parent } }
            }
            KDDW.DockWidget {
                id: stroke
                uniqueName: "stroke"
                title: qsTr("Stroke")
                SpPanel { StrokePanel { anchors.fill: parent } }
            }

            // Illustrator's default workspace, roughly: properties and layers
            // above; color, swatches and stroke below.
            Component.onCompleted: {
                addDockWidget(properties, KDDW.KDDockWidgets.Location_OnRight, null,
                              Qt.size(Spectrum.standardPanelWidth + 20, 0));
                properties.addDockWidgetAsTab(layers);
                properties.addDockWidgetAsTab(transform);
                addDockWidget(colorPanel, KDDW.KDDockWidgets.Location_OnBottom, properties);
                colorPanel.addDockWidgetAsTab(swatches);
                colorPanel.addDockWidgetAsTab(stroke);
                properties.setAsCurrentTab();
                colorPanel.setAsCurrentTab();
                const panels = { properties: properties, layers: layers, transform: transform,
                                 color: colorPanel, swatches: swatches, stroke: stroke };
                for (const name of window.argValue("tabs").split(","))
                    if (panels[name])
                        panels[name].setAsCurrentTab();
            }
        }
    }

    // Tools (spec 4.2).
    Shortcut { sequence: "V"; onActivated: Session.tool = 0 }
    Shortcut { sequence: "A"; onActivated: Session.tool = 10 }
    Shortcut { sequence: "P"; onActivated: Session.tool = 6 }
    // "+" needs Shift on most layouts; "=" is the same key unshifted on US ones.
    Shortcut { sequences: ["+", "Shift++", "="]; onActivated: Session.tool = 7 }
    Shortcut { sequence: "-"; onActivated: Session.tool = 8 }
    Shortcut { sequence: "Shift+C"; onActivated: Session.tool = 9 }
    Shortcut { sequence: "\\"; onActivated: Session.tool = 5 }
    Shortcut { sequence: "M"; onActivated: Session.tool = 1 }
    Shortcut { sequence: "L"; onActivated: Session.tool = 2 }
    Shortcut { sequence: "I"; onActivated: Session.tool = 11 }
    Shortcut { sequence: "H"; onActivated: Session.tool = 12 }
    Shortcut { sequence: "Z"; onActivated: Session.tool = 13 }

    // Fill and stroke.
    Shortcut { sequence: "X"; onActivated: Session.fillActive = !Session.fillActive }
    Shortcut { sequence: "Shift+X"; onActivated: Session.swapFillAndStroke() }
    Shortcut { sequence: "D"; onActivated: Session.defaultFillAndStroke() }
    Shortcut { sequence: "/"; onActivated: Session.setActiveNone() }

    // Edit and Object menu shortcuts, as in Illustrator (spec 4.2, 7.1).
    Shortcut { sequence: "Ctrl+Z"; onActivated: Session.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: Session.redo() }
    Shortcut { sequence: "Ctrl+A"; onActivated: Session.selectAll() }
    Shortcut { sequence: "Ctrl+Shift+A"; onActivated: Session.deselect() }
    Shortcut { sequence: "Ctrl+G"; onActivated: Session.group() }
    Shortcut { sequence: "Ctrl+Shift+G"; onActivated: Session.ungroup() }
    Shortcut { sequence: "Ctrl+Shift+]"; onActivated: Session.arrange(0) }
    Shortcut { sequence: "Ctrl+]"; onActivated: Session.arrange(1) }
    Shortcut { sequence: "Ctrl+["; onActivated: Session.arrange(2) }
    Shortcut { sequence: "Ctrl+Shift+["; onActivated: Session.arrange(3) }
    Shortcut { sequence: "Ctrl+J"; onActivated: Session.joinEnds() }

    // View menu.
    Shortcut { sequence: "Ctrl+Y"; onActivated: if (window.canvas) window.canvas.outlineView = !window.canvas.outlineView }
    Shortcut { sequence: "Ctrl+U"; onActivated: Session.smartGuides = !Session.smartGuides }
    Shortcut { sequence: "Ctrl+0"; onActivated: if (window.canvas) window.canvas.fitArtboard() }
    Shortcut { sequence: "Ctrl+1"; onActivated: if (window.canvas) window.canvas.actualSize() }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++"]; onActivated: if (window.canvas) window.canvas.zoomIn() }
    Shortcut { sequence: "Ctrl+-"; onActivated: if (window.canvas) window.canvas.zoomOut() }

    Connections {
        target: window.bench ? window.canvas : null
        function onStatsChanged() { window.samples.push([window.canvas.fps, window.canvas.drawMs]); }
    }
    FrameAnimation {
        running: window.bench && !!window.canvas
        onTriggered: {
            const t = elapsedTime;
            window.canvas.zoom = 0.15 + 1.85 * (0.5 + 0.5 * Math.sin(t * 0.8));
            window.canvas.panX = -400 * t;
            window.canvas.panY = -200 * t;
        }
    }
    Timer {
        running: window.bench
        interval: 10000
        onTriggered: {
            const avg = i => window.samples.reduce((s, v) => s + v[i], 0) / window.samples.length;
            console.log("bench paths=" + Session.objectCount + " fps=" + avg(0).toFixed(1)
                        + " drawMs=" + avg(1).toFixed(2));
            Qt.quit();
        }
    }
}
