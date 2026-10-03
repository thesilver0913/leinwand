// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/shape.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <variant>

#include "core/object.h"
#include "core/transform.h"

using namespace leinwand::core;
using Catch::Approx;

namespace {

// Bounds of the anchors (enough for shapes whose extremes are anchors).
Rect AnchorBounds(const PathData& path) {
  Rect r;
  for (const auto& a : path.anchors) r = r.Union(a.position);
  return r;
}

ObjectPtr Shape(ShapeParams params, Matrix transform = {}) {
  ShapeObject shape;
  shape.common.id = "s";
  shape.shape = params;
  shape.transform = transform;
  return MakeObject(std::move(shape));
}

}  // namespace

TEST_CASE("A plain rectangle is four corners around its centre") {
  const PathData path = ShapePath(RectangleShape{100, 50});
  REQUIRE(path.anchors.size() == 4);
  CHECK(path.closed);
  CHECK(AnchorBounds(path) == Rect{-50, -25, 50, 25});
  CHECK(path.anchors[0].position == Point{-50, -25});  // Top-left first, clockwise.
  CHECK(path.anchors[1].position == Point{50, -25});
}

TEST_CASE("Rounded corners replace a corner with a quarter arc") {
  RectangleShape rect{100, 50};
  rect.corners[0] = {10};  // Top-left only.
  const PathData path = ShapePath(rect);
  REQUIRE(path.anchors.size() == 5);
  // On the left edge 10 below the corner, then on the top edge 10 along.
  CHECK(path.anchors[0].position.x == Approx(-50));
  CHECK(path.anchors[0].position.y == Approx(-15));
  CHECK(path.anchors[1].position.x == Approx(-40));
  CHECK(path.anchors[1].position.y == Approx(-25));
  // Quarter-circle handles: 0.5523 * r, pointing at the corner.
  CHECK(path.anchors[0].handle_out.y == Approx(-5.5228).margin(1e-3));
  CHECK(path.anchors[1].handle_in.x == Approx(-5.5228).margin(1e-3));
}

TEST_CASE("Corner radii are clamped to half the shorter edge") {
  RectangleShape rect{100, 20};
  for (auto& c : rect.corners) c = {50};
  const PathData path = ShapePath(rect);
  CHECK(AnchorBounds(path).height() == Approx(20));
  CHECK(path.anchors[1].position.x == Approx(-40));  // Took 10, not 50, along the top.
}

TEST_CASE("Inverted and chamfered corners stay inside the box") {
  for (CornerKind kind : {CornerKind::kInvertedRound, CornerKind::kChamfer}) {
    RectangleShape rect{100, 100};
    for (auto& c : rect.corners) c = {20, kind};
    const PathData path = ShapePath(rect);
    CHECK(path.anchors.size() == 8);
    CHECK(AnchorBounds(path) == Rect{-50, -50, 50, 50});
  }
  RectangleShape rect{100, 100};
  rect.corners[0] = {20, CornerKind::kInvertedRound};
  // The inverted arc's handles point into the shape (+x and +y at top-left).
  const PathData path = ShapePath(rect);
  CHECK(path.anchors[0].handle_out.x > 0);
  CHECK(path.anchors[1].handle_in.y > 0);
}

TEST_CASE("Ellipses: full, and pies that close through the centre") {
  const PathData full = ShapePath(EllipseShape{100, 50});
  CHECK(full.anchors.size() == 4);
  CHECK(AnchorBounds(full) == Rect{-50, -25, 50, 25});

  // Counter-clockwise on screen from 3 o'clock: 0 to 90 is the top-right quarter.
  const PathData quarter = ShapePath(EllipseShape{100, 50, 0, 90});
  REQUIRE(quarter.anchors.size() == 3);
  CHECK(quarter.anchors[0].position == Point{50, 0});
  CHECK(quarter.anchors[1].position.x == Approx(0).margin(1e-9));
  CHECK(quarter.anchors[1].position.y == Approx(-25));
  CHECK(quarter.anchors[2].position == Point{0, 0});
}

TEST_CASE("Polygons and stars point up") {
  const PathData hexagon = ShapePath(PolygonShape{6, 50});
  REQUIRE(hexagon.anchors.size() == 6);
  CHECK(hexagon.anchors[0].position.x == Approx(0).margin(1e-9));
  CHECK(hexagon.anchors[0].position.y == Approx(-50));
  CHECK(ShapePath(PolygonShape{6, 50, 5}).anchors.size() == 12);  // Rounded corners.
  CHECK(ShapePath(PolygonShape{2, 50}).anchors.size() == 3);      // At least a triangle.

  const PathData star = ShapePath(StarShape{5, 50, 20});
  REQUIRE(star.anchors.size() == 10);
  CHECK(star.anchors[0].position.y == Approx(-50));
  const Point inner = star.anchors[1].position;
  CHECK(std::hypot(inner.x, inner.y) == Approx(20));
}

TEST_CASE("Lines are open segments centred on the origin") {
  const PathData line = ShapePath(LineShape{80});
  CHECK_FALSE(line.closed);
  REQUIRE(line.anchors.size() == 2);
  CHECK(line.anchors[0].position == Point{-40, 0});
}

TEST_CASE("Scaling a live shape changes its parameters, not its transform") {
  const ObjectPtr scaled =
      Transformed(Shape(RectangleShape{100, 50}, Matrix::Translate(10, 10)), Matrix::Scale(2, 3));
  const auto* shape = std::get_if<ShapeObject>(&*scaled);
  REQUIRE(shape);
  const auto& rect = std::get<RectangleShape>(shape->shape);
  CHECK(rect.width == Approx(200));
  CHECK(rect.height == Approx(150));
  CHECK(shape->transform.Map({0, 0}) == Point{20, 30});  // Centre moved with the scale.
  CHECK(shape->transform.a == Approx(1));                // No scale left in the matrix.
}

TEST_CASE("Rotation stays in the transform; mirroring keeps the shape live") {
  const ObjectPtr rotated =
      Transformed(Shape(EllipseShape{100, 50}), Matrix::Rotate(std::numbers::pi / 6));
  const auto& shape = std::get<ShapeObject>(*rotated);
  CHECK(std::get<EllipseShape>(shape.shape).width == Approx(100));
  CHECK(std::atan2(shape.transform.b, shape.transform.a) == Approx(std::numbers::pi / 6));

  const ObjectPtr mirrored = Transformed(Shape(RectangleShape{100, 50}), Matrix::Scale(-1, 1));
  CHECK(std::holds_alternative<ShapeObject>(*mirrored));
}

TEST_CASE("A transform the shape cannot keep turns it into a path") {
  // Rotate, then stretch along x: that shears the rotated rectangle.
  const ObjectPtr skewed =
      Transformed(Shape(RectangleShape{100, 50}, Matrix::Rotate(0.5)), Matrix::Scale(2, 1));
  REQUIRE(std::holds_alternative<PathObject>(*skewed));
  CHECK(CommonOf(*skewed).id == "s");  // Same object, now a path.

  // Polygons and stars must scale evenly.
  CHECK(std::holds_alternative<PathObject>(
      *Transformed(Shape(PolygonShape{6, 50}), Matrix::Scale(2, 1))));
  CHECK(std::holds_alternative<ShapeObject>(
      *Transformed(Shape(PolygonShape{6, 50}), Matrix::Scale(2, 2))));
}

TEST_CASE("A line stays live under any transform") {
  const ObjectPtr line = Transformed(Shape(LineShape{100}), Matrix{1, 0, 1, 1, 0, 0});  // Shear.
  const auto& shape = std::get<ShapeObject>(*line);
  CHECK(std::get<LineShape>(shape.shape).length == Approx(100));  // A horizontal line is unsheared.
  const ObjectPtr turned = Transformed(Shape(LineShape{100}),
                                       Matrix::Rotate(std::numbers::pi / 2) * Matrix::Scale(1, 5));
  CHECK(std::get<LineShape>(std::get<ShapeObject>(*turned).shape).length == Approx(100));
}

TEST_CASE("Expanded turns a shape into an identical path and leaves others alone") {
  const ObjectPtr shape = Shape(RectangleShape{100, 50}, Matrix::Translate(5, 5));
  const ObjectPtr path = Expanded(shape);
  REQUIRE(std::holds_alternative<PathObject>(*path));
  CHECK(AnchorBounds(std::get<PathObject>(*path).path) == Rect{-45, -20, 55, 30});
  CHECK(Expanded(path) == path);
  CHECK(IsClosed(*shape));
  CHECK_FALSE(IsClosed(*Shape(LineShape{10})));
}
