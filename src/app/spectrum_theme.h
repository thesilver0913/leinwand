// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QQmlPropertyMap>

// The Spectrum 2 design tokens as the QML singleton `Spectrum` (spec 7,
// "デザイン言語"). Every token converted at build time from
// @adobe/spectrum-tokens is a property named in camelCase
// (background-layer-1-color -> backgroundLayer1Color): colors as colors,
// sizes as numbers of pixels at the desktop scale. `dark` switches the theme
// and all bindings follow.
class SpectrumTheme : public QQmlPropertyMap {
  Q_OBJECT
  QML_NAMED_ELEMENT(Spectrum)
  QML_SINGLETON
  Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
  // Adobe Clean is reserved for Adobe products; the spec uses Source Sans 3.
  Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)

 public:
  explicit SpectrumTheme(QObject* parent = nullptr);

  bool dark() const { return dark_; }
  void setDark(bool dark);
  QString fontFamily() const { return QStringLiteral("Source Sans 3"); }
  static int tokenCount();

 signals:
  void darkChanged();

 private:
  void Load();

  bool dark_ = true;
};
