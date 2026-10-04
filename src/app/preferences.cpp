// SPDX-License-Identifier: GPL-3.0-or-later
#include "preferences.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace {

Preferences* g_instance = nullptr;

// Keys and defaults. Lengths are in points, distances on screen in pixels.
const QVariantMap& Defaults() {
  static const QVariantMap defaults = {
      // General.
      {"keyboardIncrement", 1.0},   // Arrow keys move this far (Shift: ten times).
      {"showWelcome", true},        // The welcome screen at start.
      {"rubberBand", true},         // The pen's preview of the next segment.
      {"japaneseTrimMarks", true},  // Object > Create Trim Marks: Japanese or Western.
      // Preflight (spec 7.5): the current settings and saved presets
      // ({"name", "minStroke", "checks"}).
      {"preflightMinStroke", 0.1},  // mm
      {"preflightChecks", QVariantList{true, true, true, true, true, true, true}},
      {"preflightPresets", QVariantList()},
      // Selection and anchor display.
      {"pickTolerance", 4.0},  // px
      {"anchorSize", 6.0},     // px
      {"snapTolerance", 4.0},  // px, smart guides.
      // User interface.
      {"theme", QStringLiteral("dark")},       // "dark", "light" or "system".
      {"language", QStringLiteral("system")},  // "system", "ja" or "en".
      {"uiScale", 100.0},                      // Percent; applies after a restart.
      {"canvasColor", QStringLiteral("#535353")},
      // Performance.
      {"undoLimit", 0.0},  // 0: unlimited.
      // File handling.
      {"autosaveMinutes", 2.0},
      {"autosaveToFile", false},        // Saved .lwd files are written in place too.
      {"autosaveBackups", 5.0},         // Earlier versions kept per document.
      {"recoveryFolder", QString()},    // Empty: the default.
      {"recentFiles", QVariantList()},  // Newest first.
  };
  return defaults;
}

QString FilePath() { return Preferences::Folder() + QStringLiteral("/preferences.json"); }

}  // namespace

Preferences::Preferences(QObject* parent) : QQmlPropertyMap(this, parent) {
  g_instance = this;
  Load();
  connect(this, &QQmlPropertyMap::valueChanged, this, [this](const QString& key, const QVariant&) {
    Save();
    emit changed(key);
  });
}

Preferences* Preferences::instance() { return g_instance; }

Preferences* Preferences::create(QQmlEngine*, QJSEngine*) {
  QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
  return g_instance;
}

QString Preferences::Folder() {
#ifdef Q_OS_WIN
  // %APPDATA%\Leinwand (spec 7.3).
  const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  const QString folder = qEnvironmentVariable("APPDATA", base) + QStringLiteral("/Leinwand");
#elif defined(Q_OS_MACOS)
  // ~/Library/Application Support/Leinwand.
  const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                         QStringLiteral("/Leinwand");
#else
  const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                         QStringLiteral("/leinwand");
#endif
  QDir().mkpath(folder);
  return folder;
}

QVariant Preferences::ReadEarly(const QString& key) {
  QFile file(FilePath());
  if (file.open(QIODevice::ReadOnly)) {
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.contains(key)) return json.value(key).toVariant();
  }
  return Defaults().value(key);
}

void Preferences::Load() {
  QVariantHash values;
  for (auto it = Defaults().begin(); it != Defaults().end(); ++it)
    values.insert(it.key(), it.value());
  QFile file(FilePath());
  if (file.open(QIODevice::ReadOnly)) {
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = json.begin(); it != json.end(); ++it) {
      if (Defaults().contains(it.key())) {
        values.insert(it.key(), it.value().toVariant());
      } else {
        unknown_.insert(it.key(), it.value().toVariant());
      }
    }
  }
  insert(values);
}

void Preferences::Save() const {
  QJsonObject json = QJsonObject::fromVariantMap(unknown_);
  for (const QString& key : keys()) json.insert(key, QJsonValue::fromVariant(value(key)));
  QSaveFile file(FilePath());
  if (!file.open(QIODevice::WriteOnly)) return;
  file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
  file.commit();
}

QVariant Preferences::updateValue(const QString& key, const QVariant& input) {
  // Keep values within sense; unknown keys are not settings.
  if (!Defaults().contains(key)) return value(key);
  const QVariant fallback = Defaults().value(key);
  if (fallback.typeId() == QMetaType::Double) {
    bool ok = false;
    double v = input.toDouble(&ok);
    if (!ok) return value(key);
    if (key == QLatin1String("uiScale")) v = std::clamp(v, 50.0, 300.0);
    if (key == QLatin1String("autosaveMinutes")) v = std::clamp(v, 0.0, 120.0);
    if (key == QLatin1String("undoLimit")) v = std::clamp(v, 0.0, 10000.0);
    if (key == QLatin1String("pickTolerance") || key == QLatin1String("snapTolerance")) {
      v = std::clamp(v, 1.0, 20.0);
    }
    if (key == QLatin1String("anchorSize")) v = std::clamp(v, 3.0, 16.0);
    if (key == QLatin1String("keyboardIncrement")) v = std::clamp(v, 0.001, 1000.0);
    return v;
  }
  if (fallback.typeId() == QMetaType::Bool) return input.toBool();
  if (fallback.typeId() == QMetaType::QVariantList) return input.toList();
  if (key == QLatin1String("canvasColor")) {
    const QColor color(input.toString());
    return color.isValid() ? QVariant(color.name()) : value(key);
  }
  return input.toString();
}

void Preferences::reset() {
  for (auto it = Defaults().begin(); it != Defaults().end(); ++it) {
    if (value(it.key()) != it.value()) {
      insert(it.key(), it.value());
      emit changed(it.key());
    }
  }
  Save();
}

QVariant Preferences::defaultValue(const QString& key) const { return Defaults().value(key); }
