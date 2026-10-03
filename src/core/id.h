// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <unordered_set>

namespace leinwand::core {

// Hands out short ids that are unique within one document ("o7f3k2" in
// spec 3.2). Ids already in use, e.g. from a loaded file, must be reserved
// first so they are never reissued.
class IdGenerator {
 public:
  explicit IdGenerator(std::uint64_t seed = std::random_device{}());

  std::string Next();
  void Reserve(const std::string& id) { used_.insert(id); }
  bool IsUsed(const std::string& id) const { return used_.contains(id); }

 private:
  std::mt19937_64 rng_;
  std::unordered_set<std::string> used_;
};

}  // namespace leinwand::core
