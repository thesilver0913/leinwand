// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/text.h"

#include <algorithm>
#include <utility>

namespace leinwand::core {

namespace {

constexpr char32_t kParagraphBreak = U'\n';

// Joins neighbouring runs with equal styles and drops empty runs, keeping at
// least one.
void Normalize(std::vector<CharacterRun>& runs) {
  std::vector<CharacterRun> out;
  for (auto& run : runs) {
    if (run.length == 0) continue;
    if (!out.empty() && out.back().style == run.style) {
      out.back().length += run.length;
    } else {
      out.push_back(std::move(run));
    }
  }
  if (out.empty()) out.push_back({0, runs.empty() ? CharacterStyle{} : runs.front().style});
  runs = std::move(out);
}

// Splits the runs so that one starts at `at`; returns the index of that run
// (runs.size() at the end).
std::size_t SplitAt(std::vector<CharacterRun>& runs, std::size_t at) {
  std::size_t start = 0;
  for (std::size_t i = 0; i < runs.size(); ++i) {
    if (at == start) return i;
    if (at < start + runs[i].length) {
      CharacterRun tail = runs[i];
      tail.length = start + runs[i].length - at;
      runs[i].length = at - start;
      runs.insert(runs.begin() + static_cast<std::ptrdiff_t>(i) + 1, tail);
      return i + 1;
    }
    start += runs[i].length;
  }
  return runs.size();
}

}  // namespace

FontRef DefaultFont() { return {"Source Han Sans JP", "Regular", "SourceHanSansJP-Regular"}; }

double LeadingOf(const CharacterStyle& style) {
  return style.leading.value_or(style.size * kAutoLeading);
}

Story MakeStory(std::string id, std::u32string text, const CharacterStyle& style,
                const ParagraphStyle& paragraph) {
  Story story;
  story.id = std::move(id);
  story.characters = {{text.size(), style}};
  const auto breaks = std::count(text.begin(), text.end(), kParagraphBreak);
  story.paragraphs.assign(static_cast<std::size_t>(breaks) + 1, paragraph);
  story.text = std::move(text);
  return story;
}

bool IsConsistent(const Story& story) {
  if (story.characters.empty()) return false;
  std::size_t total = 0;
  for (const auto& run : story.characters) total += run.length;
  const auto breaks = std::count(story.text.begin(), story.text.end(), kParagraphBreak);
  return total == story.text.size() &&
         story.paragraphs.size() == static_cast<std::size_t>(breaks) + 1;
}

const CharacterStyle& StyleAt(const Story& story, std::size_t index) {
  std::size_t start = 0;
  for (const auto& run : story.characters) {
    if (index < start + run.length) return run.style;
    start += run.length;
  }
  return story.characters.back().style;
}

std::vector<CharacterStyle> StylesIn(const Story& story, std::size_t from, std::size_t to) {
  if (from >= to) return {StyleAt(story, from == 0 ? 0 : from - 1)};
  std::vector<CharacterStyle> styles;
  std::size_t start = 0;
  for (const auto& run : story.characters) {
    const std::size_t end = start + run.length;
    if (end > from && start < to) {
      if (std::find(styles.begin(), styles.end(), run.style) == styles.end()) {
        styles.push_back(run.style);
      }
    }
    start = end;
  }
  if (styles.empty()) styles.push_back(story.characters.back().style);
  return styles;
}

std::size_t ParagraphOf(const Story& story, std::size_t index) {
  const std::size_t end = std::min(index, story.text.size());
  return static_cast<std::size_t>(
      std::count(story.text.begin(), story.text.begin() + static_cast<std::ptrdiff_t>(end),
                 kParagraphBreak));
}

std::size_t ParagraphStart(const Story& story, std::size_t paragraph) {
  std::size_t seen = 0;
  for (std::size_t i = 0; i < story.text.size(); ++i) {
    if (seen == paragraph) return i;
    if (story.text[i] == kParagraphBreak) ++seen;
  }
  return story.text.size();
}

std::size_t ParagraphEnd(const Story& story, std::size_t paragraph) {
  const std::size_t start = ParagraphStart(story, paragraph);
  const std::size_t found = story.text.find(kParagraphBreak, start);
  return found == std::u32string::npos ? story.text.size() : found;
}

Story Inserted(const Story& story, std::size_t at, std::u32string_view text,
               const CharacterStyle* style) {
  Story result = story;
  at = std::min(at, story.text.size());
  if (text.empty()) return result;
  const CharacterStyle typed = style ? *style : StyleAt(story, at == 0 ? 0 : at - 1);
  result.text.insert(at, text);
  const std::size_t index = SplitAt(result.characters, at);
  result.characters.insert(result.characters.begin() + static_cast<std::ptrdiff_t>(index),
                           CharacterRun{text.size(), typed});
  Normalize(result.characters);
  // Each break splits its paragraph; the new one copies its style.
  const std::size_t paragraph = ParagraphOf(story, at);
  const auto breaks = std::count(text.begin(), text.end(), kParagraphBreak);
  result.paragraphs.insert(result.paragraphs.begin() + static_cast<std::ptrdiff_t>(paragraph) + 1,
                           static_cast<std::size_t>(breaks), story.paragraphs[paragraph]);
  return result;
}

Story Erased(const Story& story, std::size_t from, std::size_t to) {
  to = std::min(to, story.text.size());
  if (from >= to) return story;
  Story result = story;
  const std::size_t first = ParagraphOf(story, from);
  const auto breaks = std::count(story.text.begin() + static_cast<std::ptrdiff_t>(from),
                                 story.text.begin() + static_cast<std::ptrdiff_t>(to),
                                 kParagraphBreak);
  result.text.erase(from, to - from);
  const std::size_t begin = SplitAt(result.characters, from);
  const std::size_t end = SplitAt(result.characters, to);
  // The style to keep typing with when everything goes.
  const CharacterStyle kept = StyleAt(story, from);
  result.characters.erase(result.characters.begin() + static_cast<std::ptrdiff_t>(begin),
                          result.characters.begin() + static_cast<std::ptrdiff_t>(end));
  if (result.characters.empty()) result.characters.push_back({0, kept});
  Normalize(result.characters);
  result.paragraphs.erase(
      result.paragraphs.begin() + static_cast<std::ptrdiff_t>(first) + 1,
      result.paragraphs.begin() + static_cast<std::ptrdiff_t>(first) + 1 + breaks);
  return result;
}

Story WithCharacterStyle(const Story& story, std::size_t from, std::size_t to,
                         const std::function<void(CharacterStyle&)>& edit) {
  Story result = story;
  to = std::min(to, story.text.size());
  if (story.text.empty()) {
    // An empty story: the style it will type with.
    edit(result.characters.front().style);
    return result;
  }
  if (from >= to) return result;
  const std::size_t begin = SplitAt(result.characters, from);
  const std::size_t end = SplitAt(result.characters, to);
  for (std::size_t i = begin; i < end; ++i) edit(result.characters[i].style);
  Normalize(result.characters);
  return result;
}

Story WithParagraphStyle(const Story& story, std::size_t from, std::size_t to,
                         const std::function<void(ParagraphStyle&)>& edit) {
  Story result = story;
  const std::size_t first = ParagraphOf(story, from);
  const std::size_t last = to > from ? ParagraphOf(story, to - 1) : first;
  for (std::size_t p = first; p <= last && p < result.paragraphs.size(); ++p) {
    edit(result.paragraphs[p]);
  }
  return result;
}

std::u32string FromUtf8(std::string_view text) {
  std::u32string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    int extra = 0;
    char32_t cp = 0;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else {
      out.push_back(U'�');
      ++i;
      continue;
    }
    if (i + static_cast<std::size_t>(extra) >= text.size()) {  // Cut short.
      out.push_back(U'�');
      break;
    }
    bool ok = true;
    for (int k = 1; k <= extra; ++k) {
      const auto next = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
      if ((next & 0xC0) != 0x80) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (next & 0x3F);
    }
    if (!ok || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      out.push_back(U'�');
      ++i;
      continue;
    }
    out.push_back(cp);
    i += static_cast<std::size_t>(extra) + 1;
  }
  return out;
}

std::string ToUtf8(std::u32string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char32_t cp : text) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = U'�';
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

}  // namespace leinwand::core
