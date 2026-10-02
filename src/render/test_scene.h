// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace leinwand::render {

// Maps document points to target pixels: pixel = point * zoom + pan.
struct View {
  double pan_x = 0.0;
  double pan_y = 0.0;
  double zoom = 1.0;
};

// A synthetic document of random filled and stroked cubic paths, used by the
// M0 performance check. Deterministic for a given seed.
class TestScene {
 public:
  explicit TestScene(int path_count, std::uint32_t seed = 1);
  ~TestScene();
  TestScene(const TestScene&) = delete;
  TestScene& operator=(const TestScene&) = delete;

  int path_count() const;

  // Renders on the CPU into tightly packed premultiplied RGBA8 pixels.
  std::vector<std::uint8_t> RenderRaster(int width, int height, const View& view) const;

  struct Impl;
  const Impl& impl() const { return *impl_; }

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace leinwand::render
