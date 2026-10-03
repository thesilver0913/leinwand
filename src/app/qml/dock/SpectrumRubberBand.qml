// SPDX-License-Identifier: GPL-3.0-or-later
// Where the dragged panel will land. Spectrum drop-zone tokens:
// drop-zone-background-color at drop-zone-background-color-opacity-filled.
import QtQuick
import Leinwand

Rectangle {
    anchors.fill: parent
    radius: Spectrum.cornerRadiusSmallDefault
    color: Qt.rgba(Spectrum.dropZoneBackgroundColor.r, Spectrum.dropZoneBackgroundColor.g, Spectrum.dropZoneBackgroundColor.b, 0.3)
    border { color: Spectrum.dropZoneBackgroundColor; width: 2 }
}
