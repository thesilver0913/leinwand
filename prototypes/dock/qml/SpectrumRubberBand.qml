// SPDX-License-Identifier: GPL-3.0-or-later
// Where the dragged panel will land. Spectrum drop-zone tokens:
// drop-zone-background-color at drop-zone-background-color-opacity-filled.
import QtQuick

Rectangle {
    anchors.fill: parent
    radius: Spectrum.cornerRadiusSmall
    color: Qt.rgba(Spectrum.dropZone.r, Spectrum.dropZone.g, Spectrum.dropZone.b, 0.3)
    border { color: Spectrum.dropZone; width: 2 }
}
