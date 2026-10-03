// SPDX-License-Identifier: GPL-3.0-or-later
// The fill and stroke boxes at the bottom of the toolbar (spec 7.1): the
// active one sits in front (X), with swap (Shift+X), default (D), and
// color / none for the active one (/).
import QtQuick
import QtQuick.Layouts
import Leinwand

ColumnLayout {
    id: root
    readonly property var style: Session.style
    spacing: 2

    Item {
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 44
        implicitHeight: 44

        SpSwatch {  // Fill, top left.
            id: fillBox
            x: 0
            y: 0
            z: Session.fillActive ? 2 : 1
            width: 28
            height: 28
            swatchColor: root.style.fill ?? "white"
            none: root.style.fillNone ?? false
            mixed: root.style.fillMixed ?? false
            selected: Session.fillActive
            MouseArea {
                anchors.fill: parent
                onClicked: Session.fillActive = true
            }
        }
        SpSwatch {  // Stroke, bottom right.
            x: 16
            y: 16
            z: Session.fillActive ? 1 : 2
            width: 28
            height: 28
            stroke: true
            swatchColor: root.style.stroke ?? "black"
            none: root.style.strokeNone ?? false
            mixed: root.style.strokeMixed ?? false
            selected: !Session.fillActive
            MouseArea {
                anchors.fill: parent
                onClicked: Session.fillActive = false
            }
        }
    }
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 0
        SpActionButton {
            iconName: "DefaultFillStroke"
            iconSize: 14
            implicitHeight: 20
            tip: qsTr("Default Fill and Stroke (D)")
            onClicked: Session.defaultFillAndStroke()
        }
        SpActionButton {
            iconName: "SwapFillStroke"
            iconSize: 14
            implicitHeight: 20
            tip: qsTr("Swap Fill and Stroke (Shift+X)")
            onClicked: Session.swapFillAndStroke()
        }
    }
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 2
        SpActionButton {  // Color: the last color, or black.
            implicitWidth: 20
            implicitHeight: 20
            tip: qsTr("Color")
            contentItem: Item {}
            background: Rectangle {
                color: "black"
                border.width: 1
                border.color: Spectrum.gray500
            }
            onClicked: {
                const s = root.style;
                const current = Session.fillActive ? s.fill : s.stroke;
                Session.setActiveColor(current && current.valid ? current : "black");
            }
        }
        SpActionButton {
            implicitWidth: 20
            implicitHeight: 20
            tip: qsTr("None (/)")
            contentItem: Item {}
            background: SpSwatch {
                none: true
            }
            onClicked: Session.setActiveNone()
        }
    }
}
