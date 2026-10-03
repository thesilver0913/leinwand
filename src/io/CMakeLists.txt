# SPDX-License-Identifier: GPL-3.0-or-later
# File formats (spec 3.1-3.3, 6): .lwd and SVG. No Qt, no Skia.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

add_library(io STATIC
  import_report.cpp
  lwd_file.cpp
  lwd_json.cpp
  svg_export.cpp
  svg_import.cpp
  svg_syntax.cpp
)
target_include_directories(io PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(io
  PUBLIC core geometry
  PRIVATE nlohmann_json::nlohmann_json miniz::miniz pugixml::pugixml
)
