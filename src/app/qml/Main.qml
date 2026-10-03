// SPDX-License-Identifier: GPL-3.0-or-later
// The main window (spec 7.1): control bar on top, toolbar on the left, the
// canvas in the middle of a docking layout with the panels on the right, and
// the status bar at the bottom.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import com.kdab.dockwidgets 2.0 as KDDW
import Leinwand

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    title: Session.hasDocument ? Session.displayName + (Session.dirty ? "*" : "") + " - Leinwand" : "Leinwand"

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
        // Without these, disabled menu items look like enabled ones.
        disabled {
            windowText: Spectrum.disabledContentColor
            text: Spectrum.disabledContentColor
            buttonText: Spectrum.disabledContentColor
            highlightedText: Spectrum.disabledContentColor
        }
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
    // Development aids for checking the UI: --showcase (the sample document),
    // --light, --select-all, --tabs=layers,swatches to bring panels to the
    // front, and --preferences=N.
    function argValue(name) {
        const arg = Qt.application.arguments.find(a => a.startsWith("--" + name + "="));
        return arg ? arg.substring(name.length + 3) : "";
    }
    Component.onCompleted: {
        // A file to open ("Open with", or a path on the command line).
        const file = Qt.application.arguments.slice(1).find(a => !a.startsWith("--"));
        if (file)
            Qt.callLater(() => Session.openPath(file));  // After the panel layout is set up.
        else if (pathsArg > 0)
            Session.loadTestDocument(pathsArg);
        else if (Qt.application.arguments.indexOf("--showcase") >= 0)
            Session.loadShowcase();
        // Spec 9: the welcome screen, unless a file was given or it is off.
        else if (Preferences.showWelcome && !bench)
            Qt.callLater(() => {
                // Unless a file arrived first (on macOS, as an event).
                if (!Session.hasDocument)
                    showWelcome();
            });
        if (Qt.application.arguments.indexOf("--light") >= 0)
            Spectrum.dark = false;  // For this run only; the preference stays.
        // --preferences=N: open the preferences at page N (for checking layouts).
        if (argValue("preferences") !== "") {
            preferencesDialog.category = parseInt(argValue("preferences"));
            preferencesDialog.show();
        }
        if (Qt.application.arguments.indexOf("--select-all") >= 0)
            Session.selectAll();
    }

    menuBar: AppMenuBar { window: window }
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
                id: artboardsPanel
                uniqueName: "artboards"
                title: qsTr("Artboards")
                SpPanel { ArtboardsPanel { anchors.fill: parent } }
            }
            KDDW.DockWidget {
                id: transform
                uniqueName: "transform"
                title: qsTr("Transform")
                SpPanel { TransformPanel { anchors.fill: parent } }
            }
            KDDW.DockWidget {
                id: alignPanel
                uniqueName: "align"
                title: qsTr("Align")
                SpPanel { AlignPanel { anchors.fill: parent } }
            }
            KDDW.DockWidget {
                id: pathfinderPanel
                uniqueName: "pathfinder"
                title: qsTr("Pathfinder")
                SpPanel { PathfinderPanel { anchors.fill: parent } }
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
            KDDW.DockWidget {
                id: importReport
                uniqueName: "importReport"
                title: qsTr("Import Report")
                SpPanel { ImportReportPanel { anchors.fill: parent } }
            }

            // Illustrator's default workspace, roughly: properties, layers and
            // artboards; transform, align and pathfinder; color, swatches and stroke.
            Component.onCompleted: {
                addDockWidget(properties, KDDW.KDDockWidgets.Location_OnRight, null,
                              Qt.size(Spectrum.standardPanelWidth + 20, 0));
                properties.addDockWidgetAsTab(layers);
                properties.addDockWidgetAsTab(artboardsPanel);
                // Transform, Align and Pathfinder in a group of their own, as
                // in Illustrator, so that no group has more tabs than fit.
                addDockWidget(transform, KDDW.KDDockWidgets.Location_OnBottom, properties);
                transform.addDockWidgetAsTab(alignPanel);
                transform.addDockWidgetAsTab(pathfinderPanel);
                addDockWidget(colorPanel, KDDW.KDDockWidgets.Location_OnBottom, transform);
                colorPanel.addDockWidgetAsTab(swatches);
                colorPanel.addDockWidgetAsTab(stroke);
                colorPanel.addDockWidgetAsTab(importReport);
                properties.setAsCurrentTab();
                transform.setAsCurrentTab();
                colorPanel.setAsCurrentTab();
                const panels = { properties: properties, layers: layers, transform: transform,
                                 align: alignPanel, pathfinder: pathfinderPanel,
                                 artboards: artboardsPanel,
                                 color: colorPanel, swatches: swatches, stroke: stroke };
                for (const name of window.argValue("tabs").split(","))
                    if (panels[name])
                        panels[name].setAsCurrentTab();
            }
        }
    }

    // Panels with their own keys (Illustrator's Window menu).
    Shortcut {
        sequences: Shortcuts.windowAlign
        onActivated: {
            alignPanel.open();
            alignPanel.setAsCurrentTab();
        }
    }
    Shortcut {
        sequences: Shortcuts.windowPathfinder
        onActivated: {
            pathfinderPanel.open();
            pathfinderPanel.setAsCurrentTab();
        }
    }

    // Tools (spec 4.2), keys from the Shortcuts registry (spec 7.3).
    Shortcut { sequences: Shortcuts.toolSelection; enabled: Session.hasDocument; onActivated: Session.tool = 0 }
    Shortcut { sequences: Shortcuts.toolDirectSelection; enabled: Session.hasDocument; onActivated: Session.tool = 10 }
    Shortcut { sequences: Shortcuts.toolPen; enabled: Session.hasDocument; onActivated: Session.tool = 6 }
    Shortcut { sequences: Shortcuts.toolAddAnchor; enabled: Session.hasDocument; onActivated: Session.tool = 7 }
    Shortcut { sequences: Shortcuts.toolPolygon; enabled: Session.hasDocument; onActivated: Session.tool = 3 }
    Shortcut { sequences: Shortcuts.toolStar; enabled: Session.hasDocument; onActivated: Session.tool = 4 }
    Shortcut { sequences: Shortcuts.toolDeleteAnchor; enabled: Session.hasDocument; onActivated: Session.tool = 8 }
    Shortcut { sequences: Shortcuts.toolAnchorPoint; enabled: Session.hasDocument; onActivated: Session.tool = 9 }
    Shortcut { sequences: Shortcuts.toolLine; enabled: Session.hasDocument; onActivated: Session.tool = 5 }
    Shortcut { sequences: Shortcuts.toolRectangle; enabled: Session.hasDocument; onActivated: Session.tool = 1 }
    Shortcut { sequences: Shortcuts.toolEllipse; enabled: Session.hasDocument; onActivated: Session.tool = 2 }
    Shortcut { sequences: Shortcuts.toolEyedropper; enabled: Session.hasDocument; onActivated: Session.tool = 11 }
    Shortcut { sequences: Shortcuts.toolScissors; enabled: Session.hasDocument; onActivated: Session.tool = 14 }
    Shortcut { sequences: Shortcuts.toolArtboard; enabled: Session.hasDocument; onActivated: Session.tool = 15 }
    Shortcut { sequences: Shortcuts.toolHand; enabled: Session.hasDocument; onActivated: Session.tool = 12 }
    Shortcut { sequences: Shortcuts.toolZoom; enabled: Session.hasDocument; onActivated: Session.tool = 13 }

    // Fill and stroke.
    Shortcut { sequences: Shortcuts.paintToggle; enabled: Session.hasDocument; onActivated: Session.fillActive = !Session.fillActive }
    Shortcut { sequences: Shortcuts.paintSwap; enabled: Session.hasDocument; onActivated: Session.swapFillAndStroke() }
    Shortcut { sequences: Shortcuts.paintDefault; enabled: Session.hasDocument; onActivated: Session.defaultFillAndStroke() }
    Shortcut { sequences: Shortcuts.paintNone; enabled: Session.hasDocument; onActivated: Session.setActiveNone() }



    // --- Files (spec 3.3, 6) ---------------------------------------------------

    readonly property var panels: [properties, layers, artboardsPanel, transform, alignPanel, pathfinderPanel,
                                   colorPanel,
                                   swatches, stroke, importReport]
    property alias openDialog: openDialog
    property alias saveAsDialog: saveAsDialog
    property alias pngOptions: pngOptions
    property alias aboutDialog: aboutDialog
    property alias preferencesDialog: preferencesDialog
    property alias averageDialog: averageDialog
    property alias coverDialog: coverDialog
    property alias shortcutsDialog: shortcutsDialog

    function showWelcome() {
        welcome.show();
        welcome.raise();
        welcome.requestActivate();
    }
    property var pending: null  // What to do once unsaved changes are dealt with.
    property bool closing: false

    // Runs `action` now, or after asking about unsaved changes.
    function guard(action) {
        if (!Session.dirty) {
            action();
            return;
        }
        pending = action;
        unsavedDialog.open();
    }
    // Saves to the file, or asks for one first; then runs `then`.
    function saveDocument(then) {
        if (Session.filePath === "") {
            saveAsDialog.then = then;
            saveAsDialog.open();
            return;
        }
        if (Session.save() && then)
            then();
    }
    function exportSvg() {
        const issues = Session.svgExportIssues();
        if (issues.length === 0) {
            exportSvgDialog.open();
            return;
        }
        exportIssues.issues = issues;
        exportIssues.open();
    }

    onClosing: close => {
        if (closing || !Session.dirty) {
            Session.closeCleanly();
            return;
        }
        close.accepted = false;
        guard(() => {
            closing = true;
            Session.closeCleanly();
            window.close();
        });
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open")
        nameFilters: [qsTr("Leinwand and SVG files (*.lwd *.svg)"), qsTr("Leinwand documents (*.lwd)"),
                      qsTr("SVG files (*.svg)")]
        onAccepted: Session.open(selectedFile)
    }
    FileDialog {
        id: saveAsDialog
        property var then: null
        title: qsTr("Save As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "lwd"
        nameFilters: [qsTr("Leinwand documents (*.lwd)")]
        onAccepted: {
            if (Session.saveAs(selectedFile) && then)
                then();
            then = null;
        }
        onRejected: then = null
    }
    FileDialog {
        id: exportSvgDialog
        title: qsTr("Export as SVG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "svg"
        nameFilters: [qsTr("SVG files (*.svg)")]
        onAccepted: Session.exportSvg(selectedFile)
    }
    FileDialog {
        id: exportPngDialog
        property real scale: 1
        property bool transparent: false
        property bool allArtboards: false
        title: qsTr("Export as PNG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG images (*.png)")]
        onAccepted: Session.exportPng(selectedFile, scale, transparent, allArtboards)
    }

    MessageDialog {
        id: unsavedDialog
        text: qsTr("Save changes to %1?").arg(Session.displayName)
        informativeText: qsTr("Your changes will be lost if you don't save them.")
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: (button, role) => {
            const action = window.pending;
            window.pending = null;
            if (button === MessageDialog.Save)
                window.saveDocument(action);
            else if (button === MessageDialog.Discard && action)
                action();
        }
    }
    MessageDialog {
        id: errorDialog
        text: Session.error
        buttons: MessageDialog.Ok
    }
    Connections {
        target: Session
        function onErrorChanged() { errorDialog.open(); }
        function onImportReportChanged() {
            if (Session.importReport.length > 0) {
                importReport.open();
                importReport.setAsCurrentTab();
            }
        }
    }

    // Spec 3.3: a recovery file left behind means the last session ended
    // abnormally.
    MessageDialog {
        id: recoveryDialog
        property var file: Session.recoveryFiles.length > 0 ? Session.recoveryFiles[0] : null
        text: qsTr("Leinwand did not close normally last time.")
        informativeText: file ? qsTr("Recover the unsaved changes to %1 from %2?")
                                    .arg(file.original || qsTr("an untitled document"))
                                    .arg(Qt.formatDateTime(file.time))
                              : ""
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Yes)
                Session.restore(file.path);
            else
                Session.discardRecovery(file.path);
        }
    }
    Timer {
        // After the window is up.
        interval: 300
        running: Session.recoveryFiles.length > 0 && !window.bench
        onTriggered: recoveryDialog.open()
    }

    // PNG export options (spec 6: resolution).
    Dialog {
        id: pngOptions
        title: qsTr("PNG Export Options")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            spacing: 8
            SpLabel { text: qsTr("Resolution"); subdued: false }
            SpPicker {
                id: resolution
                Layout.preferredWidth: 200
                model: [qsTr("Screen (72 ppi)"), qsTr("Medium (150 ppi)"), qsTr("High (300 ppi)")]
                currentIndex: 0
            }
            SpCheckBox {
                id: transparentBackground
                text: qsTr("Transparent background")
            }
            SpLabel { text: qsTr("Range"); subdued: false }
            SpPicker {
                id: exportRange
                Layout.preferredWidth: 260
                model: [qsTr("Active artboard"), qsTr("All artboards (one file each)")]
                currentIndex: 0
            }
        }
        onAccepted: {
            exportPngDialog.scale = [1, 150 / 72, 300 / 72][resolution.currentIndex];
            exportPngDialog.transparent = transparentBackground.checked;
            exportPngDialog.allArtboards = exportRange.currentIndex === 1;
            exportPngDialog.open();
        }
    }

    // Spec 6.3: what the format cannot hold is listed before writing.
    Dialog {
        id: exportIssues
        property var issues: []
        title: qsTr("Export as SVG")
        anchors.centerIn: parent
        width: 460
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            SpLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                subdued: false
                text: qsTr("SVG cannot hold everything in this document as it is:")
            }
            Repeater {
                model: exportIssues.issues
                SpLabel {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "• " + modelData.kind + " ×" + modelData.count
                }
            }
        }
        onAccepted: exportSvgDialog.open()
    }

    WelcomeScreen {
        id: welcome
        guard: action => window.guard(action)
        onOpenRequested: window.guard(() => openDialog.open())
        transientParent: window
    }
    AverageDialog { id: averageDialog }
    CoverDialog { id: coverDialog }
    PreferencesDialog {
        id: preferencesDialog
        transientParent: window
    }
    ShortcutsDialog {
        id: shortcutsDialog
        transientParent: window
    }
    Connections {
        target: Session
        // Opening from anywhere closes the welcome screen.
        function onFileChanged() { if (Session.hasDocument) welcome.close(); }
    }

    Dialog {
        id: aboutDialog
        title: qsTr("About Leinwand")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        SpLabel {
            subdued: false
            text: qsTr("Leinwand %1, a vector graphics editor.\nLicensed under the GNU GPL, version 3 or later.\nNot affiliated with or endorsed by Adobe. Adobe, Illustrator and Spectrum are\ntrademarks of Adobe Inc.").arg(Qt.application.version)
        }
    }

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
