// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/color.h"
#include "core/document.h"
#include "core/marks.h"
#include "render/overlay.h"
#include "render/view.h"

namespace leinwand::render {

// PDF export (spec 6.2, "書き出しの設定"): one page per artboard.
struct PdfOptions {
  std::vector<int> artboards;  // In page order; empty: every artboard.
  // Text as glyphs with the font embedded (TrueType; CFF fonts come out as
  // Type 3, M8), or as outlines.
  bool outline_text = false;
  // Trim marks around each artboard, with the bleed (the artboard's, else
  // 3 mm) inside the page.
  std::optional<core::TrimMarkStyle> marks;
  std::string title;
  std::string creator;
};

// A printed or exported page: the document area it shows, the part of it
// with artwork (the artboard, or its bleed with marks), and the marks.
struct Page {
  core::Rect area;
  core::Rect artwork;
  std::vector<core::PathData> marks;  // Lines, drawn 0.3 pt black.
};

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

  // The document as PDF; empty if there is nothing to write.
  static std::vector<std::uint8_t> ExportPdf(const core::Document& document,
                                             const PdfOptions& options);
  // Where each page goes in the document, for options (marks widen pages).
  static core::Rect PdfPageArea(const core::Artboard& artboard, const PdfOptions& options);
  // The page for an artboard, with or without trim marks.
  static Page ArtboardPage(const core::Artboard& artboard,
                           std::optional<core::TrimMarkStyle> marks);

  // Part of a page on white paper, for printing in bands: `width` x
  // `height` pixels starting `top_row` rows down, at `scale` pixels per
  // point. Premultiplied RGBA8; empty if too large.
  std::vector<std::uint8_t> RenderPage(const core::Document& document, const Page& page,
                                       double scale, int top_row, int width, int height);

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
