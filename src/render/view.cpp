// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/view.h"

#include <algorithm>
#include <array>

namespace leinwand::render {

namespace {

// Zoom presets in percent, after Illustrator's (not yet checked against
// Illustrator itself; see CLAUDE.md, 未検証の前提).
constexpr std::array<double, 31> kPresets = {3.13,  4.17,  6.25,  8.33,  12.5,  16.67, 25,   33.33,
                                             50,    66.67, 100,   150,   200,   300,   400,  600,
                                             800,   1200,  1600,  2400,  3200,  4800,  6400, 9600,
                                             12800, 16000, 19200, 25600, 32000, 48000, 64000};

}  // namespace

View View::ZoomedAt(core::Point anchor, double factor) const {
  const double new_zoom = std::clamp(zoom * factor, kMinZoom, kMaxZoom);
  const core::Point doc = ToDocument(anchor);
  return {anchor.x - doc.x * new_zoom, anchor.y - doc.y * new_zoom, new_zoom};
}

View View::Fit(const core::Rect& rect, double viewport_width, double viewport_height,
               double margin) {
  if (!rect.IsValid() || rect.width() <= 0 || rect.height() <= 0) return {};
  const double zoom = std::clamp(std::min((viewport_width - 2 * margin) / rect.width(),
                                          (viewport_height - 2 * margin) / rect.height()),
                                 kMinZoom, kMaxZoom);
  return {(viewport_width - rect.width() * zoom) / 2 - rect.left * zoom,
          (viewport_height - rect.height() * zoom) / 2 - rect.top * zoom, zoom};
}

double View::NextZoomIn(double zoom) {
  const double percent = zoom * 100;
  for (double preset : kPresets) {
    if (preset > percent * 1.001) return preset / 100;
  }
  return kMaxZoom;
}

double View::NextZoomOut(double zoom) {
  const double percent = zoom * 100;
  for (auto it = kPresets.rbegin(); it != kPresets.rend(); ++it) {
    if (*it < percent * 0.999) return *it / 100;
  }
  return kMinZoom;
}

}  // namespace leinwand::render
