// SPDX-License-Identifier: GPL-3.0-or-later
// Basic value types. Units are points (1/72 in), origin top-left, Y down.
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace leinwand::core {

struct Point {
  double x = 0.0;
  double y = 0.0;

  friend Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
  friend Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
  friend Point operator*(Point a, double s) { return {a.x * s, a.y * s}; }
  friend Point operator*(double s, Point a) { return a * s; }
  friend bool operator==(Point a, Point b) = default;
};

// Axis-aligned rectangle. Empty when either side is negative or zero; the
// default value is "nothing", which Union treats as the identity.
struct Rect {
  double left = 0.0;
  double top = 0.0;
  double right = -1.0;
  double bottom = -1.0;

  static Rect FromXYWH(double x, double y, double w, double h) { return {x, y, x + w, y + h}; }
  static Rect FromPoint(Point p) { return {p.x, p.y, p.x, p.y}; }

  double width() const { return right - left; }
  double height() const { return bottom - top; }
  // A single point or a line still counts as a valid (degenerate) rect.
  bool IsValid() const { return right >= left && bottom >= top; }
  bool Contains(Point p) const {
    return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
  }
  bool Intersects(const Rect& o) const {
    return IsValid() && o.IsValid() && left <= o.right && o.left <= right && top <= o.bottom &&
           o.top <= bottom;
  }
  Rect Union(const Rect& o) const {
    if (!IsValid()) return o;
    if (!o.IsValid()) return *this;
    return {std::min(left, o.left), std::min(top, o.top), std::max(right, o.right),
            std::max(bottom, o.bottom)};
  }
  Rect Union(Point p) const { return Union(FromPoint(p)); }
  Rect Outset(double d) const { return {left - d, top - d, right + d, bottom + d}; }

  friend bool operator==(const Rect&, const Rect&) = default;
};

// 2D affine transform: x' = a*x + c*y + e, y' = b*x + d*y + f.
struct Matrix {
  double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;

  static Matrix Translate(double tx, double ty) { return {1, 0, 0, 1, tx, ty}; }
  static Matrix Scale(double sx, double sy) { return {sx, 0, 0, sy, 0, 0}; }
  static Matrix Rotate(double radians) {
    const double c = std::cos(radians), s = std::sin(radians);
    return {c, s, -s, c, 0, 0};
  }

  bool IsIdentity() const { return *this == Matrix{}; }
  double Determinant() const { return a * d - b * c; }
  // The inverse; a singular matrix (zero determinant) yields std::nullopt.
  std::optional<Matrix> Inverted() const {
    const double det = Determinant();
    if (det == 0.0 || !std::isfinite(det)) return std::nullopt;
    return Matrix{
        d / det, -b / det, -c / det, a / det, (c * f - d * e) / det, (b * e - a * f) / det};
  }
  Point Map(Point p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  // Maps a direction (no translation), e.g. a handle offset.
  Point MapVector(Point v) const { return {a * v.x + c * v.y, b * v.x + d * v.y}; }

  // (this * o) applies o first, then this.
  Matrix operator*(const Matrix& o) const {
    return {a * o.a + c * o.b, b * o.a + d * o.b,     a * o.c + c * o.d,
            b * o.c + d * o.d, a * o.e + c * o.f + e, b * o.e + d * o.f + f};
  }

  friend bool operator==(const Matrix&, const Matrix&) = default;
};

}  // namespace leinwand::core
