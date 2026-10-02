// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/hit_test.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace leinwand::core;
using Catch::Approx;
using leinwand::geometry::DistanceToOutline;
using leinwand::geometry::FillContains;
using leinwand::geometry::Flatten;
using leinwand::geometry::HitTest;
using leinwand::geometry::ObjectsTouching;

namespace {

PathData Square(double x, double y, double size) {
  PathData path;
  path.anchors = {{{x, y}}, {{x + size, y}}, {{x + size, y + size}}, {{x, y + size}}};
  path.closed = true;
  return path;
}

PathData Circle(double cx, double cy, double r) {
  const double k = r * 0.5522847498;
  PathData path;
  path.closed = true;
  path.anchors = {{{cx + r, cy}, {0, -k}, {0, k}},
                  {{cx, cy + r}, {k, 0}, {-k, 0}},
                  {{cx - r, cy}, {0, k}, {0, -k}},
                  {{cx, cy - r}, {-k, 0}, {k, 0}}};
  return path;
}

ObjectPtr Shape(const std::string& id, PathData path, Appearance appearance) {
  PathObject object;
  object.common.id = id;
  object.common.appearance = std::move(appearance);
  object.path = std::move(path);
  return MakeObject(std::move(object));
}

const Fill kFill{RgbColor{1, 0, 0}};

Document With(std::vector<LayerChild> children) {
  Layer layer;
  layer.id = "layer";
  layer.children = std::move(children);
  Document document;
  document.layers = {MakeLayer(std::move(layer))};
  return document;
}

std::string HitId(const Document& document, Point p) {
  const auto hit = HitTest(document, p, 2.0);
  return hit ? hit->top_level_id : "";
}

}  // namespace

TEST_CASE("Flatten stays close to the curve") {
  const PathData circle = Circle(0, 0, 100);
  for (const Point& p : Flatten(circle, 0.1)) {
    CHECK(std::hypot(p.x, p.y) == Approx(100).margin(0.15));
  }
  CHECK(Flatten(Square(0, 0, 10), 0.1).size() == 5);  // Straight edges: one step each, closed.
}

TEST_CASE("FillContains follows the fill rule") {
  const std::vector<PathData> donut = {Circle(0, 0, 50), Circle(0, 0, 20)};
  CHECK(FillContains(donut, FillRule::kEvenOdd, {35, 0}, 0.1));
  CHECK_FALSE(FillContains(donut, FillRule::kEvenOdd, {0, 0}, 0.1));
  CHECK(FillContains(donut, FillRule::kNonZero, {0, 0}, 0.1));  // Same direction: no hole.
  CHECK_FALSE(FillContains(donut, FillRule::kNonZero, {60, 0}, 0.1));
}

TEST_CASE("DistanceToOutline measures to the nearest edge") {
  CHECK(DistanceToOutline(Square(0, 0, 10), {5, -3}, 0.1) == Approx(3));
  CHECK(DistanceToOutline(Square(0, 0, 10), {5, 5}, 0.1) == Approx(5));
}

TEST_CASE("HitTest picks the frontmost object, by fill or by stroke") {
  Stroke thick{RgbColor{0, 0, 0}};
  thick.width = 10;
  const Document document = With({
      Shape("back", Square(0, 0, 100), {kFill}),
      Shape("ring", Circle(50, 50, 30), {thick}),  // Stroke only: the middle is empty.
  });
  CHECK(HitId(document, {10, 10}) == "back");
  CHECK(HitId(document, {80, 50}) == "ring");  // On the stroke.
  CHECK(HitId(document, {86, 50}) == "ring");  // Stroke reach 5 + tolerance 2.
  CHECK(HitId(document, {50, 50}) == "back");  // Through the unfilled middle.
  CHECK(HitId(document, {150, 150}).empty());
}

TEST_CASE("HitTest selects the top-level group but reports the leaf") {
  GroupObject group;
  group.common.id = "group";
  group.transform = Matrix::Translate(100, 0);
  group.children = {Shape("inner", Square(0, 0, 50), {kFill})};
  const Document document = With({MakeObject(std::move(group))});
  const auto hit = HitTest(document, {125, 25}, 1);
  REQUIRE(hit);
  CHECK(hit->top_level_id == "group");
  CHECK(hit->leaf_id == "inner");
  CHECK_FALSE(HitTest(document, {25, 25}, 1));  // Where it would be without the transform.
}

TEST_CASE("HitTest skips hidden and locked objects and layers") {
  PathObject hidden;
  hidden.common.id = "hidden";
  hidden.common.visible = false;
  hidden.common.appearance = {kFill};
  hidden.path = Square(0, 0, 10);
  PathObject locked = hidden;
  locked.common.id = "locked";
  locked.common.visible = true;
  locked.common.locked = true;
  const Document document =
      With({Shape("below", Square(0, 0, 10), {kFill}), MakeObject(hidden), MakeObject(locked)});
  CHECK(HitId(document, {5, 5}) == "below");

  Document hidden_layer = With({Shape("a", Square(0, 0, 10), {kFill})});
  Layer layer = *hidden_layer.layers[0];
  layer.visible = false;
  hidden_layer.layers[0] = MakeLayer(std::move(layer));
  CHECK(HitId(hidden_layer, {5, 5}).empty());
}

TEST_CASE("Clipped content is only hit inside the clip path") {
  GroupObject group;
  group.common.id = "clip";
  group.clipped = true;
  group.children = {Shape("content", Square(0, 0, 100), {kFill}),
                    Shape("mask", Square(25, 25, 50), {})};
  const Document document = With({MakeObject(std::move(group))});
  CHECK(HitId(document, {50, 50}) == "clip");
  CHECK(HitId(document, {10, 10}).empty());
}

TEST_CASE("ObjectsTouching finds objects the marquee crosses or sits inside") {
  const Document document = With({
      Shape("a", Square(0, 0, 10), {kFill}),
      Shape("b", Square(50, 50, 100), {kFill}),
      Shape("outline", Square(200, 0, 100), {}),  // Unfilled: only its edges count.
  });
  CHECK(ObjectsTouching(document, Rect{-5, -5, 5, 5}, 0.5) == IdSet{"a"});
  CHECK(ObjectsTouching(document, Rect{90, 90, 95, 95}, 0.5) == IdSet{"b"});  // Inside the fill.
  CHECK(ObjectsTouching(document, Rect{240, 40, 260, 60}, 0.5).empty());      // Inside, no fill.
  CHECK(ObjectsTouching(document, Rect{-10, -10, 400, 400}, 0.5) == IdSet{"a", "b", "outline"});
}
