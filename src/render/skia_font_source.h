// SPDX-License-Identifier: GPL-3.0-or-later
// The OS fonts for the text engine (spec 5.2), through Skia's platform font
// managers: DirectWrite on Windows, Core Text on macOS, Fontconfig on Linux
// (M8). Skia's types stay inside; the engine only sees font bytes.
#pragma once

#include <memory>

#include "text/font.h"

namespace leinwand::render {

std::shared_ptr<const text::FontSource> MakeSystemFontSource();

}  // namespace leinwand::render
