// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "core/edit.h"
#include "core/path.h"
#include "core/types.h"

namespace leinwand::render {

// A path shown with its anchors for editing (document coordinates).
struct EditedPath {
  core::PathData path;
  std::set<int> selected;      // Solid anchors; the rest are hollow.
  std::set<int> with_handles;  // Anchors whose handles are drawn.
};

// Editing feedback drawn over the document (spec 2: overlays are drawn by
// Skia so they track zoom without lag). Document coordinates; sizes of lines
// and handles stay constant in view pixels.
struct Overlay {
  core::IdSet selection;                   // Outlined, with their anchors.
  std::optional<core::Rect> bounding_box;  // Drawn with eight handles.
  std::optional<core::Rect> marquee;
  double pixel_ratio = 1.0;       // Device pixels per view pixel, for line and handle sizes.
  double anchor_size = 6.0;       // View pixels across an anchor square (preferences).
  std::vector<EditedPath> paths;  // Pen and direct selection.
  std::optional<core::PathData> rubber_band;                // The pen's next segment.
  std::vector<std::pair<core::Point, core::Point>> guides;  // Smart guides.
  bool outline = false;                  // Outline view (Ctrl+Y): paths only, no paint.
  std::optional<core::Rect> key_object;  // The Align panel's key object, framed thickly.
  // The gradient annotator: from the start (a circle) to the end (a square).
  std::optional<std::pair<core::Point, core::Point>> gradient_line;
};

}  // namespace leinwand::render
