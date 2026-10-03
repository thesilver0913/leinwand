// SPDX-License-Identifier: GPL-3.0-or-later
// A workflow icon (20x20 grid) in a theme color, crisp at any device pixel
// ratio. `name` is the icon's file name without extension.
import QtQuick
import QtQuick.Window
import Leinwand

Image {
    id: root
    property string name
    property color color: Spectrum.neutralContentColorDefault
    property int size: 18

    width: size
    height: size
    sourceSize: Qt.size(size * Screen.devicePixelRatio, size * Screen.devicePixelRatio)
    source: name ? "image://icon/" + name + "/" + color.toString().substring(1) : ""
    fillMode: Image.PreserveAspectFit
}
