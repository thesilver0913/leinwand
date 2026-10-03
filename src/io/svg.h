// SPDX-License-Identifier: GPL-3.0-or-later
// SVG to and from the model (spec 6.1). Phase 1 takes over paths, the basic
// shapes (rect and ellipse as live shapes), groups, transforms, fill and
// stroke, CSS (element, class, id and descendant selectors) and clip paths.
// Everything else is kept as XML and written back in place, or
// approximated, and reported (spec 6.3).
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/document.h"
#include "io/import_report.h"

namespace leinwand::io {

struct SvgImport {
  std::optional<core::Document> document;
  std::string error;  // When the XML does not parse or has no <svg> root.
  ImportReport report;
};

// 1 SVG user unit (px) becomes 1 pt, as in Illustrator; a root size in
// physical units (mm, in, pt) keeps its physical size.
SvgImport ImportSvg(std::string_view xml);

struct SvgExportOptions {
  int decimals = 3;  // Spec 6.1: 1-7, default 3.
  // The first artboard's area (the default), or the bounds of everything.
  bool whole_document = false;
};

// Presentation attributes, Inkscape-style layers, preserved elements back in
// place. What SVG cannot show as it is (inside/outside strokes, CMYK and
// spot colors, unknown .lwd content) is listed in `issues`.
std::string ExportSvg(const core::Document& document, const SvgExportOptions& options = {},
                      ImportReport* issues = nullptr);

}  // namespace leinwand::io
