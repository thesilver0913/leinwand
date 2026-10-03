// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/svg.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <variant>

#include "core/edit.h"
#include "core/style.h"
#include "render/document_renderer.h"
#include "render/image_compare.h"
#include "render/test_document.h"

using namespace leinwand;
using Catch::Approx;

namespace {

std::string ReadTestFile(const std::string& name) {
  std::ifstream in(std::string(LEINWAND_TESTDATA_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

const core::Object* Find(const core::Document& d, const std::string& id) {
  return d.FindObject(id);
}

// Renders the first artboard at its own size in points.
std::vector<std::uint8_t> Render(const core::Document& document, int* width, int* height) {
  const core::Rect area = document.artboards.front().bounds;
  *width = static_cast<int>(area.width());
  *height = static_cast<int>(area.height());
  render::RenderSettings settings;
  settings.artwork_only = true;
  render::DocumentRenderer renderer(settings);
  return renderer.RenderRaster(document, *width, *height, render::View{-area.left, -area.top, 1});
}

// For looking at a failure: the document as PNG in the test output folder.
void Dump(const core::Document& document, const std::string& name) {
  const auto png =
      render::DocumentRenderer::ExportPng(document, document.artboards.front().bounds, 1.0, false);
  const std::filesystem::path path = std::filesystem::path(LEINWAND_TEST_OUTPUT_DIR) / name;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

bool HasRow(const io::ImportReport& report, const std::string& start, io::ReportAction action) {
  for (const auto& row : report.rows) {
    if (row.kind.rfind(start, 0) == 0 && row.action == action) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("SVG import: size, layers, shapes, CSS and what it could not take") {
  const auto result = io::ImportSvg(ReadTestFile("svg/features.svg"));
  REQUIRE(result.document);
  const core::Document& d = *result.document;
  // 200 x 150 px shows the 400 x 300 viewBox at half size; px count as pt.
  CHECK(d.artboards.front().bounds.width() == 200);
  REQUIRE(d.layers.size() == 2);
  CHECK(d.layers[0]->name == "Background");
  CHECK(d.layers[1]->name == "Art");

  // The CSS class gives the rounded rectangle its paint; it stays live.
  const auto& art = d.layers[1]->children;
  const auto* rect = std::get_if<core::ShapeObject>(std::get<core::ObjectPtr>(art[0]).get());
  REQUIRE(rect);
  const auto& shape = std::get<core::RectangleShape>(rect->shape);
  CHECK(shape.width == Approx(60));  // 120 user units at half scale.
  CHECK(shape.corners[0].radius == Approx(6));
  const auto& a = rect->common.appearance;
  REQUIRE(core::FrontFill(a));
  CHECK(core::FrontFill(a)->paint ==
        core::Color{core::RgbColor{0xe3 / 255.0, 0x48 / 255.0, 0x50 / 255.0}});
  CHECK(core::FrontStroke(a)->width == Approx(2));  // Scaled with the geometry.

  // #star gets gold from the id rule and is clipped by the circle.
  CHECK(HasRow(result.report, "clip-path", io::ReportAction::kConverted));
  // The sky's gradient stays a gradient (phase 2), laid across the whole
  // rect from its bounding box and scaled by the viewBox with it.
  bool sky = false;
  for (const auto& id : core::AllObjectIds(*result.document)) {
    const auto* fill = core::FrontFill(core::CommonOf(*result.document->FindObject(id)).appearance);
    if (!fill || !fill->gradient) continue;
    sky = true;
    CHECK(fill->gradient->start.x == Approx(0));
    CHECK(fill->gradient->end.x == Approx(200));
    CHECK(fill->gradient->stops.size() == 2);
  }
  CHECK(sky);
  CHECK_FALSE(HasRow(result.report, "fill gradient", io::ReportAction::kApproximated));
  CHECK(HasRow(result.report, "<text>", io::ReportAction::kPreserved));
  CHECK(HasRow(result.report, "<defs>", io::ReportAction::kPreserved));
}

TEST_CASE("SVG import renders like the reference") {
  const auto result = io::ImportSvg(ReadTestFile("svg/features.svg"));
  REQUIRE(result.document);
  int w = 0, h = 0;
  const auto pixels = Render(*result.document, &w, &h);
  CHECK(testing::MatchesBaseline("svg/features", pixels, w, h));
}

TEST_CASE("SVG export and import again looks the same") {
  // SVG has no inside or outside strokes: they are written centered and
  // reported.
  const core::Document original = render::MakeShowcaseDocument();
  io::ImportReport issues;
  io::ExportSvg(original, {}, &issues);
  CHECK(HasRow(issues, "inside or outside stroke", io::ReportAction::kApproximated));
  // Everything else comes back the same.
  const core::Document showcase =
      core::EditAppearance(original, core::AllObjectIds(original), [](core::Appearance& a) {
        for (auto& item : a) {
          if (auto* stroke = std::get_if<core::Stroke>(&item))
            stroke->align = core::StrokeAlign::kCenter;
        }
      });
  const std::string svg = io::ExportSvg(showcase);
  const auto again = io::ImportSvg(svg);
  REQUIRE(again.document);
  int w = 0, h = 0, w2 = 0, h2 = 0;
  const auto before = Render(showcase, &w, &h);
  const auto after = Render(*again.document, &w2, &h2);
  REQUIRE(w == w2);
  REQUIRE(h == h2);
  const double difference = testing::DifferingFraction(before, after);
  CHECK(difference < 0.002);
  if (difference >= 0.002) {
    Dump(showcase, "svg/roundtrip-before.png");
    Dump(*again.document, "svg/roundtrip-after.png");
  }

  // A second round trip is stable.
  const std::string svg2 = io::ExportSvg(*again.document);
  const auto third = io::ImportSvg(svg2);
  REQUIRE(third.document);
  int w3 = 0, h3 = 0;
  CHECK(testing::DifferingFraction(after, Render(*third.document, &w3, &h3)) < 0.002);
}

TEST_CASE("Preserved SVG elements are written back in place") {
  const auto result = io::ImportSvg(ReadTestFile("svg/features.svg"));
  REQUIRE(result.document);
  const std::string svg = io::ExportSvg(*result.document);
  CHECK(svg.find(">Hello</text>") != std::string::npos);
  CHECK(svg.find("linearGradient") != std::string::npos);
  // The text's CSS class was turned into an inline style.
  CHECK(svg.find("fill:#e34850") != std::string::npos);
  CHECK(svg.find("inkscape:label=\"Art\"") != std::string::npos);
}

TEST_CASE("SVG: bad input is an error, not a crash") {
  CHECK(!io::ImportSvg("<svg").document);
  CHECK(!io::ImportSvg("<html/>").document);
  const auto minimal = io::ImportSvg("<svg xmlns='http://www.w3.org/2000/svg'/>");
  REQUIRE(minimal.document);
  CHECK(minimal.document->layers.size() == 1);
}

TEST_CASE("SVG: a physical size keeps its size in points") {
  const auto result = io::ImportSvg(
      "<svg xmlns='http://www.w3.org/2000/svg' width='210mm' height='297mm' viewBox='0 0 210 297'>"
      "<rect x='10' y='10' width='10' height='10'/></svg>");
  REQUIRE(result.document);
  CHECK(result.document->artboards.front().bounds.width() == Approx(595.2756));
  const auto& rect = std::get<core::ShapeObject>(
      *std::get<core::ObjectPtr>(result.document->layers[0]->children[0]));
  CHECK(std::get<core::RectangleShape>(rect.shape).width == Approx(28.3465).epsilon(1e-4));
}

TEST_CASE("SVG gradients: user space, transforms, inherited stops, radial") {
  const auto result = io::ImportSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
      xmlns:xlink="http://www.w3.org/1999/xlink" width="200" height="100">
    <defs>
      <linearGradient id="base">
        <stop offset="0" stop-color="red"/>
        <stop offset="50%" stop-color="lime" stop-opacity="0.5"/>
        <stop offset="1" stop-color="blue"/>
      </linearGradient>
      <linearGradient id="user" xlink:href="#base" gradientUnits="userSpaceOnUse"
          x1="10" y1="0" x2="110" y2="0" gradientTransform="translate(5 0)"/>
      <radialGradient id="ring" xlink:href="#base" cx="0.5" cy="0.5" r="0.5" fx="0.25"/>
    </defs>
    <rect id="a" width="200" height="50" fill="url(#user)"/>
    <rect id="b" y="50" width="200" height="50" fill="url(#ring)"/>
  </svg>)svg");
  REQUIRE(result.document);
  const auto gradient = [&](const char* id) {
    return core::FrontFill(core::CommonOf(*result.document->FindObject(id)).appearance)->gradient;
  };
  const auto a = gradient("a");
  REQUIRE(a);
  CHECK(a->type == core::GradientType::kLinear);
  CHECK(a->start.x == Approx(15));
  CHECK(a->end.x == Approx(115));
  REQUIRE(a->stops.size() == 3);  // Inherited through href.
  CHECK(a->stops[1].offset == Approx(0.5));
  CHECK(a->stops[1].opacity == Approx(0.5));
  // Radial in the 200 x 50 box: centre (100, 75), radius 100 across, 25
  // down (aspect 0.25), the highlight a quarter of the way in.
  const auto b = gradient("b");
  REQUIRE(b);
  CHECK(b->type == core::GradientType::kRadial);
  CHECK(b->start.x == Approx(100));
  CHECK(b->start.y == Approx(75));
  CHECK(b->end.x == Approx(200));
  CHECK(b->aspect == Approx(0.25));
  REQUIRE(b->focal);
  CHECK(b->focal->x == Approx(50));
}
