// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_compare.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "include/codec/SkCodec.h"
#include "include/codec/SkPngDecoder.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/encode/SkPngEncoder.h"

namespace leinwand::testing {

namespace fs = std::filesystem;

namespace {

bool WritePng(const fs::path& path, const SkPixmap& pixmap) {
  fs::create_directories(path.parent_path());
  SkFILEWStream stream(path.string().c_str());
  return stream.isValid() && SkPngEncoder::Encode(&stream, pixmap, {});
}

}  // namespace

bool MatchesBaseline(const std::string& name, const std::vector<std::uint8_t>& pixels, int width,
                     int height) {
  const SkImageInfo info =
      SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
  const SkPixmap actual(info, pixels.data(), info.minRowBytes());
  const fs::path baseline = fs::path(LEINWAND_TESTDATA_DIR) / (name + ".png");
  const fs::path output = fs::path(LEINWAND_TEST_OUTPUT_DIR) / (name + ".png");

  const char* update = std::getenv("LEINWAND_UPDATE_BASELINES");
  if (update && std::string(update) == "1") {
    std::cerr << "Writing baseline " << baseline << "\n";
    return WritePng(baseline, actual);
  }

  std::unique_ptr<SkCodec> codec =
      SkPngDecoder::Decode(SkData::MakeFromFileName(baseline.string().c_str()), nullptr);
  if (!codec) {
    WritePng(output, actual);
    std::cerr << "Missing baseline " << baseline << "; actual written to " << output << "\n";
    return false;
  }
  std::vector<std::uint8_t> expected(info.computeMinByteSize());
  if (codec->getInfo().dimensions() != info.dimensions() ||
      codec->getPixels(info, expected.data(), info.minRowBytes()) != SkCodec::kSuccess) {
    WritePng(output, actual);
    std::cerr << "Baseline " << baseline << " has a different size or could not be read\n";
    return false;
  }

  int differing = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    for (int c = 0; c < 4; ++c) {
      if (std::abs(int{pixels[i + c]} - int{expected[i + c]}) > 8) {
        ++differing;
        break;
      }
    }
  }
  const double fraction = static_cast<double>(differing) / (width * height);
  if (fraction > 0.005) {
    WritePng(output, actual);
    std::cerr << name << ": " << differing << " pixels differ (" << fraction * 100
              << "%); actual written to " << output << "\n";
    return false;
  }
  return true;
}

}  // namespace leinwand::testing
