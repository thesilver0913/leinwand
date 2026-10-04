// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/text.h"

#include <catch2/catch_test_macros.hpp>

using namespace leinwand::core;

namespace {

CharacterStyle Sized(double size) {
  CharacterStyle style;
  style.size = size;
  return style;
}

}  // namespace

TEST_CASE("UTF-8 and code points convert both ways, bad bytes become U+FFFD") {
  const std::string text = "Aあ永😀";
  const std::u32string points = FromUtf8(text);
  REQUIRE(points.size() == 4);
  CHECK(points[3] == U'\U0001F600');
  CHECK(ToUtf8(points) == text);
  CHECK(FromUtf8("a\xff") == U"a�");
  CHECK(FromUtf8("\xe3\x81") == U"�");  // Cut short.
}

TEST_CASE("A story keeps one paragraph style per paragraph") {
  const Story story = MakeStory("s", U"一行目\n二行目\n三");
  CHECK(story.paragraphs.size() == 3);
  CHECK(IsConsistent(story));
  CHECK(ParagraphOf(story, 0) == 0);
  CHECK(ParagraphOf(story, 4) == 1);
  CHECK(ParagraphStart(story, 1) == 4);
  CHECK(ParagraphEnd(story, 1) == 7);
  CHECK(ParagraphEnd(story, 2) == 9);
}

TEST_CASE("Inserted text takes the style before it, or the one given") {
  Story story = MakeStory("s", U"ab", Sized(10));
  story = WithCharacterStyle(story, 1, 2, [](CharacterStyle& s) { s.size = 20; });
  REQUIRE(story.characters.size() == 2);
  Story typed = Inserted(story, 2, U"c");
  CHECK(typed.characters.back().length == 2);  // Joined the 20 pt run.
  CHECK(StyleAt(typed, 2).size == 20);
  const CharacterStyle big = Sized(30);
  typed = Inserted(typed, 0, U"x", &big);
  CHECK(StyleAt(typed, 0).size == 30);
  CHECK(IsConsistent(typed));
}

TEST_CASE("Breaks split paragraphs; erasing them joins, keeping the first style") {
  Story story = MakeStory("s", U"abcd");
  story = WithParagraphStyle(story, 0, 0, [](ParagraphStyle& p) { p.align = TextAlign::kRight; });
  story = Inserted(story, 2, U"\n");
  REQUIRE(story.paragraphs.size() == 2);
  CHECK(story.paragraphs[1].align == TextAlign::kRight);  // Copied.
  story = WithParagraphStyle(story, 3, 3, [](ParagraphStyle& p) { p.align = TextAlign::kCenter; });
  CHECK(story.paragraphs[0].align == TextAlign::kRight);
  story = Erased(story, 1, 4);
  CHECK(story.text == U"ad");
  REQUIRE(story.paragraphs.size() == 1);
  CHECK(story.paragraphs[0].align == TextAlign::kRight);
  CHECK(IsConsistent(story));
}

TEST_CASE("Erasing everything keeps the style to type with") {
  Story story = MakeStory("s", U"abc", Sized(40));
  story = Erased(story, 0, 3);
  CHECK(story.text.empty());
  REQUIRE(story.characters.size() == 1);
  CHECK(story.characters[0].length == 0);
  CHECK(story.characters[0].style.size == 40);
  CHECK(IsConsistent(story));
}

TEST_CASE("Character styles apply to a range and list what is in it") {
  Story story = MakeStory("s", U"abcdef", Sized(10));
  story = WithCharacterStyle(story, 2, 4, [](CharacterStyle& s) { s.tracking = 100; });
  REQUIRE(story.characters.size() == 3);
  CHECK(StylesIn(story, 0, 6).size() == 2);
  CHECK(StylesIn(story, 2, 4).size() == 1);
  CHECK(StylesIn(story, 3, 3).front().tracking == 100);  // The one before the caret.
  story = WithCharacterStyle(story, 0, 6, [](CharacterStyle& s) { s.tracking = 0; });
  CHECK(story.characters.size() == 1);  // Equal neighbours join.
  CHECK(LeadingOf(Sized(10)) == 17.5);
}
