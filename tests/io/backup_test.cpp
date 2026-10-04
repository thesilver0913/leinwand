// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/backup.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace leinwand;

namespace {

std::string Read(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("Backups copy the file before it is replaced and keep the newest") {
  const auto dir = std::filesystem::path(LEINWAND_TEST_OUTPUT_DIR) / "backup";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto file = dir / "doc.lwd";
  const auto folder = dir / "backups";

  // No file yet: nothing to keep.
  CHECK(io::BackUp(file, folder, "20261004-100000-000", 3));
  CHECK(!std::filesystem::exists(folder));

  for (int i = 0; i < 5; ++i) {
    std::ofstream(file, std::ios::binary) << "version " << i;
    CHECK(io::BackUp(file, folder, "20261004-10000" + std::to_string(i) + "-000", 3));
  }
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(folder)) {
    (void)entry;
    ++count;
  }
  CHECK(count == 3);
  // The newest three stay.
  CHECK(Read(folder / "20261004-100004-000.lwd") == "version 4");
  CHECK(Read(folder / "20261004-100002-000.lwd") == "version 2");
  CHECK(!std::filesystem::exists(folder / "20261004-100001-000.lwd"));

  // Keeping none makes no copy.
  CHECK(io::BackUp(file, dir / "none", "20261004-100009-000", 0));
  CHECK(!std::filesystem::exists(dir / "none"));
}
