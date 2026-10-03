// SPDX-License-Identifier: GPL-3.0-or-later
// Book and booklet covers (spec 7.5, "同人誌・冊子の表紙テンプレート"): the
// back cover, spine and front cover side by side, with the spine width
// worked out from the page count and the paper's thickness.
#pragma once

#include <optional>
#include <string>

#include "core/types.h"

namespace leinwand::core {

struct Document;

// Kept in the document so that the cover can be laid out again when the
// page count or paper changes. Lengths in points.
struct CoverSpec {
  double width = 0;  // One cover (front or back), trimmed.
  double height = 0;
  int pages = 0;                // Pages of the body, counted as pages (not sheets).
  double paper_thickness = 0;   // One sheet of the body paper.
  std::optional<double> spine;  // Given directly; otherwise worked out.
  double bleed = 0;
  friend bool operator==(const CoverSpec&, const CoverSpec&) = default;
};

// Each sheet carries two pages, so the spine is pages / 2 sheets thick.
double SpineWidth(const CoverSpec& spec);

// The names given to the cover's artboards (localized by the app).
struct CoverNames {
  std::string spread = "Cover spread";
  std::string back = "Back cover";
  std::string spine = "Spine";
  std::string front = "Front cover";
};

// The artboards of the cover laid out from `spec`, left to right: the back
// cover, the spine and the front cover from x = 0, plus the whole spread
// (with the bleed, for export) underneath them. Existing cover artboards
// (found by their ids) are moved and resized and keep their names; other
// artboards and the artwork are left alone. The spec is stored in the
// document.
Document WithCover(Document document, const CoverSpec& spec, const CoverNames& names = {});

// Ids of the cover's artboards.
inline constexpr const char* kCoverSpreadId = "cover-spread";
inline constexpr const char* kCoverBackId = "cover-back";
inline constexpr const char* kCoverSpineId = "cover-spine";
inline constexpr const char* kCoverFrontId = "cover-front";

}  // namespace leinwand::core
