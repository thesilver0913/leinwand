# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

project(Leinwand VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The app needs Qt; the render library and its tests do not.
option(LEINWAND_BUILD_APP "Build the Qt Quick application" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/geometry)
add_subdirectory(src/render)
add_subdirectory(src/app/editor)
if(LEINWAND_BUILD_APP)
  add_subdirectory(tools/spectrum_theme)
  include(cmake/KDDockWidgets.cmake)
  add_subdirectory(src/app)
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
