// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QQuickRhiItem>

#include "editor/editor.h"
#include "render/overlay.h"
#include "render/view.h"

class Session;

// The document canvas: a view of the Session's document. Skia draws into the
// item's texture with the GPU device Qt Quick already uses (Vulkan, or Metal
// on macOS); the render thread gets an immutable snapshot each frame.
//
// Navigation follows Illustrator: the wheel scrolls (Ctrl+wheel sideways),
// Alt+wheel zooms at the cursor, Space+drag or middle-drag pans, and the hand
// and zoom tools work on the view. The left button drives the current tool;
// Ctrl and Alt switch tools while held (spec 4.2).
class CanvasItem : public QQuickRhiItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
  Q_PROPERTY(double panX READ panX WRITE setPanX NOTIFY viewChanged)
  Q_PROPERTY(double panY READ panY WRITE setPanY NOTIFY viewChanged)
  // Outline view (Ctrl+Y): paths as hairlines, without paint.
  Q_PROPERTY(bool outlineView READ outlineView WRITE setOutlineView NOTIFY viewChanged)
  // The pointer in document points, for the status bar.
  Q_PROPERTY(QPointF pointer READ pointer NOTIFY pointerChanged)
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
  bool outlineView() const { return outline_view_; }
  void setOutlineView(bool outline);
  QPointF pointer() const { return pointer_; }
  double fps() const { return fps_; }
  double drawMs() const { return draw_ms_; }
  QString error() const { return error_; }

  // The render thread's view of the state, read during synchronize().
  const leinwand::core::Document& document() const;
  // With new text and IME compositions, for drawing.
  const leinwand::core::Document& shownDocument() const;
  const leinwand::render::View& view() const { return view_; }
  leinwand::render::Overlay overlay(double pixel_ratio) const;

  Q_INVOKABLE void fitArtboard();  // Ctrl+0: the active artboard.
  Q_INVOKABLE void fitAll();       // Alt+Ctrl+0: every artboard.
  Q_INVOKABLE void actualSize();   // Ctrl+1
  Q_INVOKABLE void zoomIn();       // Ctrl+=
  Q_INVOKABLE void zoomOut();      // Ctrl+-
  // The middle of the view in document points (where Paste puts things).
  Q_INVOKABLE QPointF documentCentre() const;

  // Called from the render thread through queued invocations.
  Q_INVOKABLE void reportStats(double fps, double drawMs);
  Q_INVOKABLE void reportError(const QString& error);

 signals:
  void viewChanged();
  void pointerChanged();
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
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  bool event(QEvent* event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void inputMethodEvent(QInputMethodEvent* event) override;

 private:
  leinwand::editor::Editor& editor() const;
  // `by_user`: zooming or panning ends the automatic fitting.
  void SetView(const leinwand::render::View& view, bool by_user = true);
  void EditorChanged();  // After input changed the document or selection.
  void UpdateCursor(QPointF position);
  void ZoomClick(QPointF position, bool out);
  leinwand::core::Point ToDocument(QPointF position) const;
  double PickRadius() const;  // A few view pixels, in document points.
  leinwand::editor::Modifiers ToolModifiers() const;
  // Keys for the text being edited; false when the key is not for it.
  bool TextKey(QKeyEvent* event);
  bool IsTextKey(const QKeyEvent* event) const;
  void TextChanged();  // Redraws and tells the IME where the caret is.

  Session* session_;
  leinwand::render::View view_;
  // Refit the artboard whenever the item resizes, until the user moves the
  // view (the first sizes during window layout are not final).
  bool fit_pending_ = true;
  bool space_held_ = false;
  bool panning_ = false;
  bool tool_dragging_ = false;
  bool outline_view_ = false;
  QPointF last_mouse_;
  QPointF pointer_;
  Qt::KeyboardModifiers modifiers_;
  double fps_ = 0.0;
  double draw_ms_ = 0.0;
  QString error_;
};
