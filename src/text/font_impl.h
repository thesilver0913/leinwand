// SPDX-License-Identifier: GPL-3.0-or-later
// HarfBuzz's side of a face, for the text engine's own files.
#pragma once

#include <hb.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "text/font.h"

namespace leinwand::text {

struct Face::Impl {
  std::shared_ptr<const std::vector<char>> bytes;
  int index = 0;
  hb_blob_t* blob = nullptr;
  hb_face_t* face = nullptr;
  hb_font_t* font = nullptr;
  unsigned upem = 1000;
  std::string family, style, postscript_name;
  double ascender = 0.88, descender = 0.12;
  std::uint64_t id = 0;
  ~Impl();
};

}  // namespace leinwand::text
