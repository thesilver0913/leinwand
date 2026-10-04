// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/backup.h"

#include <algorithm>
#include <system_error>
#include <vector>

namespace leinwand::io {

bool BackUp(const std::filesystem::path& file, const std::filesystem::path& folder,
            const std::string& stamp, int keep) {
  std::error_code error;
  if (keep <= 0 || !std::filesystem::exists(file, error)) return true;
  std::filesystem::create_directories(folder, error);
  if (error) return false;
  // Two saves within a second get "-2", "-3", ...
  std::filesystem::path target = folder / (stamp + ".lwd");
  for (int n = 2; std::filesystem::exists(target, error); ++n) {
    target = folder / (stamp + "-" + std::to_string(n) + ".lwd");
  }
  std::filesystem::copy_file(file, target, error);
  if (error) return false;
  // The newest `keep` copies stay; names sort by time.
  std::vector<std::filesystem::path> copies;
  for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
    if (entry.path().extension() == ".lwd") copies.push_back(entry.path());
  }
  std::sort(copies.begin(), copies.end());
  while (copies.size() > static_cast<std::size_t>(keep)) {
    std::filesystem::remove(copies.front(), error);
    copies.erase(copies.begin());
  }
  return true;
}

}  // namespace leinwand::io
