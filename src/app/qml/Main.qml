// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import Leinwand

Window {
    id: window
    width: 1280
    height: 800
    visible: true
    title: "Leinwand"

    // --paths=N: show N generated blobs instead of the showcase document.
    // --bench: animate zoom and pan over the blobs for 10 s, print the
    // averages, then quit.
    readonly property bool bench: Qt.application.arguments.indexOf("--bench") >= 0
    readonly property int pathsArg: {
        const arg = Qt.application.arguments.find(a => a.startsWith("--paths="))
        return arg ? parseInt(arg.substring(8)) : (bench ? 10000 : 0)
    }
    property var samples: []

    CanvasItem {
        id: canvas
        anchors.fill: parent
        focus: true
        onStatsChanged: if (window.bench) window.samples.push([fps, drawMs])
        Component.onCompleted: if (window.pathsArg > 0) loadTestDocument(window.pathsArg)
    }

    // Edit and Object menu shortcuts, as in Illustrator (spec 4.2, 7.1).
    Shortcut { sequence: "Ctrl+Z"; onActivated: canvas.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; onActivated: canvas.redo() }
    Shortcut { sequence: "Ctrl+A"; onActivated: canvas.selectAll() }
    Shortcut { sequence: "Ctrl+Shift+A"; onActivated: canvas.deselect() }
    Shortcut { sequence: "Ctrl+G"; onActivated: canvas.group() }
    Shortcut { sequence: "Ctrl+Shift+G"; onActivated: canvas.ungroup() }
    Shortcut { sequence: "Ctrl+Shift+]"; onActivated: canvas.arrange(0) }
    Shortcut { sequence: "Ctrl+]"; onActivated: canvas.arrange(1) }
    Shortcut { sequence: "Ctrl+["; onActivated: canvas.arrange(2) }
    Shortcut { sequence: "Ctrl+Shift+["; onActivated: canvas.arrange(3) }

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
    Text {
        anchors { left: parent.left; bottom: parent.bottom; margins: 8 }
        color: "white"
        style: Text.Outline
        text: canvas.error !== ""
              ? qsTr("Canvas error: %1").arg(canvas.error)
              : qsTr("%1%  %2 objects, %3 selected  undo: %4  %5 fps  draw %6 ms")
                    .arg((canvas.zoom * 100).toFixed(2)).arg(canvas.objectCount)
                    .arg(canvas.selectionCount).arg(canvas.undoAction || "-")
                    .arg(canvas.fps.toFixed(1)).arg(canvas.drawMs.toFixed(2))
    }
}
