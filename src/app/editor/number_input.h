// SPDX-License-Identifier: GPL-3.0-or-later
// What panel number fields accept (spec 7.2): numbers with units and the
// four operations, e.g. "10mm", "2in", "100+20", "50/2", "(1+2)*3mm".
#pragma once

#include <optional>
#include <string_view>

namespace leinwand::editor {

enum class Unit { kPoint, kPixel, kMillimeter, kCentimeter, kInch, kPica };

// Points per unit (a pixel is a point, as in Illustrator).
double PointsPer(Unit unit);

// Evaluates a length entry and returns it in points. Numbers without a unit
// are in `unit` (the document's unit); numbers with one are converted, so
// "100+20mm" in a point field adds 20 mm to 100 pt and "10mm*2" doubles
// 10 mm. Null for anything that does not parse, or division by zero.
std::optional<double> EvaluateLength(std::string_view text, Unit unit = Unit::kPoint);

// Evaluates a unitless entry (angles, counts, percentages); a trailing "°"
// or "%" is allowed and ignored.
std::optional<double> EvaluateNumber(std::string_view text);

}  // namespace leinwand::editor
