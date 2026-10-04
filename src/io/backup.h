// SPDX-License-Identifier: GPL-3.0-or-later
// Backups for autosaving to the document's own file (spec 3.3): before the
// file is replaced, its current contents are copied into a folder of its
// own, and only the newest copies are kept.
#pragma once

#include <filesystem>
#include <string>

namespace leinwand::io {

// Copies `file` into `folder` as `<stamp>.lwd` (stamp: a sortable time,
// e.g. "20261004-120000"), then deletes the oldest copies beyond `keep`.
// Nothing happens when `file` does not exist or `keep` is 0. False if the
// copy failed (the caller should not overwrite the file then).
bool BackUp(const std::filesystem::path& file, const std::filesystem::path& folder,
            const std::string& stamp, int keep);

}  // namespace leinwand::io
