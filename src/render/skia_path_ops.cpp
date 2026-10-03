// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/skia_path_ops.h"

#include <cmath>

#include "geometry/bezier.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathIter.h"
#include "include/pathops/SkPathOps.h"

namespace leinwand::render {

namespace {

using core::Anchor;
using core::PathData;
using core::Point;
using geometry::Region;

// The centre of everything taking part, so coordinates near it convert to
// float without losing the fine detail (M8 check 1).
Point Centre(std::initializer_list<const Region*> regions) {
  core::Rect bounds;
  for (const Region* region : regions) {
    for (const PathData& path : region->subpaths) {
      if (!path.anchors.empty()) bounds = bounds.Union(geometry::Bounds(path));
    }
  }
  if (!bounds.IsValid()) return {};
  return {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
}

SkPoint ToSk(Point p, Point centre) {
  return SkPoint::Make(static_cast<float>(p.x - centre.x), static_cast<float>(p.y - centre.y));
}
Point FromSk(SkPoint p, Point centre) { return {double(p.fX) + centre.x, double(p.fY) + centre.y}; }

SkPath ToSkPath(const Region& region, Point centre) {
  SkPathBuilder builder;
  builder.setFillType(region.fill_rule == core::FillRule::kEvenOdd ? SkPathFillType::kEvenOdd
                                                                   : SkPathFillType::kWinding);
  for (const PathData& path : region.subpaths) {
    if (path.anchors.size() < 2) continue;  // An isolated point has no area.
    builder.moveTo(ToSk(path.anchors[0].position, centre));
    // Filled areas are closed, so open subpaths get their closing segment.
    const int n = static_cast<int>(path.anchors.size());
    for (int i = 0; i < n; ++i) {
      const bool last = i == n - 1;
      if (last && !path.closed) break;
      const Anchor& a = path.anchors[size_t(i)];
      const Anchor& b = path.anchors[size_t(last ? 0 : i + 1)];
      if (a.handle_out == Point{} && b.handle_in == Point{}) {
        builder.lineTo(ToSk(b.position, centre));
      } else {
        builder.cubicTo(ToSk(a.out_point(), centre), ToSk(b.in_point(), centre),
                        ToSk(b.position, centre));
      }
    }
    builder.close();
  }
  return builder.detach();
}

// Smooth when both handles exist and point in opposite directions.
core::AnchorKind KindOf(const Anchor& a) {
  const Point in = a.handle_in, out = a.handle_out;
  const double li = std::hypot(in.x, in.y), lo = std::hypot(out.x, out.y);
  if (li == 0 || lo == 0) return core::AnchorKind::kCorner;
  const double cross = (in.x * out.y - in.y * out.x) / (li * lo);
  const double dot = (in.x * out.x + in.y * out.y) / (li * lo);
  return std::abs(cross) < 1e-3 && dot < 0 ? core::AnchorKind::kSmooth : core::AnchorKind::kCorner;
}

Region FromSkPath(const SkPath& path, Point centre) {
  Region region;
  region.fill_rule = path.getFillType() == SkPathFillType::kEvenOdd ||
                             path.getFillType() == SkPathFillType::kInverseEvenOdd
                         ? core::FillRule::kEvenOdd
                         : core::FillRule::kNonZero;
  PathData current;
  SkPoint start{}, last{};
  auto finish = [&] {
    if (current.anchors.size() >= 2) {
      // A contour ending where it began: the last anchor is the first one.
      if (last == start && current.anchors.size() > 2) {
        current.anchors.front().handle_in = current.anchors.back().handle_in;
        current.anchors.pop_back();
      }
      current.closed = true;
      for (Anchor& a : current.anchors) a.kind = KindOf(a);
      region.subpaths.push_back(std::move(current));
    }
    current = {};
  };
  auto cubic = [&](SkPoint p0, SkPoint c1, SkPoint c2, SkPoint p3) {
    Anchor& from = current.anchors.back();
    from.handle_out = FromSk(c1, centre) - FromSk(p0, centre);
    Anchor to;
    to.position = FromSk(p3, centre);
    to.handle_in = FromSk(c2, centre) - to.position;
    current.anchors.push_back(to);
    last = p3;
  };
  auto quad = [&](SkPoint p0, SkPoint c, SkPoint p2) {
    // Exact degree elevation.
    auto toward = [](SkPoint from, SkPoint to) {
      return SkPoint::Make(from.fX + (to.fX - from.fX) * 2 / 3,
                           from.fY + (to.fY - from.fY) * 2 / 3);
    };
    cubic(p0, toward(p0, c), toward(p2, c), p2);
  };
  SkPathIter iter = path.iter();
  while (auto rec = iter.next()) {
    const auto& pts = rec->fPoints;
    switch (rec->fVerb) {
      case SkPathVerb::kMove:
        finish();
        current.anchors.push_back({FromSk(pts[0], centre), {}, {}});
        start = last = pts[0];
        break;
      case SkPathVerb::kLine:
        current.anchors.push_back({FromSk(pts[1], centre), {}, {}});
        last = pts[1];
        break;
      case SkPathVerb::kQuad:
        quad(pts[0], pts[1], pts[2]);
        break;
      case SkPathVerb::kConic: {
        // Approximated by eight quadratics.
        SkPoint quads[1 + 2 * 8];
        const int count =
            SkPath::ConvertConicToQuads(pts[0], pts[1], pts[2], rec->fConicWeight, quads, 3);
        for (int i = 0; i < count; ++i) quad(quads[2 * i], quads[2 * i + 1], quads[2 * i + 2]);
        break;
      }
      case SkPathVerb::kCubic:
        cubic(pts[0], pts[1], pts[2], pts[3]);
        break;
      case SkPathVerb::kClose:
        finish();
        break;
    }
  }
  finish();
  return region;
}

SkPathOp ToSkOp(geometry::BooleanOp op) {
  switch (op) {
    case geometry::BooleanOp::kUnion:
      return kUnion_SkPathOp;
    case geometry::BooleanOp::kIntersect:
      return kIntersect_SkPathOp;
    case geometry::BooleanOp::kDifference:
      return kDifference_SkPathOp;
    case geometry::BooleanOp::kExclude:
      return kXOR_SkPathOp;
  }
  return kUnion_SkPathOp;
}

}  // namespace

std::optional<Region> SkiaPathOps::Apply(const Region& a, const Region& b,
                                         geometry::BooleanOp op) const {
  const Point centre = Centre({&a, &b});
  const std::optional<SkPath> result = Op(ToSkPath(a, centre), ToSkPath(b, centre), ToSkOp(op));
  if (!result) return std::nullopt;
  return FromSkPath(*result, centre);
}

std::optional<Region> SkiaPathOps::Simplify(const Region& region) const {
  const Point centre = Centre({&region});
  const std::optional<SkPath> result = ::Simplify(ToSkPath(region, centre));
  if (!result) return std::nullopt;
  return FromSkPath(*result, centre);
}

}  // namespace leinwand::render
