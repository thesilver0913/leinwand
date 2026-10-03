// SPDX-License-Identifier: GPL-3.0-or-later
// Align panel (spec 7.2): align and distribute objects, distribute the
// spacing between them, and choose what they align to (the selection, a key
// object, or the artboard). Edge numbers follow editor::AlignEdge.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root

    component Buttons: Row {
        id: row
        property var entries: []
        property bool usable: true
        signal chosen(int value)
        spacing: 2
        Repeater {
            model: row.entries
            SpActionButton {
                required property var modelData
                implicitWidth: 32
                implicitHeight: 32
                iconSize: 20
                iconName: modelData.icon
                tip: modelData.tip
                enabled: row.usable
                onClicked: row.chosen(modelData.value)
            }
        }
    }

    ColumnLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 6

        SpLabel { text: qsTr("Align Objects:") }
        Buttons {
            usable: Session.selectionCount >= 1 || Session.anchorCount >= 2
            entries: [
                { value: 0, icon: "AlignLeft", tip: qsTr("Horizontal Align Left") },
                { value: 1, icon: "AlignCenterHorizontal", tip: qsTr("Horizontal Align Center") },
                { value: 2, icon: "AlignRight", tip: qsTr("Horizontal Align Right") },
                { value: 3, icon: "AlignTop", tip: qsTr("Vertical Align Top") },
                { value: 4, icon: "AlignCenterVertical", tip: qsTr("Vertical Align Center") },
                { value: 5, icon: "AlignBottom", tip: qsTr("Vertical Align Bottom") }
            ]
            onChosen: v => Session.align(v)
        }

        SpLabel { text: qsTr("Distribute Objects:"); Layout.topMargin: 4 }
        Buttons {
            usable: Session.selectionCount >= 3
            entries: [
                { value: 3, icon: "DistributeTop", tip: qsTr("Vertical Distribute Top") },
                { value: 4, icon: "DistributeCenterVertical", tip: qsTr("Vertical Distribute Center") },
                { value: 5, icon: "DistributeBottom", tip: qsTr("Vertical Distribute Bottom") },
                { value: 0, icon: "DistributeLeft", tip: qsTr("Horizontal Distribute Left") },
                { value: 1, icon: "DistributeCenterHorizontal", tip: qsTr("Horizontal Distribute Center") },
                { value: 2, icon: "DistributeRight", tip: qsTr("Horizontal Distribute Right") }
            ]
            onChosen: v => Session.distribute(v)
        }

        SpLabel { text: qsTr("Distribute Spacing:"); Layout.topMargin: 4 }
        RowLayout {
            spacing: 8
            Buttons {
                // With a spacing value: the key object stays; else automatic.
                usable: Session.hasKeyObject ? Session.selectionCount >= 2 : Session.selectionCount >= 3
                entries: [
                    { value: 0, icon: "DistributeSpaceVertical", tip: qsTr("Vertical Distribute Space") },
                    { value: 1, icon: "DistributeSpaceHorizontal", tip: qsTr("Horizontal Distribute Space") }
                ]
                onChosen: v => Session.distributeSpacing(v === 1,
                                                         Session.hasKeyObject ? spacing.value : NaN)
            }
            SpNumberField {
                id: spacing
                Layout.preferredWidth: 80
                enabled: Session.hasKeyObject
                value: 0
                minimum: -10000
                maximum: 10000
                onCommitted: v => value = v
            }
        }

        SpLabel { text: qsTr("Align To:"); Layout.topMargin: 4 }
        SpPicker {
            Layout.preferredWidth: 200
            model: [qsTr("Align to Selection"), qsTr("Align to Key Object"), qsTr("Align to Artboard")]
            currentIndex: Session.alignTo
            onActivated: i => Session.alignTo = i
        }
    }
}
