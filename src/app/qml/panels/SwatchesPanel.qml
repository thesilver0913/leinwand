// SPDX-License-Identifier: GPL-3.0-or-later
// Swatches panel (spec 7.2): the document's swatches; a click applies one to
// the active fill or stroke. New swatches take the active color.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    property string selectedId

    ColumnLayout {
        anchors { fill: parent; margins: 10 }
        spacing: 8

        Flow {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            SpSwatch {  // [None]
                width: 22; height: 22
                none: true
                MouseArea { anchors.fill: parent; onClicked: Session.setActiveNone() }
            }
            Repeater {
                model: Session.swatches
                SpSwatch {
                    required property var modelData
                    width: 22; height: 22
                    swatchColor: modelData.color
                    selected: root.selectedId === modelData.id
                    // A spot color shows a dot in its corner, as in Illustrator.
                    Rectangle {
                        visible: parent.modelData.spot
                        anchors { right: parent.right; bottom: parent.bottom; margins: 2 }
                        width: 5; height: 5; radius: 2.5
                        color: "white"
                        border.width: 1
                        border.color: "black"
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            root.selectedId = parent.modelData.id;
                            Session.applySwatch(parent.modelData.id);
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            SpLabel {
                Layout.fillWidth: true
                text: qsTr("%n swatch(es)", "", Session.swatches.length)
            }
            SpActionButton {
                iconName: "Add"
                iconSize: 16
                tip: qsTr("New Swatch")
                onClicked: Session.addSwatch(qsTr("Swatch %1").arg(Session.swatches.length + 1))
            }
            SpActionButton {
                iconName: "Delete"
                iconSize: 16
                tip: qsTr("Delete Swatch")
                enabled: root.selectedId !== ""
                onClicked: {
                    Session.removeSwatch(root.selectedId);
                    root.selectedId = "";
                }
            }
        }
    }
}
