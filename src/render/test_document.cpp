// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/test_document.h"

#include <cmath>
#include <numbers>
#include <random>
#include <string>
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

}  // namespace

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
