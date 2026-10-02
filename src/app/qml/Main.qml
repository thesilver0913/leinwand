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

    // --bench: animate zoom and pan for 10 s, print the averages, then quit.
    readonly property bool bench: Qt.application.arguments.indexOf("--bench") >= 0
    property var samples: []

    // --paths=N: number of test paths (default 10000).
    readonly property int pathsArg: {
        const arg = Qt.application.arguments.find(a => a.startsWith("--paths="))
        return arg ? parseInt(arg.substring(8)) : 10000
    }

    CanvasItem {
        id: canvas
        anchors.fill: parent
        pathCount: window.pathsArg
        onStatsChanged: if (window.bench) window.samples.push([fps, drawMs])
    }

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
            console.log("bench paths=" + canvas.pathCount + " fps=" + avg(0).toFixed(1)
                        + " drawMs=" + avg(1).toFixed(2))
            Qt.quit()
        }
    }

    Text {
        anchors { left: parent.left; top: parent.top; margins: 8 }
        color: "white"
        style: Text.Outline
        text: canvas.error !== ""
              ? qsTr("Canvas error: %1").arg(canvas.error)
              : qsTr("%1 paths  %2 fps  draw %3 ms  zoom %4")
                    .arg(canvas.pathCount).arg(canvas.fps.toFixed(1))
                    .arg(canvas.drawMs.toFixed(2)).arg(canvas.zoom.toFixed(2))
    }
}
