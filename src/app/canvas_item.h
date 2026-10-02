// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QQuickRhiItem>

// Canvas prototype for M0: Skia draws into the item's texture with the
// Vulkan device Qt Quick already uses.
class CanvasItem : public QQuickRhiItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(int pathCount READ pathCount WRITE setPathCount NOTIFY pathCountChanged)
  Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
  Q_PROPERTY(double panX READ panX WRITE setPanX NOTIFY viewChanged)
  Q_PROPERTY(double panY READ panY WRITE setPanY NOTIFY viewChanged)
  Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
  Q_PROPERTY(double drawMs READ drawMs NOTIFY statsChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)

 public:
  explicit CanvasItem(QQuickItem* parent = nullptr);

  int pathCount() const { return path_count_; }
  void setPathCount(int count);
  double zoom() const { return zoom_; }
  void setZoom(double zoom);
  double panX() const { return pan_x_; }
  void setPanX(double x);
  double panY() const { return pan_y_; }
  void setPanY(double y);
  double fps() const { return fps_; }
  double drawMs() const { return draw_ms_; }
  QString error() const { return error_; }

  // Zooms by factor, keeping the point under (x, y) in place.
  Q_INVOKABLE void zoomAt(double x, double y, double factor);

  // Called from the render thread through queued invocations.
  Q_INVOKABLE void reportStats(double fps, double drawMs);
  Q_INVOKABLE void reportError(const QString& error);

 signals:
  void pathCountChanged();
  void viewChanged();
  void statsChanged();
  void errorChanged();

 protected:
  QQuickRhiItemRenderer* createRenderer() override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 private:
  int path_count_ = 10000;
  double zoom_ = 1.0;
  double pan_x_ = 0.0;
  double pan_y_ = 0.0;
  double fps_ = 0.0;
  double draw_ms_ = 0.0;
  QString error_;
  QPointF last_mouse_;
};
