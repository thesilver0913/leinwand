// SPDX-License-Identifier: GPL-3.0-or-later
// Point text as drawn on the canvas and in PNG export, on the bundled fonts.
#include <catch2/catch_test_macros.hpp>
#include <numbers>

#include "core/gradient.h"
#include "image_compare.h"
#include "render/document_renderer.h"
#include "text/layout.h"

using namespace leinwand;
using render::DocumentRenderer;
using render::View;

namespace {

core::ObjectPtr Text(const std::string& id, std::u32string content, core::Point at,
                     core::CharacterStyle style, core::Appearance appearance,
                     core::ParagraphStyle paragraph = {}, core::Matrix extra = {}) {
  core::TextObject text;
  text.common.id = id;
  text.common.appearance = std::move(appearance);
  text.story = std::make_shared<const core::Story>(
      core::MakeStory(id + "-story", std::move(content), style, paragraph));
  text.transform = core::Matrix::Translate(at.x, at.y) * extra;
  return core::MakeObject(std::move(text));
}

core::Document TextDocument() {
  core::Layer layer;
  layer.id = "l";
  const core::Fill black{core::RgbColor{0.1, 0.1, 0.1}};

  core::CharacterStyle body;
  body.size = 24;
  core::CharacterStyle first = body;
  first.size = 20;
  layer.children.push_back(
      Text("mixed", U"Leinwand で日本語と English を組む。", {20, 50}, first, {black}));

  // Two paragraphs, centred, with a bold run and a smaller one.
  core::Story story = core::MakeStory("centre-story", U"見出し Bold\n小さな文字 small", body);
  story = core::WithCharacterStyle(
      story, 4, 8, [](core::CharacterStyle& s) { s.font = {"Source Sans 3", "Bold", {}}; });
  story = core::WithCharacterStyle(story, 9, story.text.size(),
                                   [](core::CharacterStyle& s) { s.size = 14; });
  story = core::WithParagraphStyle(story, 0, story.text.size(), [](core::ParagraphStyle& p) {
    p.align = core::TextAlign::kCenter;
  });
  core::TextObject centre;
  centre.common.id = "centre";
  centre.common.appearance = {black};
  centre.story = std::make_shared<const core::Story>(std::move(story));
  centre.transform = core::Matrix::Translate(200, 110);
  layer.children.push_back(core::MakeObject(std::move(centre)));

  // A gradient fill under a stroke, tracking and wide characters.
  core::Fill gradient{core::RgbColor{0.18, 0.42, 0.87}};
  gradient.gradient =
      core::DefaultGradient(core::GradientType::kLinear, core::Rect::FromXYWH(20, 160, 300, 40),
                            core::RgbColor{0.18, 0.42, 0.87}, core::RgbColor{1.0, 0.54, 0.0});
  core::Stroke outline{core::RgbColor{0, 0, 0}};
  outline.width = 0.75;
  core::CharacterStyle wide = body;
  wide.size = 36;
  wide.tracking = 100;
  wide.horizontal_scale = 1.3;
  wide.font = {"Source Sans 3", "Bold", {}};
  layer.children.push_back(Text("paint", U"Gradient", {20, 200}, wide, {outline, gradient}));

  // The whole text rotated, and single characters rotated.
  core::CharacterStyle turned = body;
  turned.rotation = 30;
  layer.children.push_back(Text("rotated", U"回る文字", {300, 260}, turned, {black}, {},
                                core::Matrix::Rotate(-std::numbers::pi / 12)));

  core::Document document;
  document.artboards = {{"ab", "Artboard 1", core::Rect::FromXYWH(0, 0, 400, 300), {}, 0}};
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

}  // namespace

TEST_CASE("Point text matches its baseline image") {
  DocumentRenderer renderer;
  const auto pixels = renderer.RenderRaster(TextDocument(), 400, 300, View{0, 0, 1});
  CHECK(testing::MatchesBaseline("render/text", pixels, 400, 300));
}

TEST_CASE("Text draws exactly like its outlines") {
  // Text is drawn from the same glyph outlines Create Outlines makes.
  core::Document document = TextDocument();
  core::Document outlined = document;
  core::Layer layer = *outlined.layers.front();
  for (auto& child : layer.children) {
    const auto& object = std::get<core::ObjectPtr>(child);
    const auto* text = std::get_if<core::TextObject>(object.get());
    if (!text) continue;
    core::CompoundPathObject path;
    path.common = text->common;
    path.subpaths = text::OutlineOf(*text);
    child = core::MakeObject(std::move(path));
  }
  outlined.layers = {core::MakeLayer(std::move(layer))};
  DocumentRenderer renderer;
  const auto a = renderer.RenderRaster(document, 400, 300, View{0, 0, 1});
  const auto b = renderer.RenderRaster(outlined, 400, 300, View{0, 0, 1});
  CHECK(testing::DifferingFraction(a, b) < 0.0005);
}
