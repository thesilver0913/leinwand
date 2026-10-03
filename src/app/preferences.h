// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QQmlPropertyMap>
#include <QString>

class QQmlEngine;
class QJSEngine;

// The preferences (spec 7.3), as the QML singleton `Preferences`: one
// property per setting, saved to a JSON file in the settings folder
// (%APPDATA%\Leinwand on Windows, ~/.config/leinwand on Linux) whenever one
// changes. Unknown keys in the file are kept.
class Preferences : public QQmlPropertyMap {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  explicit Preferences(QObject* parent = nullptr);
  static Preferences* instance();
  static Preferences* create(QQmlEngine*, QJSEngine*);

  // The settings folder, created on demand.
  static QString Folder();
  // Reads one value from the file before the application object exists
  // (the UI scale must be set before Qt starts).
  static QVariant ReadEarly(const QString& key);

  double Number(const QString& key) const { return value(key).toDouble(); }
  bool Flag(const QString& key) const { return value(key).toBool(); }
  QString Text(const QString& key) const { return value(key).toString(); }

  // Every setting back to its default (spec 7.3, "環境設定をリセット").
  Q_INVOKABLE void reset();
  // The defaults, for the dialog's per-item reset.
  Q_INVOKABLE QVariant defaultValue(const QString& key) const;

 signals:
  // Any setting changed (from QML or C++).
  void changed(const QString& key);

 protected:
  QVariant updateValue(const QString& key, const QVariant& input) override;

 private:
  void Load();
  void Save() const;

  QVariantMap unknown_;  // Keys from a newer version.
};
