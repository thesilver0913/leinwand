// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "geometry/path_ops.h"

namespace leinwand::render {

// The path operations engine on Skia's PathOps (spec 4.3, M8 check 1).
// Skia keeps coordinates as 32-bit floats, so the inputs are moved to be
// centred on the origin before conversion and the result is moved back:
// precision then depends on the inputs' size, not on where they are.
class SkiaPathOps : public geometry::PathOpsEngine {
 public:
  std::optional<geometry::Region> Apply(const geometry::Region& a, const geometry::Region& b,
                                        geometry::BooleanOp op) const override;
  std::optional<geometry::Region> Simplify(const geometry::Region& region) const override;
};

}  // namespace leinwand::render
