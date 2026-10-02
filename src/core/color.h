// SPDX-License-Identifier: GPL-3.0-or-later
// Colors keep their own color space and components (spec 3, "カラー"); they
// are never stored only as converted RGB. Components are 0..1.
#pragma once

#include <string>
#include <variant>

namespace leinwand::core {

struct RgbColor {
  double r = 0.0, g = 0.0, b = 0.0;
  friend bool operator==(const RgbColor&, const RgbColor&) = default;
};

struct CmykColor {
  double c = 0.0, m = 0.0, y = 0.0, k = 0.0;
  friend bool operator==(const CmykColor&, const CmykColor&) = default;
};

struct GrayColor {
  double gray = 0.0;  // 0 is black, 1 is white.
  friend bool operator==(const GrayColor&, const GrayColor&) = default;
};

// A tint of a spot color swatch.
struct SpotColor {
  std::string swatch_id;
  double tint = 1.0;
  friend bool operator==(const SpotColor&, const SpotColor&) = default;
};

// A global color: follows the swatch when the swatch changes.
struct SwatchRef {
  std::string swatch_id;
  friend bool operator==(const SwatchRef&, const SwatchRef&) = default;
};

using Color = std::variant<RgbColor, CmykColor, GrayColor, SpotColor, SwatchRef>;

// A process color that a swatch can hold (no references to other swatches).
using ProcessColor = std::variant<RgbColor, CmykColor, GrayColor>;

struct Swatch {
  enum class Kind {
    kProcess,  // Plain or global process color.
    kSpot,     // Spot color; `color` is its alternate (CMYK or Lab in print).
  };
  std::string id;
  std::string name;
  Kind kind = Kind::kProcess;
  ProcessColor color;
  friend bool operator==(const Swatch&, const Swatch&) = default;
};

}  // namespace leinwand::core
