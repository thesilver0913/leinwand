// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor/preflight.h"

#include <cmath>
#include <functional>
#include <map>
#include <variant>

#include "geometry/bezier.h"

namespace leinwand::editor {

namespace {

using core::Matrix;
using core::Rect;

bool HasFill(const core::Object& object) {
  if (const auto* group = std::get_if<core::GroupObject>(&object)) {
    for (const auto& child : group->children) {
      if (HasFill(*child)) return true;
    }
    return false;
  }
  if (std::holds_alternative<core::TextObject>(object)) return false;
  for (const auto& item : core::CommonOf(object).appearance) {
    if (std::holds_alternative<core::Fill>(item)) return true;
  }
  return false;
}

// How much a transform scales lengths, roughly.
double ScaleOf(const Matrix& m) { return std::sqrt(std::abs(m.Determinant())); }

}  // namespace

std::vector<PreflightIssue> Preflight(const core::Document& document,
                                      const PreflightSettings& settings) {
  std::map<PreflightCheck, std::vector<std::string>> found;
  auto add = [&](PreflightCheck check, const std::string& id) {
    if (!settings.enabled[static_cast<int>(check)]) return;
    auto& ids = found[check];
    if (ids.empty() || ids.back() != id) ids.push_back(id);
  };

  // Every object, with what its layers and groups make of it.
  std::function<void(const core::ObjectPtr&, const Matrix&, bool, bool)> visit =
      [&](const core::ObjectPtr& object, const Matrix& to_document, bool hidden, bool locked) {
        const core::ObjectCommon& common = core::CommonOf(*object);
        const bool is_hidden = hidden || !common.visible;
        const bool is_locked = locked || common.locked;
        if (!common.visible && !hidden) add(PreflightCheck::kHidden, common.id);
        if (common.locked && !locked) add(PreflightCheck::kLocked, common.id);
        if (const auto* group = std::get_if<core::GroupObject>(object.get())) {
          for (const auto& child : group->children) {
            visit(child, to_document * group->transform, is_hidden, is_locked);
          }
          return;
        }
        if (is_hidden) return;  // Not printed: the other checks do not apply.
        if (const auto* text = std::get_if<core::TextObject>(object.get())) {
          if (!text->story || text->story->text.empty()) {
            add(PreflightCheck::kEmptyText, common.id);
          } else {
            add(PreflightCheck::kLiveText, common.id);
          }
        }
        if (const auto* path = std::get_if<core::PathObject>(object.get())) {
          if (path->path.anchors.size() == 1) add(PreflightCheck::kStrayPoint, common.id);
        }
        if (const auto* compound = std::get_if<core::CompoundPathObject>(object.get())) {
          for (const auto& sub : compound->subpaths) {
            if (sub.anchors.size() == 1) add(PreflightCheck::kStrayPoint, common.id);
          }
        }
        for (const auto& item : common.appearance) {
          const auto* stroke = std::get_if<core::Stroke>(&item);
          if (!stroke || stroke->width <= 0) continue;
          if (stroke->width * ScaleOf(to_document) < settings.min_stroke - 1e-9) {
            add(PreflightCheck::kThinStroke, common.id);
          }
        }
      };

  // Top-level objects, for the bleed: painted artwork that reaches an
  // artboard's edge but not the bleed beyond it.
  std::vector<std::pair<core::ObjectPtr, bool>> top;  // With "in a hidden layer".
  // Layers: objects in hidden or locked layers count as hidden or locked;
  // those in non-printing layers are left out of the other checks.
  std::function<void(const core::Layer&, bool, bool, bool)> layer_visit =
      [&](const core::Layer& layer, bool hidden, bool locked, bool unprinted) {
        const bool is_hidden = hidden || !layer.visible;
        const bool is_locked = locked || layer.locked;
        const bool is_unprinted = unprinted || !layer.printable;
        for (const auto& child : layer.children) {
          if (const auto* object = std::get_if<core::ObjectPtr>(&child)) {
            const std::string& id = core::CommonOf(**object).id;
            if (is_hidden) add(PreflightCheck::kHidden, id);
            if (is_locked) add(PreflightCheck::kLocked, id);
            visit(*object, Matrix{}, is_hidden || is_unprinted, is_locked);
            top.push_back({*object, is_hidden || is_unprinted});
          } else {
            layer_visit(*std::get<core::LayerPtr>(child), is_hidden, is_locked, is_unprinted);
          }
        }
      };
  for (const auto& layer : document.layers) layer_visit(*layer, false, false, false);

  constexpr double kNear = 0.5;  // Points: "at the edge".
  for (const auto& [object, hidden] : top) {
    if (hidden || !core::CommonOf(*object).visible || !HasFill(*object)) continue;
    const Rect bounds = geometry::Bounds(*object);
    if (!bounds.IsValid()) continue;
    for (const core::Artboard& board : document.artboards) {
      const Rect& trim = board.bounds;
      if (!bounds.Intersects(trim)) continue;
      const double bleed = board.bleed > 0 ? board.bleed : settings.bleed;
      const bool short_of_bleed =
          (bounds.left <= trim.left + kNear && bounds.left > trim.left - bleed + kNear) ||
          (bounds.top <= trim.top + kNear && bounds.top > trim.top - bleed + kNear) ||
          (bounds.right >= trim.right - kNear && bounds.right < trim.right + bleed - kNear) ||
          (bounds.bottom >= trim.bottom - kNear && bounds.bottom < trim.bottom + bleed - kNear);
      if (short_of_bleed) {
        add(PreflightCheck::kShortOfBleed, core::CommonOf(*object).id);
        break;
      }
    }
  }

  std::vector<PreflightIssue> issues;
  for (int c = 0; c < kPreflightCheckCount; ++c) {
    const auto check = static_cast<PreflightCheck>(c);
    if (const auto it = found.find(check); it != found.end() && !it->second.empty()) {
      issues.push_back({check, it->second});
    }
  }
  return issues;
}

}  // namespace leinwand::editor
