// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/edit.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <variant>

#include "core/transform.h"

namespace leinwand::core {

namespace {

// Layer lists hold objects and sublayers; group lists hold objects only.
const ObjectPtr* ObjectIn(const ObjectPtr& entry) { return &entry; }
const ObjectPtr* ObjectIn(const LayerChild& entry) { return std::get_if<ObjectPtr>(&entry); }

template <typename Entry>
bool IsListed(const Entry& entry, const IdSet& ids) {
  const ObjectPtr* object = ObjectIn(entry);
  return object && ids.contains(CommonOf(**object).id);
}

// Walks the tree bottom-up and lets `edit(list, list_to_document)` rewrite
// every child list (layer or group). Lists and their owners are copied only
// when something below them changed; everything else is returned as is.
template <typename Edit>
class Rewriter {
 public:
  explicit Rewriter(Edit& edit) : edit_(edit) {}

  Document Run(const Document& document) {
    Document result = document;
    for (auto& layer : result.layers) layer = Rewrite(layer);
    return result;
  }

 private:
  ObjectPtr Rewrite(const ObjectPtr& object, const Matrix& to_document) {
    const auto* group = std::get_if<GroupObject>(&*object);
    if (!group) return object;
    const Matrix inner = to_document * group->transform;
    std::vector<ObjectPtr> children = group->children;
    bool changed = false;
    for (auto& child : children) {
      ObjectPtr rewritten = Rewrite(child, inner);
      changed |= rewritten != child;
      child = std::move(rewritten);
    }
    changed |= edit_(children, inner);
    if (!changed) return object;
    GroupObject copy = *group;
    copy.children = std::move(children);
    return MakeObject(std::move(copy));
  }

  LayerPtr Rewrite(const LayerPtr& layer) {
    std::vector<LayerChild> children = layer->children;
    bool changed = false;
    for (auto& child : children) {
      if (const auto* object = std::get_if<ObjectPtr>(&child)) {
        ObjectPtr rewritten = Rewrite(*object, Matrix{});
        if (rewritten != *object) {
          child = std::move(rewritten);
          changed = true;
        }
      } else {
        const LayerPtr& sublayer = std::get<LayerPtr>(child);
        LayerPtr rewritten = Rewrite(sublayer);
        if (rewritten != sublayer) {
          child = std::move(rewritten);
          changed = true;
        }
      }
    }
    changed |= edit_(children, Matrix{});
    if (!changed) return layer;
    Layer copy = *layer;
    copy.children = std::move(children);
    return MakeLayer(std::move(copy));
  }

  Edit& edit_;
};

template <typename Edit>
Document Rewrite(const Document& document, Edit&& edit) {
  return Rewriter<std::remove_reference_t<Edit>>(edit).Run(document);
}

template <typename Visit>
void Walk(const ObjectPtr& object, const Matrix& to_document, bool inside_listed, Visit& visit) {
  const bool listed = visit(object, to_document, inside_listed);
  if (const auto* group = std::get_if<GroupObject>(&*object)) {
    const Matrix inner = to_document * group->transform;
    for (const auto& child : group->children) Walk(child, inner, inside_listed || listed, visit);
  }
}

template <typename Visit>
void Walk(const Layer& layer, Visit& visit) {
  for (const auto& child : layer.children) {
    if (const auto* object = std::get_if<ObjectPtr>(&child)) {
      Walk(*object, Matrix{}, false, visit);
    } else {
      Walk(*std::get<LayerPtr>(child), visit);
    }
  }
}

// Calls visit(object, to_document, inside_listed) for every object in
// painting order; visit returns whether the object counts as "listed" for
// its descendants.
template <typename Visit>
void Walk(const Document& document, Visit&& visit) {
  for (const auto& layer : document.layers) Walk(*layer, visit);
}

ObjectPtr WithFreshIds(const ObjectPtr& object, IdGenerator& ids) {
  return std::visit(
      [&](const auto& o) -> ObjectPtr {
        using T = std::decay_t<decltype(o)>;
        T copy = o;
        copy.common.id = ids.Next();
        if constexpr (std::is_same_v<T, GroupObject>) {
          for (auto& child : copy.children) child = WithFreshIds(child, ids);
        }
        if (copy.common.mask && copy.common.mask->art) {
          OpacityMask mask = *copy.common.mask;
          mask.art = WithFreshIds(mask.art, ids);
          copy.common.mask = std::make_shared<const OpacityMask>(std::move(mask));
        }
        return MakeObject(std::move(copy));
      },
      object->base());
}

}  // namespace

std::vector<Located> FindObjects(const Document& document, const IdSet& ids) {
  std::vector<Located> found;
  if (ids.empty()) return found;
  Walk(document, [&](const ObjectPtr& object, const Matrix& to_document, bool) {
    const bool listed = ids.contains(CommonOf(*object).id);
    if (listed) found.push_back({object, to_document});
    return listed;
  });
  return found;
}

IdSet WithoutNested(const Document& document, const IdSet& ids) {
  IdSet result;
  Walk(document, [&](const ObjectPtr& object, const Matrix&, bool inside_listed) {
    const bool listed = ids.contains(CommonOf(*object).id);
    if (listed && !inside_listed) result.insert(CommonOf(*object).id);
    return listed;
  });
  return result;
}

Document AddObject(const Document& document, ObjectPtr object, const std::string& layer_id) {
  Document result = document;
  for (auto it = result.layers.rbegin(); it != result.layers.rend(); ++it) {
    if (!(*it)->visible || (*it)->locked) continue;
    Layer layer = **it;
    layer.children.push_back(std::move(object));
    *it = MakeLayer(std::move(layer));
    return result;
  }
  Layer layer;
  layer.id = layer_id;
  layer.name = "Layer 1";
  layer.children.push_back(std::move(object));
  result.layers.push_back(MakeLayer(std::move(layer)));
  return result;
}

IdSet AllObjectIds(const Document& document) {
  IdSet ids;
  // Opacity masks' art too, so that new ids never clash with it.
  std::function<void(const Object&)> add = [&](const Object& object) {
    ids.insert(CommonOf(object).id);
    const auto& mask = CommonOf(object).mask;
    if (!mask || !mask->art) return;
    add(*mask->art);
    if (const auto* group = std::get_if<GroupObject>(mask->art.get())) {
      for (const auto& child : group->children) add(*child);
    }
  };
  VisitObjects(document, add);
  return ids;
}

Document ReplaceObjects(const Document& document,
                        const std::map<std::string, ObjectPtr>& replacements) {
  return Rewrite(document, [&](auto& list, const Matrix&) {
    bool changed = false;
    for (auto& entry : list) {
      const ObjectPtr* object = ObjectIn(entry);
      if (!object) continue;
      const auto it = replacements.find(CommonOf(**object).id);
      if (it != replacements.end()) {
        entry = it->second;
        changed = true;
      }
    }
    return changed;
  });
}

Document RemoveObjects(const Document& document, const IdSet& ids) {
  return Rewrite(document, [&](auto& list, const Matrix&) {
    return std::erase_if(list, [&](const auto& entry) { return IsListed(entry, ids); }) > 0;
  });
}

Document TransformObjects(const Document& document, const IdSet& ids, const Matrix& matrix) {
  const IdSet targets = WithoutNested(document, ids);
  return Rewrite(document, [&](auto& list, const Matrix& to_document) {
    // Express the document-space matrix in this list's coordinates.
    const auto inverse = to_document.Inverted();
    if (!inverse) return false;
    const Matrix local = *inverse * matrix * to_document;
    bool changed = false;
    for (auto& entry : list) {
      if (!IsListed(entry, targets)) continue;
      entry = Transformed(*ObjectIn(entry), local);
      changed = true;
    }
    return changed;
  });
}

Document ArrangeObjects(const Document& document, const IdSet& ids, Arrange arrange) {
  return Rewrite(document, [&](auto& list, const Matrix&) {
    auto listed = [&](const auto& entry) { return IsListed(entry, ids); };
    if (std::none_of(list.begin(), list.end(), listed)) return false;
    const auto before = list;
    const int n = static_cast<int>(list.size());
    switch (arrange) {
      case Arrange::kBringToFront:
        std::stable_partition(list.begin(), list.end(), [&](const auto& e) { return !listed(e); });
        break;
      case Arrange::kSendToBack:
        std::stable_partition(list.begin(), list.end(), listed);
        break;
      case Arrange::kBringForward:
        // From the front down, so a run of selected objects moves as a block.
        for (int i = n - 2; i >= 0; --i) {
          if (listed(list[i]) && !listed(list[i + 1])) std::swap(list[i], list[i + 1]);
        }
        break;
      case Arrange::kSendBackward:
        for (int i = 1; i < n; ++i) {
          if (listed(list[i]) && !listed(list[i - 1])) std::swap(list[i], list[i - 1]);
        }
        break;
    }
    return list != before;
  });
}

Document GroupObjects(const Document& document, const IdSet& ids, const std::string& group_id) {
  const std::vector<Located> members = FindObjects(document, WithoutNested(document, ids));
  if (members.empty()) return document;
  const std::string frontmost = CommonOf(*members.back().object).id;

  // Children in document coordinates; the group itself undoes the transform
  // of wherever it lands.
  GroupObject group;
  group.common.id = group_id;
  for (const auto& member : members) {
    group.children.push_back(Transformed(member.object, member.to_document));
  }
  IdSet listed;
  for (const auto& member : members) listed.insert(CommonOf(*member.object).id);

  return Rewrite(document, [&](auto& list, const Matrix& to_document) {
    bool changed = false;
    for (auto it = list.begin(); it != list.end();) {
      if (!IsListed(*it, listed)) {
        ++it;
        continue;
      }
      changed = true;
      if (CommonOf(**ObjectIn(*it)).id == frontmost) {
        GroupObject placed = group;
        placed.transform = to_document.Inverted().value_or(Matrix{});
        *it = MakeObject(std::move(placed));
        ++it;
      } else {
        it = list.erase(it);
      }
    }
    return changed;
  });
}

Document UngroupObjects(const Document& document, const IdSet& ids, IdSet* released) {
  return Rewrite(document, [&](auto& list, const Matrix&) {
    using Entry = typename std::decay_t<decltype(list)>::value_type;
    bool changed = false;
    std::vector<Entry> result;
    for (const auto& entry : list) {
      const ObjectPtr* object = ObjectIn(entry);
      const auto* group = object ? std::get_if<GroupObject>(&**object) : nullptr;
      if (!group || !ids.contains(group->common.id)) {
        result.push_back(entry);
        continue;
      }
      changed = true;
      for (const auto& child : group->children) {
        ObjectPtr released_child = Transformed(child, group->transform);
        if (group->common.opacity < 1.0) {
          std::visit(
              [&](const auto& c) {
                auto copy = c;
                copy.common.opacity *= group->common.opacity;
                released_child = MakeObject(std::move(copy));
              },
              released_child->base());
        }
        if (released) released->insert(CommonOf(*released_child).id);
        result.push_back(std::move(released_child));
      }
    }
    if (changed) list = std::move(result);
    return changed;
  });
}

Document DuplicateObjects(const Document& document, const IdSet& ids, IdGenerator& id_generator,
                          IdSet* copies) {
  const IdSet targets = WithoutNested(document, ids);
  return Rewrite(document, [&](auto& list, const Matrix&) {
    using Entry = typename std::decay_t<decltype(list)>::value_type;
    bool changed = false;
    std::vector<Entry> result;
    for (const auto& entry : list) {
      result.push_back(entry);
      if (!IsListed(entry, targets)) continue;
      ObjectPtr copy = WithFreshIds(*ObjectIn(entry), id_generator);
      if (copies) copies->insert(CommonOf(*copy).id);
      result.push_back(std::move(copy));
      changed = true;
    }
    if (changed) list = std::move(result);
    return changed;
  });
}

}  // namespace leinwand::core
