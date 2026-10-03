// SPDX-License-Identifier: GPL-3.0-or-later
#include "session.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <variant>

#include "core/gradient.h"
#include "core/style.h"
#include "editor/number_input.h"
#include "editor/preflight.h"
#include "io/lwd.h"
#include "io/svg.h"
#include "layers_model.h"
#include "preferences.h"
#include "render/document_renderer.h"
#include "render/test_document.h"
#include "text/font.h"

namespace {

using leinwand::core::Color;
using leinwand::core::RgbColor;
using leinwand::editor::Tool;

constexpr int kHand = 12;
constexpr int kZoom = 13;
// Editor tools after the eyedropper come after the view tools in QML's
// numbering: QML's 14 is the editor's kScissors, 15 its kArtboard, 16 its
// kGradient, 17 its kType.
constexpr int kAfterView = 2;

Session* g_instance = nullptr;

int CountObjects(const leinwand::core::Document& document) {
  int count = 0;
  leinwand::core::VisitObjects(document, [&](const leinwand::core::Object&) { ++count; });
  return count;
}

RgbColor ToRgb(const QColor& color) {
  const QColor rgb = color.toRgb();
  return {rgb.redF(), rgb.greenF(), rgb.blueF()};
}

QColor ToQColor(const std::optional<Color>& paint, const leinwand::core::Document& document) {
  if (!paint) return {};
  const auto rgb = leinwand::core::ToRgb(*paint, document);
  if (!rgb) return {};
  return QColor::fromRgbF(static_cast<float>(rgb->r), static_cast<float>(rgb->g),
                          static_cast<float>(rgb->b));
}

constexpr const char* kAppVersion = "Leinwand " LEINWAND_VERSION;

// The preferences' recovery folder, or the default in the local data folder.
QString RecoveryDir() {
  const QString chosen = Preferences::instance()->Text(QStringLiteral("recoveryFolder"));
  if (!chosen.isEmpty()) return chosen;
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
         QStringLiteral("/recovery");
}

// A path from a file dialog's URL, or the string itself.
QString LocalPath(const QUrl& url) {
  return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

std::filesystem::path FsPath(const QString& path) {
  return std::filesystem::path(path.toStdWString());
}

bool WriteBytes(const QString& path, const std::vector<std::uint8_t>& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  return file.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size());
}

// The first artboard, at most 512 px on its longer side (spec 3.1).
std::vector<std::uint8_t> Thumbnail(const leinwand::core::Document& document) {
  if (document.artboards.empty()) return {};
  const auto area = document.artboards.front().bounds;
  const double longer = std::max(area.width(), area.height());
  if (longer <= 0) return {};
  return leinwand::render::DocumentRenderer::ExportPng(document, area, std::min(1.0, 512 / longer),
                                                       false);
}

QString ActionName(leinwand::io::ReportAction action) {
  switch (action) {
    case leinwand::io::ReportAction::kPreserved:
      return QStringLiteral("preserved");
    case leinwand::io::ReportAction::kApproximated:
      return QStringLiteral("approximated");
    case leinwand::io::ReportAction::kConverted:
      return QStringLiteral("converted");
    case leinwand::io::ReportAction::kDiscarded:
      return QStringLiteral("discarded");
  }
  return {};
}

QVariantList ReportRows(const leinwand::io::ImportReport& report) {
  QVariantList rows;
  for (const auto& row : report.rows) {
    QVariantList ids;
    for (const auto& id : row.object_ids) ids.append(QString::fromStdString(id));
    rows.append(QVariantMap{{"kind", QString::fromStdString(row.kind)},
                            {"action", ActionName(row.action)},
                            {"count", row.count},
                            {"ids", ids}});
  }
  return rows;
}

}  // namespace

Session::Session(QObject* parent) : QObject(parent), layers_(std::make_unique<LayersModel>(this)) {
  g_instance = this;
  // Recovery files from earlier sessions that did not close normally.
  const QDir dir(RecoveryDir());
  const auto entries = dir.entryInfoList({QStringLiteral("*.lwd")}, QDir::Files, QDir::Time);
  for (const QFileInfo& info : entries) {
    QFile sidecar(info.absoluteFilePath() + QStringLiteral(".json"));
    QString original;
    if (sidecar.open(QIODevice::ReadOnly)) {
      original = QJsonDocument::fromJson(sidecar.readAll()).object().value("original").toString();
    }
    recovery_files_.append(QVariantMap{
        {"path", info.absoluteFilePath()}, {"original", original}, {"time", info.lastModified()}});
  }
  recovery_path_ = dir.absoluteFilePath(QStringLiteral("session-%1-%2.lwd")
                                            .arg(QCoreApplication::applicationPid())
                                            .arg(QDateTime::currentMSecsSinceEpoch()));
  autosave_ = new QTimer(this);
  connect(autosave_, &QTimer::timeout, this, &Session::Autosave);
  connect(Preferences::instance(), &Preferences::changed, this, [this] { ApplyPreferences(); });
  closeDocument();
}

Session::~Session() {
  if (g_instance == this) g_instance = nullptr;
}

Session* Session::instance() { return g_instance; }

Session* Session::create(QQmlEngine*, QJSEngine*) {
  // Created in main(); QML must not take ownership.
  QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
  return g_instance;
}

void Session::closeDocument() {
  RemoveRecovery();
  SetDocument(leinwand::core::Document{});
  has_document_ = false;
  file_path_.clear();
  display_name_.clear();
  emit fileChanged();
}

void Session::AddRecent(const QString& path) {
  Preferences& preferences = *Preferences::instance();
  QVariantList recent = preferences.value(QStringLiteral("recentFiles")).toList();
  recent.removeAll(path);
  recent.prepend(path);
  while (recent.size() > 20) recent.removeLast();
  preferences.insert(QStringLiteral("recentFiles"), recent);
  emit preferences.valueChanged(QStringLiteral("recentFiles"), recent);
}

void Session::SetDocument(leinwand::core::Document document) {
  has_document_ = true;
  const bool guides = editor_ ? editor_->smart_guides() : true;
  editor_ = std::make_unique<leinwand::editor::Editor>(std::move(document));
  editor_->SetSmartGuides(guides);
  editor_->SetPathOpsEngine(&path_ops_);
  editor_->SetArtboardNamePrefix(tr("Artboard").toStdString());
  editor_->SetTrimMarksName(tr("Trim Marks").toStdString());
  view_tool_ = -1;
  ApplyPreferences();
  saved_revision_ = autosaved_revision_ = editor_->history().revision();
  if (!import_report_.isEmpty()) SetReport({});
  Changed();
  emit toolChanged();
  emit documentReplaced();
}

void Session::ApplyPreferences() {
  const Preferences& p = *Preferences::instance();
  const double pick = p.Number(QStringLiteral("pickTolerance"));
  editor_->SetSnapScale(pick > 0 ? p.Number(QStringLiteral("snapTolerance")) / pick : 1.0);
  editor_->SetRubberBand(p.Flag(QStringLiteral("rubberBand")));
  editor_->SetUndoLimit(static_cast<std::size_t>(p.Number(QStringLiteral("undoLimit"))));
  // Autosave every so many minutes (spec 3.3); 0 turns it off.
  const double minutes = p.Number(QStringLiteral("autosaveMinutes"));
  if (minutes > 0) {
    autosave_->start(static_cast<int>(minutes * 60 * 1000));
  } else {
    autosave_->stop();
  }
}

bool Session::dirty() const { return editor_->history().revision() != saved_revision_; }

bool Session::Fail(const QString& message) {
  error_ = message;
  emit errorChanged();
  return false;
}

void Session::SetReport(const leinwand::io::ImportReport& report) {
  // Fonts the document names that are not here (spec 5.2): shown with a
  // substitute until they are installed.
  leinwand::io::ImportReport full = report;
  if (editor_) {
    leinwand::core::VisitObjects(editor_->document(), [&](const leinwand::core::Object& object) {
      const auto* text = std::get_if<leinwand::core::TextObject>(&object);
      if (!text || !text->story) return;
      std::set<std::string> reported;
      for (const auto& run : text->story->characters) {
        const auto& font = run.style.font;
        if (leinwand::text::IsAvailable(font)) continue;
        const std::string name = font.family + " " + font.style;
        if (reported.insert(name).second) {
          full.Add("missing font \"" + name + "\" (shown with a substitute)",
                   leinwand::io::ReportAction::kApproximated, leinwand::core::CommonOf(object).id);
        }
      }
    });
  }
  import_report_ = ReportRows(full);
  emit importReportChanged();
}

void Session::newDocument(double width, double height, double bleed) {
  RemoveRecovery();
  leinwand::core::Document document =
      leinwand::core::NewDocument(tr("Layer 1").toStdString(), std::max(width, 1.0),
                                  std::max(height, 1.0), std::max(bleed, 0.0));
  document.artboards.front().name = tr("Artboard %1").arg(1).toStdString();
  SetDocument(std::move(document));
  file_path_.clear();
  display_name_ = tr("Untitled-%1").arg(++untitled_);
  emit fileChanged();
}

bool Session::open(const QUrl& url) {
  const QString path = LocalPath(url);
  const QFileInfo info(path);
  if (info.suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Fail(tr("Could not read %1.").arg(info.fileName()));
    const QByteArray xml = file.readAll();
    auto result = leinwand::io::ImportSvg(std::string_view(xml.constData(), xml.size()));
    if (!result.document) {
      return Fail(tr("%1 is not a readable SVG file (%2).")
                      .arg(info.fileName(), QString::fromStdString(result.error)));
    }
    RemoveRecovery();
    SetDocument(std::move(*result.document));
    // Imported: saving asks for a .lwd file name.
    file_path_.clear();
    display_name_ = info.fileName();
    emit fileChanged();
    SetReport(result.report);
    AddRecent(info.absoluteFilePath());
    return true;
  }
  auto result = leinwand::io::LoadLwd(FsPath(path));
  switch (result.error) {
    case leinwand::io::LoadError::kNone:
      break;
    case leinwand::io::LoadError::kNotFound:
      return Fail(tr("Could not read %1.").arg(info.fileName()));
    case leinwand::io::LoadError::kNotLeinwand:
      return Fail(tr("%1 is not a Leinwand document.").arg(info.fileName()));
    case leinwand::io::LoadError::kNewerVersion:
      return Fail(tr("%1 was made with a newer version of Leinwand (format %2). Update Leinwand "
                     "to open it.")
                      .arg(info.fileName(), QString::fromStdString(result.message)));
    case leinwand::io::LoadError::kCorrupt:
      return Fail(tr("%1 is damaged and cannot be opened (%2).")
                      .arg(info.fileName(), QString::fromStdString(result.message)));
  }
  RemoveRecovery();
  SetDocument(std::move(*result.document));
  file_path_ = info.absoluteFilePath();
  display_name_ = info.fileName();
  emit fileChanged();
  SetReport(result.report);
  AddRecent(file_path_);
  return true;
}

bool Session::openPath(const QString& path) {
  return open(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
}

bool Session::save() {
  if (file_path_.isEmpty()) return false;
  return saveAs(QUrl::fromLocalFile(file_path_));
}

bool Session::saveAs(const QUrl& url) {
  QString path = LocalPath(url);
  if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".lwd");
  const auto& document = editor_->document();
  std::string error;
  if (!leinwand::io::SaveLwd(FsPath(path), document, kAppVersion, Thumbnail(document), &error)) {
    return Fail(tr("Could not save %1 (%2). The file on disk was not changed.")
                    .arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
  }
  file_path_ = QFileInfo(path).absoluteFilePath();
  display_name_ = QFileInfo(path).fileName();
  saved_revision_ = editor_->history().revision();
  RemoveRecovery();  // Saved: nothing to recover.
  AddRecent(file_path_);
  emit fileChanged();
  emit documentChanged();
  return true;
}

QVariantList Session::svgExportIssues() const {
  leinwand::io::ImportReport issues;
  leinwand::io::ExportSvg(editor_->document(), {}, &issues);
  return ReportRows(issues);
}

bool Session::exportSvg(const QUrl& url) {
  const QString path = LocalPath(url);
  leinwand::io::SvgExportOptions options;
  options.artboard = editor_->active_artboard();
  const std::string svg = leinwand::io::ExportSvg(editor_->document(), options);
  if (!WriteBytes(path, std::vector<std::uint8_t>(svg.begin(), svg.end()))) {
    return Fail(tr("Could not write %1.").arg(QFileInfo(path).fileName()));
  }
  return true;
}

bool Session::exportPdf(const QUrl& url, bool all_artboards, bool outline_text, int marks) {
  QString path = LocalPath(url);
  if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".pdf");
  const auto& document = editor_->document();
  if (document.artboards.empty()) return Fail(tr("The document has no artboard to export."));
  leinwand::render::PdfOptions options;
  if (!all_artboards) options.artboards = {editor_->active_artboard()};
  options.outline_text = outline_text;
  if (marks == 1) options.marks = leinwand::core::TrimMarkStyle::kJapanese;
  if (marks == 2) options.marks = leinwand::core::TrimMarkStyle::kWestern;
  options.title = display_name_.toStdString();
  options.creator = std::string("Leinwand ") + LEINWAND_VERSION;
  const auto pdf = leinwand::render::DocumentRenderer::ExportPdf(document, options);
  if (pdf.empty()) return Fail(tr("Could not make the PDF."));
  if (!WriteBytes(path, pdf))
    return Fail(tr("Could not write %1.").arg(QFileInfo(path).fileName()));
  return true;
}

bool Session::exportPng(const QUrl& url, double scale, bool transparent, bool all_artboards) {
  const QString path = LocalPath(url);
  const auto& document = editor_->document();
  if (document.artboards.empty()) return Fail(tr("The document has no artboard to export."));
  std::vector<std::pair<QString, leinwand::core::Rect>> files;
  if (all_artboards) {
    // name.png -> name-Artboard 1.png, ... (characters files cannot hold
    // become "_").
    const QFileInfo info(path);
    static const QRegularExpression bad(QStringLiteral(R"([\\/:*?"<>|])"));
    for (const auto& artboard : document.artboards) {
      QString name = QString::fromStdString(artboard.name);
      name.replace(bad, QStringLiteral("_"));
      files.push_back({info.dir().filePath(info.completeBaseName() + QStringLiteral("-") + name +
                                           QStringLiteral(".png")),
                       artboard.bounds});
    }
  } else {
    files.push_back({path, document.artboards[size_t(editor_->active_artboard())].bounds});
  }
  for (const auto& [file, area] : files) {
    const auto png =
        leinwand::render::DocumentRenderer::ExportPng(document, area, scale, transparent);
    if (png.empty()) return Fail(tr("The image would be too large at this resolution."));
    if (!WriteBytes(file, png)) {
      return Fail(tr("Could not write %1.").arg(QFileInfo(file).fileName()));
    }
  }
  return true;
}

bool Session::restore(const QString& path) {
  auto result = leinwand::io::LoadLwd(FsPath(path));
  if (!result.document) return Fail(tr("The recovery file could not be read."));
  QString original;
  for (const QVariant& entry : recovery_files_) {
    const QVariantMap map = entry.toMap();
    if (map.value("path").toString() == path) original = map.value("original").toString();
  }
  RemoveRecovery();
  SetDocument(std::move(*result.document));
  // Unsaved: the recovered state is newer than any file.
  saved_revision_ = ~std::uint64_t{0};
  // Keep writing to the same recovery file until the document is saved.
  recovery_path_ = path;
  file_path_ = original;
  display_name_ = original.isEmpty() ? tr("Recovered") : QFileInfo(original).fileName();
  emit fileChanged();
  emit documentChanged();
  return true;
}

void Session::discardRecovery(const QString& path) {
  QFile::remove(path);
  QFile::remove(path + QStringLiteral(".json"));
}

void Session::selectReported(const QVariantList& ids) {
  leinwand::core::IdSet set;
  for (const QVariant& id : ids) set.insert(id.toString().toStdString());
  editor_->Select(set);
  Changed();
}

void Session::Autosave() {
  if (!dirty() || editor_->history().revision() == autosaved_revision_) return;
  autosaved_revision_ = editor_->history().revision();
  QDir().mkpath(RecoveryDir());
  // A snapshot: the model is immutable, so the copy can be written on another
  // thread while editing goes on (spec 3.3).
  const leinwand::core::Document snapshot = editor_->document();
  const std::filesystem::path path = FsPath(recovery_path_);
  const QString sidecar = recovery_path_ + QStringLiteral(".json");
  const QByteArray info = QJsonDocument(QJsonObject{{"original", file_path_}}).toJson();
  QThreadPool::globalInstance()->start([snapshot, path, sidecar, info] {
    leinwand::io::SaveLwd(path, snapshot, kAppVersion, {}, nullptr);
    QFile file(sidecar);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(info);
  });
}

void Session::RemoveRecovery() {
  QThreadPool::globalInstance()->waitForDone();
  discardRecovery(recovery_path_);
  autosaved_revision_ = 0;
}

void Session::closeCleanly() { RemoveRecovery(); }

void Session::Changed() {
  object_count_ = CountObjects(editor_->document());
  layers_->Refresh();
  emit documentChanged();
}

void Session::loadShowcase() {
  SetDocument(leinwand::render::MakeShowcaseDocument());
  file_path_.clear();
  display_name_ = QStringLiteral("Showcase");
  emit fileChanged();
}

void Session::loadTestDocument(int pathCount) {
  SetDocument(leinwand::render::MakeTestDocument(pathCount));
  file_path_.clear();
  display_name_ = QStringLiteral("Test %1").arg(pathCount);
  emit fileChanged();
}

QString Session::undoAction() const {
  return QString::fromStdString(editor_->history().undo_action());
}

QString Session::redoAction() const {
  return QString::fromStdString(editor_->history().redo_action());
}

int Session::tool() const {
  if (view_tool_ >= 0) return view_tool_;
  const int tool = static_cast<int>(editor_->tool());
  return tool > static_cast<int>(Tool::kEyedropper) ? tool + kAfterView : tool;
}

void Session::setTool(int tool) {
  if (tool == kHand || tool == kZoom) {
    if (view_tool_ == tool) return;
    editor_->FinishPath();
    view_tool_ = tool;
  } else {
    if (tool > kZoom) tool -= kAfterView;
    if (tool < 0 || tool > static_cast<int>(Tool::kType)) return;
    if (view_tool_ < 0 && tool == static_cast<int>(editor_->chosen_tool()) &&
        tool == this->tool()) {
      return;
    }
    view_tool_ = -1;
    editor_->SetTool(static_cast<Tool>(tool));
  }
  emit toolChanged();
  Changed();
}

void Session::UpdateTemporaryTool(Qt::KeyboardModifiers modifiers) {
  if (view_tool_ >= 0) return;
  const Tool chosen = editor_->chosen_tool();
  std::optional<Tool> temporary;
  if (modifiers.testFlag(Qt::ControlModifier)) {
    if (chosen != Tool::kSelection && chosen != Tool::kDirectSelection) {
      temporary = editor_->last_selection_tool();
    }
  } else if (modifiers.testFlag(Qt::AltModifier) && chosen == Tool::kPen) {
    temporary = Tool::kConvertAnchor;
  }
  if (temporary.value_or(chosen) == editor_->tool()) return;
  editor_->SetTemporaryTool(temporary);
  emit toolChanged();
}

void Session::setSmartGuides(bool on) {
  if (on == editor_->smart_guides()) return;
  editor_->SetSmartGuides(on);
  emit settingsChanged();
}

void Session::SetCanvas(QObject* canvas) {
  if (canvas == canvas_) return;
  canvas_ = canvas;
  emit canvasChanged();
}

void Session::setFillActive(bool fill) {
  if (fill == editor_->fill_active()) return;
  editor_->SetFillActive(fill);
  emit documentChanged();
}

#define LEINWAND_COMMAND(name, call) \
  void Session::name() {             \
    editor_->call();                 \
    Changed();                       \
  }
LEINWAND_COMMAND(undo, Undo)
LEINWAND_COMMAND(redo, Redo)
LEINWAND_COMMAND(selectAll, SelectAll)
LEINWAND_COMMAND(deselect, Deselect)
LEINWAND_COMMAND(deleteSelection, Delete)
LEINWAND_COMMAND(group, Group)
LEINWAND_COMMAND(ungroup, Ungroup)
LEINWAND_COMMAND(removeAnchors, RemoveSelectedAnchors)
LEINWAND_COMMAND(cutAtAnchor, CutAtSelectedAnchor)
LEINWAND_COMMAND(joinEnds, JoinSelectedEnds)
LEINWAND_COMMAND(makeCompoundPath, MakeCompoundPath)
LEINWAND_COMMAND(releaseCompoundPath, ReleaseCompoundPath)
LEINWAND_COMMAND(makeClippingMask, MakeClippingMask)
LEINWAND_COMMAND(createOutlines, CreateOutlines)
LEINWAND_COMMAND(revertOutlines, RevertOutlines)
LEINWAND_COMMAND(releaseClippingMask, ReleaseClippingMask)
LEINWAND_COMMAND(makeOpacityMask, MakeOpacityMask)
LEINWAND_COMMAND(releaseOpacityMask, ReleaseOpacityMask)
LEINWAND_COMMAND(swapFillAndStroke, SwapFillAndStroke)
LEINWAND_COMMAND(defaultFillAndStroke, DefaultFillAndStroke)
#undef LEINWAND_COMMAND

QVariantList Session::artboards() const {
  QVariantList list;
  for (const auto& artboard : editor_->document().artboards) {
    const auto& b = artboard.bounds;
    list.append(QVariantMap{{"name", QString::fromStdString(artboard.name)},
                            {"x", b.left},
                            {"y", b.top},
                            {"width", b.width()},
                            {"height", b.height()}});
  }
  return list;
}

void Session::setActiveArtboard(int index) {
  if (index == activeArtboard()) return;
  editor_->SetActiveArtboard(index);
  emit documentChanged();
}

namespace {

leinwand::core::CoverSpec CoverOf(double width, double height, int pages, double thickness,
                                  double spine, double bleed) {
  leinwand::core::CoverSpec spec;
  spec.width = width;
  spec.height = height;
  spec.pages = pages;
  spec.paper_thickness = thickness;
  if (!std::isnan(spine)) spec.spine = spine;
  spec.bleed = std::max(bleed, 0.0);
  return spec;
}

leinwand::core::CoverNames CoverNames() {
  return {QCoreApplication::translate("Session", "Cover spread").toStdString(),
          QCoreApplication::translate("Session", "Back cover").toStdString(),
          QCoreApplication::translate("Session", "Spine").toStdString(),
          QCoreApplication::translate("Session", "Front cover").toStdString()};
}

}  // namespace

void Session::newCoverDocument(double width, double height, int pages, double thickness,
                               double spine, double bleed) {
  const auto spec = CoverOf(width, height, pages, thickness, spine, bleed);
  if (spec.width <= 0 || spec.height <= 0) return;
  RemoveRecovery();
  leinwand::core::Document document =
      leinwand::core::NewDocument(tr("Layer 1").toStdString(), 1, 1, 0);
  document.artboards.clear();
  SetDocument(leinwand::core::WithCover(std::move(document), spec, CoverNames()));
  file_path_.clear();
  display_name_ = tr("Untitled-%1").arg(++untitled_);
  emit fileChanged();
}

void Session::setCover(double width, double height, int pages, double thickness, double spine,
                       double bleed) {
  editor_->SetCover(CoverOf(width, height, pages, thickness, spine, bleed), CoverNames());
  Changed();
}

QVariantMap Session::cover() const {
  const auto& cover = editor_->document().cover;
  if (!cover) return {};
  return {{"width", cover->width},
          {"height", cover->height},
          {"pages", cover->pages},
          {"thickness", cover->paper_thickness},
          {"spine", cover->spine ? QVariant(*cover->spine) : QVariant()},
          {"spineWidth", leinwand::core::SpineWidth(*cover)},
          {"bleed", cover->bleed}};
}

void Session::addArtboard() {
  // Same size as the active one, to the right of everything, 20 pt apart.
  const auto& artboards = editor_->document().artboards;
  leinwand::core::Rect size = {0, 0, 595.28, 841.89};
  double right = 0, top = 0;
  if (!artboards.empty()) {
    size = artboards[size_t(editor_->active_artboard())].bounds;
    right = artboards.front().bounds.right;
    top = artboards.front().bounds.top;
    for (const auto& a : artboards) right = std::max(right, a.bounds.right);
  }
  editor_->AddArtboard(
      leinwand::core::Rect::FromXYWH(right + 20, top, size.width(), size.height()));
  Changed();
}

void Session::removeArtboard() {
  editor_->RemoveActiveArtboard();
  Changed();
}

void Session::moveArtboard(int from, int to) {
  editor_->MoveArtboard(from, to);
  Changed();
}

void Session::renameArtboard(int index, const QString& name) {
  editor_->RenameArtboard(index, name.trimmed().toStdString());
  Changed();
}

void Session::setArtboardBounds(int index, double x, double y, double width, double height) {
  editor_->SetArtboardBounds(index, leinwand::core::Rect::FromXYWH(x, y, width, height));
  Changed();
}

void Session::setAlignTo(int to) {
  if (to < 0 || to > 2 || to == alignTo()) return;
  editor_->SetAlignTo(static_cast<leinwand::editor::AlignTo>(to));
  emit documentChanged();
}

void Session::align(int edge) {
  if (edge < 0 || edge > 5) return;
  editor_->AlignSelection(static_cast<leinwand::editor::AlignEdge>(edge));
  Changed();
}

void Session::distribute(int edge) {
  if (edge < 0 || edge > 5) return;
  editor_->DistributeSelection(static_cast<leinwand::editor::AlignEdge>(edge));
  Changed();
}

void Session::distributeSpacing(bool horizontal, double spacing) {
  editor_->DistributeSpacing(horizontal,
                             std::isnan(spacing) ? std::nullopt : std::optional<double>(spacing));
  Changed();
}

void Session::average(int axis) {
  editor_->AverageAnchors(axis != 1, axis != 0);
  Changed();
}

void Session::pathfinder(int operation) {
  using leinwand::editor::Editor;
  if (operation < 0 || operation > static_cast<int>(leinwand::geometry::Pathfinder::kMinusBack)) {
    return;
  }
  const auto outcome =
      editor_->ApplyPathfinder(static_cast<leinwand::geometry::Pathfinder>(operation));
  if (outcome == Editor::PathfinderOutcome::kFailed) {
    Fail(tr("The path operation could not be completed. Nothing was changed."));
    return;
  }
  Changed();
}

void Session::arrange(int how) {
  using leinwand::core::Arrange;
  static constexpr Arrange kOrder[] = {Arrange::kBringToFront, Arrange::kBringForward,
                                       Arrange::kSendBackward, Arrange::kSendToBack};
  if (how < 0 || how > 3) return;
  editor_->Arrange(kOrder[how]);
  Changed();
}

void Session::nudge(double dx, double dy) {
  editor_->Nudge(dx, dy);
  Changed();
}

void Session::convertAnchors(bool smooth) {
  editor_->ConvertSelectedAnchors(smooth);
  Changed();
}

QVariantMap Session::selectionInfo() const {
  QVariantMap map;
  const auto info = editor_->Info();
  map["valid"] = info.has_value();
  if (!info) return map;
  map["x"] = info->bounds.left;
  map["y"] = info->bounds.top;
  map["width"] = info->bounds.width();
  map["height"] = info->bounds.height();
  map["rotation"] = info->rotation;
  // What a single selected object is, for the control bar.
  if (editor_->selection().size() == 1) {
    using namespace leinwand::core;
    if (const Object* o = editor_->document().FindObject(*editor_->selection().begin())) {
      map["kind"] = std::holds_alternative<TextObject>(*o) ? QStringLiteral("text")
                    : std::holds_alternative<CompoundPathObject>(*o)
                        ? QStringLiteral("compoundPath")
                    : std::holds_alternative<GroupObject>(*o)
                        ? (std::get<GroupObject>(*o).clipped ? QStringLiteral("clipGroup")
                                                             : QStringLiteral("group"))
                        : QStringLiteral("path");
    }
  }
  if (!info->shape) {
    map["shape"] = QString();
    return map;
  }
  std::visit(
      [&](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        using namespace leinwand::core;
        if constexpr (std::is_same_v<T, RectangleShape>) {
          map["shape"] = QStringLiteral("rectangle");
          map["shapeWidth"] = s.width;
          map["shapeHeight"] = s.height;
          map["cornerRadius"] = s.corners[0].radius;
          map["cornerKind"] = static_cast<int>(s.corners[0].kind);
        } else if constexpr (std::is_same_v<T, EllipseShape>) {
          map["shape"] = QStringLiteral("ellipse");
          map["shapeWidth"] = s.width;
          map["shapeHeight"] = s.height;
          map["pieStart"] = s.pie_start;
          map["pieEnd"] = s.pie_end;
        } else if constexpr (std::is_same_v<T, PolygonShape>) {
          map["shape"] = QStringLiteral("polygon");
          map["sides"] = s.sides;
          map["radius"] = s.radius;
          map["polygonCornerRadius"] = s.corner_radius;
        } else if constexpr (std::is_same_v<T, StarShape>) {
          map["shape"] = QStringLiteral("star");
          map["points"] = s.points;
          map["outerRadius"] = s.outer_radius;
          map["innerRadius"] = s.inner_radius;
        } else {
          map["shape"] = QStringLiteral("line");
          map["length"] = s.length;
        }
      },
      *info->shape);
  return map;
}

void Session::setBounds(double x, double y, double width, double height) {
  if (width <= 0 || height <= 0) return;
  editor_->SetBounds(leinwand::core::Rect::FromXYWH(x, y, width, height));
  Changed();
}

void Session::setRotation(double degrees) {
  editor_->SetRotation(degrees);
  Changed();
}

void Session::setShapeValue(const QString& key, double value) {
  const auto info = editor_->Info();
  if (!info || !info->shape) return;
  leinwand::core::ShapeParams params = *info->shape;
  const double size = std::max(value, 0.0);
  const int count = std::clamp(static_cast<int>(std::lround(value)), 3, 1000);
  std::visit(
      [&](auto& s) {
        using T = std::decay_t<decltype(s)>;
        using namespace leinwand::core;
        if constexpr (std::is_same_v<T, RectangleShape>) {
          if (key == "width") s.width = size;
          if (key == "height") s.height = size;
          for (auto& corner : s.corners) {
            if (key == "cornerRadius") corner.radius = size;
            if (key == "cornerKind") {
              corner.kind = static_cast<CornerKind>(std::clamp(static_cast<int>(value), 0, 2));
            }
          }
        } else if constexpr (std::is_same_v<T, EllipseShape>) {
          if (key == "width") s.width = size;
          if (key == "height") s.height = size;
          if (key == "pieStart") s.pie_start = value;
          if (key == "pieEnd") s.pie_end = value;
        } else if constexpr (std::is_same_v<T, PolygonShape>) {
          if (key == "sides") s.sides = count;
          if (key == "radius") s.radius = size;
          if (key == "polygonCornerRadius") s.corner_radius = size;
        } else if constexpr (std::is_same_v<T, StarShape>) {
          if (key == "points") s.points = count;
          if (key == "outerRadius") s.outer_radius = size;
          if (key == "innerRadius") s.inner_radius = size;
        } else {
          if (key == "length") s.length = size;
        }
      },
      params);
  editor_->SetShape(params);
  Changed();
}

QVariantMap Session::style() const {
  const auto state = editor_->Style();
  const auto& document = editor_->document();
  QVariantMap map;
  map["fillNone"] = !state.fill.has_value();
  map["fillMixed"] = state.fill_mixed;
  map["fill"] = ToQColor(state.fill, document);
  map["strokeNone"] = !state.stroke.has_value();
  map["strokeMixed"] = state.stroke_mixed;
  map["stroke"] = ToQColor(state.stroke, document);
  map["hasStroke"] = state.stroke_style.has_value();
  const leinwand::core::Stroke stroke = state.stroke_style.value_or(leinwand::core::Stroke{});
  map["strokeWidth"] = state.stroke_style ? stroke.width : 0.0;
  map["cap"] = static_cast<int>(stroke.cap);
  map["join"] = static_cast<int>(stroke.join);
  map["miterLimit"] = stroke.miter_limit;
  map["align"] = static_cast<int>(stroke.align);
  map["dashed"] = !stroke.dashes.empty();
  QVariantList dashes;
  for (double d : stroke.dashes) dashes.append(d);
  map["dashes"] = dashes;
  map["opacity"] = state.opacity;
  map["opacityMixed"] = state.opacity_mixed;
  if (state.gradient) {
    const leinwand::core::Gradient& g = *state.gradient;
    QVariantMap gradient;
    gradient["type"] = static_cast<int>(g.type);
    gradient["angle"] = leinwand::core::GradientAngle(g);
    gradient["aspect"] = g.aspect;
    QVariantList stops;
    for (const auto& stop : g.stops) {
      QVariantMap s;
      s["offset"] = stop.offset;
      s["color"] = ToQColor(stop.color, document);
      s["opacity"] = stop.opacity;
      s["midpoint"] = stop.midpoint;
      stops.append(s);
    }
    gradient["stops"] = stops;
    map["gradient"] = gradient;
    const int selected = editor_->gradient_stop();
    map["gradientStop"] = selected < static_cast<int>(g.stops.size()) ? selected : -1;
  } else {
    map["gradientStop"] = -1;
  }
  return map;
}

QVariantList Session::preflight(double min_stroke_mm, const QVariantList& checks) const {
  leinwand::editor::PreflightSettings settings;
  settings.min_stroke = std::max(min_stroke_mm, 0.0) * 72.0 / 25.4;
  for (int i = 0; i < leinwand::editor::kPreflightCheckCount && i < checks.size(); ++i) {
    settings.enabled[i] = checks[i].toBool();
  }
  QVariantList rows;
  for (const auto& issue : leinwand::editor::Preflight(editor_->document(), settings)) {
    QVariantList ids;
    for (const auto& id : issue.ids) ids.append(QString::fromStdString(id));
    rows.append(QVariantMap{{"check", static_cast<int>(issue.check)}, {"ids", ids}});
  }
  return rows;
}

void Session::createTrimMarks() {
  const bool japanese = Preferences::instance()->Flag(QStringLiteral("japaneseTrimMarks"));
  editor_->CreateTrimMarks(japanese ? leinwand::core::TrimMarkStyle::kJapanese
                                    : leinwand::core::TrimMarkStyle::kWestern);
  Changed();
}

QVariantMap Session::characterStyle() const {
  const auto state = editor_->TextStyle();
  const auto& styles = state.characters;
  const leinwand::core::CharacterStyle& first = styles.front();
  auto mixed = [&](auto field) {
    return std::any_of(styles.begin(), styles.end(),
                       [&](const auto& s) { return field(s) != field(first); });
  };
  QVariantMap map;
  map["editing"] = state.editing;
  map["selectedText"] = state.selected_text;
  map["family"] = QString::fromStdString(first.font.family);
  map["style"] = QString::fromStdString(first.font.style);
  map["familyMixed"] = mixed([](const auto& s) { return s.font.family; });
  map["styleMixed"] = mixed([](const auto& s) { return s.font.style; });
  map["missing"] = !leinwand::text::IsAvailable(first.font);
  map["size"] = first.size;
  map["sizeMixed"] = mixed([](const auto& s) { return s.size; });
  map["autoLeading"] = !first.leading.has_value();
  map["leading"] = leinwand::core::LeadingOf(first);
  map["leadingMixed"] = mixed([](const auto& s) { return leinwand::core::LeadingOf(s); });
  map["tracking"] = first.tracking;
  map["trackingMixed"] = mixed([](const auto& s) { return s.tracking; });
  map["baselineShift"] = first.baseline_shift;
  map["baselineShiftMixed"] = mixed([](const auto& s) { return s.baseline_shift; });
  map["horizontalScale"] = first.horizontal_scale;
  map["horizontalScaleMixed"] = mixed([](const auto& s) { return s.horizontal_scale; });
  map["verticalScale"] = first.vertical_scale;
  map["verticalScaleMixed"] = mixed([](const auto& s) { return s.vertical_scale; });
  map["rotation"] = first.rotation;
  map["rotationMixed"] = mixed([](const auto& s) { return s.rotation; });
  map["kerning"] = static_cast<int>(first.kerning);
  map["kerningMixed"] = mixed([](const auto& s) { return s.kerning; });
  return map;
}

QVariantMap Session::paragraphStyle() const {
  const auto state = editor_->TextStyle();
  const auto& styles = state.paragraphs;
  const leinwand::core::ParagraphStyle& first = styles.front();
  auto mixed = [&](auto field) {
    return std::any_of(styles.begin(), styles.end(),
                       [&](const auto& s) { return field(s) != field(first); });
  };
  QVariantMap map;
  map["align"] = static_cast<int>(first.align);
  map["alignMixed"] = mixed([](const auto& s) { return s.align; });
  map["leftIndent"] = first.left_indent;
  map["leftIndentMixed"] = mixed([](const auto& s) { return s.left_indent; });
  map["rightIndent"] = first.right_indent;
  map["rightIndentMixed"] = mixed([](const auto& s) { return s.right_indent; });
  map["firstLineIndent"] = first.first_line_indent;
  map["firstLineIndentMixed"] = mixed([](const auto& s) { return s.first_line_indent; });
  map["spaceBefore"] = first.space_before;
  map["spaceBeforeMixed"] = mixed([](const auto& s) { return s.space_before; });
  map["spaceAfter"] = first.space_after;
  map["spaceAfterMixed"] = mixed([](const auto& s) { return s.space_after; });
  return map;
}

QStringList Session::fontFamilies() const {
  static const QStringList families = [] {
    QStringList list;
    for (const auto& family : leinwand::text::Families()) {
      list.append(QString::fromStdString(family.name));
    }
    return list;
  }();
  return families;
}

QStringList Session::fontStyles(const QString& family) const {
  const std::string name = family.toStdString();
  for (const auto& f : leinwand::text::Families()) {
    if (f.name != name) continue;
    QStringList styles;
    for (const auto& style : f.styles) styles.append(QString::fromStdString(style));
    return styles;
  }
  return {};
}

void Session::setFont(const QString& family, const QString& style) {
  QString chosen = style;
  const QStringList styles = fontStyles(family);
  if (!styles.isEmpty() && !styles.contains(chosen)) {
    // Keep the style when the new family has it; else its regular one.
    chosen =
        styles.contains(QStringLiteral("Regular")) ? QStringLiteral("Regular") : styles.front();
  }
  const leinwand::core::FontRef font{family.toStdString(), chosen.toStdString(), {}};
  editor_->EditCharacterStyle([&](leinwand::core::CharacterStyle& s) { s.font = font; });
  Changed();
}

void Session::setCharacterValue(const QString& key, double value) {
  using leinwand::core::CharacterStyle;
  editor_->EditCharacterStyle([&](CharacterStyle& s) {
    if (key == "size") s.size = std::clamp(value, 0.1, 1296.0);
    if (key == "leading") {
      if (value <= 0) {
        s.leading.reset();
      } else {
        s.leading = std::min(value, 5000.0);
      }
    }
    if (key == "tracking") s.tracking = std::clamp(value, -1000.0, 10000.0);
    if (key == "baselineShift") s.baseline_shift = value;
    if (key == "horizontalScale") s.horizontal_scale = std::clamp(value, 0.01, 100.0);
    if (key == "verticalScale") s.vertical_scale = std::clamp(value, 0.01, 100.0);
    if (key == "rotation") s.rotation = value;
    if (key == "kerning") {
      s.kerning =
          value >= 1 ? leinwand::core::KerningMode::kNone : leinwand::core::KerningMode::kMetrics;
    }
  });
  Changed();
}

void Session::setParagraphValue(const QString& key, double value) {
  using leinwand::core::ParagraphStyle;
  editor_->EditParagraphStyle([&](ParagraphStyle& p) {
    if (key == "align") {
      p.align = static_cast<leinwand::core::TextAlign>(std::clamp(static_cast<int>(value), 0, 2));
    }
    if (key == "leftIndent") p.left_indent = value;
    if (key == "rightIndent") p.right_indent = value;
    if (key == "firstLineIndent") p.first_line_indent = value;
    if (key == "spaceBefore") p.space_before = value;
    if (key == "spaceAfter") p.space_after = value;
  });
  Changed();
}

QVariantMap Session::transparency() const {
  const auto state = editor_->Transparency();
  QVariantMap map;
  map["selected"] = state.selected;
  map["opacity"] = state.opacity;
  map["opacityMixed"] = state.opacity_mixed;
  map["blendMode"] = static_cast<int>(state.blend_mode);
  map["blendMixed"] = state.blend_mixed;
  map["hasMask"] = state.mask.has_value();
  map["maskClip"] = state.mask ? state.mask->clip : true;
  map["maskInvert"] = state.mask ? state.mask->invert : false;
  map["hasGroup"] = state.has_group;
  map["isolated"] = state.isolated;
  return map;
}

void Session::setBlendMode(int mode) {
  if (mode < 0 || mode > static_cast<int>(leinwand::core::BlendMode::kLuminosity)) return;
  editor_->SetBlendMode(static_cast<leinwand::core::BlendMode>(mode));
  Changed();
}

void Session::setIsolated(bool isolated) {
  editor_->SetIsolated(isolated);
  Changed();
}

void Session::setMaskClip(bool clip) {
  editor_->SetMaskClip(clip);
  Changed();
}

void Session::setMaskInvert(bool invert) {
  editor_->SetMaskInvert(invert);
  Changed();
}

void Session::applyGradient(int type) {
  editor_->ApplyGradient(type == 1 ? leinwand::core::GradientType::kRadial
                                   : leinwand::core::GradientType::kLinear);
  Changed();
}

void Session::setGradientAngle(double degrees) {
  editor_->SetGradientAngle(degrees);
  Changed();
}

void Session::setGradientAspect(double aspect) {
  editor_->SetGradientAspect(aspect);
  Changed();
}

void Session::selectGradientStop(int index) {
  editor_->SelectGradientStop(index);
  Changed();
}

int Session::addGradientStop(double offset) {
  const int added = editor_->AddGradientStop(offset);
  Changed();
  return added;
}

void Session::removeGradientStop(int index) {
  editor_->RemoveGradientStop(index);
  Changed();
}

int Session::moveGradientStop(int index, double offset) {
  const int moved = editor_->MoveGradientStop(index, offset);
  Changed();
  return moved;
}

void Session::setGradientStopOpacity(int index, double opacity) {
  editor_->SetGradientStopOpacity(index, opacity);
  Changed();
}

void Session::setGradientStopMidpoint(int index, double midpoint) {
  editor_->SetGradientStopMidpoint(index, midpoint);
  Changed();
}

void Session::setFillColor(const QColor& color) {
  editor_->SetFill(Color{ToRgb(color)});
  Changed();
}

void Session::setStrokeColor(const QColor& color) {
  editor_->SetStroke(Color{ToRgb(color)});
  Changed();
}

void Session::setActiveColor(const QColor& color) {
  fillActive() ? setFillColor(color) : setStrokeColor(color);
}

void Session::setActiveNone() { fillActive() ? setFillNone() : setStrokeNone(); }

void Session::setFillNone() {
  editor_->SetFill(std::nullopt);
  Changed();
}

void Session::setStrokeNone() {
  editor_->SetStroke(std::nullopt);
  Changed();
}

void Session::applySwatch(const QString& id) {
  const auto* swatch = editor_->document().FindSwatch(id.toStdString());
  if (!swatch) return;
  // A spot color is applied as itself; a process swatch as its color.
  const Color paint = swatch->kind == leinwand::core::Swatch::Kind::kSpot
                          ? Color{leinwand::core::SpotColor{swatch->id, 1.0}}
                          : std::visit([](const auto& c) -> Color { return c; }, swatch->color);
  fillActive() ? editor_->SetFill(paint) : editor_->SetStroke(paint);
  Changed();
}

void Session::setStrokeValue(const QString& key, double value) {
  using namespace leinwand::core;
  editor_->EditStroke([&](Stroke& s) {
    if (key == "width") s.width = std::max(value, 0.0);
    if (key == "cap") s.cap = static_cast<StrokeCap>(std::clamp(static_cast<int>(value), 0, 2));
    if (key == "join") s.join = static_cast<StrokeJoin>(std::clamp(static_cast<int>(value), 0, 2));
    if (key == "miterLimit") s.miter_limit = std::clamp(value, 1.0, 500.0);
    if (key == "align") {
      s.align = static_cast<StrokeAlign>(std::clamp(static_cast<int>(value), 0, 2));
    }
  });
  Changed();
}

void Session::setDashes(const QVariantList& dashes) {
  std::vector<double> values;
  for (const QVariant& d : dashes) {
    const double v = d.toDouble();
    if (v >= 0) values.push_back(v);
  }
  // All zeros would draw nothing; treat as solid.
  if (std::all_of(values.begin(), values.end(), [](double v) { return v == 0; })) values.clear();
  editor_->EditStroke([&](leinwand::core::Stroke& s) { s.dashes = values; });
  Changed();
}

void Session::setOpacity(double opacity) {
  editor_->SetOpacity(opacity);
  Changed();
}

void Session::beginGesture() { editor_->BeginGesture(); }

void Session::endGesture() { editor_->EndGesture(); }

QVariantList Session::swatches() const {
  QVariantList list;
  for (const auto& swatch : editor_->document().swatches) {
    const auto rgb = leinwand::core::ToRgb(swatch.color);
    QVariantMap map;
    map["id"] = QString::fromStdString(swatch.id);
    map["name"] = QString::fromStdString(swatch.name);
    map["color"] = QColor::fromRgbF(static_cast<float>(rgb.r), static_cast<float>(rgb.g),
                                    static_cast<float>(rgb.b));
    map["spot"] = swatch.kind == leinwand::core::Swatch::Kind::kSpot;
    list.append(map);
  }
  return list;
}

void Session::addSwatch(const QString& name) {
  const auto state = editor_->Style();
  const auto& paint = editor_->fill_active() ? state.fill : state.stroke;
  if (!paint) return;
  const auto rgb = leinwand::core::ToRgb(*paint, editor_->document());
  if (!rgb) return;
  leinwand::core::Swatch swatch;
  swatch.name = name.toStdString();
  swatch.color = *rgb;
  editor_->AddSwatch(swatch);
  Changed();
}

void Session::removeSwatch(const QString& id) {
  editor_->RemoveSwatch(id.toStdString());
  Changed();
}

double Session::evaluateLength(const QString& text) const {
  return leinwand::editor::EvaluateLength(text.toStdString())
      .value_or(std::numeric_limits<double>::quiet_NaN());
}

double Session::evaluateNumber(const QString& text) const {
  return leinwand::editor::EvaluateNumber(text.toStdString())
      .value_or(std::numeric_limits<double>::quiet_NaN());
}
