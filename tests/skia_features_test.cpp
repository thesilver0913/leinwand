// SPDX-License-Identifier: GPL-3.0-or-later
// M0: confirms the vcpkg Skia build has the features phase 1 relies on.
// Vulkan is covered by render linking GrDirectContexts::MakeVulkan.
#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkStream.h"
#include "include/docs/SkPDFDocument.h"
#include "include/docs/SkPDFJpegHelpers.h"
#include "include/pathops/SkPathOps.h"

TEST_CASE("Skia PathOps unions two overlapping squares") {
  const SkPath a = SkPath::Rect(SkRect::MakeXYWH(0, 0, 10, 10));
  const SkPath b = SkPath::Rect(SkRect::MakeXYWH(5, 5, 10, 10));
  SkPath result;
  REQUIRE(Op(a, b, kUnion_SkPathOp, &result));
  CHECK(result.getBounds() == SkRect::MakeXYWH(0, 0, 15, 15));
  CHECK(result.contains(12, 12));
  CHECK_FALSE(result.contains(12, 2));
}

TEST_CASE("Skia PDF backend writes a document") {
  SkDynamicMemoryWStream stream;
  {
    // Skia refuses to create a PDF without JPEG callbacks.
    sk_sp<SkDocument> document = SkPDF::MakeDocument(&stream, SkPDF::JPEG::MetadataWithCallbacks());
    REQUIRE(document);
    SkCanvas* page = document->beginPage(200, 200);
    page->drawRect(SkRect::MakeXYWH(10, 10, 50, 50), SkPaint());
    document->endPage();
    document->close();
  }
  const sk_sp<SkData> pdf = stream.detachAsData();
  REQUIRE(pdf->size() > 4);
  CHECK(std::memcmp(pdf->data(), "%PDF", 4) == 0);
}
