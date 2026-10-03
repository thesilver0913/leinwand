// SPDX-License-Identifier: GPL-3.0-or-later
// Stroke panel (spec 7.2): weight, cap, corner, miter limit, alignment and
// dashes of the front stroke. Arrowheads and width profiles need model
// support first (later phases).
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property var style: Session.style
    readonly property bool hasStroke: style.hasStroke ?? false
    readonly property var dashes: style.dashes ?? []

    function dashAt(i) { return i < dashes.length ? dashes[i] : 0; }
    function setDash(i, v) {
        const list = [];
        for (let k = 0; k < 6; ++k)
            list.push(k === i ? v : dashAt(k));
        while (list.length > 0 && list[list.length - 1] === 0)
            list.pop();
        Session.setDashes(list);
    }

    GridLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        columns: 2
        columnSpacing: 8
        rowSpacing: 6
        enabled: root.hasStroke

        SpLabel { text: qsTr("Weight") }
        SpNumberField {
            Layout.preferredWidth: 80
            value: root.style.strokeWidth ?? 0
            minimum: 0
            maximum: 1000
            onCommitted: v => Session.setStrokeValue("width", v)
        }

        SpLabel { text: qsTr("Cap") }
        SpSegmented {
            current: root.style.cap ?? 0
            options: [{ text: qsTr("Butt"), tip: qsTr("Butt Cap") }, { text: qsTr("Round"), tip: qsTr("Round Cap") },
                      { text: qsTr("Projecting"), tip: qsTr("Projecting Cap") }]
            onChosen: i => Session.setStrokeValue("cap", i)
        }

        SpLabel { text: qsTr("Corner") }
        RowLayout {
            spacing: 8
            SpSegmented {
                current: root.style.join ?? 0
                options: [{ text: qsTr("Miter"), tip: qsTr("Miter Join") }, { text: qsTr("Round"), tip: qsTr("Round Join") },
                          { text: qsTr("Bevel"), tip: qsTr("Bevel Join") }]
                onChosen: i => Session.setStrokeValue("join", i)
            }
        }

        SpLabel { text: qsTr("Limit") }
        SpNumberField {
            Layout.preferredWidth: 80
            value: root.style.miterLimit ?? 10
            unit: "x"
            decimals: 1
            minimum: 1
            maximum: 500
            enabled: (root.style.join ?? 0) === 0
            onCommitted: v => Session.setStrokeValue("miterLimit", v)
        }

        SpLabel { text: qsTr("Align Stroke") }
        SpSegmented {
            current: root.style.align ?? 0
            options: [{ text: qsTr("Center"), tip: qsTr("Align Stroke to Center") },
                      { text: qsTr("Inside"), tip: qsTr("Align Stroke to Inside") },
                      { text: qsTr("Outside"), tip: qsTr("Align Stroke to Outside") }]
            onChosen: i => Session.setStrokeValue("align", i)
        }

        SpCheckBox {
            Layout.columnSpan: 2
            text: qsTr("Dashed Line")
            checked: root.style.dashed ?? false
            onClicked: Session.setDashes(checked ? [12, 12] : [])
        }

        GridLayout {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            visible: root.style.dashed ?? false
            columns: 3
            columnSpacing: 4
            rowSpacing: 2
            Repeater {
                model: 3
                SpNumberField {
                    required property int index
                    Layout.fillWidth: true
                    value: root.dashAt(index * 2)
                    onCommitted: v => root.setDash(index * 2, Math.max(v, 0))
                }
            }
            Repeater {
                model: [qsTr("dash"), qsTr("dash"), qsTr("dash")]
                SpLabel {
                    required property var modelData
                    text: modelData
                    Layout.alignment: Qt.AlignHCenter
                }
            }
            Repeater {
                model: 3
                SpNumberField {
                    required property int index
                    Layout.fillWidth: true
                    value: root.dashAt(index * 2 + 1)
                    onCommitted: v => root.setDash(index * 2 + 1, Math.max(v, 0))
                }
            }
            Repeater {
                model: [qsTr("gap"), qsTr("gap"), qsTr("gap")]
                SpLabel {
                    required property var modelData
                    text: modelData
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }
    }
}
