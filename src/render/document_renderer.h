// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/color.h"
#include "core/document.h"
#include "render/overlay.h"
#include "render/view.h"

namespace leinwand::render {

struct RenderSettings {
  // The pasteboard around the artboards; set independently of the UI theme.
  core::RgbColor pasteboard{0x53 / 255.0, 0x53 / 255.0, 0x53 / 255.0};
  // Export: the artwork alone, on white or (when `transparent`) on nothing,
  // without the pasteboard and artboard frames.
  bool artwork_only = false;
  bool transparent = false;
};

// Draws documents. Keeps a cache of converted paths keyed by object
// identity, which is safe because objects are immutable: an edited object is
// a new object. One renderer per target; not thread-safe.
class DocumentRenderer {
 public:
  explicit DocumentRenderer(RenderSettings settings = {});
  ~DocumentRenderer();
  DocumentRenderer(const DocumentRenderer&) = delete;
  DocumentRenderer& operator=(const DocumentRenderer&) = delete;

  // The pasteboard color (preferences: the canvas background).
  void SetPasteboard(core::RgbColor color);

  // Renders on the CPU into tightly packed premultiplied RGBA8 pixels.
  std::vector<std::uint8_t> RenderRaster(const core::Document& document, int width, int height,
                                         const View& view, const Overlay* overlay = nullptr);

  // PNG of the document's `area` (document points) at `scale` pixels per
  // point (spec 6, "PNG"): the artwork only, on white or transparent.
  // Empty if the area is empty or too large.
  static std::vector<std::uint8_t> ExportPng(const core::Document& document, const core::Rect& area,
                                             double scale, bool transparent);

  struct Stats {
    int drawn = 0;   // Objects painted in the last frame (groups count once).
    int culled = 0;  // Objects skipped because they were off screen.
  };
  Stats last_stats() const;

  struct Impl;
  Impl& impl() { return *impl_; }

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace leinwand::render
