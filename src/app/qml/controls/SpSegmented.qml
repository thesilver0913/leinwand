// SPDX-License-Identifier: GPL-3.0-or-later
// A row of buttons choosing one of several values (cap, corner, align).
import QtQuick
import Leinwand

Row {
    id: root
    // [{icon, tip}] or [{text, tip}]
    property var options: []
    property int current: -1
    signal chosen(int index)
    spacing: 2

    Repeater {
        model: root.options
        SpActionButton {
            required property int index
            required property var modelData
            iconName: modelData.icon ?? ""
            text: modelData.text ?? ""
            tip: modelData.tip ?? ""
            iconSize: 16
            checkable: true
            checked: root.current === index
            onClicked: root.chosen(index)
        }
    }
}
