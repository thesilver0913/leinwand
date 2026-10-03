// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QSet>
#include <QString>
#include <map>
#include <vector>

#include "core/document.h"
#include "core/edit.h"

class Session;

// The Layers panel's tree (spec 7.2) as a flat list of the expanded rows,
// frontmost first as in Illustrator. Layers start expanded and groups
// collapsed; expanding is view state, not part of the document.
class LayersModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by Session")
  Q_PROPERTY(QString activeLayer READ activeLayer NOTIFY activeLayerChanged)
  Q_PROPERTY(QString activeLayerName READ activeLayerName NOTIFY activeLayerChanged)
  // Layers at all depths, for naming new ones ("Layer 3").
  Q_PROPERTY(int layerCount READ layerCount NOTIFY activeLayerChanged)

 public:
  enum Role {
    kItemId = Qt::UserRole + 1,
    kName,  // Empty when unnamed; QML shows a name derived from the kind.
    kKind,  // "layer", "group", "clipGroup", "path", "compoundPath", "rectangle", ...
    kDepth,
    kVisible,  // The item's own flag.
    kLocked,
    kDimmed,          // Hidden by a layer or group around it.
    kSelected,        // The object is selected.
    kHoldsSelection,  // Something inside is selected.
    kExpandable,
    kExpanded,
    kParentId,
    kActive,  // The layer new artwork goes into.
  };

  explicit LayersModel(Session* session);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString activeLayer() const;
  QString activeLayerName() const;
  int layerCount() const;

  // Rebuilds the rows from the session's document and selection.
  void Refresh();

  Q_INVOKABLE void toggleExpanded(const QString& id);
  Q_INVOKABLE void setVisible(const QString& id, bool visible);
  Q_INVOKABLE void setLocked(const QString& id, bool locked);
  Q_INVOKABLE void rename(const QString& id, const QString& name);
  // An object row: selects it (Shift: adds or removes it). A layer row:
  // makes it the active layer.
  Q_INVOKABLE void activate(const QString& id, bool extend);
  // Moves `id` so that it lands just in front of (above) row `row`, or into
  // `row` when `into` is set (a layer or group). For drag and drop.
  Q_INVOKABLE void moveToRow(const QString& id, int row, bool into);
  Q_INVOKABLE void addLayer(const QString& name);
  Q_INVOKABLE void removeLayer(const QString& id);

 signals:
  void activeLayerChanged();

 private:
  struct Row {
    std::string id;
    std::string name;
    QString kind;
    int depth = 0;
    bool visible = true;
    bool locked = false;
    bool dimmed = false;
    bool selected = false;
    bool holds_selection = false;
    bool expandable = false;
    bool expanded = false;
    std::string parent;
  };
  void AddLayerRows(const leinwand::core::Layer& layer, int depth, const std::string& parent,
                    bool dimmed, const leinwand::core::IdSet& selection);
  bool AddObjectRows(const leinwand::core::ObjectPtr& object, int depth, const std::string& parent,
                     bool dimmed, const leinwand::core::IdSet& selection);
  bool IsExpanded(const std::string& id, bool is_layer) const;

  Session* session_;
  std::vector<Row> rows_;
  std::map<std::string, bool> expanded_;  // Overrides of the defaults.
};
