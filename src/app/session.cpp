// SPDX-License-Identifier: GPL-3.0-or-later
#include "session.h"

#include <QQmlEngine>
#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

#include "core/style.h"
#include "editor/number_input.h"
#include "layers_model.h"
#include "render/test_document.h"

namespace {

using leinwand::core::Color;
using leinwand::core::RgbColor;
using leinwand::editor::Tool;

constexpr int kHand = 12;
constexpr int kZoom = 13;

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

}  // namespace

Session::Session(QObject* parent) : QObject(parent), layers_(std::make_unique<LayersModel>(this)) {
  g_instance = this;
  loadShowcase();
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

void Session::SetDocument(leinwand::core::Document document) {
  const bool guides = editor_ ? editor_->smart_guides() : true;
  editor_ = std::make_unique<leinwand::editor::Editor>(std::move(document));
  editor_->SetSmartGuides(guides);
  view_tool_ = -1;
  Changed();
  emit toolChanged();
  emit documentReplaced();
}

void Session::Changed() {
  object_count_ = CountObjects(editor_->document());
  layers_->Refresh();
  emit documentChanged();
}

void Session::loadShowcase() { SetDocument(leinwand::render::MakeShowcaseDocument()); }

void Session::loadTestDocument(int pathCount) {
  SetDocument(leinwand::render::MakeTestDocument(pathCount));
}

QString Session::undoAction() const {
  return QString::fromStdString(editor_->history().undo_action());
}

QString Session::redoAction() const {
  return QString::fromStdString(editor_->history().redo_action());
}

int Session::tool() const {
  return view_tool_ >= 0 ? view_tool_ : static_cast<int>(editor_->tool());
}

void Session::setTool(int tool) {
  if (tool == kHand || tool == kZoom) {
    if (view_tool_ == tool) return;
    editor_->FinishPath();
    view_tool_ = tool;
  } else {
    if (tool < 0 || tool > static_cast<int>(Tool::kEyedropper)) return;
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
LEINWAND_COMMAND(swapFillAndStroke, SwapFillAndStroke)
LEINWAND_COMMAND(defaultFillAndStroke, DefaultFillAndStroke)
#undef LEINWAND_COMMAND

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
  return map;
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
