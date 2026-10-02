// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/document_renderer.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "image_compare.h"
#include "render/test_document.h"

using namespace leinwand::core;
using leinwand::render::DocumentRenderer;
using leinwand::render::View;

namespace {

constexpr int kSize = 100;

struct Rgba {
  int r, g, b, a;
};

Rgba PixelAt(const std::vector<std::uint8_t>& pixels, int x, int y) {
  const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
  return {pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]};
}

bool Near(Rgba p, int r, int g, int b, int tolerance = 3) {
  return std::abs(p.r - r) <= tolerance && std::abs(p.g - g) <= tolerance &&
         std::abs(p.b - b) <= tolerance;
}

PathData Rectangle(double l, double t, double r, double b) {
  PathData path;
  path.anchors = {{{l, t}}, {{r, t}}, {{r, b}}, {{l, b}}};
  path.closed = true;
  return path;
}

ObjectPtr Shape(PathData path, Appearance appearance, std::string id = "o") {
  PathObject object;
  object.common.id = std::move(id);
  object.common.appearance = std::move(appearance);
  object.path = std::move(path);
  return MakeObject(std::move(object));
}

// A 100x100 artboard filling the target, with one layer holding `objects`.
Document DocumentWith(std::vector<ObjectPtr> objects) {
  Layer layer;
  layer.id = "layer";
  for (auto& object : objects) layer.children.push_back(std::move(object));
  Document document;
  document.artboards = {{"ab", "Artboard 1", Rect::FromXYWH(0, 0, kSize, kSize)}};
  document.layers = {MakeLayer(std::move(layer))};
  return document;
}

std::vector<std::uint8_t> Render(const Document& document, View view = {}) {
  DocumentRenderer renderer;
  return renderer.RenderRaster(document, kSize, kSize, view);
}

const Fill kRed{RgbColor{1, 0, 0}};
const Fill kBlue{RgbColor{0, 0, 1}};

}  // namespace

TEST_CASE("Artboard is white over the pasteboard, and fills paint inside paths") {
  const auto pixels = Render(DocumentWith({Shape(Rectangle(20, 20, 80, 80), {kRed})}),
                             View{10, 10, 0.8});         // Leaves pasteboard at the edges.
  CHECK(Near(PixelAt(pixels, 2, 2), 0x53, 0x53, 0x53));  // Pasteboard.
  CHECK(Near(PixelAt(pixels, 12, 12), 255, 255, 255));   // Artboard.
  CHECK(Near(PixelAt(pixels, 50, 50), 255, 0, 0));       // Fill.
}

TEST_CASE("The appearance stack is front to back") {
  const auto pixels = Render(DocumentWith({Shape(Rectangle(20, 20, 80, 80), {kBlue, kRed})}));
  CHECK(Near(PixelAt(pixels, 50, 50), 0, 0, 255));
}

TEST_CASE("Stroke alignment puts the stroke centred, inside or outside the path") {
  auto render = [](StrokeAlign align) {
    Stroke stroke{RgbColor{0, 0, 0}};
    stroke.width = 10;
    stroke.align = align;
    return Render(DocumentWith({Shape(Rectangle(20, 20, 80, 80), {stroke})}));
  };
  // The left edge is x = 20. Sample 3 px outside and 3 px inside it.
  const auto center = render(StrokeAlign::kCenter);
  CHECK(Near(PixelAt(center, 17, 50), 0, 0, 0));
  CHECK(Near(PixelAt(center, 23, 50), 0, 0, 0));
  const auto inside = render(StrokeAlign::kInside);
  CHECK(Near(PixelAt(inside, 17, 50), 255, 255, 255));
  CHECK(Near(PixelAt(inside, 27, 50), 0, 0, 0));
  const auto outside = render(StrokeAlign::kOutside);
  CHECK(Near(PixelAt(outside, 13, 50), 0, 0, 0));
  CHECK(Near(PixelAt(outside, 23, 50), 255, 255, 255));
}

TEST_CASE("Open paths ignore inside and outside alignment") {
  Stroke stroke{RgbColor{0, 0, 0}};
  stroke.width = 10;
  stroke.align = StrokeAlign::kInside;
  PathData line;
  line.anchors = {{{10, 50}}, {{90, 50}}};
  const auto pixels = Render(DocumentWith({Shape(line, {stroke})}));
  CHECK(Near(PixelAt(pixels, 50, 47), 0, 0, 0));
  CHECK(Near(PixelAt(pixels, 50, 53), 0, 0, 0));
}

TEST_CASE("Groups apply their transform and opacity") {
  GroupObject group;
  group.common.id = "g";
  group.common.opacity = 0.5;
  group.transform = Matrix::Translate(50, 0);
  group.children = {Shape(Rectangle(0, 0, 40, 40), {kRed})};
  const auto pixels = Render(DocumentWith({MakeObject(std::move(group))}));
  CHECK(Near(PixelAt(pixels, 20, 20), 255, 255, 255));  // Moved away from here.
  CHECK(Near(PixelAt(pixels, 70, 20), 255, 128, 128));  // Half red over white.
}

TEST_CASE("Group opacity applies to the group as a whole") {
  // Two overlapping opaque squares in a half-transparent group: the overlap
  // must not get darker than the rest.
  GroupObject group;
  group.common.opacity = 0.5;
  group.children = {Shape(Rectangle(10, 10, 60, 60), {kBlue}),
                    Shape(Rectangle(40, 40, 90, 90), {kBlue})};
  const auto pixels = Render(DocumentWith({MakeObject(std::move(group))}));
  CHECK(Near(PixelAt(pixels, 20, 20), 128, 128, 255));
  CHECK(Near(PixelAt(pixels, 50, 50), 128, 128, 255));
}

TEST_CASE("Clipping groups clip to their frontmost child and do not paint it") {
  GroupObject group;
  group.clipped = true;
  group.children = {Shape(Rectangle(0, 0, 100, 100), {kRed}),
                    Shape(Rectangle(25, 25, 75, 75), {kBlue})};  // The clip path.
  const auto pixels = Render(DocumentWith({MakeObject(std::move(group))}));
  CHECK(Near(PixelAt(pixels, 10, 10), 255, 255, 255));  // Clipped away.
  CHECK(Near(PixelAt(pixels, 50, 50), 255, 0, 0));      // Red, not the blue clip path.
}

TEST_CASE("Compound paths honour the even-odd rule") {
  CompoundPathObject donut;
  donut.subpaths = {Rectangle(10, 10, 90, 90), Rectangle(30, 30, 70, 70)};
  donut.fill_rule = FillRule::kEvenOdd;
  donut.common.appearance = {kRed};
  const auto pixels = Render(DocumentWith({MakeObject(std::move(donut))}));
  CHECK(Near(PixelAt(pixels, 20, 20), 255, 0, 0));
  CHECK(Near(PixelAt(pixels, 50, 50), 255, 255, 255));  // The hole.
}

TEST_CASE("Hidden objects and hidden layers are not painted") {
  PathObject hidden;
  hidden.common.visible = false;
  hidden.common.appearance = {kRed};
  hidden.path = Rectangle(0, 0, 50, 50);
  Document document = DocumentWith({MakeObject(std::move(hidden))});
  CHECK(Near(PixelAt(Render(document), 25, 25), 255, 255, 255));

  document = DocumentWith({Shape(Rectangle(0, 0, 50, 50), {kRed})});
  Layer layer = *document.layers[0];
  layer.visible = false;
  document.layers[0] = MakeLayer(std::move(layer));
  CHECK(Near(PixelAt(Render(document), 25, 25), 255, 255, 255));
}

TEST_CASE("Spot colors and global colors resolve through swatches") {
  Document document = DocumentWith({
      Shape(Rectangle(0, 0, 50, 100), {Fill{SpotColor{"spot", 0.5}}}),
      Shape(Rectangle(50, 0, 100, 100), {Fill{SwatchRef{"global"}}}),
      Shape(Rectangle(0, 0, 10, 10), {Fill{SwatchRef{"missing"}}}),
  });
  document.swatches = {{"spot", "PANTONE Red", Swatch::Kind::kSpot, RgbColor{1, 0, 0}},
                       {"global", "Cyan", Swatch::Kind::kProcess, CmykColor{1, 0, 0, 0}}};
  const auto pixels = Render(document);
  CHECK(Near(PixelAt(pixels, 25, 50), 255, 128, 128));  // 50% tint of red.
  CHECK(Near(PixelAt(pixels, 75, 50), 0, 255, 255));    // Naive CMYK cyan.
  CHECK(Near(PixelAt(pixels, 5, 5), 255, 128, 128));    // Missing swatch paints nothing.
}

TEST_CASE("Objects outside the view are culled without changing the picture") {
  std::vector<ObjectPtr> objects;
  for (int i = 0; i < 10; ++i) {
    objects.push_back(Shape(Rectangle(i * 100 + 10, 10, i * 100 + 90, 90), {kRed}));
  }
  const Document document = DocumentWith(std::move(objects));
  DocumentRenderer renderer;
  const auto pixels = renderer.RenderRaster(document, kSize, kSize, View{});
  CHECK(renderer.last_stats().drawn == 1);
  CHECK(renderer.last_stats().culled == 9);
  CHECK(Near(PixelAt(pixels, 50, 50), 255, 0, 0));
  // Panning to the fourth square draws only that one.
  renderer.RenderRaster(document, kSize, kSize, View{-300, 0, 1});
  CHECK(renderer.last_stats().drawn == 1);
}

TEST_CASE("Replacing an object is picked up despite the path cache") {
  DocumentRenderer renderer;
  Document document = DocumentWith({Shape(Rectangle(0, 0, 100, 100), {kRed})});
  CHECK(Near(PixelAt(renderer.RenderRaster(document, kSize, kSize, {}), 50, 50), 255, 0, 0));
  document = DocumentWith({Shape(Rectangle(0, 0, 100, 100), {kBlue})});
  CHECK(Near(PixelAt(renderer.RenderRaster(document, kSize, kSize, {}), 50, 50), 0, 0, 255));
}

TEST_CASE("The overlay outlines the selection and draws the box, handles and marquee") {
  const Document document = DocumentWith({Shape(Rectangle(20, 20, 80, 80), {kRed}, "sq")});
  leinwand::render::Overlay overlay;
  overlay.selection = {"sq"};
  overlay.bounding_box = Rect{20, 20, 80, 80};
  overlay.marquee = Rect{5, 85, 95, 95};
  DocumentRenderer renderer;
  const auto pixels = renderer.RenderRaster(document, kSize, kSize, View{}, &overlay);
  CHECK(Near(PixelAt(pixels, 50, 50), 255, 0, 0));             // The fill is untouched.
  CHECK(Near(PixelAt(pixels, 50, 20), 255, 255, 255, 10));     // Top handle: white centre.
  CHECK(Near(PixelAt(pixels, 35, 20), 0x40, 0x69, 0xfd, 60));  // Box edge: selection blue.
  CHECK((Near(PixelAt(pixels, 20, 20), 255, 255, 255, 10) ||   // Corner: handle over anchor.
         Near(PixelAt(pixels, 20, 20), 0x40, 0x69, 0xfd, 60)));
  CHECK(leinwand::testing::MatchesBaseline("render/overlay", pixels, kSize, kSize));
}

TEST_CASE("The overlay draws edited paths, the pen's rubber band and smart guides") {
  const Document document = DocumentWith({Shape(Rectangle(20, 20, 80, 80), {kRed}, "sq")});
  leinwand::render::Overlay overlay;
  leinwand::core::PathData curve;
  curve.anchors = {{{10, 60}, {}, {0, -30}},
                   {{50, 40}, {-20, 0}, {20, 0}, leinwand::core::AnchorKind::kSmooth},
                   {{90, 60}}};
  overlay.paths = {{curve, {1}, {0, 1, 2}}};
  leinwand::core::PathData band;
  band.anchors = {{{90, 60}}, {{90, 90}}};
  overlay.rubber_band = band;
  overlay.guides = {{{0, 10.5}, {100, 10.5}}};  // Centred on a pixel row.
  DocumentRenderer renderer;
  const auto pixels = renderer.RenderRaster(document, kSize, kSize, View{}, &overlay);
  CHECK(Near(PixelAt(pixels, 50, 40), 0x40, 0x69, 0xfd, 10));  // Selected anchor: solid.
  CHECK(Near(PixelAt(pixels, 10, 60), 255, 255, 255, 10));     // Unselected: hollow.
  CHECK(Near(PixelAt(pixels, 70, 40), 0x40, 0x69, 0xfd, 60));  // A handle's dot.
  CHECK(Near(PixelAt(pixels, 50, 10), 255, 0, 255, 60));       // The guide.
  CHECK(leinwand::testing::MatchesBaseline("render/edited_path", pixels, kSize, kSize));
}

TEST_CASE("Outline view draws hairlines without paint") {
  const Document document = DocumentWith({Shape(Rectangle(20, 20, 80, 80), {kRed}, "sq")});
  leinwand::render::Overlay overlay;
  overlay.outline = true;
  DocumentRenderer renderer;
  const auto pixels = renderer.RenderRaster(document, kSize, kSize, View{}, &overlay);
  CHECK(Near(PixelAt(pixels, 50, 50), 255, 255, 255, 10));  // No fill.
  const Rgba edge = PixelAt(pixels, 50, 20);                // The outline, anti-aliased.
  CHECK((edge.r < 200 && edge.r == edge.g && edge.g == edge.b));
}

TEST_CASE("The showcase document matches its baseline image") {
  // 800x600 pt at quarter scale. The hidden layer is a full-artboard red
  // rectangle; it must not show anywhere.
  DocumentRenderer renderer;
  const auto pixels =
      renderer.RenderRaster(leinwand::render::MakeShowcaseDocument(), 200, 150, View{0, 0, 0.25});
  CHECK(leinwand::testing::MatchesBaseline("render/showcase", pixels, 200, 150));
  // Top-left corner of the artboard is blank paper, not the hidden red.
  const std::size_t corner = (2 * 200 + 2) * 4;
  CHECK(pixels[corner] > 240);
  CHECK(pixels[corner + 1] > 240);
}

TEST_CASE("A mixed scene matches its baseline image") {
  // Curves, a dashed round-capped stroke, a rotated group and a CMYK fill.
  PathData blob;
  blob.anchors = {{{50, 10}, {-25, 0}, {25, 0}, AnchorKind::kSymmetric},
                  {{90, 50}, {0, -25}, {0, 25}, AnchorKind::kSymmetric},
                  {{50, 90}, {25, 0}, {-25, 0}, AnchorKind::kSymmetric},
                  {{10, 50}, {0, 25}, {0, -25}, AnchorKind::kSymmetric}};
  blob.closed = true;
  Stroke dashed{RgbColor{0.1, 0.1, 0.4}};
  dashed.width = 4;
  dashed.cap = StrokeCap::kRound;
  dashed.dashes = {6, 6};

  GroupObject rotated;
  rotated.transform = Matrix::Translate(50, 50) * Matrix::Rotate(0.4) * Matrix::Translate(-15, -15);
  rotated.common.opacity = 0.8;
  rotated.children = {Shape(Rectangle(0, 0, 30, 30), {Fill{CmykColor{0, 0.8, 0.9, 0}}})};

  const Document document = DocumentWith(
      {Shape(blob, {dashed, Fill{RgbColor{0.6, 0.85, 1}}}), MakeObject(std::move(rotated))});
  const auto pixels = Render(document);
  CHECK(leinwand::testing::MatchesBaseline("render/mixed_scene", pixels, kSize, kSize));
}
