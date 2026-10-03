// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/path_edit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace leinwand::core;
using namespace leinwand::geometry;
using Catch::Approx;

namespace {

void CheckPoint(Point actual, Point expected, double margin = 1e-9) {
  CHECK(actual.x == Approx(expected.x).margin(margin));
  CHECK(actual.y == Approx(expected.y).margin(margin));
}

// An arch from (0,0) to (100,0) through (50,75).
PathData Arch() {
  PathData path;
  path.anchors = {{{0, 0}, {}, {0, 100}}, {{100, 0}, {0, 100}, {}}};
  return path;
}

PathData Polyline(std::initializer_list<Point> points, bool closed = false) {
  PathData path;
  for (Point p : points) path.anchors.push_back({p});
  path.closed = closed;
  return path;
}

}  // namespace

TEST_CASE("Split halves trace the same curve") {
  const CubicBezier c = SegmentAt(Arch(), 0);
  const auto [left, right] = Split(c, 0.3);
  CheckPoint(left.p3, c.Evaluate(0.3));
  for (double u : {0.0, 0.25, 0.5, 1.0}) {
    CheckPoint(left.Evaluate(u), c.Evaluate(0.3 * u));
    CheckPoint(right.Evaluate(u), c.Evaluate(0.3 + 0.7 * u));
  }
}

TEST_CASE("NearestSegment finds the closest point on the path") {
  const auto hit = NearestSegment(Arch(), {50, 80});
  REQUIRE(hit);
  CHECK(hit->segment == 0);
  CHECK(hit->t == Approx(0.5).margin(1e-6));
  CHECK(hit->distance == Approx(5).margin(1e-6));
}

TEST_CASE("InsertAnchor keeps the shape and straight segments straight") {
  PathData arch = Arch();
  const PathData before = arch;
  CHECK(InsertAnchor(arch, 0, 0.5) == 1);
  REQUIRE(arch.anchors.size() == 3);
  CheckPoint(arch.anchors[1].position, {50, 75});
  CHECK(arch.anchors[1].kind == AnchorKind::kSmooth);
  for (double u : {0.0, 0.5, 1.0}) {
    CheckPoint(SegmentAt(arch, 0).Evaluate(u), SegmentAt(before, 0).Evaluate(0.5 * u));
    CheckPoint(SegmentAt(arch, 1).Evaluate(u), SegmentAt(before, 0).Evaluate(0.5 + 0.5 * u));
  }

  PathData square = Polyline({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true);
  CHECK(InsertAnchor(square, 3, 0.5) == 4);  // On the closing segment.
  CheckPoint(square.anchors[4].position, {0, 5});
  CHECK(square.anchors[4].handle_in == Point{});
  CHECK(square.anchors[0].handle_in == Point{});
}

TEST_CASE("RemoveAnchor keeps neighbours; a closed path left too small opens") {
  PathData triangle = Polyline({{0, 0}, {10, 0}, {5, 10}}, true);
  RemoveAnchor(triangle, 1);
  CHECK(triangle.anchors.size() == 2);
  CHECK(triangle.closed);
  RemoveAnchor(triangle, 0);
  CHECK_FALSE(triangle.closed);
}

TEST_CASE("MakeSmooth pulls handles along the neighbours' direction; MakeCorner retracts") {
  PathData path = Polyline({{0, 0}, {30, 30}, {60, 0}});
  MakeSmooth(path, 1);
  const Anchor& a = path.anchors[1];
  CHECK(a.kind == AnchorKind::kSmooth);
  CHECK(a.handle_out.y == Approx(0));  // Parallel to prev->next (horizontal).
  CHECK(a.handle_out.x == Approx(std::hypot(30, 30) / 3));
  CHECK(a.handle_in.x == Approx(-std::hypot(30, 30) / 3));
  MakeCorner(path, 1);
  CHECK(path.anchors[1].handle_in == Point{});
  CHECK(path.anchors[1].handle_out == Point{});
  CHECK(path.anchors[1].kind == AnchorKind::kCorner);
}

TEST_CASE("MoveHandle keeps smooth anchors collinear and symmetric ones mirrored") {
  PathData path;
  path.anchors = {{{0, 0}, {-10, 0}, {20, 0}, AnchorKind::kSmooth}};
  MoveHandle(path, 0, Side::kOut, {0, 40});
  CheckPoint(path.anchors[0].handle_in, {0, -10});  // Opposite direction, own length.

  path.anchors[0].kind = AnchorKind::kSymmetric;
  MoveHandle(path, 0, Side::kIn, {30, 0});
  CheckPoint(path.anchors[0].handle_out, {-30, 0});

  MoveHandle(path, 0, Side::kOut, {5, 5}, /*split=*/true);
  CHECK(path.anchors[0].kind == AnchorKind::kCorner);
  CheckPoint(path.anchors[0].handle_in, {30, 0});  // Left alone.
}

TEST_CASE("DragSegment bends the segment through the pointer") {
  PathData line = Polyline({{0, 0}, {100, 0}});
  DragSegment(line, 0, 0.5, {50, 30});
  CheckPoint(SegmentAt(line, 0).Evaluate(0.5), {50, 30}, 1e-6);
  CheckPoint(line.anchors[0].position, {0, 0});  // Anchors stay.
}

TEST_CASE("CutAt opens a closed path, or splits an open one") {
  const auto opened = CutAt(Polyline({{0, 0}, {10, 0}, {10, 10}}, true), 1);
  REQUIRE(opened.size() == 1);
  CHECK_FALSE(opened[0].closed);
  REQUIRE(opened[0].anchors.size() == 4);
  CheckPoint(opened[0].anchors.front().position, {10, 0});
  CheckPoint(opened[0].anchors.back().position, {10, 0});

  const auto split = CutAt(Polyline({{0, 0}, {10, 0}, {20, 0}}), 1);
  REQUIRE(split.size() == 2);
  CHECK(split[0].anchors.size() == 2);
  CHECK(split[1].anchors.size() == 2);
  CHECK(CutAt(Polyline({{0, 0}, {10, 0}}), 0).size() == 1);  // At an end: nothing to cut.
}

TEST_CASE("DeleteAnchors removes the touching segments and splits the rest") {
  const PathData line = Polyline({{0, 0}, {10, 0}, {20, 0}, {30, 0}, {40, 0}});
  const auto split = DeleteAnchors(line, {2});
  REQUIRE(split.size() == 2);
  CheckPoint(split[0].anchors.back().position, {10, 0});
  CheckPoint(split[1].anchors.front().position, {30, 0});
  CHECK(DeleteAnchors(line, {1, 3}).empty());  // Only single anchors remain.
  CHECK(DeleteAnchors(line, {0}).at(0).anchors.size() == 4);

  const PathData square = Polyline({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true);
  const auto opened = DeleteAnchors(square, {0});
  REQUIRE(opened.size() == 1);
  CHECK_FALSE(opened[0].closed);
  CheckPoint(opened[0].anchors.front().position, {10, 0});
  CheckPoint(opened[0].anchors.back().position, {0, 10});
  CHECK(DeleteAnchors(square, {}).at(0).closed);
}

TEST_CASE("Join connects chosen ends and merges coincident ones") {
  const PathData a = Polyline({{0, 0}, {10, 0}});
  const PathData b = Polyline({{20, 0}, {30, 0}});
  const PathData joined = Join(a, true, b, false);  // a's end to b's start.
  REQUIRE(joined.anchors.size() == 4);
  CheckPoint(joined.anchors[2].position, {20, 0});

  const PathData touching = Polyline({{10, 0}, {10, 10}});
  const PathData merged = Join(a, true, touching, false);
  CHECK(merged.anchors.size() == 3);  // The shared point appears once.

  const PathData reversed = Join(a, false, b, true);  // a's start to b's end.
  CheckPoint(reversed.anchors.front().position, {10, 0});
  CheckPoint(reversed.anchors.back().position, {20, 0});
}

TEST_CASE("Reversed swaps handle sides") {
  const PathData reversed = Reversed(Arch());
  CheckPoint(reversed.anchors[0].position, {100, 0});
  CheckPoint(reversed.anchors[0].handle_out, {0, 100});
  CheckPoint(SegmentAt(reversed, 0).Evaluate(0.5), {50, 75});
}
