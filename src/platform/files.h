// SPDX-License-Identifier: GPL-3.0-or-later
// File questions the OSes answer differently (CLAUDE.md: OS-specific code
// stays behind platform).
#pragma once

#include <filesystem>

namespace leinwand::platform {

// Whether the file's contents are on this computer. False for cloud files
// that are only downloaded when read: OneDrive, Dropbox and Google Drive
// "online-only" files on Windows, dataless files of macOS file providers.
// Reading such a file starts a download, so previews skip them.
bool IsLocal(const std::filesystem::path& path);

}  // namespace leinwand::platform
