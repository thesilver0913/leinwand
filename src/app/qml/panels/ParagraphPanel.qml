// SPDX-License-Identifier: GPL-3.0-or-later
// Paragraph panel (spec 7.2), stage 1: alignment, indents and the space
// before and after paragraphs. Point text lines up on its anchor; the
// justified alignments come with area text (stage 2).
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    readonly property var ps: Session.paragraphStyle

    ColumnLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 8
        enabled: Session.hasDocument

        SpSegmented {
            current: root.ps.alignMixed ? -1 : (root.ps.align ?? 0)
            options: [{ icon: "AlignLeft", tip: qsTr("Align Left") },
                      { icon: "AlignCenterHorizontal", tip: qsTr("Align Center") },
                      { icon: "AlignRight", tip: qsTr("Align Right") }]
            onChosen: i => Session.setParagraphValue("align", i)
        }

        GridLayout {
            columns: 2
            columnSpacing: 6
            rowSpacing: 6

            SpLabel { text: qsTr("Left Indent") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.ps.leftIndent ?? 0
                mixed: root.ps.leftIndentMixed ?? false
                onCommitted: v => Session.setParagraphValue("leftIndent", v)
            }
            SpLabel { text: qsTr("Right Indent") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.ps.rightIndent ?? 0
                mixed: root.ps.rightIndentMixed ?? false
                onCommitted: v => Session.setParagraphValue("rightIndent", v)
            }
            SpLabel { text: qsTr("First Line") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.ps.firstLineIndent ?? 0
                mixed: root.ps.firstLineIndentMixed ?? false
                onCommitted: v => Session.setParagraphValue("firstLineIndent", v)
            }
            SpLabel { text: qsTr("Space Before") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.ps.spaceBefore ?? 0
                mixed: root.ps.spaceBeforeMixed ?? false
                onCommitted: v => Session.setParagraphValue("spaceBefore", v)
            }
            SpLabel { text: qsTr("Space After") }
            SpNumberField {
                Layout.preferredWidth: 110
                value: root.ps.spaceAfter ?? 0
                mixed: root.ps.spaceAfterMixed ?? false
                onCommitted: v => Session.setParagraphValue("spaceAfter", v)
            }
        }
    }
}
