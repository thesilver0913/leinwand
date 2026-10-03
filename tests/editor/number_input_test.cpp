// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor/number_input.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using leinwand::editor::EvaluateLength;
using leinwand::editor::EvaluateNumber;
using leinwand::editor::Unit;

TEST_CASE("Lengths take units and convert to points") {
  CHECK(*EvaluateLength("12") == Approx(12));
  CHECK(*EvaluateLength("12 pt") == Approx(12));
  CHECK(*EvaluateLength("10mm") == Approx(28.3464567));
  CHECK(*EvaluateLength("2in") == Approx(144));
  CHECK(*EvaluateLength("1\"") == Approx(72));
  CHECK(*EvaluateLength("1pc") == Approx(12));
  CHECK(*EvaluateLength("1cm") == Approx(28.3464567));
  CHECK(*EvaluateLength("10", Unit::kMillimeter) == Approx(28.3464567));  // Field in mm.
}

TEST_CASE("Lengths allow arithmetic, mixing units") {
  CHECK(*EvaluateLength("100+20") == Approx(120));
  CHECK(*EvaluateLength("50/2") == Approx(25));
  CHECK(*EvaluateLength("(1+2)*3") == Approx(9));
  CHECK(*EvaluateLength("-5 + 2*3") == Approx(1));
  CHECK(*EvaluateLength("100+1in") == Approx(172));
  CHECK(*EvaluateLength("10mm*2") == Approx(56.6929134));
}

TEST_CASE("Bad entries do not evaluate") {
  CHECK_FALSE(EvaluateLength(""));
  CHECK_FALSE(EvaluateLength("abc"));
  CHECK_FALSE(EvaluateLength("1/0"));
  CHECK_FALSE(EvaluateLength("(1+2"));
  CHECK_FALSE(EvaluateLength("10 furlongs"));
  CHECK_FALSE(EvaluateNumber("10mm"));  // Units belong to lengths.
}

TEST_CASE("Plain numbers ignore a degree or percent sign") {
  CHECK(*EvaluateNumber("45°") == Approx(45));
  CHECK(*EvaluateNumber("50 %") == Approx(50));
  CHECK(*EvaluateNumber("90/2") == Approx(45));
}
