# SPDX-License-Identifier: GPL-3.0-or-later
# The text engine (spec 5.1): HarfBuzz for shaping and outlines, ICU for
# grapheme clusters and words. Depends on core only (no Qt, no Skia); the OS
# fonts come in through the FontSource interface (render/skia_font_source.h).
find_package(harfbuzz CONFIG REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc data)
add_library(text STATIC
  font.cpp
  layout.cpp
)
target_include_directories(text PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(text
  PUBLIC core
  PRIVATE harfbuzz::harfbuzz ICU::uc ICU::data
)
