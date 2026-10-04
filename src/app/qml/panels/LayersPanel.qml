// SPDX-License-Identifier: GPL-3.0-or-later
// Layers panel (spec 7.2): the tree of layers and objects, frontmost on top.
// Eye and lock toggles, expand arrows, double-click to rename, drag to
// restack (onto the middle of a layer or group to move into it). A click on
// an object selects it; on a layer, makes it the active layer.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property var model: Session.layers
    readonly property int rowHeight: 26
    property string dragId
    property int dropRow: -1
    property bool dropInto: false

    function kindName(kind) {
        return ({ layer: qsTr("Layer"), group: qsTr("<Group>"), clipGroup: qsTr("<Clip Group>"),
                  path: qsTr("<Path>"), compoundPath: qsTr("<Compound Path>"),
                  rectangle: qsTr("<Rectangle>"), ellipse: qsTr("<Ellipse>"),
                  polygon: qsTr("<Polygon>"), star: qsTr("<Star>"), line: qsTr("<Line>"),
                  text: qsTr("<Text>") })[kind] ?? kind;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.model
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: row
                required property int index
                required property string itemId
                required property string name
                required property string kind
                required property int depth
                required property bool itemVisible
                required property bool itemLocked
                required property bool dimmed
                required property bool selected
                required property bool holdsSelection
                required property bool expandable
                required property bool expanded
                required property bool active
                readonly property bool isLayer: kind === "layer"

                width: ListView.view.width
                height: root.rowHeight
                color: selected ? Spectrum.gray300
                     : active ? Spectrum.gray200
                     : rowMouse.containsMouse ? Spectrum.hoverOverlay : "transparent"

                Rectangle {  // Drop marker.
                    visible: root.dragId !== "" && root.dropRow === row.index
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    height: root.dropInto ? parent.height : 2
                    color: root.dropInto ? "transparent" : Spectrum.accentBackgroundColorDefault
                    border.width: root.dropInto ? 2 : 0
                    border.color: Spectrum.accentBackgroundColorDefault
                    z: 2
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    property point pressPoint
                    onPressed: mouse => pressPoint = Qt.point(mouse.x, mouse.y)
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return;
                        if (root.dragId === "" && Math.abs(mouse.y - pressPoint.y) > 4)
                            root.dragId = row.itemId;
                        if (root.dragId === "")
                            return;
                        const y = mapToItem(list.contentItem, mouse.x, mouse.y).y;
                        const r = Math.floor(y / root.rowHeight);
                        const within = y - r * root.rowHeight;
                        root.dropRow = Math.max(0, Math.min(r, list.count));
                        // The middle of a layer or group: into it.
                        const target = r >= 0 && r < list.count ? list.itemAtIndex(r) : null;
                        root.dropInto = !!target && (target.isLayer || target.kind === "group")
                                        && within > root.rowHeight / 4 && within < root.rowHeight * 3 / 4;
                        if (!root.dropInto && within >= root.rowHeight * 3 / 4)
                            root.dropRow = Math.min(r + 1, list.count);
                    }
                    onReleased: {
                        if (root.dragId !== "") {
                            root.model.moveToRow(root.dragId, root.dropRow, root.dropInto);
                            root.dragId = "";
                            root.dropRow = -1;
                        }
                    }
                    onClicked: mouse => root.model.activate(row.itemId, mouse.modifiers & Qt.ShiftModifier)
                    onDoubleClicked: {
                        rename.text = row.name || root.kindName(row.kind);
                        rename.visible = true;
                        rename.forceActiveFocus();
                        rename.selectAll();
                    }
                }

                RowLayout {
                    anchors { fill: parent; leftMargin: 4; rightMargin: 6 }
                    spacing: 2

                    SpActionButton {
                        implicitWidth: 22
                        implicitHeight: 22
                        iconName: row.itemVisible ? "Visibility" : ""
                        iconSize: 14
                        tip: qsTr("Toggle Visibility")
                        onClicked: root.model.setVisible(row.itemId, !row.itemVisible)
                    }
                    SpActionButton {
                        implicitWidth: 22
                        implicitHeight: 22
                        iconName: row.itemLocked ? "Lock" : ""
                        iconSize: 14
                        tip: qsTr("Toggle Lock")
                        onClicked: root.model.setLocked(row.itemId, !row.itemLocked)
                    }
                    Item { implicitWidth: row.depth * 12 }
                    SpActionButton {
                        implicitWidth: 16
                        implicitHeight: 22
                        visible: row.expandable
                        iconName: row.expanded ? "ChevronDown" : "ChevronRight"
                        iconSize: 10
                        onClicked: root.model.toggleExpanded(row.itemId)
                    }
                    Item { implicitWidth: 16; visible: !row.expandable }
                    SpLabel {
                        Layout.fillWidth: true
                        text: row.name || root.kindName(row.kind)
                        subdued: !row.isLayer
                        opacity: row.dimmed || !row.itemVisible ? 0.5 : 1
                        font.bold: row.isLayer
                        visible: !rename.visible
                    }
                    SpTextField {
                        id: rename
                        Layout.fillWidth: true
                        visible: false
                        onEditingFinished: {
                            if (visible && text !== "")
                                root.model.rename(row.itemId, text);
                            visible = false;
                        }
                        Keys.onEscapePressed: visible = false
                    }
                    Rectangle {  // Selection: a filled square; inside: an outline.
                        implicitWidth: 8
                        implicitHeight: 8
                        visible: row.selected || row.holdsSelection
                        color: row.selected ? Spectrum.accentBackgroundColorDefault : "transparent"
                        border.width: 1
                        border.color: Spectrum.accentBackgroundColorDefault
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Spectrum.gray300
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 4
            SpLabel {
                Layout.fillWidth: true
                leftPadding: 4
                text: qsTr("Active: %1").arg(root.model.activeLayerName || kindName("layer"))
            }
            SpActionButton {
                iconName: "Add"
                iconSize: 16
                tip: qsTr("Create New Layer")
                onClicked: root.model.addLayer(qsTr("Layer %1").arg(root.model.layerCount + 1))
            }
            SpActionButton {
                iconName: "Delete"
                iconSize: 16
                tip: qsTr("Delete Layer")
                onClicked: root.model.removeLayer(root.model.activeLayer)
            }
        }
    }
}
