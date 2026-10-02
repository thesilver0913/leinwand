# SPDX-License-Identifier: GPL-3.0-or-later
find_package(unofficial-skia CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_library(render STATIC
  document_renderer.cpp
  test_document.cpp
  view.cpp
  vma_allocator.cpp
  vulkan_canvas.cpp
)
target_include_directories(render PUBLIC ${PROJECT_SOURCE_DIR}/src)
# Vulkan types appear in the public interface; Skia types must not.
target_link_libraries(render
  PUBLIC core geometry Vulkan::Headers
  PRIVATE unofficial::skia::skia GPUOpen::VulkanMemoryAllocator
)
