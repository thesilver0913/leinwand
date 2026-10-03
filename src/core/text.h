// SPDX-License-Identifier: GPL-3.0-or-later
// Text content (spec 5, 5.1): a story is the characters with their
// character styles and paragraph styles. Text objects refer to a story; the
// layout (glyphs and positions) is not part of the model and is computed by
// the text engine (src/text) whenever it is needed.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace leinwand::core {

// A font as the document names it (spec 5.2): the file is not embedded.
struct FontRef {
  std::string family;
  std::string style;            // "Regular", "Bold", ...
  std::string postscript_name;  // Matched first when it is known.
  friend bool operator==(const FontRef&, const FontRef&) = default;
};

// The default font: the bundled Source Han Sans (spec 5.2, "フォールバック").
FontRef DefaultFont();

enum class KerningMode { kMetrics, kNone };

// Character attributes (spec 5, "文字属性"), stage 1.
struct CharacterStyle {
  FontRef font = DefaultFont();
  double size = 12.0;              // Points.
  std::optional<double> leading;   // Points; none means auto (kAutoLeading × size).
  double tracking = 0.0;           // 1/1000 em after each character.
  double baseline_shift = 0.0;     // Points, upwards.
  double horizontal_scale = 1.0;   // Glyph width.
  double vertical_scale = 1.0;     // Glyph height.
  double rotation = 0.0;           // Degrees, counter-clockwise, each character.
  KerningMode kerning = KerningMode::kMetrics;
  std::string unknown_fields;      // From a newer version, kept (spec 3.3).
  friend bool operator==(const CharacterStyle&, const CharacterStyle&) = default;
};

// Auto leading as Illustrator's Japanese version sets it.
inline constexpr double kAutoLeading = 1.75;
double LeadingOf(const CharacterStyle& style);

// Point text lines up on its anchor: starting at it, centred on it, or
// ending at it. The justified alignments come with area text (stage 2).
enum class TextAlign { kLeft, kCenter, kRight };

// Paragraph attributes (spec 5, "段落属性"), stage 1.
struct ParagraphStyle {
  TextAlign align = TextAlign::kLeft;
  double left_indent = 0.0;
  double right_indent = 0.0;
  double first_line_indent = 0.0;
  double space_before = 0.0;
  double space_after = 0.0;
  std::string unknown_fields;
  friend bool operator==(const ParagraphStyle&, const ParagraphStyle&) = default;
};

struct CharacterRun {
  std::size_t length = 0;  // Code points.
  CharacterStyle style;
  friend bool operator==(const CharacterRun&, const CharacterRun&) = default;
};

// The characters (code points; U+000A ends a paragraph) and their styles.
// Character runs cover the text exactly; there is always at least one, so
// that an empty story still knows the style to type in. Paragraph styles
// are one per paragraph: one more than there are paragraph breaks.
struct Story {
  std::string id;
  std::u32string text;
  std::vector<CharacterRun> characters{CharacterRun{}};
  std::vector<ParagraphStyle> paragraphs{ParagraphStyle{}};
  std::string unknown_fields;
  friend bool operator==(const Story&, const Story&) = default;
};

using StoryPtr = std::shared_ptr<const Story>;

// A story with `text` in one style and one paragraph style per paragraph.
Story MakeStory(std::string id, std::u32string text, const CharacterStyle& style = {},
                const ParagraphStyle& paragraph = {});

// Whether the runs and paragraphs agree with the text (used when reading).
bool IsConsistent(const Story& story);

// Edits; indices are code points, ranges are [from, to).
// Inserted text takes the style of the character before `at` (or of the
// first one), or `style` when given. A paragraph break inserted splits the
// paragraph; both halves keep its style.
Story Inserted(const Story& story, std::size_t at, std::u32string_view text,
               const CharacterStyle* style = nullptr);
// Joined paragraphs keep the first one's style.
Story Erased(const Story& story, std::size_t from, std::size_t to);
// An empty range changes nothing (the editor keeps a pending style for it).
Story WithCharacterStyle(const Story& story, std::size_t from, std::size_t to,
                         const std::function<void(CharacterStyle&)>& edit);
// Every paragraph the range touches; an empty range, the one it is in.
Story WithParagraphStyle(const Story& story, std::size_t from, std::size_t to,
                         const std::function<void(ParagraphStyle&)>& edit);

// The style of the character at `index` (of the last one at the end).
const CharacterStyle& StyleAt(const Story& story, std::size_t index);
// The character styles used in [from, to), in order, without repeats; the
// style at `from` for an empty range.
std::vector<CharacterStyle> StylesIn(const Story& story, std::size_t from, std::size_t to);
// The paragraph `index` lies in, and where paragraph `paragraph` starts and
// ends (the end is its break, or the end of the text).
std::size_t ParagraphOf(const Story& story, std::size_t index);
std::size_t ParagraphStart(const Story& story, std::size_t paragraph);
std::size_t ParagraphEnd(const Story& story, std::size_t paragraph);

// UTF-8 <-> code points. Invalid UTF-8 becomes U+FFFD.
std::u32string FromUtf8(std::string_view text);
std::string ToUtf8(std::u32string_view text);

}  // namespace leinwand::core
