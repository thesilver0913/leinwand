// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace leinwand::render {

// Maps document points to target pixels: pixel = point * zoom + pan.
struct View {
  double pan_x = 0.0;
  double pan_y = 0.0;
  double zoom = 1.0;
};

}  // namespace leinwand::render
