// SPDX-License-Identifier: GPL-3.0-or-later
// The awkward inputs of spec 4.3 (from the M8 probe) on the model's path
// operations: each pair must satisfy |A∪B| + |A∩B| = |A| + |B|,
// |A−B| = |A| − |A∩B| and |A xor B| = |A∪B| − |A∩B|.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include "geometry/path_ops.h"
#include "render/skia_path_ops.h"

using leinwand::core::Anchor;
using leinwand::core::PathData;
using leinwand::core::Point;
using leinwand::geometry::Area;
using leinwand::geometry::BooleanOp;
using leinwand::geometry::Region;
using leinwand::render::SkiaPathOps;

namespace {

Region Polygon(const std::vector<Point>& points) {
  PathData path;
  path.closed = true;
  for (Point p : points) path.anchors.push_back(Anchor{p, {}, {}});
  return {{path}};
}

Region Square(double x, double y, double size, double degrees = 0) {
  const double c = std::cos(degrees * std::numbers::pi / 180);
  const double s = std::sin(degrees * std::numbers::pi / 180);
  const Point centre{x + size / 2, y + size / 2};
  std::vector<Point> corners;
  for (Point p : {Point{x, y}, Point{x + size, y}, Point{x + size, y + size}, Point{x, y + size}}) {
    const Point d = p - centre;
    corners.push_back({centre.x + d.x * c - d.y * s, centre.y + d.x * s + d.y * c});
  }
  return Polygon(corners);
}

Region Circle(double cx, double cy, double r) {
  const double k = r * 0.5522847498307936;
  PathData path;
  path.closed = true;
  path.anchors = {Anchor{{cx + r, cy}, {0, -k}, {0, k}}, Anchor{{cx, cy + r}, {k, 0}, {-k, 0}},
                  Anchor{{cx - r, cy}, {0, k}, {0, -k}}, Anchor{{cx, cy - r}, {-k, 0}, {k, 0}}};
  return {{path}};
}

struct Case {
  std::string name;
  Region a, b;
};

}  // namespace

TEST_CASE("Path operations hold on awkward inputs") {
  std::vector<Case> cases = {
      {"squares sharing an edge", Square(0, 0, 100), Square(100, 0, 100)},
      {"squares with collinear edges", Square(0, 0, 100), Square(50, 0, 100)},
      {"squares touching at a corner", Square(0, 0, 100), Square(100, 100, 100)},
      {"identical squares", Square(0, 0, 100), Square(0, 0, 100)},
      {"identical circles", Circle(0, 0, 100), Circle(0, 0, 100)},
      {"overlapping circles", Circle(0, 0, 100), Circle(100, 0, 100)},
      {"circles touching outside", Circle(0, 0, 100), Circle(200, 0, 100)},
      {"circles touching inside", Circle(0, 0, 100), Circle(50, 0, 50)},
      {"circles 1e-4 apart", Circle(0, 0, 100), Circle(200.0001, 0, 100)},
      {"concentric circles", Circle(0, 0, 100), Circle(0, 0, 50)},
      {"square turned 0.001 deg", Square(0, 0, 100), Square(0, 0, 100, 0.001)},
      {"square turned 1e-6 deg", Square(0, 0, 100), Square(0, 0, 100, 1e-6)},
      {"circle with tangent square", Circle(0, 0, 100), Square(100, -50, 100)},
      {"tiny circles", Circle(0, 0, 0.01), Circle(0.01, 0, 0.01)},
      {"huge circles", Circle(0, 0, 1e5), Circle(1e5, 0, 1e5)},
      {"far away, r 10", Circle(1e6, 1e6, 10), Circle(1e6 + 10, 1e6, 10)},
      {"far away, r 0.1", Circle(1e6, 1e6, 0.1), Circle(1e6 + 0.1, 1e6, 0.1)},
  };
  // A self-crossing star (nonzero fills its middle) against a circle.
  std::vector<Point> star;
  for (int i = 0; i < 5; ++i) {
    const double angle = -std::numbers::pi / 2 + i * 4 * std::numbers::pi / 5;
    star.push_back({100 * std::cos(angle), 100 * std::sin(angle)});
  }
  cases.push_back({"self-crossing star", Polygon(star), Circle(0, 0, 50)});
  // Many overlapping circles in one region against one more.
  Region many;
  for (int i = 0; i < 40; ++i) {
    many.subpaths.push_back(
        Circle(40 * std::cos(i * 0.7) + i, 40 * std::sin(i * 1.3), 15 + (i % 7) * 3).subpaths[0]);
  }
  cases.push_back({"40 circles", many, Circle(30, 0, 45)});

  const SkiaPathOps ops;
  for (const Case& c : cases) {
    INFO(c.name);
    const auto a = ops.Simplify(c.a), b = ops.Simplify(c.b);
    const auto u = ops.Apply(c.a, c.b, BooleanOp::kUnion);
    const auto i = ops.Apply(c.a, c.b, BooleanOp::kIntersect);
    const auto d = ops.Apply(c.a, c.b, BooleanOp::kDifference);
    const auto x = ops.Apply(c.a, c.b, BooleanOp::kExclude);
    REQUIRE((a && b && u && i && d && x));
    // Flattening finely enough for the smallest case.
    const double tolerance = 1e-5;
    const double area_a = Area(*a, tolerance), area_b = Area(*b, tolerance);
    const double au = Area(*u, tolerance), ai = Area(*i, tolerance), ad = Area(*d, tolerance),
                 ax = Area(*x, tolerance);
    const double scale = area_a + area_b;
    CHECK(std::abs(au + ai - area_a - area_b) / scale < 1e-4);
    CHECK(std::abs(ad - (area_a - ai)) / scale < 1e-4);
    CHECK(std::abs(ax - (au - ai)) / scale < 1e-4);
  }
}
