// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/style.h"

#include <catch2/catch_test_macros.hpp>
#include <variant>

using namespace leinwand::core;

namespace {

const RgbColor kRed{1, 0, 0};
const RgbColor kBlue{0, 0, 1};

ObjectPtr Path(const std::string& id, Appearance appearance) {
  PathObject path;
  path.common.id = id;
  path.common.appearance = std::move(appearance);
  path.path.anchors = {{{0, 0}}, {{10, 0}}, {{10, 10}}};
  return MakeObject(std::move(path));
}

Document With(std::vector<ObjectPtr> objects) {
  Layer layer;
  layer.id = "layer";
  for (auto& o : objects) layer.children.push_back(std::move(o));
  Document document;
  document.layers = {MakeLayer(std::move(layer))};
  return document;
}

const Appearance& AppearanceOf(const Document& document, const std::string& id) {
  return CommonOf(*document.FindObject(id)).appearance;
}

}  // namespace

TEST_CASE("Fill and stroke paints edit the front items; none removes them") {
  Appearance a = {Stroke{kBlue}, Fill{kRed}};
  SetFillPaint(a, RgbColor{0, 1, 0});
  REQUIRE(FrontFill(a));
  CHECK(FrontFill(a)->paint == Color{RgbColor{0, 1, 0}});
  SetStrokePaint(a, std::nullopt);
  CHECK_FALSE(FrontStroke(a));
  CHECK(a.size() == 1);

  SetStrokePaint(a, kBlue);  // Added in front, 1 pt.
  REQUIRE(std::holds_alternative<Stroke>(a.front()));
  CHECK(std::get<Stroke>(a.front()).width == 1.0);
  SetFillPaint(a, std::nullopt);
  SetFillPaint(a, kRed);  // Added at the back.
  CHECK(std::holds_alternative<Fill>(a.back()));
}

TEST_CASE("Swapping fill and stroke carries none across") {
  Appearance a = {Fill{kRed}};
  SwapFillAndStroke(a);
  CHECK_FALSE(FrontFill(a));
  REQUIRE(FrontStroke(a));
  CHECK(FrontStroke(a)->paint == Color{kRed});
}

TEST_CASE("EditAppearance reaches into groups and shares untouched objects") {
  GroupObject group;
  group.common.id = "g";
  group.children = {Path("a", {Fill{kRed}}), Path("b", {})};
  const ObjectPtr other = Path("c", {Fill{kRed}});
  const Document document = With({MakeObject(group), other});
  const Document result =
      EditAppearance(document, {"g"}, [](Appearance& a) { SetFillPaint(a, kBlue); });
  CHECK(FrontFill(AppearanceOf(result, "a"))->paint == Color{kBlue});
  CHECK(FrontFill(AppearanceOf(result, "b"))->paint == Color{kBlue});
  CHECK(AppearanceOf(result, "g").empty());  // The group itself keeps none.
  CHECK(std::get<ObjectPtr>(result.layers[0]->children[1]) == other);
}

TEST_CASE("Opacity is clamped and set on the listed objects") {
  const Document document = With({Path("a", {}), Path("b", {})});
  const Document result = SetOpacity(document, {"a"}, 1.5);
  CHECK(CommonOf(*result.FindObject("a")).opacity == 1.0);
  CHECK(CommonOf(*SetOpacity(document, {"b"}, 0.25).FindObject("b")).opacity == 0.25);
}

TEST_CASE("Removing a swatch turns its uses into plain colors") {
  Document document = With({Path("a", {Fill{SwatchRef{"s"}}, Stroke{SpotColor{"s", 0.5}}}),
                            Path("b", {Fill{SwatchRef{"t"}}})});
  document.swatches = {{"s", "Spot", Swatch::Kind::kSpot, RgbColor{0, 0, 0}},
                       {"t", "Other", Swatch::Kind::kProcess, RgbColor{1, 0, 0}}};
  const Document result = RemoveSwatch(document, "s");
  REQUIRE(result.swatches.size() == 1);
  CHECK(FrontFill(AppearanceOf(result, "a"))->paint == Color{RgbColor{0, 0, 0}});
  CHECK(FrontStroke(AppearanceOf(result, "a"))->paint == Color{RgbColor{0.5, 0.5, 0.5}});
  CHECK(FrontFill(AppearanceOf(result, "b"))->paint == Color{SwatchRef{"t"}});
}

TEST_CASE("Colors resolve to RGB for display") {
  Document document;
  document.swatches = {{"s", "Spot", Swatch::Kind::kSpot, RgbColor{0, 0, 0}}};
  const auto half = ToRgb(SpotColor{"s", 0.5}, document);
  REQUIRE(half);
  CHECK(half->r == 0.5);
  CHECK(ToRgb(GrayColor{0.25}, document)->g == 0.25);
  CHECK(ToRgb(CmykColor{0, 1, 1, 0}, document) == RgbColor{1, 0, 0});
  CHECK_FALSE(ToRgb(SwatchRef{"missing"}, document));
}
