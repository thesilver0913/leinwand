// SPDX-License-Identifier: GPL-3.0-or-later
// The Pathfinder panel's operations (spec 4.3, "パスファインダーの対応"),
// built from the engine's Boolean operations.
#pragma once

#include <optional>
#include <vector>

#include "geometry/path_ops.h"

namespace leinwand::geometry {

enum class Pathfinder {
  // Shape modes: one result.
  kUnite,       // 合体: everything together.
  kMinusFront,  // 前面で型抜き: the backmost minus everything in front.
  kIntersect,   // 交差: where all overlap.
  kExclude,     // 中マド: where an odd number overlap.
  // Pathfinders: several pieces, grouped by the caller.
  kDivide,     // 分割: every region the outlines enclose.
  kTrim,       // 刈り込み: what is visible of each object.
  kMerge,      // 合流: trim, then join pieces of the same fill.
  kCrop,       // 切り抜き: what is visible inside the frontmost.
  kOutline,    // アウトライン: the divided regions' edges as open paths.
  kMinusBack,  // 背面で型抜き: the frontmost minus everything behind.
};

struct PathfinderPiece {
  Region region;  // Open paths for kOutline; filled regions otherwise.
  int source;     // The input whose appearance the piece takes.
};

// `inputs` are back to front. `fill_keys` (for kMerge) gives each input a
// key; inputs with the same key have the same fill and merge. Returns
// nullopt when the engine fails, and an empty list when nothing is left (two
// shapes that do not touch have no intersection). Fewer than two inputs:
// nullopt.
std::optional<std::vector<PathfinderPiece>> RunPathfinder(const PathOpsEngine& engine,
                                                          Pathfinder operation,
                                                          const std::vector<Region>& inputs,
                                                          const std::vector<int>& fill_keys = {});

// Whether the operation's pieces lose their strokes (trim, merge and crop,
// as in Illustrator).
bool DropsStrokes(Pathfinder operation);

}  // namespace leinwand::geometry
