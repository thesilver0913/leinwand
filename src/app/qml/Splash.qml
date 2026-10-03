// SPDX-License-Identifier: GPL-3.0-or-later
// The splash screen (spec 9): the artwork (1200 x 640) at three quarters
// size, the version beside the logotype, the licence at the lower left, and
// on the right the start-up step in progress with a progress bar. main.cpp
// sets `step` and `progress` and closes it when the main window is ready.
import QtQuick
import QtQuick.Window
import Leinwand

Window {
    id: root
    property string step
    property real progress: 0  // 0..1
    // Positions below are in the artwork's own pixels.
    readonly property real scale: width / 1200

    width: 900
    height: 480
    visible: true
    color: "#141418"
    flags: Qt.SplashScreen | Qt.FramelessWindowHint
    title: "Leinwand"

    Image {
        anchors.fill: parent
        source: "qrc:/resources/splash.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    // The version, on the logotype's baseline to its right (the logotype ends
    // at about x 385, its baseline at about y 566).
    Text {
        x: 400 * root.scale
        y: 566 * root.scale - baselineOffset
        text: "v" + Qt.application.version
        color: "#c9c9d2"
        font { family: Spectrum.fontFamily; pixelSize: 15 }
    }

    // The licence, at the lower left under the logotype.
    Text {
        x: 68 * root.scale
        y: root.height - height - 12
        text: qsTr("© 2026 the Leinwand authors. Licensed under the GNU GPL v3 or later.")
        color: "#8c8c98"
        font { family: Spectrum.fontFamily; pixelSize: 11 }
    }

    // Start-up progress, on the right.
    Column {
        x: root.width - width - 68 * root.scale
        y: 520 * root.scale
        width: 300
        spacing: 8
        Text {
            width: parent.width
            text: root.step
            color: "#c9c9d2"
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            font { family: Spectrum.fontFamily; pixelSize: 12 }
        }
        Rectangle {  // The track, with the filled part in the artwork's violet.
            width: parent.width
            height: 4
            radius: 2
            color: "#33ffffff"
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, root.progress))
                height: parent.height
                radius: 2
                color: "#9b5cff"
                Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            }
        }
    }
}
