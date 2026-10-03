// SPDX-License-Identifier: GPL-3.0-or-later
// The Align panel (spec 7.2) and Object > Path > Average: moving objects or
// anchors so that their edges, centres or gaps line up.
#include <algorithm>
#include <map>
#include <numeric>

#include "core/transform.h"
#include "editor/editor.h"
#include "geometry/bezier.h"
#include "geometry/path_edit.h"

namespace leinwand::editor {

namespace {

using core::Point;
using core::Rect;

struct Placed {
  std::string id;
  Rect bounds;  // Geometric bounds in document coordinates.
};

// Where `edge` lies on a rect: an x for horizontal edges, else a y.
double EdgeOf(const Rect& r, AlignEdge edge) {
  switch (edge) {
    case AlignEdge::kLeft:
      return r.left;
    case AlignEdge::kHorizontalCenter:
      return (r.left + r.right) / 2;
    case AlignEdge::kRight:
      return r.right;
    case AlignEdge::kTop:
      return r.top;
    case AlignEdge::kVerticalCenter:
      return (r.top + r.bottom) / 2;
    case AlignEdge::kBottom:
      return r.bottom;
  }
  return 0;
}

bool Horizontal(AlignEdge edge) {
  return edge == AlignEdge::kLeft || edge == AlignEdge::kHorizontalCenter ||
         edge == AlignEdge::kRight;
}

Point Shift(AlignEdge edge, double amount) {
  return Horizontal(edge) ? Point{amount, 0} : Point{0, amount};
}

}  // namespace

std::string Editor::key_object() const {
  return selection().contains(key_object_) ? key_object_ : std::string();
}

std::optional<Rect> Editor::KeyObjectBounds() const {
  const std::string key = key_object();
  if (key.empty()) return std::nullopt;
  for (const core::Located& found : core::FindObjects(document(), {key})) {
    return geometry::MapRect(geometry::Bounds(*found.object), found.to_document);
  }
  return std::nullopt;
}

namespace {

// The selected top-level objects with their document bounds, back to front.
std::vector<Placed> PlacedObjects(const core::Document& document, const core::IdSet& selection) {
  std::vector<Placed> placed;
  for (const core::Located& found :
       core::FindObjects(document, core::WithoutNested(document, selection))) {
    const Rect bounds = geometry::MapRect(geometry::Bounds(*found.object), found.to_document);
    if (bounds.IsValid()) placed.push_back({core::CommonOf(*found.object).id, bounds});
  }
  return placed;
}

core::Document Moved(core::Document document, const std::string& id, Point delta) {
  if (delta == Point{}) return document;
  return core::TransformObjects(document, {id}, core::Matrix::Translate(delta.x, delta.y));
}

}  // namespace

void Editor::AlignSelection(AlignEdge edge) {
  // Anchors with direct selection.
  if (tool() == Tool::kDirectSelection && anchors_.size() >= 2) {
    std::map<std::string, std::pair<PathRef, std::vector<int>>> paths;
    Rect bounds;
    for (const AnchorRef& a : anchors_) {
      auto ref = GetPath(document(), a.id);
      if (!ref || a.index >= int(ref->path.anchors.size())) continue;
      bounds = bounds.Union(ref->to_document.Map(ref->path.anchors[size_t(a.index)].position));
      auto [it, added] = paths.try_emplace(a.id, *ref, std::vector<int>{});
      it->second.second.push_back(a.index);
    }
    const Rect target = align_to_ == AlignTo::kArtboard && !document().artboards.empty()
                            ? document().artboards[size_t(active_artboard())].bounds
                            : bounds;
    const double line = EdgeOf(target, edge);
    core::Document result = document();
    for (auto& [id, entry] : paths) {
      auto& [ref, indices] = entry;
      const core::Matrix to_local = ref.to_document.Inverted().value_or(core::Matrix{});
      for (int index : indices) {
        Point p = ref.to_document.Map(ref.path.anchors[size_t(index)].position);
        (Horizontal(edge) ? p.x : p.y) = line;
        geometry::MoveAnchor(ref.path, index, to_local.Map(p));
      }
      result = WithPath(result, ref);
    }
    Commit("align anchors", {std::move(result), selection()});
    return;
  }

  const std::vector<Placed> placed = PlacedObjects(document(), selection());
  if (placed.empty()) return;
  // The reference: the artboard, the key object, or the whole selection.
  std::optional<Rect> target;
  const std::string key = key_object();
  if ((align_to_ == AlignTo::kArtboard || placed.size() == 1) && !document().artboards.empty()) {
    target = document().artboards[size_t(active_artboard())].bounds;
  } else if (align_to_ == AlignTo::kKeyObject && !key.empty()) {
    target = KeyObjectBounds();
  }
  if (!target) {
    if (placed.size() < 2) return;
    target = placed[0].bounds;
    for (const Placed& p : placed) target = target->Union(p.bounds);
  }
  const double line = EdgeOf(*target, edge);
  core::Document result = document();
  for (const Placed& p : placed) {
    if (p.id == key && align_to_ == AlignTo::kKeyObject) continue;  // The key stays.
    result = Moved(std::move(result), p.id, Shift(edge, line - EdgeOf(p.bounds, edge)));
  }
  Commit("align", {std::move(result), selection()});
}

void Editor::DistributeSelection(AlignEdge edge) {
  std::vector<Placed> placed = PlacedObjects(document(), selection());
  if (placed.size() < 3) return;
  std::stable_sort(placed.begin(), placed.end(), [edge](const Placed& a, const Placed& b) {
    return EdgeOf(a.bounds, edge) < EdgeOf(b.bounds, edge);
  });
  const double first = EdgeOf(placed.front().bounds, edge);
  const double step = (EdgeOf(placed.back().bounds, edge) - first) / double(placed.size() - 1);
  core::Document result = document();
  for (size_t i = 1; i + 1 < placed.size(); ++i) {
    const double want = first + step * double(i);
    result =
        Moved(std::move(result), placed[i].id, Shift(edge, want - EdgeOf(placed[i].bounds, edge)));
  }
  Commit("distribute", {std::move(result), selection()});
}

void Editor::DistributeSpacing(bool horizontal, std::optional<double> spacing) {
  std::vector<Placed> placed = PlacedObjects(document(), selection());
  if (placed.size() < 2) return;
  const AlignEdge start = horizontal ? AlignEdge::kLeft : AlignEdge::kTop;
  const AlignEdge end = horizontal ? AlignEdge::kRight : AlignEdge::kBottom;
  std::stable_sort(placed.begin(), placed.end(), [start](const Placed& a, const Placed& b) {
    return EdgeOf(a.bounds, start) < EdgeOf(b.bounds, start);
  });
  auto size = [&](const Placed& p) { return EdgeOf(p.bounds, end) - EdgeOf(p.bounds, start); };

  // Where each object should start.
  std::vector<double> starts(placed.size());
  const std::string key = key_object();
  const auto key_at =
      std::find_if(placed.begin(), placed.end(), [&](const Placed& p) { return p.id == key; });
  if (spacing && align_to_ == AlignTo::kKeyObject && key_at != placed.end()) {
    // From the key outwards, `spacing` apart.
    const size_t k = size_t(key_at - placed.begin());
    starts[k] = EdgeOf(placed[k].bounds, start);
    for (size_t i = k + 1; i < placed.size(); ++i) {
      starts[i] = starts[i - 1] + size(placed[i - 1]) + *spacing;
    }
    for (size_t i = k; i-- > 0;) starts[i] = starts[i + 1] - *spacing - size(placed[i]);
  } else {
    if (placed.size() < 3) return;
    const double from = EdgeOf(placed.front().bounds, start);
    const double to = EdgeOf(placed.back().bounds, end);
    double total = 0;
    for (const Placed& p : placed) total += size(p);
    const double gap = (to - from - total) / double(placed.size() - 1);
    double at = from;
    for (size_t i = 0; i < placed.size(); ++i) {
      starts[i] = at;
      at += size(placed[i]) + gap;
    }
  }
  core::Document result = document();
  for (size_t i = 0; i < placed.size(); ++i) {
    result = Moved(std::move(result), placed[i].id,
                   Shift(start, starts[i] - EdgeOf(placed[i].bounds, start)));
  }
  Commit("distribute spacing", {std::move(result), selection()});
}

void Editor::AverageAnchors(bool horizontal, bool vertical) {
  if (anchors_.size() < 2 || (!horizontal && !vertical)) return;
  struct Entry {
    PathRef ref;
    std::vector<int> indices;
  };
  std::map<std::string, Entry> paths;
  Point sum{};
  int count = 0;
  for (const AnchorRef& a : anchors_) {
    auto ref = GetPath(document(), a.id);
    if (!ref || a.index >= int(ref->path.anchors.size())) continue;
    sum = sum + ref->to_document.Map(ref->path.anchors[size_t(a.index)].position);
    ++count;
    auto [it, added] = paths.try_emplace(a.id, Entry{*ref, {}});
    it->second.indices.push_back(a.index);
  }
  if (count < 2) return;
  const Point mean = sum * (1.0 / count);
  core::Document result = document();
  for (auto& [id, entry] : paths) {
    const core::Matrix to_local = entry.ref.to_document.Inverted().value_or(core::Matrix{});
    for (int index : entry.indices) {
      Point p = entry.ref.to_document.Map(entry.ref.path.anchors[size_t(index)].position);
      // Horizontal: onto one horizontal line (a shared y); vertical: a
      // shared x, as Illustrator's Average dialog.
      if (horizontal) p.y = mean.y;
      if (vertical) p.x = mean.x;
      geometry::MoveAnchor(entry.ref.path, index, to_local.Map(p));
    }
    result = WithPath(result, entry.ref);
  }
  Commit("average", {std::move(result), selection()});
}

}  // namespace leinwand::editor
