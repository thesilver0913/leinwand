// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_item.h"

#include <rhi/qrhi.h>

#include <QElapsedTimer>
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
    if (path_count_ != item->pathCount()) {
      path_count_ = item->pathCount();
      document_ = leinwand::render::MakeTestDocument(path_count_);
    }
    const double dpr = item->window()->effectiveDevicePixelRatio();
    view_ = {item->panX() * dpr, item->panY() * dpr, item->zoom() * dpr};

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
  int path_count_ = -1;
  View view_;
  bool failed_ = false;
  QString error_;
  int frames_ = 0;
  qint64 draw_ns_ = 0;
  QElapsedTimer interval_;
};

}  // namespace

CanvasItem::CanvasItem(QQuickItem* parent) : QQuickRhiItem(parent) {
  setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
}

QQuickRhiItemRenderer* CanvasItem::createRenderer() { return new CanvasRenderer; }

void CanvasItem::setPathCount(int count) {
  if (count == path_count_) return;
  path_count_ = count;
  emit pathCountChanged();
  update();
}

void CanvasItem::setZoom(double zoom) {
  if (zoom == zoom_) return;
  zoom_ = zoom;
  emit viewChanged();
  update();
}

void CanvasItem::setPanX(double x) {
  if (x == pan_x_) return;
  pan_x_ = x;
  emit viewChanged();
  update();
}

void CanvasItem::setPanY(double y) {
  if (y == pan_y_) return;
  pan_y_ = y;
  emit viewChanged();
  update();
}

void CanvasItem::zoomAt(double x, double y, double factor) {
  pan_x_ = x - (x - pan_x_) * factor;
  pan_y_ = y - (y - pan_y_) * factor;
  zoom_ *= factor;
  emit viewChanged();
  update();
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

void CanvasItem::wheelEvent(QWheelEvent* event) {
  const double steps = event->angleDelta().y() / 120.0;
  zoomAt(event->position().x(), event->position().y(), std::pow(1.2, steps));
}

void CanvasItem::mousePressEvent(QMouseEvent* event) { last_mouse_ = event->position(); }

void CanvasItem::mouseMoveEvent(QMouseEvent* event) {
  const QPointF delta = event->position() - last_mouse_;
  last_mouse_ = event->position();
  pan_x_ += delta.x();
  pan_y_ += delta.y();
  emit viewChanged();
  update();
}
