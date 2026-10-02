// SPDX-License-Identifier: GPL-3.0-or-later
// The toolbar (spec 7.1): related tools share one button that shows the last
// used of them; hold it (or right-click) to pick another. Fill and stroke at
// the bottom.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import Leinwand

Rectangle {
    id: root
    implicitWidth: 44
    color: Spectrum.backgroundLayer1Color

    // Tool ids follow Session.tool. Shortcuts are listed in the tooltips.
    readonly property var groups: [
        [{ tool: 0, icon: "Select", tip: qsTr("Selection Tool (V)") }],
        [{ tool: 10, icon: "DirectSelect", tip: qsTr("Direct Selection Tool (A)") }],
        [{ tool: 6, icon: "Pen", tip: qsTr("Pen Tool (P)") },
         { tool: 7, icon: "PenAdd", tip: qsTr("Add Anchor Point Tool (+)") },
         { tool: 8, icon: "PenDelete", tip: qsTr("Delete Anchor Point Tool (-)") },
         { tool: 9, icon: "AnchorPoint", tip: qsTr("Anchor Point Tool (Shift+C)") }],
        [{ tool: 5, icon: "Line", tip: qsTr("Line Segment Tool (\\)") }],
        [{ tool: 1, icon: "RectangleHoriz", tip: qsTr("Rectangle Tool (M)") },
         { tool: 2, icon: "Circle", tip: qsTr("Ellipse Tool (L)") },
         { tool: 3, icon: "Polygon6", tip: qsTr("Polygon Tool") },
         { tool: 4, icon: "Star", tip: qsTr("Star Tool") }],
        [{ tool: 11, icon: "Eyedropper", tip: qsTr("Eyedropper Tool (I)") }],
        [{ tool: 12, icon: "Hand", tip: qsTr("Hand Tool (H)") }],
        [{ tool: 13, icon: "ZoomIn", tip: qsTr("Zoom Tool (Z)") }]
    ]

    ColumnLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 4 }
        spacing: 2

        Repeater {
            model: root.groups
            delegate: SpActionButton {
                id: button
                required property var modelData
                // The tool this button shows: the active one of its group, or the
                // last one used.
                property int shown: 0
                readonly property var entry: modelData[shown]
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 32
                implicitHeight: 32
                iconSize: 20
                iconName: entry.icon
                tip: entry.tip
                hasMenu: modelData.length > 1
                checkable: true
                checked: modelData.some(t => t.tool === Session.tool)
                onClicked: Session.tool = entry.tool
                onPressAndHold: if (hasMenu) flyout.open()

                Connections {
                    target: Session
                    function onToolChanged() {
                        const i = button.modelData.findIndex(t => t.tool === Session.tool);
                        if (i >= 0)
                            button.shown = i;
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: if (button.hasMenu) flyout.open()
                }

                T.Popup {
                    id: flyout
                    x: button.width + 2
                    y: 0
                    padding: 4
                    background: Rectangle {
                        color: Spectrum.backgroundElevatedColor
                        radius: Spectrum.cornerRadiusSmallDefault
                        border.width: 1
                        border.color: Spectrum.gray300
                    }
                    contentItem: Column {
                        spacing: 2
                        Repeater {
                            model: button.modelData
                            SpActionButton {
                                required property var modelData
                                width: 220
                                implicitHeight: 28
                                iconName: modelData.icon
                                text: modelData.tip
                                checked: Session.tool === modelData.tool
                                contentItem: Row {
                                    spacing: 8
                                    leftPadding: 6
                                    SpIcon {
                                        name: parent.parent.iconName
                                        size: 18
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    SpLabel {
                                        text: parent.parent.text
                                        subdued: false
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                onClicked: {
                                    Session.tool = modelData.tool;
                                    flyout.close();
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.bottomMargin: 6
            implicitHeight: 1
            color: Spectrum.gray300
        }

        FillStroke {
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
