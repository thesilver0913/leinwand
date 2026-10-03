// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/number.h"

#include <charconv>
#include <system_error>

#if !defined(__cpp_lib_to_chars)
#include <xlocale.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#endif

namespace leinwand::core {

std::size_t ParseDouble(std::string_view text, double* value) {
  if (text.empty() || !(text[0] == '-' || text[0] == '.' || (text[0] >= '0' && text[0] <= '9'))) {
    return 0;
  }
#if defined(__cpp_lib_to_chars)
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), *value);
  if (error != std::errc{}) return 0;
  return static_cast<std::size_t>(end - text.data());
#else
  // strtod with the C locale, on a copy limited to the characters a decimal
  // number can have (strtod would also take hexadecimal, "inf" and "nan").
  std::size_t length = 0;
  while (length < text.size()) {
    const char c = text[length];
    if (!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) {
      break;
    }
    ++length;
  }
  const std::string copy(text.substr(0, length));
  static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
  char* end = nullptr;
  errno = 0;
  const double result = strtod_l(copy.c_str(), &end, c_locale);
  if (end == copy.c_str() || errno == ERANGE) return 0;
  *value = result;
  return static_cast<std::size_t>(end - copy.c_str());
#endif
}

}  // namespace leinwand::core
