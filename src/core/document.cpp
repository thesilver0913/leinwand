// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/document.h"

namespace leinwand::core {

namespace {

void VisitObject(const Object& object, const std::function<void(const Object&)>& visit) {
  visit(object);
  if (const auto* group = std::get_if<GroupObject>(&object)) {
    for (const ObjectPtr& child : group->children) VisitObject(*child, visit);
  }
}

void VisitLayer(const Layer& layer, const std::function<void(const Object&)>& visit) {
  for (const LayerChild& child : layer.children) {
    if (const auto* object = std::get_if<ObjectPtr>(&child)) {
      VisitObject(**object, visit);
    } else {
      VisitLayer(*std::get<LayerPtr>(child), visit);
    }
  }
}

}  // namespace

const Swatch* Document::FindSwatch(const std::string& id) const {
  for (const Swatch& swatch : swatches) {
    if (swatch.id == id) return &swatch;
  }
  return nullptr;
}

const Object* Document::FindObject(const std::string& id) const {
  const Object* found = nullptr;
  VisitObjects(*this, [&](const Object& object) {
    if (!found && CommonOf(object).id == id) found = &object;
  });
  return found;
}

void VisitObjects(const Document& document, const std::function<void(const Object&)>& visit) {
  for (const LayerPtr& layer : document.layers) VisitLayer(*layer, visit);
}

}  // namespace leinwand::core
