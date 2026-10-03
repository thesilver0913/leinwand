// SPDX-License-Identifier: GPL-3.0-or-later
// Fill, stroke and opacity; the eyedropper; layer panel commands (spec 7.2).
#include <functional>
#include <limits>
#include <variant>

#include "core/gradient.h"
#include "core/layers.h"
#include "core/style.h"
#include "editor/editor.h"
#include "geometry/bezier.h"
#include "geometry/hit_test.h"

namespace leinwand::editor {

namespace {

std::optional<core::Color> FillPaint(const core::Appearance& a) {
  const core::Fill* fill = core::FrontFill(a);
  return fill ? std::optional{fill->paint} : std::nullopt;
}

std::optional<core::Color> StrokePaint(const core::Appearance& a) {
  const core::Stroke* stroke = core::FrontStroke(a);
  return stroke ? std::optional{stroke->paint} : std::nullopt;
}

// The appearances of the leaf objects (groups' contents) among `ids`.
std::vector<const core::Appearance*> LeafAppearances(const core::Document& document,
                                                     const core::IdSet& ids) {
  std::vector<const core::Appearance*> result;
  std::function<void(const core::Object&)> visit = [&](const core::Object& object) {
    if (const auto* group = std::get_if<core::GroupObject>(&object)) {
      for (const auto& child : group->children) visit(*child);
    } else {
      result.push_back(&core::CommonOf(object).appearance);
    }
  };
  for (const auto& found : core::FindObjects(document, core::WithoutNested(document, ids))) {
    visit(*found.object);
  }
  return result;
}

// The basic appearance new objects take from `a`: its front fill and stroke.
core::Appearance Basic(const core::Appearance& a) {
  core::Appearance basic;
  if (const core::Stroke* stroke = core::FrontStroke(a)) basic.push_back(*stroke);
  if (const core::Fill* fill = core::FrontFill(a)) basic.push_back(*fill);
  return basic;
}

core::Appearance DefaultStyle() {
  core::Stroke stroke{core::RgbColor{0, 0, 0}};
  stroke.width = 1;
  return {stroke, core::Fill{core::RgbColor{1, 1, 1}}};
}

// Drops selected ids that are gone, hidden or locked (themselves or through
// a layer or group around them).
core::IdSet StillSelectable(const core::Document& document, const core::IdSet& selection) {
  core::IdSet result;
  std::function<void(const core::ObjectPtr&)> visit_object = [&](const core::ObjectPtr& object) {
    const auto& common = core::CommonOf(*object);
    if (!common.visible || common.locked) return;
    if (selection.contains(common.id)) result.insert(common.id);
    if (const auto* group = std::get_if<core::GroupObject>(object.get())) {
      for (const auto& child : group->children) visit_object(child);
    }
  };
  std::function<void(const core::Layer&)> visit_layer = [&](const core::Layer& layer) {
    if (!layer.visible || layer.locked) return;
    for (const auto& child : layer.children) {
      if (const auto* object = std::get_if<core::ObjectPtr>(&child)) {
        visit_object(*object);
      } else {
        visit_layer(*std::get<core::LayerPtr>(child));
      }
    }
  };
  for (const auto& layer : document.layers) visit_layer(*layer);
  return result;
}

}  // namespace

StyleState Editor::Style() const {
  StyleState state;
  std::vector<const core::Appearance*> stacks = LeafAppearances(document(), selection());
  if (stacks.empty()) stacks = {&new_style_};
  state.fill = FillPaint(*stacks[0]);
  // The Gradient panel shows the first gradient among the selected objects.
  for (const auto* a : stacks) {
    for (const auto& item : *a) {
      if (fill_active_) {
        if (const auto* f = std::get_if<core::Fill>(&item)) {
          state.gradient = f->gradient;
          break;
        }
      } else if (const auto* st = std::get_if<core::Stroke>(&item)) {
        state.gradient = st->gradient;
        break;
      }
    }
    if (state.gradient) break;
  }
  state.stroke = StrokePaint(*stacks[0]);
  for (const auto* a : stacks) {
    if (!state.stroke_style) {
      if (const core::Stroke* stroke = core::FrontStroke(*a)) state.stroke_style = *stroke;
    }
    state.fill_mixed |= FillPaint(*a) != state.fill;
    state.stroke_mixed |= StrokePaint(*a) != state.stroke;
  }
  const auto found = core::FindObjects(document(), core::WithoutNested(document(), selection()));
  if (!found.empty()) state.opacity = core::CommonOf(*found[0].object).opacity;
  for (const auto& f : found)
    state.opacity_mixed |= core::CommonOf(*f.object).opacity != state.opacity;
  return state;
}

void Editor::AdoptSelectionStyle() {
  const auto stacks = LeafAppearances(document(), selection());
  if (!stacks.empty()) new_style_ = Basic(*stacks[0]);
}

void Editor::ApplyStyle(const std::string& action,
                        const std::function<void(core::Appearance&)>& edit) {
  edit(new_style_);
  if (selection().empty()) return;
  core::Document result = core::EditAppearance(document(), selection(), edit);
  if (result.layers == document().layers) return;
  Commit(action, {std::move(result), selection()});
}

void Editor::SetFill(const std::optional<core::Color>& paint) {
  // With a gradient stop selected, the color goes to the stop (spec 7.2).
  if (paint && fill_active_ && gradient_stop() >= 0 && Style().gradient) {
    const int stop = gradient_stop();
    EditGradient("gradient stop", [&](core::Gradient& g) {
      if (stop < static_cast<int>(g.stops.size())) g.stops[size_t(stop)].color = *paint;
    });
    return;
  }
  ApplyStyle("fill", [&](core::Appearance& a) { core::SetFillPaint(a, paint); });
}

void Editor::SetStroke(const std::optional<core::Color>& paint) {
  if (paint && !fill_active_ && gradient_stop() >= 0 && Style().gradient) {
    const int stop = gradient_stop();
    EditGradient("gradient stop", [&](core::Gradient& g) {
      if (stop < static_cast<int>(g.stops.size())) g.stops[size_t(stop)].color = *paint;
    });
    return;
  }
  ApplyStyle("stroke", [&](core::Appearance& a) { core::SetStrokePaint(a, paint); });
}

void Editor::SwapFillAndStroke() {
  ApplyStyle("fill and stroke", [](core::Appearance& a) { core::SwapFillAndStroke(a); });
}

void Editor::DefaultFillAndStroke() {
  ApplyStyle("fill and stroke", [](core::Appearance& a) { a = DefaultStyle(); });
}

void Editor::EditStroke(const std::function<void(core::Stroke&)>& edit) {
  ApplyStyle("stroke", [&](core::Appearance& a) {
    for (auto& item : a) {
      if (auto* stroke = std::get_if<core::Stroke>(&item)) {
        edit(*stroke);
        return;
      }
    }
  });
}

void Editor::SetOpacity(double opacity) {
  if (selection().empty()) return;
  core::Document result = core::SetOpacity(document(), selection(), opacity);
  if (result.layers == document().layers) return;
  Commit("opacity", {std::move(result), selection()});
}

void Editor::EyedropperDown(core::Point p, double pick) {
  const auto hit = geometry::HitTest(document(), p, pick);
  if (!hit) return;
  const auto found = core::FindObjects(document(), {hit->leaf_id});
  if (found.size() != 1) return;
  const core::Appearance source = core::CommonOf(*found[0].object).appearance;
  new_style_ = Basic(source);
  if (selection().empty()) return;
  core::Document result =
      core::EditAppearance(document(), selection(), [&](core::Appearance& a) { a = source; });
  if (result.layers == document().layers) return;
  Commit("eyedropper", {std::move(result), selection()});
}

std::string Editor::AddSwatch(core::Swatch swatch) {
  swatch.id = ids_.Next();
  core::Document result = document();
  result.swatches.push_back(swatch);
  Commit("new swatch", {std::move(result), selection()});
  return swatch.id;
}

void Editor::RemoveSwatch(const std::string& id) {
  core::Document result = core::RemoveSwatch(document(), id);
  if (result.swatches.size() == document().swatches.size()) return;
  Commit("delete swatch", {std::move(result), selection()});
}

void Editor::Select(const core::IdSet& ids) {
  anchors_.clear();
  SetSelection(StillSelectable(document(), ids));
}

core::Document Editor::WithNewObject(const core::Document& document, core::ObjectPtr object) const {
  const std::string id = core::CommonOf(*object).id;
  // Gradients from the new-object style are fitted to the new object, at
  // their angle (as Illustrator does).
  const auto& appearance = core::CommonOf(*object).appearance;
  if (std::any_of(appearance.begin(), appearance.end(), [](const core::AppearanceItem& item) {
        const auto* f = std::get_if<core::Fill>(&item);
        const auto* s = std::get_if<core::Stroke>(&item);
        return (f && f->gradient) || (s && s->gradient);
      })) {
    core::Object copy = *object;
    const core::Rect bounds = geometry::Bounds(copy);
    auto fit = [&](core::Gradient& g) {
      core::Gradient fitted = core::DefaultGradient(g.type, bounds, core::Color{}, core::Color{});
      fitted = core::WithAngle(fitted, core::GradientAngle(g));
      g.start = fitted.start;
      g.end = fitted.end;
      g.focal.reset();
    };
    std::visit(
        [&](auto& o) {
          for (auto& item : o.common.appearance) {
            if (auto* f = std::get_if<core::Fill>(&item); f && f->gradient) fit(*f->gradient);
            if (auto* st = std::get_if<core::Stroke>(&item); st && st->gradient) fit(*st->gradient);
          }
        },
        copy);
    object = std::make_shared<const core::Object>(std::move(copy));
  }
  core::Document result = core::AddObject(document, std::move(object), "layer-" + id);
  if (!active_layer_.empty() && core::LayerAcceptsArt(result, active_layer_)) {
    result = core::MoveItem(result, id, active_layer_, std::numeric_limits<int>::max());
  }
  return result;
}

void Editor::SetItemVisible(const std::string& id, bool visible) {
  core::Document result = core::SetItemVisible(document(), id, visible);
  if (result.layers == document().layers) return;
  Commit(visible ? "show" : "hide", {result, StillSelectable(result, selection())});
}

void Editor::SetItemLocked(const std::string& id, bool locked) {
  core::Document result = core::SetItemLocked(document(), id, locked);
  if (result.layers == document().layers) return;
  Commit(locked ? "lock" : "unlock", {result, StillSelectable(result, selection())});
}

void Editor::RenameItem(const std::string& id, const std::string& name) {
  core::Document result = core::RenameItem(document(), id, name);
  if (result.layers == document().layers) return;
  Commit("rename", {std::move(result), selection()});
}

void Editor::MoveItem(const std::string& id, const std::string& parent, int index) {
  core::Document result = core::MoveItem(document(), id, parent, index);
  if (result.layers == document().layers) return;
  Commit("arrange", {result, StillSelectable(result, selection())});
}

std::string Editor::AddLayer(const std::string& above, const std::string& name) {
  core::Layer layer;
  layer.id = ids_.Next();
  layer.name = name;
  const std::string id = layer.id;
  Commit("new layer", {core::AddLayer(document(), std::move(layer), above), selection()});
  return id;
}

void Editor::RemoveLayer(const std::string& id) {
  // The document keeps at least one layer, as in Illustrator.
  if (document().layers.size() == 1 && document().layers[0]->id == id) return;
  core::Document result = core::RemoveLayer(document(), id);
  if (result.layers == document().layers) return;
  Commit("delete layer", {result, StillSelectable(result, selection())});
}

}  // namespace leinwand::editor
