// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/files.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace leinwand::platform {

bool IsLocal(const std::filesystem::path& path) {
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;
  // Cloud files stand in as placeholders until their data is recalled.
  constexpr DWORD kRecallOnDataAccess = 0x00400000;  // FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
  constexpr DWORD kRecallOnOpen = 0x00040000;        // FILE_ATTRIBUTE_RECALL_ON_OPEN
  return (attributes & (kRecallOnDataAccess | kRecallOnOpen | FILE_ATTRIBUTE_OFFLINE)) == 0;
#elif defined(__APPLE__)
  struct stat info{};
  if (stat(path.c_str(), &info) != 0) return false;
#if defined(SF_DATALESS)
  return (info.st_flags & SF_DATALESS) == 0;
#else
  return (info.st_flags & 0x40000000) == 0;  // SF_DATALESS
#endif
#else
  std::error_code error;
  return std::filesystem::exists(path, error);
#endif
}

}  // namespace leinwand::platform
