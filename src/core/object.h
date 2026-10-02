// SPDX-License-Identifier: GPL-3.0-or-later
// Document objects (spec 3, "階層"). Objects are immutable and shared through
// ObjectPtr: an edit builds new nodes along the changed branch and reuses the
// rest (structural sharing), which keeps undo history and autosave cheap.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/appearance.h"
#include "core/path.h"
#include "core/types.h"

namespace leinwand::core {

struct Object;
using ObjectPtr = std::shared_ptr<const Object>;

// Fields every object has (spec 3.2, "オブジェクトの共通フィールド").
struct ObjectCommon {
  std::string id;    // Unique in the document; never changes once assigned.
  std::string name;  // Empty means "derive from the object type".
  bool visible = true;
  bool locked = false;
  double opacity = 1.0;
  BlendMode blend_mode = BlendMode::kNormal;
  Appearance appearance;
};

// Coordinates are stored with transforms already applied.
struct PathObject {
  ObjectCommon common;
  PathData path;
};

struct CompoundPathObject {
  ObjectCommon common;
  std::vector<PathData> subpaths;
  FillRule fill_rule = FillRule::kNonZero;
};

struct GroupObject {
  ObjectCommon common;
  std::vector<ObjectPtr> children;  // Back to front: children[0] is painted first.
  // Clipping group: the frontmost child (children.back()) is the clipping
  // path and is not painted itself, as in Illustrator.
  bool clipped = false;
  Matrix transform;
};

struct Object : std::variant<PathObject, CompoundPathObject, GroupObject> {
  using variant::variant;
  // std::visit on classes derived from std::variant needs C++23 (P2162).
  const variant& base() const { return *this; }
};

template <typename T>
ObjectPtr MakeObject(T value) {
  return std::make_shared<const Object>(std::move(value));
}

inline const ObjectCommon& CommonOf(const Object& object) {
  return std::visit([](const auto& o) -> const ObjectCommon& { return o.common; }, object.base());
}

}  // namespace leinwand::core
