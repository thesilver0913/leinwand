// SPDX-License-Identifier: GPL-3.0-or-later
// The canvas in the middle of the docking layout. KDDockWidgets creates it
// from this file; the window reaches it through Session.canvas.
import QtQuick
import Leinwand

CanvasItem {
    anchors.fill: parent
    focus: true
}
