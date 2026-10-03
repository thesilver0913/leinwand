// SPDX-License-Identifier: GPL-3.0-or-later
// Transparency panel (spec 7.2): blend mode and opacity of the selected
// objects, isolated blending for groups, and the opacity mask. The mask is
// made from the frontmost selected object; editing the mask in place comes
// later.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property var info: Session.transparency
    readonly property bool selected: info.selected ?? false
    readonly property bool hasMask: info.hasMask ?? false

    // In core::BlendMode order, which is Illustrator's menu order.
    readonly property var modes: [qsTr("Normal"), qsTr("Darken"), qsTr("Multiply"), qsTr("Color Burn"),
                                  qsTr("Lighten"), qsTr("Screen"), qsTr("Color Dodge"), qsTr("Overlay"),
                                  qsTr("Soft Light"), qsTr("Hard Light"), qsTr("Difference"),
                                  qsTr("Exclusion"), qsTr("Hue"), qsTr("Saturation"), qsTr("Color"),
                                  qsTr("Luminosity")]

    ColumnLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 8
        enabled: root.selected

        GridLayout {
            columns: 2
            columnSpacing: 8
            rowSpacing: 6

            SpLabel { text: qsTr("Blending Mode") }
            SpPicker {
                Layout.preferredWidth: 130
                model: root.modes
                currentIndex: root.info.blendMixed ? -1 : (root.info.blendMode ?? 0)
                displayText: root.info.blendMixed ? "—" : currentText
                onActivated: index => Session.setBlendMode(index)
            }
            SpLabel { text: qsTr("Opacity") }
            SpNumberField {
                Layout.preferredWidth: 64
                value: Math.round((root.info.opacity ?? 1) * 100)
                mixed: root.info.opacityMixed ?? false
                unit: "%"
                decimals: 0
                minimum: 0
                maximum: 100
                onCommitted: v => Session.setOpacity(v / 100)
            }
        }

        SpActionButton {
            quiet: false
            text: root.hasMask ? qsTr("Release") : qsTr("Make Mask")
            tip: root.hasMask ? qsTr("Release Opacity Mask")
                              : qsTr("Make an opacity mask from the frontmost selected object")
            enabled: root.hasMask || Session.selectionCount >= 2
            onClicked: root.hasMask ? Session.releaseOpacityMask() : Session.makeOpacityMask()
        }
        Flow {
            Layout.fillWidth: true
            spacing: 12
            SpCheckBox {
                text: qsTr("Clip")
                enabled: root.hasMask
                checked: root.info.maskClip ?? true
                onClicked: Session.setMaskClip(checked)
            }
            SpCheckBox {
                text: qsTr("Invert Mask")
                enabled: root.hasMask
                checked: root.info.maskInvert ?? false
                onClicked: Session.setMaskInvert(checked)
            }
        }

        SpCheckBox {
            text: qsTr("Isolate Blending")
            enabled: root.info.hasGroup ?? false
            checked: root.info.isolated ?? false
            onClicked: Session.setIsolated(checked)
        }
    }
}
