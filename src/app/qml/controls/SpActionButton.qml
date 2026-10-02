// SPDX-License-Identifier: GPL-3.0-or-later
// Spectrum quiet action button: an icon and/or a label, with hover, down
// and selected states and an optional tooltip.
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Leinwand

T.AbstractButton {
    id: root
    property string iconName
    property int iconSize: 18
    property string tip
    property bool quiet: true
    // A small triangle in the corner: hold for more tools (toolbar groups).
    property bool hasMenu: false

    implicitWidth: Math.max(implicitHeight, contentItem.implicitWidth + (text ? 16 : 0))
    implicitHeight: Spectrum.componentHeight75 + 4
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    ToolTip.visible: hovered && tip !== "" && !pressed
    ToolTip.text: tip
    ToolTip.delay: 600

    background: Rectangle {
        radius: Spectrum.cornerRadiusSmallDefault
        color: root.checked ? Spectrum.gray300
             : root.down ? Spectrum.gray200
             : root.hovered ? Spectrum.hoverOverlay
             : root.quiet ? "transparent" : Spectrum.gray100
        border.width: root.quiet ? 0 : 1
        border.color: Spectrum.gray300

        Canvas {
            visible: root.hasMenu
            anchors { right: parent.right; bottom: parent.bottom; margins: 2 }
            width: 4
            height: 4
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.fillStyle = Spectrum.neutralSubduedContentColorDefault;
                ctx.beginPath();
                ctx.moveTo(4, 0);
                ctx.lineTo(4, 4);
                ctx.lineTo(0, 4);
                ctx.closePath();
                ctx.fill();
            }
        }
    }

    contentItem: Row {
        spacing: 4
        anchors.centerIn: parent
        SpIcon {
            visible: root.iconName !== ""
            name: root.iconName
            size: root.iconSize
            anchors.verticalCenter: parent.verticalCenter
            color: root.enabled ? (root.hovered || root.checked ? Spectrum.neutralContentColorHover
                                                                : Spectrum.neutralContentColorDefault)
                                : Spectrum.disabledContentColor
        }
        Text {
            visible: root.text !== ""
            text: root.text
            anchors.verticalCenter: parent.verticalCenter
            color: root.enabled ? Spectrum.neutralContentColorDefault : Spectrum.disabledContentColor
            font { family: Spectrum.fontFamily; pixelSize: Spectrum.fontSize75 }
        }
    }
}
