// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/gradient.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace leinwand::core {

namespace {

void Sort(Gradient& gradient) {
  std::stable_sort(
      gradient.stops.begin(), gradient.stops.end(),
      [](const GradientStop& a, const GradientStop& b) { return a.offset < b.offset; });
}

}  // namespace

Gradient DefaultGradient(GradientType type, const Rect& bounds, const Color& from,
                         const Color& to) {
  Gradient gradient;
  gradient.type = type;
  gradient.stops = {{0.0, from, 1.0, 0.5}, {1.0, to, 1.0, 0.5}};
  const Point centre{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
  if (type == GradientType::kLinear) {
    gradient.start = {bounds.left, centre.y};
    gradient.end = {bounds.right, centre.y};
  } else {
    gradient.start = centre;
    gradient.end = {centre.x + std::max(bounds.width(), bounds.height()) / 2, centre.y};
  }
  return gradient;
}

double GradientAngle(const Gradient& gradient) {
  const Point d = gradient.end - gradient.start;
  if (d.x == 0 && d.y == 0) return 0;
  return -std::atan2(d.y, d.x) * 180 / std::numbers::pi;
}

Gradient WithAngle(Gradient gradient, double degrees) {
  const Point d = gradient.end - gradient.start;
  const double length = std::hypot(d.x, d.y);
  const double radians = -degrees * std::numbers::pi / 180;
  gradient.end = gradient.start + Point{std::cos(radians), std::sin(radians)} * length;
  return gradient;
}

Gradient Transformed(const Gradient& gradient, const Matrix& matrix) {
  Gradient result = gradient;
  result.start = matrix.Map(gradient.start);
  result.end = matrix.Map(gradient.end);
  if (gradient.focal) result.focal = matrix.Map(*gradient.focal);
  return result;
}

int AddStop(Gradient& gradient, double offset) {
  offset = std::clamp(offset, 0.0, 1.0);
  Sort(gradient);
  GradientStop stop;
  stop.offset = offset;
  // The stops on either side of the new one.
  const GradientStop* before = nullptr;
  const GradientStop* after = nullptr;
  for (const GradientStop& s : gradient.stops) {
    if (s.offset <= offset) before = &s;
    if (s.offset >= offset && !after) after = &s;
  }
  if (before) {
    stop.color = before->color;
    stop.opacity = before->opacity;
  } else if (after) {
    stop.color = after->color;
    stop.opacity = after->opacity;
  }
  if (before && after && before != after) {
    const auto* a = std::get_if<RgbColor>(&before->color);
    const auto* b = std::get_if<RgbColor>(&after->color);
    const double t = (offset - before->offset) / std::max(1e-9, after->offset - before->offset);
    if (a && b)
      stop.color =
          RgbColor{a->r + (b->r - a->r) * t, a->g + (b->g - a->g) * t, a->b + (b->b - a->b) * t};
    stop.opacity = before->opacity + (after->opacity - before->opacity) * t;
  }
  gradient.stops.push_back(stop);
  Sort(gradient);
  for (int i = static_cast<int>(gradient.stops.size()) - 1; i >= 0; --i) {
    if (gradient.stops[size_t(i)].offset == offset) return i;
  }
  return 0;
}

void RemoveStop(Gradient& gradient, int index) {
  if (gradient.stops.size() <= 2 || index < 0 || index >= static_cast<int>(gradient.stops.size())) {
    return;
  }
  gradient.stops.erase(gradient.stops.begin() + index);
}

int MoveStop(Gradient& gradient, int index, double offset) {
  if (index < 0 || index >= static_cast<int>(gradient.stops.size())) return index;
  GradientStop moved = gradient.stops[size_t(index)];
  moved.offset = std::clamp(offset, 0.0, 1.0);
  gradient.stops.erase(gradient.stops.begin() + index);
  const auto at =
      std::upper_bound(gradient.stops.begin(), gradient.stops.end(), moved.offset,
                       [](double value, const GradientStop& s) { return value < s.offset; });
  const int result = static_cast<int>(at - gradient.stops.begin());
  gradient.stops.insert(at, moved);
  return result;
}

}  // namespace leinwand::core
