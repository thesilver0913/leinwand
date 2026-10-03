// SPDX-License-Identifier: GPL-3.0-or-later
// The text engine (spec 5.1), stage 1: point text, horizontal. A story is
// split into paragraphs (one line each: point text does not wrap), into runs
// by style, script and font (with fallback), shaped with HarfBuzz, and
// placed. Grapheme clusters come from ICU. Coordinates are the text
// object's: the first baseline at y = 0, y down, points.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "core/object.h"
#include "core/text.h"
#include "core/types.h"
#include "text/font.h"

namespace leinwand::text {

// Glyphs of one face and one character style.
struct GlyphRun {
  FacePtr face;
  double size = 12;
  double horizontal_scale = 1, vertical_scale = 1;
  double rotation = 0;  // Degrees, counter-clockwise, about each glyph's centre.
  std::vector<std::uint16_t> glyphs;
  std::vector<core::Point> positions;  // Each glyph's origin on its (shifted) baseline.
  std::vector<double> advances;
  std::vector<std::size_t> clusters;  // The story index each glyph starts at.
  bool fallback = false;              // The style's font lacked these characters.
};

struct Line {
  std::size_t start = 0, end = 0;  // Story indices; `end` is before the break.
  double baseline = 0;
  double ascent = 0, descent = 0;  // Above and below the baseline.
  double left = 0, right = 0;      // Where the text starts and ends.
  // Where the caret goes before each index start..end (end + 1 values).
  std::vector<double> carets;
};

struct Layout {
  std::vector<Line> lines;
  std::vector<GlyphRun> runs;
  // Story indices where the caret may stop: grapheme cluster boundaries.
  std::vector<std::size_t> boundaries;
  core::Rect bounds;  // The lines' boxes (ascent to descent, start to end).
  bool missing_fonts = false;
};
using LayoutPtr = std::shared_ptr<const Layout>;

// Laid out once per story (and font setup), then shared.
LayoutPtr LayoutOf(const core::StoryPtr& story);

// Glyph outlines in the text's coordinates, and in the parent's.
std::vector<core::PathData> Outlines(const Layout& layout);
// The same, one list of contours per glyph (blank glyphs left out).
std::vector<std::vector<core::PathData>> GlyphOutlines(const Layout& layout);
// One glyph of a run, in the text's coordinates.
std::vector<core::PathData> RunGlyphOutline(const GlyphRun& run, std::size_t glyph);
std::vector<core::PathData> OutlineOf(const core::TextObject& text);
// The lines' boxes in the parent's coordinates (an empty story still has a
// caret-sized box).
core::Rect BoundsOf(const core::TextObject& text);
// Whether `p` (parent's coordinates) is on a line of the text.
bool Hits(const core::TextObject& text, core::Point p, double tolerance);

// Editing helpers; indices are story indices, points the text's coordinates.
std::size_t LineOf(const Layout& layout, std::size_t index);
std::size_t CaretAt(const Layout& layout, core::Point p);
std::size_t NextBoundary(const Layout& layout, std::size_t index);
std::size_t PreviousBoundary(const Layout& layout, std::size_t index);
// The same x on the line above or below (index unchanged at the ends).
std::size_t LineUp(const Layout& layout, std::size_t index, double x);
std::size_t LineDown(const Layout& layout, std::size_t index, double x);
double CaretX(const Layout& layout, std::size_t index);
// The caret as a line from its top to its bottom.
std::pair<core::Point, core::Point> CaretLine(const Layout& layout, std::size_t index);
// Boxes covering [from, to), one per line.
std::vector<core::Rect> SelectionBoxes(const Layout& layout, std::size_t from, std::size_t to);
// Word boundaries around `index` (double-click), as story indices.
std::pair<std::size_t, std::size_t> WordAt(const core::Story& story, std::size_t index);

}  // namespace leinwand::text
