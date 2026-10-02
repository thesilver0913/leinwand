# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Quick QuickControls2 Svg)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(leinwand WIN32
  main.cpp
)
qt_add_qml_module(leinwand
  URI Leinwand
  QML_FILES
    qml/Main.qml
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
    qml/panels/LayersPanel.qml
    qml/panels/PropertiesPanel.qml
    qml/panels/StrokePanel.qml
    qml/panels/SwatchesPanel.qml
    qml/panels/TransformFields.qml
    qml/panels/TransformPanel.qml
  SOURCES
    canvas_item.h canvas_item.cpp
    icon_provider.h icon_provider.cpp
    layers_model.h layers_model.cpp
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
  editor render Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::QuickControls2 Qt6::Svg
  KDAB::kddockwidgets
)
