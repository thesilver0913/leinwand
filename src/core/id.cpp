// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/id.h"

namespace leinwand::core {

namespace {
constexpr char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
constexpr int kLength = 6;  // 36^6 ≈ 2.2 billion ids.
}  // namespace

IdGenerator::IdGenerator(std::uint64_t seed) : rng_(seed) {}

std::string IdGenerator::Next() {
  std::uniform_int_distribution<int> digit(0, 35);
  std::string id(kLength, '0');
  do {
    for (char& c : id) c = kAlphabet[digit(rng_)];
  } while (!used_.insert(id).second);
  return id;
}

}  // namespace leinwand::core
