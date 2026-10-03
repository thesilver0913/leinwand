# SPDX-License-Identifier: GPL-3.0-or-later
# Editing session and tools for the canvas. Part of the app layer but free of
# Qt, so it can be unit-tested.
add_library(editor STATIC
  editor.cpp
  editor_align.cpp
  editor_artboards.cpp
  editor_gradient.cpp
  editor_text.cpp
  editor_transparency.cpp
  editor_paths.cpp
  editor_pathfinder.cpp
  editor_snap.cpp
  editor_style.cpp
  number_input.cpp
)
target_include_directories(editor PUBLIC ${PROJECT_SOURCE_DIR}/src/app)
target_link_libraries(editor PUBLIC core geometry)
