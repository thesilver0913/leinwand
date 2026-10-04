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
#include "geometry/bezier.h"
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
    const core::Object* object = result.document->FindObject(id);
    if (!object) continue;  // A story id.
    const auto* fill = core::FrontFill(core::CommonOf(*object).appearance);
    if (!fill || !fill->gradient) continue;
    sky = true;
    CHECK(fill->gradient->start.x == Approx(0));
    CHECK(fill->gradient->end.x == Approx(200));
    CHECK(fill->gradient->stops.size() == 2);
  }
  CHECK(sky);
  CHECK_FALSE(HasRow(result.report, "fill gradient", io::ReportAction::kApproximated));
  // <text> becomes point text (M14).
  CHECK_FALSE(HasRow(result.report, "<text>", io::ReportAction::kPreserved));
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
  CHECK(svg.find(">Hello</tspan></text>") != std::string::npos);
  CHECK(svg.find("linearGradient") != std::string::npos);
  // The text's CSS class gave it its paint.
  CHECK(svg.find("fill=\"#e34850\"") != std::string::npos);
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

TEST_CASE("SVG opacity masks: clip, invert and isolated groups come back") {
  const core::Document original = render::MakeShowcaseDocument();
  const auto again = io::ImportSvg(io::ExportSvg(original));
  REQUIRE(again.document);
  // The masked objects come back inside a group that carries the mask.
  std::vector<core::OpacityMask> masks;
  core::VisitObjects(*again.document, [&](const core::Object& o) {
    if (const auto& mask = core::CommonOf(o).mask) masks.push_back(*mask);
  });
  REQUIRE(masks.size() == 3);
  CHECK((masks[0].clip && !masks[0].invert));
  CHECK((!masks[1].clip && !masks[1].invert));
  CHECK((masks[2].clip && masks[2].invert));
  for (const auto& row : again.report.rows) CHECK(row.kind.find("mask") == std::string::npos);

  const auto isolated = io::ImportSvg(R"(<svg xmlns="http://www.w3.org/2000/svg" width="10"
      height="10"><g id="g" style="isolation:isolate"><rect width="5" height="5"/></g></svg>)");
  REQUIRE(isolated.document);
  CHECK(std::get<core::GroupObject>(*isolated.document->FindObject("g")).isolated);
}

TEST_CASE("SVG masks from other programs: user-space content, element transform") {
  const auto result = io::ImportSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100"
      height="100"><defs><mask id="m"><circle cx="10" cy="10" r="5" fill="white"/></mask></defs>
      <rect id="r" x="0" y="0" width="20" height="20" transform="translate(30 0)"
      mask="url(#m)"/><rect id="s" width="5" height="5" mask="url(#missing)"/></svg>)svg");
  REQUIRE(result.document);
  const core::Object* r = result.document->FindObject("r");
  REQUIRE(r);
  const auto& mask = core::CommonOf(*r).mask;
  REQUIRE(mask);
  // In the parent's coordinates, like the rectangle itself.
  const core::Rect bounds = geometry::Bounds(*mask->art);
  CHECK(bounds.left == Approx(35));
  CHECK(bounds.right == Approx(45));
  CHECK(HasRow(result.report, "mask (missing", io::ReportAction::kDiscarded));
}

// Writes the showcase as SVG and PNG for comparing in a browser:
// LEINWAND_TEST_OUTPUT_DIR/svg/showcase.svg and .png.
TEST_CASE("Showcase as SVG for a browser", "[.][showcase-svg]") {
  const core::Document showcase = render::MakeShowcaseDocument();
  const std::filesystem::path path =
      std::filesystem::path(LEINWAND_TEST_OUTPUT_DIR) / "svg" / "showcase.svg";
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << io::ExportSvg(showcase);
  Dump(showcase, "svg/showcase.png");
}

TEST_CASE("SVG text: written as text with tspans, read back as point text") {
  core::CharacterStyle style;
  style.size = 20;
  core::Story story = core::MakeStory("s", U"日本語 Text\n二行目", style);
  story = core::WithCharacterStyle(
      story, 4, 8, [](core::CharacterStyle& s) { s.font = {"Source Sans 3", "Bold", {}}; });
  core::TextObject text;
  text.common.id = "t";
  text.common.appearance = {core::Fill{core::RgbColor{1, 0, 0}}};
  text.story = std::make_shared<const core::Story>(story);
  text.transform = core::Matrix::Translate(20, 40);
  core::Layer layer;
  layer.id = "l";
  layer.children = {core::MakeObject(text)};
  core::Document document;
  document.artboards = {{"ab", "Artboard 1", core::Rect::FromXYWH(0, 0, 200, 100), {}, 0}};
  document.layers = {core::MakeLayer(std::move(layer))};

  const std::string svg = io::ExportSvg(document);
  CHECK(svg.find("<text") != std::string::npos);
  CHECK(svg.find("font-family=\"'Source Sans 3'\"") != std::string::npos);
  CHECK(svg.find("font-weight=\"700\"") != std::string::npos);
  const auto again = io::ImportSvg(svg);
  REQUIRE(again.document);
  const core::TextObject* back = nullptr;
  core::VisitObjects(*again.document, [&](const core::Object& o) {
    if (const auto* t = std::get_if<core::TextObject>(&o)) back = t;
  });
  REQUIRE(back);
  CHECK(back->story->text == story.text);
  CHECK(core::StyleAt(*back->story, 5).font.style == "Bold");
  CHECK(core::StyleAt(*back->story, 0).font.family == core::DefaultFont().family);
  // And it looks the same.
  int w = 0, h = 0, w2 = 0, h2 = 0;
  const auto before = Render(document, &w, &h);
  const auto after = Render(*again.document, &w2, &h2);
  REQUIRE(w == w2);
  CHECK(testing::DifferingFraction(before, after) < 0.002);
}

TEST_CASE("SVG text from other programs: anchors, inherited fonts, text on a path kept") {
  const auto result = io::ImportSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200"
      height="100"><g font-family="Arial, sans-serif" font-size="12"><text id="a" x="100" y="20"
      text-anchor="middle" font-weight="bold">  centred
      text </text></g><defs><path id="p" d="M0 0 H100"/></defs>
      <text id="b"><textPath href="#p">on a path</textPath></text></svg>)svg");
  REQUIRE(result.document);
  const auto* a = std::get_if<core::TextObject>(result.document->FindObject("a"));
  REQUIRE(a);
  CHECK(a->story->text == U"centred text");  // White space collapsed.
  CHECK(a->story->paragraphs[0].align == core::TextAlign::kCenter);
  CHECK(core::StyleAt(*a->story, 0).size == 12);
  CHECK(std::holds_alternative<core::PreservedObject>(*result.document->FindObject("b")));
}
