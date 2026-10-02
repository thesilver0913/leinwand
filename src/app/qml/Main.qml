// SPDX-License-Identifier: GPL-3.0-or-later
// Interim main window: tools on the left, canvas, transform panel on the
// right. Plain Qt Quick Controls until the Spectrum components arrive (M5).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Leinwand

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    title: "Leinwand"

    // Spectrum 2 dark colors on the Fusion style until the Spectrum
    // components arrive (M5). Tokens: qml/Spectrum.qml.
    color: Spectrum.backgroundLayer1
    font.family: Spectrum.fontFamily
    font.pixelSize: Spectrum.fontSize75
    palette {
        window: Spectrum.backgroundLayer1
        windowText: Spectrum.content
        base: Spectrum.backgroundBase
        alternateBase: Spectrum.backgroundLayer2
        text: Spectrum.content
        placeholderText: Spectrum.contentSubdued
        button: Spectrum.gray200
        buttonText: Spectrum.content
        brightText: Spectrum.contentHover
        highlight: Spectrum.accent
        highlightedText: "#ffffff"
        light: Spectrum.gray400
        midlight: Spectrum.gray300
        mid: Spectrum.gray200
        dark: Spectrum.backgroundBase
        shadow: "#000000"
        toolTipBase: Spectrum.gray100
        toolTipText: Spectrum.content
        disabled {
            windowText: Spectrum.contentSubdued
            text: Spectrum.contentSubdued
            buttonText: Spectrum.contentSubdued
            base: Spectrum.backgroundLayer2
        }
    }

    // --paths=N: show N generated blobs instead of the showcase document.
    // --bench: animate zoom and pan over the blobs for 10 s, print the
    // averages, then quit.
    readonly property bool bench: Qt.application.arguments.indexOf("--bench") >= 0
    readonly property int pathsArg: {
        const arg = Qt.application.arguments.find(a => a.startsWith("--paths="))
        return arg ? parseInt(arg.substring(8)) : (bench ? 10000 : 0)
    }
    property var samples: []
    readonly property var info: canvas.selectionInfo

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Tools (spec 4.2 shortcuts; polygon and star have none).
        ColumnLayout {
            Layout.fillHeight: true
            // Children fill the column's width; the column itself must not
            // grow, or it inherits their fillWidth and squeezes the canvas.
            Layout.fillWidth: false
            Layout.preferredWidth: 150
            Layout.maximumWidth: 150
            Layout.margins: 4
            spacing: 2
            Repeater {
                model: [
                    { tool: 0, label: qsTr("Selection (V)") },
                    { tool: 10, label: qsTr("Direct selection (A)") },
                    { tool: 6, label: qsTr("Pen (P)") },
                    { tool: 7, label: qsTr("Add anchor (+)") },
                    { tool: 8, label: qsTr("Delete anchor (-)") },
                    { tool: 9, label: qsTr("Anchor point (Shift+C)") },
                    { tool: 1, label: qsTr("Rectangle (M)") },
                    { tool: 2, label: qsTr("Ellipse (L)") },
                    { tool: 3, label: qsTr("Polygon") },
                    { tool: 4, label: qsTr("Star") },
                    { tool: 5, label: qsTr("Line (\\)") }
                ]
                Button {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    checkable: true
                    checked: canvas.tool === modelData.tool
                    onClicked: { canvas.tool = modelData.tool; canvas.forceActiveFocus() }
                }
            }
            Item { Layout.fillHeight: true }
            CheckBox {
                text: qsTr("Outline (Ctrl+Y)")
                checked: canvas.outlineView
                onClicked: { canvas.outlineView = checked; canvas.forceActiveFocus() }
            }
            CheckBox {
                text: qsTr("Smart guides (Ctrl+U)")
                checked: canvas.smartGuides
                onClicked: { canvas.smartGuides = checked; canvas.forceActiveFocus() }
            }
        }

        CanvasItem {
            id: canvas
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            onStatsChanged: if (window.bench) window.samples.push([fps, drawMs])
            Component.onCompleted: if (window.pathsArg > 0) loadTestDocument(window.pathsArg)
        }

        // Transform panel (spec 7.2): position and size of the selection's
        // bounds (reference point: top-left), and a live shape's parameters.
        Pane {
            Layout.fillHeight: true
            Layout.fillWidth: false
            Layout.preferredWidth: 260
            Layout.maximumWidth: 260

            GridLayout {
                enabled: window.info.valid
                anchors { left: parent.left; right: parent.right; top: parent.top }
                columns: 2
                columnSpacing: 8
                rowSpacing: 4

                Label { text: qsTr("Transform"); font.bold: true; Layout.columnSpan: 2 }
                Label { text: "X" }
                NumberField {
                    value: window.info.x ?? 0
                    onCommitted: v => canvas.setBounds(v, window.info.y, window.info.width, window.info.height)
                }
                Label { text: "Y" }
                NumberField {
                    value: window.info.y ?? 0
                    onCommitted: v => canvas.setBounds(window.info.x, v, window.info.width, window.info.height)
                }
                Label { text: qsTr("W") }
                NumberField {
                    value: window.info.width ?? 0
                    onCommitted: v => canvas.setBounds(window.info.x, window.info.y, v, window.info.height)
                }
                Label { text: qsTr("H") }
                NumberField {
                    value: window.info.height ?? 0
                    onCommitted: v => canvas.setBounds(window.info.x, window.info.y, window.info.width, v)
                }
                Label { text: qsTr("Angle") }
                NumberField {
                    value: window.info.rotation ?? 0
                    suffix: "°"
                    onCommitted: v => canvas.setRotation(v)
                }

                // Live shape parameters (spec 4.1).
                Label {
                    text: ({ rectangle: qsTr("Rectangle"), ellipse: qsTr("Ellipse"),
                             polygon: qsTr("Polygon"), star: qsTr("Star"),
                             line: qsTr("Line") })[window.info.shape] ?? ""
                    font.bold: true
                    Layout.columnSpan: 2
                    Layout.topMargin: 12
                    visible: !!window.info.shape
                }
                Label { text: qsTr("Width"); visible: window.info.shape === "rectangle" || window.info.shape === "ellipse" }
                NumberField {
                    visible: window.info.shape === "rectangle" || window.info.shape === "ellipse"
                    value: window.info.shapeWidth ?? 0
                    suffix: " pt"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("width", v)
                }
                Label { text: qsTr("Height"); visible: window.info.shape === "rectangle" || window.info.shape === "ellipse" }
                NumberField {
                    visible: window.info.shape === "rectangle" || window.info.shape === "ellipse"
                    value: window.info.shapeHeight ?? 0
                    suffix: " pt"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("height", v)
                }
                Label { text: qsTr("Corner radius"); visible: window.info.shape === "rectangle" }
                NumberField {
                    visible: window.info.shape === "rectangle"
                    value: window.info.cornerRadius ?? 0
                    suffix: " pt"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("cornerRadius", v)
                }
                Label { text: qsTr("Corner type"); visible: window.info.shape === "rectangle" }
                ComboBox {
                    Layout.fillWidth: true
                    visible: window.info.shape === "rectangle"
                    model: [qsTr("Round"), qsTr("Inverted round"), qsTr("Chamfer")]
                    currentIndex: window.info.cornerKind ?? 0
                    onActivated: index => canvas.setShapeValue("cornerKind", index)
                }
                Label { text: qsTr("Pie start"); visible: window.info.shape === "ellipse" }
                NumberField {
                    visible: window.info.shape === "ellipse"
                    value: window.info.pieStart ?? 0
                    suffix: "°"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("pieStart", v)
                }
                Label { text: qsTr("Pie end"); visible: window.info.shape === "ellipse" }
                NumberField {
                    visible: window.info.shape === "ellipse"
                    value: window.info.pieEnd ?? 0
                    suffix: "°"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("pieEnd", v)
                }
                Label { text: qsTr("Sides"); visible: window.info.shape === "polygon" }
                NumberField {
                    visible: window.info.shape === "polygon"
                    value: window.info.sides ?? 0
                    suffix: ""
                    decimals: 0
                    onCommitted: v => canvas.setShapeValue("sides", v)
                }
                Label { text: qsTr("Radius"); visible: window.info.shape === "polygon" }
                NumberField {
                    visible: window.info.shape === "polygon"
                    value: window.info.radius ?? 0
                    suffix: " pt"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("radius", v)
                }
                Label { text: qsTr("Corner radius"); visible: window.info.shape === "polygon" }
                NumberField {
                    visible: window.info.shape === "polygon"
                    value: window.info.polygonCornerRadius ?? 0
                    suffix: " pt"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("polygonCornerRadius", v)
                }
                Label { text: qsTr("Points"); visible: window.info.shape === "star" }
                NumberField {
                    visible: window.info.shape === "star"
                    value: window.info.points ?? 0
                    suffix: ""
                    decimals: 0
                    onCommitted: v => canvas.setShapeValue("points", v)
                }
                Label { text: qsTr("Radius 1"); visible: window.info.shape === "star" }
                NumberField {
                    visible: window.info.shape === "star"
                    value: window.info.outerRadius ?? 0
                    suffix: " pt"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("outerRadius", v)
                }
                Label { text: qsTr("Radius 2"); visible: window.info.shape === "star" }
                NumberField {
                    visible: window.info.shape === "star"
                    value: window.info.innerRadius ?? 0
                    suffix: " pt"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("innerRadius", v)
                }
                Label { text: qsTr("Length"); visible: window.info.shape === "line" }
                NumberField {
                    visible: window.info.shape === "line"
                    value: window.info.length ?? 0
                    suffix: " pt"
                    decimals: 2
                    onCommitted: v => canvas.setShapeValue("length", v)
                }

                // Anchor commands, standing in for the control bar (spec 4.2).
                Label {
                    text: qsTr("Anchors: %1").arg(canvas.anchorCount)
                    font.bold: true
                    Layout.columnSpan: 2
                    Layout.topMargin: 12
                    visible: canvas.anchorCount > 0
                }
                Button {
                    text: qsTr("Corner")
                    visible: canvas.anchorCount > 0
                    Layout.fillWidth: true
                    onClicked: { canvas.convertAnchors(false); canvas.forceActiveFocus() }
                }
                Button {
                    text: qsTr("Smooth")
                    visible: canvas.anchorCount > 0
                    Layout.fillWidth: true
                    onClicked: { canvas.convertAnchors(true); canvas.forceActiveFocus() }
                }
                Button {
                    text: qsTr("Remove")
                    visible: canvas.anchorCount > 0
                    Layout.fillWidth: true
                    onClicked: { canvas.removeAnchors(); canvas.forceActiveFocus() }
                }
                Button {
                    text: qsTr("Cut path")
                    visible: canvas.anchorCount === 1
                    Layout.fillWidth: true
                    onClicked: { canvas.cutAtAnchor(); canvas.forceActiveFocus() }
                }
                Button {
                    text: qsTr("Join (Ctrl+J)")
                    visible: canvas.anchorCount === 2
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    onClicked: { canvas.joinEnds(); canvas.forceActiveFocus() }
                }
            }
        }
    }

    // A number field that commits on Enter or when focus leaves.
    component NumberField: TextField {
        property real value
        property int decimals: 2
        property string suffix: " pt"
        signal committed(real value)
        Layout.fillWidth: true
        selectByMouse: true
        text: Number(value).toFixed(decimals) + suffix
        onEditingFinished: {
            const v = parseFloat(text)
            if (!isNaN(v) && v !== value) committed(v)
            text = Qt.binding(() => Number(value).toFixed(decimals) + suffix)
        }
    }

    // Tools.
    Shortcut { sequence: "V"; onActivated: canvas.tool = 0 }
    Shortcut { sequence: "M"; onActivated: canvas.tool = 1 }
    Shortcut { sequence: "L"; onActivated: canvas.tool = 2 }
    Shortcut { sequence: "\\"; onActivated: canvas.tool = 5 }
    Shortcut { sequence: "P"; onActivated: canvas.tool = 6 }
    // "+" needs Shift on most layouts; "=" is the same key unshifted on US ones.
    Shortcut { sequences: ["+", "Shift++", "="]; onActivated: canvas.tool = 7 }
    Shortcut { sequence: "-"; onActivated: canvas.tool = 8 }
    Shortcut { sequence: "Shift+C"; onActivated: canvas.tool = 9 }
    Shortcut { sequence: "A"; onActivated: canvas.tool = 10 }

    // Edit and Object menu shortcuts, as in Illustrator (spec 4.2, 7.1).
    Shortcut { sequence: "Ctrl+Z"; onActivated: canvas.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: canvas.redo() }
    Shortcut { sequence: "Ctrl+A"; onActivated: canvas.selectAll() }
    Shortcut { sequence: "Ctrl+Shift+A"; onActivated: canvas.deselect() }
    Shortcut { sequence: "Ctrl+G"; onActivated: canvas.group() }
    Shortcut { sequence: "Ctrl+Shift+G"; onActivated: canvas.ungroup() }
    Shortcut { sequence: "Ctrl+Shift+]"; onActivated: canvas.arrange(0) }
    Shortcut { sequence: "Ctrl+]"; onActivated: canvas.arrange(1) }
    Shortcut { sequence: "Ctrl+["; onActivated: canvas.arrange(2) }
    Shortcut { sequence: "Ctrl+Shift+["; onActivated: canvas.arrange(3) }

    Shortcut { sequence: "Ctrl+J"; onActivated: canvas.joinEnds() }

    // View menu.
    Shortcut { sequence: "Ctrl+Y"; onActivated: canvas.outlineView = !canvas.outlineView }
    Shortcut { sequence: "Ctrl+U"; onActivated: canvas.smartGuides = !canvas.smartGuides }
    Shortcut { sequence: "Ctrl+0"; onActivated: canvas.fitArtboard() }
    Shortcut { sequence: "Ctrl+1"; onActivated: canvas.actualSize() }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++"]; onActivated: canvas.zoomIn() }
    Shortcut { sequence: "Ctrl+-"; onActivated: canvas.zoomOut() }

    FrameAnimation {
        running: window.bench
        onTriggered: {
            const t = elapsedTime
            canvas.zoom = 0.15 + 1.85 * (0.5 + 0.5 * Math.sin(t * 0.8))
            canvas.panX = -400 * t
            canvas.panY = -200 * t
        }
    }

    Timer {
        running: window.bench
        interval: 10000
        onTriggered: {
            const avg = i => window.samples.reduce((s, v) => s + v[i], 0) / window.samples.length
            console.log("bench paths=" + canvas.objectCount + " fps=" + avg(0).toFixed(1)
                        + " drawMs=" + avg(1).toFixed(2))
            Qt.quit()
        }
    }

    // Stand-in for the status bar (spec 7.1) until the real UI arrives (M5).
    footer: Label {
        padding: 4
        text: canvas.error !== ""
              ? qsTr("Canvas error: %1").arg(canvas.error)
              : qsTr("%1%  %2 objects, %3 selected  undo: %4  %5 fps  draw %6 ms")
                    .arg((canvas.zoom * 100).toFixed(2)).arg(canvas.objectCount)
                    .arg(canvas.selectionCount).arg(canvas.undoAction || "-")
                    .arg(canvas.fps.toFixed(1)).arg(canvas.drawMs.toFixed(2))
    }
}
