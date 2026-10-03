// SPDX-License-Identifier: GPL-3.0-or-later
// Field labels and panel text.
import QtQuick
import Leinwand

Text {
    property bool subdued: true
    property bool heading: false
    color: !enabled ? Spectrum.disabledContentColor
         : subdued && !heading ? Spectrum.neutralSubduedContentColorDefault
         : Spectrum.neutralContentColorDefault
    font { family: Spectrum.fontFamily; pixelSize: Spectrum.fontSize75; bold: heading }
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
}
