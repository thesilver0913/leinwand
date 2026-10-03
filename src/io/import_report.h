// SPDX-License-Identifier: GPL-3.0-or-later
// What a file reader could not take over as it was (spec 6.3, "読み込み
// レポート"). Nothing is dropped silently: every element that is kept only as
// data, approximated, converted or discarded is counted here.
#pragma once

#include <string>
#include <vector>

namespace leinwand::io {

enum class ReportAction {
  kPreserved,     // Kept as data; written back by the same format.
  kApproximated,  // Replaced by something that looks close.
  kConverted,     // Looks the same, edited differently.
  kDiscarded,     // Not read.
};

struct ReportRow {
  std::string kind;  // What it was, e.g. "<linearGradient>" or "filter".
  ReportAction action = ReportAction::kPreserved;
  int count = 0;
  std::vector<std::string> object_ids;  // Objects it affected (not for kDiscarded).
};

struct ImportReport {
  std::vector<ReportRow> rows;

  // Counts one more of `kind` handled by `action`, on object `id` if any.
  void Add(const std::string& kind, ReportAction action, const std::string& id = {});
  bool empty() const { return rows.empty(); }
};

}  // namespace leinwand::io
