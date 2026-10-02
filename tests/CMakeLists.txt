# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Catch2 3 CONFIG REQUIRED)
find_package(unofficial-skia CONFIG REQUIRED)
include(Catch)

add_executable(core_tests
  core/types_test.cpp
  core/document_test.cpp
  core/edit_test.cpp
  core/history_test.cpp
  core/shape_test.cpp
)
target_link_libraries(core_tests PRIVATE core Catch2::Catch2WithMain)
catch_discover_tests(core_tests)

add_executable(geometry_tests
  geometry/bezier_test.cpp
  geometry/hit_test_test.cpp
  geometry/path_edit_test.cpp
)
target_link_libraries(geometry_tests PRIVATE geometry Catch2::Catch2WithMain)
catch_discover_tests(geometry_tests)

add_executable(editor_tests
  editor/editor_test.cpp
  editor/shape_tools_test.cpp
  editor/pen_tool_test.cpp
  editor/direct_selection_test.cpp
  editor/smart_guides_test.cpp
)
target_link_libraries(editor_tests PRIVATE editor Catch2::Catch2WithMain)
catch_discover_tests(editor_tests)

# Rendering runs on Skia's CPU backend so it works on CI machines without a GPU.
add_executable(render_tests
  render/test_document_test.cpp
  skia_features_test.cpp
  render/document_renderer_test.cpp
  render/image_compare.cpp
  render/view_test.cpp
)
target_link_libraries(render_tests PRIVATE render unofficial::skia::skia Catch2::Catch2WithMain)
target_compile_definitions(render_tests PRIVATE
  LEINWAND_TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata"
  LEINWAND_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output"
)
catch_discover_tests(render_tests)
