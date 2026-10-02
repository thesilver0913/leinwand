// SPDX-License-Identifier: GPL-3.0-or-later
// Cubic Bézier math on the document model's paths.
#pragma once

#include "core/object.h"
#include "core/path.h"
#include "core/types.h"

namespace leinwand::geometry {

struct CubicBezier {
  core::Point p0, p1, p2, p3;

  core::Point Evaluate(double t) const;
  // Tight bounds: endpoints plus the curve's extrema, not the control polygon.
  core::Rect Bounds() const;
};

// Segment i of a path (see core::PathData for the numbering).
CubicBezier SegmentAt(const core::PathData& path, int index);

// Tight bounds of the path's geometry, ignoring strokes. An isolated point
// gives a degenerate rect; an empty path gives an invalid one.
core::Rect Bounds(const core::PathData& path);

// Bounds a rect's image under `matrix` (the four corners); exact for
// scale and translation, conservative under rotation.
core::Rect MapRect(const core::Rect& rect, const core::Matrix& matrix);

// Geometric bounds of an object in its parent's coordinates, ignoring
// strokes and effects. Groups include their transform; hidden children count.
core::Rect Bounds(const core::Object& object);

}  // namespace leinwand::geometry
