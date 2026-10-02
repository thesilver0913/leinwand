# SPDX-License-Identifier: GPL-3.0-or-later
# M0 check 3: can KDDockWidgets (Qt Quick) be made to look like Spectrum 2?
# Prototype only; removed or folded into src/app once the decision is made.
include(FetchContent)

# KDDockWidgets builds against the Qt found through CMAKE_PREFIX_PATH, so
# it is fetched from source instead of vcpkg (which would build its own Qt).
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
                ${CMAKE_CURRENT_SOURCE_DIR}/patches/kddw-indicators.patch
  UPDATE_DISCONNECTED TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(KDDockWidgets)

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Quick)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(leinwand-dock-prototype WIN32 main.cpp)

# The canvas from the main app, to check it inside a docking layout.
qt_add_qml_module(leinwand-dock-prototype
  URI LeinwandCanvas
  SOURCES ${PROJECT_SOURCE_DIR}/src/app/canvas_item.h ${PROJECT_SOURCE_DIR}/src/app/canvas_item.cpp
)

# Plain resources rather than QML_FILES: they import KDDockWidgets' base
# types from qrc, which qmlcachegen cannot resolve at build time.
qt_add_resources(leinwand-dock-prototype dock_qml
  PREFIX /dock
  FILES
    qml/Main.qml
    qml/Spectrum.qml
    qml/qmldir
    qml/SpectrumTitleBar.qml
    qml/SpectrumTabBar.qml
    qml/SpectrumSeparator.qml
    qml/SpectrumGroup.qml
    qml/CentralCanvas.qml
    qml/PanelContent.qml
    qml/SpectrumIndicatorsOverlay.qml
    qml/ClassicIndicator.qml
    qml/SpectrumRubberBand.qml
    qml/SpectrumFloatingWindow.qml
    icons/close.svg
    icons/layers.svg
    icons/properties.svg
)

target_include_directories(leinwand-dock-prototype PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
target_link_libraries(leinwand-dock-prototype PRIVATE
  render Qt6::Gui Qt6::GuiPrivate Qt6::Quick KDAB::kddockwidgets
)
