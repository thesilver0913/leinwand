// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QQuickImageProvider>

// Spectrum workflow icons for QML: image://icon/<Name>/<RRGGBB or
// AARRGGBB>. The icon SVGs color themselves with a CSS variable, which Qt's
// SVG renderer does not read; the provider substitutes the requested color
// (spec 7, "アイコン").
class IconProvider : public QQuickImageProvider {
 public:
  IconProvider();
  QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;
};
