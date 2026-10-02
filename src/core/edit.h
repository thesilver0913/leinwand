// SPDX-License-Identifier: GPL-3.0-or-later
// Editing operations on documents. Each returns a new document that shares
// every untouched layer and object with the input, so unedited objects keep
// their identity (and their render caches).
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/document.h"
#include "core/id.h"
#include "core/types.h"

namespace leinwand::core {

using IdSet = std::set<std::string>;

// An object in the tree, with the transform from its coordinates (its
// parent's) to document coordinates.
struct Located {
  ObjectPtr object;
  Matrix to_document;
};

// The objects with the given ids, in painting order (back to front).
std::vector<Located> FindObjects(const Document& document, const IdSet& ids);

// Drops ids whose object sits inside another listed object: when a group is
// selected, its contents move with it and must not be edited twice.
IdSet WithoutNested(const Document& document, const IdSet& ids);

Document ReplaceObjects(const Document& document,
                        const std::map<std::string, ObjectPtr>& replacements);
Document RemoveObjects(const Document& document, const IdSet& ids);

// Applies `matrix`, given in document coordinates, to each object.
Document TransformObjects(const Document& document, const IdSet& ids, const Matrix& matrix);

// Stacking order changes within each object's own layer or group.
enum class Arrange { kBringToFront, kBringForward, kSendBackward, kSendToBack };
Document ArrangeObjects(const Document& document, const IdSet& ids, Arrange arrange);

// Moves the objects into a new group placed where the frontmost of them was.
// Objects taken out of transformed groups keep their position on the page.
Document GroupObjects(const Document& document, const IdSet& ids, const std::string& group_id);

// Replaces each listed group by its children, with the group's transform
// applied. The ids of the released children go to `released`. A group's own
// opacity is multiplied into its children, which matches only when they
// don't overlap.
Document UngroupObjects(const Document& document, const IdSet& ids, IdSet* released);

// Puts a copy of each object directly in front of it, with fresh ids for the
// copy and everything inside it. The copies' top-level ids go to `copies`.
Document DuplicateObjects(const Document& document, const IdSet& ids, IdGenerator& id_generator,
                          IdSet* copies);

// Every object id in the document, for IdGenerator::Reserve.
IdSet AllObjectIds(const Document& document);

}  // namespace leinwand::core
