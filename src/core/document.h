// SPDX-License-Identifier: GPL-3.0-or-later
// The document (spec 3): artboards and the layer tree are kept separately;
// artboards are export frames and never parent objects.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "core/color.h"
#include "core/object.h"
#include "core/types.h"

namespace leinwand::core {

struct Layer;
using LayerPtr = std::shared_ptr<const Layer>;

// A layer holds objects and sublayers, mixed, back to front.
using LayerChild = std::variant<ObjectPtr, LayerPtr>;

struct Layer {
  std::string id;
  std::string name;
  bool visible = true;
  bool locked = false;
  bool printable = true;  // Included in print and export.
  std::vector<LayerChild> children;
  std::string unknown_fields;  // See ObjectCommon::unknown_fields.
};

inline LayerPtr MakeLayer(Layer layer) { return std::make_shared<const Layer>(std::move(layer)); }

struct Artboard {
  std::string id;
  std::string name;
  Rect bounds;
  std::string unknown_fields;
};

enum class ColorMode { kRgb, kCmyk };

struct DocumentSettings {
  ColorMode color_mode = ColorMode::kRgb;  // Phases 1-3 create RGB documents only.
  std::string icc_profile = "sRGB IEC61966-2.1";
  std::string unknown_fields;
};

// A document is a cheap value: copying it copies only the top-level lists,
// and all layers and objects are shared until replaced.
struct Document {
  DocumentSettings settings;
  std::vector<Artboard> artboards;
  std::vector<Swatch> swatches;
  std::vector<LayerPtr> layers;  // Back to front.
  // Top-level entries of a newer version (e.g. "symbols"), as JSON object
  // text, written back unchanged.
  std::string unknown_fields;

  const Swatch* FindSwatch(const std::string& id) const;
  // Searches layers, sublayers and groups. Returns null when absent.
  const Object* FindObject(const std::string& id) const;
};

// The basic palette a new document starts with, like Illustrator's.
std::vector<Swatch> DefaultSwatches();

// A new, empty document: one A4 portrait artboard, one layer named
// `layer_name`, and the default swatches.
Document NewDocument(const std::string& layer_name);

// Calls `visit` for every object, depth first in painting order (back to
// front), including objects inside groups. Hidden objects are included.
void VisitObjects(const Document& document, const std::function<void(const Object&)>& visit);

}  // namespace leinwand::core
