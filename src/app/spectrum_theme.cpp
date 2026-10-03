// SPDX-License-Identifier: GPL-3.0-or-later
#include "spectrum_theme.h"

#include <QColor>
#include <iterator>

namespace {

struct Token {
  const char* name;
  char kind;  // c color, n number, s string.
  const char* light;
  const char* dark;
};

constexpr Token kTokens[] = {
#include "spectrum_tokens.inc"
};

QVariant ValueOf(const Token& token, bool dark) {
  const QString text = QString::fromUtf8(dark ? token.dark : token.light);
  switch (token.kind) {
    case 'c':
      return QColor::fromString(text);
    case 'n':
      return text.toDouble();
    default:
      return text;
  }
}

}  // namespace

SpectrumTheme::SpectrumTheme(QObject* parent) : QQmlPropertyMap(this, parent) { Load(); }

int SpectrumTheme::tokenCount() { return static_cast<int>(std::size(kTokens)); }

void SpectrumTheme::setDark(bool dark) {
  if (dark == dark_) return;
  dark_ = dark;
  Load();
  emit darkChanged();
}

void SpectrumTheme::Load() {
  QVariantHash values;
  values.reserve(static_cast<qsizetype>(std::size(kTokens)));
  for (const Token& token : kTokens) {
    values.insert(QString::fromUtf8(token.name), ValueOf(token, dark_));
  }
  // Leinwand's names for theme-dependent choices between tokens.
  values.insert(QStringLiteral("hoverOverlay"),
                values.value(dark_ ? QStringLiteral("transparentWhite100")
                                   : QStringLiteral("transparentBlack100")));
  insert(values);
}
