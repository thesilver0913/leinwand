// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <numbers>

using leinwand::core::Matrix;
using leinwand::core::Point;
using leinwand::core::Rect;

TEST_CASE("Default Rect is empty and is the identity of Union") {
  const Rect empty;
  CHECK_FALSE(empty.IsValid());
  const Rect r = Rect::FromXYWH(10, 20, 30, 40);
  CHECK(empty.Union(r) == r);
  CHECK(r.Union(empty) == r);
}

TEST_CASE("Rect union, containment and intersection") {
  const Rect a = Rect::FromXYWH(0, 0, 10, 10);
  const Rect b = Rect::FromXYWH(5, 5, 10, 10);
  CHECK(a.Union(b) == Rect{0, 0, 15, 15});
  CHECK(a.Intersects(b));
  CHECK_FALSE(a.Intersects(Rect::FromXYWH(20, 20, 1, 1)));
  CHECK(a.Contains({10, 10}));  // Edges are inside.
  CHECK_FALSE(a.Contains({10.01, 5}));
  CHECK(Rect::FromPoint({3, 4}).IsValid());  // A point is a degenerate rect.
  CHECK(a.Union(Point{-2, 3}) == Rect{-2, 0, 10, 10});
}

TEST_CASE("Matrix maps points and composes right to left") {
  const Matrix t = Matrix::Translate(10, 20);
  const Matrix s = Matrix::Scale(2, 3);
  CHECK(t.Map({1, 1}) == Point{11, 21});
  CHECK((t * s).Map({1, 1}) == Point{12, 23});  // Scale first, then translate.
  CHECK((s * t).Map({1, 1}) == Point{22, 63});
  CHECK(t.MapVector({1, 1}) == Point{1, 1});
  CHECK(Matrix{}.IsIdentity());
  CHECK_FALSE(t.IsIdentity());
}

TEST_CASE("Matrix rotation follows the Y-down coordinate system") {
  // A quarter turn maps +X to +Y, which is clockwise on screen.
  const Point p = Matrix::Rotate(std::numbers::pi / 2).Map({1, 0});
  CHECK(p.x == Catch::Approx(0).margin(1e-12));
  CHECK(p.y == Catch::Approx(1));
}
