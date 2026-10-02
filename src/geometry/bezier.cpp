// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/bezier.h"

#include <cmath>
#include <variant>

namespace leinwand::geometry {

using core::Point;
using core::Rect;

namespace {

// Roots in (0, 1) of the derivative of one coordinate of a cubic Bézier.
// B'(t)/3 = a t^2 + b t + c with the coefficients below.
template <typename F>
void ForEachExtremum(double p0, double p1, double p2, double p3, F&& f) {
  const double a = -p0 + 3 * p1 - 3 * p2 + p3;
  const double b = 2 * (p0 - 2 * p1 + p2);
  const double c = p1 - p0;
  constexpr double kEpsilon = 1e-12;
  auto emit = [&](double t) {
    if (t > 0.0 && t < 1.0) f(t);
  };
  if (std::abs(a) < kEpsilon) {
    if (std::abs(b) > kEpsilon) emit(-c / b);  // Derivative is linear.
    return;
  }
  const double discriminant = b * b - 4 * a * c;
  if (discriminant < 0) return;
  const double root = std::sqrt(discriminant);
  emit((-b + root) / (2 * a));
  emit((-b - root) / (2 * a));
}

}  // namespace

Point CubicBezier::Evaluate(double t) const {
  const double u = 1.0 - t;
  return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
}

Rect CubicBezier::Bounds() const {
  Rect bounds = Rect::FromPoint(p0).Union(p3);
  auto include = [&](double t) { bounds = bounds.Union(Evaluate(t)); };
  ForEachExtremum(p0.x, p1.x, p2.x, p3.x, include);
  ForEachExtremum(p0.y, p1.y, p2.y, p3.y, include);
  return bounds;
}

CubicBezier SegmentAt(const core::PathData& path, int index) {
  const auto& from = path.anchors[index];
  const auto& to = path.anchors[(index + 1) % path.anchors.size()];
  return {from.position, from.out_point(), to.in_point(), to.position};
}

Rect Bounds(const core::PathData& path) {
  if (path.anchors.empty()) return {};
  Rect bounds = Rect::FromPoint(path.anchors[0].position);
  for (int i = 0; i < path.segment_count(); ++i) bounds = bounds.Union(SegmentAt(path, i).Bounds());
  return bounds;
}

core::PathData Transform(const core::PathData& path, const core::Matrix& matrix) {
  core::PathData result = path;
  for (auto& anchor : result.anchors) {
    anchor.position = matrix.Map(anchor.position);
    anchor.handle_in = matrix.MapVector(anchor.handle_in);
    anchor.handle_out = matrix.MapVector(anchor.handle_out);
  }
  return result;
}

Rect MapRect(const Rect& rect, const core::Matrix& matrix) {
  if (!rect.IsValid()) return {};
  return Rect::FromPoint(matrix.Map({rect.left, rect.top}))
      .Union(matrix.Map({rect.right, rect.top}))
      .Union(matrix.Map({rect.right, rect.bottom}))
      .Union(matrix.Map({rect.left, rect.bottom}));
}

Rect Bounds(const core::Object& object) {
  if (const auto* path = std::get_if<core::PathObject>(&object)) return Bounds(path->path);
  if (const auto* compound = std::get_if<core::CompoundPathObject>(&object)) {
    Rect bounds;
    for (const auto& subpath : compound->subpaths) bounds = bounds.Union(Bounds(subpath));
    return bounds;
  }
  const auto& group = std::get<core::GroupObject>(object);
  Rect bounds;
  for (const auto& child : group.children) bounds = bounds.Union(Bounds(*child));
  return MapRect(bounds, group.transform);
}

}  // namespace leinwand::geometry
