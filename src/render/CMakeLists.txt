# SPDX-License-Identifier: GPL-3.0-or-later
find_package(unofficial-skia CONFIG REQUIRED)

add_library(render STATIC
  document_renderer.cpp
  skia_path_ops.cpp
  test_document.cpp
  view.cpp
)
target_include_directories(render PUBLIC ${PROJECT_SOURCE_DIR}/src)
# Skia types must not appear in the public interface.
target_link_libraries(render
  PUBLIC core geometry
  PRIVATE unofficial::skia::skia
)

# The GPU canvas: Metal on macOS, Vulkan elsewhere (spec 3).
if(APPLE)
  target_sources(render PRIVATE metal_canvas.cpp)
  target_link_libraries(render PRIVATE "-framework Metal" "-framework Foundation")
else()
  find_package(VulkanHeaders CONFIG REQUIRED)
  find_package(VulkanMemoryAllocator CONFIG REQUIRED)
  target_sources(render PRIVATE vma_allocator.cpp vulkan_canvas.cpp)
  # Vulkan types appear in VulkanCanvas's interface.
  target_link_libraries(render
    PUBLIC Vulkan::Headers
    PRIVATE GPUOpen::VulkanMemoryAllocator
  )
endif()
