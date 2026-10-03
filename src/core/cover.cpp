// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cover.h"

#include <algorithm>

#include "core/document.h"

namespace leinwand::core {

double SpineWidth(const CoverSpec& spec) {
  if (spec.spine) return std::max(0.0, *spec.spine);
  return std::max(0, spec.pages) / 2.0 * std::max(0.0, spec.paper_thickness);
}

Document WithCover(Document document, const CoverSpec& spec, const CoverNames& names) {
  const double w = spec.width, h = spec.height, spine = SpineWidth(spec);
  struct Part {
    const char* id;
    const std::string* name;
    Rect bounds;
    double bleed;
  };
  const Part parts[] = {
      {kCoverSpreadId, &names.spread, Rect::FromXYWH(0, 0, w * 2 + spine, h), spec.bleed},
      {kCoverBackId, &names.back, Rect::FromXYWH(0, 0, w, h), 0},
      {kCoverSpineId, &names.spine, Rect::FromXYWH(w, 0, spine, h), 0},
      {kCoverFrontId, &names.front, Rect::FromXYWH(w + spine, 0, w, h), 0},
  };
  for (const Part& part : parts) {
    auto found = std::find_if(document.artboards.begin(), document.artboards.end(),
                              [&](const Artboard& a) { return a.id == part.id; });
    if (found == document.artboards.end()) {
      document.artboards.push_back({part.id, *part.name, part.bounds, {}, part.bleed});
    } else {
      found->bounds = part.bounds;
      found->bleed = part.bleed;
    }
  }
  document.cover = spec;
  return document;
}

}  // namespace leinwand::core
