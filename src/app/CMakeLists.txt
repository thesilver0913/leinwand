# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Quick)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(leinwand WIN32
  main.cpp
)
qt_add_qml_module(leinwand
  URI Leinwand
  QML_FILES qml/Main.qml
  SOURCES canvas_item.h canvas_item.cpp
)
# QRhi (rhi/qrhi.h) is only reachable through GuiPrivate in Qt 6.8.
target_link_libraries(leinwand PRIVATE editor render Qt6::Gui Qt6::GuiPrivate Qt6::Quick)
