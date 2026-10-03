// SPDX-License-Identifier: GPL-3.0-or-later
// The native format (spec 3.1-3.3): a ZIP container holding `mimetype`
// (first, uncompressed), `document.json` and `thumbnail.png`.
//
// document.json is indented with a fixed key order and omits fields equal to
// their defaults; numbers are rounded to 6 decimals. Fields and object types
// this version does not know are kept and written back unchanged.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/document.h"
#include "io/import_report.h"

namespace leinwand::io {

// The format version written (spec 3.3: major.minor).
inline constexpr int kFormatMajor = 1;
inline constexpr int kFormatMinor = 0;
inline constexpr std::string_view kMimeType = "application/x-leinwand-document";

enum class LoadError {
  kNone,
  kNotFound,      // The file could not be read.
  kNotLeinwand,   // No ZIP, or no Leinwand mimetype.
  kNewerVersion,  // A newer major format version: not opened (spec 3.3).
  kCorrupt,       // The JSON does not parse or misses required parts.
};

struct LoadResult {
  std::optional<core::Document> document;
  LoadError error = LoadError::kNone;
  std::string message;  // Details for kCorrupt.
  ImportReport report;  // Unknown object types kept as placeholders.
};

// document.json alone.
std::string WriteDocumentJson(const core::Document& document, std::string_view app_version);
LoadResult ReadDocumentJson(std::string_view json);

// The whole container in memory.
std::vector<std::uint8_t> WriteLwd(const core::Document& document, std::string_view app_version,
                                   const std::vector<std::uint8_t>& thumbnail_png);
LoadResult ReadLwd(const std::vector<std::uint8_t>& bytes);

// Saves safely (spec 3.3, "保存の手順"): writes a temporary file next to
// `path`, reads it back to check it, then replaces `path`. On failure the
// original file is untouched and `error` says why.
bool SaveLwd(const std::filesystem::path& path, const core::Document& document,
             std::string_view app_version, const std::vector<std::uint8_t>& thumbnail_png,
             std::string* error);
LoadResult LoadLwd(const std::filesystem::path& path);

}  // namespace leinwand::io
