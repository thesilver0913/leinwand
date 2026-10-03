# SPDX-License-Identifier: GPL-3.0-or-later
# vcpkg's arm64-osx with the oldest macOS Leinwand supports (that of Qt 6.8),
# so the libraries built here run wherever the app does.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 12.0)
