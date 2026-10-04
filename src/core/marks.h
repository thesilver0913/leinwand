// SPDX-License-Identifier: GPL-3.0-or-later
// Trim marks (spec 7.5, "日本式トンボ"): lines around a finished size that
// show where to cut. Japanese marks are double (the trim and the bleed) with
// centre marks; Western ones are single lines set off by the bleed.
#pragma once

#include <vector>

#include "core/path.h"
#include "core/types.h"

namespace leinwand::core {

enum class TrimMarkStyle { kJapanese, kWestern };

// 3 mm, the usual Japanese bleed, used when the artboard has none.
inline constexpr double kDefaultBleed = 3.0 * 72.0 / 25.4;
// 10 mm marks.
inline constexpr double kTrimMarkLength = 10.0 * 72.0 / 25.4;
// Marks are drawn 0.3 pt wide (a common print-shop request).
inline constexpr double kTrimMarkWidth = 0.3;

// Open two-point paths for the marks around `trim`.
std::vector<PathData> TrimMarks(const Rect& trim, double bleed, TrimMarkStyle style,
                                double length = kTrimMarkLength);

// How far the marks reach beyond the trim (page margin they need).
double TrimMarkReach(double bleed, double length = kTrimMarkLength);

}  // namespace leinwand::core
