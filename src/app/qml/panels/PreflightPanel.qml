// SPDX-License-Identifier: GPL-3.0-or-later
// Preflight panel (spec 7.5, "入稿チェック"): what print shops commonly
// reject, with the objects concerned (click a row to select them). The
// checks and the thinnest stroke allowed can be saved as presets, one per
// print shop. Phase 2 checks what an RGB document can tell.
import QtQuick
import QtQuick.Layouts
import Leinwand

Item {
    id: root
    // In editor::PreflightCheck order.
    readonly property var names: [qsTr("Live text (not outlined)"), qsTr("Empty text"),
                                  qsTr("Stray points"), qsTr("Strokes thinner than the minimum"),
                                  qsTr("Artwork short of the bleed"), qsTr("Hidden objects"),
                                  qsTr("Locked objects")]
    property var rows: []
    property bool stale: true

    function check() {
        rows = Session.hasDocument ? Session.preflight(Preferences.preflightMinStroke,
                                                       Preferences.preflightChecks) : [];
        stale = false;
    }
    function setCheck(index, on) {
        const checks = Preferences.preflightChecks.slice();
        checks[index] = on;
        Preferences.preflightChecks = checks;
        check();
    }

    // Kept current while the panel is shown.
    Timer {
        id: refresh
        interval: 400
        onTriggered: if (root.visible) root.check()
    }
    Connections {
        target: Session
        function onDocumentChanged() { refresh.restart(); }
    }
    onVisibleChanged: if (visible) check()

    // Results first; the settings fold away below them.
    property bool showSettings: false

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 20
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            x: 10
            y: 10
            width: parent.width - 20
            spacing: 8
            enabled: Session.hasDocument

            SpLabel {
                visible: root.rows.length === 0
                text: qsTr("No problems found.")
            }
            Repeater {
                model: root.rows
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 26
                    radius: Spectrum.cornerRadiusSmallDefault
                    color: area.containsMouse ? Spectrum.gray200 : "transparent"
                    RowLayout {
                        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                        SpLabel {
                            Layout.fillWidth: true
                            text: root.names[modelData.check]
                            subdued: false
                            elide: Text.ElideRight
                        }
                        SpLabel { text: modelData.ids.length }
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        // Hidden and locked objects cannot be selected.
                        onClicked: Session.selectReported(modelData.ids)
                    }
                }
            }

            SpActionButton {
                text: (root.showSettings ? "▾ " : "▸ ") + qsTr("Checks and Presets")
                onClicked: root.showSettings = !root.showSettings
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.showSettings
                spacing: 8

                RowLayout {
                    spacing: 6
                    SpPicker {
                        id: presets
                        Layout.fillWidth: true
                        model: [qsTr("Custom")].concat(Preferences.preflightPresets.map(p => p.name))
                        currentIndex: 0
                        onActivated: index => {
                            if (index === 0)
                                return;
                            const preset = Preferences.preflightPresets[index - 1];
                            Preferences.preflightMinStroke = preset.minStroke;
                            Preferences.preflightChecks = preset.checks;
                            root.check();
                        }
                    }
                    SpActionButton {
                        iconName: "Delete"
                        tip: qsTr("Delete Preset")
                        enabled: presets.currentIndex > 0
                        onClicked: {
                            const list = Preferences.preflightPresets.slice();
                            list.splice(presets.currentIndex - 1, 1);
                            Preferences.preflightPresets = list;
                            presets.currentIndex = 0;
                        }
                    }
                }
                RowLayout {
                    spacing: 6
                    SpTextField {
                        id: presetName
                        Layout.fillWidth: true
                        placeholderText: qsTr("Preset name (print shop)")
                    }
                    SpActionButton {
                        quiet: false
                        text: qsTr("Save Preset")
                        enabled: presetName.text.trim() !== ""
                        onClicked: {
                            const name = presetName.text.trim();
                            const list = Preferences.preflightPresets.filter(p => p.name !== name);
                            list.push({ name: name, minStroke: Preferences.preflightMinStroke,
                                        checks: Preferences.preflightChecks });
                            Preferences.preflightPresets = list;
                            presets.currentIndex = list.findIndex(p => p.name === name) + 1;
                            presetName.text = "";
                        }
                    }
                }

                Repeater {
                    model: root.names
                    RowLayout {
                        required property int index
                        required property var modelData
                        spacing: 6
                        SpCheckBox {
                            text: modelData
                            checked: Preferences.preflightChecks[index] ?? true
                            onClicked: root.setCheck(index, checked)
                        }
                        SpNumberField {
                            visible: index === 3
                            Layout.preferredWidth: 80
                            value: Preferences.preflightMinStroke
                            unit: " mm"
                            decimals: 2
                            minimum: 0
                            maximum: 10
                            onCommitted: v => {
                                Preferences.preflightMinStroke = v;
                                root.check();
                            }
                        }
                    }
                }

            }
        }
    }
}
