// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QJsonObject>
#include <QQmlPropertyMap>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

class QQmlEngine;
class QJSEngine;

// Keyboard shortcuts (spec 4.2, 7.3), as the QML singleton `Shortcuts`: one
// property per command holding its key sequences (a list; the first is shown
// in menus). Illustrator's defaults; changes are saved as a set in
// shortcuts.json in the settings folder, and sets can be exported and
// imported as files.
class Shortcuts : public QQmlPropertyMap {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  // {id, group, sequences, defaults} in menu order, for the dialog.
  Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged)

 public:
  explicit Shortcuts(QObject* parent = nullptr);
  static Shortcuts* create(QQmlEngine*, QJSEngine*);

  QVariantList commands() const;

  // Sets a command's sequences; returns the ids of other commands that use
  // one of them (the dialog warns), without changing those.
  Q_INVOKABLE QStringList set(const QString& id, const QStringList& sequences);
  Q_INVOKABLE void resetAll();
  Q_INVOKABLE bool exportSet(const QUrl& url) const;
  Q_INVOKABLE bool importSet(const QUrl& url);
  // A key press as sequence text ("Ctrl+Shift+Z"), or "" for a lone
  // modifier; for the dialog's key capture.
  Q_INVOKABLE QString sequenceFor(int key, int modifiers) const;

 signals:
  void commandsChanged();

 private:
  void Save() const;
  QJsonObject Overrides() const;
  void Apply(const QJsonObject& overrides);
};
