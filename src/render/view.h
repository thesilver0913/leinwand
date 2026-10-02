// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/types.h"

namespace leinwand::render {

// Maps document points to target pixels: pixel = point * zoom + pan.
struct View {
  double pan_x = 0.0;
  double pan_y = 0.0;
  double zoom = 1.0;

  // Illustrator's zoom range: 3.13% to 64000%.
  static constexpr double kMinZoom = 0.0313;
  static constexpr double kMaxZoom = 640.0;

  core::Point ToView(core::Point doc) const { return {doc.x * zoom + pan_x, doc.y * zoom + pan_y}; }
  core::Point ToDocument(core::Point view) const {
    return {(view.x - pan_x) / zoom, (view.y - pan_y) / zoom};
  }

  // Zooms by `factor`, keeping the document point under `anchor` (view
  // pixels) in place. The result stays within [kMinZoom, kMaxZoom].
  View ZoomedAt(core::Point anchor, double factor) const;

  // The view that shows `rect` centred in a viewport of the given size, with
  // `margin` view pixels on each side.
  static View Fit(const core::Rect& rect, double viewport_width, double viewport_height,
                  double margin = 20.0);

  // Zoom steps for Ctrl+= and Ctrl+-, through preset levels.
  static double NextZoomIn(double zoom);
  static double NextZoomOut(double zoom);
};

}  // namespace leinwand::render
