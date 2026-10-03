// SPDX-License-Identifier: GPL-3.0-or-later
// Path operations (spec 4.3): Boolean operations on filled regions, behind
// an interface so the engine can be replaced. The first engine is Skia's
// PathOps (render/skia_path_ops.h); geometry and the editor only see this.
#pragma once

#include <optional>
#include <vector>

#include "core/path.h"

namespace leinwand::geometry {

// A filled area: subpaths with a fill rule, as a path or compound path is
// filled (open subpaths count as closed).
struct Region {
  std::vector<core::PathData> subpaths;
  core::FillRule fill_rule = core::FillRule::kNonZero;

  bool empty() const { return subpaths.empty(); }
  friend bool operator==(const Region&, const Region&) = default;
};

enum class BooleanOp {
  kUnion,       // A ∪ B
  kIntersect,   // A ∩ B
  kDifference,  // A − B
  kExclude,     // A xor B
};

class PathOpsEngine {
 public:
  virtual ~PathOpsEngine() = default;

  // The result keeps curves as curves. nullopt means the engine failed; it
  // never returns a result it knows to be wrong (spec 4.3). An empty region
  // is a valid result (two shapes that do not overlap have no intersection).
  virtual std::optional<Region> Apply(const Region& a, const Region& b, BooleanOp op) const = 0;

  // The same area without self-intersections or overlapping subpaths.
  virtual std::optional<Region> Simplify(const Region& region) const = 0;
};

// These two expect contours that do not cross each other, as an engine's
// results have (they may touch at points).

// Splits a region into its separate pieces: each outer contour with the
// holes directly inside it. Islands inside holes become pieces of their own.
std::vector<Region> SplitIntoPieces(const Region& region);

// The filled area (outer contours minus holes), flattened to `tolerance`.
double Area(const Region& region, double tolerance = 0.01);

}  // namespace leinwand::geometry
