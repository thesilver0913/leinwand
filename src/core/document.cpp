// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/document.h"

#include <string>

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

std::vector<Swatch> DefaultSwatches() {
  struct Entry {
    const char* name;
    int rgb;
  };
  static constexpr Entry kPalette[] = {
      {"White", 0xffffff},   {"Black", 0x000000},   {"Red", 0xed1c24},      {"Orange", 0xf7931e},
      {"Yellow", 0xfcee21},  {"Green", 0x39b54a},   {"Cyan", 0x00aeef},     {"Blue", 0x2e3192},
      {"Violet", 0x662d91},  {"Magenta", 0xec008c}, {"Gray 75%", 0x404040}, {"Gray 50%", 0x808080},
      {"Gray 25%", 0xbfbfbf}};
  std::vector<Swatch> swatches;
  for (const auto& [name, rgb] : kPalette) {
    swatches.push_back(
        {"swatch-" + std::to_string(swatches.size() + 1),
         name,
         Swatch::Kind::kProcess,
         RgbColor{((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0},
         {}});
  }
  return swatches;
}

Document NewDocument(const std::string& layer_name) {
  Document document;
  // A4 in points.
  document.artboards = {{"artboard-1", "Artboard 1", Rect::FromXYWH(0, 0, 595.28, 841.89), {}}};
  document.swatches = DefaultSwatches();
  Layer layer;
  layer.id = "layer-1";
  layer.name = layer_name;
  document.layers = {MakeLayer(std::move(layer))};
  return document;
}

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
