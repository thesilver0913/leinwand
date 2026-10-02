// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import LeinwandCanvas

CanvasItem {
    anchors.fill: parent
    Component.onCompleted: loadTestDocument(10000)
}
