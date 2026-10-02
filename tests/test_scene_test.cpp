// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "render/test_scene.h"

using leinwand::render::TestScene;
using leinwand::render::View;

TEST_CASE("TestScene builds the requested number of paths") {
  CHECK(TestScene(10000).path_count() == 10000);
  CHECK(TestScene(0).path_count() == 0);
}

TEST_CASE("TestScene renders deterministically on the CPU") {
  const View view{0.0, 0.0, 0.5};
  const auto a = TestScene(1000, 7).RenderRaster(256, 256, view);
  const auto b = TestScene(1000, 7).RenderRaster(256, 256, view);
  REQUIRE(a.size() == 256u * 256u * 4u);
  CHECK(a == b);
  CHECK(a != TestScene(1000, 8).RenderRaster(256, 256, view));
}

TEST_CASE("TestScene draws something other than the background") {
  const auto pixels = TestScene(100).RenderRaster(128, 128, View{});
  int differing = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    if (pixels[i] != 0x53 || pixels[i + 1] != 0x53 || pixels[i + 2] != 0x53) ++differing;
  }
  CHECK(differing > 128 * 128 / 4);
}
