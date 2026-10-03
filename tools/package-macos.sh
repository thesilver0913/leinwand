#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the macOS release file (spec 9) from a finished release build:
#   Leinwand-<version>-macos-arm64.dmg   Leinwand.app, with a link to /Applications
#
#   tools/package-macos.sh [build dir (build/macos-release)] [output dir (dist)]
#
# The app is signed ad hoc (Apple Silicon runs nothing unsigned) but not with
# a Developer ID or notarized, so Gatekeeper blocks it on first open; the
# release notes say how to allow it.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-build/macos-release}
out=${2:-dist}
case $out in /*) ;; *) out=$root/$out ;; esac
cd "$root"

version=$(sed -n 's/^project(Leinwand VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$version" ] || { echo "No version in CMakeLists.txt." >&2; exit 1; }
# A pre-release label (LEINWAND_PRERELEASE) is part of the file name.
prerelease=$(sed -n 's/^set(LEINWAND_PRERELEASE "\([^"]*\)")/\1/p' CMakeLists.txt)
if [ -n "$prerelease" ]; then version=$version-$prerelease; fi
name=Leinwand-$version-macos-arm64
stage=$out/$name
rm -rf "$stage"
mkdir -p "$out"

# The bundle with Qt's frameworks and plugins (macdeployqt, through the
# deploy script), the licences beside it.
cmake --install "$build" --prefix "$stage"
codesign --force --deep --sign - "$stage/Leinwand.app"
codesign --verify --deep --strict "$stage/Leinwand.app"
ln -s /Applications "$stage/Applications"

dmg=$out/$name.dmg
rm -f "$dmg"
hdiutil create -volname Leinwand -srcfolder "$stage" -ov -format UDZO "$dmg"
shasum -a 256 "$dmg" | sed "s|  .*|  $name.dmg|" > "$dmg.sha256"
echo "Package: $dmg"
