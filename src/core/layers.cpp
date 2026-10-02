// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/layers.h"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <variant>

#include "core/transform.h"

namespace leinwand::core {

namespace {

using Item = LayerChild;  // An object or a layer.

const std::string& IdOf(const Item& item) {
  if (const auto* object = std::get_if<ObjectPtr>(&item)) return CommonOf(**object).id;
  return std::get<LayerPtr>(item)->id;
}

// Rewrites child lists bottom-up: `edit(list, owner, to_document)` may
// change the list of a layer or group (`owner` is its id, "" for the
// top-level layers) and returns whether it did. Unchanged parts are shared.
using ListEdit = std::function<bool(std::vector<Item>&, const std::string&, const Matrix&)>;

ObjectPtr RewriteObject(const ObjectPtr& object, const Matrix& to_document, const ListEdit& edit);

LayerPtr RewriteLayer(const LayerPtr& layer, const ListEdit& edit) {
  std::vector<Item> children = layer->children;
  bool changed = false;
  for (auto& child : children) {
    Item rewritten = child;
    if (const auto* object = std::get_if<ObjectPtr>(&child)) {
      rewritten = RewriteObject(*object, Matrix{}, edit);
    } else {
      rewritten = RewriteLayer(std::get<LayerPtr>(child), edit);
    }
    if (rewritten != child) {
      child = std::move(rewritten);
      changed = true;
    }
  }
  changed |= edit(children, layer->id, Matrix{});
  if (!changed) return layer;
  Layer copy = *layer;
  copy.children = std::move(children);
  return MakeLayer(std::move(copy));
}

ObjectPtr RewriteObject(const ObjectPtr& object, const Matrix& to_document, const ListEdit& edit) {
  const auto* group = std::get_if<GroupObject>(&*object);
  if (!group) return object;
  const Matrix inner = to_document * group->transform;
  std::vector<Item> children(group->children.begin(), group->children.end());
  bool changed = false;
  for (auto& child : children) {
    const ObjectPtr& original = std::get<ObjectPtr>(child);
    ObjectPtr rewritten = RewriteObject(original, inner, edit);
    if (rewritten != original) {
      child = std::move(rewritten);
      changed = true;
    }
  }
  changed |= edit(children, group->common.id, inner);
  if (!changed) return object;
  GroupObject copy = *group;
  copy.children.clear();
  for (const auto& child : children) copy.children.push_back(std::get<ObjectPtr>(child));
  return MakeObject(std::move(copy));
}

Document RewriteLists(const Document& document, const ListEdit& edit) {
  std::vector<Item> layers(document.layers.begin(), document.layers.end());
  bool changed = false;
  for (auto& item : layers) {
    const LayerPtr& original = std::get<LayerPtr>(item);
    LayerPtr rewritten = RewriteLayer(original, edit);
    if (rewritten != original) {
      item = std::move(rewritten);
      changed = true;
    }
  }
  changed |= edit(layers, "", Matrix{});
  if (!changed) return document;
  Document result = document;
  result.layers.clear();
  for (const auto& item : layers) result.layers.push_back(std::get<LayerPtr>(item));
  return result;
}

// Where everything sits: each item with its list's owner and that list's
// transform to the document, and the transform of each layer's or group's
// own list.
struct Tree {
  struct Place {
    Item item;
    std::string parent;
    Matrix parent_to_document;
  };
  std::map<std::string, Place> items;
  std::map<std::string, Matrix> lists;  // Owner id ("" too) -> its children's transform.
  std::map<std::string, bool> is_group;
};

Tree Survey(const Document& document) {
  Tree tree;
  RewriteLists(document, [&](std::vector<Item>& list, const std::string& owner, const Matrix& m) {
    tree.lists[owner] = m;
    for (const auto& item : list) {
      tree.items[IdOf(item)] = {item, owner, m};
      if (const auto* object = std::get_if<ObjectPtr>(&item)) {
        tree.is_group[IdOf(item)] = std::holds_alternative<GroupObject>(**object);
      }
    }
    return false;
  });
  return tree;
}

// Replaces the item `id` wherever it is.
Document EditItem(const Document& document, const std::string& id,
                  const std::function<void(Layer&)>& edit_layer,
                  const std::function<void(ObjectCommon&)>& edit_object) {
  return RewriteLists(document, [&](std::vector<Item>& list, const std::string&, const Matrix&) {
    for (auto& item : list) {
      if (IdOf(item) != id) continue;
      if (const auto* layer = std::get_if<LayerPtr>(&item)) {
        Layer copy = **layer;
        edit_layer(copy);
        item = MakeLayer(std::move(copy));
      } else {
        item = std::visit(
            [&](const auto& o) -> ObjectPtr {
              auto copy = o;
              edit_object(copy.common);
              return MakeObject(std::move(copy));
            },
            std::get<ObjectPtr>(item)->base());
      }
      return true;
    }
    return false;
  });
}

}  // namespace

Document SetItemVisible(const Document& document, const std::string& id, bool visible) {
  return EditItem(
      document, id, [&](Layer& l) { l.visible = visible; },
      [&](ObjectCommon& c) { c.visible = visible; });
}

Document SetItemLocked(const Document& document, const std::string& id, bool locked) {
  return EditItem(
      document, id, [&](Layer& l) { l.locked = locked; },
      [&](ObjectCommon& c) { c.locked = locked; });
}

Document RenameItem(const Document& document, const std::string& id, const std::string& name) {
  return EditItem(
      document, id, [&](Layer& l) { l.name = name; }, [&](ObjectCommon& c) { c.name = name; });
}

bool LayerAcceptsArt(const Document& document, const std::string& id) {
  std::function<bool(const Layer&)> find = [&](const Layer& layer) {
    if (!layer.visible || layer.locked) return false;
    if (layer.id == id) return true;
    for (const auto& child : layer.children) {
      if (const auto* sublayer = std::get_if<LayerPtr>(&child); sublayer && find(**sublayer)) {
        return true;
      }
    }
    return false;
  };
  return std::any_of(document.layers.begin(), document.layers.end(),
                     [&](const LayerPtr& layer) { return find(*layer); });
}

std::optional<std::string> ParentOf(const Document& document, const std::string& id) {
  const Tree tree = Survey(document);
  const auto it = tree.items.find(id);
  if (it == tree.items.end()) return std::nullopt;
  return it->second.parent;
}

Document MoveItem(const Document& document, const std::string& id, const std::string& parent,
                  int index) {
  const Tree tree = Survey(document);
  const auto found = tree.items.find(id);
  if (found == tree.items.end() || !tree.lists.contains(parent)) return document;
  const bool is_layer = std::holds_alternative<LayerPtr>(found->second.item);
  const bool into_group = tree.is_group.contains(parent) && tree.is_group.at(parent);
  if (is_layer ? into_group : parent.empty()) return document;
  // Not into itself or its own contents.
  for (std::string up = parent; !up.empty(); up = tree.items.at(up).parent) {
    if (up == id) return document;
  }

  Item item = found->second.item;
  if (const auto* object = std::get_if<ObjectPtr>(&item)) {
    // Keep the object where it is on the page.
    const auto to_parent = tree.lists.at(parent).Inverted();
    if (!to_parent) return document;
    const Matrix change = *to_parent * found->second.parent_to_document;
    if (!(change == Matrix{})) item = Transformed(*object, change);
  }

  bool removed = false;
  Document result =
      RewriteLists(document, [&](std::vector<Item>& list, const std::string&, const Matrix&) {
        const auto it =
            std::find_if(list.begin(), list.end(), [&](const Item& i) { return IdOf(i) == id; });
        if (it == list.end()) return false;
        list.erase(it);
        removed = true;
        return true;
      });
  if (!removed) return document;
  return RewriteLists(result,
                      [&](std::vector<Item>& list, const std::string& owner, const Matrix&) {
                        if (owner != parent) return false;
                        const int at = std::clamp(index, 0, static_cast<int>(list.size()));
                        list.insert(list.begin() + at, item);
                        return true;
                      });
}

Document AddLayer(const Document& document, Layer layer, const std::string& above) {
  Document result = document;
  const auto it = std::find_if(result.layers.begin(), result.layers.end(),
                               [&](const LayerPtr& l) { return l->id == above; });
  result.layers.insert(it == result.layers.end() ? it : it + 1, MakeLayer(std::move(layer)));
  return result;
}

Document RemoveLayer(const Document& document, const std::string& id) {
  return RewriteLists(document, [&](std::vector<Item>& list, const std::string&, const Matrix&) {
    const auto it = std::find_if(list.begin(), list.end(), [&](const Item& i) {
      return std::holds_alternative<LayerPtr>(i) && IdOf(i) == id;
    });
    if (it == list.end()) return false;
    list.erase(it);
    return true;
  });
}

}  // namespace leinwand::core
