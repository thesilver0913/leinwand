// SPDX-License-Identifier: GPL-3.0-or-later
// The allocation rules follow Skia's VulkanAMDMemoryAllocator (BSD-3-Clause).
#include "render/vma_allocator.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace leinwand::render {

namespace {

using skgpu::VulkanAlloc;
using skgpu::VulkanBackendMemory;

class SkiaVmaAllocator final : public skgpu::VulkanMemoryAllocator {
 public:
  explicit SkiaVmaAllocator(::VmaAllocator allocator) : allocator_(allocator) {}
  ~SkiaVmaAllocator() override { vmaDestroyAllocator(allocator_); }

  VkResult allocateImageMemory(VkImage image, uint32_t flags,
                               VulkanBackendMemory* memory) override {
    VmaAllocationCreateInfo info = {};
    info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (flags & kDedicatedAllocation_AllocationPropertyFlag) {
      info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    if (flags & kLazyAllocation_AllocationPropertyFlag) {
      info.requiredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }
    if (flags & kProtected_AllocationPropertyFlag) {
      info.requiredFlags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
    }
    VmaAllocation allocation;
    const VkResult result =
        vmaAllocateMemoryForImage(allocator_, image, &info, &allocation, nullptr);
    if (result == VK_SUCCESS) *memory = reinterpret_cast<VulkanBackendMemory>(allocation);
    return result;
  }

  VkResult allocateBufferMemory(VkBuffer buffer, BufferUsage usage, uint32_t flags,
                                VulkanBackendMemory* memory) override {
    VmaAllocationCreateInfo info = {};
    switch (usage) {
      case BufferUsage::kGpuOnly:
        info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
      case BufferUsage::kCpuWritesGpuReads:
        info.requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
      case BufferUsage::kTransfersFromCpuToGpu:
        info.requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
      case BufferUsage::kTransfersFromGpuToCpu:
        info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    }
    if (flags & kDedicatedAllocation_AllocationPropertyFlag) {
      info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    if ((flags & kLazyAllocation_AllocationPropertyFlag) && usage == BufferUsage::kGpuOnly) {
      info.preferredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }
    if (flags & kPersistentlyMapped_AllocationPropertyFlag) {
      info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    if (flags & kProtected_AllocationPropertyFlag) {
      info.requiredFlags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
    }
    VmaAllocation allocation;
    const VkResult result =
        vmaAllocateMemoryForBuffer(allocator_, buffer, &info, &allocation, nullptr);
    if (result == VK_SUCCESS) *memory = reinterpret_cast<VulkanBackendMemory>(allocation);
    return result;
  }

  void getAllocInfo(const VulkanBackendMemory& memory, VulkanAlloc* alloc) const override {
    VmaAllocationInfo info;
    vmaGetAllocationInfo(allocator_, ToVma(memory), &info);
    VkMemoryPropertyFlags properties;
    vmaGetMemoryTypeProperties(allocator_, info.memoryType, &properties);

    uint32_t flags = 0;
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) flags |= VulkanAlloc::kMappable_Flag;
    if (!(properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      flags |= VulkanAlloc::kNoncoherent_Flag;
    }
    if (properties & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
      flags |= VulkanAlloc::kLazilyAllocated_Flag;
    }
    alloc->fMemory = info.deviceMemory;
    alloc->fOffset = info.offset;
    alloc->fSize = info.size;
    alloc->fFlags = flags;
    alloc->fBackendMemory = memory;
  }

  VkResult mapMemory(const VulkanBackendMemory& memory, void** data) override {
    return vmaMapMemory(allocator_, ToVma(memory), data);
  }

  void unmapMemory(const VulkanBackendMemory& memory) override {
    vmaUnmapMemory(allocator_, ToVma(memory));
  }

  VkResult flushMemory(const VulkanBackendMemory& memory, VkDeviceSize offset,
                       VkDeviceSize size) override {
    return vmaFlushAllocation(allocator_, ToVma(memory), offset, size);
  }

  VkResult invalidateMemory(const VulkanBackendMemory& memory, VkDeviceSize offset,
                            VkDeviceSize size) override {
    return vmaInvalidateAllocation(allocator_, ToVma(memory), offset, size);
  }

  void freeMemory(const VulkanBackendMemory& memory) override {
    vmaFreeMemory(allocator_, ToVma(memory));
  }

  std::pair<uint64_t, uint64_t> totalAllocatedAndUsedMemory() const override {
    VmaTotalStatistics stats;
    vmaCalculateStatistics(allocator_, &stats);
    return {stats.total.statistics.blockBytes, stats.total.statistics.allocationBytes};
  }

 private:
  static VmaAllocation ToVma(const VulkanBackendMemory& memory) {
    return reinterpret_cast<VmaAllocation>(memory);
  }

  ::VmaAllocator allocator_;
};

}  // namespace

sk_sp<skgpu::VulkanMemoryAllocator> MakeVmaAllocator(VkInstance instance,
                                                     VkPhysicalDevice physical_device,
                                                     VkDevice device,
                                                     PFN_vkGetInstanceProcAddr get_instance_proc) {
  VmaVulkanFunctions functions = {};
  functions.vkGetInstanceProcAddr = get_instance_proc;
  functions.vkGetDeviceProcAddr =
      reinterpret_cast<PFN_vkGetDeviceProcAddr>(get_instance_proc(instance, "vkGetDeviceProcAddr"));

  VmaAllocatorCreateInfo info = {};
  // Skia only touches the allocator from the render thread.
  info.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
  info.physicalDevice = physical_device;
  info.device = device;
  info.instance = instance;
  info.preferredLargeHeapBlockSize = 4 * 1024 * 1024;  // Skia's choice.
  info.pVulkanFunctions = &functions;
  info.vulkanApiVersion = VK_API_VERSION_1_1;

  ::VmaAllocator allocator;
  if (vmaCreateAllocator(&info, &allocator) != VK_SUCCESS) return nullptr;
  return sk_make_sp<SkiaVmaAllocator>(allocator);
}

}  // namespace leinwand::render
