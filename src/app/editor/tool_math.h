// SPDX-License-Identifier: GPL-3.0-or-later
// Small helpers shared by the editor's tools.
#pragma once

#include <cmath>
#include <numbers>

#include "core/types.h"

namespace leinwand::editor {

inline double Distance(core::Point a, core::Point b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Shift-drag: the nearest multiple of 45 degrees, projecting the drag onto
// that direction as Illustrator does.
inline core::Point ConstrainTo45(core::Point d) {
  const double length = std::hypot(d.x, d.y);
  if (length == 0) return d;
  const double step = std::numbers::pi / 4;
  const double angle = std::round(std::atan2(d.y, d.x) / step) * step;
  const core::Point dir{std::cos(angle), std::sin(angle)};
  return dir * (d.x * dir.x + d.y * dir.y);
}

}  // namespace leinwand::editor
