// SPDX-License-Identifier: GPL-3.0-or-later
// Gradients (spec 7, "グラデーション"): building, reshaping and editing the
// stops of a core::Gradient.
#pragma once

#include "core/appearance.h"
#include "core/types.h"

namespace leinwand::core {

// A two-stop gradient across `bounds`, as Illustrator applies one: linear
// from the left edge to the right edge through the middle; radial from the
// centre out to half the longer side.
Gradient DefaultGradient(GradientType type, const Rect& bounds, const Color& from, const Color& to);

// The angle of the gradient's axis in degrees, counter-clockwise on screen
// (Illustrator's Gradient panel; the document's y axis points down).
double GradientAngle(const Gradient& gradient);
// The same gradient turned to `degrees` about its start, the length kept.
Gradient WithAngle(Gradient gradient, double degrees);

// The gradient with its points mapped by `matrix` (an object being moved,
// scaled or rotated). The radial aspect is kept.
Gradient Transformed(const Gradient& gradient, const Matrix& matrix);

// Stop editing. Stops stay sorted by offset; at least two always remain.
// A new stop takes the color of the gradient where it is added (between two
// RGB stops) or of the stop before it. Returns the new stop's index.
int AddStop(Gradient& gradient, double offset);
void RemoveStop(Gradient& gradient, int index);
// Moves a stop to `offset` (0..1); returns its index after re-sorting.
int MoveStop(Gradient& gradient, int index, double offset);

}  // namespace leinwand::core
