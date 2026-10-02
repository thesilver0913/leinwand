# SPDX-License-Identifier: GPL-3.0-or-later
# Editing session and tools for the canvas. Part of the app layer but free of
# Qt, so it can be unit-tested.
add_library(editor STATIC
  editor.cpp
)
target_include_directories(editor PUBLIC ${PROJECT_SOURCE_DIR}/src/app)
target_link_libraries(editor PUBLIC core geometry)
