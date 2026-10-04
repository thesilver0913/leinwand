// SPDX-License-Identifier: GPL-3.0-or-later
// Preflight (spec 7.5, "入稿チェック"): what print shops commonly reject,
// listed with the objects concerned. Phase 2 checks what can be told from
// an RGB document; ink checks come with CMYK documents (phase 3).
#pragma once

#include <string>
#include <vector>

#include "core/document.h"

namespace leinwand::editor {

enum class PreflightCheck {
  kLiveText,      // Text not converted to outlines.
  kEmptyText,     // Text objects without characters.
  kStrayPoint,    // Paths of a single anchor.
  kThinStroke,    // Strokes thinner than the minimum (scaled by groups).
  kShortOfBleed,  // Artwork that reaches the trim but stops short of the bleed.
  kHidden,        // Hidden objects (or in hidden layers).
  kLocked,        // Locked objects (or in locked layers).
};
inline constexpr int kPreflightCheckCount = 7;

struct PreflightSettings {
  double min_stroke = 0.1 * 72.0 / 25.4;  // 0.1 mm.
  // The bleed asked for when an artboard has none: 3 mm.
  double bleed = 3.0 * 72.0 / 25.4;
  bool enabled[kPreflightCheckCount] = {true, true, true, true, true, true, true};
};

struct PreflightIssue {
  PreflightCheck check;
  std::vector<std::string> ids;  // The objects concerned, in document order.
};

// One entry per check that found something, in the order of the checks.
std::vector<PreflightIssue> Preflight(const core::Document& document,
                                      const PreflightSettings& settings);

}  // namespace leinwand::editor
