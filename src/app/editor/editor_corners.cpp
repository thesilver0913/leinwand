// SPDX-License-Identifier: GPL-3.0-or-later
// Live corners (spec 10, phase 2): with one live rectangle or polygon
// selected, the selection tool shows a widget inside each corner. Dragging
// one sets the radius of every corner; Alt-clicking one cycles the corner
// type (round, inverted round, chamfer), as in Illustrator.
#include <algorithm>
#include <cmath>
#include <numbers>

#include "editor/editor.h"
#include "editor/tool_math.h"

namespace leinwand::editor {

namespace {

using core::Matrix;
using core::Point;

double Length(Point p) { return std::hypot(p.x, p.y); }
double Dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }

Point Normalized(Point p) {
  const double l = Length(p);
  return l > 0 ? p * (1.0 / l) : Point{};
}

}  // namespace

// The corners of the single selected live shape, in its own coordinates:
// where each corner is, the way into the shape along its bisector, how far
// the arc's centre lies per unit of radius, and the radius now.
std::vector<Editor::CornerRef> Editor::Corners(Matrix* to_document) const {
  const core::ShapeObject* shape = SingleShape();
  if (!shape) return {};
  const auto found = core::FindObjects(document(), selection());
  if (found.size() != 1) return {};
  if (to_document) *to_document = found[0].to_document * shape->transform;
  std::vector<CornerRef> corners;
  if (const auto* rect = std::get_if<core::RectangleShape>(&shape->shape)) {
    const double w = rect->width / 2, h = rect->height / 2;
    const Point at[] = {{-w, -h}, {w, -h}, {w, h}, {-w, h}};
    for (int i = 0; i < 4; ++i) {
      // Right angles: the bisector is the diagonal, whatever the proportions.
      const Point inward{at[i].x < 0 ? 1.0 : -1.0, at[i].y < 0 ? 1.0 : -1.0};
      corners.push_back({at[i], inward * (1 / std::numbers::sqrt2), std::numbers::sqrt2,
                         rect->corners[std::size_t(i)].radius});
    }
  } else if (const auto* polygon = std::get_if<core::PolygonShape>(&shape->shape)) {
    const int n = std::max(3, polygon->sides);
    // Half the interior angle: the arc's centre is r / sin(half) away.
    const double half = (std::numbers::pi - 2 * std::numbers::pi / n) / 2;
    for (int i = 0; i < n; ++i) {
      const double a = -std::numbers::pi / 2 + 2 * std::numbers::pi * i / n;
      const Point vertex{polygon->radius * std::cos(a), polygon->radius * std::sin(a)};
      corners.push_back(
          {vertex, Normalized(vertex * -1.0), 1 / std::sin(half), polygon->corner_radius});
    }
  }
  return corners;
}

std::vector<Point> Editor::CornerWidgets(double pick) const {
  Matrix m;
  const auto corners = Corners(&m);
  if (corners.empty()) return {};
  // Not on shapes too small to tell the widgets from the corners.
  const double scale = std::sqrt(std::abs(m.Determinant()));
  const double offset = 3.5 * pick / std::max(scale, 1e-9);
  const core::Rect box = *SelectionBounds();
  if (std::min(box.width(), box.height()) < 4 * 3.5 * pick) return {};
  std::vector<Point> widgets;
  for (const CornerRef& c : corners) {
    widgets.push_back(m.Map(c.at + c.inward * std::max(c.radius * c.factor, offset)));
  }
  return widgets;
}

std::optional<int> Editor::CornerWidgetAt(Point p, double pick) const {
  const auto widgets = CornerWidgets(pick);
  for (std::size_t i = 0; i < widgets.size(); ++i) {
    if (Distance(p, widgets[i]) <= pick * 1.5) return static_cast<int>(i);
  }
  return std::nullopt;
}

void Editor::CornerDown(int index, Modifiers modifiers) {
  const core::ShapeObject* shape = SingleShape();
  if (!shape) return;
  core::ShapeObject copy = *shape;
  if (modifiers.alt) {
    // Round, inverted round, chamfer, round again: every corner alike.
    auto next = [](core::CornerKind k) {
      return k == core::CornerKind::kRound           ? core::CornerKind::kInvertedRound
             : k == core::CornerKind::kInvertedRound ? core::CornerKind::kChamfer
                                                     : core::CornerKind::kRound;
    };
    if (auto* rect = std::get_if<core::RectangleShape>(&copy.shape)) {
      const core::CornerKind kind = next(rect->corners[std::size_t(index)].kind);
      for (auto& corner : rect->corners) corner.kind = kind;
    } else {
      return;  // Polygons have round corners only.
    }
    Commit("corner type",
           {core::ReplaceObjects(document(), {{copy.common.id, core::MakeObject(copy)}}),
            selection()});
    drag_ = {};
    return;
  }
  drag_.kind = DragKind::kCornerRadius;
  drag_.index = index;
}

void Editor::CornerDrag() {
  Matrix m;
  const auto corners = Corners(&m);
  if (drag_.index < 0 || drag_.index >= static_cast<int>(corners.size())) return;
  const auto inverse = m.Inverted();
  if (!inverse) return;
  const CornerRef& c = corners[std::size_t(drag_.index)];
  const Point local = inverse->Map(drag_.current);
  double radius = std::max(0.0, Dot(local - c.at, c.inward) / c.factor);
  const core::EditorState& base = history_.current();
  const auto found = core::FindObjects(base.document, base.selection);
  if (found.size() != 1) return;
  const auto* shape = std::get_if<core::ShapeObject>(found[0].object.get());
  if (!shape) return;
  core::ShapeObject copy = *shape;
  if (auto* rect = std::get_if<core::RectangleShape>(&copy.shape)) {
    radius = std::min(radius, std::min(rect->width, rect->height) / 2);
    for (auto& corner : rect->corners) corner.radius = radius;
  } else if (auto* polygon = std::get_if<core::PolygonShape>(&copy.shape)) {
    polygon->corner_radius = std::min(radius, polygon->radius);
  }
  drag_.preview = core::EditorState{
      core::ReplaceObjects(base.document, {{copy.common.id, core::MakeObject(copy)}}),
      base.selection};
}

}  // namespace leinwand::editor
