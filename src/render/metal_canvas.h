// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>

#include "core/document.h"
#include "render/document_renderer.h"
#include "render/overlay.h"
#include "render/view.h"

namespace leinwand::render {

// A Metal device and command queue owned by someone else (Qt Quick), as
// id<MTLDevice> and id<MTLCommandQueue>. Skia commits its command buffers to
// the same queue, ahead of Qt's frame, so Qt sees the finished drawing.
struct MetalDevice {
  const void* device = nullptr;
  const void* queue = nullptr;
};

// A texture to draw into: an id<MTLTexture> in RGBA8 with render target use.
struct MetalTarget {
  const void* texture = nullptr;
  int width = 0;
  int height = 0;
};

// The macOS counterpart of VulkanCanvas (spec 3: Metal is added for macOS).
class MetalCanvas {
 public:
  // Returns nullptr and sets *error when Skia cannot use the device.
  static std::unique_ptr<MetalCanvas> Create(const MetalDevice& device, std::string* error);
  ~MetalCanvas();

  // Draws the document and commits the work to the queue. `renderer` keeps
  // its caches between frames.
  bool Draw(DocumentRenderer& renderer, const core::Document& document, const View& view,
            const Overlay& overlay, const MetalTarget& target);

  struct Impl;

 private:
  explicit MetalCanvas(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace leinwand::render
