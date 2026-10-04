// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to render: may use Skia types.
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "core/document.h"
#include "core/types.h"
#include "include/core/SkPath.h"
#include "render/document_renderer.h"

class SkCanvas;

namespace leinwand::render {

struct DocumentRenderer::Impl {
  explicit Impl(RenderSettings s) : settings(s) {}

  // Draws the whole document onto `canvas`, whose pixels map to `view`.
  void Draw(SkCanvas* canvas, const core::Document& document, const View& view, int width,
            int height, const Overlay* overlay = nullptr);

  // The layers alone (no pasteboard, nothing cleared), for PDF pages.
  void DrawArtwork(SkCanvas* canvas, const core::Document& document, const core::Rect& visible);

  RenderSettings settings;
  Stats stats;
  // Text as glyphs (PDF) rather than as outline paths.
  bool glyph_text = false;

 private:
  struct CacheEntry {
    std::weak_ptr<const core::Object> owner;  // Detects a reused address.
    SkPath path;                              // Empty for groups.
    core::Rect bounds;  // Painted bounds (strokes included), parent coordinates.
    std::uint64_t last_used = 0;
  };

  const CacheEntry& Entry(const core::ObjectPtr& object);
  void DrawLayer(SkCanvas* canvas, const core::Layer& layer, const core::Rect& visible);
  void DrawObject(SkCanvas* canvas, const core::ObjectPtr& object, const core::Rect& visible);
  void DrawShape(SkCanvas* canvas, const core::Object& object, const SkPath& path);
  void DrawText(SkCanvas* canvas, const core::TextObject& text);
  void DrawMask(SkCanvas* canvas, const core::OpacityMask& mask, const SkRect& bounds,
                const core::Rect& visible);
  void DrawOverlay(SkCanvas* canvas, const core::Document& document, const Overlay& overlay,
                   float px);
  void DrawOutline(SkCanvas* canvas, const core::ObjectPtr& object, float anchor_half);
  void DrawEditedPath(SkCanvas* canvas, const EditedPath& edited, float px, float anchor_half);
  void PruneCache();

  const core::Document* document_ = nullptr;
  bool outline_ = false;  // Outline view: paths as hairlines, no paint.
  std::unordered_map<const core::Object*, CacheEntry> cache_;
  std::uint64_t frame_ = 0;
};

}  // namespace leinwand::render
