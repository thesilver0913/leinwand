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
  core/style_test.cpp
  core/layers_test.cpp
  core/number_test.cpp
  core/cover_test.cpp
  core/gradient_test.cpp
)
target_link_libraries(core_tests PRIVATE core Catch2::Catch2WithMain)
catch_discover_tests(core_tests)

add_executable(geometry_tests
  geometry/bezier_test.cpp
  geometry/hit_test_test.cpp
  geometry/path_edit_test.cpp
  geometry/path_ops_test.cpp
)
target_link_libraries(geometry_tests PRIVATE geometry Catch2::Catch2WithMain)
catch_discover_tests(geometry_tests)

add_executable(editor_tests
  editor/editor_test.cpp
  editor/shape_tools_test.cpp
  editor/pen_tool_test.cpp
  editor/direct_selection_test.cpp
  editor/smart_guides_test.cpp
  editor/style_test.cpp
  editor/number_input_test.cpp
  editor/pathfinder_test.cpp
  editor/align_test.cpp
  editor/artboards_test.cpp
  editor/gradient_test.cpp
  editor/transparency_test.cpp
)
# The pathfinder tests run on render's Skia PathOps engine.
target_link_libraries(editor_tests PRIVATE editor render Catch2::Catch2WithMain)
catch_discover_tests(editor_tests)

# Rendering runs on Skia's CPU backend so it works on CI machines without a GPU.
add_executable(render_tests
  render/test_document_test.cpp
  skia_features_test.cpp
  render/document_renderer_test.cpp
  render/image_compare.cpp
  render/view_test.cpp
  render/skia_path_ops_test.cpp
  render/path_ops_corpus_test.cpp
  render/pathops_probe_test.cpp
  render/text_probe_test.cpp
)
# HarfBuzz and ICU for the text engine probe (M8).
find_package(harfbuzz CONFIG REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc data)
target_link_libraries(render_tests PRIVATE
  render unofficial::skia::skia harfbuzz::harfbuzz ICU::uc ICU::data Catch2::Catch2WithMain
)
target_compile_definitions(render_tests PRIVATE
  LEINWAND_TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata"
  LEINWAND_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output"
  LEINWAND_FONTS_DIR="${PROJECT_SOURCE_DIR}/third_party/fonts"
)
catch_discover_tests(render_tests)

# File formats; SVG results are compared by rendering them.
find_package(nlohmann_json CONFIG REQUIRED)
add_executable(io_tests
  io/lwd_test.cpp
  io/svg_syntax_test.cpp
  io/svg_test.cpp
  render/image_compare.cpp
)
target_include_directories(io_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(io_tests PRIVATE
  io render unofficial::skia::skia nlohmann_json::nlohmann_json Catch2::Catch2WithMain
)
target_compile_definitions(io_tests PRIVATE
  LEINWAND_TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata"
  LEINWAND_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output"
  LEINWAND_FONTS_DIR="${PROJECT_SOURCE_DIR}/third_party/fonts"
)
catch_discover_tests(io_tests)
