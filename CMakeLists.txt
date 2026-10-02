# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

project(Leinwand VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The app needs Qt; the render library and its tests do not.
option(LEINWAND_BUILD_APP "Build the Qt Quick application" ON)
option(LEINWAND_BUILD_PROTOTYPES "Build the M0 verification prototypes" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/render)
if(LEINWAND_BUILD_APP)
  add_subdirectory(src/app)
  if(LEINWAND_BUILD_PROTOTYPES)
    add_subdirectory(prototypes/dock)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
