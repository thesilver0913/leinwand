# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Quick QuickControls2 Svg LinguistTools)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(leinwand WIN32 MACOSX_BUNDLE
  main.cpp
)
if(WIN32)
  # The program icon and version information (spec 9).
  configure_file(leinwand.rc.in ${CMAKE_CURRENT_BINARY_DIR}/leinwand.rc @ONLY)
  target_sources(leinwand PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/leinwand.rc)
elseif(APPLE)
  # Leinwand.app (spec 9): its description, and the icon made with iconutil
  # from the PNGs tools/make_icons.ps1 renders.
  set(iconset ${CMAKE_CURRENT_BINARY_DIR}/leinwand.iconset)
  set(icns ${CMAKE_CURRENT_BINARY_DIR}/leinwand.icns)
  set(iconset_commands)
  foreach(size 16 32 128 256 512)
    math(EXPR double "${size} * 2")
    list(APPEND iconset_commands
      COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/resources/icons/app/mac-${size}.png
              ${iconset}/icon_${size}x${size}.png
      COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/resources/icons/app/mac-${double}.png
              ${iconset}/icon_${size}x${size}@2x.png)
  endforeach()
  add_custom_command(
    OUTPUT ${icns}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${iconset}
    ${iconset_commands}
    COMMAND iconutil --convert icns --output ${icns} ${iconset}
    DEPENDS ${PROJECT_SOURCE_DIR}/resources/icons/app/mac-1024.png
    COMMENT "Making the macOS app icon"
  )
  target_sources(leinwand PRIVATE ${icns})
  set_source_files_properties(${icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(leinwand PROPERTIES
    OUTPUT_NAME Leinwand
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.thesilver0913.leinwand
    MACOSX_BUNDLE_EXECUTABLE_NAME Leinwand
    MACOSX_BUNDLE_ICON_FILE leinwand.icns
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_COPYRIGHT "© 2026 the Leinwand authors. GPL-3.0-or-later."
  )
endif()
qt_add_qml_module(leinwand
  URI Leinwand
  QML_FILES
    qml/Main.qml
    qml/PreferencesDialog.qml
    qml/ShortcutsDialog.qml
    qml/Splash.qml
    qml/WelcomeScreen.qml
    qml/AppMenuBar.qml
    qml/AverageDialog.qml
    qml/CoverDialog.qml
    qml/CoverForm.qml
    qml/ControlBar.qml
    qml/FillStroke.qml
    qml/StatusBar.qml
    qml/Toolbar.qml
    qml/controls/SpActionButton.qml
    qml/controls/SpCheckBox.qml
    qml/controls/SpIcon.qml
    qml/controls/SpLabel.qml
    qml/controls/SpNumberField.qml
    qml/controls/SpPanel.qml
    qml/controls/SpPicker.qml
    qml/controls/SpSection.qml
    qml/controls/SpSegmented.qml
    qml/controls/SpSlider.qml
    qml/controls/SpSwatch.qml
    qml/controls/SpTextField.qml
    qml/panels/ColorPanel.qml
    qml/panels/ImportReportPanel.qml
    qml/panels/AlignPanel.qml
    qml/panels/ArtboardsPanel.qml
    qml/panels/LayersPanel.qml
    qml/panels/PathfinderPanel.qml
    qml/panels/PropertiesPanel.qml
    qml/panels/StrokePanel.qml
    qml/panels/SwatchesPanel.qml
    qml/panels/TransformFields.qml
    qml/panels/TransformPanel.qml
  SOURCES
    canvas_item.h canvas_item.cpp
    icon_provider.h icon_provider.cpp
    layers_model.h layers_model.cpp
    preferences.h preferences.cpp
    shortcuts.h shortcuts.cpp
    session.h session.cpp
    spectrum_theme.h spectrum_theme.cpp
)

# KDDockWidgets' chrome, replaced with Spectrum-styled QML. Plain resources
# rather than QML_FILES: they import KDDockWidgets' base types from qrc,
# which qmlcachegen cannot resolve at build time.
qt_add_resources(leinwand dock_qml
  PREFIX /dock/qml
  BASE qml/dock
  FILES
    qml/dock/CentralCanvas.qml
    qml/dock/ClassicIndicator.qml
    qml/dock/SpectrumFloatingWindow.qml
    qml/dock/SpectrumGroup.qml
    qml/dock/SpectrumIndicatorsOverlay.qml
    qml/dock/SpectrumRubberBand.qml
    qml/dock/SpectrumSeparator.qml
    qml/dock/SpectrumTabBar.qml
    qml/dock/SpectrumTitleBar.qml
)

# Leinwand's own icons for tools Spectrum has none for (spec 7).
file(GLOB leinwand_icons ${PROJECT_SOURCE_DIR}/resources/icons/*.svg)
qt_add_resources(leinwand leinwand_icons
  PREFIX /icons/leinwand
  BASE ${PROJECT_SOURCE_DIR}/resources/icons
  FILES ${leinwand_icons}
)

# Spectrum 2 tokens, converted at build time (spec 7, "デザイン言語").
set(spectrum_tokens ${PROJECT_SOURCE_DIR}/third_party/spectrum-tokens/variables.json)
set(generated ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
  OUTPUT ${generated}/spectrum_tokens.inc
  COMMAND ${CMAKE_COMMAND} -E make_directory ${generated}
  COMMAND spectrum_theme ${spectrum_tokens} ${generated}/spectrum_tokens.inc
  DEPENDS spectrum_theme ${spectrum_tokens}
  COMMENT "Converting the Spectrum design tokens"
)
target_sources(leinwand PRIVATE ${generated}/spectrum_tokens.inc)
target_include_directories(leinwand PRIVATE ${generated})

# Workflow icons, recolored on load by IconProvider.
file(GLOB spectrum_icons
  ${PROJECT_SOURCE_DIR}/third_party/spectrum-workflow-icons/svg/*.svg)
qt_add_resources(leinwand spectrum_icons
  PREFIX /icons/spectrum
  BASE ${PROJECT_SOURCE_DIR}/third_party/spectrum-workflow-icons/svg
  FILES ${spectrum_icons}
)

# QRhi (rhi/qrhi.h) is only reachable through GuiPrivate in Qt 6.8.
target_link_libraries(leinwand PRIVATE
  editor render io Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::QuickControls2 Qt6::Svg
  KDAB::kddockwidgets
)
target_compile_definitions(leinwand PRIVATE LEINWAND_VERSION="${LEINWAND_VERSION_STRING}")

# The splash artwork and the window icon (spec 9).
file(GLOB app_icons ${PROJECT_SOURCE_DIR}/resources/icons/app/leinwand-*.png)
qt_add_resources(leinwand splash
  PREFIX /
  BASE ${PROJECT_SOURCE_DIR}
  FILES ${PROJECT_SOURCE_DIR}/resources/splash.png ${app_icons}
)

# UI translations (spec 7.3): Japanese and English. `cmake --build --target
# update_translations` refreshes the .ts files from the sources.
qt_add_translations(leinwand
  TS_FILES ${PROJECT_SOURCE_DIR}/i18n/leinwand_ja.ts ${PROJECT_SOURCE_DIR}/i18n/leinwand_en.ts
  PLURALS_TS_FILE ${PROJECT_SOURCE_DIR}/i18n/leinwand_en.ts
  RESOURCE_PREFIX /i18n
)

# The bundled UI fonts (SIL OFL), next to the executable (in the bundle's
# Resources on macOS).
file(GLOB ui_fonts ${PROJECT_SOURCE_DIR}/third_party/fonts/*.otf)
if(APPLE)
  set(fonts_dir $<TARGET_BUNDLE_CONTENT_DIR:leinwand>/Resources/fonts)
else()
  set(fonts_dir $<TARGET_FILE_DIR:leinwand>/fonts)
endif()
add_custom_command(TARGET leinwand POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${fonts_dir}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ui_fonts} ${fonts_dir}
)

# Installation (spec 9): the program with its Qt and vcpkg libraries, the UI
# fonts and the licences. `cmake --install` gives the folder that is zipped
# into the release package (tools/package-windows.ps1).
include(GNUInstallDirs)
install(TARGETS leinwand
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(WIN32)
  # Skia and the other vcpkg DLLs, which vcpkg copies next to the program
  # when it is built (Skia's own dependencies are not CMake targets).
  install(CODE "
    file(GLOB vcpkg_dlls \"$<TARGET_FILE_DIR:leinwand>/*.dll\")
    file(INSTALL \${vcpkg_dlls} DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
  ")
endif()
if(NOT APPLE)  # Already inside the bundle.
  install(FILES ${ui_fonts} DESTINATION ${CMAKE_INSTALL_BINDIR}/fonts)
endif()
install(FILES
  ${PROJECT_SOURCE_DIR}/LICENSE
  ${PROJECT_SOURCE_DIR}/NOTICE.md
  DESTINATION .
)
install(FILES
  ${PROJECT_SOURCE_DIR}/third_party/fonts/LICENSE-SourceSans3.md
  ${PROJECT_SOURCE_DIR}/third_party/fonts/LICENSE-SourceHanSans.txt
  ${PROJECT_SOURCE_DIR}/third_party/spectrum-tokens/LICENSE
  DESTINATION licenses
)
if(WIN32)
  # Drawing is Vulkan only: no software OpenGL or Direct3D shader compiler.
  set(deploy_options --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler
                     --no-compiler-runtime)
endif()
qt_generate_deploy_qml_app_script(
  TARGET leinwand
  OUTPUT_SCRIPT deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR
  DEPLOY_TOOL_OPTIONS ${deploy_options}
)
if(WIN32)
  # The MSVC runtime DLLs next to the program rather than the redistributable
  # installer.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
  include(InstallRequiredSystemLibraries)
endif()
install(SCRIPT ${deploy_script})
