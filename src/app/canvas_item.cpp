// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_item.h"

#include <rhi/qrhi.h>

#include <QElapsedTimer>
#include <QFile>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QVulkanInstance>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "preferences.h"
#include "render/document_renderer.h"
#include "render/overlay.h"
#include "render/vulkan_canvas.h"
#include "session.h"

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
    const QColor canvas(Preferences::instance()->Text(QStringLiteral("canvasColor")));
    renderer_.SetPasteboard({canvas.redF(), canvas.greenF(), canvas.blueF()});

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

}  // namespace

CanvasItem::CanvasItem(QQuickItem* parent) : QQuickRhiItem(parent), session_(Session::instance()) {
  setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
  setAcceptHoverEvents(true);
  setFlag(ItemIsFocusScope);
  setActiveFocusOnTab(true);
  session_->SetCanvas(this);
  connect(session_, &Session::documentChanged, this, [this] { update(); });
  connect(Preferences::instance(), &Preferences::changed, this, [this] { update(); });
  connect(session_, &Session::toolChanged, this, [this] { UpdateCursor(last_mouse_); });
  connect(session_, &Session::documentReplaced, this, [this] {
    fit_pending_ = true;
    if (width() > 0 && height() > 0) fitArtboard();
  });
}

CanvasItem::~CanvasItem() {
  if (session_->canvas() == this) session_->SetCanvas(nullptr);
}

QQuickRhiItemRenderer* CanvasItem::createRenderer() { return new CanvasRenderer; }

leinwand::editor::Editor& CanvasItem::editor() const { return session_->editor(); }

const leinwand::core::Document& CanvasItem::document() const { return editor().document(); }

leinwand::render::Overlay CanvasItem::overlay(double pixel_ratio) const {
  leinwand::editor::Overlay o = editor().overlay();
  leinwand::render::Overlay overlay;
  overlay.selection = std::move(o.selection);
  overlay.bounding_box = o.bounding_box;
  overlay.marquee = o.marquee;
  overlay.pixel_ratio = pixel_ratio;
  overlay.anchor_size = Preferences::instance()->Number(QStringLiteral("anchorSize"));
  for (auto& path : o.paths) {
    overlay.paths.push_back(
        {std::move(path.path), std::move(path.selected), std::move(path.with_handles)});
  }
  overlay.rubber_band = std::move(o.rubber_band);
  overlay.guides = std::move(o.guides);
  overlay.outline = outline_view_;
  return overlay;
}

void CanvasItem::EditorChanged() { session_->Changed(); }

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

void CanvasItem::setOutlineView(bool outline) {
  if (outline == outline_view_) return;
  outline_view_ = outline;
  emit viewChanged();
  update();
}

void CanvasItem::fitArtboard() {
  const auto& artboards = document().artboards;
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

void CanvasItem::ZoomClick(QPointF position, bool out) {
  const double next = out ? View::NextZoomOut(view_.zoom) : View::NextZoomIn(view_.zoom);
  SetView(view_.ZoomedAt({position.x(), position.y()}, next / view_.zoom));
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

double CanvasItem::PickRadius() const {
  return Preferences::instance()->Number(QStringLiteral("pickTolerance")) / view_.zoom;
}

leinwand::editor::Modifiers CanvasItem::ToolModifiers() const {
  return {.shift = modifiers_.testFlag(Qt::ShiftModifier),
          .alt = modifiers_.testFlag(Qt::AltModifier),
          .space = space_held_ && tool_dragging_};
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
  if (!tool_dragging_) session_->UpdateTemporaryTool(modifiers_);
  if (event->isAutoRepeat() && event->key() == Qt::Key_Space) return;
  if (event->key() == Qt::Key_Space) space_held_ = true;
  if (tool_dragging_ && (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt ||
                         event->key() == Qt::Key_Space)) {
    // Modifiers change the drag (constrain, copy, move the anchor) even when
    // the mouse is still.
    editor().PointerMove(ToDocument(last_mouse_), ToolModifiers());
    EditorChanged();
  }
  if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Alt) UpdateCursor(last_mouse_);
  // Up and down while drawing a polygon or star change its sides or points.
  const bool counting = editor().tool() == Tool::kPolygon || editor().tool() == Tool::kStar;
  if (tool_dragging_ && counting && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
    editor().AdjustToolCount(event->key() == Qt::Key_Up ? 1 : -1);
    EditorChanged();
    event->accept();
    return;
  }
  // Arrow keys nudge by the keyboard increment (1 pt; Shift: 10 pt).
  const double increment = Preferences::instance()->Number(QStringLiteral("keyboardIncrement"));
  const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? increment * 10 : increment;
  switch (event->key()) {
    case Qt::Key_Space:
      UpdateCursor(last_mouse_);  // Temporary hand tool.
      break;
    case Qt::Key_Left:
      session_->nudge(-step, 0);
      break;
    case Qt::Key_Right:
      session_->nudge(step, 0);
      break;
    case Qt::Key_Up:
      session_->nudge(0, -step);
      break;
    case Qt::Key_Down:
      session_->nudge(0, step);
      break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
      session_->deleteSelection();
      break;
    case Qt::Key_Escape:
      if (tool_dragging_) {
        editor().CancelDrag();
        tool_dragging_ = false;
      } else {
        editor().FinishPath();  // The pen leaves the path open (spec 4.2).
      }
      EditorChanged();
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      editor().FinishPath();
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
  if (!tool_dragging_) session_->UpdateTemporaryTool(modifiers_);
  const bool space = event->key() == Qt::Key_Space && !event->isAutoRepeat();
  if (space) space_held_ = false;
  if (tool_dragging_ && (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt || space)) {
    editor().PointerMove(ToDocument(last_mouse_), ToolModifiers());
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
  constexpr int kHand = 12, kZoom = 13;
  const bool left = event->button() == Qt::LeftButton;
  if (event->button() == Qt::MiddleButton || (left && (space_held_ || session_->tool() == kHand))) {
    panning_ = true;
  } else if (left && session_->tool() == kZoom) {
    ZoomClick(event->position(), modifiers_.testFlag(Qt::AltModifier));
  } else if (left) {
    session_->UpdateTemporaryTool(modifiers_);
    tool_dragging_ = true;
    editor().PointerDown(ToDocument(event->position()), ToolModifiers(), PickRadius());
    EditorChanged();
  }
  UpdateCursor(event->position());
  event->accept();
}

void CanvasItem::mouseMoveEvent(QMouseEvent* event) {
  const QPointF delta = event->position() - last_mouse_;
  last_mouse_ = event->position();
  modifiers_ = event->modifiers();
  const auto p = ToDocument(event->position());
  pointer_ = {p.x, p.y};
  emit pointerChanged();
  if (panning_) {
    SetView({view_.pan_x + delta.x(), view_.pan_y + delta.y(), view_.zoom});
  } else if (tool_dragging_) {
    editor().PointerMove(p, ToolModifiers());
    // Only the canvas changes during a drag; the panels follow on release.
    update();
  }
}

void CanvasItem::mouseReleaseEvent(QMouseEvent* event) {
  modifiers_ = event->modifiers();
  if (tool_dragging_) {
    editor().PointerUp(ToDocument(event->position()), ToolModifiers());
    tool_dragging_ = false;
    session_->UpdateTemporaryTool(modifiers_);  // Keys may have changed during the drag.
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
  const auto p = ToDocument(event->position());
  pointer_ = {p.x, p.y};
  emit pointerChanged();
  session_->UpdateTemporaryTool(modifiers_);
  if (editor().drawing_path()) {
    editor().PointerHover(p);  // The rubber band.
    update();
  }
  UpdateCursor(event->position());
}

namespace {

// Tool cursors drawn from the toolbar's icons (spec 4.2: the pen's cursor
// shows what a click would do): the icon in black with a white outline, and
// an optional mark at the lower right. `hotspot` is in the icon's 20x20 grid.
QCursor IconCursor(const QString& icon, QPointF hotspot, const QString& mark = {}) {
  static QHash<QString, QCursor> cache;
  const QString key = icon + u'|' + mark;
  if (const auto it = cache.constFind(key); it != cache.constEnd()) return *it;
  constexpr int kIcon = 24;
  constexpr int kMargin = 2;
  auto render = [&](const QColor& color) {
    QFile file(QStringLiteral(":/icons/leinwand/%1.svg").arg(icon));
    if (!file.open(QIODevice::ReadOnly)) {
      file.setFileName(QStringLiteral(":/icons/spectrum/%1.svg").arg(icon));
      file.open(QIODevice::ReadOnly);
    }
    static const QRegularExpression var(QStringLiteral(R"(var\(--[A-Za-z]+,\s*#[0-9A-Fa-f]+\))"));
    const QByteArray svg = QString::fromUtf8(file.readAll()).replace(var, color.name()).toUtf8();
    QImage image(kIcon, kIcon, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QSvgRenderer(svg).render(&painter);
    return image;
  };
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  // A white outline: the white icon shifted all around, then the black one.
  const QImage white = render(Qt::white);
  for (int dx = -1; dx <= 1; ++dx) {
    for (int dy = -1; dy <= 1; ++dy) painter.drawImage(kMargin + dx, kMargin + dy, white);
  }
  painter.drawImage(kMargin, kMargin, render(Qt::black));
  if (!mark.isEmpty()) {
    QFont font = painter.font();
    font.setPixelSize(12);
    font.setBold(true);
    QPainterPath text;
    text.addText(QPointF(21, 30), font, mark);
    painter.strokePath(text, QPen(Qt::white, 3));
    painter.fillPath(text, Qt::black);
  }
  painter.end();
  const double scale = kIcon / 20.0;
  return *cache.insert(key, QCursor(pixmap, static_cast<int>(kMargin + hotspot.x() * scale),
                                    static_cast<int>(kMargin + hotspot.y() * scale)));
}

QCursor PenCursor(const QString& mark) {
  return IconCursor(QStringLiteral("Pen"), {2.75, 17.25}, mark);
}

// The direct selection tool's white arrow (Qt has only the black one).
QCursor WhiteArrow() {
  static const QCursor cursor = [] {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF arrow[] = {{1.5, 1.5}, {1.5, 18},   {6, 13.5},   {9.5, 21},
                             {12, 20},   {8.5, 12.5}, {14.5, 12.5}};
    painter.setPen(QPen(Qt::black, 1.2));
    painter.setBrush(Qt::white);
    painter.drawPolygon(arrow, std::size(arrow));
    painter.end();
    return QCursor(pixmap, 1, 1);
  }();
  return cursor;
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
  if (space_held_ || session_->tool() == 12) {
    setCursor(Qt::OpenHandCursor);
    return;
  }
  if (session_->tool() == 13) {
    const bool out = modifiers_.testFlag(Qt::AltModifier);
    setCursor(IconCursor(out ? QStringLiteral("ZoomOut") : QStringLiteral("ZoomIn"), {8, 8}));
    return;
  }
  switch (editor().tool()) {
    case Tool::kSelection:
      break;
    case Tool::kDirectSelection:
      setCursor(WhiteArrow());
      return;
    case Tool::kPen:
      switch (editor().PenActionAt(ToDocument(position), PickRadius())) {
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
      setCursor(IconCursor(QStringLiteral("AnchorPoint"), {10, 4.5}));
      return;
    case Tool::kEyedropper:
      setCursor(IconCursor(QStringLiteral("Eyedropper"), {2.2, 17.8}));
      return;
    default:
      setCursor(Qt::CrossCursor);
      return;
  }
  using leinwand::editor::Handle;
  using Kind = leinwand::editor::Hover::Kind;
  const auto hover = editor().HoverAt(ToDocument(position), PickRadius());
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
