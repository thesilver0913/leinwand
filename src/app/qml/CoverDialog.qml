// SPDX-License-Identifier: GPL-3.0-or-later
// File > Cover Setup (spec 7.5): changes the page count, paper or size of a
// book cover; the cover's artboards are laid out again (one undo step).
import QtQuick
import QtQuick.Controls
import Leinwand

Dialog {
    id: root
    title: qsTr("Cover Setup")
    modal: true
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.Ok | Dialog.Cancel
    background: Rectangle {
        color: Spectrum.backgroundLayer2Color
        radius: Spectrum.cornerRadiusMediumDefault
        border.width: 1
        border.color: Spectrum.gray300
    }
    onAboutToShow: form.load(Session.cover())

    CoverForm { id: form }

    onAccepted: {
        const c = form.values();
        Session.setCover(c.width, c.height, c.pages, c.thickness, c.spine, c.bleed);
    }
}
