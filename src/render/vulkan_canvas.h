// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>
#include <string>

#include "render/test_scene.h"

namespace leinwand::render {

// A Vulkan device owned by someone else (Qt Quick). Skia draws with it
// directly, so Skia and the scene graph share one VkDevice and VkQueue.
struct VulkanDevice {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t queue_family_index = 0;
  std::uint32_t api_version = VK_API_VERSION_1_1;
  PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
};

// An image to draw into, in its current state.
struct VulkanTarget {
  VkImage image = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  VkImageUsageFlags usage = 0;
  int width = 0;
  int height = 0;
};

class VulkanCanvas {
 public:
  // Returns nullptr and sets *error when Skia cannot use the device.
  static std::unique_ptr<VulkanCanvas> Create(const VulkanDevice& device, std::string* error);
  ~VulkanCanvas();

  // Draws the scene, submits to the queue and leaves the image in final_layout.
  bool Draw(const TestScene& scene, const View& view, const VulkanTarget& target,
            VkImageLayout final_layout);

  struct Impl;

 private:
  explicit VulkanCanvas(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace leinwand::render
