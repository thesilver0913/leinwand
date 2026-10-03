// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/test_document.h"

#include <cmath>
#include <numbers>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace leinwand::render {

namespace {

using core::Point;

// Paths are laid out on a square grid, one blob per cell.
constexpr double kCellSize = 60.0;

core::PathData MakeBlob(std::mt19937& rng, double cx, double cy) {
  std::uniform_int_distribution<int> anchor_count(3, 7);
  std::uniform_real_distribution<double> radius(10.0, 28.0);
  std::uniform_real_distribution<double> jitter(-0.3, 0.3);

  const int n = anchor_count(rng);
  std::vector<Point> points(n);
  for (int i = 0; i < n; ++i) {
    const double angle = (i + jitter(rng)) * 2.0 * std::numbers::pi / n;
    const double r = radius(rng);
    points[i] = {cx + r * std::cos(angle), cy + r * std::sin(angle)};
  }

  // Catmull-Rom style tangents give a smooth closed curve through the points.
  core::PathData path;
  path.closed = true;
  for (int i = 0; i < n; ++i) {
    const Point tangent = (points[(i + 1) % n] - points[(i + n - 1) % n]) * (1.0 / 6.0);
    path.anchors.push_back({points[i], tangent * -1.0, tangent, core::AnchorKind::kSmooth});
  }
  return path;
}

core::PathData Rectangle(double x, double y, double w, double h) {
  core::PathData path;
  path.anchors = {{{x, y}}, {{x + w, y}}, {{x + w, y + h}}, {{x, y + h}}};
  path.closed = true;
  return path;
}

core::PathData Circle(double cx, double cy, double r) {
  const double k = r * 0.5522847498;  // Handle length for a quarter circle.
  core::PathData path;
  path.closed = true;
  path.anchors = {{{cx + r, cy}, {0, -k}, {0, k}, core::AnchorKind::kSymmetric},
                  {{cx, cy + r}, {k, 0}, {-k, 0}, core::AnchorKind::kSymmetric},
                  {{cx - r, cy}, {0, k}, {0, -k}, core::AnchorKind::kSymmetric},
                  {{cx, cy - r}, {-k, 0}, {k, 0}, core::AnchorKind::kSymmetric}};
  return path;
}

core::PathData Star(double cx, double cy, double outer, double inner, int points) {
  core::PathData path;
  path.closed = true;
  for (int i = 0; i < points * 2; ++i) {
    const double r = i % 2 == 0 ? outer : inner;
    const double angle = -std::numbers::pi / 2 + i * std::numbers::pi / points;
    path.anchors.push_back({{cx + r * std::cos(angle), cy + r * std::sin(angle)}});
  }
  return path;
}

core::RgbColor Rgb(int hex) {
  return {((hex >> 16) & 0xff) / 255.0, ((hex >> 8) & 0xff) / 255.0, (hex & 0xff) / 255.0};
}

// Builds objects with readable, unique ids.
class Builder {
 public:
  core::ObjectPtr Path(core::PathData path, core::Appearance appearance) {
    core::PathObject object;
    object.common.id = Id("path");
    object.common.appearance = std::move(appearance);
    object.path = std::move(path);
    return core::MakeObject(std::move(object));
  }

  core::ObjectPtr Group(std::vector<core::ObjectPtr> children,
                        void (*setup)(core::GroupObject&) = nullptr) {
    core::GroupObject group;
    group.common.id = Id("group");
    group.children = std::move(children);
    if (setup) setup(group);
    return core::MakeObject(std::move(group));
  }

  std::string Id(const char* kind) { return std::string(kind) + std::to_string(++count_); }

 private:
  int count_ = 0;
};

core::Stroke MakeStroke(core::Color color, double width,
                        core::StrokeAlign align = core::StrokeAlign::kCenter) {
  core::Stroke stroke{color};
  stroke.width = width;
  stroke.align = align;
  return stroke;
}

}  // namespace

core::Document MakeShowcaseDocument() {
  using core::Fill;
  Builder b;
  const core::Stroke hairline = MakeStroke(Rgb(0xe0245e), 1);  // Marks the true path.

  // Row 1: stroke alignment (centre, inside, outside), each with the path
  // itself drawn as a thin line on top.
  std::vector<core::LayerChild> strokes;
  const core::StrokeAlign aligns[] = {core::StrokeAlign::kCenter, core::StrokeAlign::kInside,
                                      core::StrokeAlign::kOutside};
  for (int i = 0; i < 3; ++i) {
    strokes.push_back(
        b.Path(Rectangle(60 + i * 130, 60, 90, 90),
               {hairline, MakeStroke(Rgb(0x2d6cdf), 16, aligns[i]), Fill{Rgb(0xdbe8ff)}}));
  }
  // Dashed circle with round caps.
  core::Stroke dashed = MakeStroke(Rgb(0x1b1b1b), 6);
  dashed.cap = core::StrokeCap::kRound;
  dashed.dashes = {0.01, 14};  // Dots.
  strokes.push_back(b.Path(Circle(510, 105, 45), {dashed}));
  // Two fills: a translucent multiply fill over a solid one.
  strokes.push_back(b.Path(
      Star(660, 105, 55, 25, 5),
      {MakeStroke(Rgb(0x6b3a00), 2),
       Fill{Rgb(0xff8a00), std::nullopt, 0.6, core::BlendMode::kMultiply}, Fill{Rgb(0xffe066)}}));

  // Row 2: compound path, group opacity, blend modes, clipping.
  std::vector<core::LayerChild> composition;
  core::CompoundPathObject donut;
  donut.common.id = b.Id("compound");
  donut.common.appearance = {MakeStroke(Rgb(0x1b1b1b), 2), Fill{Rgb(0x3fbf7f)}};
  donut.subpaths = {Circle(105, 290, 55), Circle(105, 290, 25)};
  donut.fill_rule = core::FillRule::kEvenOdd;
  composition.push_back(core::MakeObject(std::move(donut)));

  composition.push_back(
      b.Group({b.Path(Rectangle(200, 240, 80, 80), {Fill{Rgb(0x7a3cff)}}),
               b.Path(Rectangle(240, 280, 80, 80), {Fill{Rgb(0x7a3cff)}})},
              [](core::GroupObject& g) { g.common.opacity = 0.5; }));  // Overlap stays uniform.

  const int venn[] = {0x00b7ff, 0xff3db5, 0xffe500};
  const core::Point centres[] = {{420, 270}, {460, 270}, {440, 305}};
  std::vector<core::ObjectPtr> circles;
  for (int i = 0; i < 3; ++i) {
    core::PathObject circle;
    circle.common.id = b.Id("venn");
    circle.common.blend_mode = core::BlendMode::kMultiply;
    circle.common.appearance = {Fill{Rgb(venn[i])}};
    circle.path = Circle(centres[i].x, centres[i].y, 38);
    circles.push_back(core::MakeObject(std::move(circle)));
  }
  composition.push_back(b.Group(std::move(circles)));

  std::vector<core::ObjectPtr> stripes;
  for (int i = 0; i < 8; ++i) {
    stripes.push_back(b.Path(Rectangle(560 + i * 18, 230, 9, 130), {Fill{Rgb(0xff5a36)}}));
  }
  stripes.push_back(b.Path(Circle(630, 295, 55), {}));  // The clip path.
  composition.push_back(
      b.Group(std::move(stripes), [](core::GroupObject& g) { g.clipped = true; }));

  // Row 3: CMYK process color and tints of a spot color.
  std::vector<core::LayerChild> colors;
  colors.push_back(b.Path(Rectangle(60, 420, 90, 90), {Fill{core::CmykColor{0, 0.6, 1, 0}}}));
  const double tints[] = {1.0, 0.75, 0.5, 0.25};
  for (int i = 0; i < 4; ++i) {
    colors.push_back(b.Path(Rectangle(180 + i * 70, 420, 60, 90),
                            {Fill{core::SpotColor{"spot-teal", tints[i]}}}));
  }
  // Gradients: linear with three stops and a moved midpoint; radial,
  // squashed, with an off-centre highlight.
  Fill linear{Rgb(0x2d6cdf)};
  linear.gradient = core::Gradient{core::GradientType::kLinear,
                                   {{0.0, Rgb(0x2d6cdf), 1.0, 0.25},
                                    {0.6, Rgb(0xffffff), 1.0, 0.5},
                                    {1.0, Rgb(0xff8a00), 0.5, 0.5}},
                                   {470, 440},
                                   {560, 440}};
  colors.push_back(b.Path(Rectangle(470, 420, 90, 40), {linear}));
  Fill radial{Rgb(0xffffff)};
  radial.gradient = core::Gradient{core::GradientType::kRadial,
                                   {{0.0, Rgb(0xffffff), 1.0, 0.5}, {1.0, Rgb(0x7a3cff), 1.0, 0.5}},
                                   {515, 490},
                                   {560, 490},
                                   0.6,
                                   core::Point{500, 480}};
  colors.push_back(b.Path(Rectangle(470, 470, 90, 40), {radial}));

  // Row 4: opacity masks. Stripes fading out under a gradient mask; a mask
  // without "Clip" (a black circle punches a hole); an inverted one.
  std::vector<core::ObjectPtr> fading;
  for (int i = 0; i < 15; ++i) {
    fading.push_back(b.Path(Rectangle(180 + i * 18, 530, 9, 50), {Fill{Rgb(0x2d6cdf)}}));
  }
  Fill fade{Rgb(0xffffff)};
  fade.gradient = core::Gradient{core::GradientType::kLinear,
                                 {{0.0, Rgb(0xffffff), 1.0, 0.5}, {1.0, Rgb(0x000000), 1.0, 0.5}},
                                 {180, 555},
                                 {450, 555}};
  const auto masked = [&](core::ObjectPtr object, core::ObjectPtr art, bool clip, bool invert) {
    return std::visit(
        [&](const auto& o) -> core::ObjectPtr {
          auto copy = o;
          copy.common.mask = std::make_shared<const core::OpacityMask>(
              core::OpacityMask{std::move(art), clip, invert});
          return core::MakeObject(std::move(copy));
        },
        object->base());
  };
  colors.push_back(masked(b.Group(std::move(fading)), b.Path(Rectangle(180, 530, 270, 50), {fade}),
                          true, false));
  colors.push_back(masked(b.Path(Rectangle(470, 530, 90, 50), {Fill{Rgb(0x3fbf7f)}}),
                          b.Path(Circle(515, 555, 18), {Fill{Rgb(0x000000)}}), false, false));
  colors.push_back(masked(b.Path(Rectangle(60, 530, 90, 50), {Fill{Rgb(0xff5a36)}}),
                          b.Path(Circle(105, 555, 18), {Fill{Rgb(0xffffff)}}), true, true));

  // A sublayer with a rotated group.
  core::Layer rotated;
  rotated.id = "sublayer";
  rotated.name = "Rotated";
  rotated.children = {
      b.Group({b.Path(Star(0, 0, 60, 30, 6),
                      {MakeStroke(Rgb(0x1b1b1b), 2), Fill{core::SwatchRef{"global-pink"}}})},
              [](core::GroupObject& g) {
                g.transform =
                    core::Matrix::Translate(640, 465) * core::Matrix::Rotate(std::numbers::pi / 12);
              })};

  core::Layer artwork;
  artwork.id = "artwork";
  artwork.name = "Artwork";
  for (auto* row : {&strokes, &composition, &colors}) {
    artwork.children.insert(artwork.children.end(), row->begin(), row->end());
  }
  artwork.children.push_back(core::MakeLayer(std::move(rotated)));

  // Must never show up.
  core::Layer hidden;
  hidden.id = "hidden";
  hidden.name = "Hidden";
  hidden.visible = false;
  hidden.children = {b.Path(Rectangle(0, 0, 800, 600), {Fill{Rgb(0xff0000)}})};

  core::Document document;
  document.artboards = {{"artboard", "Artboard 1", core::Rect::FromXYWH(0, 0, 800, 600)}};
  document.swatches = {
      {"spot-teal", "Spot Teal", core::Swatch::Kind::kSpot, core::CmykColor{0.9, 0, 0.45, 0}},
      {"global-pink", "Global Pink", core::Swatch::Kind::kProcess, Rgb(0xff6fae)},
  };
  // A basic palette, like the swatches a new Illustrator document starts with.
  for (auto swatch : core::DefaultSwatches()) document.swatches.push_back(std::move(swatch));
  document.layers = {core::MakeLayer(std::move(artwork)), core::MakeLayer(std::move(hidden))};
  return document;
}

core::Document MakeTestDocument(int path_count, std::uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> channel(40 / 255.0, 230 / 255.0);
  std::uniform_real_distribution<double> width(0.5, 3.0);

  const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(path_count))));
  core::Layer layer;
  layer.id = "layer";
  layer.name = "Layer 1";
  layer.children.reserve(path_count);
  for (int i = 0; i < path_count; ++i) {
    core::PathObject blob;
    blob.common.id = "p" + std::to_string(i);
    blob.path = MakeBlob(rng, (i % columns + 0.5) * kCellSize, (i / columns + 0.5) * kCellSize);
    core::Stroke stroke{core::RgbColor{20 / 255.0, 20 / 255.0, 20 / 255.0}};
    stroke.width = width(rng);
    blob.common.appearance = {stroke,
                              core::Fill{core::RgbColor{channel(rng), channel(rng), channel(rng)}}};
    layer.children.push_back(core::MakeObject(std::move(blob)));
  }

  core::Document document;
  const double side = columns * kCellSize;
  document.artboards = {{"artboard", "Artboard 1", core::Rect::FromXYWH(0, 0, side, side)}};
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

}  // namespace leinwand::render
