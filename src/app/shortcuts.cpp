// SPDX-License-Identifier: GPL-3.0-or-later
#include "shortcuts.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QQmlEngine>
#include <QSaveFile>

#include "preferences.h"

namespace {

Shortcuts* g_instance = nullptr;

struct Command {
  const char* id;
  const char* group;
  QStringList defaults;
};

// Illustrator's defaults (spec 4.2), Windows notation.
const std::vector<Command>& Commands() {
  static const std::vector<Command> commands = {
      {"fileNew", "file", {"Ctrl+N"}},
      {"fileOpen", "file", {"Ctrl+O"}},
      {"fileSave", "file", {"Ctrl+S"}},
      {"fileSaveAs", "file", {"Ctrl+Shift+S"}},
      {"fileExportSvg", "file", {}},
      {"fileExportPng", "file", {"Ctrl+Alt+E"}},
      {"fileClose", "file", {"Ctrl+W"}},
      {"fileQuit", "file", {"Ctrl+Q"}},
      {"editUndo", "edit", {"Ctrl+Z"}},
      {"editRedo", "edit", {"Ctrl+Shift+Z"}},
      {"editClear", "edit", {}},
      {"editPreferences", "edit", {"Ctrl+K"}},
      {"editShortcuts", "edit", {"Ctrl+Alt+Shift+K"}},
      {"objectBringToFront", "object", {"Ctrl+Shift+]"}},
      {"objectBringForward", "object", {"Ctrl+]"}},
      {"objectSendBackward", "object", {"Ctrl+["}},
      {"objectSendToBack", "object", {"Ctrl+Shift+["}},
      {"objectGroup", "object", {"Ctrl+G"}},
      {"objectUngroup", "object", {"Ctrl+Shift+G"}},
      {"objectJoin", "object", {"Ctrl+J"}},
      {"selectAll", "select", {"Ctrl+A"}},
      {"selectDeselect", "select", {"Ctrl+Shift+A"}},
      {"viewOutline", "view", {"Ctrl+Y"}},
      {"viewZoomIn", "view", {"Ctrl+="}},
      {"viewZoomOut", "view", {"Ctrl+-"}},
      {"viewFitArtboard", "view", {"Ctrl+0"}},
      {"viewActualSize", "view", {"Ctrl+1"}},
      {"viewSmartGuides", "view", {"Ctrl+U"}},
      {"toolSelection", "tools", {"V"}},
      {"toolDirectSelection", "tools", {"A"}},
      {"toolPen", "tools", {"P"}},
      // "+" needs Shift on most layouts; "=" is the same key unshifted on US ones.
      {"toolAddAnchor", "tools", {"+", "Shift++", "="}},
      {"toolDeleteAnchor", "tools", {"-"}},
      {"toolAnchorPoint", "tools", {"Shift+C"}},
      {"toolLine", "tools", {"\\"}},
      {"toolRectangle", "tools", {"M"}},
      {"toolEllipse", "tools", {"L"}},
      {"toolPolygon", "tools", {}},
      {"toolStar", "tools", {}},
      {"toolEyedropper", "tools", {"I"}},
      {"toolHand", "tools", {"H"}},
      {"toolZoom", "tools", {"Z"}},
      {"paintToggle", "paint", {"X"}},
      {"paintSwap", "paint", {"Shift+X"}},
      {"paintDefault", "paint", {"D"}},
      {"paintNone", "paint", {"/"}},
  };
  return commands;
}

QString FilePath() { return Preferences::Folder() + QStringLiteral("/shortcuts.json"); }

}  // namespace

Shortcuts::Shortcuts(QObject* parent) : QQmlPropertyMap(this, parent) {
  g_instance = this;
  for (const auto& c : Commands()) insert(QString::fromLatin1(c.id), c.defaults);
  QFile file(FilePath());
  if (file.open(QIODevice::ReadOnly)) {
    Apply(QJsonDocument::fromJson(file.readAll()).object().value("overrides").toObject());
  }
}

Shortcuts* Shortcuts::create(QQmlEngine*, QJSEngine*) {
  QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
  return g_instance;
}

QVariantList Shortcuts::commands() const {
  QVariantList list;
  for (const auto& c : Commands()) {
    const QString id = QString::fromLatin1(c.id);
    list.append(QVariantMap{{"id", id},
                            {"group", QString::fromLatin1(c.group)},
                            {"sequences", value(id)},
                            {"defaults", c.defaults}});
  }
  return list;
}

QStringList Shortcuts::set(const QString& id, const QStringList& sequences) {
  QStringList conflicts;
  for (const auto& c : Commands()) {
    const QString other = QString::fromLatin1(c.id);
    if (other == id) continue;
    for (const QString& s : value(other).toStringList()) {
      if (sequences.contains(s)) conflicts.append(other);
    }
  }
  insert(id, sequences);
  Save();
  emit commandsChanged();
  return conflicts;
}

void Shortcuts::resetAll() {
  for (const auto& c : Commands()) insert(QString::fromLatin1(c.id), c.defaults);
  Save();
  emit commandsChanged();
}

QJsonObject Shortcuts::Overrides() const {
  QJsonObject overrides;
  for (const auto& c : Commands()) {
    const QStringList current = value(QString::fromLatin1(c.id)).toStringList();
    if (current != c.defaults) overrides.insert(c.id, QJsonArray::fromStringList(current));
  }
  return overrides;
}

void Shortcuts::Apply(const QJsonObject& overrides) {
  for (auto it = overrides.begin(); it != overrides.end(); ++it) {
    if (!contains(it.key())) continue;  // A command this version does not have.
    QStringList sequences;
    for (const QJsonValue& v : it.value().toArray()) sequences.append(v.toString());
    insert(it.key(), sequences);
  }
}

void Shortcuts::Save() const {
  QSaveFile file(FilePath());
  if (!file.open(QIODevice::WriteOnly)) return;
  file.write(QJsonDocument(QJsonObject{{"set", "Custom"}, {"overrides", Overrides()}})
                 .toJson(QJsonDocument::Indented));
  file.commit();
}

bool Shortcuts::exportSet(const QUrl& url) const {
  QSaveFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
  if (!file.open(QIODevice::WriteOnly)) return false;
  file.write(QJsonDocument(QJsonObject{{"set", "Custom"}, {"overrides", Overrides()}})
                 .toJson(QJsonDocument::Indented));
  return file.commit();
}

bool Shortcuts::importSet(const QUrl& url) {
  QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
  if (!file.open(QIODevice::ReadOnly)) return false;
  const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
  if (!json.contains("overrides")) return false;
  for (const auto& c : Commands()) insert(QString::fromLatin1(c.id), c.defaults);
  Apply(json.value("overrides").toObject());
  Save();
  emit commandsChanged();
  return true;
}

QString Shortcuts::sequenceFor(int key, int modifiers) const {
  if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta ||
      key == 0 || key == Qt::Key_unknown) {
    return {};
  }
  return QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key)))
      .toString(QKeySequence::PortableText);
}
