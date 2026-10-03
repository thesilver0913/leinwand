// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor/number_input.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <string>

namespace leinwand::editor {

namespace {

// Recursive descent over: expr = term {(+|-) term}; term = factor {(*|/)
// factor}; factor = [+|-] (number [unit] | "(" expr ")").
class Parser {
 public:
  Parser(std::string_view text, std::optional<Unit> unit) : text_(text), unit_(unit) {}

  std::optional<double> Run() {
    auto value = Expr();
    Skip();
    if (!value || pos_ != text_.size() || !std::isfinite(*value)) return std::nullopt;
    return value;
  }

 private:
  void Skip() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
  }

  bool Take(char c) {
    Skip();
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool TakeWord(std::string_view word) {
    if (text_.substr(pos_, word.size()) != word) return false;
    // Not the start of a longer word ("in" in "inch" is fine to refuse).
    const size_t end = pos_ + word.size();
    if (end < text_.size() && std::isalpha(static_cast<unsigned char>(text_[end]))) return false;
    pos_ = end;
    return true;
  }

  std::optional<double> Expr() {
    auto value = Term();
    while (value) {
      if (Take('+')) {
        const auto rhs = Term();
        if (!rhs) return std::nullopt;
        *value += *rhs;
      } else if (Take('-')) {
        const auto rhs = Term();
        if (!rhs) return std::nullopt;
        *value -= *rhs;
      } else {
        break;
      }
    }
    return value;
  }

  std::optional<double> Term() {
    auto value = Factor();
    while (value) {
      if (Take('*')) {
        const auto rhs = Factor();
        if (!rhs) return std::nullopt;
        *value *= *rhs;
      } else if (Take('/')) {
        const auto rhs = Factor();
        if (!rhs || *rhs == 0) return std::nullopt;
        *value /= *rhs;
      } else {
        break;
      }
    }
    return value;
  }

  std::optional<double> Factor() {
    if (Take('-')) {
      const auto value = Factor();
      return value ? std::optional{-*value} : std::nullopt;
    }
    if (Take('+')) return Factor();
    if (Take('(')) {
      const auto value = Expr();
      if (!value || !Take(')')) return std::nullopt;
      return value;
    }
    Skip();
    const size_t start = pos_;
    while (pos_ < text_.size() &&
           (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.')) {
      ++pos_;
    }
    if (pos_ == start) return std::nullopt;
    double number = 0;
    const auto [end, error] = std::from_chars(text_.data() + start, text_.data() + pos_, number);
    if (error != std::errc{} || end != text_.data() + pos_) return std::nullopt;
    return number * UnitFactor();
  }

  // A unit after a number, as a factor into the field's unit. Unitless
  // fields take none.
  double UnitFactor() {
    Skip();
    if (!unit_) {
      return 1.0;
    }
    struct Spelling {
      std::string_view text;
      Unit unit;
    };
    static constexpr Spelling kUnits[] = {
        {"pt", Unit::kPoint},      {"px", Unit::kPixel}, {"mm", Unit::kMillimeter},
        {"cm", Unit::kCentimeter}, {"in", Unit::kInch},  {"\"", Unit::kInch},
        {"pc", Unit::kPica},
    };
    for (const auto& [text, unit] : kUnits) {
      if (TakeWord(text)) return PointsPer(unit) / PointsPer(*unit_);
    }
    return 1.0;
  }

  std::string_view text_;
  std::optional<Unit> unit_;
  size_t pos_ = 0;
};

}  // namespace

double PointsPer(Unit unit) {
  switch (unit) {
    case Unit::kPoint:
    case Unit::kPixel:
      return 1.0;
    case Unit::kMillimeter:
      return 72.0 / 25.4;
    case Unit::kCentimeter:
      return 72.0 / 2.54;
    case Unit::kInch:
      return 72.0;
    case Unit::kPica:
      return 12.0;
  }
  return 1.0;
}

std::optional<double> EvaluateLength(std::string_view text, Unit unit) {
  const auto value = Parser(text, unit).Run();
  return value ? std::optional{*value * PointsPer(unit)} : std::nullopt;
}

std::optional<double> EvaluateNumber(std::string_view text) {
  std::string trimmed(text);
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
    trimmed.pop_back();
  }
  for (std::string_view suffix : {"°", "%"}) {
    if (trimmed.size() >= suffix.size() &&
        std::string_view(trimmed).substr(trimmed.size() - suffix.size()) == suffix) {
      trimmed.resize(trimmed.size() - suffix.size());
      break;
    }
  }
  return Parser(trimmed, std::nullopt).Run();
}

}  // namespace leinwand::editor
