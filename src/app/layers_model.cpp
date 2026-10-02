// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers_model.h"

#include <algorithm>
#include <functional>
#include <variant>

#include "core/layers.h"
#include "session.h"

namespace {

using leinwand::core::GroupObject;
using leinwand::core::IdSet;
using leinwand::core::Layer;
using leinwand::core::LayerPtr;
using leinwand::core::ObjectPtr;

QString KindOf(const leinwand::core::Object& object) {
  using namespace leinwand::core;
  if (const auto* group = std::get_if<GroupObject>(&object)) {
    return group->clipped ? QStringLiteral("clipGroup") : QStringLiteral("group");
  }
  if (std::holds_alternative<CompoundPathObject>(object)) return QStringLiteral("compoundPath");
  if (const auto* shape = std::get_if<ShapeObject>(&object)) {
    return std::visit(
        [](const auto& s) {
          using T = std::decay_t<decltype(s)>;
          if constexpr (std::is_same_v<T, RectangleShape>) return QStringLiteral("rectangle");
          if constexpr (std::is_same_v<T, EllipseShape>) return QStringLiteral("ellipse");
          if constexpr (std::is_same_v<T, PolygonShape>) return QStringLiteral("polygon");
          if constexpr (std::is_same_v<T, StarShape>) return QStringLiteral("star");
          return QStringLiteral("line");
        },
        shape->shape);
  }
  return QStringLiteral("path");
}

// The ids directly inside `parent` (a layer or group; "" for the top-level
// layers), back to front.
std::vector<std::string> ChildIds(const leinwand::core::Document& document,
                                  const std::string& parent) {
  std::vector<std::string> ids;
  if (parent.empty()) {
    for (const auto& layer : document.layers) ids.push_back(layer->id);
    return ids;
  }
  std::function<bool(const Layer&)> in_layer;
  std::function<bool(const ObjectPtr&)> in_object = [&](const ObjectPtr& object) {
    const auto* group = std::get_if<GroupObject>(object.get());
    if (!group) return false;
    if (group->common.id == parent) {
      for (const auto& child : group->children) {
        ids.push_back(leinwand::core::CommonOf(*child).id);
      }
      return true;
    }
    return std::any_of(group->children.begin(), group->children.end(), in_object);
  };
  in_layer = [&](const Layer& layer) {
    if (layer.id == parent) {
      for (const auto& child : layer.children) {
        if (const auto* object = std::get_if<ObjectPtr>(&child)) {
          ids.push_back(leinwand::core::CommonOf(**object).id);
        } else {
          ids.push_back(std::get<LayerPtr>(child)->id);
        }
      }
      return true;
    }
    for (const auto& child : layer.children) {
      if (const auto* object = std::get_if<ObjectPtr>(&child)) {
        if (in_object(*object)) return true;
      } else if (in_layer(*std::get<LayerPtr>(child))) {
        return true;
      }
    }
    return false;
  };
  for (const auto& layer : document.layers) {
    if (in_layer(*layer)) break;
  }
  return ids;
}

}  // namespace

LayersModel::LayersModel(Session* session) : session_(session) {}

int LayersModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant LayersModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) return {};
  const Row& row = rows_[index.row()];
  switch (role) {
    case kItemId:
      return QString::fromStdString(row.id);
    case kName:
      return QString::fromStdString(row.name);
    case kKind:
      return row.kind;
    case kDepth:
      return row.depth;
    case kVisible:
      return row.visible;
    case kLocked:
      return row.locked;
    case kDimmed:
      return row.dimmed;
    case kSelected:
      return row.selected;
    case kHoldsSelection:
      return row.holds_selection;
    case kExpandable:
      return row.expandable;
    case kExpanded:
      return row.expanded;
    case kParentId:
      return QString::fromStdString(row.parent);
    case kActive:
      return row.kind == QLatin1String("layer") && QString::fromStdString(row.id) == activeLayer();
    default:
      return {};
  }
}

QHash<int, QByteArray> LayersModel::roleNames() const {
  return {
      {kItemId, "itemId"},         {kName, "name"},           {kKind, "kind"},
      {kDepth, "depth"},           {kVisible, "itemVisible"}, {kLocked, "itemLocked"},
      {kDimmed, "dimmed"},         {kSelected, "selected"},   {kHoldsSelection, "holdsSelection"},
      {kExpandable, "expandable"}, {kExpanded, "expanded"},   {kParentId, "parentId"},
      {kActive, "active"}};
}

QString LayersModel::activeLayer() const {
  const auto& editor = session_->editor();
  if (!editor.active_layer().empty() &&
      leinwand::core::LayerAcceptsArt(editor.document(), editor.active_layer())) {
    return QString::fromStdString(editor.active_layer());
  }
  // Where new artwork goes without an active layer: the frontmost layer
  // that takes it.
  const auto& layers = editor.document().layers;
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    if ((*it)->visible && !(*it)->locked) return QString::fromStdString((*it)->id);
  }
  return {};
}

QString LayersModel::activeLayerName() const {
  const std::string id = activeLayer().toStdString();
  std::function<const Layer*(const Layer&)> find = [&](const Layer& layer) -> const Layer* {
    if (layer.id == id) return &layer;
    for (const auto& child : layer.children) {
      if (const auto* sublayer = std::get_if<LayerPtr>(&child)) {
        if (const Layer* found = find(**sublayer)) return found;
      }
    }
    return nullptr;
  };
  for (const auto& layer : session_->editor().document().layers) {
    if (const Layer* found = find(*layer)) return QString::fromStdString(found->name);
  }
  return {};
}

int LayersModel::layerCount() const {
  std::function<int(const Layer&)> count = [&](const Layer& layer) {
    int n = 1;
    for (const auto& child : layer.children) {
      if (const auto* sublayer = std::get_if<LayerPtr>(&child)) n += count(**sublayer);
    }
    return n;
  };
  int n = 0;
  for (const auto& layer : session_->editor().document().layers) n += count(*layer);
  return n;
}

bool LayersModel::IsExpanded(const std::string& id, bool is_layer) const {
  const auto it = expanded_.find(id);
  return it != expanded_.end() ? it->second : is_layer;
}

void LayersModel::Refresh() {
  beginResetModel();
  rows_.clear();
  const auto& document = session_->editor().document();
  const IdSet& selection = session_->editor().selection();
  for (auto it = document.layers.rbegin(); it != document.layers.rend(); ++it) {
    AddLayerRows(**it, 0, "", false, selection);
  }
  endResetModel();
  emit activeLayerChanged();
}

void LayersModel::AddLayerRows(const Layer& layer, int depth, const std::string& parent,
                               bool dimmed, const IdSet& selection) {
  const size_t at = rows_.size();
  const bool expanded = IsExpanded(layer.id, true);
  rows_.push_back({layer.id, layer.name, QStringLiteral("layer"), depth, layer.visible,
                   layer.locked, dimmed, false, false, !layer.children.empty(), expanded, parent});
  bool holds = false;
  const bool inner_dimmed = dimmed || !layer.visible;
  for (auto it = layer.children.rbegin(); it != layer.children.rend(); ++it) {
    if (const auto* object = std::get_if<ObjectPtr>(&*it)) {
      if (expanded) {
        holds |= AddObjectRows(*object, depth + 1, layer.id, inner_dimmed, selection);
      } else {
        // Collapsed: still note a selection inside.
        std::function<bool(const ObjectPtr&)> any = [&](const ObjectPtr& o) {
          if (selection.contains(leinwand::core::CommonOf(*o).id)) return true;
          const auto* group = std::get_if<GroupObject>(o.get());
          return group && std::any_of(group->children.begin(), group->children.end(), any);
        };
        holds |= any(*object);
      }
    } else if (expanded) {
      const size_t sub = rows_.size();
      AddLayerRows(*std::get<LayerPtr>(*it), depth + 1, layer.id, inner_dimmed, selection);
      holds |= rows_[sub].holds_selection;
    }
  }
  rows_[at].holds_selection = holds;
}

bool LayersModel::AddObjectRows(const ObjectPtr& object, int depth, const std::string& parent,
                                bool dimmed, const IdSet& selection) {
  const auto& common = leinwand::core::CommonOf(*object);
  const auto* group = std::get_if<GroupObject>(object.get());
  const bool expanded = group && IsExpanded(common.id, false);
  const bool selected = selection.contains(common.id);
  const size_t at = rows_.size();
  rows_.push_back({common.id, common.name, KindOf(*object), depth, common.visible, common.locked,
                   dimmed, selected, false, group && !group->children.empty(), expanded, parent});
  bool holds = selected;
  if (group) {
    for (auto it = group->children.rbegin(); it != group->children.rend(); ++it) {
      if (expanded) {
        holds |= AddObjectRows(*it, depth + 1, common.id, dimmed || !common.visible, selection);
      } else {
        holds |= selection.contains(leinwand::core::CommonOf(**it).id);
      }
    }
  }
  rows_[at].holds_selection = holds;
  return holds;
}

void LayersModel::toggleExpanded(const QString& id) {
  const std::string key = id.toStdString();
  const auto row =
      std::find_if(rows_.begin(), rows_.end(), [&](const Row& r) { return r.id == key; });
  if (row == rows_.end()) return;
  expanded_[key] = !row->expanded;
  Refresh();
}

void LayersModel::setVisible(const QString& id, bool visible) {
  session_->editor().SetItemVisible(id.toStdString(), visible);
  session_->Changed();
}

void LayersModel::setLocked(const QString& id, bool locked) {
  session_->editor().SetItemLocked(id.toStdString(), locked);
  session_->Changed();
}

void LayersModel::rename(const QString& id, const QString& name) {
  session_->editor().RenameItem(id.toStdString(), name.toStdString());
  session_->Changed();
}

void LayersModel::activate(const QString& id, bool extend) {
  const std::string key = id.toStdString();
  const auto row =
      std::find_if(rows_.begin(), rows_.end(), [&](const Row& r) { return r.id == key; });
  if (row == rows_.end()) return;
  auto& editor = session_->editor();
  if (row->kind == QLatin1String("layer")) {
    editor.SetActiveLayer(key);
    emit activeLayerChanged();
    emit dataChanged(index(0), index(rowCount() - 1), {kActive});
    return;
  }
  IdSet selection = extend ? editor.selection() : IdSet{};
  if (extend && selection.contains(key)) {
    selection.erase(key);
  } else {
    selection.insert(key);
  }
  editor.Select(selection);
  session_->Changed();
}

void LayersModel::moveToRow(const QString& id, int row, bool into) {
  const std::string key = id.toStdString();
  const auto& document = session_->editor().document();
  std::string parent;
  int index = 0;  // Back to front, after the item is taken out.
  if (row < 0 || row >= static_cast<int>(rows_.size())) {
    // Below everything: the back of the bottom layer (or of the list).
    const bool is_layer = std::any_of(document.layers.begin(), document.layers.end(),
                                      [&](const LayerPtr& l) { return l->id == key; });
    if (!is_layer && !document.layers.empty()) parent = document.layers.front()->id;
  } else if (into) {
    parent = rows_[row].id;
    auto siblings = ChildIds(document, parent);
    std::erase(siblings, key);
    index = static_cast<int>(siblings.size());  // Frontmost.
  } else {
    const Row& target = rows_[row];
    if (target.id == key) return;
    parent = target.parent;
    auto siblings = ChildIds(document, parent);
    std::erase(siblings, key);
    const auto it = std::find(siblings.begin(), siblings.end(), target.id);
    if (it == siblings.end()) return;
    index = static_cast<int>(it - siblings.begin()) + 1;  // Just in front of it.
  }
  session_->editor().MoveItem(key, parent, index);
  session_->Changed();
}

void LayersModel::addLayer(const QString& name) {
  auto& editor = session_->editor();
  const std::string id = editor.AddLayer(activeLayer().toStdString(), name.toStdString());
  editor.SetActiveLayer(id);
  session_->Changed();
}

void LayersModel::removeLayer(const QString& id) {
  session_->editor().RemoveLayer(id.toStdString());
  session_->Changed();
}
