// SPDX-License-Identifier: GPL-3.0-or-later
// The appearance stack (spec 3, "アピアランス"): any number of fills and
// strokes, ordered front to back. Effects arrive in phase 3.
#pragma once

#include <variant>
#include <vector>

#include "core/color.h"

namespace leinwand::core {

// Illustrator's blend modes.
enum class BlendMode {
  kNormal,
  kDarken,
  kMultiply,
  kColorBurn,
  kLighten,
  kScreen,
  kColorDodge,
  kOverlay,
  kSoftLight,
  kHardLight,
  kDifference,
  kExclusion,
  kHue,
  kSaturation,
  kColor,
  kLuminosity,
};

enum class StrokeCap { kButt, kRound, kSquare };
enum class StrokeJoin { kMiter, kRound, kBevel };
// Inside and outside apply to closed paths only; open paths always use center.
enum class StrokeAlign { kCenter, kInside, kOutside };

struct Fill {
  Color paint = RgbColor{0, 0, 0};
  double opacity = 1.0;
  BlendMode blend_mode = BlendMode::kNormal;
  friend bool operator==(const Fill&, const Fill&) = default;
};

struct Stroke {
  Color paint = RgbColor{0, 0, 0};
  double width = 1.0;
  StrokeCap cap = StrokeCap::kButt;
  StrokeJoin join = StrokeJoin::kMiter;
  double miter_limit = 10.0;  // Illustrator's default.
  StrokeAlign align = StrokeAlign::kCenter;
  std::vector<double> dashes;  // Alternating dash and gap lengths; empty is solid.
  double dash_offset = 0.0;
  double opacity = 1.0;
  BlendMode blend_mode = BlendMode::kNormal;
  friend bool operator==(const Stroke&, const Stroke&) = default;
};

using AppearanceItem = std::variant<Fill, Stroke>;

// Front to back: items[0] is drawn last.
using Appearance = std::vector<AppearanceItem>;

}  // namespace leinwand::core
