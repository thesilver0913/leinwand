// SPDX-License-Identifier: GPL-3.0-or-later
// Import report (spec 6.3): what the last opened file had that Leinwand could
// not take over as it was. A row selects the objects it affected.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property var actions: ({
        preserved: qsTr("Preserved"), approximated: qsTr("Approximated"),
        converted: qsTr("Converted"), discarded: qsTr("Discarded")
    })

    ColumnLayout {
        anchors { fill: parent; margins: 10 }
        spacing: 6

        SpLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: Session.importReport.length === 0
                  ? qsTr("Everything in the last opened file was taken over as it was.")
                  : qsTr("Some content could not be taken over as it was. Select a row to select the objects.")
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Session.importReport
            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 26
                color: area.containsMouse ? Spectrum.hoverOverlay : "transparent"
                RowLayout {
                    anchors { fill: parent; leftMargin: 4; rightMargin: 4 }
                    SpLabel {
                        Layout.fillWidth: true
                        text: row.modelData.kind
                        subdued: false
                    }
                    SpLabel { text: "×" + row.modelData.count }
                    SpLabel {
                        Layout.preferredWidth: 90
                        text: root.actions[row.modelData.action] ?? row.modelData.action
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: row.modelData.ids.length > 0
                    onClicked: Session.selectReported(row.modelData.ids)
                }
            }
        }
    }
}
