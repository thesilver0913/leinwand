# SPDX-License-Identifier: GPL-3.0-or-later
# The document model. Depends on the standard library only (no Qt, no Skia).
add_library(core STATIC
  cover.cpp
  document.cpp
  edit.cpp
  layers.cpp
  number.cpp
  history.cpp
  object.cpp
  shape.cpp
  style.cpp
  id.cpp
  transform.cpp
)
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/src)
