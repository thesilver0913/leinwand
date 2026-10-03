// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to render: Skia typefaces for the text engine's faces, for
// drawing text as glyphs (PDF keeps it as text with the font embedded).
#pragma once

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"
#include "text/font.h"

namespace leinwand::render {

// Made once per face from its bytes by the platform's font manager; null if
// Skia cannot read it.
sk_sp<SkTypeface> TypefaceOf(const text::Face& face);

}  // namespace leinwand::render
