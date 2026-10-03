// SPDX-License-Identifier: GPL-3.0-or-later
// Properties panel (spec 7.2): the main settings of the selection in one
// place, as in Illustrator: transform, appearance (fill, stroke, opacity) and
// quick actions. With nothing selected it shows the style for new objects.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Leinwand

Flickable {
    id: root
    readonly property var style: Session.style
    readonly property int count: Session.selectionCount
    contentHeight: column.implicitHeight + 20
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: column
        x: 10
        y: 10
        width: root.width - 20
        spacing: 8

        SpLabel {
            text: root.count === 0 ? qsTr("No Selection")
                : root.count === 1 ? qsTr("1 object selected")
                : qsTr("%1 objects selected").arg(root.count)
            subdued: false
        }

        SpSection {
            title: qsTr("Transform")
            visible: root.count > 0
            TransformFields { Layout.fillWidth: true }
        }

        SpSection {
            title: qsTr("Appearance")
            GridLayout {
                columns: 3
                columnSpacing: 6
                rowSpacing: 6
                Layout.fillWidth: true

                SpLabel { text: qsTr("Fill") }
                SpSwatch {
                    swatchColor: root.style.fill ?? "white"
                    none: root.style.fillNone ?? false
                    mixed: root.style.fillMixed ?? false
                    selected: Session.fillActive
                    MouseArea { anchors.fill: parent; onClicked: Session.fillActive = true }
                }
                Item { Layout.fillWidth: true }

                SpLabel { text: qsTr("Stroke") }
                SpSwatch {
                    stroke: true
                    swatchColor: root.style.stroke ?? "black"
                    none: root.style.strokeNone ?? false
                    mixed: root.style.strokeMixed ?? false
                    selected: !Session.fillActive
                    MouseArea { anchors.fill: parent; onClicked: Session.fillActive = false }
                }
                SpNumberField {
                    Layout.fillWidth: true
                    enabled: root.style.hasStroke ?? false
                    value: root.style.strokeWidth ?? 0
                    minimum: 0
                    onCommitted: v => Session.setStrokeValue("width", v)
                }

                SpLabel { text: qsTr("Opacity") }
                SpSlider {
                    Layout.fillWidth: true
                    enabled: root.count > 0
                    from: 0
                    to: 100
                    value: (root.style.opacity ?? 1) * 100
                    onChanged: v => Session.setOpacity(v / 100)
                }
                SpNumberField {
                    Layout.preferredWidth: 56
                    enabled: root.count > 0
                    value: Math.round((root.style.opacity ?? 1) * 100)
                    mixed: root.style.opacityMixed ?? false
                    unit: "%"
                    decimals: 0
                    minimum: 0
                    maximum: 100
                    onCommitted: v => Session.setOpacity(v / 100)
                }
            }
        }

        SpSection {
            title: qsTr("Quick Actions")
            visible: root.count > 0
            Flow {
                Layout.fillWidth: true
                spacing: 4
                SpActionButton { quiet: false; text: qsTr("Group"); onClicked: Session.group() }
                SpActionButton { quiet: false; text: qsTr("Ungroup"); onClicked: Session.ungroup() }
                SpActionButton { quiet: false; iconName: "OrderTop"; tip: qsTr("Bring to Front"); onClicked: Session.arrange(0) }
                SpActionButton { quiet: false; iconName: "OrderOneUp"; tip: qsTr("Bring Forward"); onClicked: Session.arrange(1) }
                SpActionButton { quiet: false; iconName: "OrderOneDown"; tip: qsTr("Send Backward"); onClicked: Session.arrange(2) }
                SpActionButton { quiet: false; iconName: "OrderBottom"; tip: qsTr("Send to Back"); onClicked: Session.arrange(3) }
            }
        }
    }
}
