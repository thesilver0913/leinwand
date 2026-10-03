// SPDX-License-Identifier: GPL-3.0-or-later
#include "text/layout.h"

#include <hb.h>
#include <unicode/ubrk.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <unordered_map>

#include "core/transform.h"
#include "text/font_impl.h"

namespace leinwand::text {

namespace {

using core::Point;
using core::Rect;

// UTF-16 for ICU, with the story index of each UTF-16 unit (and one past
// the end).
struct Utf16 {
  std::u16string text;
  std::vector<std::size_t> index;
};

Utf16 ToUtf16(const std::u32string& text) {
  Utf16 out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    char32_t c = text[i];
    if (c >= 0x10000) {
      c -= 0x10000;
      out.text.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
      out.text.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
      out.index.push_back(i);
      out.index.push_back(i);
    } else {
      out.text.push_back(static_cast<char16_t>(c));
      out.index.push_back(i);
    }
  }
  out.index.push_back(text.size());
  return out;
}

std::vector<std::size_t> Breaks(UBreakIteratorType type, const std::u32string& text) {
  std::vector<std::size_t> breaks;
  const Utf16 u = ToUtf16(text);
  UErrorCode status = U_ZERO_ERROR;
  UBreakIterator* it = ubrk_open(type, "ja", reinterpret_cast<const UChar*>(u.text.data()),
                                 static_cast<int32_t>(u.text.size()), &status);
  if (U_FAILURE(status)) {
    // Without ICU's data: every code point.
    for (std::size_t i = 0; i <= text.size(); ++i) breaks.push_back(i);
    return breaks;
  }
  for (int32_t b = ubrk_first(it); b != UBRK_DONE; b = ubrk_next(it)) {
    breaks.push_back(u.index[static_cast<std::size_t>(b)]);
  }
  ubrk_close(it);
  if (breaks.empty() || breaks.front() != 0) breaks.insert(breaks.begin(), 0);
  if (breaks.back() != text.size()) breaks.push_back(text.size());
  return breaks;
}

hb_script_t ScriptOf(char32_t c) {
  return hb_unicode_script(hb_unicode_funcs_get_default(), c);
}

bool IsNeutral(hb_script_t s) {
  return s == HB_SCRIPT_COMMON || s == HB_SCRIPT_INHERITED || s == HB_SCRIPT_UNKNOWN;
}

// A stretch of a paragraph shaped in one go.
struct Segment {
  std::size_t start, end;
  std::size_t run;  // Character run.
  FacePtr face;
  hb_script_t script;
  bool fallback;
};

LayoutPtr Compute(const core::Story& story) {
  auto layout = std::make_shared<Layout>();
  const std::u32string& text = story.text;
  layout->boundaries = Breaks(UBRK_CHARACTER, text);

  // Character run of each index.
  std::vector<std::size_t> run_of(text.size());
  {
    std::size_t i = 0;
    for (std::size_t r = 0; r < story.characters.size(); ++r) {
      for (std::size_t k = 0; k < story.characters[r].length && i < text.size(); ++k) {
        run_of[i++] = r;
      }
    }
  }
  // Whether a grapheme cluster starts at the index (fallback picks one face
  // per cluster, so that marks stay with their base).
  std::vector<bool> cluster_start(text.size() + 1, false);
  for (std::size_t b : layout->boundaries) cluster_start[b] = true;

  double previous_baseline = 0;
  double previous_space_after = 0;
  for (std::size_t p = 0; p < story.paragraphs.size(); ++p) {
    const core::ParagraphStyle& paragraph = story.paragraphs[p];
    const std::size_t ps = core::ParagraphStart(story, p), pe = core::ParagraphEnd(story, p);
    Line line;
    line.start = ps;
    line.end = pe;

    // Segments by run, face and script.
    std::vector<Segment> segments;
    hb_script_t script = HB_SCRIPT_COMMON;
    for (std::size_t i = ps; i < pe; ++i) {
      const core::CharacterStyle& style = story.characters[run_of[i]].style;
      FacePtr face;
      bool fallback = false;
      if (cluster_start[i] || segments.empty()) {
        FacePtr primary = FaceOrSubstitute(style.font);
        face = FaceForCharacter(text[i], style.font);
        if (!face) face = primary;
        fallback = face && primary && face != primary;
        if (!IsAvailable(style.font)) layout->missing_fonts = true;
      } else {
        face = segments.back().face;
        fallback = segments.back().fallback;
      }
      const hb_script_t s = ScriptOf(text[i]);
      if (!IsNeutral(s)) script = s;
      const bool same = !segments.empty() && segments.back().run == run_of[i] &&
                        segments.back().face == face &&
                        (IsNeutral(s) || segments.back().script == s ||
                         IsNeutral(segments.back().script));
      if (same) {
        segments.back().end = i + 1;
        if (IsNeutral(segments.back().script)) segments.back().script = script;
      } else {
        segments.push_back({i, i + 1, run_of[i], face, IsNeutral(s) ? script : s, fallback});
      }
    }

    // Shape and place along the baseline at y = 0.
    double x = 0;
    std::vector<std::pair<std::size_t, std::pair<double, double>>> clusters;  // index, x range
    std::size_t first_run = layout->runs.size();
    for (const Segment& segment : segments) {
      const core::CharacterStyle& style = story.characters[segment.run].style;
      if (!segment.face) continue;
      GlyphRun run;
      run.face = segment.face;
      run.size = style.size;
      run.horizontal_scale = style.horizontal_scale;
      run.vertical_scale = style.vertical_scale;
      run.rotation = style.rotation;
      run.fallback = segment.fallback;
      const Face::Impl& face = segment.face->impl();
      hb_buffer_t* buffer = hb_buffer_create();
      hb_buffer_add_utf32(buffer, reinterpret_cast<const uint32_t*>(text.data() + ps),
                          static_cast<int>(pe - ps), static_cast<unsigned>(segment.start - ps),
                          static_cast<int>(segment.end - segment.start));
      hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
      hb_buffer_set_script(buffer, segment.script);
      hb_buffer_set_language(buffer, hb_language_from_string("ja", -1));
      std::vector<hb_feature_t> features;
      if (style.kerning == core::KerningMode::kNone) {
        hb_feature_t kern{};
        hb_feature_from_string("kern=0", -1, &kern);
        features.push_back(kern);
      }
      hb_shape(face.font, buffer, features.data(), static_cast<unsigned>(features.size()));
      unsigned count = 0;
      const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, &count);
      const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer, &count);
      const double sx = style.size / face.upem * style.horizontal_scale;
      const double sy = style.size / face.upem * style.vertical_scale;
      const double tracking = style.tracking / 1000.0 * style.size;
      for (unsigned g = 0; g < count; ++g) {
        const std::size_t index = ps + info[g].cluster;
        const bool new_cluster = g == 0 || info[g].cluster != info[g - 1].cluster;
        if (new_cluster) clusters.push_back({index, {x, x}});
        run.glyphs.push_back(static_cast<std::uint16_t>(info[g].codepoint));
        run.clusters.push_back(index);
        run.positions.push_back({x + pos[g].x_offset * sx, -style.baseline_shift - pos[g].y_offset * sy});
        const double advance = pos[g].x_advance * sx;
        run.advances.push_back(advance);
        x += advance;
        const bool cluster_ends = g + 1 == count || info[g + 1].cluster != info[g].cluster;
        if (cluster_ends) x += tracking;
        clusters.back().second.second = x;
      }
      hb_buffer_destroy(buffer);
      layout->runs.push_back(std::move(run));
    }
    const double width = x;

    // Metrics: the largest ascent, descent and leading on the line.
    double leading = 0;
    for (std::size_t i = ps; i < pe; ++i) {
      leading = std::max(leading, core::LeadingOf(story.characters[run_of[i]].style));
    }
    for (std::size_t r = first_run; r < layout->runs.size(); ++r) {
      const GlyphRun& run = layout->runs[r];
      const double scale = run.size * run.vertical_scale;
      line.ascent = std::max(line.ascent, run.face->ascender() * scale);
      line.descent = std::max(line.descent, run.face->descender() * scale);
    }
    if (pe == ps || line.ascent == 0) {
      const core::CharacterStyle& style = core::StyleAt(story, ps == 0 ? 0 : ps - 1);
      FacePtr face = FaceOrSubstitute(style.font);
      line.ascent = std::max(line.ascent, (face ? face->ascender() : 0.88) * style.size);
      line.descent = std::max(line.descent, (face ? face->descender() : 0.12) * style.size);
      if (leading == 0) leading = core::LeadingOf(style);
    }
    line.baseline =
        p == 0 ? 0 : previous_baseline + leading + previous_space_after + paragraph.space_before;
    previous_baseline = line.baseline;
    previous_space_after = paragraph.space_after;

    // Alignment on the anchor.
    double dx = 0;
    switch (paragraph.align) {
      case core::TextAlign::kLeft:
        dx = paragraph.left_indent + paragraph.first_line_indent;
        break;
      case core::TextAlign::kCenter:
        dx = -width / 2 + (paragraph.left_indent - paragraph.right_indent) / 2;
        break;
      case core::TextAlign::kRight:
        dx = -width - paragraph.right_indent;
        break;
    }
    for (std::size_t r = first_run; r < layout->runs.size(); ++r) {
      for (Point& position : layout->runs[r].positions) {
        position.x += dx;
        position.y += line.baseline;
      }
    }
    line.left = dx;
    line.right = dx + width;

    // Carets: at cluster starts, spread evenly inside clusters of several
    // characters (ligatures).
    line.carets.assign(pe - ps + 1, std::numeric_limits<double>::quiet_NaN());
    std::sort(clusters.begin(), clusters.end());
    for (std::size_t c = 0; c < clusters.size(); ++c) {
      const std::size_t from = clusters[c].first;
      const std::size_t to = c + 1 < clusters.size() ? clusters[c + 1].first : pe;
      const auto [x0, x1] = clusters[c].second;
      for (std::size_t k = from; k < to; ++k) {
        line.carets[k - ps] = dx + x0 + (x1 - x0) * double(k - from) / double(to - from);
      }
    }
    line.carets.back() = dx + width;
    for (std::size_t k = line.carets.size() - 1; k-- > 0;) {
      if (std::isnan(line.carets[k])) line.carets[k] = line.carets[k + 1];
    }
    layout->bounds = layout->bounds.Union(
        Rect{line.left, line.baseline - line.ascent, line.right, line.baseline + line.descent});
    layout->lines.push_back(std::move(line));
  }
  return layout;
}

struct CacheEntry {
  std::weak_ptr<const core::Story> story;
  std::uint64_t generation = 0;
  LayoutPtr layout;
};

std::mutex cache_mutex;
std::unordered_map<const core::Story*, CacheEntry> cache;

using Outline = std::shared_ptr<const std::vector<core::PathData>>;
std::mutex outline_mutex;
std::map<std::pair<std::uint64_t, std::uint16_t>, Outline> outlines;

Outline GlyphOutline(const Face& face, std::uint16_t glyph) {
  const auto key = std::make_pair(face.id(), glyph);
  {
    std::lock_guard lock(outline_mutex);
    if (const auto it = outlines.find(key); it != outlines.end()) return it->second;
  }
  auto outline = std::make_shared<const std::vector<core::PathData>>(face.Outline(glyph));
  std::lock_guard lock(outline_mutex);
  if (outlines.size() > 20000) outlines.clear();
  outlines[key] = outline;
  return outline;
}

}  // namespace

LayoutPtr LayoutOf(const core::StoryPtr& story) {
  if (!story) return std::make_shared<Layout>();
  const std::uint64_t generation = FontsGeneration();
  {
    std::lock_guard lock(cache_mutex);
    if (const auto it = cache.find(story.get()); it != cache.end()) {
      if (it->second.story.lock() == story && it->second.generation == generation) {
        return it->second.layout;
      }
    }
  }
  LayoutPtr layout = Compute(*story);
  std::lock_guard lock(cache_mutex);
  if (cache.size() > 512) {
    std::erase_if(cache, [](const auto& entry) { return entry.second.story.expired(); });
  }
  cache[story.get()] = {story, generation, layout};
  return layout;
}

std::vector<core::PathData> Outlines(const Layout& layout) {
  std::vector<core::PathData> paths;
  for (const GlyphRun& run : layout.runs) {
    const double sx = run.size * run.horizontal_scale, sy = run.size * run.vertical_scale;
    for (std::size_t g = 0; g < run.glyphs.size(); ++g) {
      core::Matrix m = core::Matrix::Translate(run.positions[g].x, run.positions[g].y);
      if (run.rotation != 0) {
        // About the middle of the glyph's em box.
        const Point centre{run.advances[g] / 2,
                           -(run.face->ascender() - run.face->descender()) / 2 * sy};
        m = m * core::Matrix::Translate(centre.x, centre.y) *
            core::Matrix::Rotate(-run.rotation * std::numbers::pi / 180) *
            core::Matrix::Translate(-centre.x, -centre.y);
      }
      m = m * core::Matrix::Scale(sx, sy);
      for (const core::PathData& path : *GlyphOutline(*run.face, run.glyphs[g])) {
        paths.push_back(core::Transformed(path, m));
      }
    }
  }
  return paths;
}

std::vector<core::PathData> OutlineOf(const core::TextObject& text) {
  std::vector<core::PathData> paths = Outlines(*LayoutOf(text.story));
  if (!text.transform.IsIdentity()) {
    for (auto& path : paths) path = core::Transformed(path, text.transform);
  }
  return paths;
}

core::Rect BoundsOf(const core::TextObject& text) {
  const LayoutPtr layout = LayoutOf(text.story);
  Rect local = layout->bounds;
  if (!local.IsValid()) local = Rect{0, -10, 0, 2};
  const Point corners[] = {{local.left, local.top},
                           {local.right, local.top},
                           {local.right, local.bottom},
                           {local.left, local.bottom}};
  Rect bounds;
  for (const Point& c : corners) bounds = bounds.Union(text.transform.Map(c));
  return bounds;
}

bool Hits(const core::TextObject& text, Point p, double tolerance) {
  const auto inverse = text.transform.Inverted();
  if (!inverse) return false;
  const Point local = inverse->Map(p);
  const LayoutPtr layout = LayoutOf(text.story);
  for (const Line& line : layout->lines) {
    if (local.x >= line.left - tolerance && local.x <= line.right + tolerance &&
        local.y >= line.baseline - line.ascent - tolerance &&
        local.y <= line.baseline + line.descent + tolerance) {
      return true;
    }
  }
  return false;
}

std::size_t LineOf(const Layout& layout, std::size_t index) {
  for (std::size_t l = 0; l < layout.lines.size(); ++l) {
    if (index <= layout.lines[l].end) return l;
  }
  return layout.lines.empty() ? 0 : layout.lines.size() - 1;
}

double CaretX(const Layout& layout, std::size_t index) {
  if (layout.lines.empty()) return 0;
  const Line& line = layout.lines[LineOf(layout, index)];
  index = std::clamp(index, line.start, line.end);
  return line.carets[index - line.start];
}

namespace {

std::size_t NearestOnLine(const Layout& layout, const Line& line, double x) {
  std::size_t best = line.start;
  double best_distance = std::numeric_limits<double>::infinity();
  for (std::size_t b : layout.boundaries) {
    if (b < line.start || b > line.end) continue;
    const double d = std::abs(line.carets[b - line.start] - x);
    if (d < best_distance) {
      best_distance = d;
      best = b;
    }
  }
  return best;
}

}  // namespace

std::size_t CaretAt(const Layout& layout, Point p) {
  if (layout.lines.empty()) return 0;
  const Line* best = &layout.lines.front();
  double best_distance = std::numeric_limits<double>::infinity();
  for (const Line& line : layout.lines) {
    const double top = line.baseline - line.ascent, bottom = line.baseline + line.descent;
    const double d = p.y < top ? top - p.y : p.y > bottom ? p.y - bottom : 0;
    if (d < best_distance) {
      best_distance = d;
      best = &line;
    }
  }
  return NearestOnLine(layout, *best, p.x);
}

std::size_t NextBoundary(const Layout& layout, std::size_t index) {
  const auto it = std::upper_bound(layout.boundaries.begin(), layout.boundaries.end(), index);
  return it == layout.boundaries.end() ? index : *it;
}

std::size_t PreviousBoundary(const Layout& layout, std::size_t index) {
  const auto it = std::lower_bound(layout.boundaries.begin(), layout.boundaries.end(), index);
  return it == layout.boundaries.begin() ? index : *(it - 1);
}

std::size_t LineUp(const Layout& layout, std::size_t index, double x) {
  const std::size_t line = LineOf(layout, index);
  if (line == 0) return index;
  return NearestOnLine(layout, layout.lines[line - 1], x);
}

std::size_t LineDown(const Layout& layout, std::size_t index, double x) {
  const std::size_t line = LineOf(layout, index);
  if (line + 1 >= layout.lines.size()) return index;
  return NearestOnLine(layout, layout.lines[line + 1], x);
}

std::pair<Point, Point> CaretLine(const Layout& layout, std::size_t index) {
  if (layout.lines.empty()) return {{0, -10}, {0, 2}};
  const Line& line = layout.lines[LineOf(layout, index)];
  const double x = CaretX(layout, index);
  return {{x, line.baseline - line.ascent}, {x, line.baseline + line.descent}};
}

std::vector<Rect> SelectionBoxes(const Layout& layout, std::size_t from, std::size_t to) {
  std::vector<Rect> boxes;
  if (from >= to) return boxes;
  for (const Line& line : layout.lines) {
    if (to <= line.start || from > line.end) continue;
    const std::size_t a = std::max(from, line.start), b = std::min(to, line.end);
    double left = line.carets[a - line.start], right = line.carets[b - line.start];
    // The paragraph break is selected too: show a little of it.
    if (to > line.end && line.end < layout.boundaries.back()) {
      right += (line.ascent + line.descent) * 0.3;
    }
    if (right > left) {
      boxes.push_back({left, line.baseline - line.ascent, right, line.baseline + line.descent});
    }
  }
  return boxes;
}

std::pair<std::size_t, std::size_t> WordAt(const core::Story& story, std::size_t index) {
  const std::vector<std::size_t> words = Breaks(UBRK_WORD, story.text);
  for (std::size_t w = 0; w + 1 < words.size(); ++w) {
    if (index >= words[w] && index < words[w + 1]) return {words[w], words[w + 1]};
  }
  return {index, index};
}

}  // namespace leinwand::text
