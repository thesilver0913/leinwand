// SPDX-License-Identifier: GPL-3.0-or-later
// Live shapes (spec 4.1, "ライブシェイプ"): parameters plus a transform; the
// path is generated when needed. Each shape is defined around its centre in
// its own coordinates, and `ShapeObject::transform` places it.
#pragma once

#include <array>
#include <variant>

#include "core/path.h"
#include "core/types.h"

namespace leinwand::core {

enum class CornerKind { kRound, kInvertedRound, kChamfer };

struct Corner {
  double radius = 0.0;
  CornerKind kind = CornerKind::kRound;
  friend bool operator==(const Corner&, const Corner&) = default;
};

struct RectangleShape {
  double width = 0.0;
  double height = 0.0;
  std::array<Corner, 4> corners;  // Top-left, top-right, bottom-right, bottom-left.
  friend bool operator==(const RectangleShape&, const RectangleShape&) = default;
};

// Angles in degrees, counter-clockwise on screen from 3 o'clock. Equal
// angles (or a full turn) mean a whole ellipse; otherwise a pie.
struct EllipseShape {
  double width = 0.0;
  double height = 0.0;
  double pie_start = 0.0;
  double pie_end = 360.0;
  friend bool operator==(const EllipseShape&, const EllipseShape&) = default;
};

// A regular polygon with a vertex pointing up.
struct PolygonShape {
  int sides = 6;
  double radius = 0.0;  // Centre to vertex.
  double corner_radius = 0.0;
  friend bool operator==(const PolygonShape&, const PolygonShape&) = default;
};

// A star with a point pointing up.
struct StarShape {
  int points = 5;
  double outer_radius = 0.0;
  double inner_radius = 0.0;
  friend bool operator==(const StarShape&, const StarShape&) = default;
};

// A horizontal segment centred on the origin; the transform gives its angle.
struct LineShape {
  double length = 0.0;
  friend bool operator==(const LineShape&, const LineShape&) = default;
};

using ShapeParams = std::variant<RectangleShape, EllipseShape, PolygonShape, StarShape, LineShape>;

// The path of a shape in its own coordinates (before the shape's transform).
PathData ShapePath(const ShapeParams& shape);

// A closed polygon through `vertices` with each corner rounded, inverted or
// chamfered as given (radii are clamped to fit the edges).
PathData PolygonWithCorners(const std::vector<Point>& vertices, const std::vector<Corner>& corners);

}  // namespace leinwand::core
