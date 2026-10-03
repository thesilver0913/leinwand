// SPDX-License-Identifier: GPL-3.0-or-later
// A number field as panels need it (spec 7.2): units and arithmetic
// ("10mm", "100+20"), Up/Down to step (Shift: ten times), empty when the
// selected objects differ. Commits on Enter or when focus leaves.
import QtQuick
import Leinwand

SpTextField {
    id: root
    property real value
    property bool mixed: false
    property int decimals: 2
    // "pt" for lengths (any unit may be typed), else a suffix shown after the
    // number ("°", "%", "") with plain arithmetic.
    property string unit: "pt"
    property real step: 1
    property real minimum: -1e9
    property real maximum: 1e9
    signal committed(real value)

    function format(v) {
        let text = Number(v).toFixed(decimals);
        if (text.indexOf(".") >= 0)
            text = text.replace(/0+$/, "").replace(/\.$/, "");
        return unit === "" ? text : text + (unit === "pt" ? " pt" : unit);
    }
    function commit(v) {
        const clamped = Math.min(maximum, Math.max(minimum, v));
        if (mixed || clamped !== value) committed(clamped);
        text = Qt.binding(() => mixed ? "" : format(value));
    }

    text: mixed ? "" : format(value)
    placeholderText: mixed ? "—" : ""
    horizontalAlignment: TextInput.AlignLeft

    onEditingFinished: {
        // A field's own suffix (" px", " min") may be typed back.
        let entry = text.trim();
        if (unit !== "pt" && unit.trim() !== "" && entry.endsWith(unit.trim()))
            entry = entry.slice(0, -unit.trim().length);
        const v = unit === "pt" ? Session.evaluateLength(entry) : Session.evaluateNumber(entry);
        if (isNaN(v)) {
            text = Qt.binding(() => mixed ? "" : format(value));
            return;
        }
        commit(v);
    }
    Keys.onUpPressed: event => commit((mixed ? 0 : value) + step * (event.modifiers & Qt.ShiftModifier ? 10 : 1))
    Keys.onDownPressed: event => commit((mixed ? 0 : value) - step * (event.modifiers & Qt.ShiftModifier ? 10 : 1))
    Keys.onEscapePressed: {
        text = Qt.binding(() => mixed ? "" : format(value));
        focus = false;
    }
}
