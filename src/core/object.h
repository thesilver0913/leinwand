// SPDX-License-Identifier: GPL-3.0-or-later
// Document objects (spec 3, "階層"). Objects are immutable and shared through
// ObjectPtr: an edit builds new nodes along the changed branch and reuses the
// rest (structural sharing), which keeps undo history and autosave cheap.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/appearance.h"
#include "core/path.h"
#include "core/shape.h"
#include "core/text.h"
#include "core/types.h"

namespace leinwand::core {

struct Object;
using ObjectPtr = std::shared_ptr<const Object>;
struct OpacityMask;

// Fields every object has (spec 3.2, "オブジェクトの共通フィールド").
struct ObjectCommon {
  std::string id;    // Unique in the document; never changes once assigned.
  std::string name;  // Empty means "derive from the object type".
  bool visible = true;
  bool locked = false;
  double opacity = 1.0;
  BlendMode blend_mode = BlendMode::kNormal;
  Appearance appearance;
  // The opacity mask (spec 7.2, the Transparency panel), or none.
  std::shared_ptr<const OpacityMask> mask;
  // Fields of a newer file version this one does not know, as JSON object
  // text; written back unchanged (spec 3.3, "未知のフィールド").
  std::string unknown_fields;
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
  // Isolate blending: the children's blend modes act within the group only.
  bool isolated = false;
  Matrix transform;
  // Create Outlines keeps the text it came from (spec 7.5), in the group's
  // coordinates, so that it can be turned back into text. Not drawn.
  ObjectPtr outlined_text;
};

// A live shape (spec 4.1): parameters plus a transform that places the
// shape's centre. The transform holds rotation, translation and reflection
// only; scaling goes into the parameters.
struct ShapeObject {
  ObjectCommon common;
  ShapeParams shape;
  Matrix transform;
};

// Text (spec 5). Point text starts at the transform's origin, on the first
// line's baseline, and does not wrap; lines follow downwards. The transform
// holds the position and any rotation, scaling or shear of the whole text.
enum class TextKind { kPoint };
enum class TextOrientation { kHorizontal };

struct TextObject {
  ObjectCommon common;
  TextKind kind = TextKind::kPoint;
  TextOrientation orientation = TextOrientation::kHorizontal;
  StoryPtr story;
  Matrix transform;
};

// Content kept but not understood (spec 3.3, 6.1): an object of a type from
// a newer .lwd, or an SVG element Leinwand does not support. It is written
// back as it came (to the same format) and shown as a frame when its bounds
// are known. It can be moved (the transform), restacked and deleted, but not
// edited.
struct PreservedObject {
  ObjectCommon common;
  std::string format;  // "lwd" (JSON text) or "svg" (XML text).
  std::string data;
  std::optional<Rect> bounds;  // Before `transform`.
  Matrix transform;
};

struct Object : std::variant<PathObject, CompoundPathObject, GroupObject, ShapeObject,
                             PreservedObject, TextObject> {
  using variant::variant;
  // std::visit on classes derived from std::variant needs C++23 (P2162).
  const variant& base() const { return *this; }
};

// An opacity mask: the art's luminance becomes the masked object's opacity
// (white shows, black hides). The art is in the same coordinates as the
// masked object (its parent's) and moves with it.
struct OpacityMask {
  ObjectPtr art;
  // Outside the art the object is hidden (Illustrator's "Clip"); otherwise
  // it shows there.
  bool clip = true;
  bool invert = false;  // Dark shows, light hides.
};

template <typename T>
ObjectPtr MakeObject(T value) {
  return std::make_shared<const Object>(std::move(value));
}

// The outline of a path-like object (path, compound path or shape) in its
// parent's coordinates, as subpaths; empty for groups. A preserved object
// gives its frame (none without known bounds).
std::vector<PathData> OutlineOf(const Object& object);
FillRule FillRuleOf(const Object& object);
// Whether every subpath is closed (inside/outside strokes need a region).
bool IsClosed(const Object& object);

// Replaces a live shape with a plain path that looks the same (spec 4.1:
// done when a shape can no longer stay live). Other objects pass through.
ObjectPtr Expanded(const ObjectPtr& object);

inline const ObjectCommon& CommonOf(const Object& object) {
  return std::visit([](const auto& o) -> const ObjectCommon& { return o.common; }, object.base());
}

}  // namespace leinwand::core
