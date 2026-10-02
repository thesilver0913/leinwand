// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "core/document.h"

namespace leinwand::render {

// A synthetic document for development and benchmarks: `path_count` random
// closed cubic blobs on a grid, each with a fill and a stroke, on one layer,
// with one artboard around them. Deterministic for a given seed.
core::Document MakeTestDocument(int path_count, std::uint32_t seed = 1);

}  // namespace leinwand::render
