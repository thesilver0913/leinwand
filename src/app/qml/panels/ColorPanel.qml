// SPDX-License-Identifier: GPL-3.0-or-later
// Color panel (spec 7.2): the active fill or stroke as RGB or HSB sliders,
// a hex field and a spectrum to pick from. Grayscale and CMYK come with color
// management (phase 4).
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property var style: Session.style
    readonly property bool none: Session.fillActive ? (style.fillNone ?? true) : (style.strokeNone ?? true)
    readonly property bool mixed: Session.fillActive ? (style.fillMixed ?? false) : (style.strokeMixed ?? false)
    readonly property color current: none || mixed ? "white"
                                   : (Session.fillActive ? style.fill : style.stroke)
    property bool hsb: false

    function apply(c) { Session.setActiveColor(c); }
    function hex(c) { return c.toString().substring(1).toUpperCase(); }

    ColumnLayout {
        anchors { fill: parent; margins: 10 }
        spacing: 6

        RowLayout {
            spacing: 8
            Item {  // Fill and stroke, as in Illustrator's Color panel.
                implicitWidth: 34
                implicitHeight: 34
                SpSwatch {
                    width: 22; height: 22
                    z: Session.fillActive ? 2 : 1
                    swatchColor: root.style.fill ?? "white"
                    none: root.style.fillNone ?? false
                    mixed: root.style.fillMixed ?? false
                    selected: Session.fillActive
                    MouseArea { anchors.fill: parent; onClicked: Session.fillActive = true }
                }
                SpSwatch {
                    x: 12; y: 12
                    width: 22; height: 22
                    z: Session.fillActive ? 1 : 2
                    stroke: true
                    swatchColor: root.style.stroke ?? "black"
                    none: root.style.strokeNone ?? false
                    mixed: root.style.strokeMixed ?? false
                    selected: !Session.fillActive
                    MouseArea { anchors.fill: parent; onClicked: Session.fillActive = false }
                }
            }
            Item { Layout.fillWidth: true }
            SpPicker {
                implicitWidth: 76
                model: ["RGB", "HSB"]
                currentIndex: root.hsb ? 1 : 0
                onActivated: index => root.hsb = index === 1
            }
        }

        Repeater {
            model: root.hsb
                   ? [{ label: "H", max: 360, unit: "°" }, { label: "S", max: 100, unit: "%" },
                      { label: "B", max: 100, unit: "%" }]
                   : [{ label: "R", max: 255, unit: "" }, { label: "G", max: 255, unit: "" },
                      { label: "B", max: 255, unit: "" }]
            delegate: RowLayout {
                id: channel
                required property int index
                required property var modelData
                readonly property real value: {
                    const c = root.current;
                    if (root.hsb) {
                        const h = Math.max(c.hsvHue, 0);
                        return [h * 360, c.hsvSaturation * 100, c.hsvValue * 100][index];
                    }
                    return [c.r * 255, c.g * 255, c.b * 255][index];
                }
                function withValue(v) {
                    const c = root.current;
                    if (root.hsb) {
                        const h = Math.max(c.hsvHue, 0), s = c.hsvSaturation, b = c.hsvValue;
                        const parts = [h, s, b];
                        parts[index] = v / modelData.max;
                        return Qt.hsva(Math.min(parts[0], 0.9999), parts[1], parts[2], 1);
                    }
                    const parts = [c.r, c.g, c.b];
                    parts[index] = v / 255;
                    return Qt.rgba(parts[0], parts[1], parts[2], 1);
                }
                spacing: 6
                enabled: !root.none

                SpLabel {
                    text: channel.modelData.label
                    Layout.preferredWidth: 12
                }
                SpSlider {
                    Layout.fillWidth: true
                    from: 0
                    to: channel.modelData.max
                    value: channel.value
                    gradientStops: {
                        if (root.hsb && channel.index === 0)
                            return ["#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff", "#ff0000"];
                        return [channel.withValue(0), channel.withValue(channel.modelData.max)];
                    }
                    onChanged: v => root.apply(channel.withValue(v))
                }
                SpNumberField {
                    Layout.preferredWidth: 52
                    value: Math.round(channel.value)
                    mixed: root.mixed
                    decimals: 0
                    unit: channel.modelData.unit
                    minimum: 0
                    maximum: channel.modelData.max
                    onCommitted: v => root.apply(channel.withValue(v))
                }
            }
        }

        RowLayout {
            spacing: 6
            SpLabel { text: "#" }
            SpTextField {
                Layout.preferredWidth: 72
                text: root.none || root.mixed ? "" : root.hex(root.current)
                enabled: !root.none
                onEditingFinished: {
                    const t = text.replace(/^#/, "");
                    if (/^[0-9a-fA-F]{6}$/.test(t))
                        root.apply("#" + t);
                    else
                        text = Qt.binding(() => root.none || root.mixed ? "" : root.hex(root.current));
                }
            }
        }

        // Spectrum: hue across, white at the top and black at the bottom.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            border.width: 1
            border.color: Spectrum.gray400
            Rectangle {
                anchors { fill: parent; margins: 1 }
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#ff0000" }
                    GradientStop { position: 0.167; color: "#ffff00" }
                    GradientStop { position: 0.333; color: "#00ff00" }
                    GradientStop { position: 0.5; color: "#00ffff" }
                    GradientStop { position: 0.667; color: "#0000ff" }
                    GradientStop { position: 0.833; color: "#ff00ff" }
                    GradientStop { position: 1.0; color: "#ff0000" }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#ffffffff" }
                        GradientStop { position: 0.5; color: "#00ffffff" }
                        GradientStop { position: 0.5; color: "#00000000" }
                        GradientStop { position: 1.0; color: "#ff000000" }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    function pick(x, y) {
                        const h = Math.min(Math.max(x / width, 0), 0.9999);
                        const t = Math.min(Math.max(y / height, 0), 1);
                        // Top half: towards white; bottom half: towards black.
                        const c = t < 0.5 ? Qt.hsva(h, t * 2, 1, 1) : Qt.hsva(h, 1, (1 - t) * 2, 1);
                        root.apply(c);
                    }
                    onPressed: mouse => { Session.beginGesture(); pick(mouse.x, mouse.y); }
                    onPositionChanged: mouse => pick(mouse.x, mouse.y)
                    onReleased: Session.endGesture()
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
