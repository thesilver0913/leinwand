// SPDX-License-Identifier: GPL-3.0-or-later
// Spectrum slider. A drag is one undo step (spec 7.2): it opens a gesture on
// press and closes it on release. `gradientStops` paints the track with a
// color ramp (Color panel).
import QtQuick
import QtQuick.Templates as T
import Leinwand

T.Slider {
    id: root
    property var gradientStops: null  // [color, ...] across the track, or null.
    signal changed(real value)       // Each move while dragging.

    implicitWidth: 120
    implicitHeight: Spectrum.componentHeight75
    padding: 6
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    onPressedChanged: pressed ? Session.beginGesture() : Session.endGesture()
    onMoved: changed(value)

    background: Item {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: root.gradientStops ? 6 : 2

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            visible: !root.gradientStops
            color: Spectrum.gray400
            Rectangle {
                width: root.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: Spectrum.neutralContentColorDefault
            }
        }
        Canvas {
            id: ramp
            anchors.fill: parent
            visible: !!root.gradientStops
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const stops = root.gradientStops || [];
                if (stops.length === 0)
                    return;
                const g = ctx.createLinearGradient(0, 0, width, 0);
                for (let i = 0; i < stops.length; ++i)
                    g.addColorStop(stops.length > 1 ? i / (stops.length - 1) : 0, stops[i]);
                ctx.fillStyle = g;
                ctx.fillRect(0, 0, width, height);
            }
            Connections {
                target: root
                function onGradientStopsChanged() { ramp.requestPaint(); }
            }
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: 14
        height: 14
        radius: 7
        color: Spectrum.gray25
        border.width: 2
        border.color: root.pressed || root.hovered ? Spectrum.neutralContentColorHover
                                                   : Spectrum.neutralContentColorDefault
    }
}
