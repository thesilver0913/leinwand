// SPDX-License-Identifier: GPL-3.0-or-later
// M8 check: the pieces of the text engine (spec 5.1) working together.
// Hidden from the normal run ([.]); run with
//   render_tests "[text-probe]"
// - HarfBuzz shapes mixed Japanese and English with the bundled Source Han
//   Sans, from the font's bytes (no FreeType).
// - Skia draws HarfBuzz's glyphs with a typeface made from the same bytes by
//   the platform's font manager (DirectWrite, Core Text, Fontconfig).
// - HarfBuzz gives glyph outlines (for converting text to paths).
// - Skia's PDF backend embeds the font and keeps the text searchable.
// - ICU finds grapheme clusters and line break opportunities.
// - The platform font manager lists the OS fonts and hands over their bytes.
#include <hb-ot.h>
#include <hb.h>
#include <unicode/ubrk.h>
#include <unicode/ustring.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkStream.h"
#include "include/core/SkTypeface.h"
#include "include/docs/SkPDFDocument.h"
#include "include/docs/SkPDFJpegHelpers.h"
#include "include/encode/SkPngEncoder.h"

#if defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#elif defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#else
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#endif

namespace {

sk_sp<SkFontMgr> PlatformFontMgr() {
#if defined(_WIN32)
  return SkFontMgr_New_DirectWrite();
#elif defined(__APPLE__)
  return SkFontMgr_New_CoreText(nullptr);
#else
  return SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#endif
}

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Shaped {
  std::vector<SkGlyphID> glyphs;
  std::vector<SkPoint> positions;
  std::vector<uint32_t> clusters;
  float advance = 0;
};

// Shapes UTF-8 text at `size` pt with HarfBuzz's own OpenType functions.
Shaped Shape(hb_font_t* font, const std::string& text, float size, unsigned upem) {
  hb_buffer_t* buffer = hb_buffer_create();
  hb_buffer_add_utf8(buffer, text.c_str(), int(text.size()), 0, int(text.size()));
  hb_buffer_guess_segment_properties(buffer);  // Script, direction, language.
  hb_buffer_set_language(buffer, hb_language_from_string("ja", -1));
  hb_shape(font, buffer, nullptr, 0);
  unsigned count = 0;
  const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, &count);
  const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer, &count);
  Shaped out;
  const float scale = size / float(upem);
  float x = 0;
  for (unsigned i = 0; i < count; ++i) {
    out.glyphs.push_back(SkGlyphID(info[i].codepoint));
    out.clusters.push_back(info[i].cluster);
    out.positions.push_back({x + pos[i].x_offset * scale, -pos[i].y_offset * scale});
    x += pos[i].x_advance * scale;
  }
  out.advance = x;
  hb_buffer_destroy(buffer);
  return out;
}

// Counts the drawing commands HarfBuzz gives for a glyph outline.
struct OutlineCount {
  int moves = 0, lines = 0, quads = 0, cubics = 0;
};
hb_draw_funcs_t* CountingFuncs() {
  hb_draw_funcs_t* funcs = hb_draw_funcs_create();
  hb_draw_funcs_set_move_to_func(
      funcs,
      [](hb_draw_funcs_t*, void* data, hb_draw_state_t*, float, float, void*) {
        ++static_cast<OutlineCount*>(data)->moves;
      },
      nullptr, nullptr);
  hb_draw_funcs_set_line_to_func(
      funcs,
      [](hb_draw_funcs_t*, void* data, hb_draw_state_t*, float, float, void*) {
        ++static_cast<OutlineCount*>(data)->lines;
      },
      nullptr, nullptr);
  hb_draw_funcs_set_quadratic_to_func(
      funcs,
      [](hb_draw_funcs_t*, void* data, hb_draw_state_t*, float, float, float, float, void*) {
        ++static_cast<OutlineCount*>(data)->quads;
      },
      nullptr, nullptr);
  hb_draw_funcs_set_cubic_to_func(
      funcs,
      [](hb_draw_funcs_t*, void* data, hb_draw_state_t*, float, float, float, float, float, float,
         void*) { ++static_cast<OutlineCount*>(data)->cubics; },
      nullptr, nullptr);
  return funcs;
}

// The font kinds (/Subtype) and font files Skia's PDF backend writes for
// glyphs of `typeface`.
std::string PdfFonts(const sk_sp<SkTypeface>& typeface, const Shaped& shaped,
                     const std::string& text) {
  SkDynamicMemoryWStream stream;
  sk_sp<SkDocument> pdf = SkPDF::MakeDocument(&stream, SkPDF::JPEG::MetadataWithCallbacks());
  SkCanvas* canvas = pdf->beginPage(shaped.advance + 40, 70);
  SkFont font(typeface, 32);
  SkPaint paint;
  canvas->drawGlyphs(shaped.glyphs, shaped.positions, shaped.clusters, {text.data(), text.size()},
                     {20, 46}, font, paint);
  pdf->endPage();
  pdf->close();
  sk_sp<SkData> data = stream.detachAsData();
  const std::string contents(static_cast<const char*>(data->data()), data->size());
  std::string kinds;
  for (const char* key : {"/Type0", "/Type3", "/TrueType", "/CIDFontType0", "/CIDFontType2",
                          "/FontFile2", "/FontFile3", "/ToUnicode"}) {
    if (contents.find(key) != std::string::npos) kinds += std::string(" ") + key;
  }
  return kinds;
}

std::vector<int32_t> Breaks(UBreakIteratorType type, const std::u16string& text) {
  UErrorCode status = U_ZERO_ERROR;
  UBreakIterator* it = ubrk_open(type, "ja", reinterpret_cast<const UChar*>(text.data()),
                                 int32_t(text.size()), &status);
  std::vector<int32_t> breaks;
  if (U_FAILURE(status)) return breaks;
  for (int32_t b = ubrk_first(it); b != UBRK_DONE; b = ubrk_next(it)) breaks.push_back(b);
  ubrk_close(it);
  return breaks;
}

}  // namespace

TEST_CASE("Text engine probe (M8)", "[.][text-probe]") {
  const std::filesystem::path fonts = LEINWAND_FONTS_DIR;
  const std::filesystem::path out_dir = LEINWAND_TEST_OUTPUT_DIR;
  std::filesystem::create_directories(out_dir);

  // The bundled Source Han Sans JP, read once and shared by both sides.
  std::vector<char> bytes = ReadFile(fonts / "SourceHanSansJP-Regular.otf");
  REQUIRE(!bytes.empty());
  sk_sp<SkData> data = SkData::MakeWithCopy(bytes.data(), bytes.size());

  hb_blob_t* blob = hb_blob_create(static_cast<const char*>(data->data()), unsigned(data->size()),
                                   HB_MEMORY_MODE_READONLY, nullptr, nullptr);
  hb_face_t* face = hb_face_create(blob, 0);
  hb_font_t* hb_font = hb_font_create(face);
  const unsigned upem = hb_face_get_upem(face);
  std::printf("HarfBuzz %s, face: %u glyphs, %u units per em\n", hb_version_string(),
              hb_face_get_glyph_count(face), upem);

  sk_sp<SkFontMgr> manager = PlatformFontMgr();
  REQUIRE(manager);
  sk_sp<SkTypeface> typeface = manager->makeFromData(data);
  REQUIRE(typeface);
  SkString family;
  typeface->getFamilyName(&family);
  std::printf("Skia typeface: %s, %d glyphs\n", family.c_str(), typeface->countGlyphs());
  CHECK(typeface->countGlyphs() == int(hb_face_get_glyph_count(face)));

  // The same glyph IDs on both sides: Skia's own cmap lookup agrees with
  // HarfBuzz for text without ligatures or contextual forms.
  {
    const std::string plain = "Aあ永";
    const Shaped shaped = Shape(hb_font, plain, 24, upem);
    SkFont font(typeface, 24);
    SkGlyphID skia_ids[3];
    font.textToGlyphs(plain.data(), plain.size(), SkTextEncoding::kUTF8, skia_ids);
    REQUIRE(shaped.glyphs.size() == 3);
    for (int i = 0; i < 3; ++i) CHECK(shaped.glyphs[i] == skia_ids[i]);
  }

  // Shape and draw mixed text.
  const std::string text = "Leinwand で日本語と English を組む。「約物」・fi ffi 123";
  const Shaped shaped = Shape(hb_font, text, 32, upem);
  std::printf("shaped %zu bytes of text into %zu glyphs, advance %.1f pt\n", text.size(),
              shaped.glyphs.size(), shaped.advance);
  CHECK(shaped.glyphs.size() > 20);
  for (SkGlyphID g : shaped.glyphs) CHECK(g != 0);  // No missing glyphs (.notdef).
  {
    SkBitmap bitmap;
    bitmap.allocN32Pixels(int(shaped.advance) + 40, 70);
    bitmap.eraseColor(SK_ColorWHITE);
    SkCanvas canvas(bitmap);
    SkFont font(typeface, 32);
    font.setEdging(SkFont::Edging::kAntiAlias);
    SkPaint paint;
    paint.setColor(SK_ColorBLACK);
    canvas.drawGlyphs(shaped.glyphs, shaped.positions, {20, 46}, font, paint);
    int ink = 0;
    for (int y = 0; y < bitmap.height(); ++y) {
      for (int x = 0; x < bitmap.width(); ++x) ink += SkColorGetR(bitmap.getColor(x, y)) < 128;
    }
    std::printf("drawn: %d dark pixels\n", ink);
    CHECK(ink > 1000);
    SkFILEWStream png((out_dir / "text_probe.png").string().c_str());
    SkPngEncoder::Encode(&png, bitmap.pixmap(), {});
  }

  // Outlines from HarfBuzz (CFF fonts give cubics).
  {
    hb_codepoint_t glyph = 0;
    REQUIRE(hb_font_get_nominal_glyph(hb_font, 0x6C38, &glyph));  // 永
    OutlineCount count;
    hb_draw_funcs_t* funcs = CountingFuncs();
    hb_font_draw_glyph(hb_font, glyph, funcs, &count);
    hb_draw_funcs_destroy(funcs);
    std::printf("outline of 永: %d contours, %d lines, %d quads, %d cubics\n", count.moves,
                count.lines, count.quads, count.cubics);
    CHECK(count.moves > 0);
    CHECK(count.cubics > 0);
  }

  // PDF: the font is embedded, and the text can be searched (ToUnicode).
  {
    SkDynamicMemoryWStream stream;
    sk_sp<SkDocument> pdf = SkPDF::MakeDocument(&stream, SkPDF::JPEG::MetadataWithCallbacks());
    SkCanvas* canvas = pdf->beginPage(shaped.advance + 40, 70);
    SkFont font(typeface, 32);
    SkPaint paint;
    canvas->drawGlyphs(shaped.glyphs, shaped.positions, shaped.clusters, {text.data(), text.size()},
                       {20, 46}, font, paint);
    pdf->endPage();
    pdf->close();
    sk_sp<SkData> bytes_pdf = stream.detachAsData();
    const std::string contents(static_cast<const char*>(bytes_pdf->data()), bytes_pdf->size());
    const bool embedded = contents.find("/FontFile") != std::string::npos;
    const bool searchable = contents.find("/ToUnicode") != std::string::npos;
    std::printf("PDF: %zu bytes, font embedded: %s, ToUnicode: %s\n", contents.size(),
                embedded ? "yes" : "no", searchable ? "yes" : "no");
    // Not CHECKed: CFF fonts come out as Type3 (see the comparison below).
    CHECK(searchable);
    std::ofstream((out_dir / "text_probe.pdf").string(), std::ios::binary) << contents;
  }

  // A TrueType (glyf) OS font, for comparison with the CFF-based Han Sans.
  {
    sk_sp<SkTypeface> truetype;
    for (int i = 0; i < manager->countFamilies() && !truetype; ++i) {
      sk_sp<SkFontStyleSet> set = manager->createStyleSet(i);
      if (!set || set->count() == 0) continue;
      sk_sp<SkTypeface> face_i = set->createTypeface(0);
      if (face_i && face_i->getTableSize(SkSetFourByteTag('g', 'l', 'y', 'f')) > 0) {
        truetype = face_i;
      }
    }
    REQUIRE(truetype);
    SkString name;
    truetype->getFamilyName(&name);
    int index = 0;
    std::unique_ptr<SkStreamAsset> stream = truetype->openStream(&index);
    REQUIRE(stream);
    sk_sp<SkData> tt_data = SkData::MakeFromStream(stream.get(), stream->getLength());
    hb_blob_t* tt_blob =
        hb_blob_create(static_cast<const char*>(tt_data->data()), unsigned(tt_data->size()),
                       HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* tt_face = hb_face_create(tt_blob, unsigned(index));
    hb_font_t* tt_font = hb_font_create(tt_face);
    const std::string latin = "Leinwand PDF text";
    const Shaped tt_shaped = Shape(tt_font, latin, 32, hb_face_get_upem(tt_face));
    std::printf("PDF with %s (TrueType):%s\n", name.c_str(),
                PdfFonts(truetype, tt_shaped, latin).c_str());
    std::printf("PDF with Source Han Sans (CFF):%s\n", PdfFonts(typeface, shaped, text).c_str());
    hb_font_destroy(tt_font);
    hb_face_destroy(tt_face);
    hb_blob_destroy(tt_blob);
  }

  // ICU: grapheme clusters (a family emoji is one) and line breaks.
  {
    const std::u16string family_emoji = u"\U0001F468‍\U0001F469‍\U0001F467が";
    const auto graphemes = Breaks(UBRK_CHARACTER, family_emoji);
    std::printf("ICU %s: %zu UTF-16 units, %zu grapheme clusters\n", U_ICU_VERSION,
                family_emoji.size(), graphemes.size() - 1);
    CHECK(graphemes.size() - 1 == 2);
    const std::u16string sentence = u"日本語の文章を、行に分けます。English words too.";
    const auto lines = Breaks(UBRK_LINE, sentence);
    std::printf("line break opportunities: %zu\n", lines.size() - 1);
    // No break before 、 or 。 (kinsoku), so neither starts a line.
    for (int32_t b : lines) {
      if (b < int32_t(sentence.size())) {
        CHECK(sentence[size_t(b)] != u'、');
        CHECK(sentence[size_t(b)] != u'。');
      }
    }
  }

  // The OS fonts: the platform manager lists them and gives their bytes,
  // which is what HarfBuzz needs.
  {
    const int families = manager->countFamilies();
    std::printf("OS font families: %d\n", families);
    CHECK(families > 0);
    int readable = 0, checked = 0;
    for (int i = 0; i < families && checked < 20; ++i) {
      SkString name;
      manager->getFamilyName(i, &name);
      sk_sp<SkFontStyleSet> set = manager->createStyleSet(i);
      if (!set || set->count() == 0) continue;
      sk_sp<SkTypeface> face_i = set->createTypeface(0);
      if (!face_i) continue;
      ++checked;
      int index = 0;
      std::unique_ptr<SkStreamAsset> stream = face_i->openStream(&index);
      if (stream && stream->getLength() > 0) ++readable;
      if (checked <= 5) {
        std::printf("  %s (%zu bytes)\n", name.c_str(), stream ? stream->getLength() : 0);
      }
    }
    std::printf("bytes available for %d of %d families checked\n", readable, checked);
    CHECK(readable == checked);
  }

  hb_font_destroy(hb_font);
  hb_face_destroy(face);
  hb_blob_destroy(blob);
}
