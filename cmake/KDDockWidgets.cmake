# SPDX-License-Identifier: GPL-3.0-or-later
# KDDockWidgets (Qt Quick) for the panels (M0 check 3). It builds against the
# Qt found through CMAKE_PREFIX_PATH, so it is fetched from source instead of
# vcpkg (which would build its own Qt).
include(FetchContent)

set(KDDockWidgets_FRONTENDS "qtquick" CACHE STRING "" FORCE)
set(KDDockWidgets_QT6 ON CACHE BOOL "" FORCE)
set(KDDockWidgets_STATIC ON CACHE BOOL "" FORCE)
set(KDDockWidgets_EXAMPLES OFF CACHE BOOL "" FORCE)
set(KDDockWidgets_TESTS OFF CACHE BOOL "" FORCE)
set(KDDockWidgets_NO_SPDLOG ON CACHE BOOL "" FORCE)
# The patch lets the drop indicators be drawn inside the hovered window (Qt
# Quick on Vulkan on Windows cannot show translucent windows), and lets the
# indicators and rubber band be replaced with our own QML. Candidate upstream.
find_package(Git REQUIRED)
FetchContent_Declare(KDDockWidgets
  GIT_REPOSITORY https://github.com/KDAB/KDDockWidgets.git
  GIT_TAG v2.4.1
  GIT_SHALLOW TRUE
  PATCH_COMMAND ${GIT_EXECUTABLE} checkout -- .
        COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace
                ${CMAKE_CURRENT_LIST_DIR}/patches/kddw-indicators.patch
  UPDATE_DISCONNECTED TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(KDDockWidgets)
