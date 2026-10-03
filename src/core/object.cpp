// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/object.h"

#include <algorithm>

#include "core/transform.h"

namespace leinwand::core {

std::vector<PathData> OutlineOf(const Object& object) {
  if (const auto* path = std::get_if<PathObject>(&object)) return {path->path};
  if (const auto* compound = std::get_if<CompoundPathObject>(&object)) return compound->subpaths;
  if (const auto* shape = std::get_if<ShapeObject>(&object)) {
    return {Transformed(ShapePath(shape->shape), shape->transform)};
  }
  if (const auto* preserved = std::get_if<PreservedObject>(&object)) {
    // Its frame, so that it can be picked, moved and snapped to.
    if (!preserved->bounds) return {};
    const Rect& r = *preserved->bounds;
    PathData frame;
    frame.anchors = {
        {{r.left, r.top}}, {{r.right, r.top}}, {{r.right, r.bottom}}, {{r.left, r.bottom}}};
    frame.closed = true;
    return {Transformed(frame, preserved->transform)};
  }
  return {};
}

FillRule FillRuleOf(const Object& object) {
  const auto* compound = std::get_if<CompoundPathObject>(&object);
  return compound ? compound->fill_rule : FillRule::kNonZero;
}

bool IsClosed(const Object& object) {
  if (std::holds_alternative<GroupObject>(object)) return false;
  const auto outline = OutlineOf(object);
  return !outline.empty() &&
         std::all_of(outline.begin(), outline.end(), [](const PathData& p) { return p.closed; });
}

ObjectPtr Expanded(const ObjectPtr& object) {
  const auto* shape = std::get_if<ShapeObject>(&*object);
  if (!shape) return object;
  PathObject path;
  path.common = shape->common;
  path.path = OutlineOf(*object).front();
  return MakeObject(std::move(path));
}

}  // namespace leinwand::core
