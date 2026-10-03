// SPDX-License-Identifier: GPL-3.0-or-later
// PDF export (spec 6.2) and trim marks (spec 7.5).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

#include "core/marks.h"
#include "render/document_renderer.h"
#include "render/test_document.h"

using namespace leinwand;
using Catch::Approx;
using render::DocumentRenderer;
using render::PdfOptions;

namespace {

std::string AsText(const std::vector<std::uint8_t>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

int Count(const std::string& text, const std::string& what) {
  int count = 0;
  for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
    ++count;
  }
  return count;
}

// The first page's MediaBox width and height.
std::pair<double, double> MediaBox(const std::string& pdf) {
  std::smatch m;
  const std::regex box(R"(/MediaBox\s*\[\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s*\])");
  if (!std::regex_search(pdf, m, box)) return {0, 0};
  return {std::stod(m[3]) - std::stod(m[1]), std::stod(m[4]) - std::stod(m[2])};
}

core::Document TwoArtboards() {
  core::Document document = render::MakeShowcaseDocument();
  document.artboards.push_back(
      {"ab2", "Artboard 2", core::Rect::FromXYWH(900, 0, 300, 200), {}, 0});
  return document;
}

}  // namespace

TEST_CASE("PDF: one page per artboard, text kept as text") {
  const std::string pdf = AsText(DocumentRenderer::ExportPdf(TwoArtboards(), {}));
  REQUIRE(pdf.rfind("%PDF", 0) == 0);
  CHECK(Count(pdf, "/Type /Page\n") + Count(pdf, "/Type /Page ") + Count(pdf, "/Type /Page>") >= 2);
  // The showcase's text is real text: searchable, its font in the file.
  CHECK(pdf.find("/ToUnicode") != std::string::npos);
  const auto [w, h] = MediaBox(pdf);
  CHECK(w == Approx(800));
  CHECK(h == Approx(600));

  PdfOptions one;
  one.artboards = {1};
  const std::string single = AsText(DocumentRenderer::ExportPdf(TwoArtboards(), one));
  CHECK(MediaBox(single).first == Approx(300));

  PdfOptions outlines;
  outlines.outline_text = true;
  const std::string paths = AsText(DocumentRenderer::ExportPdf(TwoArtboards(), outlines));
  CHECK(paths.find("/ToUnicode") == std::string::npos);
}

TEST_CASE("PDF with trim marks: the page grows by the bleed and the marks") {
  PdfOptions options;
  options.artboards = {0};
  options.marks = core::TrimMarkStyle::kJapanese;
  const std::string pdf = AsText(DocumentRenderer::ExportPdf(TwoArtboards(), options));
  const double reach = std::ceil(core::TrimMarkReach(core::kDefaultBleed) + 6);
  CHECK(MediaBox(pdf).first == Approx(800 + 2 * reach).margin(0.01));
}

TEST_CASE("Japanese trim marks are double with centre marks; Western ones single") {
  const core::Rect trim = core::Rect::FromXYWH(0, 0, 100, 50);
  const auto japanese = core::TrimMarks(trim, 9, core::TrimMarkStyle::kJapanese, 20);
  CHECK(japanese.size() == 4 * 4 + 4 * 2);
  const auto western = core::TrimMarks(trim, 9, core::TrimMarkStyle::kWestern, 20);
  CHECK(western.size() == 4 * 2);
  // Every mark stays outside the bleed, and within the reach.
  for (const auto& line : japanese) {
    REQUIRE(line.anchors.size() == 2);
    for (const auto& a : line.anchors) {
      const core::Point p = a.position;
      const bool outside = p.x <= -9 || p.x >= 109 || p.y <= -9 || p.y >= 59;
      const bool on_trim_line = p.x == 0 || p.x == 100 || p.y == 0 || p.y == 50;
      CHECK((outside || on_trim_line));
      CHECK(p.x >= -29);
      CHECK(p.y <= 79);
    }
  }
}

// Writes the showcase as PDF for looking at it: test-output/pdf/.
TEST_CASE("Showcase as PDF", "[.][showcase-pdf]") {
  const std::filesystem::path dir = std::filesystem::path(LEINWAND_TEST_OUTPUT_DIR) / "pdf";
  std::filesystem::create_directories(dir);
  const auto plain = DocumentRenderer::ExportPdf(TwoArtboards(), {});
  std::ofstream(dir / "showcase.pdf", std::ios::binary)
      .write(reinterpret_cast<const char*>(plain.data()), std::streamsize(plain.size()));
  PdfOptions marks;
  marks.marks = core::TrimMarkStyle::kJapanese;
  const auto marked = DocumentRenderer::ExportPdf(TwoArtboards(), marks);
  std::ofstream(dir / "showcase-marks.pdf", std::ios::binary)
      .write(reinterpret_cast<const char*>(marked.data()), std::streamsize(marked.size()));
}
