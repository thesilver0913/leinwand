// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace leinwand::testing {

// Compares premultiplied RGBA8 pixels with testdata/<name>.png. Passes when
// at most 0.5% of pixels differ by more than 8 in any channel, which absorbs
// anti-aliasing differences between platforms.
//
// On a mismatch or a missing baseline, writes <build>/test-output/<name>.png
// for inspection. Set LEINWAND_UPDATE_BASELINES=1 to write the baseline
// instead (then review and commit it).
bool MatchesBaseline(const std::string& name, const std::vector<std::uint8_t>& pixels, int width,
                     int height);

// The fraction of pixels that differ by more than 8 in any channel between
// two images of the same size (premultiplied RGBA8).
double DifferingFraction(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b);

}  // namespace leinwand::testing
