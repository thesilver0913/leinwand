// SPDX-License-Identifier: GPL-3.0-or-later
// Fonts for the text engine (spec 5.2). A face is a font file's bytes read
// with HarfBuzz; sources say which fonts exist and hand over their bytes.
// The OS fonts come through Skia's font managers (render/skia_font_source.h,
// the same split as the path operations); the bundled fonts are read from
// their folder here.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/path.h"
#include "core/text.h"

namespace leinwand::text {

// One face of a font file. Immutable and shared; safe to use from several
// threads.
class Face {
 public:
  // Null when the bytes are not a font HarfBuzz can read.
  static std::shared_ptr<const Face> FromData(std::shared_ptr<const std::vector<char>> bytes,
                                              int index = 0);
  ~Face();
  Face(const Face&) = delete;
  Face& operator=(const Face&) = delete;

  // Names from the name table (typographic family and subfamily when there).
  const std::string& family() const;
  const std::string& style() const;
  const std::string& postscript_name() const;
  core::FontRef ref() const { return {family(), style(), postscript_name()}; }

  // Unique for the run of the program (render caches typefaces by it).
  std::uint64_t id() const;
  const std::vector<char>& bytes() const;
  int index() const;

  bool HasGlyph(char32_t c) const;
  // Ascender and descender as fractions of the em (both positive).
  double ascender() const;
  double descender() const;
  // A glyph's outline for a 1 pt em: x right, y down from the baseline.
  std::vector<core::PathData> Outline(std::uint16_t glyph) const;

  struct Impl;
  const Impl& impl() const { return *impl_; }

 private:
  explicit Face(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
using FacePtr = std::shared_ptr<const Face>;

struct FontFamily {
  std::string name;
  std::vector<std::string> styles;
};

// Where fonts come from.
class FontSource {
 public:
  virtual ~FontSource() = default;
  virtual std::vector<FontFamily> Families() const = 0;
  // The face for the font, or null when this source does not have it.
  virtual FacePtr Find(const core::FontRef& font) const = 0;
  // A face with a glyph for `c`, in a style close to `like` (fallback).
  virtual FacePtr ForCharacter(char32_t c, const core::FontRef& like) const = 0;
};

// The fonts in one folder (.otf, .ttf, .ttc), read once.
class FolderFontSource : public FontSource {
 public:
  explicit FolderFontSource(const std::filesystem::path& folder);
  std::vector<FontFamily> Families() const override;
  FacePtr Find(const core::FontRef& font) const override;
  FacePtr ForCharacter(char32_t c, const core::FontRef& like) const override;

 private:
  std::vector<FacePtr> faces_;
};

// The sources the text engine uses, in order: the first that has a font
// wins. Set once at startup (the app: the bundled fonts, then the OS fonts;
// tests: the bundled fonts). Changing them invalidates every layout.
void SetFontSources(std::vector<std::shared_ptr<const FontSource>> sources);
std::uint64_t FontsGeneration();

// Every family, merged across sources and sorted by name.
std::vector<FontFamily> Families();
// The face for the font; null when no source has it (a missing font).
FacePtr FindFace(const core::FontRef& font);
bool IsAvailable(const core::FontRef& font);
// The face to use for `font`: the font itself, else the default font, else
// any face there is; null only without any fonts.
FacePtr FaceOrSubstitute(const core::FontRef& font);
// A face that has `c`, preferring `font`, then the default font, then the
// sources' own fallback; null when none has it.
FacePtr FaceForCharacter(char32_t c, const core::FontRef& font);

}  // namespace leinwand::text
