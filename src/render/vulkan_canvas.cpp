// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/vulkan_canvas.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/ganesh/vk/GrVkTypes.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"
#include "render/test_scene_impl.h"

namespace leinwand::render {

struct VulkanCanvas::Impl {
  std::uint32_t queue_family_index = 0;
  skgpu::VulkanExtensions extensions;
  sk_sp<GrDirectContext> context;
};

VulkanCanvas::VulkanCanvas(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

VulkanCanvas::~VulkanCanvas() {
  if (impl_->context) {
    // The device belongs to Qt, so wait for our work before letting go of it.
    impl_->context->flushAndSubmit(GrSyncCpu::kYes);
  }
}

std::unique_ptr<VulkanCanvas> VulkanCanvas::Create(const VulkanDevice& device,
                                                   std::string* error) {
  if (!device.get_instance_proc_addr) {
    *error = "vkGetInstanceProcAddr is missing";
    return nullptr;
  }
  const auto get_instance_proc = device.get_instance_proc_addr;
  const auto get_device_proc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
      get_instance_proc(device.instance, "vkGetDeviceProcAddr"));
  skgpu::VulkanGetProc get_proc = [=](const char* name, VkInstance instance,
                                      VkDevice dev) -> PFN_vkVoidFunction {
    if (dev != VK_NULL_HANDLE) return get_device_proc(dev, name);
    return get_instance_proc(instance, name);
  };

  auto impl = std::make_unique<Impl>();
  impl->queue_family_index = device.queue_family_index;
  // Qt enabled the extensions it wanted; Skia is told about none of them.
  impl->extensions.init(get_proc, device.instance, device.physical_device, 0, nullptr, 0,
                        nullptr);

  skgpu::VulkanBackendContext backend;
  backend.fInstance = device.instance;
  backend.fPhysicalDevice = device.physical_device;
  backend.fDevice = device.device;
  backend.fQueue = device.queue;
  backend.fGraphicsQueueIndex = device.queue_family_index;
  backend.fMaxAPIVersion = device.api_version;
  backend.fVkExtensions = &impl->extensions;
  backend.fGetProc = get_proc;

  impl->context = GrDirectContexts::MakeVulkan(backend);
  if (!impl->context) {
    *error = "GrDirectContexts::MakeVulkan failed";
    return nullptr;
  }
  return std::unique_ptr<VulkanCanvas>(new VulkanCanvas(std::move(impl)));
}

bool VulkanCanvas::Draw(const TestScene& scene, const View& view, const VulkanTarget& target,
                        VkImageLayout final_layout) {
  GrVkImageInfo info;
  info.fImage = target.image;
  info.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
  info.fImageLayout = target.layout;
  info.fFormat = target.format;
  info.fImageUsageFlags = target.usage;
  info.fSampleCount = 1;
  info.fLevelCount = 1;
  info.fCurrentQueueFamily = impl_->queue_family_index;

  // Wrapped every frame so Skia always starts from the layout Qt reports.
  const GrBackendTexture texture = GrBackendTextures::MakeVk(target.width, target.height, info);
  sk_sp<SkSurface> surface =
      SkSurfaces::WrapBackendTexture(impl_->context.get(), texture, kTopLeft_GrSurfaceOrigin, 1,
                                     kRGBA_8888_SkColorType, nullptr, nullptr);
  if (!surface) return false;

  scene.impl().Draw(surface->getCanvas(), view);

  const skgpu::MutableTextureState state =
      skgpu::MutableTextureStates::MakeVulkan(final_layout, impl_->queue_family_index);
  impl_->context->flush(surface.get(), GrFlushInfo{}, &state);
  impl_->context->submit();
  return true;
}

}  // namespace leinwand::render
