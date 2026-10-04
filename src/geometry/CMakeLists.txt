# SPDX-License-Identifier: GPL-3.0-or-later
# Bézier math and the path-operation interface. Depends on core, and on the
# text engine for the outlines and bounds of text.
add_library(geometry STATIC
  bezier.cpp
  hit_test.cpp
  path_edit.cpp
  path_ops.cpp
  pathfinder.cpp
)
target_link_libraries(geometry PUBLIC core text)
