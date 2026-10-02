// SPDX-License-Identifier: GPL-3.0-or-later
// Spectrum picker (a drop-down choice) at the panel size.
import QtQuick
import QtQuick.Templates as T
import Leinwand

T.ComboBox {
    id: root
    implicitWidth: 120
    implicitHeight: Spectrum.componentHeight75
    leftPadding: 8
    rightPadding: 24
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    font.family: Spectrum.fontFamily
    font.pixelSize: Spectrum.fontSize75

    background: Rectangle {
        radius: Spectrum.cornerRadiusSmallDefault
        color: root.down || root.hovered ? Spectrum.gray200 : Spectrum.gray100
        border.width: 1
        border.color: Spectrum.gray300
    }
    contentItem: SpLabel {
        text: root.displayText
        subdued: false
    }
    indicator: SpIcon {
        x: root.width - width - 6
        y: (root.height - height) / 2
        name: "ChevronDown"
        size: 12
    }
    delegate: T.ItemDelegate {
        id: item
        required property int index
        required property var modelData
        width: ListView.view ? ListView.view.width : 0
        implicitHeight: Spectrum.componentHeight75 + 4
        hoverEnabled: true
        background: Rectangle {
            color: item.hovered ? Spectrum.hoverOverlay : "transparent"
        }
        contentItem: SpLabel {
            leftPadding: 8
            text: root.textRole ? item.modelData[root.textRole] : item.modelData
            subdued: false
            font.bold: root.currentIndex === item.index
        }
    }
    popup: T.Popup {
        y: root.height + 2
        width: Math.max(root.width, 120)
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        background: Rectangle {
            color: Spectrum.backgroundElevatedColor
            radius: Spectrum.cornerRadiusSmallDefault
            border.width: 1
            border.color: Spectrum.gray300
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex
        }
    }
}
