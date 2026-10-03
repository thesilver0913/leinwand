// SPDX-License-Identifier: GPL-3.0-or-later
// Locale-independent number parsing.
#pragma once

#include <cstddef>
#include <string_view>

namespace leinwand::core {

// Reads a decimal number (an optional '-', digits with an optional '.', an
// optional exponent) from the start of `text`, like std::from_chars: no
// locale, no leading whitespace or '+'. Returns how many characters it used,
// or 0 when `text` does not start with a number or the number is out of
// range. std::from_chars for double is missing from Apple's standard library,
// so this is the one place that deals with that.
std::size_t ParseDouble(std::string_view text, double* value);

}  // namespace leinwand::core
