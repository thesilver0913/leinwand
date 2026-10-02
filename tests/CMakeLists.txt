# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Catch2 3 CONFIG REQUIRED)
find_package(unofficial-skia CONFIG REQUIRED)

add_executable(render_tests
  test_scene_test.cpp
  skia_features_test.cpp
)
target_link_libraries(render_tests PRIVATE render unofficial::skia::skia Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(render_tests)
