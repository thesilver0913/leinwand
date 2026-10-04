// SPDX-License-Identifier: GPL-3.0-or-later
// Artboards (spec 7, "アートボード"): the artboard tool, and the edits the
// Artboards panel makes. Artboards are part of the document, so every edit
// is undoable; which one is active is not.
#include <algorithm>

#include "core/marks.h"
#include "editor/editor.h"
#include "editor/tool_math.h"
#include "geometry/bezier.h"

namespace leinwand::editor {

namespace {

using core::Point;
using core::Rect;

constexpr Handle kArtboardHandles[] = {Handle::kTopLeft,    Handle::kTop,         Handle::kTopRight,
                                       Handle::kRight,      Handle::kBottomRight, Handle::kBottom,
                                       Handle::kBottomLeft, Handle::kLeft};

bool MovesLeft(Handle h) {
  return h == Handle::kTopLeft || h == Handle::kLeft || h == Handle::kBottomLeft;
}
bool MovesRight(Handle h) {
  return h == Handle::kTopRight || h == Handle::kRight || h == Handle::kBottomRight;
}
bool MovesTop(Handle h) {
  return h == Handle::kTopLeft || h == Handle::kTop || h == Handle::kTopRight;
}
bool MovesBottom(Handle h) {
  return h == Handle::kBottomLeft || h == Handle::kBottom || h == Handle::kBottomRight;
}

// A rect from two corners, at least a point wide and high.
Rect Spanning(Point a, Point b) {
  Rect r{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
  r.right = std::max(r.right, r.left + 1);
  r.bottom = std::max(r.bottom, r.top + 1);
  return r;
}

}  // namespace

int Editor::active_artboard() const {
  const int count = static_cast<int>(document().artboards.size());
  if (count == 0) return -1;
  return std::clamp(active_artboard_, 0, count - 1);
}

void Editor::SetActiveArtboard(int index) {
  if (index < 0 || index >= static_cast<int>(document().artboards.size())) return;
  active_artboard_ = index;
}

std::string Editor::NewArtboardName(const core::Document& document) const {
  // "Artboard N" with the first N not in use.
  for (int n = static_cast<int>(document.artboards.size()) + 1;; ++n) {
    const std::string name = artboard_prefix_ + " " + std::to_string(n);
    if (std::none_of(document.artboards.begin(), document.artboards.end(),
                     [&](const core::Artboard& a) { return a.name == name; })) {
      return name;
    }
  }
}

void Editor::AddArtboard(const Rect& bounds) {
  core::Document result = document();
  core::Artboard artboard;
  artboard.id = ids_.Next();
  artboard.name = NewArtboardName(result);
  artboard.bounds = bounds;
  result.artboards.push_back(std::move(artboard));
  const int index = static_cast<int>(result.artboards.size()) - 1;
  Commit("add artboard", {std::move(result), selection()});
  active_artboard_ = index;
}

void Editor::RemoveActiveArtboard() {
  if (document().artboards.size() < 2) return;
  core::Document result = document();
  const int index = active_artboard();
  result.artboards.erase(result.artboards.begin() + index);
  Commit("delete artboard", {std::move(result), selection()});
  active_artboard_ = std::max(0, index - 1);
}

void Editor::MoveArtboard(int from, int to) {
  const int count = static_cast<int>(document().artboards.size());
  if (from < 0 || from >= count || to < 0 || to >= count || from == to) return;
  core::Document result = document();
  core::Artboard moving = std::move(result.artboards[size_t(from)]);
  result.artboards.erase(result.artboards.begin() + from);
  result.artboards.insert(result.artboards.begin() + to, std::move(moving));
  const bool was_active = active_artboard() == from;
  Commit("reorder artboards", {std::move(result), selection()});
  if (was_active) active_artboard_ = to;
}

void Editor::RenameArtboard(int index, const std::string& name) {
  if (index < 0 || index >= static_cast<int>(document().artboards.size())) return;
  if (name.empty() || document().artboards[size_t(index)].name == name) return;
  core::Document result = document();
  result.artboards[size_t(index)].name = name;
  Commit("rename artboard", {std::move(result), selection()});
}

void Editor::SetArtboardBounds(int index, const Rect& bounds) {
  if (index < 0 || index >= static_cast<int>(document().artboards.size())) return;
  if (bounds.width() <= 0 || bounds.height() <= 0) return;
  if (document().artboards[size_t(index)].bounds == bounds) return;
  core::Document result = document();
  result.artboards[size_t(index)].bounds = bounds;
  Commit("edit artboard", {std::move(result), selection()});
}

void Editor::SetCover(const core::CoverSpec& spec, const core::CoverNames& names) {
  if (spec.width <= 0 || spec.height <= 0) return;
  if (document().cover == spec) return;
  Commit("cover", {core::WithCover(document(), spec, names), selection()});
}

void Editor::ArtboardDown(Point p, double pick) {
  drag_ = {};
  drag_.start = drag_.current = p;
  drag_.threshold = pick / 2;
  const auto& artboards = document().artboards;
  // A handle of the active artboard resizes it.
  if (const int active = active_artboard(); active >= 0) {
    const Rect box = artboards[size_t(active)].bounds;
    for (Handle h : kArtboardHandles) {
      if (Distance(p, HandlePosition(box, h)) <= pick * 1.5) {
        drag_.kind = DragKind::kArtboardResize;
        drag_.index = active;
        drag_.box = box;
        drag_.handle = h;
        return;
      }
    }
  }
  // Inside an artboard (the last listed wins): it becomes active and moves
  // with the artwork lying entirely on it.
  for (int i = static_cast<int>(artboards.size()) - 1; i >= 0; --i) {
    const Rect box = artboards[size_t(i)].bounds;
    if (!box.Contains(p)) continue;
    active_artboard_ = i;
    drag_.kind = DragKind::kArtboardMove;
    drag_.index = i;
    drag_.box = box;
    for (const core::Located& found :
         core::FindObjects(document(), core::AllObjectIds(document()))) {
      const Rect b = geometry::MapRect(geometry::Bounds(*found.object), found.to_document);
      if (b.IsValid() && b.left >= box.left && b.right <= box.right && b.top >= box.top &&
          b.bottom <= box.bottom) {
        drag_.carried.insert(core::CommonOf(*found.object).id);
      }
    }
    drag_.carried = core::WithoutNested(document(), drag_.carried);
    return;
  }
  drag_.kind = DragKind::kArtboardDraw;
  drag_.index = static_cast<int>(artboards.size());
}

void Editor::ArtboardDrag() {
  const Point p = drag_.current;
  if (Distance(p, drag_.start) < drag_.threshold) {
    drag_.preview.reset();
    return;
  }
  const core::EditorState& base = history_.current();
  core::Document result = base.document;
  switch (drag_.kind) {
    case DragKind::kArtboardDraw: {
      core::Artboard artboard;
      if (drag_.new_id.empty()) drag_.new_id = ids_.Next();
      artboard.id = drag_.new_id;
      artboard.name = NewArtboardName(result);
      artboard.bounds = Spanning(drag_.start, p);
      result.artboards.push_back(std::move(artboard));
      break;
    }
    case DragKind::kArtboardMove: {
      Point d = p - drag_.start;
      if (drag_.modifiers.shift) d = ConstrainTo45(d);
      Rect& box = result.artboards[size_t(drag_.index)].bounds;
      box = {drag_.box.left + d.x, drag_.box.top + d.y, drag_.box.right + d.x,
             drag_.box.bottom + d.y};
      if (!drag_.carried.empty()) {
        result = core::TransformObjects(result, drag_.carried, core::Matrix::Translate(d.x, d.y));
      }
      break;
    }
    case DragKind::kArtboardResize: {
      Rect box = drag_.box;
      const Handle h = drag_.handle;
      if (MovesLeft(h)) box.left = p.x;
      if (MovesRight(h)) box.right = p.x;
      if (MovesTop(h)) box.top = p.y;
      if (MovesBottom(h)) box.bottom = p.y;
      result.artboards[size_t(drag_.index)].bounds =
          Spanning({box.left, box.top}, {box.right, box.bottom});
      break;
    }
    default:
      return;
  }
  drag_.preview = core::EditorState{std::move(result), base.selection};
}

// Object > Create Trim Marks (spec 7.5): around the selection, or the
// active artboard (with its bleed) when nothing is selected. A group of
// 0.3 pt black lines, which can be moved and edited like any artwork.
void Editor::CreateTrimMarks(core::TrimMarkStyle style) {
  core::Rect trim;
  double bleed = core::kDefaultBleed;
  if (const auto bounds = SelectionBounds()) {
    trim = *bounds;
  } else {
    const int active = active_artboard();
    if (active < 0) return;
    const core::Artboard& board = document().artboards[std::size_t(active)];
    trim = board.bounds;
    if (board.bleed > 0) bleed = board.bleed;
  }
  core::GroupObject group;
  group.common.id = ids_.Next();
  group.common.name = trim_marks_name_;
  core::Stroke line{core::RgbColor{0, 0, 0}};
  line.width = core::kTrimMarkWidth;
  for (core::PathData& mark : core::TrimMarks(trim, bleed, style)) {
    core::PathObject path;
    path.common.id = ids_.Next();
    path.common.appearance = {line};
    path.path = std::move(mark);
    group.children.push_back(core::MakeObject(std::move(path)));
  }
  const std::string id = group.common.id;
  Commit("create trim marks",
         {WithNewObject(document(), core::MakeObject(std::move(group))), {id}});
}

}  // namespace leinwand::editor
