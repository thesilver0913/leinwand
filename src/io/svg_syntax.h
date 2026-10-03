// SPDX-License-Identifier: GPL-3.0-or-later
// The small languages inside SVG attributes (spec 6.1): path data,
// transforms, colors, lengths, and CSS declarations and rules.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/color.h"
#include "core/path.h"
#include "core/types.h"

namespace leinwand::io::svg {

// Path data ("d"): every command, relative ones, arcs (A) and quadratic
// curves (Q, T) become cubic segments. Returns the subpaths; parsing stops
// at the first error (as SVG renderers do), keeping what came before.
std::vector<core::PathData> ParsePathData(std::string_view d);

// The "transform" attribute: matrix, translate, scale, rotate (with an
// optional centre), skewX and skewY, applied left to right. Null on errors.
std::optional<core::Matrix> ParseTransform(std::string_view text);

// A color: #rgb, #rrggbb, rgb()/rgba() with numbers or percentages, and the
// CSS named colors. "none", "currentColor" and url() are handled by callers.
// The alpha of rgba() goes to `alpha` when given.
std::optional<core::RgbColor> ParseColor(std::string_view text, double* alpha = nullptr);

// A length in user units (px), with units converted (1in = 96px; % is
// relative to `percent_of`).
std::optional<double> ParseLength(std::string_view text, double percent_of = 0.0);

// A list of numbers separated by commas and/or spaces (points, dash arrays,
// viewBox).
std::vector<double> ParseNumberList(std::string_view text);

// "name: value; ..." from a style attribute, names lower-case.
std::map<std::string, std::string> ParseDeclarations(std::string_view text);

// A CSS rule from a <style> element. Selectors supported: element, .class,
// #id, compounds of these (rect.a) and descendant combinators (g .a).
struct CssRule {
  // Each compound of the selector, outermost first; each is
  // {element or "", ids, classes}.
  struct Compound {
    std::string element;
    std::vector<std::string> ids;
    std::vector<std::string> classes;
  };
  std::vector<Compound> selector;
  int specificity = 0;  // ids * 100 + classes * 10 + elements.
  int order = 0;        // Position in the style sheets.
  std::map<std::string, std::string> declarations;
};
// Rules whose selectors are not supported (attribute selectors, child and
// sibling combinators, pseudo-classes) are skipped and counted in
// `skipped`.
std::vector<CssRule> ParseStyleSheet(std::string_view css, int first_order, int* skipped);

}  // namespace leinwand::io::svg
