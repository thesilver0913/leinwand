// SPDX-License-Identifier: GPL-3.0-or-later
// Character panel (spec 7.2): font, style, size, leading, kerning,
// tracking, scaling, baseline shift and rotation of the selected text (the
// text selection while typing), or of new text when nothing is selected.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property var cs: Session.characterStyle
    readonly property var families: Session.fontFamilies
    readonly property var styles: Session.fontStyles(cs.family ?? "")

    ColumnLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 6
        enabled: Session.hasDocument

        SpPicker {
            Layout.fillWidth: true
            model: root.families
            currentIndex: root.cs.familyMixed ? -1 : root.families.indexOf(root.cs.family ?? "")
            // A missing font keeps its name (spec 5.2), marked as missing.
            displayText: root.cs.familyMixed ? "—"
                       : root.cs.missing ? qsTr("%1 (missing)").arg(root.cs.family) : (root.cs.family ?? "")
            onActivated: index => Session.setFont(root.families[index], root.cs.style ?? "")
        }
        SpPicker {
            Layout.fillWidth: true
            model: root.styles
            currentIndex: root.cs.styleMixed ? -1 : root.styles.indexOf(root.cs.style ?? "")
            displayText: root.cs.styleMixed ? "—" : (root.cs.style ?? "")
            onActivated: index => Session.setFont(root.cs.family, root.styles[index])
        }

        GridLayout {
            columns: 2
            columnSpacing: 6
            rowSpacing: 6

            SpLabel { text: qsTr("Size") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.cs.size ?? 12
                mixed: root.cs.sizeMixed ?? false
                minimum: 0.1
                maximum: 1296
                onCommitted: v => Session.setCharacterValue("size", v)
            }
            SpLabel { text: qsTr("Leading") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.cs.leading ?? 21
                // Auto leading shows its value in parentheses, as in Illustrator.
                mixed: (root.cs.leadingMixed ?? false) || (root.cs.autoLeading ?? false)
                placeholderText: root.cs.leadingMixed ? "—"
                               : qsTr("Auto") + " (" + Number(root.cs.leading ?? 0).toFixed(1) + " pt)"
                minimum: 0
                maximum: 5000
                onCommitted: v => Session.setCharacterValue("leading", v)
            }

            SpLabel { text: qsTr("Kerning") }
            SpPicker {
                Layout.preferredWidth: 110
                model: [qsTr("Metrics"), qsTr("None")]
                currentIndex: root.cs.kerningMixed ? -1 : (root.cs.kerning ?? 0)
                displayText: root.cs.kerningMixed ? "—" : currentText
                onActivated: index => Session.setCharacterValue("kerning", index)
            }
            SpLabel { text: qsTr("Tracking") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.cs.tracking ?? 0
                mixed: root.cs.trackingMixed ?? false
                unit: ""
                decimals: 0
                step: 10
                minimum: -1000
                maximum: 10000
                onCommitted: v => Session.setCharacterValue("tracking", v)
            }

            SpLabel { text: qsTr("Vertical Scale") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: (root.cs.verticalScale ?? 1) * 100
                mixed: root.cs.verticalScaleMixed ?? false
                unit: "%"
                decimals: 1
                minimum: 1
                maximum: 10000
                onCommitted: v => Session.setCharacterValue("verticalScale", v / 100)
            }
            SpLabel { text: qsTr("Horizontal Scale") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: (root.cs.horizontalScale ?? 1) * 100
                mixed: root.cs.horizontalScaleMixed ?? false
                unit: "%"
                decimals: 1
                minimum: 1
                maximum: 10000
                onCommitted: v => Session.setCharacterValue("horizontalScale", v / 100)
            }

            SpLabel { text: qsTr("Baseline Shift") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.cs.baselineShift ?? 0
                mixed: root.cs.baselineShiftMixed ?? false
                onCommitted: v => Session.setCharacterValue("baselineShift", v)
            }
            SpLabel { text: qsTr("Rotation") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.cs.rotation ?? 0
                mixed: root.cs.rotationMixed ?? false
                unit: "°"
                decimals: 1
                minimum: -360
                maximum: 360
                onCommitted: v => Session.setCharacterValue("rotation", v)
            }
        }
    }
}
