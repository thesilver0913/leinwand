// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor/editor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <utility>
#include <variant>

#include "geometry/bezier.h"
#include "geometry/hit_test.h"

namespace leinwand::editor {

using core::Matrix;
using core::Point;
using core::Rect;

namespace {

constexpr Handle kHandles[] = {Handle::kTopLeft,    Handle::kTop,         Handle::kTopRight,
                               Handle::kRight,      Handle::kBottomRight, Handle::kBottom,
                               Handle::kBottomLeft, Handle::kLeft};
constexpr Handle kCorners[] = {Handle::kTopLeft, Handle::kTopRight, Handle::kBottomRight,
                               Handle::kBottomLeft};

Handle Opposite(Handle h) { return static_cast<Handle>((static_cast<int>(h) + 4) % 8); }
bool MovesX(Handle h) { return h != Handle::kTop && h != Handle::kBottom; }
bool MovesY(Handle h) { return h != Handle::kLeft && h != Handle::kRight; }

double Distance(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }

Point Centre(const Rect& r) { return {(r.left + r.right) / 2, (r.top + r.bottom) / 2}; }

// Shift-drag: the nearest multiple of 45 degrees.
Point ConstrainTo45(Point d) {
  const double length = std::hypot(d.x, d.y);
  if (length == 0) return d;
  const double step = std::numbers::pi / 4;
  const double angle = std::round(std::atan2(d.y, d.x) / step) * step;
  // Project onto the snapped direction, as Illustrator does.
  const Point dir{std::cos(angle), std::sin(angle)};
  const double along = d.x * dir.x + d.y * dir.y;
  return dir * along;
}

core::IdSet TopLevelSelectable(const core::Document& document) {
  core::IdSet ids;
  std::function<void(const core::Layer&)> visit = [&](const core::Layer& layer) {
    if (!layer.visible || layer.locked) return;
    for (const auto& child : layer.children) {
      if (const auto* object = std::get_if<core::ObjectPtr>(&child)) {
        const auto& common = core::CommonOf(**object);
        if (common.visible && !common.locked) ids.insert(common.id);
      } else {
        visit(*std::get<core::LayerPtr>(child));
      }
    }
  };
  for (const auto& layer : document.layers) visit(*layer);
  return ids;
}

}  // namespace

Point HandlePosition(const Rect& box, Handle handle) {
  const Point c = Centre(box);
  switch (handle) {
    case Handle::kTopLeft:
      return {box.left, box.top};
    case Handle::kTop:
      return {c.x, box.top};
    case Handle::kTopRight:
      return {box.right, box.top};
    case Handle::kRight:
      return {box.right, c.y};
    case Handle::kBottomRight:
      return {box.right, box.bottom};
    case Handle::kBottom:
      return {c.x, box.bottom};
    case Handle::kBottomLeft:
      return {box.left, box.bottom};
    case Handle::kLeft:
      return {box.left, c.y};
  }
  return c;
}

Editor::Editor(core::Document document) : history_({std::move(document), {}}) {
  for (const auto& id : core::AllObjectIds(history_.current().document)) ids_.Reserve(id);
}

const core::Document& Editor::document() const {
  return drag_.preview ? drag_.preview->document : history_.current().document;
}

const core::IdSet& Editor::selection() const {
  return drag_.preview ? drag_.preview->selection : history_.current().selection;
}

std::optional<Rect> Editor::SelectionBounds() const {
  if (selection().empty()) return std::nullopt;
  Rect bounds;
  for (const auto& found : core::FindObjects(document(), selection())) {
    bounds = bounds.Union(geometry::MapRect(geometry::Bounds(*found.object), found.to_document));
  }
  if (!bounds.IsValid()) return std::nullopt;
  return bounds;
}

Overlay Editor::overlay() const {
  Overlay overlay;
  overlay.selection = selection();
  // The box hides while the selection is being transformed, as in Illustrator.
  if (drag_.kind == DragKind::kNone || drag_.kind == DragKind::kPending ||
      drag_.kind == DragKind::kMarquee) {
    overlay.bounding_box = SelectionBounds();
  }
  if (drag_.kind == DragKind::kMarquee) {
    overlay.marquee = Rect::FromPoint(drag_.start).Union(drag_.current);
  }
  return overlay;
}

std::optional<Handle> Editor::HandleAt(Point p, double pick) const {
  const auto box = SelectionBounds();
  if (!box) return std::nullopt;
  for (Handle h : kHandles) {
    if (Distance(p, HandlePosition(*box, h)) <= pick * 1.5) return h;
  }
  return std::nullopt;
}

std::optional<Handle> Editor::RotateZoneAt(Point p, double pick) const {
  const auto box = SelectionBounds();
  if (!box || box->Contains(p)) return std::nullopt;
  for (Handle h : kCorners) {
    const double d = Distance(p, HandlePosition(*box, h));
    if (d > pick * 1.5 && d <= pick * 5) return h;
  }
  return std::nullopt;
}

Hover Editor::HoverAt(Point p, double pick) const {
  if (const auto h = HandleAt(p, pick)) return {Hover::Kind::kHandle, *h};
  if (const auto h = RotateZoneAt(p, pick)) return {Hover::Kind::kRotate, *h};
  if (geometry::HitTest(document(), p, pick)) return {Hover::Kind::kObject};
  return {};
}

void Editor::SetSelection(core::IdSet selection) { history_.SetSelection(std::move(selection)); }

void Editor::Commit(const std::string& action, core::EditorState state) {
  history_.Push(action, std::move(state));
}

void Editor::PointerDown(Point p, Modifiers modifiers, double pick) {
  drag_ = {};
  drag_.start = drag_.current = p;
  drag_.threshold = pick / 2;
  if (!selection().empty()) {
    if (const auto h = HandleAt(p, pick)) {
      drag_.kind = DragKind::kScale;
      drag_.handle = *h;
      drag_.box = *SelectionBounds();
      return;
    }
    if (RotateZoneAt(p, pick)) {
      drag_.kind = DragKind::kRotate;
      drag_.box = *SelectionBounds();
      return;
    }
  }
  const auto hit = geometry::HitTest(document(), p, pick);
  if (!hit) {
    if (!modifiers.shift) SetSelection({});
    drag_.kind = DragKind::kMarquee;
    return;
  }
  core::IdSet selection = this->selection();
  if (modifiers.shift) {
    // Shift-click toggles; a deselected object is not dragged.
    if (selection.erase(hit->top_level_id) == 0) selection.insert(hit->top_level_id);
    SetSelection(selection);
    if (!selection.contains(hit->top_level_id)) return;
  } else if (!selection.contains(hit->top_level_id)) {
    SetSelection({hit->top_level_id});
  }
  drag_.kind = DragKind::kPending;
}

void Editor::PointerMove(Point p, Modifiers modifiers) {
  drag_.current = p;
  switch (drag_.kind) {
    case DragKind::kNone:
    case DragKind::kMarquee:
      return;
    case DragKind::kPending:
      if (Distance(p, drag_.start) < drag_.threshold) return;
      drag_.kind = DragKind::kMove;
      break;
    default:
      break;
  }
  UpdatePreview(modifiers);
}

void Editor::UpdatePreview(Modifiers modifiers) {
  const core::EditorState& base = history_.current();
  Matrix matrix;
  const Point p = drag_.current;
  switch (drag_.kind) {
    case DragKind::kMove: {
      const Point d = modifiers.shift ? ConstrainTo45(p - drag_.start) : p - drag_.start;
      matrix = Matrix::Translate(d.x, d.y);
      break;
    }
    case DragKind::kScale: {
      const Handle h = drag_.handle;
      const Point anchor =
          modifiers.alt ? Centre(drag_.box) : HandlePosition(drag_.box, Opposite(h));
      const Point from = HandlePosition(drag_.box, h);
      auto factor = [](double to, double from, double anchor) {
        const double span = from - anchor;
        return span == 0 ? 1.0 : (to - anchor) / span;
      };
      double sx = MovesX(h) ? factor(p.x, from.x, anchor.x) : 1.0;
      double sy = MovesY(h) ? factor(p.y, from.y, anchor.y) : 1.0;
      if (modifiers.shift) {
        // Proportional: the larger change wins; side handles drive both axes.
        const double s = !MovesX(h)   ? std::abs(sy)
                         : !MovesY(h) ? std::abs(sx)
                                      : std::max(std::abs(sx), std::abs(sy));
        sx = std::copysign(s, MovesX(h) ? sx : 1.0);
        sy = std::copysign(s, MovesY(h) ? sy : 1.0);
      }
      // Never collapse to zero; the objects could not be scaled back.
      auto nonzero = [](double s) { return std::abs(s) < 1e-4 ? std::copysign(1e-4, s) : s; };
      matrix = Matrix::Translate(anchor.x, anchor.y) * Matrix::Scale(nonzero(sx), nonzero(sy)) *
               Matrix::Translate(-anchor.x, -anchor.y);
      break;
    }
    case DragKind::kRotate: {
      const Point c = Centre(drag_.box);
      double angle =
          std::atan2(p.y - c.y, p.x - c.x) - std::atan2(drag_.start.y - c.y, drag_.start.x - c.x);
      if (modifiers.shift)
        angle = std::round(angle / (std::numbers::pi / 4)) * (std::numbers::pi / 4);
      matrix = Matrix::Translate(c.x, c.y) * Matrix::Rotate(angle) * Matrix::Translate(-c.x, -c.y);
      break;
    }
    default:
      return;
  }

  // Alt while moving drags a copy and leaves the originals in place.
  const bool copy = drag_.kind == DragKind::kMove && modifiers.alt;
  if (copy && !drag_.duplicate) {
    core::IdSet copies;
    core::Document duplicated =
        core::DuplicateObjects(base.document, base.selection, ids_, &copies);
    drag_.duplicate = core::EditorState{std::move(duplicated), std::move(copies)};
  }
  const core::EditorState& source = copy ? *drag_.duplicate : base;
  drag_.preview = core::EditorState{
      core::TransformObjects(source.document, source.selection, matrix), source.selection};
}

void Editor::PointerUp(Point p, Modifiers modifiers) {
  drag_.current = p;
  const DragKind kind = drag_.kind;
  if (kind == DragKind::kMarquee) {
    const core::IdSet touched =
        geometry::ObjectsTouching(document(), Rect::FromPoint(drag_.start).Union(p), 0.25);
    core::IdSet selection = modifiers.shift ? this->selection() : core::IdSet{};
    selection.insert(touched.begin(), touched.end());
    drag_ = {};
    SetSelection(std::move(selection));
    return;
  }
  if (drag_.preview) {
    core::EditorState result = std::move(*drag_.preview);
    const bool copied = drag_.duplicate && kind == DragKind::kMove && modifiers.alt;
    drag_ = {};
    Commit(copied                      ? "duplicate"
           : kind == DragKind::kScale  ? "scale"
           : kind == DragKind::kRotate ? "rotate"
                                       : "move",
           std::move(result));
    return;
  }
  drag_ = {};
}

void Editor::CancelDrag() { drag_ = {}; }

void Editor::SelectAll() { SetSelection(TopLevelSelectable(document())); }

void Editor::Deselect() { SetSelection({}); }

void Editor::Delete() {
  if (selection().empty()) return;
  Commit("delete", {core::RemoveObjects(document(), selection()), {}});
}

void Editor::Group() {
  if (selection().empty()) return;
  const std::string id = ids_.Next();
  Commit("group", {core::GroupObjects(document(), selection(), id), {id}});
}

void Editor::Ungroup() {
  core::IdSet groups, others;
  for (const auto& found : core::FindObjects(document(), selection())) {
    const std::string& id = core::CommonOf(*found.object).id;
    (std::holds_alternative<core::GroupObject>(*found.object) ? groups : others).insert(id);
  }
  if (groups.empty()) return;
  core::IdSet released;
  core::Document result = core::UngroupObjects(document(), groups, &released);
  released.insert(others.begin(), others.end());
  Commit("ungroup", {std::move(result), std::move(released)});
}

void Editor::Arrange(core::Arrange arrange) {
  if (selection().empty()) return;
  core::Document result = core::ArrangeObjects(document(), selection(), arrange);
  if (result.layers == document().layers) return;  // Already there.
  Commit("arrange", {std::move(result), selection()});
}

void Editor::Nudge(double dx, double dy) {
  if (selection().empty()) return;
  Commit("move",
         {core::TransformObjects(document(), selection(), Matrix::Translate(dx, dy)), selection()});
}

void Editor::Undo() {
  drag_ = {};
  history_.Undo();
}

void Editor::Redo() {
  drag_ = {};
  history_.Redo();
}

}  // namespace leinwand::editor
