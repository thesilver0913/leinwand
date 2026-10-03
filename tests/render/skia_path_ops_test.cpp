// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/skia_path_ops.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <numbers>

using leinwand::core::Anchor;
using leinwand::core::PathData;
using leinwand::core::Point;
using leinwand::geometry::Area;
using leinwand::geometry::BooleanOp;
using leinwand::geometry::Region;
using leinwand::render::SkiaPathOps;

namespace {

Region Square(double x, double y, double size) {
  PathData path;
  path.closed = true;
  path.anchors = {Anchor{{x, y}, {}, {}}, Anchor{{x + size, y}, {}, {}},
                  Anchor{{x + size, y + size}, {}, {}}, Anchor{{x, y + size}, {}, {}}};
  return {{path}};
}

// A circle of four cubics, as Leinwand's ellipses are built.
Region Circle(double cx, double cy, double r) {
  const double k = r * 0.5522847498307936;
  PathData path;
  path.closed = true;
  path.anchors = {Anchor{{cx + r, cy}, {0, -k}, {0, k}}, Anchor{{cx, cy + r}, {k, 0}, {-k, 0}},
                  Anchor{{cx - r, cy}, {0, k}, {0, -k}}, Anchor{{cx, cy - r}, {-k, 0}, {k, 0}}};
  return {{path}};
}

// The lens where two circles of radius r overlap, centres d apart.
double Lens(double r, double d) {
  return 2 * r * r * std::acos(d / (2 * r)) - d / 2 * std::sqrt(4 * r * r - d * d);
}

}  // namespace

TEST_CASE("SkiaPathOps applies the four operations") {
  const SkiaPathOps ops;
  const Region a = Square(0, 0, 100), b = Square(50, 0, 100);
  CHECK(Area(*ops.Apply(a, b, BooleanOp::kUnion)) == Catch::Approx(15000));
  CHECK(Area(*ops.Apply(a, b, BooleanOp::kIntersect)) == Catch::Approx(5000));
  CHECK(Area(*ops.Apply(a, b, BooleanOp::kDifference)) == Catch::Approx(5000));
  CHECK(Area(*ops.Apply(a, b, BooleanOp::kExclude)) == Catch::Approx(10000));
}

TEST_CASE("SkiaPathOps returns an empty region, not a failure, for no overlap") {
  const SkiaPathOps ops;
  const auto result = ops.Apply(Square(0, 0, 10), Square(20, 0, 10), BooleanOp::kIntersect);
  REQUIRE(result);
  CHECK(result->empty());
  const auto same = ops.Apply(Circle(0, 0, 10), Circle(0, 0, 10), BooleanOp::kDifference);
  REQUIRE(same);
  CHECK(same->empty());
}

TEST_CASE("SkiaPathOps keeps curves as curves") {
  const SkiaPathOps ops;
  const auto result = ops.Apply(Circle(0, 0, 100), Circle(100, 0, 100), BooleanOp::kIntersect);
  REQUIRE(result);
  REQUIRE(result->subpaths.size() == 1);
  const PathData& lens = result->subpaths[0];
  CHECK(lens.closed);
  int curved = 0;
  for (const Anchor& anchor : lens.anchors) curved += anchor.handle_in != Point{};
  CHECK(curved == static_cast<int>(lens.anchors.size()));
  // The four-cubic circle is slightly larger than a true one (by ~2.7e-4).
  CHECK(Area(*result) == Catch::Approx(Lens(100, 100)).epsilon(1e-3));
}

TEST_CASE("SkiaPathOps works far from the origin (inputs are recentred)") {
  // At 1e6 floats step by 1/16 pt: without recentring these circles vanish
  // (M8 check 1).
  const SkiaPathOps ops;
  const auto far =
      ops.Apply(Circle(1e6, 1e6, 0.1), Circle(1e6 + 0.1, 1e6, 0.1), BooleanOp::kIntersect);
  REQUIRE(far);
  CHECK(Area(*far, 1e-5) == Catch::Approx(Lens(0.1, 0.1)).epsilon(1e-3));
  // The result is back in place.
  REQUIRE(!far->empty());
  CHECK(far->subpaths[0].anchors[0].position.x == Catch::Approx(1e6).margin(1));
}

TEST_CASE("SkiaPathOps::Simplify removes overlaps within one region") {
  const SkiaPathOps ops;
  Region overlapping = Square(0, 0, 100);
  overlapping.subpaths.push_back(Square(50, 0, 100).subpaths[0]);
  const auto simple = ops.Simplify(overlapping);
  REQUIRE(simple);
  CHECK(simple->subpaths.size() == 1);
  CHECK(Area(*simple) == Catch::Approx(15000));
}
