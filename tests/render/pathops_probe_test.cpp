// SPDX-License-Identifier: GPL-3.0-or-later
// M8 check: how Skia's PathOps copes with the awkward inputs of spec 4.3.
// Hidden from the normal run ([.]); run with
//   render_tests "[pathops-probe]"
// It prints one line per case and operation. Each result is checked two
// ways: its area against the inclusion-exclusion identities (and exact
// values where known), and its pixels against the operation applied to the
// rasterized inputs.
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathIter.h"
#include "include/pathops/SkPathOps.h"

namespace {

constexpr double kKappa = 0.5522847498307936;

// A circle of four cubics, the way Leinwand's ellipses are built.
void AddCircle(SkPathBuilder& b, double cx, double cy, double r, bool clockwise = true) {
  const double k = r * kKappa;
  const double s = clockwise ? 1 : -1;
  auto p = [&](double x, double y) { return SkPoint::Make(float(cx + x), float(cy + s * y)); };
  b.moveTo(p(r, 0));
  b.cubicTo(p(r, k), p(k, r), p(0, r));
  b.cubicTo(p(-k, r), p(-r, k), p(-r, 0));
  b.cubicTo(p(-r, -k), p(-k, -r), p(0, -r));
  b.cubicTo(p(k, -r), p(r, -k), p(r, 0));
  b.close();
}
SkPath Circle(double cx, double cy, double r) {
  SkPathBuilder b;
  AddCircle(b, cx, cy, r);
  return b.detach();
}
SkPath Rect(double x, double y, double w, double h) {
  SkPathBuilder b;
  b.moveTo(float(x), float(y));
  b.lineTo(float(x + w), float(y));
  b.lineTo(float(x + w), float(y + h));
  b.lineTo(float(x), float(y + h));
  b.close();
  return b.detach();
}
SkPath Polygon(const std::vector<std::pair<double, double>>& points) {
  SkPathBuilder b;
  b.moveTo(float(points[0].first), float(points[0].second));
  for (size_t i = 1; i < points.size(); ++i) {
    b.lineTo(float(points[i].first), float(points[i].second));
  }
  b.close();
  return b.detach();
}
SkPath Rotated(const SkPath& path, double degrees, double cx, double cy) {
  return path.makeTransform(SkMatrix::RotateDeg(float(degrees), {float(cx), float(cy)}));
}

// The filled area, from Green's theorem over each contour finely
// subdivided. The path is first rewritten with the winding rule and holes
// wound the other way (AsWinding), so the signed areas add up.
double Area(const SkPath& input) {
  const std::optional<SkPath> wound = AsWinding(input);
  if (!wound) return -1;
  const SkPath& path = *wound;
  double area = 0;
  SkPoint start{}, last{};
  auto add_line = [&](SkPoint a, SkPoint b) {
    area += (double(a.fX) * b.fY - double(b.fX) * a.fY) / 2;
  };
  constexpr int kSteps = 512;
  SkPathIter iter = path.iter();
  while (auto rec = iter.next()) {
    const auto& pts = rec->fPoints;
    switch (rec->fVerb) {
      case SkPathVerb::kMove:
        start = last = pts[0];
        break;
      case SkPathVerb::kLine:
        add_line(pts[0], pts[1]);
        last = pts[1];
        break;
      case SkPathVerb::kQuad:
      case SkPathVerb::kConic:
      case SkPathVerb::kCubic: {
        SkPoint prev = pts[0];
        for (int i = 1; i <= kSteps; ++i) {
          const double t = double(i) / kSteps, u = 1 - t;
          double x, y;
          if (rec->fVerb == SkPathVerb::kCubic) {
            x = u * u * u * pts[0].fX + 3 * u * u * t * pts[1].fX + 3 * u * t * t * pts[2].fX +
                t * t * t * pts[3].fX;
            y = u * u * u * pts[0].fY + 3 * u * u * t * pts[1].fY + 3 * u * t * t * pts[2].fY +
                t * t * t * pts[3].fY;
          } else {
            const double w = rec->fVerb == SkPathVerb::kConic ? rec->fConicWeight : 1.0;
            const double d = u * u + 2 * w * u * t + t * t;
            x = (u * u * pts[0].fX + 2 * w * u * t * pts[1].fX + t * t * pts[2].fX) / d;
            y = (u * u * pts[0].fY + 2 * w * u * t * pts[1].fY + t * t * pts[2].fY) / d;
          }
          const SkPoint p = SkPoint::Make(float(x), float(y));
          add_line(prev, p);
          prev = p;
        }
        last = prev;
        break;
      }
      case SkPathVerb::kClose:
        add_line(last, start);
        last = start;
        break;
    }
  }
  return std::abs(area);
}

// The filled area by anti-aliased coverage on a 2048 x 2048 grid fitted to
// `bounds`: independent of how the result's contours are wound.
double CoverageArea(const SkPath& path, const SkRect& bounds) {
  constexpr int kGrid = 2048;
  SkBitmap bitmap;
  bitmap.allocPixels(SkImageInfo::MakeA8(kGrid, kGrid));
  bitmap.eraseColor(SK_ColorTRANSPARENT);
  SkCanvas canvas(bitmap);
  const double scale = (kGrid - 8) / double(std::max(bounds.width(), bounds.height()));
  canvas.translate(4, 4);
  canvas.scale(float(scale), float(scale));
  canvas.translate(-bounds.left(), -bounds.top());
  SkPaint paint;
  paint.setAntiAlias(true);
  canvas.drawPath(path, paint);
  double sum = 0;
  for (int y = 0; y < kGrid; ++y) {
    for (int x = 0; x < kGrid; ++x) sum += *bitmap.getAddr8(x, y) / 255.0;
  }
  return sum / (scale * scale);
}

struct VerbCount {
  int lines = 0, curves = 0;
};
VerbCount Verbs(const SkPath& path) {
  VerbCount count;
  SkPathIter iter = path.iter();
  while (auto rec = iter.next()) {
    if (rec->fVerb == SkPathVerb::kLine) ++count.lines;
    if (rec->fVerb == SkPathVerb::kQuad || rec->fVerb == SkPathVerb::kConic ||
        rec->fVerb == SkPathVerb::kCubic) {
      ++count.curves;
    }
  }
  return count;
}

// Rasterizes into a 512 x 512 coverage mask (no anti-aliasing) fitted to
// `bounds`.
constexpr int kSize = 512;
std::vector<uint8_t> Mask(const SkPath& path, const SkRect& bounds) {
  SkBitmap bitmap;
  bitmap.allocPixels(SkImageInfo::MakeA8(kSize, kSize));
  bitmap.eraseColor(SK_ColorTRANSPARENT);
  SkCanvas canvas(bitmap);
  const float scale = (kSize - 8) / std::max(bounds.width(), bounds.height());
  canvas.translate(4, 4);
  canvas.scale(scale, scale);
  canvas.translate(-bounds.left(), -bounds.top());
  SkPaint paint;
  paint.setAntiAlias(false);
  canvas.drawPath(path, paint);
  std::vector<uint8_t> mask(kSize * kSize);
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) mask[y * kSize + x] = *bitmap.getAddr8(x, y) ? 1 : 0;
  }
  return mask;
}

const char* OpName(SkPathOp op) {
  switch (op) {
    case kUnion_SkPathOp:
      return "union";
    case kIntersect_SkPathOp:
      return "intersect";
    case kDifference_SkPathOp:
      return "difference";
    case kXOR_SkPathOp:
      return "xor";
    default:
      return "?";
  }
}

struct Case {
  std::string name;
  SkPath a, b;
  // Exact areas where they are known: union, intersect, difference, xor.
  std::optional<std::array<double, 4>> exact;
};

int g_failures = 0;

void Run(const Case& c) {
  const SkRect bounds = [&] {
    SkRect r = c.a.getBounds();
    r.join(c.b.getBounds());
    return r;
  }();
  const auto mask_a = Mask(c.a, bounds), mask_b = Mask(c.b, bounds);
  const SkPathOp ops[] = {kUnion_SkPathOp, kIntersect_SkPathOp, kDifference_SkPathOp,
                          kXOR_SkPathOp};
  double areas[4] = {0, 0, 0, 0};
  double coverage[4] = {0, 0, 0, 0};
  bool all = true;
  for (int i = 0; i < 4; ++i) {
    const auto t0 = std::chrono::steady_clock::now();
    const std::optional<SkPath> result = Op(c.a, c.b, ops[i]);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!result) {
      std::printf("%-34s %-10s FAILED (no result)\n", c.name.c_str(), OpName(ops[i]));
      ++g_failures;
      all = false;
      continue;
    }
    areas[i] = Area(*result);
    coverage[i] = CoverageArea(*result, bounds);
    // Pixels: the operation on the masks against the result's mask. Only
    // pixels differing from all eight neighbours' expectation count, so a
    // one-pixel disagreement along an edge is not an error.
    const auto mask_r = Mask(*result, bounds);
    int wrong = 0, filled = 0;
    for (int y = 1; y < kSize - 1; ++y) {
      for (int x = 1; x < kSize - 1; ++x) {
        auto expect = [&](int px, int py) {
          const bool a = mask_a[py * kSize + px], b = mask_b[py * kSize + px];
          switch (ops[i]) {
            case kUnion_SkPathOp:
              return a || b;
            case kIntersect_SkPathOp:
              return a && b;
            case kDifference_SkPathOp:
              return a && !b;
            default:
              return a != b;
          }
        };
        const bool got = mask_r[y * kSize + x];
        filled += got;
        bool agrees = false;
        for (int dy = -1; dy <= 1 && !agrees; ++dy) {
          for (int dx = -1; dx <= 1 && !agrees; ++dx) agrees = expect(x + dx, y + dy) == got;
        }
        wrong += !agrees;
      }
    }
    const VerbCount verbs = Verbs(*result);
    std::string note;
    if (c.exact) {
      const double want = (*c.exact)[i];
      const double error = std::abs(areas[i] - want) / std::max(1e-12, std::abs(want) + 1e-9);
      char buffer[64];
      std::snprintf(buffer, sizeof buffer, " exact %.3g rel.err %.1e", want, error);
      note += buffer;
      if (error > 1e-3 && std::abs(areas[i] - want) > 1e-6) {
        note += " AREA";
        ++g_failures;
      }
    }
    if (wrong > 0) {
      note += " PIXELS";
      ++g_failures;
    }
    std::printf("%-34s %-10s area %-12.6g curves %3d lines %3d  %6.2f ms  wrong px %d%s\n",
                c.name.c_str(), OpName(ops[i]), areas[i], verbs.curves, verbs.lines, ms, wrong,
                note.c_str());
  }
  if (all) {
    // Inclusion-exclusion: |A∪B| + |A∩B| = |A| + |B|, |A−B| = |A| − |A∩B|,
    // |A xor B| = |A∪B| − |A∩B|.
    // Measured by coverage, so 1e-3 of the inputs' area is allowed.
    const double area_a = CoverageArea(c.a, bounds), area_b = CoverageArea(c.b, bounds);
    const double scale = std::max(area_a + area_b, 1e-12);
    const double e1 = std::abs(coverage[0] + coverage[1] - area_a - area_b) / scale;
    const double e2 = std::abs(coverage[2] - (area_a - coverage[1])) / scale;
    const double e3 = std::abs(coverage[3] - (coverage[0] - coverage[1])) / scale;
    const bool ok = e1 < 1e-3 && e2 < 1e-3 && e3 < 1e-3;
    if (!ok) ++g_failures;
    std::printf("%-34s identities %s (%.1e %.1e %.1e)\n", c.name.c_str(), ok ? "ok" : "BROKEN", e1,
                e2, e3);
  }
}

}  // namespace

TEST_CASE("PathOps probe (M8)", "[.][pathops-probe]") {
  const double pi = std::numbers::pi;
  std::vector<Case> cases;
  // Two squares side by side, sharing an edge exactly.
  cases.push_back({"squares sharing an edge", Rect(0, 0, 100, 100), Rect(100, 0, 100, 100),
                   std::array<double, 4>{20000, 0, 10000, 20000}});
  // Squares overlapping along part of an edge.
  cases.push_back({"squares, collinear edges", Rect(0, 0, 100, 100), Rect(50, 0, 100, 100),
                   std::array<double, 4>{15000, 5000, 5000, 10000}});
  // Touching at one corner only.
  cases.push_back({"squares touching at a corner", Rect(0, 0, 100, 100), Rect(100, 100, 100, 100),
                   std::array<double, 4>{20000, 0, 10000, 20000}});
  // The same square twice.
  cases.push_back({"identical squares", Rect(0, 0, 100, 100), Rect(0, 0, 100, 100),
                   std::array<double, 4>{10000, 10000, 0, 0}});
  // The same circle twice.
  cases.push_back({"identical circles", Circle(0, 0, 100), Circle(0, 0, 100), std::nullopt});
  // Overlapping circles (the classic lens).
  cases.push_back({"overlapping circles", Circle(0, 0, 100), Circle(100, 0, 100), std::nullopt});
  // Circles touching from outside and from inside.
  cases.push_back(
      {"circles touching outside", Circle(0, 0, 100), Circle(200, 0, 100), std::nullopt});
  cases.push_back({"circles touching inside", Circle(0, 0, 100), Circle(50, 0, 50), std::nullopt});
  // Nearly touching: a gap of 1e-4 pt.
  cases.push_back(
      {"circles 1e-4 apart", Circle(0, 0, 100), Circle(200.0001, 0, 100), std::nullopt});
  // Concentric circles: a ring.
  cases.push_back({"concentric circles", Circle(0, 0, 100), Circle(0, 0, 50), std::nullopt});
  // A square and the same square turned by a tiny angle (nearly parallel
  // edges).
  cases.push_back({"square rotated 0.001 deg", Rect(0, 0, 100, 100),
                   Rotated(Rect(0, 0, 100, 100), 0.001, 50, 50), std::nullopt});
  cases.push_back({"square rotated 1e-6 deg", Rect(0, 0, 100, 100),
                   Rotated(Rect(0, 0, 100, 100), 1e-6, 50, 50), std::nullopt});
  // A circle with a square whose edge is tangent to it.
  cases.push_back(
      {"circle with tangent square", Circle(0, 0, 100), Rect(100, -50, 100, 100), std::nullopt});
  // Self-intersecting: a five-pointed star drawn in one stroke (nonzero
  // fills the middle), and a figure eight.
  {
    std::vector<std::pair<double, double>> star;
    for (int i = 0; i < 5; ++i) {
      const double a = -pi / 2 + i * 4 * pi / 5;
      star.push_back({100 * std::cos(a), 100 * std::sin(a)});
    }
    cases.push_back(
        {"self-crossing star vs circle", Polygon(star), Circle(0, 0, 50), std::nullopt});
    SkPathBuilder eight;
    eight.moveTo(0, 0);
    eight.cubicTo(100, -100, 100, 100, 0, 0);
    eight.cubicTo(-100, -100, -100, 100, 0, 0);
    eight.close();
    cases.push_back(
        {"figure eight vs square", eight.detach(), Rect(-20, -20, 40, 40), std::nullopt});
  }
  // A cubic with a loop.
  {
    SkPathBuilder loop;
    loop.moveTo(0, 0);
    loop.cubicTo(200, 100, -100, 100, 100, 0);
    loop.close();
    cases.push_back({"looping cubic vs circle", loop.detach(), Circle(50, 30, 30), std::nullopt});
  }
  // Tiny and huge.
  cases.push_back(
      {"tiny circles (r 0.01)", Circle(0, 0, 0.01), Circle(0.01, 0, 0.01), std::nullopt});
  cases.push_back({"huge circles (r 1e5)", Circle(0, 0, 1e5), Circle(1e5, 0, 1e5), std::nullopt});
  // Far from the origin: float precision at 1e6 is 1/16 pt.
  cases.push_back(
      {"far away (1e6), r 10", Circle(1e6, 1e6, 10), Circle(1e6 + 10, 1e6, 10), std::nullopt});
  cases.push_back(
      {"far away (1e6), r 0.1", Circle(1e6, 1e6, 0.1), Circle(1e6 + 0.1, 1e6, 0.1), std::nullopt});
  // Many circles: the union of 60 overlapping circles against one more.
  {
    SkPathBuilder many;
    for (int i = 0; i < 60; ++i) {
      AddCircle(many, 40 * std::cos(i * 0.7) + i, 40 * std::sin(i * 1.3), 15 + (i % 7) * 3,
                i % 2 == 0);
    }
    SkPath merged = *Simplify(many.detach());
    cases.push_back({"60 circles vs circle", merged, Circle(30, 0, 45), std::nullopt});
  }
  // Grid of thin rectangles crossing a circle (many intersections).
  {
    SkPathBuilder bars;
    for (int i = 0; i < 40; ++i) {
      bars.addRect(SkRect::MakeXYWH(-100 + i * 5.0f, -120, 2.5f, 240));
    }
    cases.push_back({"40 thin bars vs circle", bars.detach(), Circle(0, 0, 100), std::nullopt});
  }

  for (const Case& c : cases) Run(c);
  std::printf("problems: %d\n", g_failures);
  SUCCEED();
}
