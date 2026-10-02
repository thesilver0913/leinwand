// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/path_edit.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace leinwand::geometry {

using core::Anchor;
using core::AnchorKind;
using core::PathData;
using core::Point;

namespace {

double Length(Point v) { return std::hypot(v.x, v.y); }

Point Lerp(Point a, Point b, double t) { return a + (b - a) * t; }

bool IsZero(Point v) { return v.x == 0 && v.y == 0; }

int Wrap(const PathData& path, int index) {
  const int n = static_cast<int>(path.anchors.size());
  return ((index % n) + n) % n;
}

}  // namespace

std::pair<CubicBezier, CubicBezier> Split(const CubicBezier& c, double t) {
  const Point a = Lerp(c.p0, c.p1, t), b = Lerp(c.p1, c.p2, t), d = Lerp(c.p2, c.p3, t);
  const Point e = Lerp(a, b, t), f = Lerp(b, d, t);
  const Point m = Lerp(e, f, t);
  return {{c.p0, a, e, m}, {m, f, d, c.p3}};
}

std::optional<NearestOnSegment> NearestSegment(const PathData& path, Point p) {
  std::optional<NearestOnSegment> best;
  for (int s = 0; s < path.segment_count(); ++s) {
    const CubicBezier c = SegmentAt(path, s);
    // Coarse sampling, then refine around the best sample.
    constexpr int kSamples = 48;
    double best_t = 0, best_d = std::numeric_limits<double>::infinity();
    for (int i = 0; i <= kSamples; ++i) {
      const double t = static_cast<double>(i) / kSamples;
      const double d = Length(c.Evaluate(t) - p);
      if (d < best_d) {
        best_d = d;
        best_t = t;
      }
    }
    double step = 1.0 / kSamples;
    for (int iteration = 0; iteration < 30; ++iteration) {
      step /= 2;
      for (double t : {best_t - step, best_t + step}) {
        if (t < 0 || t > 1) continue;
        const double d = Length(c.Evaluate(t) - p);
        if (d < best_d) {
          best_d = d;
          best_t = t;
        }
      }
    }
    if (!best || best_d < best->distance) best = NearestOnSegment{s, best_t, best_d};
  }
  return best;
}

int InsertAnchor(PathData& path, int segment, double t) {
  const int from = segment, to = Wrap(path, segment + 1);
  const CubicBezier c = SegmentAt(path, segment);
  Anchor inserted;
  if (c.p1 == c.p0 && c.p2 == c.p3) {
    inserted.position = c.Evaluate(t);  // A straight segment stays straight.
  } else {
    const auto [left, right] = Split(c, t);
    path.anchors[from].handle_out = left.p1 - left.p0;
    path.anchors[to].handle_in = right.p2 - right.p3;
    inserted.position = left.p3;
    inserted.handle_in = left.p2 - left.p3;
    inserted.handle_out = right.p1 - right.p0;
    inserted.kind = AnchorKind::kSmooth;
  }
  path.anchors.insert(path.anchors.begin() + segment + 1, inserted);
  return segment + 1;
}

void RemoveAnchor(PathData& path, int index) {
  path.anchors.erase(path.anchors.begin() + index);
  if (path.anchors.size() < 2) path.closed = false;
}

void MakeCorner(PathData& path, int index) {
  Anchor& a = path.anchors[index];
  a.handle_in = a.handle_out = {};
  a.kind = AnchorKind::kCorner;
}

void MakeSmooth(PathData& path, int index) {
  const int n = static_cast<int>(path.anchors.size());
  if (n < 2) return;
  Anchor& a = path.anchors[index];
  const bool has_prev = path.closed || index > 0, has_next = path.closed || index < n - 1;
  const Point prev = has_prev ? path.anchors[Wrap(path, index - 1)].position : a.position;
  const Point next = has_next ? path.anchors[Wrap(path, index + 1)].position : a.position;
  const Point direction = next - prev;
  const double length = Length(direction);
  if (length == 0) return;
  const Point unit = direction * (1.0 / length);
  // Endpoints use the one neighbour for both handles' length.
  const double in = Length(a.position - prev), out = Length(next - a.position);
  a.handle_out = unit * ((has_next ? out : in) / 3);
  a.handle_in = unit * (-(has_prev ? in : out) / 3);
  a.kind = AnchorKind::kSmooth;
}

void MoveAnchor(PathData& path, int index, Point position) {
  path.anchors[index].position = position;
}

void MoveHandle(PathData& path, int index, Side side, Point to, bool split) {
  Anchor& a = path.anchors[index];
  Point& moved = side == Side::kIn ? a.handle_in : a.handle_out;
  Point& other = side == Side::kIn ? a.handle_out : a.handle_in;
  moved = to - a.position;
  if (split) {
    a.kind = AnchorKind::kCorner;
    return;
  }
  const double length = Length(moved);
  if (a.kind == AnchorKind::kSymmetric) {
    other = moved * -1.0;
  } else if (a.kind == AnchorKind::kSmooth && length > 0 && !IsZero(other)) {
    other = moved * (-Length(other) / length);  // Same line, own length.
  }
}

void DragSegment(PathData& path, int segment, double t, Point to) {
  t = std::clamp(t, 0.05, 0.95);  // Near the ends the handles would explode.
  const CubicBezier c = SegmentAt(path, segment);
  const Point delta = to - c.Evaluate(t);
  // Move p1 and p2 by delta*(1-t)*k and delta*t*k so B(t) moves by delta.
  const double k = 1.0 / (3 * t * (1 - t) * ((1 - t) * (1 - t) + t * t));
  const Point p1 = c.p1 + delta * ((1 - t) * k), p2 = c.p2 + delta * (t * k);
  const int next = Wrap(path, segment + 1);
  MoveHandle(path, segment, Side::kOut, p1);
  MoveHandle(path, next, Side::kIn, p2);
}

std::vector<PathData> CutAt(const PathData& path, int index) {
  const int n = static_cast<int>(path.anchors.size());
  if (path.closed) {
    PathData open;
    for (int i = 0; i <= n; ++i) open.anchors.push_back(path.anchors[(index + i) % n]);
    open.closed = false;
    return {open};
  }
  if (index <= 0 || index >= n - 1) return {path};  // Already an end.
  PathData first, second;
  first.anchors.assign(path.anchors.begin(), path.anchors.begin() + index + 1);
  second.anchors.assign(path.anchors.begin() + index, path.anchors.end());
  return {first, second};
}

std::vector<PathData> DeleteAnchors(const PathData& path, const std::set<int>& indices) {
  const int n = static_cast<int>(path.anchors.size());
  auto deleted = [&](int i) { return indices.contains(i); };
  int start = 0;
  if (path.closed) {
    // Start right after a deleted anchor so no run wraps around the end.
    int first = -1;
    for (int i = 0; i < n && first < 0; ++i) {
      if (deleted(i)) first = i;
    }
    if (first < 0) return {path};
    start = first + 1;
  }
  std::vector<PathData> pieces;
  PathData run;
  auto flush = [&]() {
    if (run.anchors.size() >= 2) pieces.push_back(run);
    run.anchors.clear();
  };
  for (int k = 0; k < n; ++k) {
    const int i = (start + k) % n;
    if (deleted(i)) {
      flush();
    } else {
      run.anchors.push_back(path.anchors[i]);
    }
  }
  flush();
  return pieces;
}

PathData Reversed(const PathData& path) {
  PathData result = path;
  std::reverse(result.anchors.begin(), result.anchors.end());
  for (Anchor& a : result.anchors) std::swap(a.handle_in, a.handle_out);
  return result;
}

PathData Join(const PathData& a, bool a_end, const PathData& b, bool b_end) {
  PathData result = a_end ? a : Reversed(a);
  const PathData tail = b_end ? Reversed(b) : b;
  result.closed = false;
  auto begin = tail.anchors.begin();
  if (!result.anchors.empty() && !tail.anchors.empty() &&
      Length(result.anchors.back().position - tail.anchors.front().position) < 1e-6) {
    // One anchor where the ends meet: in from a, out from b.
    Anchor& joint = result.anchors.back();
    joint.handle_out = tail.anchors.front().handle_out;
    joint.kind = AnchorKind::kCorner;
    ++begin;
  }
  result.anchors.insert(result.anchors.end(), begin, tail.anchors.end());
  return result;
}

}  // namespace leinwand::geometry
