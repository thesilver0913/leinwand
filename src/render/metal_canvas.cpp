// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/metal_canvas.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendSurface.h"
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
#include "include/gpu/ganesh/mtl/GrMtlTypes.h"
#include "include/ports/SkCFObject.h"
#include "render/document_renderer_impl.h"

namespace leinwand::render {

struct MetalCanvas::Impl {
  sk_sp<GrDirectContext> context;
};

MetalCanvas::MetalCanvas(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

MetalCanvas::~MetalCanvas() {
  if (impl_->context) {
    // The device belongs to Qt, so wait for our work before letting go of it.
    impl_->context->flushAndSubmit(GrSyncCpu::kYes);
  }
}

std::unique_ptr<MetalCanvas> MetalCanvas::Create(const MetalDevice& device, std::string* error) {
  if (!device.device || !device.queue) {
    *error = "The Metal device or command queue is missing";
    return nullptr;
  }
  // Skia retains the objects for as long as it uses them.
  GrMtlBackendContext backend;
  backend.fDevice = sk_ret_cfp<GrMTLHandle>(device.device);
  backend.fQueue = sk_ret_cfp<GrMTLHandle>(device.queue);

  auto impl = std::make_unique<Impl>();
  impl->context = GrDirectContexts::MakeMetal(backend);
  if (!impl->context) {
    *error = "GrDirectContexts::MakeMetal failed";
    return nullptr;
  }
  return std::unique_ptr<MetalCanvas>(new MetalCanvas(std::move(impl)));
}

bool MetalCanvas::Draw(DocumentRenderer& renderer, const core::Document& document, const View& view,
                       const Overlay& overlay, const MetalTarget& target) {
  GrMtlTextureInfo info;
  info.fTexture = sk_ret_cfp<GrMTLHandle>(target.texture);
  const GrBackendTexture texture =
      GrBackendTextures::MakeMtl(target.width, target.height, skgpu::Mipmapped::kNo, info);
  sk_sp<SkSurface> surface =
      SkSurfaces::WrapBackendTexture(impl_->context.get(), texture, kTopLeft_GrSurfaceOrigin, 1,
                                     kRGBA_8888_SkColorType, nullptr, nullptr);
  if (!surface) return false;

  renderer.impl().Draw(surface->getCanvas(), document, view, target.width, target.height, &overlay);

  impl_->context->flush(surface.get(), GrFlushInfo{}, nullptr);
  impl_->context->submit();
  return true;
}

}  // namespace leinwand::render
