// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/path_ops.h"

#include <cmath>
#include <cstddef>

#include "geometry/hit_test.h"

namespace leinwand::geometry {

namespace {

using core::Point;

// Twice the signed area of a closed polygon. Measured from its first point,
// so that small shapes far from the origin keep their precision.
double SignedArea2(const std::vector<Point>& polygon) {
  if (polygon.empty()) return 0;
  const Point o = polygon[0];
  double sum = 0;
  for (size_t i = 0, n = polygon.size(); i < n; ++i) {
    const Point a = polygon[i] - o, b = polygon[(i + 1) % n] - o;
    sum += a.x * b.y - b.x * a.y;
  }
  return sum;
}

// Even-odd containment in a closed polygon.
bool Inside(const std::vector<Point>& polygon, Point p) {
  bool inside = false;
  for (size_t i = 0, n = polygon.size(), j = n - 1; i < n; j = i++) {
    const Point a = polygon[i], b = polygon[j];
    if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

// A point of the contour away from its vertices (the middle of its longest
// edge), so that contours touching at a vertex do not count as containing.
Point Probe(const std::vector<Point>& polygon) {
  size_t best = 0;
  double longest = -1;
  for (size_t i = 0, n = polygon.size(); i < n; ++i) {
    const Point a = polygon[i], b = polygon[(i + 1) % n];
    const double length = std::hypot(b.x - a.x, b.y - a.y);
    if (length > longest) {
      longest = length;
      best = i;
    }
  }
  const Point a = polygon[best], b = polygon[(best + 1) % polygon.size()];
  return {(a.x + b.x) / 2, (a.y + b.y) / 2};
}

struct Contour {
  std::vector<Point> polygon;
  int depth = 0;    // How many other contours contain this one.
  int parent = -1;  // The innermost contour containing this one.
};

// Nesting of the region's contours. Assumes contours do not cross (as in
// an engine's result); touching at points is fine.
std::vector<Contour> Nest(const Region& region, double tolerance) {
  std::vector<Contour> contours;
  for (const core::PathData& path : region.subpaths) {
    Contour c;
    c.polygon = Flatten(path, tolerance);
    if (c.polygon.size() >= 3) contours.push_back(std::move(c));
  }
  for (size_t i = 0; i < contours.size(); ++i) {
    const Point probe = Probe(contours[i].polygon);
    double parent_area = 0;
    for (size_t j = 0; j < contours.size(); ++j) {
      if (i == j || !Inside(contours[j].polygon, probe)) continue;
      ++contours[i].depth;
      const double area = std::abs(SignedArea2(contours[j].polygon));
      if (contours[i].parent < 0 || area < parent_area) {
        contours[i].parent = int(j);
        parent_area = area;
      }
    }
  }
  return contours;
}

}  // namespace

std::vector<Region> SplitIntoPieces(const Region& region) {
  std::vector<Contour> contours = Nest(region, 0.05);
  // Nest drops degenerate contours, so map back by flattening order.
  std::vector<size_t> source;
  for (size_t i = 0; i < region.subpaths.size(); ++i) {
    if (Flatten(region.subpaths[i], 0.05).size() >= 3) source.push_back(i);
  }
  std::vector<Region> pieces;
  std::vector<int> piece_of(contours.size(), -1);
  for (size_t i = 0; i < contours.size(); ++i) {
    if (contours[i].depth % 2 != 0) continue;  // A hole.
    piece_of[i] = int(pieces.size());
    pieces.push_back({{region.subpaths[source[i]]}, region.fill_rule});
  }
  for (size_t i = 0; i < contours.size(); ++i) {
    if (contours[i].depth % 2 == 0 || contours[i].parent < 0) continue;
    const int piece = piece_of[size_t(contours[i].parent)];
    if (piece >= 0) pieces[size_t(piece)].subpaths.push_back(region.subpaths[source[i]]);
  }
  return pieces;
}

double Area(const Region& region, double tolerance) {
  double area = 0;
  for (const Contour& c : Nest(region, tolerance)) {
    const double a = std::abs(SignedArea2(c.polygon)) / 2;
    area += c.depth % 2 == 0 ? a : -a;
  }
  return area;
}

}  // namespace leinwand::geometry
