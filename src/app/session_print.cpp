// SPDX-License-Identifier: GPL-3.0-or-later
// Printing (spec 7.4). Pages are drawn by the same renderer as export, in
// bands at up to 300 ppi, and sent to the printer as images through Qt's
// print support (the OS's printer drivers). Printer-specific settings come
// from the OS's own dialog.
#include <QBuffer>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <algorithm>
#include <cmath>

#include "geometry/bezier.h"
#include "render/document_renderer.h"
#include "session.h"

namespace {

using leinwand::core::Rect;
using leinwand::render::Page;

// The settings from the Print dialog (see Session::print).
struct PrintSettings {
  QString printer;
  QString paper;    // "printer" (its own), "A4", "A3", "B4", "B5", "Letter", "Legal".
  int orientation;  // 0 auto, 1 portrait, 2 landscape.
  int range;        // 0 all artboards, 1 the active one, 2 all artwork on one page.
  int scaling;      // 0 actual size, 1 fit to paper, 2 percent.
  double percent;
  int position;  // 0..8, rows of three from the top left; 4 is the middle.
  int marks;     // 0 none, 1 Japanese, 2 Western.
  int copies;
  bool collate;
  QString output;  // A PDF file instead of a printer (tests, "print to file").
};

PrintSettings SettingsOf(const QVariantMap& map) {
  PrintSettings s;
  s.printer = map.value("printer").toString();
  s.paper = map.value("paper", QStringLiteral("printer")).toString();
  s.orientation = map.value("orientation", 0).toInt();
  s.range = map.value("range", 0).toInt();
  s.scaling = map.value("scaling", 0).toInt();
  s.percent = map.value("percent", 100.0).toDouble();
  s.position = std::clamp(map.value("position", 4).toInt(), 0, 8);
  s.marks = map.value("marks", 0).toInt();
  s.copies = std::max(1, map.value("copies", 1).toInt());
  s.collate = map.value("collate", true).toBool();
  s.output = map.value("output").toString();
  return s;
}

QPageSize PaperOf(const QString& paper, const QPrinter& printer) {
  if (paper == "A4") return QPageSize(QPageSize::A4);
  if (paper == "A3") return QPageSize(QPageSize::A3);
  if (paper == "B4") return QPageSize(QPageSize::JisB4);
  if (paper == "B5") return QPageSize(QPageSize::JisB5);
  if (paper == "Letter") return QPageSize(QPageSize::Letter);
  if (paper == "Legal") return QPageSize(QPageSize::Legal);
  return printer.pageLayout().pageSize();
}

// One sheet: the page, how big on paper and where.
struct Sheet {
  Page page;
  QPageLayout::Orientation orientation;
  QSizeF paper;   // Points, after orientation.
  QRectF placed;  // Points on the paper.
  double scale = 1;
};

std::vector<Sheet> Layout(const leinwand::core::Document& document, int active,
                          const PrintSettings& s, const QPageSize& paper_size) {
  std::vector<Page> pages;
  std::optional<leinwand::core::TrimMarkStyle> marks;
  if (s.marks == 1) marks = leinwand::core::TrimMarkStyle::kJapanese;
  if (s.marks == 2) marks = leinwand::core::TrimMarkStyle::kWestern;
  if (s.range == 2) {
    // All artwork on one page, artboards ignored.
    Rect bounds;
    for (const auto& layer : document.layers) {
      for (const auto& child : layer->children) {
        if (const auto* object = std::get_if<leinwand::core::ObjectPtr>(&child)) {
          bounds = bounds.Union(leinwand::geometry::Bounds(**object));
        }
      }
    }
    if (bounds.IsValid() && bounds.width() > 0 && bounds.height() > 0) {
      pages.push_back({bounds, bounds, {}});
    }
  } else {
    for (int i = 0; i < static_cast<int>(document.artboards.size()); ++i) {
      if (s.range == 1 && i != active) continue;
      pages.push_back(leinwand::render::DocumentRenderer::ArtboardPage(
          document.artboards[std::size_t(i)], marks));
    }
  }
  std::vector<Sheet> sheets;
  const QSizeF portrait = paper_size.size(QPageSize::Point);
  for (const Page& page : pages) {
    Sheet sheet;
    sheet.page = page;
    const double w = page.area.width(), h = page.area.height();
    sheet.orientation = s.orientation == 2 ? QPageLayout::Landscape
                        : s.orientation == 1
                            ? QPageLayout::Portrait
                            : (w > h ? QPageLayout::Landscape : QPageLayout::Portrait);
    const bool landscape = sheet.orientation == QPageLayout::Landscape;
    const double pw = landscape ? std::max(portrait.width(), portrait.height())
                                : std::min(portrait.width(), portrait.height());
    const double ph = landscape ? std::min(portrait.width(), portrait.height())
                                : std::max(portrait.width(), portrait.height());
    sheet.paper = {pw, ph};
    sheet.scale = s.scaling == 1   ? std::min(pw / w, ph / h)
                  : s.scaling == 2 ? std::max(s.percent, 1.0) / 100
                                   : 1.0;
    const double sw = w * sheet.scale, sh = h * sheet.scale;
    const int col = s.position % 3, row = s.position / 3;
    const double x = col == 0 ? 0 : col == 1 ? (pw - sw) / 2 : pw - sw;
    const double y = row == 0 ? 0 : row == 1 ? (ph - sh) / 2 : ph - sh;
    sheet.placed = {x, y, sw, sh};
    sheets.push_back(sheet);
  }
  return sheets;
}

}  // namespace

QStringList Session::printers() const { return QPrinterInfo::availablePrinterNames(); }

QString Session::defaultPrinter() const { return QPrinterInfo::defaultPrinterName(); }

QPrinter& Session::Printer() {
  if (!printer_) printer_ = std::make_unique<QPrinter>(QPrinter::HighResolution);
  return *printer_;
}

void Session::printerSetup(const QString& printer) {
  QPrinter& device = Printer();
  if (!printer.isEmpty()) device.setPrinterName(printer);
  QPrintDialog dialog(&device);
  dialog.setOption(QAbstractPrintDialog::PrintToFile, false);
  dialog.exec();
}

QVariantMap Session::printPreview(const QVariantMap& settings) {
  const PrintSettings s = SettingsOf(settings);
  QPrinter& device = Printer();
  if (!s.printer.isEmpty() && s.output.isEmpty()) device.setPrinterName(s.printer);
  const auto sheets =
      Layout(editor_->document(), editor_->active_artboard(), s, PaperOf(s.paper, device));
  QVariantMap map;
  map["pages"] = static_cast<int>(sheets.size());
  if (sheets.empty()) return map;
  const Sheet& sheet = sheets.front();
  map["paperWidth"] = sheet.paper.width();
  map["paperHeight"] = sheet.paper.height();
  map["x"] = sheet.placed.x();
  map["y"] = sheet.placed.y();
  map["width"] = sheet.placed.width();
  map["height"] = sheet.placed.height();
  map["scale"] = sheet.scale;
  // The first page, small, for the dialog.
  const double scale = 360.0 / std::max(sheet.page.area.width(), sheet.page.area.height());
  const int w = std::max(1, static_cast<int>(std::ceil(sheet.page.area.width() * scale)));
  const int h = std::max(1, static_cast<int>(std::ceil(sheet.page.area.height() * scale)));
  leinwand::render::DocumentRenderer renderer;
  auto pixels = renderer.RenderPage(editor_->document(), sheet.page, scale, 0, w, h);
  if (!pixels.empty()) {
    const QImage image(pixels.data(), w, h, QImage::Format_RGBA8888_Premultiplied);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    map["image"] = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
  }
  return map;
}

bool Session::print(const QVariantMap& settings) {
  const PrintSettings s = SettingsOf(settings);
  QPrinter& device = Printer();
  if (!s.output.isEmpty()) {
    device.setOutputFormat(QPrinter::PdfFormat);
    device.setOutputFileName(s.output);
  } else {
    device.setOutputFormat(QPrinter::NativeFormat);
    if (!s.printer.isEmpty()) device.setPrinterName(s.printer);
  }
  const QPageSize paper = PaperOf(s.paper, device);
  const auto sheets = Layout(editor_->document(), editor_->active_artboard(), s, paper);
  if (sheets.empty()) return Fail(tr("There is nothing to print."));
  device.setFullPage(true);
  device.setCopyCount(s.copies);
  device.setCollateCopies(s.collate);
  device.setPageSize(paper);
  device.setPageOrientation(sheets.front().orientation);
  QPainter painter;
  if (!painter.begin(&device)) return Fail(tr("Could not start printing."));
  leinwand::render::DocumentRenderer renderer;
  // Rasters at the printer's resolution, up to 300 ppi, in bands.
  const double ppi = std::min(300, std::max(72, device.resolution()));
  for (std::size_t i = 0; i < sheets.size(); ++i) {
    const Sheet& sheet = sheets[i];
    if (i > 0) {
      device.setPageOrientation(sheet.orientation);
      device.newPage();
    }
    const double device_per_point = device.resolution() / 72.0;
    const double raster_scale = sheet.scale * ppi / 72.0;  // Raster pixels per document point.
    const int width = static_cast<int>(std::ceil(sheet.page.area.width() * raster_scale));
    const int height = static_cast<int>(std::ceil(sheet.page.area.height() * raster_scale));
    constexpr int kBand = 512;
    for (int top = 0; top < height; top += kBand) {
      const int rows = std::min(kBand, height - top);
      auto pixels =
          renderer.RenderPage(editor_->document(), sheet.page, raster_scale, top, width, rows);
      if (pixels.empty()) {
        painter.end();
        return Fail(tr("The page is too large to print at this size."));
      }
      const QImage band(pixels.data(), width, rows, QImage::Format_RGBA8888_Premultiplied);
      // The band on the paper, in device pixels.
      const double to_paper = sheet.scale / raster_scale;  // Points on paper per raster pixel.
      const QRectF target((sheet.placed.x()) * device_per_point,
                          (sheet.placed.y() + top * to_paper) * device_per_point,
                          width * to_paper * device_per_point, rows * to_paper * device_per_point);
      painter.drawImage(target, band);
    }
  }
  painter.end();
  return true;
}
