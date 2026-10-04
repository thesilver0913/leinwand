// SPDX-License-Identifier: GPL-3.0-or-later
// Clipping masks and the Transparency panel (spec 7.2): blend mode,
// isolated blending and opacity masks.
#include <algorithm>
#include <map>

#include "core/transform.h"
#include "editor/editor.h"

namespace leinwand::editor {

namespace {

using core::Matrix;
using core::ObjectPtr;

// A copy of the object with its common fields edited.
ObjectPtr WithCommon(const ObjectPtr& object,
                     const std::function<void(core::ObjectCommon&)>& edit) {
  return std::visit(
      [&](const auto& o) -> ObjectPtr {
        auto copy = o;
        edit(copy.common);
        return core::MakeObject(std::move(copy));
      },
      object->base());
}

bool IsPathLike(const core::Object& object) {
  return std::holds_alternative<core::PathObject>(object) ||
         std::holds_alternative<core::CompoundPathObject>(object) ||
         std::holds_alternative<core::ShapeObject>(object);
}

}  // namespace

void Editor::EditSelected(const std::string& action,
                          const std::function<ObjectPtr(const ObjectPtr&)>& edit) {
  if (selection().empty()) return;
  std::map<std::string, ObjectPtr> replacements;
  for (const core::Located& found :
       core::FindObjects(document(), core::WithoutNested(document(), selection()))) {
    ObjectPtr edited = edit(found.object);
    if (edited && edited != found.object) {
      replacements[core::CommonOf(*found.object).id] = std::move(edited);
    }
  }
  if (replacements.empty()) return;
  Commit(action, {core::ReplaceObjects(document(), replacements), selection()});
}

void Editor::SetBlendMode(core::BlendMode mode) {
  EditSelected("blend mode", [&](const ObjectPtr& object) -> ObjectPtr {
    if (core::CommonOf(*object).blend_mode == mode) return object;
    return WithCommon(object, [&](core::ObjectCommon& c) { c.blend_mode = mode; });
  });
}

void Editor::SetIsolated(bool isolated) {
  EditSelected("isolate blending", [&](const ObjectPtr& object) -> ObjectPtr {
    const auto* group = std::get_if<core::GroupObject>(object.get());
    if (!group || group->isolated == isolated) return object;
    core::GroupObject copy = *group;
    copy.isolated = isolated;
    return core::MakeObject(std::move(copy));
  });
}

void Editor::MakeClippingMask() {
  const auto members = core::FindObjects(document(), core::WithoutNested(document(), selection()));
  // The frontmost object clips the others; it has to be a path.
  if (members.size() < 2 || !IsPathLike(*members.back().object)) return;
  const std::string id = ids_.Next();
  core::Document result = core::GroupObjects(document(), selection(), id);
  // The clipping path loses its paint, as in Illustrator.
  const auto found = core::FindObjects(result, {id});
  if (found.size() != 1) return;
  core::GroupObject group = std::get<core::GroupObject>(*found[0].object);
  group.clipped = true;
  group.children.back() =
      WithCommon(group.children.back(), [](core::ObjectCommon& c) { c.appearance.clear(); });
  result = core::ReplaceObjects(result, {{id, core::MakeObject(std::move(group))}});
  anchors_.clear();
  Commit("make clipping mask", {std::move(result), {id}});
}

void Editor::ReleaseClippingMask() {
  // The groups stay; their clipping paths stay too, unpainted.
  EditSelected("release clipping mask", [](const ObjectPtr& object) -> ObjectPtr {
    const auto* group = std::get_if<core::GroupObject>(object.get());
    if (!group || !group->clipped) return object;
    core::GroupObject copy = *group;
    copy.clipped = false;
    return core::MakeObject(std::move(copy));
  });
}

void Editor::MakeOpacityMask() {
  const auto members = core::FindObjects(document(), core::WithoutNested(document(), selection()));
  if (members.size() < 2) return;
  // The frontmost object becomes the mask of the rest (grouped when there
  // are several), as in Illustrator.
  const core::Located& front = members.back();
  if (core::CommonOf(*front.object).mask) return;
  const ObjectPtr art_in_document = core::Transformed(front.object, front.to_document);
  const std::string art_id = core::CommonOf(*front.object).id;
  core::Document result = core::RemoveObjects(document(), {art_id});
  core::IdSet rest;
  for (size_t i = 0; i + 1 < members.size(); ++i) {
    rest.insert(core::CommonOf(*members[i].object).id);
  }
  std::string target = *rest.begin();
  if (rest.size() > 1) {
    target = ids_.Next();
    result = core::GroupObjects(result, rest, target);
  }
  const auto found = core::FindObjects(result, {target});
  if (found.size() != 1) return;
  if (core::CommonOf(*found[0].object).mask) return;  // One mask per object.
  core::OpacityMask mask;
  mask.art = core::Transformed(art_in_document, found[0].to_document.Inverted().value_or(Matrix{}));
  auto shared = std::make_shared<const core::OpacityMask>(std::move(mask));
  result = core::ReplaceObjects(
      result,
      {{target, WithCommon(found[0].object, [&](core::ObjectCommon& c) { c.mask = shared; })}});
  anchors_.clear();
  Commit("make opacity mask", {std::move(result), {target}});
}

void Editor::ReleaseOpacityMask() {
  // The mask art goes back in front of its object, as it was.
  core::Document result = document();
  core::IdSet selected = selection();
  bool any = false;
  for (const core::Located& found :
       core::FindObjects(document(), core::WithoutNested(document(), selection()))) {
    const auto& mask = core::CommonOf(*found.object).mask;
    if (!mask || !mask->art) continue;
    any = true;
    // A temporary group of the two, released in place.
    const std::string temporary = ids_.Next();
    core::GroupObject pair;
    pair.common.id = temporary;
    pair.children = {WithCommon(found.object, [](core::ObjectCommon& c) { c.mask.reset(); }),
                     mask->art};
    result = core::ReplaceObjects(
        result, {{core::CommonOf(*found.object).id, core::MakeObject(std::move(pair))}});
    core::IdSet released;
    result = core::UngroupObjects(result, {temporary}, &released);
    selected.insert(core::CommonOf(*mask->art).id);
  }
  if (!any) return;
  Commit("release opacity mask", {std::move(result), std::move(selected)});
}

void Editor::EditMask(const std::function<void(core::OpacityMask&)>& edit) {
  EditSelected("opacity mask", [&](const ObjectPtr& object) -> ObjectPtr {
    const auto& mask = core::CommonOf(*object).mask;
    if (!mask) return object;
    core::OpacityMask copy = *mask;
    edit(copy);
    auto shared = std::make_shared<const core::OpacityMask>(std::move(copy));
    return WithCommon(object, [&](core::ObjectCommon& c) { c.mask = shared; });
  });
}

void Editor::SetMaskClip(bool clip) {
  EditMask([&](core::OpacityMask& m) { m.clip = clip; });
}

void Editor::SetMaskInvert(bool invert) {
  EditMask([&](core::OpacityMask& m) { m.invert = invert; });
}

TransparencyState Editor::Transparency() const {
  TransparencyState state;
  const auto found = core::FindObjects(document(), core::WithoutNested(document(), selection()));
  if (found.empty()) return state;
  const core::ObjectCommon& first = core::CommonOf(*found[0].object);
  state.selected = true;
  state.opacity = first.opacity;
  state.blend_mode = first.blend_mode;
  for (const auto& f : found) {
    const core::ObjectCommon& c = core::CommonOf(*f.object);
    state.opacity_mixed |= c.opacity != state.opacity;
    state.blend_mixed |= c.blend_mode != state.blend_mode;
    if (c.mask && !state.mask) state.mask = *c.mask;
    if (const auto* group = std::get_if<core::GroupObject>(f.object.get())) {
      state.has_group = true;
      state.isolated = state.isolated || group->isolated;
    }
  }
  return state;
}

}  // namespace leinwand::editor
