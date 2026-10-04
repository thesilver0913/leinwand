// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor/editor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <utility>
#include <variant>

#include "core/transform.h"
#include "editor/tool_math.h"
#include "geometry/bezier.h"
#include "geometry/hit_test.h"
#include "geometry/path_edit.h"

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

Point Centre(const Rect& r) { return {(r.left + r.right) / 2, (r.top + r.bottom) / 2}; }

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
  const core::Document& doc = history_.current().document;
  for (const auto& id : core::AllObjectIds(doc)) ids_.Reserve(id);
  for (const auto& swatch : doc.swatches) ids_.Reserve(swatch.id);
  std::function<void(const core::Layer&)> reserve = [&](const core::Layer& layer) {
    ids_.Reserve(layer.id);
    for (const auto& child : layer.children) {
      if (const auto* sublayer = std::get_if<core::LayerPtr>(&child)) reserve(**sublayer);
    }
  };
  for (const auto& layer : doc.layers) reserve(*layer);
  // Illustrator's defaults for new artwork: white fill, 1 pt black stroke.
  core::Stroke stroke{core::RgbColor{0, 0, 0}};
  stroke.width = 1;
  new_style_ = {stroke, core::Fill{core::RgbColor{1, 1, 1}}};
}

void Editor::SetTool(Tool tool) {
  FinishPath();
  if (tool != Tool::kType) EndTextEdit();
  drag_ = {};
  tool_ = tool;
  temporary_tool_.reset();
  if (tool == Tool::kSelection || tool == Tool::kDirectSelection) last_selection_tool_ = tool;
}

void Editor::AdjustToolCount(int delta) {
  if (tool_ == Tool::kPolygon) polygon_sides_ = std::clamp(polygon_sides_ + delta, 3, 1000);
  if (tool_ == Tool::kStar) star_points_ = std::clamp(star_points_ + delta, 3, 1000);
  if (drag_.kind == DragKind::kDraw) UpdateDrawing();
}

std::optional<core::ObjectPtr> Editor::DrawnShape() const {
  const Point s = drag_.start;
  Point d = drag_.current - s;
  if (std::hypot(d.x, d.y) < drag_.threshold) return std::nullopt;
  const Modifiers m = drag_.modifiers;

  core::ShapeObject shape;
  shape.common.id = drag_.new_id;
  shape.common.appearance = new_style_;
  switch (tool()) {
    case Tool::kRectangle:
    case Tool::kEllipse: {
      if (m.shift) {  // Square or circle.
        const double side = std::max(std::abs(d.x), std::abs(d.y));
        d = {std::copysign(side, d.x), std::copysign(side, d.y)};
      }
      // Alt: the press point is the centre instead of a corner.
      const Point centre = m.alt ? s : s + d * 0.5;
      const double w = std::abs(d.x) * (m.alt ? 2 : 1), h = std::abs(d.y) * (m.alt ? 2 : 1);
      if (w <= 0 || h <= 0) return std::nullopt;
      if (tool() == Tool::kRectangle) {
        shape.shape = core::RectangleShape{w, h};
      } else {
        shape.shape = core::EllipseShape{w, h};
      }
      shape.transform = Matrix::Translate(centre.x, centre.y);
      break;
    }
    case Tool::kPolygon:
    case Tool::kStar: {
      // Drawn from the centre; a vertex follows the pointer unless Shift
      // keeps the shape upright.
      const double radius = std::hypot(d.x, d.y);
      const double angle = m.shift ? 0.0 : std::atan2(d.y, d.x) + std::numbers::pi / 2;
      if (tool() == Tool::kPolygon) {
        shape.shape = core::PolygonShape{polygon_sides_, radius};
      } else {
        shape.shape = core::StarShape{star_points_, radius, radius / 2};
      }
      shape.transform = Matrix::Translate(s.x, s.y) * Matrix::Rotate(angle);
      break;
    }
    case Tool::kLine: {
      if (m.shift) d = ConstrainTo45(d);
      const Point a = m.alt ? s - d : s, b = s + d;
      shape.shape = core::LineShape{std::hypot(b.x - a.x, b.y - a.y)};
      const Point mid = (a + b) * 0.5;
      shape.transform =
          Matrix::Translate(mid.x, mid.y) * Matrix::Rotate(std::atan2(b.y - a.y, b.x - a.x));
      // A line has nothing to fill.
      std::erase_if(shape.common.appearance, [](const core::AppearanceItem& item) {
        return std::holds_alternative<core::Fill>(item);
      });
      break;
    }
    default:
      return std::nullopt;
  }
  return core::MakeObject(std::move(shape));
}

void Editor::UpdateDrawing() {
  const auto shape = DrawnShape();
  if (!shape) {
    drag_.preview.reset();
    return;
  }
  drag_.preview =
      core::EditorState{WithNewObject(history_.current().document, *shape), {drag_.new_id}};
}

const core::ShapeObject* Editor::SingleShape() const {
  if (selection().size() != 1) return nullptr;
  const auto found = core::FindObjects(document(), selection());
  if (found.size() != 1) return nullptr;
  return std::get_if<core::ShapeObject>(&*found[0].object);
}

std::optional<SelectionInfo> Editor::Info() const {
  const auto bounds = SelectionBounds();
  if (!bounds) return std::nullopt;
  SelectionInfo info;
  info.bounds = *bounds;
  if (const core::ShapeObject* shape = SingleShape()) {
    info.shape = shape->shape;
    // The panel counts counter-clockwise on screen; y points down.
    info.rotation = -std::atan2(shape->transform.b, shape->transform.a) * 180 / std::numbers::pi;
    if (std::abs(info.rotation) < 1e-9) info.rotation = 0;
  }
  return info;
}

void Editor::SetBounds(const Rect& bounds) {
  const auto old = SelectionBounds();
  if (!old || bounds == *old) return;
  const double sx = old->width() > 0 ? bounds.width() / old->width() : 1.0;
  const double sy = old->height() > 0 ? bounds.height() / old->height() : 1.0;
  if (sx <= 0 || sy <= 0) return;
  const Matrix matrix = Matrix::Translate(bounds.left, bounds.top) * Matrix::Scale(sx, sy) *
                        Matrix::Translate(-old->left, -old->top);
  Commit("transform", {core::TransformObjects(document(), selection(), matrix), selection()});
}

void Editor::SetRotation(double degrees) {
  const auto info = Info();
  if (!info) return;
  // A single shape shows its own angle, so the value is absolute; anything
  // else shows 0 and the value rotates by that much.
  const double delta = info->shape ? degrees - info->rotation : degrees;
  if (std::abs(delta) < 1e-9) return;
  const Point c = Centre(info->bounds);
  const Matrix matrix = Matrix::Translate(c.x, c.y) *
                        Matrix::Rotate(-delta * std::numbers::pi / 180) *
                        Matrix::Translate(-c.x, -c.y);
  Commit("rotate", {core::TransformObjects(document(), selection(), matrix), selection()});
}

void Editor::SetShape(const core::ShapeParams& params) {
  const core::ShapeObject* shape = SingleShape();
  if (!shape || shape->shape == params) return;
  core::ShapeObject changed = *shape;
  changed.shape = params;
  Commit("shape",
         {core::ReplaceObjects(document(), {{shape->common.id, core::MakeObject(changed)}}),
          selection()});
}

const core::Document& Editor::document() const {
  return drag_.preview ? drag_.preview->document : history_.current().document;
}

const core::Document& Editor::shown_document() const {
  if (drag_.preview) return drag_.preview->document;
  return text_preview_ ? text_preview_->document : history_.current().document;
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
  if (dragging()) overlay.guides = guides_;
  overlay.key_object = KeyObjectBounds();
  const Tool tool = this->tool();
  if (tool == Tool::kType) {
    // The text being edited shows its caret and selection, not a box.
    TextOverlay(overlay);
    return overlay;
  }
  if (tool == Tool::kGradient) {
    // The gradient tool shows the gradient annotator instead of the box.
    overlay.gradient_line = GradientLine();
    return overlay;
  }
  const bool path_tool = tool == Tool::kPen || tool == Tool::kDirectSelection ||
                         tool == Tool::kAddAnchor || tool == Tool::kDeleteAnchor ||
                         tool == Tool::kConvertAnchor;
  if (path_tool) {
    for (const auto& id : EditablePaths()) {
      const auto ref = GetPath(document(), id);
      PathOverlay path{core::Transformed(ref->path, ref->to_document), {}, {}};
      const int n = static_cast<int>(path.path.anchors.size());
      for (const auto& a : anchors_) {
        if (a.id != id || a.index >= n) continue;
        path.selected.insert(a.index);
        // Handles of the anchor and the near handles of its neighbours.
        path.with_handles.insert(a.index);
        if (a.index > 0 || path.path.closed) path.with_handles.insert((a.index + n - 1) % n);
        if (a.index < n - 1 || path.path.closed) path.with_handles.insert((a.index + 1) % n);
      }
      if (id == pen_.path_id && n > 0) {
        const int last = pen_.reverse ? 0 : n - 1;
        path.selected.insert(last);
        path.with_handles.insert(last);
      }
      overlay.paths.push_back(std::move(path));
      overlay.selection.erase(id);  // Drawn with its own anchors instead.
    }
    if (drag_.kind == DragKind::kDirectMarquee) {
      overlay.marquee = Rect::FromPoint(drag_.start).Union(drag_.current);
    }
    // The pen's next segment follows the pointer (spec 4.2).
    if (rubber_band_ && drawing_path() && hover_ && drag_.kind == DragKind::kNone) {
      if (const auto ref = GetPath(document(), pen_.path_id); ref && !ref->path.anchors.empty()) {
        const core::PathData drawn = core::Transformed(
            pen_.reverse ? geometry::Reversed(ref->path) : ref->path, ref->to_document);
        core::PathData band;
        band.anchors = {drawn.anchors.back(), {*hover_}};
        band.anchors[0].handle_in = {};
        overlay.rubber_band = std::move(band);
      }
    }
    return overlay;
  }
  if (tool == Tool::kArtboard) {
    // The artboard tool shows the active artboard with its handles instead
    // of the selection.
    overlay.selection.clear();
    overlay.key_object.reset();
    if (const int active = active_artboard(); active >= 0) {
      overlay.bounding_box = document().artboards[size_t(active)].bounds;
    }
    return overlay;
  }
  if (tool == Tool::kSelection &&
      (drag_.kind == DragKind::kNone || drag_.kind == DragKind::kCornerRadius)) {
    overlay.corner_widgets = CornerWidgets(widget_pick_);
  }
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
  if (tool() != Tool::kSelection) return {};
  widget_pick_ = pick;
  if (CornerWidgetAt(p, pick)) return {Hover::Kind::kCorner};
  if (const auto h = HandleAt(p, pick)) return {Hover::Kind::kHandle, *h};
  if (const auto h = RotateZoneAt(p, pick)) return {Hover::Kind::kRotate, *h};
  if (geometry::HitTest(document(), p, pick)) return {Hover::Kind::kObject};
  return {};
}

void Editor::SetSelection(core::IdSet selection) {
  history_.SetSelection(std::move(selection));
  AdoptSelectionStyle();
  // A preview of edited text is built on the current state; keep it so.
  if (text_preview_) UpdateTextPreview();
}

void Editor::Commit(const std::string& action, core::EditorState state) {
  if (gesture_ && gesture_pushed_) {
    history_.Amend(action, std::move(state));
  } else {
    history_.Push(action, std::move(state));
    gesture_pushed_ = gesture_;
  }
  // Edits made while text is being edited (from the panels) must show
  // through the text's preview, which is built on the current state.
  if (text_preview_) UpdateTextPreview();
}

void Editor::BeginGesture() {
  gesture_ = true;
  gesture_pushed_ = false;
}

void Editor::EndGesture() { gesture_ = gesture_pushed_ = false; }

void Editor::PointerDown(Point p, Modifiers modifiers, double pick) {
  drag_ = {};
  guides_.clear();
  drag_.start = drag_.current = drag_.grab = p;
  drag_.threshold = pick / 2;
  drag_.pick = pick;
  drag_.modifiers = modifiers;
  const Tool tool = this->tool();
  // Switching to a selection tool, even with Ctrl held, ends the pen path.
  if (tool == Tool::kSelection || tool == Tool::kDirectSelection) FinishPath();
  if (tool != Tool::kType) EndTextEdit();
  switch (tool) {
    case Tool::kType:
      TypeDown(p, modifiers, pick);
      return;
    case Tool::kPen:
      PenDown(p, modifiers, pick);
      return;
    case Tool::kAddAnchor:
    case Tool::kDeleteAnchor:
    case Tool::kConvertAnchor:
      AnchorToolDown(p, pick);
      if (drag_.kind == DragKind::kNone && tool == Tool::kConvertAnchor) {
        // Not on an anchor: maybe on a handle, which the tool splits.
        DirectDown(p, modifiers, pick);
        if (drag_.kind != DragKind::kMoveHandle) drag_ = {};
      }
      return;
    case Tool::kDirectSelection:
      DirectDown(p, modifiers, pick);
      return;
    case Tool::kEyedropper:
      EyedropperDown(p, pick);
      return;
    case Tool::kScissors:
      ScissorsDown(p, pick);
      return;
    case Tool::kArtboard:
      ArtboardDown(p, pick);
      return;
    case Tool::kGradient:
      GradientDown(p, pick);
      return;
    case Tool::kRectangle:
    case Tool::kEllipse:
    case Tool::kPolygon:
    case Tool::kStar:
    case Tool::kLine:
      drag_.kind = DragKind::kDraw;
      drag_.new_id = ids_.Next();
      drag_.start = drag_.current = drag_.grab = SnapPlaced(p, pick);
      return;
    case Tool::kSelection:
      break;
  }
  widget_pick_ = pick;
  if (const auto corner = CornerWidgetAt(p, pick)) {
    CornerDown(*corner, modifiers);
    return;
  }
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
  } else if (selection.size() > 1) {
    drag_.key_candidate = hit->top_level_id;
  }
  drag_.kind = DragKind::kPending;
}

void Editor::PointerMove(Point p, Modifiers modifiers) {
  drag_.current = SnapDrag(p);
  drag_.modifiers = modifiers;
  switch (drag_.kind) {
    case DragKind::kDraw:
      UpdateDrawing();
      return;
    case DragKind::kPen:
      PenMove();
      return;
    case DragKind::kConvert:
    case DragKind::kDirectPending:
    case DragKind::kMoveAnchors:
    case DragKind::kMoveHandle:
    case DragKind::kDragSegment:
      DirectMove();
      return;
    case DragKind::kArtboardDraw:
    case DragKind::kArtboardMove:
    case DragKind::kArtboardResize:
      ArtboardDrag();
      return;
    case DragKind::kGradient:
      GradientDrag();
      return;
    case DragKind::kTextSelect:
      TypeDrag();
      return;
    case DragKind::kCornerRadius:
      CornerDrag();
      return;
    default:
      break;
  }
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
  drag_.current = SnapDrag(p);
  const DragKind kind = drag_.kind;
  switch (kind) {
    case DragKind::kPen:
      drag_.modifiers = modifiers;
      PenMove();
      PenUp();
      return;
    case DragKind::kConvert:
    case DragKind::kDirectPending:
    case DragKind::kDirectMarquee:
    case DragKind::kMoveAnchors:
    case DragKind::kMoveHandle:
    case DragKind::kDragSegment:
      drag_.modifiers = modifiers;
      DirectMove();
      DirectUp(p, modifiers);
      return;
    default:
      break;
  }
  if (kind == DragKind::kMarquee) {
    const core::IdSet touched =
        geometry::ObjectsTouching(document(), Rect::FromPoint(drag_.start).Union(p), 0.25);
    core::IdSet selection = modifiers.shift ? this->selection() : core::IdSet{};
    selection.insert(touched.begin(), touched.end());
    drag_ = {};
    SetSelection(std::move(selection));
    return;
  }
  if (kind == DragKind::kTextSelect) {
    drag_ = {};
    return;
  }
  if (kind == DragKind::kCornerRadius) {
    std::optional<core::EditorState> result = std::move(drag_.preview);
    drag_ = {};
    if (result) Commit("corner radius", std::move(*result));
    return;
  }
  if (kind == DragKind::kGradient) {
    drag_.modifiers = modifiers;
    GradientDrag();
    std::optional<core::EditorState> result = std::move(drag_.preview);
    drag_ = {};
    if (result) Commit("gradient", std::move(*result));
    return;
  }
  if (kind == DragKind::kArtboardDraw || kind == DragKind::kArtboardMove ||
      kind == DragKind::kArtboardResize) {
    drag_.modifiers = modifiers;
    ArtboardDrag();
    std::optional<core::EditorState> result = std::move(drag_.preview);
    const int index = drag_.index;
    drag_ = {};
    if (result) {
      Commit(kind == DragKind::kArtboardDraw ? "add artboard" : "edit artboard",
             std::move(*result));
      active_artboard_ = index;
    }
    return;
  }
  if (kind == DragKind::kDraw) {
    drag_.modifiers = modifiers;
    UpdateDrawing();
    std::optional<core::EditorState> result = std::move(drag_.preview);
    drag_ = {};
    if (result) Commit("draw", std::move(*result));
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
  if (kind == DragKind::kPending && !drag_.key_candidate.empty()) {
    // A click on a selected object (spec 7.2): it becomes the key object,
    // and alignment follows it; a second click clears it.
    if (key_object() == drag_.key_candidate) {
      key_object_.clear();
      align_to_ = AlignTo::kSelection;
    } else {
      key_object_ = drag_.key_candidate;
      align_to_ = AlignTo::kKeyObject;
    }
  }
  drag_ = {};
}

void Editor::CancelDrag() { drag_ = {}; }

void Editor::SelectAll() {
  if (text_editing()) {
    SelectAllText();
    return;
  }
  SetSelection(TopLevelSelectable(document()));
}

void Editor::Deselect() {
  EndTextEdit();
  SetSelection({});
}

void Editor::Delete() {
  if (text_editing()) {
    DeleteForward();
    return;
  }
  if (tool() == Tool::kArtboard) {
    RemoveActiveArtboard();
    return;
  }
  if (tool() == Tool::kDirectSelection && !anchors_.empty()) {
    DeleteSelectedAnchors();
    return;
  }
  if (selection().empty()) return;
  Commit("delete", {core::RemoveObjects(document(), selection()), {}});
}

void Editor::Group() {
  EndTextEdit();
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
  if (tool() == Tool::kDirectSelection && !anchors_.empty()) {
    MoveSelectedAnchors({dx, dy});
    return;
  }
  if (selection().empty()) return;
  Commit("move",
         {core::TransformObjects(document(), selection(), Matrix::Translate(dx, dy)), selection()});
}

void Editor::Undo() {
  drag_ = {};
  // Undoing a composition or untyped new text just drops it.
  if (text_.pending) {
    EndTextEdit();
    return;
  }
  history_.Undo();
  ValidateTextEdit();
  anchors_.clear();
  // Undoing pen clicks keeps drawing, until the path itself is gone.
  if (drawing_path() && !GetPath(document(), pen_.path_id)) FinishPath();
}

void Editor::Redo() {
  drag_ = {};
  history_.Redo();
  ValidateTextEdit();
  anchors_.clear();
}

void Editor::MoveSelectedAnchors(Point delta) {
  drag_.base = history_.current().document;
  drag_.start = {};
  drag_.current = delta;
  drag_.kind = DragKind::kMoveAnchors;
  drag_.modifiers = {};
  DirectMove();
  std::optional<core::EditorState> result = std::move(drag_.preview);
  drag_ = {};
  if (result) Commit("move anchors", std::move(*result));
}

}  // namespace leinwand::editor
