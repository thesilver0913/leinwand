// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/test_document.h"

#include <catch2/catch_test_macros.hpp>

#include "geometry/bezier.h"
#include "render/document_renderer.h"

using namespace leinwand;

TEST_CASE("MakeTestDocument builds the requested number of paths inside its artboard") {
  const core::Document document = render::MakeTestDocument(1000);
  int count = 0;
  core::Rect bounds;
  core::VisitObjects(document, [&](const core::Object& o) {
    ++count;
    bounds = bounds.Union(geometry::Bounds(o));
  });
  CHECK(count == 1000);
  REQUIRE(document.artboards.size() == 1);
  const core::Rect& board = document.artboards[0].bounds;
  CHECK(board.Union(bounds) == board);  // Every blob sits on the artboard.
}

TEST_CASE("MakeTestDocument is deterministic per seed") {
  render::DocumentRenderer renderer;
  const render::View view{0, 0, 0.25};
  const auto a = renderer.RenderRaster(render::MakeTestDocument(500, 7), 128, 128, view);
  const auto b = renderer.RenderRaster(render::MakeTestDocument(500, 7), 128, 128, view);
  const auto c = renderer.RenderRaster(render::MakeTestDocument(500, 8), 128, 128, view);
  CHECK(a == b);
  CHECK(a != c);
}
