// SPDX-License-Identifier: GPL-3.0-or-later
// Test programs that lay out text use the bundled fonts only, so that the
// results are the same on every OS (M8: HarfBuzz gives the same glyphs).
#include <memory>

#include "text/font.h"

namespace {

const bool fonts_ready = [] {
  leinwand::text::SetFontSources(
      {std::make_shared<leinwand::text::FolderFontSource>(LEINWAND_FONTS_DIR)});
  return true;
}();

}  // namespace
