// SPDX-License-Identifier: GPL-3.0-or-later
// Smart guides: snapping to other objects' anchors and centres, and
// alignment with them (spec 4.2, M4).
#include <cmath>
#include <functional>
#include <variant>

#include "editor/editor.h"
#include "editor/tool_math.h"
#include "geometry/bezier.h"

namespace leinwand::editor {

using core::Matrix;
using core::Point;

const std::vector<Editor::SnapPoint>& Editor::SnapPoints() const {
  const core::Document& document = history_.current().document;
  if (snap_source_ && snap_source_->layers == document.layers) return snap_points_;
  snap_source_ = document;
  snap_points_.clear();

  std::function<void(const core::ObjectPtr&, const Matrix&, const std::string&)> visit =
      [&](const core::ObjectPtr& object, const Matrix& to_document, const std::string& top) {
        const auto& common = core::CommonOf(*object);
        if (!common.visible || common.locked) return;
        if (const auto* group = std::get_if<core::GroupObject>(object.get())) {
          for (const auto& child : group->children) {
            visit(child, to_document * group->transform, top);
          }
          return;
        }
        for (const auto& path : core::OutlineOf(*object)) {
          for (const auto& a : path.anchors) {
            snap_points_.push_back({to_document.Map(a.position), top});
          }
        }
        const core::Rect box = geometry::MapRect(geometry::Bounds(*object), to_document);
        if (box.IsValid()) {
          snap_points_.push_back({{(box.left + box.right) / 2, (box.top + box.bottom) / 2}, top});
        }
      };
  std::function<void(const core::Layer&)> visit_layer = [&](const core::Layer& layer) {
    if (!layer.visible || layer.locked) return;
    for (const auto& child : layer.children) {
      if (const auto* object = std::get_if<core::ObjectPtr>(&child)) {
        visit(*object, Matrix{}, core::CommonOf(**object).id);
      } else {
        visit_layer(*std::get<core::LayerPtr>(child));
      }
    }
  };
  for (const auto& layer : document.layers) visit_layer(*layer);
  return snap_points_;
}

Point Editor::Snap(Point p, double pick, const core::IdSet& exclude, Guides* guides) const {
  const SnapPoint* nearest = nullptr;
  const SnapPoint* align_x = nullptr;  // Same x: a vertical guide.
  const SnapPoint* align_y = nullptr;
  double best = pick, best_x = pick, best_y = pick;
  for (const SnapPoint& s : SnapPoints()) {
    if (exclude.contains(s.id)) continue;
    const double d = Distance(p, s.point);
    if (d <= best) {
      best = d;
      nearest = &s;
    }
    if (const double dx = std::abs(p.x - s.point.x); dx < best_x) {
      best_x = dx;
      align_x = &s;
    }
    if (const double dy = std::abs(p.y - s.point.y); dy < best_y) {
      best_y = dy;
      align_y = &s;
    }
  }
  // On an anchor or centre: that point, without guide lines.
  if (nearest) return nearest->point;
  Point snapped = p;
  if (align_x) snapped.x = align_x->point.x;
  if (align_y) snapped.y = align_y->point.y;
  if (guides) {
    if (align_x) guides->push_back({align_x->point, snapped});
    if (align_y) guides->push_back({align_y->point, snapped});
  }
  return snapped;
}

Point Editor::SnapDrag(Point p) {
  guides_.clear();
  if (!smart_guides_) return p;
  core::IdSet exclude;
  switch (drag_.kind) {
    case DragKind::kDraw:
      break;
    case DragKind::kPending:
    case DragKind::kMove:
      exclude = selection();
      break;
    case DragKind::kDirectPending:
    case DragKind::kMoveAnchors:
      for (const auto& a : anchors_) exclude.insert(a.id);
      break;
    default:
      return p;
  }
  // The grabbed point (an anchor, or the pointer) is what snaps.
  const Point offset = drag_.grab - drag_.start;
  return Snap(p + offset, drag_.pick * snap_scale_, exclude, &guides_) - offset;
}

Point Editor::SnapPlaced(Point p, double pick) {
  guides_.clear();
  return smart_guides_ ? Snap(p, pick * snap_scale_, {}, &guides_) : p;
}

}  // namespace leinwand::editor
