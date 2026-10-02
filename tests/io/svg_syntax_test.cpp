// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/svg_syntax.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "geometry/bezier.h"

using namespace leinwand;
using namespace leinwand::io::svg;
using Catch::Approx;
using core::Point;

namespace {

void CheckPoint(Point actual, Point expected, double margin = 1e-6) {
  CHECK(actual.x == Approx(expected.x).margin(margin));
  CHECK(actual.y == Approx(expected.y).margin(margin));
}

}  // namespace

TEST_CASE("Path data: lines, relative commands and closing") {
  const auto paths = ParsePathData("M10,10 l20 0 v20 H10 z m5,5 h1");
  REQUIRE(paths.size() == 2);
  CHECK(paths[0].closed);
  REQUIRE(paths[0].anchors.size() == 4);
  CheckPoint(paths[0].anchors[2].position, {30, 30});
  // After z, relative moves start from the subpath's start.
  CheckPoint(paths[1].anchors[0].position, {15, 15});
  CheckPoint(paths[1].anchors[1].position, {16, 15});
}

TEST_CASE("Path data: compact numbers and implicit repeats") {
  const auto paths = ParsePathData("M0-1.5.5.5L1e1,0 20,0");
  REQUIRE(paths.size() == 1);
  REQUIRE(paths[0].anchors.size() == 4);  // M's extra pair is a line-to.
  CheckPoint(paths[0].anchors[0].position, {0, -1.5});
  CheckPoint(paths[0].anchors[1].position, {0.5, 0.5});
  CheckPoint(paths[0].anchors[3].position, {20, 0});
}

TEST_CASE("Path data: curves, smooth curves and quadratics become cubics") {
  const auto c = ParsePathData("M0 0 C 0 10 10 10 10 0 S 20 -10 20 0");
  REQUIRE(c[0].anchors.size() == 3);
  CheckPoint(c[0].anchors[1].handle_in, {0, 10});
  CheckPoint(c[0].anchors[1].handle_out, {0, -10});  // Reflected.
  CHECK(c[0].anchors[1].kind == core::AnchorKind::kSmooth);

  const auto q = ParsePathData("M0 0 Q 10 10 20 0");
  REQUIRE(q[0].anchors.size() == 2);
  CheckPoint(q[0].anchors[0].handle_out, {20.0 / 3, 20.0 / 3});
}

TEST_CASE("Path data: arcs follow the ellipse") {
  // A half circle of radius 10 from (0,0) to (20,0) through (10,-10).
  const auto paths = ParsePathData("M0 0 A10 10 0 0 1 20 0");
  REQUIRE(paths.size() == 1);
  const auto& p = paths[0];
  REQUIRE(p.anchors.size() == 3);
  for (int i = 0; i < p.segment_count(); ++i) {
    const auto segment = geometry::SegmentAt(p, i);
    for (double t : {0.25, 0.5, 0.75}) {
      const Point q = segment.Evaluate(t);
      CHECK(std::hypot(q.x - 10, q.y) == Approx(10).margin(0.01));
      CHECK(q.y <= 1e-9);
    }
  }
  // Compact flags: "a10 10 0 0120 0".
  CHECK(ParsePathData("M0 0a10 10 0 0120 0")[0].anchors.size() == 3);
}

TEST_CASE("Path data stops at the first error, keeping what came before") {
  const auto paths = ParsePathData("M0 0 L10 0 L oops 20 20");
  REQUIRE(paths.size() == 1);
  CHECK(paths[0].anchors.size() == 2);
  CHECK(ParsePathData("L10 10").empty());
}

TEST_CASE("Transforms compose left to right") {
  const auto m = ParseTransform("translate(10,20) scale(2)");
  REQUIRE(m);
  CheckPoint(m->Map({1, 1}), {12, 22});
  const auto r = ParseTransform("rotate(90 10 10)");
  REQUIRE(r);
  CheckPoint(r->Map({20, 10}), {10, 20});
  CheckPoint(ParseTransform("matrix(1 0 0 1 5 6)")->Map({0, 0}), {5, 6});
  CheckPoint(ParseTransform("skewX(45)")->Map({0, 10}), {10, 10});
  CHECK_FALSE(ParseTransform("translate(1"));
  CHECK_FALSE(ParseTransform("spin(3)"));
}

TEST_CASE("Colors: hex, rgb() and names") {
  CHECK(*ParseColor("#f00") == core::RgbColor{1, 0, 0});
  CHECK(*ParseColor("#00FF00") == core::RgbColor{0, 1, 0});
  CHECK(*ParseColor("rgb(0, 0, 255)") == core::RgbColor{0, 0, 1});
  CHECK(ParseColor("rgb(50%, 0%, 0%)")->r == Approx(0.5));
  CHECK(*ParseColor("CornflowerBlue") == *ParseColor("#6495ed"));
  double alpha = 1;
  ParseColor("rgba(0,0,0,0.25)", &alpha);
  CHECK(alpha == Approx(0.25));
  CHECK_FALSE(ParseColor("bluish"));
}

TEST_CASE("Lengths convert units to px") {
  CHECK(*ParseLength("10") == 10);
  CHECK(*ParseLength("1in") == 96);
  CHECK(*ParseLength("72pt") == Approx(96));
  CHECK(*ParseLength("25.4mm") == Approx(96));
  CHECK(*ParseLength("50%", 200) == 100);
  CHECK_FALSE(ParseLength("ten"));
}

TEST_CASE("Declarations and style sheets") {
  const auto d = ParseDeclarations("fill: red; Stroke-Width:2 ;opacity:0.5 !important");
  CHECK(d.at("fill") == "red");
  CHECK(d.at("stroke-width") == "2");
  CHECK(d.at("opacity") == "0.5");

  int skipped = 0;
  const auto rules = ParseStyleSheet(
      "/* c */ .a{fill:red} g .b, #c{stroke:blue} rect.d{fill:green} "
      "a > b{fill:pink} @media print{.a{fill:black}} :hover{fill:none}",
      0, &skipped);
  REQUIRE(rules.size() == 4);
  CHECK(rules[0].specificity == 10);
  CHECK(rules[1].selector.size() == 2);  // g .b
  CHECK(rules[2].specificity == 100);    // #c
  CHECK(rules[3].specificity == 11);     // rect.d
  CHECK(skipped == 3);
}
