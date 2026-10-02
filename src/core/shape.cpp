// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/shape.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace leinwand::core {

namespace {

constexpr double kPi = std::numbers::pi;

double Length(Point v) { return std::hypot(v.x, v.y); }
Point Normalized(Point v) {
  const double l = Length(v);
  return l > 0 ? v * (1.0 / l) : Point{};
}
double Dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }

// Handle length for a circular arc of `sweep` radians and radius `r`.
double ArcHandle(double sweep, double r) { return 4.0 / 3.0 * std::tan(sweep / 4) * r; }

Anchor At(Point p) { return {p, {}, {}, AnchorKind::kCorner}; }

PathData Ellipse(const EllipseShape& e) {
  const double rx = e.width / 2, ry = e.height / 2;
  // Counter-clockwise on screen: y grows downwards, so negate the sine.
  auto point = [&](double a) { return Point{rx * std::cos(a), -ry * std::sin(a)}; };
  auto tangent = [&](double a) { return Point{-rx * std::sin(a), -ry * std::cos(a)}; };

  double start = e.pie_start * kPi / 180, end = e.pie_end * kPi / 180;
  double sweep = std::fmod(end - start, 2 * kPi);
  if (sweep < 0) sweep += 2 * kPi;
  const bool full = sweep < 1e-9;
  if (full) sweep = 2 * kPi;

  const int pieces = std::max(1, static_cast<int>(std::ceil(sweep / (kPi / 2) - 1e-9)));
  const double step = sweep / pieces;
  const double k = 4.0 / 3.0 * std::tan(step / 4);

  PathData path;
  path.closed = true;
  for (int i = 0; i <= pieces; ++i) {
    if (full && i == pieces) break;  // The closing segment returns to the start.
    const double a = start + i * step;
    Anchor anchor = At(point(a));
    anchor.kind = AnchorKind::kSmooth;
    if (full || i > 0) anchor.handle_in = tangent(a) * -k;
    if (full || i < pieces) anchor.handle_out = tangent(a) * k;
    path.anchors.push_back(anchor);
  }
  if (!full) {
    path.anchors.front().kind = AnchorKind::kCorner;
    path.anchors.back().kind = AnchorKind::kCorner;
    path.anchors.push_back(At({0, 0}));  // The pie's centre.
  }
  return path;
}

std::vector<Point> RegularVertices(int count, double radius, double inner_radius = -1) {
  std::vector<Point> vertices;
  const int n = inner_radius >= 0 ? count * 2 : count;
  for (int i = 0; i < n; ++i) {
    const double r = (inner_radius >= 0 && i % 2 == 1) ? inner_radius : radius;
    const double a = -kPi / 2 + i * 2 * kPi / n;  // Start pointing up, go clockwise.
    vertices.push_back({r * std::cos(a), r * std::sin(a)});
  }
  return vertices;
}

}  // namespace

PathData PolygonWithCorners(const std::vector<Point>& vertices,
                            const std::vector<Corner>& corners) {
  PathData path;
  path.closed = true;
  const int n = static_cast<int>(vertices.size());
  for (int i = 0; i < n; ++i) {
    const Point v = vertices[i];
    const Point prev = vertices[(i + n - 1) % n];
    const Point next = vertices[(i + 1) % n];
    const Corner corner = i < static_cast<int>(corners.size()) ? corners[i] : Corner{};
    const Point u1 = Normalized(prev - v), u2 = Normalized(next - v);
    const double angle = std::acos(std::clamp(Dot(u1, u2), -1.0, 1.0));  // Interior angle.
    if (corner.radius <= 0 || angle < 1e-6 || angle > kPi - 1e-6) {
      path.anchors.push_back(At(v));
      continue;
    }
    // Distance from the vertex to where the corner starts on each edge; no
    // corner may take more than half an edge.
    const double limit = std::min(Length(prev - v), Length(next - v)) / 2;
    double distance =
        corner.kind == CornerKind::kRound ? corner.radius / std::tan(angle / 2) : corner.radius;
    distance = std::min(distance, limit);
    const Point entry = v + u1 * distance, exit = v + u2 * distance;
    Anchor a = At(entry), b = At(exit);
    switch (corner.kind) {
      case CornerKind::kRound: {
        const double r = distance * std::tan(angle / 2);
        const double h = ArcHandle(kPi - angle, r);
        a.handle_out = u1 * -h;  // Towards the vertex along the edge.
        b.handle_in = u2 * -h;
        break;
      }
      case CornerKind::kInvertedRound: {
        // An arc centred on the vertex, curving into the shape.
        const double h = ArcHandle(angle, distance);
        a.handle_out = Normalized(u2 - u1 * Dot(u2, u1)) * h;
        b.handle_in = Normalized(u1 - u2 * Dot(u1, u2)) * h;
        break;
      }
      case CornerKind::kChamfer:
        break;
    }
    path.anchors.push_back(a);
    path.anchors.push_back(b);
  }
  return path;
}

PathData ShapePath(const ShapeParams& shape) {
  return std::visit(
      [](const auto& s) -> PathData {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, RectangleShape>) {
          const double w = s.width / 2, h = s.height / 2;
          return PolygonWithCorners({{-w, -h}, {w, -h}, {w, h}, {-w, h}},
                                    {s.corners.begin(), s.corners.end()});
        } else if constexpr (std::is_same_v<T, EllipseShape>) {
          return Ellipse(s);
        } else if constexpr (std::is_same_v<T, PolygonShape>) {
          const int sides = std::max(3, s.sides);
          return PolygonWithCorners(RegularVertices(sides, s.radius),
                                    std::vector<Corner>(sides, {s.corner_radius}));
        } else if constexpr (std::is_same_v<T, StarShape>) {
          return PolygonWithCorners(
              RegularVertices(std::max(3, s.points), s.outer_radius, s.inner_radius), {});
        } else {
          PathData line;
          line.anchors = {At({-s.length / 2, 0}), At({s.length / 2, 0})};
          return line;
        }
      },
      shape);
}

}  // namespace leinwand::core
