// SPDX-License-Identifier: GPL-3.0-or-later
// Finding objects under a point or inside a marquee, for the selection tools.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/document.h"
#include "core/edit.h"
#include "core/path.h"
#include "core/types.h"

namespace leinwand::geometry {

// Approximates a path with line segments no further than `tolerance` from
// the curve. Each subpath is one polyline; closed ones repeat the start.
std::vector<core::Point> Flatten(const core::PathData& path, double tolerance);

// Point-in-fill for a set of subpaths under the given rule.
bool FillContains(const std::vector<core::PathData>& subpaths, core::FillRule rule, core::Point p,
                  double tolerance);

// Distance from `p` to the path's outline.
double DistanceToOutline(const core::PathData& path, core::Point p, double tolerance);

struct Hit {
  std::string top_level_id;  // The object directly in a layer: what the selection tool selects.
  std::string leaf_id;       // The innermost path that was hit.
};

// The frontmost visible, unlocked object under `p` (document points).
// `tolerance` is the pick radius in document points; it is added to half the
// stroke width when testing outlines.
std::optional<Hit> HitTest(const core::Document& document, core::Point p, double tolerance);

// Top-level ids of visible, unlocked objects that the rectangle touches:
// part of the outline inside it, or the rectangle inside the fill.
core::IdSet ObjectsTouching(const core::Document& document, const core::Rect& rect,
                            double tolerance);

}  // namespace leinwand::geometry
