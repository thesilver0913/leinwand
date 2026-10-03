// SPDX-License-Identifier: GPL-3.0-or-later
// Pen, anchor point tools and direct selection (spec 4.2).
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <variant>

#include "core/transform.h"
#include "editor/editor.h"
#include "editor/tool_math.h"
#include "geometry/hit_test.h"
#include "geometry/path_edit.h"

namespace leinwand::editor {

using core::Matrix;
using core::PathData;
using core::Point;
using core::Rect;

namespace {

Matrix Inverse(const Matrix& m) { return m.Inverted().value_or(Matrix{}); }

// Pick radius in a path's own coordinates.
double LocalPick(const Matrix& to_document, double pick) {
  const double scale = std::sqrt(std::abs(to_document.Determinant()));
  return scale > 0 ? pick / scale : pick;
}

bool HasHandles(const core::Anchor& a) {
  return !(a.handle_in == Point{}) || !(a.handle_out == Point{});
}

}  // namespace

// --- Paths -----------------------------------------------------------------

std::optional<Editor::PathRef> Editor::GetPath(const core::Document& document,
                                               const std::string& id) const {
  if (id.empty()) return std::nullopt;
  const auto found = core::FindObjects(document, {id});
  if (found.size() != 1) return std::nullopt;
  const core::Object& object = *found[0].object;
  if (!std::holds_alternative<core::PathObject>(object) &&
      !std::holds_alternative<core::ShapeObject>(object)) {
    return std::nullopt;  // Compound paths and groups: not yet (direct selection inside, later).
  }
  return PathRef{id, found[0].to_document, core::OutlineOf(object).front()};
}

core::Document Editor::WithPath(const core::Document& document, const PathRef& ref) const {
  const auto found = core::FindObjects(document, {ref.id});
  if (found.size() != 1) return document;
  core::PathObject path;
  path.common = core::CommonOf(*found[0].object);
  path.path = ref.path;
  return core::ReplaceObjects(document, {{ref.id, core::MakeObject(std::move(path))}});
}

std::optional<std::pair<std::string, bool>> Editor::OpenEndAt(Point p, double pick,
                                                              const std::string& skip) const {
  std::optional<std::pair<std::string, bool>> best;
  double best_distance = pick;
  const core::Document& doc = document();
  for (const auto& found : core::FindObjects(doc, core::AllObjectIds(doc))) {
    const auto* path = std::get_if<core::PathObject>(&*found.object);
    if (!path || path->path.closed || path->path.anchors.empty()) continue;
    const auto& common = path->common;
    if (common.id == skip || !common.visible || common.locked) continue;
    const auto& anchors = path->path.anchors;
    for (bool at_end : {false, true}) {
      const Point end = found.to_document.Map((at_end ? anchors.back() : anchors.front()).position);
      const double d = Distance(p, end);
      if (d <= best_distance) {
        best_distance = d;
        best = {{common.id, at_end || anchors.size() == 1}};
      }
    }
  }
  return best;
}

std::vector<std::string> Editor::EditablePaths() const {
  std::vector<std::string> ids;
  for (const auto& id : selection()) {
    if (GetPath(document(), id)) ids.push_back(id);
  }
  if (drawing_path() && !selection().contains(pen_.path_id)) ids.push_back(pen_.path_id);
  return ids;
}

std::optional<AnchorRef> Editor::AnchorAt(Point p, double pick) const {
  std::optional<AnchorRef> best;
  double best_distance = pick;
  for (const auto& id : EditablePaths()) {
    const auto ref = GetPath(document(), id);
    for (int i = 0; i < static_cast<int>(ref->path.anchors.size()); ++i) {
      const double d = Distance(p, ref->to_document.Map(ref->path.anchors[i].position));
      if (d <= best_distance) {
        best_distance = d;
        best = AnchorRef{id, 0, i};
      }
    }
  }
  return best;
}

void Editor::PreviewPath(const PathRef& ref) {
  drag_.preview = core::EditorState{WithPath(*drag_.base, ref), {ref.id}};
}

void Editor::FinishPath() {
  pen_ = {};
  hover_.reset();
}

void Editor::SetTemporaryTool(std::optional<Tool> tool) { temporary_tool_ = tool; }

void Editor::PointerHover(Point p) { hover_ = p; }

// --- Pen -------------------------------------------------------------------

PenAction Editor::PenActionAt(Point p, double pick) const {
  if (const auto ref = drawing_path() ? GetPath(document(), pen_.path_id) : std::nullopt) {
    const auto& anchors = ref->path.anchors;
    const int last = pen_.reverse ? 0 : static_cast<int>(anchors.size()) - 1;
    const int first = pen_.reverse ? static_cast<int>(anchors.size()) - 1 : 0;
    auto near = [&](int i) {
      return Distance(p, ref->to_document.Map(anchors[i].position)) <= pick;
    };
    if (anchors.size() >= 2 && !ref->path.closed && near(first)) return PenAction::kClose;
    if (near(last)) return PenAction::kRemoveHandle;
    if (OpenEndAt(p, pick, pen_.path_id)) return PenAction::kJoin;
    return PenAction::kNewAnchor;
  }
  if (OpenEndAt(p, pick, "")) return PenAction::kContinue;
  if (AnchorAt(p, pick)) return PenAction::kDeleteAnchor;
  for (const auto& id : EditablePaths()) {
    const auto ref = GetPath(document(), id);
    const auto hit = geometry::NearestSegment(ref->path, Inverse(ref->to_document).Map(p));
    if (hit && hit->distance <= LocalPick(ref->to_document, pick)) return PenAction::kAddAnchor;
  }
  return PenAction::kNewPath;
}

void Editor::PenDown(Point p, Modifiers modifiers, double pick) {
  const PenAction action = PenActionAt(p, pick);
  if (action == PenAction::kNewPath || action == PenAction::kNewAnchor) p = SnapPlaced(p, pick);
  drag_ = {};
  drag_.kind = DragKind::kPen;
  drag_.pen = action;
  drag_.start = drag_.current = drag_.space_from = p;
  drag_.threshold = pick / 2;
  drag_.modifiers = modifiers;
  drag_.base = history_.current().document;

  auto current_path = [&]() {
    auto ref = GetPath(*drag_.base, pen_.path_id);
    if (ref && pen_.reverse) ref->path = geometry::Reversed(ref->path);
    return ref;
  };

  switch (action) {
    case PenAction::kNewPath: {
      core::PathObject object;
      object.common.id = ids_.Next();
      object.common.appearance = new_style_;
      object.path.anchors = {{p}};
      drag_.base = WithNewObject(*drag_.base, core::MakeObject(object));
      pen_ = {object.common.id, false};
      drag_.path_id = object.common.id;
      drag_.index = 0;
      drag_.original = object.path;
      PreviewPath({object.common.id, Matrix{}, object.path});
      return;
    }
    case PenAction::kNewAnchor: {
      auto ref = current_path();
      ref->path.anchors.push_back({Inverse(ref->to_document).Map(p)});
      drag_.path_id = ref->id;
      drag_.index = static_cast<int>(ref->path.anchors.size()) - 1;
      drag_.original = ref->path;
      PreviewPath(*ref);
      return;
    }
    case PenAction::kRemoveHandle:
    case PenAction::kClose: {
      auto ref = current_path();
      drag_.path_id = ref->id;
      drag_.index =
          action == PenAction::kClose ? 0 : static_cast<int>(ref->path.anchors.size()) - 1;
      if (action == PenAction::kClose) ref->path.closed = true;
      drag_.original = ref->path;
      PreviewPath(*ref);
      return;
    }
    case PenAction::kJoin: {
      const auto end = OpenEndAt(p, pick, pen_.path_id);
      drag_.join_id = end->first;
      drag_.join_at_end = end->second;
      return;
    }
    case PenAction::kContinue: {
      const auto end = OpenEndAt(p, pick, "");
      pen_ = {end->first, !end->second};
      SetSelection({end->first});
      auto ref = current_path();
      drag_.path_id = ref->id;
      drag_.index = static_cast<int>(ref->path.anchors.size()) - 1;
      drag_.original = ref->path;
      return;
    }
    case PenAction::kAddAnchor:
      drag_ = {};
      for (const auto& id : EditablePaths()) {
        auto ref = GetPath(document(), id);
        const auto hit = geometry::NearestSegment(ref->path, Inverse(ref->to_document).Map(p));
        if (!hit || hit->distance > LocalPick(ref->to_document, pick)) continue;
        geometry::InsertAnchor(ref->path, hit->segment, hit->t);
        Commit("add anchor", {WithPath(document(), *ref), selection()});
        return;
      }
      return;
    case PenAction::kDeleteAnchor: {
      drag_ = {};
      const auto anchor = AnchorAt(p, pick);
      auto ref = GetPath(document(), anchor->id);
      geometry::RemoveAnchor(ref->path, anchor->index);
      core::Document result = ref->path.anchors.empty() ? core::RemoveObjects(document(), {ref->id})
                                                        : WithPath(document(), *ref);
      core::IdSet selection = this->selection();
      if (ref->path.anchors.empty()) selection.erase(ref->id);
      Commit("delete anchor", {std::move(result), std::move(selection)});
      return;
    }
  }
}

void Editor::PenMove() {
  if (!drag_.original) return;
  const Modifiers m = drag_.modifiers;
  auto ref = GetPath(*drag_.base, drag_.path_id);
  if (!ref) return;
  const Matrix to_local = Inverse(ref->to_document);

  // Space: move the anchor itself while the button is held.
  if (m.space && (drag_.pen == PenAction::kNewPath || drag_.pen == PenAction::kNewAnchor)) {
    const Point delta = to_local.MapVector(drag_.current - drag_.space_from);
    drag_.original->anchors[drag_.index].position =
        drag_.original->anchors[drag_.index].position + delta;
    drag_.start = drag_.start + (drag_.current - drag_.space_from);
  }
  drag_.space_from = drag_.current;

  ref->path = *drag_.original;
  core::Anchor& anchor = ref->path.anchors[drag_.index];
  Point d = to_local.Map(drag_.current) - anchor.position;
  if (Distance(drag_.current, drag_.start) < drag_.threshold) d = {};
  if (m.shift) d = ConstrainTo45(d);

  switch (drag_.pen) {
    case PenAction::kNewPath:
    case PenAction::kNewAnchor:
    case PenAction::kClose:
      if (d == Point{}) break;
      if (m.alt) {
        // Alt: only the outgoing handle follows; the incoming one stays
        // where it was when Alt went down.
        if (!drag_.frozen_in) drag_.frozen_in = drag_.last_in;
        anchor.handle_out = d;
        anchor.handle_in = *drag_.frozen_in;
        anchor.kind = core::AnchorKind::kCorner;
      } else {
        drag_.frozen_in.reset();
        // Closing: the drag direction continues past the start point.
        anchor.handle_out = d;
        anchor.handle_in = d * -1.0;
        anchor.kind = core::AnchorKind::kSmooth;
      }
      drag_.last_in = anchor.handle_in;
      break;
    case PenAction::kRemoveHandle:
    case PenAction::kContinue:
      if (d == Point{}) return;  // Not a drag yet.
      anchor.handle_out = d;
      anchor.kind = core::AnchorKind::kCorner;
      break;
    default:
      return;
  }
  PreviewPath(*ref);
}

void Editor::PenUp() {
  const bool dragged = Distance(drag_.current, drag_.start) >= drag_.threshold;
  switch (drag_.pen) {
    case PenAction::kJoin: {
      auto active = GetPath(document(), pen_.path_id);
      auto other = GetPath(document(), drag_.join_id);
      if (active && other) {
        if (pen_.reverse) active->path = geometry::Reversed(active->path);
        const PathData other_local =
            core::Transformed(other->path, Inverse(active->to_document) * other->to_document);
        active->path = geometry::Join(active->path, true, other_local, drag_.join_at_end);
        core::Document result = core::RemoveObjects(WithPath(document(), *active), {other->id});
        drag_ = {};
        FinishPath();
        Commit("join", {std::move(result), {active->id}});
      }
      drag_ = {};
      return;
    }
    case PenAction::kRemoveHandle:
      if (!dragged) {
        // A click on the last anchor: the next segment starts straight.
        auto ref = GetPath(*drag_.base, drag_.path_id);
        ref->path = *drag_.original;
        ref->path.anchors[drag_.index].handle_out = {};
        PreviewPath(*ref);
      }
      break;
    case PenAction::kContinue:
      if (!dragged) {
        drag_ = {};
        return;  // Just picked up the path; nothing changed yet.
      }
      break;
    default:
      break;
  }
  std::optional<core::EditorState> result = std::move(drag_.preview);
  const bool closed = drag_.pen == PenAction::kClose;
  drag_ = {};
  if (!result) return;
  pen_.reverse = false;  // The committed path is in drawing order now.
  Commit("pen", std::move(*result));
  if (closed) FinishPath();
}

// --- Anchor point tools ---------------------------------------------------------

void Editor::AnchorToolDown(Point p, double pick) {
  switch (tool()) {
    case Tool::kAddAnchor:
      for (const auto& id : EditablePaths()) {
        auto ref = GetPath(document(), id);
        const auto hit = geometry::NearestSegment(ref->path, Inverse(ref->to_document).Map(p));
        if (!hit || hit->distance > LocalPick(ref->to_document, pick)) continue;
        geometry::InsertAnchor(ref->path, hit->segment, hit->t);
        Commit("add anchor", {WithPath(document(), *ref), selection()});
        return;
      }
      return;
    case Tool::kDeleteAnchor:
      if (const auto anchor = AnchorAt(p, pick)) {
        auto ref = GetPath(document(), anchor->id);
        geometry::RemoveAnchor(ref->path, anchor->index);
        Commit("delete anchor", {WithPath(document(), *ref), selection()});
      }
      return;
    case Tool::kConvertAnchor:
      if (const auto anchor = AnchorAt(p, pick)) {
        // Click: smooth to corner. Drag: pull out smooth handles.
        drag_ = {};
        drag_.kind = DragKind::kConvert;
        drag_.start = drag_.current = p;
        drag_.threshold = pick / 2;
        drag_.base = history_.current().document;
        drag_.path_id = anchor->id;
        drag_.index = anchor->index;
        drag_.original = GetPath(*drag_.base, anchor->id)->path;
      }
      return;
    default:
      return;
  }
}

// --- Direct selection ----------------------------------------------------------

void Editor::DirectDown(Point p, Modifiers modifiers, double pick) {
  drag_ = {};
  drag_.start = drag_.current = drag_.grab = p;
  drag_.pick = pick;
  drag_.threshold = pick / 2;
  drag_.modifiers = modifiers;
  drag_.base = history_.current().document;

  // Handles of selected anchors first: they sit on top.
  for (const AnchorRef& a : anchors_) {
    const auto ref = GetPath(document(), a.id);
    if (!ref || a.index >= static_cast<int>(ref->path.anchors.size())) continue;
    const core::Anchor& anchor = ref->path.anchors[a.index];
    for (bool out : {true, false}) {
      const Point handle = out ? anchor.out_point() : anchor.in_point();
      if (handle == anchor.position) continue;
      if (Distance(p, ref->to_document.Map(handle)) <= pick) {
        drag_.kind = DragKind::kMoveHandle;
        drag_.path_id = a.id;
        drag_.index = a.index;
        drag_.handle_out = out;
        drag_.original = ref->path;
        return;
      }
    }
  }

  // Anchors of the selected paths, or of the path under the pointer.
  const auto hit = geometry::HitTest(document(), p, pick);
  std::vector<std::string> candidates = EditablePaths();
  if (hit && GetPath(document(), hit->leaf_id)) candidates.push_back(hit->leaf_id);
  std::optional<AnchorRef> anchor;
  double best = pick;
  for (const auto& id : candidates) {
    const auto ref = GetPath(document(), id);
    for (int i = 0; i < static_cast<int>(ref->path.anchors.size()); ++i) {
      const double d = Distance(p, ref->to_document.Map(ref->path.anchors[i].position));
      if (d <= best) {
        best = d;
        anchor = AnchorRef{id, 0, i};
      }
    }
  }
  if (anchor) {
    if (modifiers.shift) {
      if (anchors_.erase(*anchor) == 0) anchors_.insert(*anchor);
    } else if (!anchors_.contains(*anchor)) {
      anchors_ = {*anchor};
    }
    core::IdSet ids;
    for (const auto& a : anchors_) ids.insert(a.id);
    SetSelection(ids);
    if (anchors_.contains(*anchor)) {
      drag_.kind = DragKind::kDirectPending;
      // The grabbed anchor itself snaps, not the pointer beside it.
      const auto ref = GetPath(document(), anchor->id);
      drag_.grab = ref->to_document.Map(ref->path.anchors[anchor->index].position);
    }
    return;
  }

  // A segment: curved ones bend; straight ones move with their anchors.
  for (const auto& id : candidates) {
    const auto ref = GetPath(document(), id);
    const auto near = geometry::NearestSegment(ref->path, Inverse(ref->to_document).Map(p));
    if (!near || near->distance > LocalPick(ref->to_document, pick)) continue;
    const auto c = geometry::SegmentAt(ref->path, near->segment);
    const int next = (near->segment + 1) % static_cast<int>(ref->path.anchors.size());
    if (!modifiers.shift) anchors_.clear();
    SetSelection({id});
    if (c.p1 == c.p0 && c.p2 == c.p3) {
      anchors_.insert({id, 0, near->segment});
      anchors_.insert({id, 0, next});
      drag_.kind = DragKind::kDirectPending;
    } else {
      drag_.kind = DragKind::kDragSegment;
      drag_.path_id = id;
      drag_.index = near->segment;
      drag_.t = near->t;
      drag_.original = ref->path;
    }
    return;
  }

  // Inside a filled path: the whole path.
  if (hit && GetPath(document(), hit->leaf_id)) {
    const auto ref = GetPath(document(), hit->leaf_id);
    if (!modifiers.shift) anchors_.clear();
    for (int i = 0; i < static_cast<int>(ref->path.anchors.size()); ++i) {
      anchors_.insert({ref->id, 0, i});
    }
    SetSelection({ref->id});
    drag_.kind = DragKind::kDirectPending;
    return;
  }

  if (!modifiers.shift) {
    anchors_.clear();
    SetSelection({});
  }
  drag_.kind = DragKind::kDirectMarquee;
}

void Editor::DirectMove() {
  const Modifiers m = drag_.modifiers;
  switch (drag_.kind) {
    case DragKind::kDirectPending:
      if (Distance(drag_.current, drag_.start) < drag_.threshold) return;
      drag_.kind = DragKind::kMoveAnchors;
      [[fallthrough]];
    case DragKind::kMoveAnchors: {
      Point delta = drag_.current - drag_.start;
      if (m.shift) delta = ConstrainTo45(delta);
      // Group the selected anchors by path and move them together.
      std::map<std::string, std::vector<int>> by_path;
      for (const auto& a : anchors_) by_path[a.id].push_back(a.index);
      core::Document result = *drag_.base;
      for (const auto& [id, indices] : by_path) {
        auto ref = GetPath(*drag_.base, id);
        if (!ref) continue;
        const Point local = Inverse(ref->to_document).MapVector(delta);
        for (int i : indices) {
          if (i < static_cast<int>(ref->path.anchors.size())) {
            geometry::MoveAnchor(ref->path, i, ref->path.anchors[i].position + local);
          }
        }
        result = WithPath(result, *ref);
      }
      drag_.preview = core::EditorState{std::move(result), history_.current().selection};
      return;
    }
    case DragKind::kMoveHandle: {
      auto ref = GetPath(*drag_.base, drag_.path_id);
      ref->path = *drag_.original;
      const core::Anchor& anchor = ref->path.anchors[drag_.index];
      Point to = Inverse(ref->to_document).Map(drag_.current);
      if (m.shift) to = anchor.position + ConstrainTo45(to - anchor.position);
      // Alt (or the anchor point tool) splits the handles.
      const bool split = m.alt || tool() == Tool::kConvertAnchor;
      geometry::MoveHandle(ref->path, drag_.index,
                           drag_.handle_out ? geometry::Side::kOut : geometry::Side::kIn, to,
                           split);
      drag_.preview = core::EditorState{WithPath(*drag_.base, *ref), history_.current().selection};
      return;
    }
    case DragKind::kDragSegment: {
      if (Distance(drag_.current, drag_.start) < drag_.threshold) return;
      auto ref = GetPath(*drag_.base, drag_.path_id);
      ref->path = *drag_.original;
      geometry::DragSegment(ref->path, drag_.index, drag_.t,
                            Inverse(ref->to_document).Map(drag_.current));
      drag_.preview = core::EditorState{WithPath(*drag_.base, *ref), history_.current().selection};
      return;
    }
    case DragKind::kConvert: {
      if (Distance(drag_.current, drag_.start) < drag_.threshold) return;
      auto ref = GetPath(*drag_.base, drag_.path_id);
      ref->path = *drag_.original;
      core::Anchor& anchor = ref->path.anchors[drag_.index];
      Point d = Inverse(ref->to_document).Map(drag_.current) - anchor.position;
      if (m.shift) d = ConstrainTo45(d);
      anchor.handle_out = d;
      anchor.handle_in = d * -1.0;
      anchor.kind = core::AnchorKind::kSmooth;
      drag_.preview = core::EditorState{WithPath(*drag_.base, *ref), history_.current().selection};
      return;
    }
    default:
      return;
  }
}

void Editor::DirectUp(Point p, Modifiers modifiers) {
  const DragKind kind = drag_.kind;
  if (kind == DragKind::kDirectMarquee) {
    const Rect rect = Rect::FromPoint(drag_.start).Union(p);
    if (!modifiers.shift) anchors_.clear();
    const core::Document& doc = document();
    for (const auto& found : core::FindObjects(doc, core::AllObjectIds(doc))) {
      const auto& common = core::CommonOf(*found.object);
      if (!common.visible || common.locked) continue;
      const auto ref = GetPath(doc, common.id);
      if (!ref) continue;
      for (int i = 0; i < static_cast<int>(ref->path.anchors.size()); ++i) {
        if (rect.Contains(ref->to_document.Map(ref->path.anchors[i].position))) {
          anchors_.insert({common.id, 0, i});
        }
      }
    }
    core::IdSet ids;
    for (const auto& a : anchors_) ids.insert(a.id);
    drag_ = {};
    SetSelection(ids);
    return;
  }
  if (kind == DragKind::kConvert && !drag_.preview) {
    // A click: a smooth anchor becomes a corner.
    auto ref = GetPath(*drag_.base, drag_.path_id);
    if (ref && HasHandles(ref->path.anchors[drag_.index])) {
      geometry::MakeCorner(ref->path, drag_.index);
      drag_ = {};
      Commit("convert anchor", {WithPath(document(), *ref), selection()});
    }
    drag_ = {};
    return;
  }
  std::optional<core::EditorState> result = std::move(drag_.preview);
  drag_ = {};
  if (!result) return;
  Commit(kind == DragKind::kMoveHandle    ? "move handle"
         : kind == DragKind::kDragSegment ? "reshape"
         : kind == DragKind::kConvert     ? "convert anchor"
                                          : "move anchors",
         std::move(*result));
}

// --- Anchor commands (control bar) ---------------------------------------------

void Editor::ConvertSelectedAnchors(bool smooth) {
  if (anchors_.empty()) return;
  core::Document result = document();
  std::map<std::string, std::vector<int>> by_path;
  for (const auto& a : anchors_) by_path[a.id].push_back(a.index);
  for (const auto& [id, indices] : by_path) {
    auto ref = GetPath(result, id);
    if (!ref) continue;
    for (int i : indices) {
      if (i >= static_cast<int>(ref->path.anchors.size())) continue;
      smooth ? geometry::MakeSmooth(ref->path, i) : geometry::MakeCorner(ref->path, i);
    }
    result = WithPath(result, *ref);
  }
  if (result.layers == document().layers) return;
  Commit("convert anchor", {std::move(result), selection()});
}

void Editor::RemoveSelectedAnchors() {
  if (anchors_.empty()) return;
  core::Document result = document();
  std::map<std::string, std::vector<int>> by_path;
  for (const auto& a : anchors_) by_path[a.id].push_back(a.index);
  core::IdSet emptied;
  for (auto& [id, indices] : by_path) {
    auto ref = GetPath(result, id);
    if (!ref) continue;
    std::sort(indices.rbegin(), indices.rend());  // Back to front keeps indices valid.
    for (int i : indices) {
      if (i < static_cast<int>(ref->path.anchors.size())) geometry::RemoveAnchor(ref->path, i);
    }
    if (ref->path.anchors.empty()) {
      emptied.insert(id);
    } else {
      result = WithPath(result, *ref);
    }
  }
  result = core::RemoveObjects(result, emptied);
  anchors_.clear();
  core::IdSet selection = this->selection();
  for (const auto& id : emptied) selection.erase(id);
  Commit("delete anchor", {std::move(result), std::move(selection)});
}

void Editor::DeleteSelectedAnchors() {
  if (anchors_.empty()) return;
  core::Document result = document();
  std::map<std::string, std::set<int>> by_path;
  for (const auto& a : anchors_) by_path[a.id].insert(a.index);
  core::IdSet emptied, remaining;
  for (const auto& [id, indices] : by_path) {
    auto ref = GetPath(result, id);
    if (!ref) continue;
    const auto pieces = geometry::DeleteAnchors(ref->path, indices);
    if (pieces.empty()) {
      emptied.insert(id);
      continue;
    }
    // The first piece keeps the id; the others become new paths in front.
    for (size_t i = 1; i < pieces.size(); ++i) {
      core::IdSet copies;
      result = core::DuplicateObjects(result, {id}, ids_, &copies);
      result = WithPath(result, {*copies.begin(), ref->to_document, pieces[i]});
      remaining.insert(*copies.begin());
    }
    ref->path = pieces[0];
    result = WithPath(result, *ref);
    remaining.insert(id);
  }
  result = core::RemoveObjects(result, emptied);
  anchors_.clear();
  Commit("delete", {std::move(result), std::move(remaining)});
}

void Editor::CutAtSelectedAnchor() {
  if (anchors_.size() != 1) return;
  const AnchorRef a = *anchors_.begin();
  auto ref = GetPath(document(), a.id);
  if (!ref) return;
  const auto pieces = geometry::CutAt(ref->path, a.index);
  if (pieces.size() == 1 && pieces[0] == ref->path) return;  // An end: nothing to cut.
  ref->path = pieces[0];
  core::Document result = WithPath(document(), *ref);
  core::IdSet selection = {a.id};
  if (pieces.size() == 2) {
    // The second half becomes a new path just in front of the first.
    core::IdSet copies;
    result = core::DuplicateObjects(result, {a.id}, ids_, &copies);
    PathRef second{*copies.begin(), ref->to_document, pieces[1]};
    result = WithPath(result, second);
    selection.insert(second.id);
  }
  anchors_.clear();
  Commit("cut path", {std::move(result), std::move(selection)});
}

void Editor::JoinSelectedEnds() {
  // Two selected anchors that are ends of open paths.
  if (anchors_.size() != 2) return;
  AnchorRef a = *anchors_.begin(), b = *std::next(anchors_.begin());
  // The result keeps the id and stacking place of the backmost path.
  if (a.id != b.id) {
    const auto order = core::FindObjects(document(), {a.id, b.id});
    if (order.size() == 2 && core::CommonOf(*order.front().object).id == b.id) std::swap(a, b);
  }
  auto pa = GetPath(document(), a.id), pb = GetPath(document(), b.id);
  if (!pa || !pb || pa->path.closed || pb->path.closed) return;
  auto is_end = [](const PathRef& r, int i) {
    return i == 0 || i == static_cast<int>(r.path.anchors.size()) - 1;
  };
  if (!is_end(*pa, a.index) || !is_end(*pb, b.index)) return;
  core::Document result;
  if (a.id == b.id) {
    // Both ends of one path: close it.
    pa->path.closed = true;
    result = WithPath(document(), *pa);
  } else {
    const PathData other = core::Transformed(pb->path, Inverse(pa->to_document) * pb->to_document);
    const bool a_at_end = a.index != 0 || pa->path.anchors.size() == 1;
    const bool b_at_end = b.index != 0 || pb->path.anchors.size() == 1;
    pa->path = geometry::Join(pa->path, a_at_end, other, b_at_end);
    result = core::RemoveObjects(WithPath(document(), *pa), {pb->id});
  }
  anchors_.clear();
  Commit("join", {std::move(result), {a.id}});
}

}  // namespace leinwand::editor
