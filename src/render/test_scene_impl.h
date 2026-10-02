// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to render: may use Skia types.
#pragma once

#include <vector>

#include "include/core/SkColor.h"
#include "include/core/SkPath.h"
#include "render/test_scene.h"

class SkCanvas;

namespace leinwand::render {

struct TestScene::Impl {
  struct Item {
    SkPath path;
    SkColor fill;
    SkColor stroke;
    float stroke_width;
  };
  std::vector<Item> items;

  void Draw(SkCanvas* canvas, const View& view) const;
};

}  // namespace leinwand::render
