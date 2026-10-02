// SPDX-License-Identifier: GPL-3.0-or-later
// Layer panel operations (spec 7.2): the tree of layers, sublayers, groups
// and objects. Items are named by id; layer and object ids share one space.
#pragma once

#include <optional>
#include <string>

#include "core/document.h"

namespace leinwand::core {

// Show, lock or rename a layer or an object. Unknown ids leave the document
// as it is.
Document SetItemVisible(const Document& document, const std::string& id, bool visible);
Document SetItemLocked(const Document& document, const std::string& id, bool locked);
Document RenameItem(const Document& document, const std::string& id, const std::string& name);

// Moves a layer or object to position `index` (back to front) among the
// children of `parent`: a layer or group id, or "" for the top-level layer
// list. Layers go into layers only; objects into layers or groups, keeping
// their place on the page. `index` counts the children before the move
// minus the item itself, and is clamped. Invalid moves (into itself, a
// layer into a group, an object to the top level) change nothing.
Document MoveItem(const Document& document, const std::string& id, const std::string& parent,
                  int index);

// A new empty layer just in front of the top-level layer `above` (or in
// front of all layers when `above` is not a top-level layer).
Document AddLayer(const Document& document, Layer layer, const std::string& above);

// Removes a layer with everything in it.
Document RemoveLayer(const Document& document, const std::string& id);

// Whether new artwork can go into layer `id`: it exists, and neither it nor
// a layer around it is hidden or locked.
bool LayerAcceptsArt(const Document& document, const std::string& id);

// The layer or group that directly holds `id` ("" for a top-level layer),
// or nullopt when `id` is not in the document.
std::optional<std::string> ParentOf(const Document& document, const std::string& id);

}  // namespace leinwand::core
