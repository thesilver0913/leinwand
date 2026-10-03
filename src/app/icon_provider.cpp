// SPDX-License-Identifier: GPL-3.0-or-later
#include "icon_provider.h"

#include <QColor>
#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QUrl>
#include <filesystem>

#include "io/lwd.h"

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconProvider::requestImage(const QString& id, QSize* size, const QSize& requested_size) {
  const qsizetype slash = id.lastIndexOf(u'/');
  const QString name = slash < 0 ? id : id.left(slash);
  const QColor color = slash < 0 ? QColor(Qt::black) : QColor::fromString(u'#' + id.mid(slash + 1));

  // Workflow icons first, then Leinwand's own (spec 7: drawn on the same
  // 20x20 grid where Spectrum has no icon).
  QFile file(QStringLiteral(":/icons/spectrum/%1.svg").arg(name));
  if (!file.open(QIODevice::ReadOnly)) {
    file.setFileName(QStringLiteral(":/icons/leinwand/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) return {};
  }
  QByteArray svg = file.readAll();
  // The icons use var(--iconPrimary, #222) and similar.
  static const QRegularExpression var(QStringLiteral(R"(var\(--[A-Za-z]+,\s*#[0-9A-Fa-f]+\))"));
  const QString hex = color.name(QColor::HexRgb);
  svg = QString::fromUtf8(svg).replace(var, hex).toUtf8();

  QSvgRenderer renderer(svg);
  const QSize target = requested_size.isValid() ? requested_size : renderer.defaultSize();
  QImage image(target, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setOpacity(color.alphaF());
  renderer.render(&painter);
  painter.end();
  if (size) *size = target;
  return image;
}

ThumbnailProvider::ThumbnailProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ThumbnailProvider::requestImage(const QString& id, QSize* size,
                                       const QSize& requested_size) {
  const QString path = QUrl::fromPercentEncoding(id.toUtf8());
  const auto png = leinwand::io::ReadLwdThumbnail(std::filesystem::path(path.toStdWString()));
  QImage image = QImage::fromData(png.data(), static_cast<int>(png.size()), "PNG");
  if (!image.isNull() && requested_size.isValid()) {
    image = image.scaled(requested_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  if (size) *size = image.size();
  return image;
}
