// SPDX-License-Identifier: GPL-3.0-or-later
// Applying affine transforms to model objects. Lives in core because edit
// commands need it.
#pragma once

#include "core/object.h"
#include "core/path.h"
#include "core/types.h"

namespace leinwand::core {

// Moves anchors with `matrix` and maps handles as directions.
PathData Transformed(const PathData& path, const Matrix& matrix);

// Paths and compound paths get the transform baked into their coordinates
// (spec 4.1). Groups get it prepended to their own matrix, so their children
// stay shared. Strokes are not scaled.
ObjectPtr Transformed(const ObjectPtr& object, const Matrix& matrix);

}  // namespace leinwand::core
