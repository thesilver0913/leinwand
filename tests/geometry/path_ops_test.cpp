// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/path_ops.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using leinwand::core::Anchor;
using leinwand::core::FillRule;
using leinwand::core::PathData;
using leinwand::geometry::Area;
using leinwand::geometry::Region;
using leinwand::geometry::SplitIntoPieces;

namespace {

PathData Square(double x, double y, double size, bool clockwise = true) {
  PathData path;
  path.closed = true;
  const double xs[] = {x, x + size, x + size, x};
  const double ys[] = {y, y, y + size, y + size};
  for (int i = 0; i < 4; ++i) {
    const int k = clockwise ? i : 3 - i;
    path.anchors.push_back(Anchor{{xs[k], ys[k]}, {}, {}});
  }
  return path;
}

}  // namespace

TEST_CASE("Area counts holes out, whichever way they are wound") {
  CHECK(Area({{Square(0, 0, 10)}}) == Catch::Approx(100));
  CHECK(Area({{Square(0, 0, 10), Square(2, 2, 5, false)}}) == Catch::Approx(75));
  CHECK(Area({{Square(0, 0, 10), Square(2, 2, 5, true)}, FillRule::kEvenOdd}) == Catch::Approx(75));
  CHECK(Area({}) == 0);
}

TEST_CASE("SplitIntoPieces keeps holes with their outer contour") {
  // Two separate squares.
  CHECK(SplitIntoPieces({{Square(0, 0, 10), Square(20, 0, 10)}}).size() == 2);

  // A square with a hole is one piece of two subpaths.
  const auto framed = SplitIntoPieces({{Square(0, 0, 10), Square(2, 2, 6, false)}});
  REQUIRE(framed.size() == 1);
  CHECK(framed[0].subpaths.size() == 2);

  // An island inside the hole is a piece of its own.
  const auto island =
      SplitIntoPieces({{Square(0, 0, 10), Square(2, 2, 6, false), Square(4, 4, 2)}});
  REQUIRE(island.size() == 2);
  CHECK(island[0].subpaths.size() + island[1].subpaths.size() == 3);

  // Squares touching at a corner stay two pieces.
  CHECK(SplitIntoPieces({{Square(0, 0, 10), Square(10, 10, 10)}}).size() == 2);
}
