// SPDX-License-Identifier: GPL-3.0-or-later
// The appearance stack (spec 3, "アピアランス"): any number of fills and
// strokes, ordered front to back. Effects arrive in phase 3.
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/color.h"
#include "core/types.h"

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

// A color stop of a gradient. `midpoint` (0..1, between this stop and the
// next) is where the two colors mix half and half, as in Illustrator.
struct GradientStop {
  double offset = 0.0;  // 0..1 along the gradient.
  Color color = RgbColor{0, 0, 0};
  double opacity = 1.0;
  double midpoint = 0.5;
  friend bool operator==(const GradientStop&, const GradientStop&) = default;
};

enum class GradientType { kLinear, kRadial };

// A gradient on a fill or stroke (spec 7, "グラデーション"). Its points are
// in the object's path coordinates (its parent's), so they move with the
// path. Gradient swatches arrive later; this is the gradient itself.
struct Gradient {
  GradientType type = GradientType::kLinear;
  std::vector<GradientStop> stops;  // By offset; at least two to be drawn.
  // Linear: from `start` to `end`. Radial: centred on `start`, the radius
  // reaching `end` (the direction gives the ellipse's axis).
  Point start;
  Point end{1, 0};
  double aspect = 1.0;         // Radial: the other axis as a share of the radius.
  std::optional<Point> focal;  // Radial: the highlight; the centre when unset.
  std::string unknown_fields;
  friend bool operator==(const Gradient&, const Gradient&) = default;
};

struct Fill {
  Color paint = RgbColor{0, 0, 0};   // Also shown for a gradient (its swatch in the panels).
  std::optional<Gradient> gradient;  // Painted instead of `paint` when set.
  double opacity = 1.0;
  BlendMode blend_mode = BlendMode::kNormal;
  std::string unknown_fields;  // See ObjectCommon::unknown_fields.
  friend bool operator==(const Fill&, const Fill&) = default;
};

struct Stroke {
  Color paint = RgbColor{0, 0, 0};
  std::optional<Gradient> gradient;  // Painted along the stroke instead of `paint`.
  double width = 1.0;
  StrokeCap cap = StrokeCap::kButt;
  StrokeJoin join = StrokeJoin::kMiter;
  double miter_limit = 10.0;  // Illustrator's default.
  StrokeAlign align = StrokeAlign::kCenter;
  std::vector<double> dashes;  // Alternating dash and gap lengths; empty is solid.
  double dash_offset = 0.0;
  double opacity = 1.0;
  BlendMode blend_mode = BlendMode::kNormal;
  std::string unknown_fields;
  friend bool operator==(const Stroke&, const Stroke&) = default;
};

// An item of a type from a newer file version (e.g. an effect before phase
// 3), kept as JSON text and written back in its place. Not drawn.
struct UnknownAppearanceItem {
  std::string json;
  friend bool operator==(const UnknownAppearanceItem&, const UnknownAppearanceItem&) = default;
};

using AppearanceItem = std::variant<Fill, Stroke, UnknownAppearanceItem>;

// Front to back: items[0] is drawn last.
using Appearance = std::vector<AppearanceItem>;

}  // namespace leinwand::core
