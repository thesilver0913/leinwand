// SPDX-License-Identifier: GPL-3.0-or-later
// Artboards panel (spec 7.2): the artboards in order. A click makes one
// active and shows it; a double-click renames it. Below, the active
// artboard's position and size, and buttons to add, remove and reorder.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property var boards: Session.artboards
    readonly property int active: Session.activeArtboard
    readonly property var current: boards[active] ?? null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.boards
            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData
                property bool editing: false
                width: ListView.view.width
                height: 28
                color: index === root.active ? Spectrum.gray300 : "transparent"
                radius: Spectrum.cornerRadiusSmallDefault

                SpLabel {
                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    width: 24
                    text: row.index + 1
                }
                SpLabel {
                    visible: !row.editing
                    anchors { left: parent.left; leftMargin: 36; verticalCenter: parent.verticalCenter }
                    text: row.modelData.name
                    subdued: false
                }
                SpTextField {
                    visible: row.editing
                    anchors { left: parent.left; leftMargin: 32; right: parent.right; rightMargin: 4
                              verticalCenter: parent.verticalCenter }
                    text: row.modelData.name
                    onVisibleChanged: if (visible) { forceActiveFocus(); selectAll(); }
                    onEditingFinished: {
                        if (row.editing)
                            Session.renameArtboard(row.index, text);
                        row.editing = false;
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !row.editing
                    onClicked: {
                        Session.activeArtboard = row.index;
                        if (Session.canvas)
                            Session.canvas.fitArtboard();
                    }
                    onDoubleClicked: row.editing = true
                }
            }
        }

        GridLayout {
            columns: 4
            columnSpacing: 6
            rowSpacing: 6
            enabled: root.current !== null
            SpLabel { text: "X" }
            SpNumberField {
                Layout.preferredWidth: 80
                value: root.current ? root.current.x : 0
                minimum: -100000; maximum: 100000
                onCommitted: v => Session.setArtboardBounds(root.active, v, root.current.y,
                                                            root.current.width, root.current.height)
            }
            SpLabel { text: "W" }
            SpNumberField {
                Layout.preferredWidth: 80
                value: root.current ? root.current.width : 0
                minimum: 1; maximum: 100000
                onCommitted: v => Session.setArtboardBounds(root.active, root.current.x, root.current.y,
                                                            v, root.current.height)
            }
            SpLabel { text: "Y" }
            SpNumberField {
                Layout.preferredWidth: 80
                value: root.current ? root.current.y : 0
                minimum: -100000; maximum: 100000
                onCommitted: v => Session.setArtboardBounds(root.active, root.current.x, v,
                                                            root.current.width, root.current.height)
            }
            SpLabel { text: "H" }
            SpNumberField {
                Layout.preferredWidth: 80
                value: root.current ? root.current.height : 0
                minimum: 1; maximum: 100000
                onCommitted: v => Session.setArtboardBounds(root.active, root.current.x, root.current.y,
                                                            root.current.width, v)
            }
        }

        Row {
            Layout.alignment: Qt.AlignRight
            spacing: 2
            SpActionButton {
                iconName: "ChevronUp"
                tip: qsTr("Move Up")
                enabled: root.active > 0
                onClicked: Session.moveArtboard(root.active, root.active - 1)
            }
            SpActionButton {
                iconName: "ChevronDown"
                tip: qsTr("Move Down")
                enabled: root.active < root.boards.length - 1
                onClicked: Session.moveArtboard(root.active, root.active + 1)
            }
            SpActionButton {
                iconName: "Add"
                tip: qsTr("New Artboard")
                onClicked: Session.addArtboard()
            }
            SpActionButton {
                iconName: "Delete"
                tip: qsTr("Delete Artboard")
                enabled: root.boards.length > 1
                onClicked: Session.removeArtboard()
            }
        }
    }
}
