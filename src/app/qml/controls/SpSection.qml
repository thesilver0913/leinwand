// SPDX-License-Identifier: GPL-3.0-or-later
// A titled section of a panel, as in the Properties panel.
import QtQuick
import QtQuick.Layouts
import Leinwand

ColumnLayout {
    id: root
    property string title
    default property alias content: body.data
    spacing: 6
    Layout.fillWidth: true

    SpLabel {
        text: root.title
        heading: true
        Layout.topMargin: 4
    }
    ColumnLayout {
        id: body
        spacing: 6
        Layout.fillWidth: true
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 4
        implicitHeight: 1
        color: Spectrum.gray300
    }
}
