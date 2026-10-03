// SPDX-License-Identifier: GPL-3.0-or-later
// The background of a docked panel; KDDockWidgets' own is light.
import QtQuick
import Leinwand

Rectangle {
    default property alias content: holder.data
    anchors.fill: parent
    color: Spectrum.backgroundLayer2Color

    Item {
        id: holder
        anchors.fill: parent
    }
}
