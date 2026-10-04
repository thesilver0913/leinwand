// SPDX-License-Identifier: GPL-3.0-or-later
// The Pathfinder panel's commands (spec 4.3): the selected objects become
// filled regions, the operation runs on them, and the pieces replace them.
#include <algorithm>
#include <functional>
#include <map>

#include "core/style.h"
#include "core/transform.h"
#include "editor/editor.h"

namespace leinwand::editor {

namespace {

using core::Matrix;
using core::ObjectPtr;
using core::PathData;
using geometry::Pathfinder;
using geometry::Region;

// An object's filled area in document coordinates, and the object whose
// appearance stands for it (a group's frontmost member). Objects without an
// area of their own (preserved content, empty groups) give nothing.
struct Input {
  Region region;
  const core::Object* look = nullptr;
};

std::optional<Input> ToInput(const core::Object& object, const Matrix& to_document,
                             const geometry::PathOpsEngine& engine) {
  if (const auto* path = std::get_if<core::PathObject>(&object)) {
    if (path->path.anchors.size() < 2) return std::nullopt;
    return Input{{{core::Transformed(path->path, to_document)}}, &object};
  }
  if (const auto* compound = std::get_if<core::CompoundPathObject>(&object)) {
    Input input{{{}, compound->fill_rule}, &object};
    for (const PathData& sub : compound->subpaths) {
      input.region.subpaths.push_back(core::Transformed(sub, to_document));
    }
    return input;
  }
  if (const auto* shape = std::get_if<core::ShapeObject>(&object)) {
    return Input{
        {{core::Transformed(core::ShapePath(shape->shape), to_document * shape->transform)}},
        &object};
  }
  if (const auto* group = std::get_if<core::GroupObject>(&object)) {
    // A group counts as the union of its members. One holding text or kept
    // content is left alone: replacing it would lose them.
    std::function<bool(const core::Object&)> keeps = [&](const core::Object& o) {
      if (std::holds_alternative<core::TextObject>(o) ||
          std::holds_alternative<core::PreservedObject>(o)) {
        return true;
      }
      const auto* g = std::get_if<core::GroupObject>(&o);
      return g && std::any_of(g->children.begin(), g->children.end(),
                              [&](const ObjectPtr& c) { return keeps(*c); });
    };
    if (keeps(object)) return std::nullopt;
    std::optional<Input> merged;
    for (const ObjectPtr& child : group->children) {
      auto part = ToInput(*child, to_document * group->transform, engine);
      if (!part) continue;
      if (!merged) {
        merged = std::move(part);
        continue;
      }
      auto joined = engine.Apply(merged->region, part->region, geometry::BooleanOp::kUnion);
      if (!joined) return std::nullopt;
      merged->region = std::move(*joined);
      merged->look = part->look;  // The frontmost member.
    }
    return merged;
  }
  return std::nullopt;
}

// A path for one subpath, a compound path for several.
ObjectPtr ToObject(const Region& region, core::ObjectCommon common) {
  if (region.subpaths.size() == 1) {
    return std::make_shared<const core::Object>(
        core::PathObject{std::move(common), region.subpaths[0]});
  }
  return std::make_shared<const core::Object>(
      core::CompoundPathObject{std::move(common), region.subpaths, region.fill_rule});
}

core::Appearance WithoutStrokes(core::Appearance appearance) {
  std::erase_if(appearance, [](const core::AppearanceItem& item) {
    return std::holds_alternative<core::Stroke>(item);
  });
  return appearance;
}

// Outline pieces are stroked in their region's fill colour, without a fill.
core::Appearance OutlineLook(const core::Appearance& appearance) {
  core::Stroke stroke;
  if (const core::Fill* fill = core::FrontFill(appearance)) stroke.paint = fill->paint;
  return {stroke};
}

}  // namespace

Editor::PathfinderOutcome Editor::ApplyPathfinder(Pathfinder operation) {
  if (!path_ops_) return PathfinderOutcome::kFailed;
  const std::vector<core::Located> found =
      core::FindObjects(document(), core::WithoutNested(document(), selection()));
  std::vector<Region> regions;
  std::vector<const core::Object*> looks;
  core::IdSet used;
  for (const core::Located& located : found) {
    auto input = ToInput(*located.object, located.to_document, *path_ops_);
    if (!input) continue;
    regions.push_back(std::move(input->region));
    looks.push_back(input->look);
    used.insert(core::CommonOf(*located.object).id);
  }
  if (regions.size() < 2) return PathfinderOutcome::kNothingToDo;

  // Merge joins pieces whose fronts fill alike.
  std::vector<int> fill_keys;
  for (size_t i = 0; i < looks.size(); ++i) {
    const core::Fill* fill = core::FrontFill(core::CommonOf(*looks[i]).appearance);
    int key = int(i);
    for (size_t j = 0; j < i && fill; ++j) {
      const core::Fill* other = core::FrontFill(core::CommonOf(*looks[j]).appearance);
      if (other && other->paint == fill->paint) {
        key = fill_keys[j];
        break;
      }
    }
    fill_keys.push_back(key);
  }

  const auto pieces = geometry::RunPathfinder(*path_ops_, operation, regions, fill_keys);
  if (!pieces) return PathfinderOutcome::kFailed;

  // The result goes where the frontmost input was, in its parent's
  // coordinates.
  const core::Located* front = nullptr;
  for (const core::Located& located : found) {
    if (used.contains(core::CommonOf(*located.object).id)) front = &located;
  }
  const Matrix to_local = front->to_document.Inverted().value_or(Matrix{});

  std::vector<ObjectPtr> objects;
  for (const geometry::PathfinderPiece& piece : *pieces) {
    const core::ObjectCommon& source = core::CommonOf(*looks[size_t(piece.source)]);
    core::ObjectCommon common;
    common.opacity = source.opacity;
    common.blend_mode = source.blend_mode;
    common.appearance = source.appearance;
    if (geometry::DropsStrokes(operation)) common.appearance = WithoutStrokes(common.appearance);
    if (operation == Pathfinder::kOutline) {
      // Each edge is a path of its own.
      common.appearance = OutlineLook(source.appearance);
      for (const PathData& edge : piece.region.subpaths) {
        core::ObjectCommon edge_common = common;
        edge_common.id = ids_.Next();
        objects.push_back(std::make_shared<const core::Object>(
            core::PathObject{std::move(edge_common), core::Transformed(edge, to_local)}));
      }
      continue;
    }
    common.id = ids_.Next();
    Region local{{}, piece.region.fill_rule};
    for (const PathData& sub : piece.region.subpaths) {
      local.subpaths.push_back(core::Transformed(sub, to_local));
    }
    objects.push_back(ToObject(local, std::move(common)));
  }

  const std::string front_id = core::CommonOf(*front->object).id;
  core::IdSet removed = used;
  core::Document result = document();
  core::IdSet selection;
  const bool grouped = operation == Pathfinder::kDivide || operation == Pathfinder::kTrim ||
                       operation == Pathfinder::kMerge || operation == Pathfinder::kCrop ||
                       operation == Pathfinder::kOutline;
  ObjectPtr replacement;
  if (objects.size() == 1 && !grouped) {
    replacement = objects[0];
  } else if (!objects.empty()) {
    core::GroupObject group;
    group.common.id = ids_.Next();
    group.children = std::move(objects);
    replacement = std::make_shared<const core::Object>(std::move(group));
  }
  if (replacement) {
    selection.insert(core::CommonOf(*replacement).id);
    result = core::ReplaceObjects(result, {{front_id, replacement}});
    removed.erase(front_id);
  }
  result = core::RemoveObjects(result, removed);
  anchors_.clear();
  Commit("pathfinder", {std::move(result), std::move(selection)});
  return PathfinderOutcome::kDone;
}

void Editor::MakeCompoundPath() {
  const auto found = core::FindObjects(document(), core::WithoutNested(document(), selection()));
  core::CompoundPathObject compound;
  const core::Located* front = nullptr;
  const core::Object* back = nullptr;
  core::IdSet used;
  // Collected in document coordinates, then put into the front's parent's.
  std::vector<PathData> subpaths;
  for (const core::Located& located : found) {
    if (const auto* path = std::get_if<core::PathObject>(located.object.get())) {
      subpaths.push_back(core::Transformed(path->path, located.to_document));
    } else if (const auto* c = std::get_if<core::CompoundPathObject>(located.object.get())) {
      for (const PathData& sub : c->subpaths) {
        subpaths.push_back(core::Transformed(sub, located.to_document));
      }
    } else {
      continue;
    }
    if (!back) back = located.object.get();
    front = &located;
    used.insert(core::CommonOf(*located.object).id);
  }
  if (used.size() < 2) return;
  const Matrix to_local = front->to_document.Inverted().value_or(Matrix{});
  for (const PathData& sub : subpaths)
    compound.subpaths.push_back(core::Transformed(sub, to_local));
  const core::ObjectCommon& look = core::CommonOf(*back);
  compound.common.id = ids_.Next();
  compound.common.appearance = look.appearance;
  compound.common.opacity = look.opacity;
  compound.common.blend_mode = look.blend_mode;
  if (const auto* c = std::get_if<core::CompoundPathObject>(back))
    compound.fill_rule = c->fill_rule;
  const std::string front_id = core::CommonOf(*front->object).id;
  const std::string id = compound.common.id;
  core::Document result = core::ReplaceObjects(
      document(), {{front_id, std::make_shared<const core::Object>(std::move(compound))}});
  used.erase(front_id);
  result = core::RemoveObjects(result, used);
  anchors_.clear();
  Commit("make compound path", {std::move(result), {id}});
}

void Editor::ReleaseCompoundPath() {
  core::Document result = document();
  core::IdSet released;
  bool any = false;
  for (const core::Located& located : core::FindObjects(document(), selection())) {
    const auto* compound = std::get_if<core::CompoundPathObject>(located.object.get());
    if (!compound) {
      released.insert(core::CommonOf(*located.object).id);
      continue;
    }
    // The paths stand where the compound path stood, back to front in
    // subpath order; each keeps the compound path's appearance.
    std::vector<ObjectPtr> paths;
    for (const PathData& sub : compound->subpaths) {
      core::ObjectCommon common = compound->common;
      common.id = ids_.Next();
      common.name.clear();
      released.insert(common.id);
      paths.push_back(core::MakeObject(core::PathObject{std::move(common), sub}));
    }
    core::GroupObject holder;  // Placed, then ungrouped, to keep the stacking place.
    holder.common.id = ids_.Next();
    holder.children = std::move(paths);
    const std::string holder_id = holder.common.id;
    result = core::ReplaceObjects(
        result, {{compound->common.id, std::make_shared<const core::Object>(std::move(holder))}});
    core::IdSet unused;
    result = core::UngroupObjects(result, {holder_id}, &unused);
    any = true;
  }
  if (!any) return;
  anchors_.clear();
  Commit("release compound path", {std::move(result), std::move(released)});
}

}  // namespace leinwand::editor
