# SPDX-License-Identifier: GPL-3.0-or-later
# OS-specific services behind small interfaces (CLAUDE.md). No Qt, no Skia.
add_library(platform STATIC
  files.cpp
)
target_include_directories(platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
