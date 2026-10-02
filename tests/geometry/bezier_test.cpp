// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/bezier.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <numbers>

using namespace leinwand::core;
using Catch::Approx;
using leinwand::geometry::Bounds;
using leinwand::geometry::CubicBezier;
using leinwand::geometry::MapRect;
using leinwand::geometry::SegmentAt;
using leinwand::geometry::Transform;

namespace {

void CheckRect(const Rect& actual, const Rect& expected) {
  CHECK(actual.left == Approx(expected.left).margin(1e-9));
  CHECK(actual.top == Approx(expected.top).margin(1e-9));
  CHECK(actual.right == Approx(expected.right).margin(1e-9));
  CHECK(actual.bottom == Approx(expected.bottom).margin(1e-9));
}

// An arch from (0,0) to (100,0) bulging down to y = 75 at t = 0.5.
const CubicBezier kArch{{0, 0}, {0, 100}, {100, 100}, {100, 0}};

}  // namespace

TEST_CASE("CubicBezier evaluates endpoints and midpoint") {
  CHECK(kArch.Evaluate(0) == Point{0, 0});
  CHECK(kArch.Evaluate(1) == Point{100, 0});
  CHECK(kArch.Evaluate(0.5).x == Approx(50));
  CHECK(kArch.Evaluate(0.5).y == Approx(75));
}

TEST_CASE("CubicBezier bounds are tight, not the control polygon") {
  CheckRect(kArch.Bounds(), {0, 0, 100, 75});
  // A straight line has no interior extrema.
  CheckRect(CubicBezier{{10, 10}, {20, 20}, {30, 30}, {40, 40}}.Bounds(), {10, 10, 40, 40});
  // An S-curve overshoots in x on both sides.
  const Rect s = CubicBezier{{0, 0}, {100, 0}, {-100, 100}, {0, 100}}.Bounds();
  CHECK(s.left < 0);
  CHECK(s.right > 0);
  CHECK(s.right < 100);
}

TEST_CASE("Path bounds cover every segment, including the closing one") {
  PathData path;
  CHECK_FALSE(Bounds(path).IsValid());

  path.anchors = {{{5, 5}}};
  CheckRect(Bounds(path), {5, 5, 5, 5});  // Isolated point.

  // A closed path whose closing segment bulges below the anchors.
  path.anchors = {{{0, 0}, {}, {}}, {{100, 0}, {}, {0, 100}}};
  path.anchors[0].handle_in = {0, 100};
  CheckRect(Bounds(path), {0, 0, 100, 0});  // Open: just the straight segment.
  path.closed = true;
  CheckRect(Bounds(path), {0, 0, 100, 75});
  CHECK(SegmentAt(path, 1).p0 == Point{100, 0});
  CHECK(SegmentAt(path, 1).p3 == Point{0, 0});
}

TEST_CASE("Transform moves anchors and rotates handles without translating them") {
  PathData path;
  path.anchors = {{{10, 0}, {-5, 0}, {5, 0}}};
  const PathData moved = Transform(path, Matrix::Translate(1, 2) * Matrix::Scale(2, 2));
  CHECK(moved.anchors[0].position == Point{21, 2});
  CHECK(moved.anchors[0].handle_in == Point{-10, 0});
  CHECK(moved.anchors[0].handle_out == Point{10, 0});
}

TEST_CASE("MapRect bounds the transformed corners") {
  CheckRect(MapRect(Rect::FromXYWH(0, 0, 10, 20), Matrix::Translate(5, 5)), {5, 5, 15, 25});
  // A quarter turn swaps width and height around the origin.
  CheckRect(MapRect(Rect::FromXYWH(0, 0, 10, 20), Matrix::Rotate(std::numbers::pi / 2)),
            {-20, 0, 0, 10});
  CHECK_FALSE(MapRect(Rect{}, Matrix::Scale(2, 2)).IsValid());
}

TEST_CASE("Object bounds include group transforms and compound subpaths") {
  PathData square;
  square.anchors = {{{0, 0}}, {{10, 0}}, {{10, 10}}, {{0, 10}}};
  square.closed = true;
  PathData far_square = Transform(square, Matrix::Translate(90, 90));

  CompoundPathObject compound;
  compound.subpaths = {square, far_square};
  CheckRect(Bounds(Object{compound}), {0, 0, 100, 100});

  GroupObject group;
  group.children = {MakeObject(PathObject{{}, square})};
  group.transform = Matrix::Translate(50, 0) * Matrix::Scale(2, 2);
  CheckRect(Bounds(Object{group}), {50, 0, 70, 20});

  CHECK_FALSE(Bounds(Object{GroupObject{}}).IsValid());  // Empty group.
}
