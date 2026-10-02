// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/view.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using leinwand::core::Point;
using leinwand::core::Rect;
using leinwand::render::View;

TEST_CASE("View converts between document and view coordinates") {
  const View view{10, 20, 2};
  CHECK(view.ToView({5, 5}) == Point{20, 30});
  CHECK(view.ToDocument({20, 30}) == Point{5, 5});
}

TEST_CASE("ZoomedAt keeps the point under the cursor in place") {
  const View view{10, 20, 1.5};
  const Point cursor{300, 200};
  const Point before = view.ToDocument(cursor);
  const View zoomed = view.ZoomedAt(cursor, 2.0);
  CHECK(zoomed.zoom == Approx(3.0));
  CHECK(zoomed.ToDocument(cursor).x == Approx(before.x));
  CHECK(zoomed.ToDocument(cursor).y == Approx(before.y));
}

TEST_CASE("Zoom stays within Illustrator's range") {
  CHECK(View{}.ZoomedAt({0, 0}, 1e9).zoom == Approx(View::kMaxZoom));
  CHECK(View{}.ZoomedAt({0, 0}, 1e-9).zoom == Approx(View::kMinZoom));
}

TEST_CASE("Fit centres the rect with a margin") {
  // A 100x50 rect in a 220x220 viewport with a 10 px margin: width limits.
  const View view = View::Fit(Rect::FromXYWH(0, 0, 100, 50), 220, 220, 10);
  CHECK(view.zoom == Approx(2.0));
  CHECK(view.ToView({0, 0}).x == Approx(10));
  CHECK(view.ToView({50, 25}).x == Approx(110));  // Rect centre at viewport centre.
  CHECK(view.ToView({50, 25}).y == Approx(110));
  // An offset rect lands in the same place.
  const View offset = View::Fit(Rect::FromXYWH(500, 500, 100, 50), 220, 220, 10);
  CHECK(offset.ToView({550, 525}).x == Approx(110));
}

TEST_CASE("Zoom steps follow the preset levels") {
  CHECK(View::NextZoomIn(1.0) == Approx(1.5));
  CHECK(View::NextZoomOut(1.0) == Approx(0.6667));
  CHECK(View::NextZoomIn(1.1) == Approx(1.5));  // Off-preset zooms snap to the next.
  CHECK(View::NextZoomOut(1.1) == Approx(1.0));
  CHECK(View::NextZoomIn(640.0) == Approx(View::kMaxZoom));
  CHECK(View::NextZoomOut(View::kMinZoom) == Approx(View::kMinZoom));
}
