// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/number.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinRel;
using leinwand::core::ParseDouble;

TEST_CASE("ParseDouble reads decimal numbers and says how much it used") {
  double v = 0;
  CHECK(ParseDouble("12.5", &v) == 4);
  CHECK_THAT(v, WithinRel(12.5));
  CHECK(ParseDouble("-0.25px", &v) == 5);
  CHECK_THAT(v, WithinRel(-0.25));
  CHECK(ParseDouble(".5", &v) == 2);
  CHECK_THAT(v, WithinRel(0.5));
  CHECK(ParseDouble("1e3,", &v) == 3);
  CHECK_THAT(v, WithinRel(1000.0));
  CHECK(ParseDouble("2.5E-1 ", &v) == 6);
  CHECK_THAT(v, WithinRel(0.25));
  // A number running into the next one, as in SVG path data.
  CHECK(ParseDouble("3-4", &v) == 1);
  CHECK_THAT(v, WithinRel(3.0));
}

TEST_CASE("ParseDouble rejects what is not a number") {
  double v = 7;
  CHECK(ParseDouble("", &v) == 0);
  CHECK(ParseDouble("abc", &v) == 0);
  CHECK(ParseDouble(" 1", &v) == 0);
  CHECK(ParseDouble("+1", &v) == 0);
  CHECK(ParseDouble("-", &v) == 0);
  CHECK(ParseDouble("0x10", &v) == 1);  // Only the 0: no hexadecimal.
  CHECK(v == 0);
  CHECK(ParseDouble("1e999", &v) == 0);  // Out of range.
}
