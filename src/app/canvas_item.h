// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QQuickRhiItem>
#include <memory>

#include "core/document.h"
#include "editor/editor.h"
#include "render/overlay.h"
#include "render/view.h"

// The document canvas. Skia draws into the item's texture with the Vulkan
// device Qt Quick already uses. The editing session (document, selection,
// history, selection tool) lives here on the GUI thread; the render thread
// gets an immutable snapshot each frame.
//
// Navigation follows Illustrator: the wheel scrolls (Ctrl+wheel sideways),
// Alt+wheel zooms at the cursor, and Space+drag or middle-drag pans. The left
// button drives the selection tool.
class CanvasItem : public QQuickRhiItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
  Q_PROPERTY(double panX READ panX WRITE setPanX NOTIFY viewChanged)
  Q_PROPERTY(double panY READ panY WRITE setPanY NOTIFY viewChanged)
  Q_PROPERTY(int objectCount READ objectCount NOTIFY documentChanged)
  Q_PROPERTY(int selectionCount READ selectionCount NOTIFY documentChanged)
  Q_PROPERTY(QString undoAction READ undoAction NOTIFY documentChanged)
  Q_PROPERTY(QString redoAction READ redoAction NOTIFY documentChanged)
  Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
  Q_PROPERTY(double drawMs READ drawMs NOTIFY statsChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)

 public:
  explicit CanvasItem(QQuickItem* parent = nullptr);
  ~CanvasItem() override;

  double zoom() const { return view_.zoom; }
  void setZoom(double zoom);
  double panX() const { return view_.pan_x; }
  void setPanX(double x);
  double panY() const { return view_.pan_y; }
  void setPanY(double y);
  int objectCount() const { return object_count_; }
  int selectionCount() const { return static_cast<int>(editor_->selection().size()); }
  // Stable action keys ("move", "delete", ...); QML turns them into text.
  QString undoAction() const;
  QString redoAction() const;
  double fps() const { return fps_; }
  double drawMs() const { return draw_ms_; }
  QString error() const { return error_; }

  // The render thread's view of the state, read during synchronize().
  const leinwand::core::Document& document() const { return editor_->document(); }
  const leinwand::render::View& view() const { return view_; }
  leinwand::render::Overlay overlay(double pixel_ratio) const;

  Q_INVOKABLE void loadShowcase();
  Q_INVOKABLE void loadTestDocument(int pathCount);
  Q_INVOKABLE void fitArtboard();  // Ctrl+0
  Q_INVOKABLE void actualSize();   // Ctrl+1
  Q_INVOKABLE void zoomIn();       // Ctrl+=
  Q_INVOKABLE void zoomOut();      // Ctrl+-

  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void selectAll();
  Q_INVOKABLE void deselect();
  Q_INVOKABLE void deleteSelection();
  Q_INVOKABLE void group();
  Q_INVOKABLE void ungroup();
  // 0: bring to front, 1: bring forward, 2: send backward, 3: send to back.
  Q_INVOKABLE void arrange(int how);

  // Called from the render thread through queued invocations.
  Q_INVOKABLE void reportStats(double fps, double drawMs);
  Q_INVOKABLE void reportError(const QString& error);

 signals:
  void viewChanged();
  void documentChanged();
  void statsChanged();
  void errorChanged();

 protected:
  QQuickRhiItemRenderer* createRenderer() override;
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void hoverMoveEvent(QHoverEvent* event) override;

 private:
  void SetDocument(leinwand::core::Document document);
  void SetView(const leinwand::render::View& view);
  void EditorChanged();
  void UpdateCursor(QPointF position);
  leinwand::core::Point ToDocument(QPointF position) const;
  double PickRadius() const;  // A few view pixels, in document points.

  std::unique_ptr<leinwand::editor::Editor> editor_;
  int object_count_ = 0;
  leinwand::render::View view_;
  bool fit_pending_ = true;  // Fit the artboard once the item has a size.
  bool space_held_ = false;
  bool panning_ = false;
  bool tool_dragging_ = false;
  QPointF last_mouse_;
  Qt::KeyboardModifiers modifiers_;
  double fps_ = 0.0;
  double draw_ms_ = 0.0;
  QString error_;
};
