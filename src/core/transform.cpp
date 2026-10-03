// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/transform.h"

#include <cmath>
#include <optional>
#include <variant>

#include "core/gradient.h"

namespace leinwand::core {

namespace {

// A linear map split as rotation * [sx shear; 0 sy] (QR decomposition).
struct Decomposition {
  double angle = 0.0;
  double sx = 1.0;
  double sy = 1.0;  // Negative when the map reflects.
  double shear = 0.0;
};

Decomposition Decompose(const Matrix& m) {
  Decomposition d;
  d.sx = std::hypot(m.a, m.b);
  d.angle = std::atan2(m.b, m.a);
  const double c = std::cos(d.angle), s = std::sin(d.angle);
  d.shear = c * m.c + s * m.d;
  d.sy = -s * m.c + c * m.d;
  return d;
}

// Rotation and translation (and a reflection when sy < 0), without scale.
Matrix Placement(const Matrix& m, const Decomposition& d) {
  Matrix placement = Matrix::Translate(m.e, m.f) * Matrix::Rotate(d.angle);
  if (d.sy < 0) placement = placement * Matrix::Scale(1, -1);
  return placement;
}

// The shape after `combined` (new transform times old placement), or nothing
// if it cannot stay a live shape.
std::optional<ShapeObject> TransformedShape(const ShapeObject& shape, const Matrix& combined) {
  const Decomposition d = Decompose(combined);
  const double sx = d.sx, sy = std::abs(d.sy);
  const double size = std::max(sx, sy);
  const bool sheared = std::abs(d.shear) > 1e-9 * std::max(size, 1.0);
  const bool uniform = std::abs(sx - sy) <= 1e-9 * std::max(size, 1.0);

  ShapeObject result = shape;
  result.transform = Placement(combined, d);
  bool live = true;
  std::visit(
      [&](auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, LineShape>) {
          // Any affine map keeps a line straight: refit it to the mapped ends.
          const Point a = combined.Map({-s.length / 2, 0}), b = combined.Map({s.length / 2, 0});
          s.length = std::hypot(b.x - a.x, b.y - a.y);
          const Point mid = (a + b) * 0.5;
          result.transform =
              Matrix::Translate(mid.x, mid.y) * Matrix::Rotate(std::atan2(b.y - a.y, b.x - a.x));
        } else if (sheared) {
          live = false;
        } else if constexpr (std::is_same_v<T, RectangleShape> || std::is_same_v<T, EllipseShape>) {
          // Corner radii keep their size, as with Illustrator's "Scale
          // Corners" off.
          s.width *= sx;
          s.height *= sy;
        } else if constexpr (std::is_same_v<T, PolygonShape>) {
          if (!uniform) live = false;
          s.radius *= sx;
        } else if constexpr (std::is_same_v<T, StarShape>) {
          if (!uniform) live = false;
          s.outer_radius *= sx;
          s.inner_radius *= sx;
        }
      },
      result.shape);
  if (!live) return std::nullopt;
  return result;
}

}  // namespace

PathData Transformed(const PathData& path, const Matrix& matrix) {
  PathData result = path;
  for (auto& anchor : result.anchors) {
    anchor.position = matrix.Map(anchor.position);
    anchor.handle_in = matrix.MapVector(anchor.handle_in);
    anchor.handle_out = matrix.MapVector(anchor.handle_out);
  }
  return result;
}

namespace {

// Gradients are in the path's coordinates, so they move with it.
void TransformGradients(Appearance& appearance, const Matrix& matrix) {
  for (AppearanceItem& item : appearance) {
    if (auto* fill = std::get_if<Fill>(&item); fill && fill->gradient) {
      fill->gradient = Transformed(*fill->gradient, matrix);
    } else if (auto* stroke = std::get_if<Stroke>(&item); stroke && stroke->gradient) {
      stroke->gradient = Transformed(*stroke->gradient, matrix);
    }
  }
}

}  // namespace

namespace {

// The opacity mask moves with its object.
void TransformMask(ObjectCommon& common, const Matrix& matrix) {
  if (!common.mask || !common.mask->art) return;
  OpacityMask mask = *common.mask;
  mask.art = Transformed(mask.art, matrix);
  common.mask = std::make_shared<const OpacityMask>(std::move(mask));
}

}  // namespace

ObjectPtr Transformed(const ObjectPtr& object, const Matrix& matrix) {
  if (matrix.IsIdentity()) return object;
  return std::visit(
      [&](const auto& o) -> ObjectPtr {
        using T = std::decay_t<decltype(o)>;
        T copy = o;
        if constexpr (std::is_same_v<T, PathObject>) {
          copy.path = Transformed(o.path, matrix);
          TransformGradients(copy.common.appearance, matrix);
        } else if constexpr (std::is_same_v<T, CompoundPathObject>) {
          for (auto& subpath : copy.subpaths) subpath = Transformed(subpath, matrix);
          TransformGradients(copy.common.appearance, matrix);
        } else if constexpr (std::is_same_v<T, ShapeObject>) {
          if (auto shape = TransformedShape(o, matrix * o.transform)) {
            TransformGradients(shape->common.appearance, matrix);
            TransformMask(shape->common, matrix);
            return MakeObject(*shape);
          }
          // Shear or uneven scale: becomes a plain path (spec 4.1).
          return Transformed(Expanded(object), matrix);
        } else {
          copy.transform = matrix * o.transform;
        }
        TransformMask(copy.common, matrix);
        return MakeObject(std::move(copy));
      },
      object->base());
}

}  // namespace leinwand::core
