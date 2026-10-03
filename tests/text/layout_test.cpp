// SPDX-License-Identifier: GPL-3.0-or-later
// The text engine on the bundled fonts (Source Han Sans JP, Source Sans 3).
#include "text/layout.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "text/font.h"

using namespace leinwand;
using Catch::Approx;

namespace {

core::StoryPtr StoryOf(std::u32string text, core::CharacterStyle style = {},
                       core::ParagraphStyle paragraph = {}) {
  return std::make_shared<const core::Story>(
      core::MakeStory("s", std::move(text), style, paragraph));
}

}  // namespace

TEST_CASE("The bundled fonts are found by name, with their own names") {
  const text::FacePtr han = text::FindFace(core::DefaultFont());
  REQUIRE(han);
  CHECK(han->family() == "Source Han Sans JP");
  CHECK(han->HasGlyph(U'永'));
  CHECK(text::FindFace({"Source Sans 3", "Bold", {}}));
  CHECK(!text::FindFace({"No Such Font", "Regular", {}}));
  CHECK(!text::IsAvailable({"No Such Font", "Regular", {}}));
  // A missing font is shown in the default one.
  CHECK(text::FaceOrSubstitute({"No Such Font", "Regular", {}}) == han);
}

TEST_CASE("Mixed Japanese and English is shaped without missing glyphs") {
  const auto layout = text::LayoutOf(StoryOf(U"Leinwand で日本語と English を組む。fi"));
  REQUIRE(layout->lines.size() == 1);
  std::size_t glyphs = 0;
  for (const auto& run : layout->runs) {
    for (auto g : run.glyphs) CHECK(g != 0);
    glyphs += run.glyphs.size();
  }
  CHECK(glyphs > 20);
  // Carets run left to right and end at the line's end.
  const auto& line = layout->lines[0];
  for (std::size_t i = 1; i < line.carets.size(); ++i) CHECK(line.carets[i] >= line.carets[i - 1]);
  CHECK(line.carets.back() == Approx(line.right));
  CHECK(layout->bounds.top < 0);
  CHECK(layout->bounds.bottom > 0);
}

TEST_CASE("Characters the font lacks come from the fallback font") {
  core::CharacterStyle latin;
  latin.font = {"Source Sans 3", "Regular", {}};
  const auto layout = text::LayoutOf(StoryOf(U"Ab日本", latin));
  bool fallback = false;
  for (const auto& run : layout->runs) {
    for (auto g : run.glyphs) CHECK(g != 0);
    fallback = fallback || (run.fallback && run.face->family() == "Source Han Sans JP");
  }
  CHECK(fallback);
}

TEST_CASE("Lines follow by leading and paragraph spacing; alignment is on the anchor") {
  core::CharacterStyle style;
  style.size = 10;
  core::ParagraphStyle paragraph;
  paragraph.space_after = 5;
  auto layout = text::LayoutOf(StoryOf(U"one\ntwo\n", style, paragraph));
  REQUIRE(layout->lines.size() == 3);
  CHECK(layout->lines[1].baseline == Approx(17.5 + 5));
  CHECK(layout->lines[2].start == layout->lines[2].end);  // The empty last line.

  paragraph.align = core::TextAlign::kCenter;
  layout = text::LayoutOf(StoryOf(U"centre", style, paragraph));
  CHECK(layout->lines[0].left == Approx(-layout->lines[0].right));
  paragraph.align = core::TextAlign::kRight;
  layout = text::LayoutOf(StoryOf(U"right", style, paragraph));
  CHECK(layout->lines[0].right == Approx(0));
}

TEST_CASE("Tracking and scaling widen the text") {
  core::CharacterStyle style;
  const double plain = text::LayoutOf(StoryOf(U"abc", style))->lines[0].right;
  style.tracking = 100;
  CHECK(text::LayoutOf(StoryOf(U"abc", style))->lines[0].right == Approx(plain + 3 * 1.2));
  style.tracking = 0;
  style.horizontal_scale = 2;
  CHECK(text::LayoutOf(StoryOf(U"abc", style))->lines[0].right == Approx(plain * 2));
}

TEST_CASE("The caret moves by grapheme cluster and between lines") {
  // A family emoji is one cluster of five code points.
  const std::u32string family = U"\U0001F468‍\U0001F469‍\U0001F467";
  const auto layout = text::LayoutOf(StoryOf(U"a" + family + U"b\nxy"));
  CHECK(text::NextBoundary(*layout, 1) == 6);
  CHECK(text::PreviousBoundary(*layout, 6) == 1);
  CHECK(text::LineOf(*layout, 7) == 0);  // Before the break.
  CHECK(text::LineOf(*layout, 8) == 1);
  const double x = text::CaretX(*layout, 1);
  CHECK(text::LineDown(*layout, 1, x) >= 8);
  CHECK(text::LineUp(*layout, 9, text::CaretX(*layout, 9)) <= 7);
  // A click lands on the nearest caret position.
  CHECK(text::CaretAt(*layout, {-5, 0}) == 0);
  CHECK(text::CaretAt(*layout, {1000, 0}) == 7);
  const auto boxes = text::SelectionBoxes(*layout, 0, 9);
  CHECK(boxes.size() == 2);
}

TEST_CASE("Outlines and bounds follow the text's transform") {
  core::TextObject text;
  text.story = StoryOf(U"永");
  text.transform = core::Matrix::Translate(100, 50);
  const auto outline = text::OutlineOf(text);
  REQUIRE(!outline.empty());
  for (const auto& path : outline) CHECK(path.closed);
  const core::Rect bounds = text::BoundsOf(text);
  CHECK(bounds.left == Approx(100));
  CHECK(bounds.top < 50);
  CHECK(text::Hits(text, {105, 46}, 0));
  CHECK(!text::Hits(text, {80, 46}, 0));
  CHECK(text::GlyphOutlines(*text::LayoutOf(text.story)).size() == 1);
}

TEST_CASE("Word boundaries for double-clicks") {
  const core::Story story = core::MakeStory("s", U"hello world");
  const auto [from, to] = text::WordAt(story, 7);
  CHECK(from == 6);
  CHECK(to == 11);
}
