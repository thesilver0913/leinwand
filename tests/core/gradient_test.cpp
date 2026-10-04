// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/gradient.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/style.h"
#include "core/transform.h"

using namespace leinwand::core;
using Catch::Approx;

TEST_CASE("DefaultGradient lays a gradient across the bounds") {
  const Rect bounds = Rect::FromXYWH(10, 20, 100, 40);
  const Gradient linear =
      DefaultGradient(GradientType::kLinear, bounds, RgbColor{1, 1, 1}, RgbColor{0, 0, 0});
  CHECK(linear.start == Point{10, 40});
  CHECK(linear.end == Point{110, 40});
  REQUIRE(linear.stops.size() == 2);
  const Gradient radial =
      DefaultGradient(GradientType::kRadial, bounds, RgbColor{1, 1, 1}, RgbColor{0, 0, 0});
  CHECK(radial.start == Point{60, 40});
  CHECK(radial.end == Point{110, 40});  // Half the longer side.
}

TEST_CASE("The angle turns the axis counter-clockwise, keeping its length") {
  Gradient g;
  g.start = {0, 0};
  g.end = {100, 0};
  CHECK(GradientAngle(g) == Approx(0));
  g = WithAngle(g, 90);
  CHECK(g.end.x == Approx(0).margin(1e-9));
  CHECK(g.end.y == Approx(-100));  // Up on screen.
  CHECK(GradientAngle(g) == Approx(90));
}

TEST_CASE("Stops are added, moved and removed, keeping two") {
  Gradient g = DefaultGradient(GradientType::kLinear, Rect::FromXYWH(0, 0, 10, 10),
                               RgbColor{1, 0, 0}, RgbColor{0, 0, 1});
  const int added = AddStop(g, 0.25);
  REQUIRE(g.stops.size() == 3);
  CHECK(added == 1);
  // The color where it was added.
  CHECK(std::get<RgbColor>(g.stops[1].color).r == Approx(0.75));
  CHECK(std::get<RgbColor>(g.stops[1].color).b == Approx(0.25));
  // Moving past the last stop re-sorts.
  CHECK(MoveStop(g, 1, 1.0) == 2);
  CHECK(g.stops[2].offset == 1.0);
  RemoveStop(g, 0);
  RemoveStop(g, 0);
  CHECK(g.stops.size() == 2);
}

TEST_CASE("Transforming an object moves its gradient") {
  PathObject path;
  path.common.id = "p";
  path.path.anchors = {{{0, 0}}, {{10, 0}}, {{10, 10}}};
  Fill fill{RgbColor{}};
  fill.gradient = DefaultGradient(GradientType::kLinear, Rect::FromXYWH(0, 0, 10, 10),
                                  RgbColor{1, 1, 1}, RgbColor{0, 0, 0});
  path.common.appearance = {fill};
  const ObjectPtr moved = Transformed(MakeObject(path), Matrix::Translate(5, 7));
  const Fill* f = FrontFill(CommonOf(*moved).appearance);
  REQUIRE(f->gradient);
  CHECK(f->gradient->start == Point{5, 12});
  CHECK(f->gradient->end == Point{15, 12});
}

TEST_CASE("A solid color replaces a gradient; Shift+X swaps gradients too") {
  Appearance a{Stroke{RgbColor{0, 0, 0}}, Fill{RgbColor{}}};
  std::get<Fill>(a[1]).gradient = Gradient{};
  SwapFillAndStroke(a);
  CHECK(FrontStroke(a)->gradient.has_value());
  CHECK(!FrontFill(a)->gradient.has_value());
  SetStrokePaint(a, RgbColor{1, 0, 0});
  CHECK(!FrontStroke(a)->gradient.has_value());
}
