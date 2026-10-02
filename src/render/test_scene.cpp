// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/test_scene.h"

#include <cmath>
#include <numbers>
#include <random>

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkSurface.h"
#include "render/test_scene_impl.h"

namespace leinwand::render {

namespace {

// Paths are laid out on a square grid, one blob per cell.
constexpr float kCellSize = 60.0f;

SkPath MakeBlob(std::mt19937& rng, float cx, float cy) {
  std::uniform_int_distribution<int> anchor_count(3, 7);
  std::uniform_real_distribution<float> radius(10.0f, 28.0f);
  std::uniform_real_distribution<float> jitter(-0.3f, 0.3f);

  const int n = anchor_count(rng);
  std::vector<SkPoint> anchors(n);
  for (int i = 0; i < n; ++i) {
    const float angle = (i + jitter(rng)) * 2.0f * std::numbers::pi_v<float> / n;
    const float r = radius(rng);
    anchors[i] = {cx + r * std::cos(angle), cy + r * std::sin(angle)};
  }

  // Catmull-Rom style handles give a smooth closed curve through the anchors.
  SkPathBuilder builder;
  builder.moveTo(anchors[0]);
  for (int i = 0; i < n; ++i) {
    const SkPoint p0 = anchors[(i + n - 1) % n];
    const SkPoint p1 = anchors[i];
    const SkPoint p2 = anchors[(i + 1) % n];
    const SkPoint p3 = anchors[(i + 2) % n];
    const SkPoint c1 = p1 + (p2 - p0) * (1.0f / 6.0f);
    const SkPoint c2 = p2 - (p3 - p1) * (1.0f / 6.0f);
    builder.cubicTo(c1, c2, p2);
  }
  builder.close();
  return builder.detach();
}

}  // namespace

TestScene::TestScene(int path_count, std::uint32_t seed) : impl_(std::make_unique<Impl>()) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> channel(40, 230);
  std::uniform_real_distribution<float> width(0.5f, 3.0f);

  const int columns = static_cast<int>(std::ceil(std::sqrt(path_count)));
  impl_->items.reserve(path_count);
  for (int i = 0; i < path_count; ++i) {
    const float cx = (i % columns + 0.5f) * kCellSize;
    const float cy = (i / columns + 0.5f) * kCellSize;
    impl_->items.push_back({
        MakeBlob(rng, cx, cy),
        SkColorSetRGB(channel(rng), channel(rng), channel(rng)),
        SkColorSetRGB(20, 20, 20),
        width(rng),
    });
  }
}

TestScene::~TestScene() = default;

int TestScene::path_count() const { return static_cast<int>(impl_->items.size()); }

std::vector<std::uint8_t> TestScene::RenderRaster(int width, int height, const View& view) const {
  const SkImageInfo info =
      SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
  sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
  impl_->Draw(surface->getCanvas(), view);

  std::vector<std::uint8_t> pixels(info.computeMinByteSize());
  surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0);
  return pixels;
}

void TestScene::Impl::Draw(SkCanvas* canvas, const View& view) const {
  canvas->clear(SkColorSetRGB(0x53, 0x53, 0x53));
  canvas->save();
  canvas->translate(static_cast<float>(view.pan_x), static_cast<float>(view.pan_y));
  canvas->scale(static_cast<float>(view.zoom), static_cast<float>(view.zoom));

  // Deliberately no culling: M0 measures the naive cost first.
  SkPaint fill;
  fill.setAntiAlias(true);
  SkPaint stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(SkPaint::kStroke_Style);
  for (const Item& item : items) {
    fill.setColor(item.fill);
    canvas->drawPath(item.path, fill);
    stroke.setColor(item.stroke);
    stroke.setStrokeWidth(item.stroke_width);
    canvas->drawPath(item.path, stroke);
  }
  canvas->restore();
}

}  // namespace leinwand::render
