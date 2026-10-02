// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/hit_test.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <variant>

#include "geometry/bezier.h"

namespace leinwand::geometry {

using core::Point;
using core::Rect;

namespace {

double Length(Point v) { return std::hypot(v.x, v.y); }

double DistanceToSegment(Point p, Point a, Point b) {
  const Point ab = b - a;
  const double length2 = ab.x * ab.x + ab.y * ab.y;
  double t = length2 > 0 ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / length2 : 0.0;
  t = std::clamp(t, 0.0, 1.0);
  return Length(p - (a + ab * t));
}

// Liang-Barsky: does segment ab cross (or lie in) the rectangle?
bool SegmentTouchesRect(Point a, Point b, const Rect& r) {
  double t0 = 0.0, t1 = 1.0;
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double p[] = {-dx, dx, -dy, dy};
  const double q[] = {a.x - r.left, r.right - a.x, a.y - r.top, r.bottom - a.y};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0) {
      if (q[i] < 0) return false;
      continue;
    }
    const double t = q[i] / p[i];
    if (p[i] < 0) {
      t0 = std::max(t0, t);
    } else {
      t1 = std::min(t1, t);
    }
    if (t0 > t1) return false;
  }
  return true;
}

// Calls f(a, b) for every flattened segment of every subpath.
template <typename F>
void ForEachEdge(const std::vector<core::PathData>& subpaths, double tolerance, bool close_open,
                 F&& f) {
  for (const auto& subpath : subpaths) {
    const std::vector<Point> points = Flatten(subpath, tolerance);
    for (std::size_t i = 1; i < points.size(); ++i) f(points[i - 1], points[i]);
    // Open paths are filled as if closed, as in Illustrator.
    if (close_open && !subpath.closed && points.size() > 2) f(points.back(), points.front());
  }
}

// Half the widest stroke, and whether anything is filled.
void PaintExtent(const core::Object& object, double* stroke_reach, bool* filled) {
  *stroke_reach = 0.0;
  *filled = false;
  for (const auto& item : core::CommonOf(object).appearance) {
    if (std::holds_alternative<core::Fill>(item)) {
      *filled = true;
    } else {
      const auto& stroke = std::get<core::Stroke>(item);
      // Inside/outside strokes reach a full width to one side.
      const double reach =
          stroke.align == core::StrokeAlign::kCenter ? stroke.width / 2 : stroke.width;
      *stroke_reach = std::max(*stroke_reach, reach);
    }
  }
}

// How much a transform scales lengths, roughly.
double ScaleOf(const core::Matrix& m) { return std::sqrt(std::abs(m.Determinant())); }

std::optional<std::string> HitObject(const core::Object& object, Point p, double tolerance) {
  const core::ObjectCommon& common = core::CommonOf(object);
  if (!common.visible || common.locked) return std::nullopt;

  if (const auto* group = std::get_if<core::GroupObject>(&object)) {
    const auto inverse = group->transform.Inverted();
    if (!inverse) return std::nullopt;
    const Point local = inverse->Map(p);
    const double local_tolerance = tolerance / std::max(ScaleOf(group->transform), 1e-9);
    auto end = group->children.rend();
    if (group->clipped && !group->children.empty()) {
      const core::Object& clip = *group->children.back();
      if (!FillContains(core::OutlineOf(clip), core::FillRuleOf(clip), local, local_tolerance))
        return std::nullopt;
    }
    auto it = group->children.rbegin();
    if (group->clipped && it != end) ++it;  // Skip the clip path itself.
    for (; it != end; ++it) {
      if (auto hit = HitObject(**it, local, local_tolerance)) return hit;
    }
    return std::nullopt;
  }

  double stroke_reach;
  bool filled;
  PaintExtent(object, &stroke_reach, &filled);
  // Cheap rejection before flattening any curves.
  if (!Bounds(object).Outset(stroke_reach + tolerance).Contains(p)) return std::nullopt;
  const auto subpaths = core::OutlineOf(object);
  const double flatness = std::max(tolerance / 4, 1e-3);
  if (filled && FillContains(subpaths, core::FillRuleOf(object), p, flatness)) return common.id;
  for (const auto& subpath : subpaths) {
    if (DistanceToOutline(subpath, p, flatness) <= stroke_reach + tolerance) return common.id;
  }
  return std::nullopt;
}

bool Touches(const core::Object& object, const Rect& rect, double tolerance) {
  const core::ObjectCommon& common = core::CommonOf(object);
  if (!common.visible || common.locked) return false;
  if (const auto* group = std::get_if<core::GroupObject>(&object)) {
    const auto inverse = group->transform.Inverted();
    if (!inverse) return false;
    const Rect local = MapRect(rect, *inverse);  // Conservative under rotation.
    const double local_tolerance = tolerance / std::max(ScaleOf(group->transform), 1e-9);
    const std::size_t count = group->children.size() - (group->clipped && !group->children.empty());
    for (std::size_t i = 0; i < count; ++i) {
      if (Touches(*group->children[i], local, local_tolerance)) return true;
    }
    return false;
  }
  if (!Bounds(object).Intersects(rect)) return false;
  bool touched = false;
  ForEachEdge(core::OutlineOf(object), tolerance, false,
              [&](Point a, Point b) { touched = touched || SegmentTouchesRect(a, b, rect); });
  if (touched) return true;
  double stroke_reach;
  bool filled;
  PaintExtent(object, &stroke_reach, &filled);
  const Point centre{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
  return filled &&
         FillContains(core::OutlineOf(object), core::FillRuleOf(object), centre, tolerance);
}

// Calls f for each object directly in the layer or its sublayers, until f
// returns true.
void ForEachTopLevel(const core::Layer& layer, bool front_to_back,
                     const std::function<bool(const core::Object&)>& f) {
  if (!layer.visible || layer.locked) return;
  auto visit = [&](const core::LayerChild& child) {
    if (const auto* object = std::get_if<core::ObjectPtr>(&child)) return f(**object);
    bool stop = false;
    ForEachTopLevel(*std::get<core::LayerPtr>(child), front_to_back, [&](const core::Object& o) {
      stop = f(o);
      return stop;
    });
    return stop;
  };
  if (front_to_back) {
    for (auto it = layer.children.rbegin(); it != layer.children.rend(); ++it) {
      if (visit(*it)) return;
    }
  } else {
    for (const auto& child : layer.children) {
      if (visit(child)) return;
    }
  }
}

}  // namespace

std::vector<Point> Flatten(const core::PathData& path, double tolerance) {
  std::vector<Point> points;
  if (path.anchors.empty()) return points;
  points.push_back(path.anchors[0].position);
  for (int i = 0; i < path.segment_count(); ++i) {
    const CubicBezier c = SegmentAt(path, i);
    if (c.p1 == c.p0 && c.p2 == c.p3) {  // No handles: a straight line.
      points.push_back(c.p3);
      continue;
    }
    // Wang's formula: segments needed for a cubic to stay within tolerance.
    const double d = std::max(Length(c.p0 - c.p1 * 2 + c.p2), Length(c.p1 - c.p2 * 2 + c.p3));
    const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(0.75 * d / tolerance))), 1, 256);
    for (int k = 1; k <= n; ++k) points.push_back(c.Evaluate(static_cast<double>(k) / n));
  }
  return points;
}

bool FillContains(const std::vector<core::PathData>& subpaths, core::FillRule rule, Point p,
                  double tolerance) {
  int winding = 0;
  ForEachEdge(subpaths, tolerance, true, [&](Point a, Point b) {
    const double side = (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
    if (a.y <= p.y && b.y > p.y && side > 0) ++winding;
    if (b.y <= p.y && a.y > p.y && side < 0) --winding;
  });
  return rule == core::FillRule::kEvenOdd ? (winding % 2 != 0) : winding != 0;
}

double DistanceToOutline(const core::PathData& path, Point p, double tolerance) {
  const std::vector<Point> points = Flatten(path, tolerance);
  if (points.size() == 1) return Length(p - points[0]);
  double best = std::numeric_limits<double>::infinity();
  for (std::size_t i = 1; i < points.size(); ++i) {
    best = std::min(best, DistanceToSegment(p, points[i - 1], points[i]));
  }
  return best;
}

std::optional<Hit> HitTest(const core::Document& document, Point p, double tolerance) {
  std::optional<Hit> result;
  for (auto layer = document.layers.rbegin(); layer != document.layers.rend() && !result; ++layer) {
    ForEachTopLevel(**layer, true, [&](const core::Object& object) {
      if (auto leaf = HitObject(object, p, tolerance)) {
        result = Hit{core::CommonOf(object).id, *leaf};
        return true;
      }
      return false;
    });
  }
  return result;
}

core::IdSet ObjectsTouching(const core::Document& document, const Rect& rect, double tolerance) {
  core::IdSet ids;
  for (const auto& layer : document.layers) {
    ForEachTopLevel(*layer, false, [&](const core::Object& object) {
      if (Touches(object, rect, tolerance)) ids.insert(core::CommonOf(object).id);
      return false;
    });
  }
  return ids;
}

}  // namespace leinwand::geometry
