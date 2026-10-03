// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor/preflight.h"

#include <catch2/catch_test_macros.hpp>
#include <map>

using namespace leinwand;
using editor::PreflightCheck;

namespace {

core::ObjectPtr Rect(const std::string& id, double x, double y, double w, double h,
                     core::Appearance appearance = {core::Fill{core::RgbColor{1, 0, 0}}}) {
  core::PathObject path;
  path.common.id = id;
  path.common.appearance = std::move(appearance);
  path.path.anchors = {{{x, y}}, {{x + w, y}}, {{x + w, y + h}}, {{x, y + h}}};
  path.path.closed = true;
  return core::MakeObject(std::move(path));
}

std::map<PreflightCheck, std::vector<std::string>> Run(const core::Document& document,
                                                       editor::PreflightSettings settings = {}) {
  std::map<PreflightCheck, std::vector<std::string>> out;
  for (const auto& issue : editor::Preflight(document, settings)) out[issue.check] = issue.ids;
  return out;
}

}  // namespace

TEST_CASE("Preflight finds live and empty text, stray points and thin strokes") {
  core::Layer layer;
  layer.id = "l";
  core::TextObject text;
  text.common.id = "text";
  text.story = std::make_shared<const core::Story>(core::MakeStory("s1", U"文字"));
  core::TextObject empty;
  empty.common.id = "empty";
  empty.story = std::make_shared<const core::Story>(core::MakeStory("s2", U""));
  core::PathObject point;
  point.common.id = "point";
  point.path.anchors = {{{10, 10}}};
  core::Stroke hairline{core::RgbColor{0, 0, 0}};
  hairline.width = 0.25;  // Under 0.1 mm (0.283 pt).
  core::Stroke fine{core::RgbColor{0, 0, 0}};
  fine.width = 0.5;
  // 0.5 pt in a group scaled to half is 0.25 pt on paper.
  core::GroupObject shrunk;
  shrunk.common.id = "shrunk";
  shrunk.transform = core::Matrix::Scale(0.5, 0.5);
  shrunk.children = {Rect("inner", 0, 0, 10, 10, {fine})};
  layer.children = {core::MakeObject(text),
                    core::MakeObject(empty),
                    core::MakeObject(point),
                    Rect("thin", 100, 100, 10, 10, {hairline}),
                    Rect("ok", 100, 120, 10, 10, {fine}),
                    core::MakeObject(shrunk)};
  core::Document document;
  document.artboards = {{"ab", "A", core::Rect::FromXYWH(0, 0, 500, 500), {}, 0}};
  document.layers = {core::MakeLayer(std::move(layer))};

  auto found = Run(document);
  CHECK(found[PreflightCheck::kLiveText] == std::vector<std::string>{"text"});
  CHECK(found[PreflightCheck::kEmptyText] == std::vector<std::string>{"empty"});
  CHECK(found[PreflightCheck::kStrayPoint] == std::vector<std::string>{"point"});
  CHECK(found[PreflightCheck::kThinStroke] == std::vector<std::string>{"thin", "inner"});

  editor::PreflightSettings off;
  off.enabled[static_cast<int>(PreflightCheck::kLiveText)] = false;
  off.min_stroke = 0.1;
  found = Run(document, off);
  CHECK(!found.contains(PreflightCheck::kLiveText));
  CHECK(!found.contains(PreflightCheck::kThinStroke));
}

TEST_CASE("Preflight: artwork that reaches the trim must reach the bleed") {
  core::Layer layer;
  layer.id = "l";
  layer.children = {
      Rect("short", 0, 0, 100, 100),    // Stops at the trim.
      Rect("full", -9, 200, 100, 100),  // Runs into the 3 mm bleed.
      Rect("inside", 50, 50, 10, 10),   // Nowhere near an edge.
      Rect("outline", 400, 400, 200, 200, {core::Stroke{core::RgbColor{0, 0, 0}}}),  // No fill.
  };
  core::Document document;
  document.artboards = {{"ab", "A", core::Rect::FromXYWH(0, 0, 500, 500), {}, 0}};
  document.layers = {core::MakeLayer(std::move(layer))};
  const auto found = Run(document);
  REQUIRE(found.contains(PreflightCheck::kShortOfBleed));
  CHECK(found.at(PreflightCheck::kShortOfBleed) == std::vector<std::string>{"short"});
}

TEST_CASE("Preflight lists hidden and locked objects, layers included") {
  core::Layer hidden;
  hidden.id = "hidden-layer";
  hidden.visible = false;
  hidden.children = {Rect("in-hidden", 0, 0, 1, 1)};
  core::Layer locked;
  locked.id = "locked-layer";
  locked.locked = true;
  locked.children = {Rect("in-locked", 0, 0, 1, 1)};
  core::Layer plain;
  plain.id = "plain";
  core::PathObject off;
  off.common.id = "off";
  off.common.visible = false;
  plain.children = {core::MakeObject(off)};
  core::Document document;
  document.layers = {core::MakeLayer(hidden), core::MakeLayer(locked), core::MakeLayer(plain)};
  const auto found = Run(document);
  CHECK(found.at(PreflightCheck::kHidden) == std::vector<std::string>{"in-hidden", "off"});
  CHECK(found.at(PreflightCheck::kLocked) == std::vector<std::string>{"in-locked"});
}
