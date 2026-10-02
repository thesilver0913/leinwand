// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_item.h"

#include <rhi/qrhi.h>

#include <QElapsedTimer>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QQuickWindow>
#include <QVulkanInstance>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "render/document_renderer.h"
#include "render/overlay.h"
#include "render/test_document.h"
#include "render/vulkan_canvas.h"

namespace {

using leinwand::render::View;
using leinwand::render::VulkanCanvas;

class CanvasRenderer : public QQuickRhiItemRenderer {
 public:
  void initialize(QRhiCommandBuffer*) override {
    if (canvas_ || failed_) return;  // Also called on every resize.
    std::string error;
    canvas_ = CreateCanvas(&error);
    if (!canvas_) {
      failed_ = true;
      error_ = QString::fromStdString(error);
    }
  }

  void synchronize(QQuickRhiItem* rhi_item) override {
    auto* item = static_cast<CanvasItem*>(rhi_item);
    // A snapshot: copying the document shares all of its (immutable) nodes.
    document_ = item->document();
    const double dpr = item->window()->effectiveDevicePixelRatio();
    const View& view = item->view();
    view_ = {view.pan_x * dpr, view.pan_y * dpr, view.zoom * dpr};
    overlay_ = item->overlay(dpr);

    if (!error_.isEmpty()) {
      QMetaObject::invokeMethod(item, "reportError", Qt::QueuedConnection, Q_ARG(QString, error_));
      error_.clear();
    }
    if (frames_ >= 30) {
      const double seconds = interval_.nsecsElapsed() / 1e9;
      QMetaObject::invokeMethod(item, "reportStats", Qt::QueuedConnection,
                                Q_ARG(double, frames_ / seconds),
                                Q_ARG(double, draw_ns_ / 1e6 / frames_));
      frames_ = 0;
      draw_ns_ = 0;
      interval_.restart();
    }
  }

  void render(QRhiCommandBuffer*) override {
    if (!canvas_) return;
    QRhiTexture* texture = colorTexture();
    const QRhiTexture::NativeTexture native = texture->nativeTexture();
    leinwand::render::VulkanTarget target;
    target.image = reinterpret_cast<VkImage>(native.object);
    target.layout = static_cast<VkImageLayout>(native.layout);
    target.format = VK_FORMAT_R8G8B8A8_UNORM;  // QQuickRhiItem's default RGBA8.
    target.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    target.width = texture->pixelSize().width();
    target.height = texture->pixelSize().height();

    QElapsedTimer timer;
    timer.start();
    const VkImageLayout final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (canvas_->Draw(renderer_, document_, view_, overlay_, target, final_layout)) {
      // Skia changed the layout behind QRhi's back; tell it.
      texture->setNativeLayout(final_layout);
    } else {
      error_ = QStringLiteral("Skia could not wrap the item's texture");
    }
    draw_ns_ += timer.nsecsElapsed();
    if (frames_++ == 0) interval_.start();
  }

 private:
  std::unique_ptr<VulkanCanvas> CreateCanvas(std::string* error) {
    if (rhi()->backend() != QRhi::Vulkan) {
      *error = "Qt Quick is not running on Vulkan";
      return nullptr;
    }
    const auto* handles = static_cast<const QRhiVulkanNativeHandles*>(rhi()->nativeHandles());
    QVulkanInstance* instance = handles->inst;
    const QVersionNumber version =
        instance->apiVersion().isNull() ? instance->supportedApiVersion() : instance->apiVersion();

    leinwand::render::VulkanDevice device;
    device.instance = instance->vkInstance();
    device.physical_device = handles->physDev;
    device.device = handles->dev;
    device.queue = handles->gfxQueue;
    device.queue_family_index = handles->gfxQueueFamilyIdx;
    device.api_version = VK_MAKE_API_VERSION(0, version.majorVersion(), version.minorVersion(), 0);
    device.get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        instance->getInstanceProcAddr("vkGetInstanceProcAddr"));
    return VulkanCanvas::Create(device, error);
  }

  std::unique_ptr<VulkanCanvas> canvas_;
  leinwand::render::DocumentRenderer renderer_;
  leinwand::core::Document document_;
  View view_;
  leinwand::render::Overlay overlay_;
  bool failed_ = false;
  QString error_;
  int frames_ = 0;
  qint64 draw_ns_ = 0;
  QElapsedTimer interval_;
};

int CountObjects(const leinwand::core::Document& document) {
  int count = 0;
  leinwand::core::VisitObjects(document, [&](const leinwand::core::Object&) { ++count; });
  return count;
}

}  // namespace

CanvasItem::CanvasItem(QQuickItem* parent) : QQuickRhiItem(parent) {
  setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
  setAcceptHoverEvents(true);
  setFlag(ItemIsFocusScope);
  setActiveFocusOnTab(true);
  loadShowcase();
}

CanvasItem::~CanvasItem() = default;

QQuickRhiItemRenderer* CanvasItem::createRenderer() { return new CanvasRenderer; }

leinwand::render::Overlay CanvasItem::overlay(double pixel_ratio) const {
  leinwand::editor::Overlay o = editor_->overlay();
  leinwand::render::Overlay overlay;
  overlay.selection = std::move(o.selection);
  overlay.bounding_box = o.bounding_box;
  overlay.marquee = o.marquee;
  overlay.pixel_ratio = pixel_ratio;
  for (auto& path : o.paths) {
    overlay.paths.push_back(
        {std::move(path.path), std::move(path.selected), std::move(path.with_handles)});
  }
  overlay.rubber_band = std::move(o.rubber_band);
  overlay.guides = std::move(o.guides);
  overlay.outline = outline_view_;
  return overlay;
}

QString CanvasItem::undoAction() const {
  return QString::fromStdString(editor_->history().undo_action());
}

QString CanvasItem::redoAction() const {
  return QString::fromStdString(editor_->history().redo_action());
}

void CanvasItem::SetDocument(leinwand::core::Document document) {
  editor_ = std::make_unique<leinwand::editor::Editor>(std::move(document));
  editor_->SetSmartGuides(smart_guides_);
  object_count_ = CountObjects(editor_->document());
  fit_pending_ = true;
  if (width() > 0 && height() > 0) fitArtboard();
  emit documentChanged();
  update();
}

void CanvasItem::EditorChanged() {
  object_count_ = CountObjects(editor_->document());
  emit documentChanged();
  update();
}

void CanvasItem::loadShowcase() { SetDocument(leinwand::render::MakeShowcaseDocument()); }

void CanvasItem::loadTestDocument(int pathCount) {
  SetDocument(leinwand::render::MakeTestDocument(pathCount));
}

void CanvasItem::SetView(const View& view, bool by_user) {
  if (by_user) fit_pending_ = false;
  view_ = view;
  emit viewChanged();
  update();
}

void CanvasItem::setZoom(double zoom) {
  if (zoom != view_.zoom) SetView({view_.pan_x, view_.pan_y, zoom});
}

void CanvasItem::setPanX(double x) {
  if (x != view_.pan_x) SetView({x, view_.pan_y, view_.zoom});
}

void CanvasItem::setPanY(double y) {
  if (y != view_.pan_y) SetView({view_.pan_x, y, view_.zoom});
}

void CanvasItem::fitArtboard() {
  const auto& artboards = editor_->document().artboards;
  if (artboards.empty() || width() <= 0 || height() <= 0) return;
  SetView(View::Fit(artboards.front().bounds, width(), height()), /*by_user=*/false);
}

void CanvasItem::actualSize() {
  SetView(view_.ZoomedAt({width() / 2, height() / 2}, 1.0 / view_.zoom));
}

void CanvasItem::zoomIn() {
  SetView(view_.ZoomedAt({width() / 2, height() / 2}, View::NextZoomIn(view_.zoom) / view_.zoom));
}

void CanvasItem::zoomOut() {
  SetView(view_.ZoomedAt({width() / 2, height() / 2}, View::NextZoomOut(view_.zoom) / view_.zoom));
}

void CanvasItem::undo() {
  editor_->Undo();
  EditorChanged();
}

void CanvasItem::redo() {
  editor_->Redo();
  EditorChanged();
}

void CanvasItem::selectAll() {
  editor_->SelectAll();
  EditorChanged();
}

void CanvasItem::deselect() {
  editor_->Deselect();
  EditorChanged();
}

void CanvasItem::deleteSelection() {
  editor_->Delete();
  EditorChanged();
}

void CanvasItem::group() {
  editor_->Group();
  EditorChanged();
}

void CanvasItem::ungroup() {
  editor_->Ungroup();
  EditorChanged();
}

void CanvasItem::arrange(int how) {
  using leinwand::core::Arrange;
  static constexpr Arrange kOrder[] = {Arrange::kBringToFront, Arrange::kBringForward,
                                       Arrange::kSendBackward, Arrange::kSendToBack};
  if (how < 0 || how > 3) return;
  editor_->Arrange(kOrder[how]);
  EditorChanged();
}

void CanvasItem::convertAnchors(bool smooth) {
  editor_->ConvertSelectedAnchors(smooth);
  EditorChanged();
}

void CanvasItem::removeAnchors() {
  editor_->RemoveSelectedAnchors();
  EditorChanged();
}

void CanvasItem::cutAtAnchor() {
  editor_->CutAtSelectedAnchor();
  EditorChanged();
}

void CanvasItem::joinEnds() {
  editor_->JoinSelectedEnds();
  EditorChanged();
}

void CanvasItem::setSmartGuides(bool on) {
  if (on == smart_guides_) return;
  smart_guides_ = on;
  editor_->SetSmartGuides(on);
  emit viewChanged();
}

void CanvasItem::setOutlineView(bool outline) {
  if (outline == outline_view_) return;
  outline_view_ = outline;
  emit viewChanged();
  update();
}

void CanvasItem::setTool(int tool) {
  using leinwand::editor::Tool;
  if (tool < 0 || tool > static_cast<int>(Tool::kDirectSelection)) return;
  if (tool == static_cast<int>(editor_->chosen_tool()) && tool == this->tool()) return;
  editor_->SetTool(static_cast<leinwand::editor::Tool>(tool));
  tool_dragging_ = false;
  emit toolChanged();
  EditorChanged();
}

QVariantMap CanvasItem::selectionInfo() const {
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

void CanvasItem::setBounds(double x, double y, double width, double height) {
  if (width <= 0 || height <= 0) return;
  editor_->SetBounds(leinwand::core::Rect::FromXYWH(x, y, width, height));
  EditorChanged();
}

void CanvasItem::setRotation(double degrees) {
  editor_->SetRotation(degrees);
  EditorChanged();
}

void CanvasItem::setShapeValue(const QString& key, double value) {
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
  EditorChanged();
}

void CanvasItem::reportStats(double fps, double drawMs) {
  fps_ = fps;
  draw_ms_ = drawMs;
  emit statsChanged();
}

void CanvasItem::reportError(const QString& error) {
  qWarning("Canvas: %s", qPrintable(error));
  error_ = error;
  emit errorChanged();
}

leinwand::core::Point CanvasItem::ToDocument(QPointF position) const {
  return view_.ToDocument({position.x(), position.y()});
}

double CanvasItem::PickRadius() const { return 4.0 / view_.zoom; }

leinwand::editor::Modifiers CanvasItem::ToolModifiers() const {
  return {.shift = modifiers_.testFlag(Qt::ShiftModifier),
          .alt = modifiers_.testFlag(Qt::AltModifier),
          .space = space_held_ && tool_dragging_};
}

void CanvasItem::UpdateTemporaryTool(Qt::KeyboardModifiers modifiers) {
  using leinwand::editor::Tool;
  if (tool_dragging_) return;  // Keys held during a drag change the drag instead.
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
  update();
}

void CanvasItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
  QQuickRhiItem::geometryChange(newGeometry, oldGeometry);
  if (fit_pending_ && newGeometry.width() > 0 && newGeometry.height() > 0) fitArtboard();
}

void CanvasItem::wheelEvent(QWheelEvent* event) {
  // Touchpads report pixels; mouse wheels report angles (120 per notch).
  const QPointF pixels = event->pixelDelta().isNull() ? QPointF(event->angleDelta()) / 120.0 * 40.0
                                                      : QPointF(event->pixelDelta());
  if (event->modifiers() & Qt::AltModifier) {
    // Some systems turn Alt+wheel into a horizontal wheel; accept either axis.
    const double steps = (event->angleDelta().y() + event->angleDelta().x()) / 120.0;
    SetView(view_.ZoomedAt({event->position().x(), event->position().y()}, std::pow(1.25, steps)));
  } else if (event->modifiers() & Qt::ControlModifier) {
    SetView({view_.pan_x + pixels.y() + pixels.x(), view_.pan_y, view_.zoom});
  } else {
    SetView({view_.pan_x + pixels.x(), view_.pan_y + pixels.y(), view_.zoom});
  }
  event->accept();
}

void CanvasItem::keyPressEvent(QKeyEvent* event) {
  using leinwand::editor::Tool;
  modifiers_ = event->modifiers();
  UpdateTemporaryTool(modifiers_);
  if (event->isAutoRepeat() && event->key() == Qt::Key_Space) return;
  if (event->key() == Qt::Key_Space) space_held_ = true;
  if (tool_dragging_ && (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt ||
                         event->key() == Qt::Key_Space)) {
    // Modifiers change the drag (constrain, copy, move the anchor) even when
    // the mouse is still.
    editor_->PointerMove(ToDocument(last_mouse_), ToolModifiers());
    EditorChanged();
  }
  if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Alt) UpdateCursor(last_mouse_);
  // Up and down while drawing a polygon or star change its sides or points.
  const bool counting = editor_->tool() == Tool::kPolygon || editor_->tool() == Tool::kStar;
  if (tool_dragging_ && counting && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
    editor_->AdjustToolCount(event->key() == Qt::Key_Up ? 1 : -1);
    EditorChanged();
    event->accept();
    return;
  }
  // Arrow keys nudge by the keyboard increment (1 pt; Shift: 10 pt).
  const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
  switch (event->key()) {
    case Qt::Key_Space:
      UpdateCursor(last_mouse_);  // Temporary hand tool.
      break;
    case Qt::Key_Left:
      editor_->Nudge(-step, 0);
      EditorChanged();
      break;
    case Qt::Key_Right:
      editor_->Nudge(step, 0);
      EditorChanged();
      break;
    case Qt::Key_Up:
      editor_->Nudge(0, -step);
      EditorChanged();
      break;
    case Qt::Key_Down:
      editor_->Nudge(0, step);
      EditorChanged();
      break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
      deleteSelection();
      break;
    case Qt::Key_Escape:
      if (tool_dragging_) {
        editor_->CancelDrag();
        tool_dragging_ = false;
      } else {
        editor_->FinishPath();  // The pen leaves the path open (spec 4.2).
      }
      EditorChanged();
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      editor_->FinishPath();
      EditorChanged();
      break;
    default:
      QQuickRhiItem::keyPressEvent(event);
      return;
  }
  event->accept();
}

void CanvasItem::keyReleaseEvent(QKeyEvent* event) {
  modifiers_ = event->modifiers();
  UpdateTemporaryTool(modifiers_);
  const bool space = event->key() == Qt::Key_Space && !event->isAutoRepeat();
  if (space) space_held_ = false;
  if (tool_dragging_ && (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt || space)) {
    editor_->PointerMove(ToDocument(last_mouse_), ToolModifiers());
    EditorChanged();
  }
  if (space) {
    UpdateCursor(last_mouse_);
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Alt) UpdateCursor(last_mouse_);
  QQuickRhiItem::keyReleaseEvent(event);
}

void CanvasItem::mousePressEvent(QMouseEvent* event) {
  forceActiveFocus();
  last_mouse_ = event->position();
  modifiers_ = event->modifiers();
  if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && space_held_)) {
    panning_ = true;
  } else if (event->button() == Qt::LeftButton) {
    UpdateTemporaryTool(modifiers_);
    tool_dragging_ = true;
    editor_->PointerDown(ToDocument(event->position()), ToolModifiers(), PickRadius());
    EditorChanged();
  }
  UpdateCursor(event->position());
  event->accept();
}

void CanvasItem::mouseMoveEvent(QMouseEvent* event) {
  const QPointF delta = event->position() - last_mouse_;
  last_mouse_ = event->position();
  modifiers_ = event->modifiers();
  if (panning_) {
    SetView({view_.pan_x + delta.x(), view_.pan_y + delta.y(), view_.zoom});
  } else if (tool_dragging_) {
    editor_->PointerMove(ToDocument(event->position()), ToolModifiers());
    EditorChanged();
  }
}

void CanvasItem::mouseReleaseEvent(QMouseEvent* event) {
  modifiers_ = event->modifiers();
  if (tool_dragging_) {
    editor_->PointerUp(ToDocument(event->position()), ToolModifiers());
    tool_dragging_ = false;
    UpdateTemporaryTool(modifiers_);  // Keys may have changed during the drag.
    EditorChanged();
  }
  panning_ = false;
  UpdateCursor(event->position());
}

void CanvasItem::hoverMoveEvent(QHoverEvent* event) {
  // Qt Quick re-sends hover events every frame while the cursor rests on the
  // item; only a real move needs a new hit test.
  if (event->position() == last_mouse_ && event->modifiers() == modifiers_) return;
  last_mouse_ = event->position();
  modifiers_ = event->modifiers();
  UpdateTemporaryTool(modifiers_);
  if (editor_->drawing_path()) {
    editor_->PointerHover(ToDocument(event->position()));  // The rubber band.
    update();
  }
  UpdateCursor(event->position());
}

namespace {

// Interim pen cursors until the icon set arrives (M7): a crosshair with a
// mark for what a click would do (spec 4.2).
QCursor PenCursor(const QString& mark) {
  static QHash<QString, QCursor> cache;
  if (const auto it = cache.constFind(mark); it != cache.constEnd()) return *it;
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const std::pair<QColor, double> strokes[] = {{Qt::white, 3.0}, {Qt::black, 1.0}};
  for (const auto& [color, width] : strokes) {
    painter.setPen(QPen(color, width));
    painter.drawLine(QPointF(8, 1), QPointF(8, 15));
    painter.drawLine(QPointF(1, 8), QPointF(15, 8));
  }
  if (!mark.isEmpty()) {
    QFont font = painter.font();
    font.setPixelSize(13);
    font.setBold(true);
    QPainterPath text;
    text.addText(QPointF(15, 27), font, mark);
    painter.strokePath(text, QPen(Qt::white, 3));
    painter.fillPath(text, Qt::black);
  }
  painter.end();
  return *cache.insert(mark, QCursor(pixmap, 8, 8));
}

}  // namespace

void CanvasItem::UpdateCursor(QPointF position) {
  using leinwand::editor::PenAction;
  using leinwand::editor::Tool;
  if (panning_) {
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  if (tool_dragging_) return;  // Keep the cursor the drag started with.
  if (space_held_) {
    setCursor(Qt::OpenHandCursor);
    return;
  }
  switch (editor_->tool()) {
    case Tool::kSelection:
      break;
    case Tool::kDirectSelection:
      unsetCursor();
      return;
    case Tool::kPen:
      switch (editor_->PenActionAt(ToDocument(position), PickRadius())) {
        case PenAction::kNewPath:
          setCursor(PenCursor("*"));
          return;
        case PenAction::kNewAnchor:
          setCursor(PenCursor(""));
          return;
        case PenAction::kRemoveHandle:
          setCursor(PenCursor("^"));
          return;
        case PenAction::kClose:
          setCursor(PenCursor("o"));
          return;
        case PenAction::kJoin:
        case PenAction::kContinue:
          setCursor(PenCursor("/"));
          return;
        case PenAction::kAddAnchor:
          setCursor(PenCursor("+"));
          return;
        case PenAction::kDeleteAnchor:
          setCursor(PenCursor("-"));
          return;
      }
      return;
    case Tool::kAddAnchor:
      setCursor(PenCursor("+"));
      return;
    case Tool::kDeleteAnchor:
      setCursor(PenCursor("-"));
      return;
    case Tool::kConvertAnchor:
      setCursor(PenCursor("^"));
      return;
    default:
      setCursor(Qt::CrossCursor);
      return;
  }
  using leinwand::editor::Handle;
  using Kind = leinwand::editor::Hover::Kind;
  const auto hover = editor_->HoverAt(ToDocument(position), PickRadius());
  switch (hover.kind) {
    case Kind::kHandle:
      switch (hover.handle) {
        case Handle::kTopLeft:
        case Handle::kBottomRight:
          setCursor(Qt::SizeFDiagCursor);
          return;
        case Handle::kTopRight:
        case Handle::kBottomLeft:
          setCursor(Qt::SizeBDiagCursor);
          return;
        case Handle::kTop:
        case Handle::kBottom:
          setCursor(Qt::SizeVerCursor);
          return;
        case Handle::kLeft:
        case Handle::kRight:
          setCursor(Qt::SizeHorCursor);
          return;
      }
      return;
    case Kind::kRotate:
      // Qt has no rotate cursor; a custom one comes with the icon set (M7).
      setCursor(Qt::CrossCursor);
      return;
    case Kind::kObject:
    case Kind::kNothing:
      unsetCursor();
      return;
  }
}
