# SPDX-License-Identifier: GPL-3.0-or-later
find_package(unofficial-skia CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)

add_library(render STATIC
  test_scene.cpp
  vulkan_canvas.cpp
)
target_include_directories(render PUBLIC ${PROJECT_SOURCE_DIR}/src)
# Vulkan types appear in the public interface; Skia types must not.
target_link_libraries(render
  PUBLIC Vulkan::Headers
  PRIVATE unofficial::skia::skia
)
