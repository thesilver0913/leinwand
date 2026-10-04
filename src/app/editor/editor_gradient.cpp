// SPDX-License-Identifier: GPL-3.0-or-later
// Gradients (spec 7.2, the Gradient panel and the gradient tool). They act on
// the active side, fill or stroke, of each selected object.
#include <algorithm>
#include <map>

#include "core/gradient.h"
#include "core/style.h"
#include "editor/editor.h"
#include "editor/tool_math.h"
#include "geometry/bezier.h"

namespace leinwand::editor {

namespace {

using core::Gradient;
using core::Matrix;
using core::Point;

// The active side's front item's gradient slot, if the item exists.
std::optional<Gradient>* GradientSlot(core::Appearance& appearance, bool fill) {
  for (auto& item : appearance) {
    if (fill) {
      if (auto* f = std::get_if<core::Fill>(&item)) return &f->gradient;
    } else if (auto* s = std::get_if<core::Stroke>(&item)) {
      return &s->gradient;
    }
  }
  return nullptr;
}

const std::optional<Gradient>* GradientSlot(const core::Appearance& appearance, bool fill) {
  return GradientSlot(const_cast<core::Appearance&>(appearance), fill);
}

// Rewrites every leaf object under `object` (paths, compound paths, shapes)
// through `edit`, which gets the leaf's appearance and the transform from
// its coordinates to the document.
core::ObjectPtr EditLeaves(const core::ObjectPtr& object, const Matrix& to_document,
                           const std::function<void(core::Object&, const Matrix&)>& edit) {
  if (const auto* group = std::get_if<core::GroupObject>(object.get())) {
    core::GroupObject copy = *group;
    for (auto& child : copy.children)
      child = EditLeaves(child, to_document * group->transform, edit);
    return core::MakeObject(std::move(copy));
  }
  if (std::holds_alternative<core::PreservedObject>(*object)) return object;
  core::Object copy = *object;
  edit(copy, to_document);
  return std::make_shared<const core::Object>(std::move(copy));
}

// The item's solid paint follows the gradient's first stop: the panels show
// it, and readers without gradients fall back to it (spec 3.2).
void SyncPaint(core::Appearance& appearance, bool fill) {
  for (auto& item : appearance) {
    if (fill) {
      if (auto* f = std::get_if<core::Fill>(&item)) {
        if (f->gradient && !f->gradient->stops.empty()) f->paint = f->gradient->stops.front().color;
        return;
      }
    } else if (auto* s = std::get_if<core::Stroke>(&item)) {
      if (s->gradient && !s->gradient->stops.empty()) s->paint = s->gradient->stops.front().color;
      return;
    }
  }
}

core::Appearance& AppearanceOf(core::Object& object) {
  return std::visit([](auto& o) -> core::Appearance& { return o.common.appearance; }, object);
}

// Two colors for a new gradient: white to black, as Illustrator starts.
Gradient NewGradient(core::GradientType type, const core::Rect& bounds) {
  return core::DefaultGradient(type, bounds, core::RgbColor{1, 1, 1}, core::RgbColor{0, 0, 0});
}

// The same gradient as another type, about the same axis.
Gradient Retyped(Gradient g, core::GradientType type) {
  if (g.type == type) return g;
  const Point d = g.end - g.start;
  if (type == core::GradientType::kRadial) {
    // Linear to radial: centred on the middle of the axis.
    g.start = g.start + d * 0.5;
    g.end = g.start + d * 0.5;
  } else {
    g.start = g.start - d;
  }
  g.type = type;
  g.focal.reset();
  g.aspect = 1.0;
  return g;
}

}  // namespace

void Editor::EditGradient(const std::string& action,
                          const std::function<void(core::Gradient&)>& edit) {
  ApplyStyle(action, [&](core::Appearance& a) {
    auto* slot = GradientSlot(a, fill_active_);
    if (slot && *slot) {
      edit(**slot);
      SyncPaint(a, fill_active_);
    }
  });
}

void Editor::ApplyGradient(core::GradientType type) {
  // Objects with a gradient change its type; the others get a new one
  // across their own bounds. The style for new objects too.
  auto apply = [&](core::Appearance& a, const core::Rect& bounds) {
    if (!GradientSlot(a, fill_active_)) {
      // No item on that side yet: add one.
      if (fill_active_) {
        core::SetFillPaint(a, core::RgbColor{1, 1, 1});
      } else {
        core::SetStrokePaint(a, core::RgbColor{1, 1, 1});
      }
    }
    auto* slot = GradientSlot(a, fill_active_);
    *slot = *slot ? Retyped(**slot, type) : NewGradient(type, bounds);
    SyncPaint(a, fill_active_);
  };
  apply(new_style_, core::Rect::FromXYWH(0, 0, 100, 100));
  if (selection().empty()) return;
  std::map<std::string, core::ObjectPtr> replacements;
  for (const core::Located& found :
       core::FindObjects(document(), core::WithoutNested(document(), selection()))) {
    replacements[core::CommonOf(*found.object).id] =
        EditLeaves(found.object, found.to_document, [&](core::Object& leaf, const Matrix&) {
          apply(AppearanceOf(leaf), geometry::Bounds(leaf));
        });
  }
  gradient_stop_ = -1;
  Commit("gradient", {core::ReplaceObjects(document(), replacements), selection()});
}

void Editor::SetGradientAngle(double degrees) {
  EditGradient("gradient", [&](Gradient& g) { g = core::WithAngle(g, degrees); });
}

void Editor::SetGradientAspect(double aspect) {
  EditGradient("gradient", [&](Gradient& g) {
    if (g.type == core::GradientType::kRadial) g.aspect = std::clamp(aspect, 0.01, 100.0);
  });
}

int Editor::AddGradientStop(double offset) {
  int added = -1;
  EditGradient("gradient stop", [&](Gradient& g) { added = core::AddStop(g, offset); });
  if (added >= 0) SelectGradientStop(added);
  return added;
}

void Editor::RemoveGradientStop(int index) {
  EditGradient("gradient stop", [&](Gradient& g) { core::RemoveStop(g, index); });
  gradient_stop_ = -1;
}

int Editor::MoveGradientStop(int index, double offset) {
  int moved = index;
  EditGradient("gradient stop", [&](Gradient& g) { moved = core::MoveStop(g, index, offset); });
  SelectGradientStop(moved);
  return moved;
}

void Editor::SetGradientStopOpacity(int index, double opacity) {
  EditGradient("gradient stop", [&](Gradient& g) {
    if (index >= 0 && index < static_cast<int>(g.stops.size())) {
      g.stops[size_t(index)].opacity = std::clamp(opacity, 0.0, 1.0);
    }
  });
}

void Editor::SetGradientStopMidpoint(int index, double midpoint) {
  EditGradient("gradient stop", [&](Gradient& g) {
    if (index >= 0 && index < static_cast<int>(g.stops.size())) {
      g.stops[size_t(index)].midpoint = std::clamp(midpoint, 0.05, 0.95);
    }
  });
}

// The gradient tool's line: the active gradient of the first selected leaf,
// in document coordinates.
std::optional<std::pair<Point, Point>> Editor::GradientLine() const {
  std::optional<std::pair<Point, Point>> line;
  for (const core::Located& found :
       core::FindObjects(document(), core::WithoutNested(document(), selection()))) {
    std::function<void(const core::Object&, const Matrix&)> visit = [&](const core::Object& o,
                                                                        const Matrix& m) {
      if (line) return;
      if (const auto* group = std::get_if<core::GroupObject>(&o)) {
        for (const auto& child : group->children) visit(*child, m * group->transform);
        return;
      }
      const auto* slot = GradientSlot(core::CommonOf(o).appearance, fill_active_);
      if (slot && *slot) line = std::pair{m.Map((*slot)->start), m.Map((*slot)->end)};
    };
    visit(*found.object, found.to_document);
    if (line) break;
  }
  return line;
}

void Editor::GradientDown(Point p, double pick) {
  drag_ = {};
  if (selection().empty()) return;
  drag_.kind = DragKind::kGradient;
  drag_.start = drag_.current = p;
  drag_.threshold = pick / 2;
  drag_.pick = pick;
  drag_.index = 0;  // A new line from here.
  if (const auto line = GradientLine()) {
    // On an end of the line: move that end only.
    if (Distance(p, line->first) <= pick * 1.5) {
      drag_.index = 1;
      drag_.box = core::Rect::FromPoint(line->second);  // The other end stays.
    } else if (Distance(p, line->second) <= pick * 1.5) {
      drag_.index = 2;
      drag_.box = core::Rect::FromPoint(line->first);
    }
  }
}

void Editor::GradientDrag() {
  Point p = drag_.current;
  if (drag_.index == 0 && Distance(p, drag_.start) < drag_.threshold) {
    drag_.preview.reset();
    return;
  }
  const Point fixed{drag_.box.left, drag_.box.top};
  Point from = drag_.start, to = p;
  if (drag_.index == 1) {
    from = p;
    to = fixed;
  } else if (drag_.index == 2) {
    from = fixed;
  }
  if (drag_.modifiers.shift) {
    if (drag_.index == 1) {
      from = to + ConstrainTo45(from - to);
    } else {
      to = from + ConstrainTo45(to - from);
    }
  }
  const core::EditorState& base = history_.current();
  std::map<std::string, core::ObjectPtr> replacements;
  for (const core::Located& found :
       core::FindObjects(base.document, core::WithoutNested(base.document, base.selection))) {
    replacements[core::CommonOf(*found.object).id] =
        EditLeaves(found.object, found.to_document, [&](core::Object& leaf, const Matrix& m) {
          core::Appearance& a = AppearanceOf(leaf);
          if (!GradientSlot(a, fill_active_)) {
            if (fill_active_) {
              core::SetFillPaint(a, core::RgbColor{1, 1, 1});
            } else {
              core::SetStrokePaint(a, core::RgbColor{1, 1, 1});
            }
          }
          auto* slot = GradientSlot(a, fill_active_);
          if (!*slot) {
            *slot = NewGradient(core::GradientType::kLinear, geometry::Bounds(leaf));
            SyncPaint(a, fill_active_);
          }
          // The line is drawn in the document; the gradient lives in the
          // leaf's coordinates.
          const Matrix to_local = m.Inverted().value_or(Matrix{});
          (*slot)->start = to_local.Map(from);
          (*slot)->end = to_local.Map(to);
          (*slot)->focal.reset();
        });
  }
  drag_.preview =
      core::EditorState{core::ReplaceObjects(base.document, replacements), base.selection};
}

}  // namespace leinwand::editor
