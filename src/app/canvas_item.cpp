// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_item.h"

#include <rhi/qrhi.h>

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QVulkanInstance>
#include <QWheelEvent>
#include <cmath>
#include <memory>
#include <string>

#include "render/document_renderer.h"
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
    if (canvas_->Draw(renderer_, document_, view_, target, final_layout)) {
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
  setFlag(ItemIsFocusScope);
  setActiveFocusOnTab(true);
  loadShowcase();
}

QQuickRhiItemRenderer* CanvasItem::createRenderer() { return new CanvasRenderer; }

void CanvasItem::SetDocument(leinwand::core::Document document) {
  document_ = std::move(document);
  object_count_ = CountObjects(document_);
  fit_pending_ = true;
  if (width() > 0 && height() > 0) fitArtboard();
  emit documentChanged();
  update();
}

void CanvasItem::loadShowcase() { SetDocument(leinwand::render::MakeShowcaseDocument()); }

void CanvasItem::loadTestDocument(int pathCount) {
  SetDocument(leinwand::render::MakeTestDocument(pathCount));
}

void CanvasItem::SetView(const View& view) {
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
  if (document_.artboards.empty() || width() <= 0 || height() <= 0) return;
  fit_pending_ = false;
  SetView(View::Fit(document_.artboards.front().bounds, width(), height()));
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
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    space_held_ = true;  // Temporary hand tool.
    UpdateCursor();
    event->accept();
    return;
  }
  QQuickRhiItem::keyPressEvent(event);
}

void CanvasItem::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    space_held_ = false;
    UpdateCursor();
    event->accept();
    return;
  }
  QQuickRhiItem::keyReleaseEvent(event);
}

void CanvasItem::mousePressEvent(QMouseEvent* event) {
  forceActiveFocus();
  if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && space_held_)) {
    panning_ = true;
    last_mouse_ = event->position();
    UpdateCursor();
    event->accept();
    return;
  }
  event->ignore();  // Left clicks belong to the tools (M2).
}

void CanvasItem::mouseMoveEvent(QMouseEvent* event) {
  if (!panning_) return;
  const QPointF delta = event->position() - last_mouse_;
  last_mouse_ = event->position();
  SetView({view_.pan_x + delta.x(), view_.pan_y + delta.y(), view_.zoom});
}

void CanvasItem::mouseReleaseEvent(QMouseEvent*) {
  panning_ = false;
  UpdateCursor();
}

void CanvasItem::UpdateCursor() {
  if (panning_) {
    setCursor(Qt::ClosedHandCursor);
  } else if (space_held_) {
    setCursor(Qt::OpenHandCursor);
  } else {
    unsetCursor();
  }
}
