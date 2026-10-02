# SPDX-License-Identifier: GPL-3.0-or-later
# Build-time converter from the Spectrum token JSON to the app's theme table.
find_package(nlohmann_json CONFIG REQUIRED)
add_executable(spectrum_theme main.cpp)
target_link_libraries(spectrum_theme PRIVATE nlohmann_json::nlohmann_json)
