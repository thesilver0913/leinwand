// SPDX-License-Identifier: GPL-3.0-or-later
// Gradient panel (spec 7.2): type, angle, aspect ratio and the ramp of the
// active side's gradient. Click under the ramp to add a stop, drag a stop
// along it to move it or down off it to remove it, drag a diamond above it
// to move a midpoint. A selected stop takes the color chosen in the Color
// and Swatches panels.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property var style: Session.style
    readonly property var gradient: style.gradient ?? null
    readonly property var stops: gradient ? gradient.stops : []
    readonly property int selected: style.gradientStop ?? -1
    readonly property var stop: selected >= 0 && selected < stops.length ? stops[selected] : null

    function rgba(stop) {
        return Qt.rgba(stop.color.r, stop.color.g, stop.color.b, stop.opacity);
    }

    ColumnLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 8
        enabled: Session.hasDocument

        RowLayout {
            spacing: 8
            SpLabel { text: qsTr("Type") }
            SpSegmented {
                current: root.gradient ? root.gradient.type : -1
                options: [{ text: qsTr("Linear"), tip: qsTr("Linear Gradient") },
                          { text: qsTr("Radial"), tip: qsTr("Radial Gradient") }]
                onChosen: i => Session.applyGradient(i)
            }
        }

        GridLayout {
            columns: 4
            columnSpacing: 8
            rowSpacing: 6
            enabled: root.gradient !== null

            SpLabel { text: qsTr("Angle") }
            SpNumberField {
                Layout.preferredWidth: 72
                value: root.gradient ? root.gradient.angle : 0
                unit: "°"
                decimals: 1
                minimum: -360
                maximum: 360
                onCommitted: v => Session.setGradientAngle(v)
            }
            SpLabel { text: qsTr("Aspect Ratio") }
            SpNumberField {
                Layout.preferredWidth: 72
                enabled: root.gradient !== null && root.gradient.type === 1
                value: root.gradient ? root.gradient.aspect * 100 : 100
                unit: "%"
                decimals: 1
                minimum: 1
                maximum: 10000
                onCommitted: v => Session.setGradientAspect(v / 100)
            }
        }

        // The ramp, with midpoints above and stops below.
        Item {
            id: ramp
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            implicitHeight: 52
            visible: root.gradient !== null
            readonly property real barTop: 12
            readonly property real barHeight: 22

            function offsetAt(x) { return Math.max(0, Math.min(1, x / width)); }

            Canvas {
                id: bar
                x: 0
                y: ramp.barTop
                width: ramp.width
                height: ramp.barHeight
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    // A checkerboard shows through stops with opacity.
                    const cell = 5;
                    for (let i = 0; i * cell < width; ++i) {
                        for (let j = 0; j * cell < height; ++j) {
                            ctx.fillStyle = (i + j) % 2 ? "#cccccc" : "#ffffff";
                            ctx.fillRect(i * cell, j * cell, cell, cell);
                        }
                    }
                    const stops = root.stops;
                    if (stops.length === 0)
                        return;
                    const g = ctx.createLinearGradient(0, 0, width, 0);
                    for (let k = 0; k < stops.length; ++k) {
                        g.addColorStop(stops[k].offset, root.rgba(stops[k]));
                        // A moved midpoint: the mixed color there.
                        if (k + 1 < stops.length && Math.abs(stops[k].midpoint - 0.5) > 1e-6) {
                            const a = stops[k], b = stops[k + 1];
                            g.addColorStop(a.offset + (b.offset - a.offset) * a.midpoint,
                                           Qt.rgba((a.color.r + b.color.r) / 2, (a.color.g + b.color.g) / 2,
                                                   (a.color.b + b.color.b) / 2, (a.opacity + b.opacity) / 2));
                        }
                    }
                    ctx.fillStyle = g;
                    ctx.fillRect(0, 0, width, height);
                }
                Connections {
                    target: root
                    function onStopsChanged() { bar.requestPaint(); }
                }
                onWidthChanged: requestPaint()
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.color: Spectrum.gray400
                }
            }

            // A click under the ramp adds a stop there.
            MouseArea {
                x: 0
                y: ramp.barTop + ramp.barHeight
                width: ramp.width
                height: ramp.height - y
                onClicked: mouse => Session.addGradientStop(ramp.offsetAt(mouse.x))
            }

            // Midpoints: diamonds between neighbouring stops.
            Repeater {
                model: Math.max(0, root.stops.length - 1)
                delegate: Rectangle {
                    id: diamond
                    required property int index
                    readonly property var a: root.stops[index]
                    readonly property var b: root.stops[index + 1]
                    width: 7
                    height: 7
                    rotation: 45
                    color: Spectrum.gray50
                    border.color: Spectrum.gray800
                    x: (a.offset + (b.offset - a.offset) * a.midpoint) * ramp.width - width / 2
                    y: ramp.barTop - height - 2
                    visible: b.offset - a.offset > 0.02
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        preventStealing: true
                        onPressed: Session.beginGesture()
                        onReleased: Session.endGesture()
                        onPositionChanged: mouse => {
                            const p = mapToItem(ramp, mouse.x, mouse.y);
                            const span = diamond.b.offset - diamond.a.offset;
                            if (span > 0)
                                Session.setGradientStopMidpoint(diamond.index,
                                                                (ramp.offsetAt(p.x) - diamond.a.offset) / span);
                        }
                    }
                }
            }

            // Stops: markers under the ramp, filled with their color.
            Repeater {
                model: root.stops
                delegate: Item {
                    id: marker
                    required property int index
                    required property var modelData
                    width: 14
                    height: 18
                    x: modelData.offset * ramp.width - width / 2
                    y: ramp.barTop + ramp.barHeight + 1
                    readonly property bool current: index === root.selected

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 8
                        height: 8
                        y: 0
                        rotation: 45
                        color: marker.current ? Spectrum.accentColor : Spectrum.gray700
                    }
                    Rectangle {
                        y: 4
                        width: parent.width
                        height: parent.height - 4
                        radius: 2
                        color: Qt.rgba(marker.modelData.color.r, marker.modelData.color.g,
                                       marker.modelData.color.b, 1)
                        border.width: marker.current ? 2 : 1
                        border.color: marker.current ? Spectrum.accentColor : Spectrum.gray700
                    }
                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true
                        property int dragged: -1
                        property bool removing: false
                        onPressed: {
                            Session.selectGradientStop(marker.index);
                            Session.beginGesture();
                            dragged = marker.index;
                            removing = false;
                        }
                        onPositionChanged: mouse => {
                            if (dragged < 0)
                                return;
                            const p = mapToItem(ramp, mouse.x, mouse.y);
                            // Dragged down off the ramp: removed on release
                            // (two stops always stay).
                            removing = p.y > ramp.height + 16 && root.stops.length > 2;
                            if (!removing)
                                dragged = Session.moveGradientStop(dragged, ramp.offsetAt(p.x));
                        }
                        onReleased: {
                            if (removing)
                                Session.removeGradientStop(dragged);
                            Session.endGesture();
                            dragged = -1;
                            removing = false;
                        }
                    }
                }
            }
        }

        SpLabel {
            Layout.fillWidth: true
            visible: root.gradient === null
            wrapMode: Text.WordWrap
            text: qsTr("Choose a type to apply a gradient to the %1.")
                  .arg(Session.fillActive ? qsTr("fill") : qsTr("stroke"))
        }

        GridLayout {
            columns: 4
            columnSpacing: 8
            rowSpacing: 6
            visible: root.stop !== null

            SpLabel { text: qsTr("Opacity") }
            SpNumberField {
                Layout.preferredWidth: 72
                value: root.stop ? root.stop.opacity * 100 : 100
                unit: "%"
                decimals: 0
                minimum: 0
                maximum: 100
                onCommitted: v => Session.setGradientStopOpacity(root.selected, v / 100)
            }
            SpLabel { text: qsTr("Location") }
            SpNumberField {
                Layout.preferredWidth: 72
                value: root.stop ? root.stop.offset * 100 : 0
                unit: "%"
                decimals: 1
                minimum: 0
                maximum: 100
                onCommitted: v => Session.moveGradientStop(root.selected, v / 100)
            }
            SpActionButton {
                Layout.columnSpan: 4
                text: qsTr("Delete Stop")
                enabled: root.stops.length > 2
                onClicked: Session.removeGradientStop(root.selected)
            }
        }
    }
}
