// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/import_report.h"

#include <algorithm>

namespace leinwand::io {

void ImportReport::Add(const std::string& kind, ReportAction action, const std::string& id) {
  auto it = std::find_if(rows.begin(), rows.end(), [&](const ReportRow& row) {
    return row.kind == kind && row.action == action;
  });
  if (it == rows.end()) {
    rows.push_back({kind, action, 0, {}});
    it = rows.end() - 1;
  }
  ++it->count;
  if (!id.empty() && action != ReportAction::kDiscarded) it->object_ids.push_back(id);
}

}  // namespace leinwand::io
