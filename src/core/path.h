// SPDX-License-Identifier: GPL-3.0-or-later
// Path geometry (spec 4.1): cubic Béziers built from anchors.
#pragma once

#include <vector>

#include "core/types.h"

namespace leinwand::core {

enum class AnchorKind {
  kCorner,     // Handles are independent.
  kSmooth,     // Handles stay collinear; lengths are independent.
  kSymmetric,  // Handles stay collinear and equally long.
};

// Handles are offsets from the anchor; zero means no handle. The kind is a
// constraint applied by editing tools, not re-applied to stored values.
struct Anchor {
  Point position;
  Point handle_in;
  Point handle_out;
  AnchorKind kind = AnchorKind::kCorner;

  Point in_point() const { return position + handle_in; }
  Point out_point() const { return position + handle_out; }
  friend bool operator==(const Anchor&, const Anchor&) = default;
};

// Segment i runs from anchors[i] to anchors[i + 1] (and, when closed, from
// the last anchor back to the first). A single anchor is an isolated point.
struct PathData {
  std::vector<Anchor> anchors;
  bool closed = false;

  int segment_count() const {
    const int n = static_cast<int>(anchors.size());
    if (n < 2) return 0;
    return closed ? n : n - 1;
  }
  friend bool operator==(const PathData&, const PathData&) = default;
};

enum class FillRule { kNonZero, kEvenOdd };

}  // namespace leinwand::core
