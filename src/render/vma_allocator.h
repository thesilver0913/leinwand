// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to render: may use Skia types.
#pragma once

#include <vulkan/vulkan_core.h>

#include "include/core/SkRefCnt.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"

namespace leinwand::render {

// vcpkg's Skia is built without its internal VMA allocator, so Ganesh needs
// one supplied through VulkanBackendContext::fMemoryAllocator.
sk_sp<skgpu::VulkanMemoryAllocator> MakeVmaAllocator(VkInstance instance,
                                                     VkPhysicalDevice physical_device,
                                                     VkDevice device,
                                                     PFN_vkGetInstanceProcAddr get_instance_proc);

}  // namespace leinwand::render
