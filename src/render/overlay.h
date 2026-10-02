// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "core/edit.h"
#include "core/types.h"

namespace leinwand::render {

// Editing feedback drawn over the document (spec 2: overlays are drawn by
// Skia so they track zoom without lag). Document coordinates; sizes of lines
// and handles stay constant in view pixels.
struct Overlay {
  core::IdSet selection;                   // Outlined, with their anchors.
  std::optional<core::Rect> bounding_box;  // Drawn with eight handles.
  std::optional<core::Rect> marquee;
  double pixel_ratio = 1.0;  // Device pixels per view pixel, for line and handle sizes.
};

}  // namespace leinwand::render
