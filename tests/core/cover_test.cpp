// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cover.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/document.h"

using namespace leinwand::core;

namespace {

const Artboard* Find(const Document& document, const std::string& id) {
  for (const Artboard& a : document.artboards) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("The spine is half the pages times the paper's thickness") {
  CoverSpec spec;
  spec.pages = 36;
  spec.paper_thickness = 0.3;
  CHECK(SpineWidth(spec) == Catch::Approx(5.4));
  spec.spine = 12.0;  // Given directly.
  CHECK(SpineWidth(spec) == Catch::Approx(12.0));
}

TEST_CASE("WithCover lays out back, spine and front over the spread") {
  CoverSpec spec{100, 150, 40, 0.5, std::nullopt, 9};  // Spine 10.
  const Document document = WithCover({}, spec);
  REQUIRE(document.artboards.size() == 4);
  CHECK(Find(document, kCoverSpreadId)->bounds == Rect::FromXYWH(0, 0, 210, 150));
  CHECK(Find(document, kCoverSpreadId)->bleed == 9);
  CHECK(Find(document, kCoverBackId)->bounds == Rect::FromXYWH(0, 0, 100, 150));
  CHECK(Find(document, kCoverSpineId)->bounds == Rect::FromXYWH(100, 0, 10, 150));
  CHECK(Find(document, kCoverFrontId)->bounds == Rect::FromXYWH(110, 0, 100, 150));
  CHECK(document.cover == spec);
}

TEST_CASE("WithCover again resizes the cover and leaves the rest alone") {
  CoverSpec spec{100, 150, 40, 0.5, std::nullopt, 9};
  Document document = WithCover({}, spec);
  document.artboards[1].name = "Renamed";
  document.artboards.push_back({"other", "Other", Rect::FromXYWH(500, 0, 10, 10), {}, 0});
  spec.pages = 80;  // Spine 20.
  document = WithCover(std::move(document), spec);
  REQUIRE(document.artboards.size() == 5);
  CHECK(Find(document, kCoverFrontId)->bounds == Rect::FromXYWH(120, 0, 100, 150));
  CHECK(Find(document, kCoverBackId)->name == "Renamed");
  CHECK(Find(document, "other")->bounds == Rect::FromXYWH(500, 0, 10, 10));
}
