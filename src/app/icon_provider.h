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

// Thumbnails of documents for the welcome screen: image://thumbnail/<path,
// percent-encoded>. The thumbnail stored in a .lwd file; nothing for others.
class ThumbnailProvider : public QQuickImageProvider {
 public:
  ThumbnailProvider();
  QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;
};
