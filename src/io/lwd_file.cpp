// SPDX-License-Identifier: GPL-3.0-or-later
// The .lwd ZIP container and safe saving (spec 3.1, 3.3).
#include <miniz.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#include "io/lwd.h"

namespace leinwand::io {

namespace {

constexpr const char* kDocumentEntry = "document.json";
constexpr const char* kThumbnailEntry = "thumbnail.png";

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path, bool* ok) {
  std::ifstream in(path, std::ios::binary);
  *ok = static_cast<bool>(in);
  if (!*ok) return {};
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  out.close();
  return static_cast<bool>(out);
}

}  // namespace

std::vector<std::uint8_t> WriteLwd(const core::Document& document, std::string_view app_version,
                                   const std::vector<std::uint8_t>& thumbnail_png) {
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) return {};
  const std::string json = WriteDocumentJson(document, app_version);
  // The mimetype comes first and stored, so that it can be read at a fixed
  // offset to tell the format (spec 3.1).
  bool ok =
      mz_zip_writer_add_mem(&zip, "mimetype", kMimeType.data(), kMimeType.size(),
                            MZ_NO_COMPRESSION) &&
      mz_zip_writer_add_mem(&zip, kDocumentEntry, json.data(), json.size(), MZ_DEFAULT_COMPRESSION);
  if (ok && !thumbnail_png.empty()) {
    // PNG is compressed already.
    ok = mz_zip_writer_add_mem(&zip, kThumbnailEntry, thumbnail_png.data(), thumbnail_png.size(),
                               MZ_NO_COMPRESSION);
  }
  void* buffer = nullptr;
  size_t size = 0;
  ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
  std::vector<std::uint8_t> bytes;
  if (ok) {
    const auto* data = static_cast<const std::uint8_t*>(buffer);
    bytes.assign(data, data + size);
  }
  mz_zip_writer_end(&zip);  // Frees the heap buffer.
  return bytes;
}

LoadResult ReadLwd(const std::vector<std::uint8_t>& bytes) {
  LoadResult result;
  mz_zip_archive zip{};
  if (bytes.empty() || !mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
    result.error = LoadError::kNotLeinwand;
    return result;
  }
  // The format is told by the mimetype entry, not by the extension.
  char name[64] = {};
  const bool first_is_mimetype = mz_zip_reader_get_num_files(&zip) > 0 &&
                                 mz_zip_reader_get_filename(&zip, 0, name, sizeof name) > 0 &&
                                 std::strcmp(name, "mimetype") == 0;
  size_t size = 0;
  void* mime = first_is_mimetype ? mz_zip_reader_extract_to_heap(&zip, 0, &size, 0) : nullptr;
  const bool is_leinwand =
      mime && std::string_view(static_cast<const char*>(mime), size) == kMimeType;
  mz_free(mime);
  if (!is_leinwand) {
    mz_zip_reader_end(&zip);
    result.error = LoadError::kNotLeinwand;
    return result;
  }
  void* json = mz_zip_reader_extract_file_to_heap(&zip, kDocumentEntry, &size, 0);
  if (!json) {
    mz_zip_reader_end(&zip);
    result.error = LoadError::kCorrupt;
    result.message = "document.json is missing";
    return result;
  }
  result = ReadDocumentJson(std::string_view(static_cast<const char*>(json), size));
  mz_free(json);
  mz_zip_reader_end(&zip);
  return result;
}

bool SaveLwd(const std::filesystem::path& path, const core::Document& document,
             std::string_view app_version, const std::vector<std::uint8_t>& thumbnail_png,
             std::string* error) {
  auto fail = [&](const std::string& message) {
    if (error) *error = message;
    return false;
  };
  const std::vector<std::uint8_t> bytes = WriteLwd(document, app_version, thumbnail_png);
  if (bytes.empty()) return fail("could not build the file");

  // 1. Write a temporary file next to the target (same volume for the rename).
  std::filesystem::path temp = path;
  temp += ".saving";
  if (!WriteFile(temp, bytes)) return fail("could not write " + temp.string());

  // 2. Read it back: the ZIP and the JSON must load.
  bool read_ok = false;
  const LoadResult check = ReadLwd(ReadFile(temp, &read_ok));
  if (!read_ok || !check.document) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return fail("the written file did not read back");
  }

  // 3. Replace the original; until here it was left as it was.
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return fail(ec.message());
  }
  return true;
}

LoadResult LoadLwd(const std::filesystem::path& path) {
  bool ok = false;
  const std::vector<std::uint8_t> bytes = ReadFile(path, &ok);
  if (!ok) {
    LoadResult result;
    result.error = LoadError::kNotFound;
    return result;
  }
  return ReadLwd(bytes);
}

}  // namespace leinwand::io
