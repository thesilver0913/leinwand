// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QQuickRhiItem>
#include <QVariantMap>
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
// button drives the current tool; Ctrl and Alt switch tools while held
// (spec 4.2).
class CanvasItem : public QQuickRhiItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
  Q_PROPERTY(double panX READ panX WRITE setPanX NOTIFY viewChanged)
  Q_PROPERTY(double panY READ panY WRITE setPanY NOTIFY viewChanged)
  Q_PROPERTY(int objectCount READ objectCount NOTIFY documentChanged)
  Q_PROPERTY(int selectionCount READ selectionCount NOTIFY documentChanged)
  Q_PROPERTY(int anchorCount READ anchorCount NOTIFY documentChanged)  // Direct selection.
  Q_PROPERTY(QString undoAction READ undoAction NOTIFY documentChanged)
  Q_PROPERTY(QString redoAction READ redoAction NOTIFY documentChanged)
  // 0 selection, 1 rectangle, 2 ellipse, 3 polygon, 4 star, 5 line, 6 pen,
  // 7 add anchor, 8 delete anchor, 9 anchor point (convert), 10 direct
  // selection. Reads the tool in effect, including a temporary one.
  Q_PROPERTY(int tool READ tool WRITE setTool NOTIFY toolChanged)
  // Outline view (Ctrl+Y): paths as hairlines, without paint.
  Q_PROPERTY(bool outlineView READ outlineView WRITE setOutlineView NOTIFY viewChanged)
  // Smart guides (Ctrl+U): snapping and alignment while drawing and moving.
  Q_PROPERTY(bool smartGuides READ smartGuides WRITE setSmartGuides NOTIFY viewChanged)
  // For the transform panel: "valid", "x", "y", "width", "height",
  // "rotation", and for a single live shape "shape" (its kind) plus its
  // parameters. Lengths in points, angles in degrees.
  Q_PROPERTY(QVariantMap selectionInfo READ selectionInfo NOTIFY documentChanged)
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
  int anchorCount() const { return static_cast<int>(editor_->anchor_selection().size()); }
  // Stable action keys ("move", "delete", ...); QML turns them into text.
  QString undoAction() const;
  QString redoAction() const;
  int tool() const { return static_cast<int>(editor_->tool()); }
  void setTool(int tool);
  bool outlineView() const { return outline_view_; }
  void setOutlineView(bool outline);
  bool smartGuides() const { return smart_guides_; }
  void setSmartGuides(bool on);
  QVariantMap selectionInfo() const;
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

  // Anchor commands on the direct selection (spec 4.2, control bar).
  Q_INVOKABLE void convertAnchors(bool smooth);
  Q_INVOKABLE void removeAnchors();
  Q_INVOKABLE void cutAtAnchor();
  Q_INVOKABLE void joinEnds();  // Ctrl+J

  // Transform panel edits; each is one undo step.
  Q_INVOKABLE void setBounds(double x, double y, double width, double height);
  Q_INVOKABLE void setRotation(double degrees);
  // Sets one parameter of the selected live shape: "width", "height",
  // "cornerRadius" (all corners), "cornerKind" (0 round, 1 inverted, 2
  // chamfer), "pieStart", "pieEnd", "sides", "radius", "polygonCornerRadius",
  // "points", "outerRadius", "innerRadius" or "length".
  Q_INVOKABLE void setShapeValue(const QString& key, double value);

  // Called from the render thread through queued invocations.
  Q_INVOKABLE void reportStats(double fps, double drawMs);
  Q_INVOKABLE void reportError(const QString& error);

 signals:
  void viewChanged();
  void toolChanged();
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
  // `by_user`: zooming or panning ends the automatic fitting.
  void SetView(const leinwand::render::View& view, bool by_user = true);
  void EditorChanged();
  void UpdateCursor(QPointF position);
  // Ctrl: the last selection tool; Alt with the pen: the anchor point tool.
  void UpdateTemporaryTool(Qt::KeyboardModifiers modifiers);
  leinwand::editor::Modifiers ToolModifiers() const;
  leinwand::core::Point ToDocument(QPointF position) const;
  double PickRadius() const;  // A few view pixels, in document points.

  std::unique_ptr<leinwand::editor::Editor> editor_;
  int object_count_ = 0;
  leinwand::render::View view_;
  // Refit the artboard whenever the item resizes, until the user moves the
  // view (the first sizes during window layout are not final).
  bool fit_pending_ = true;
  bool space_held_ = false;
  bool panning_ = false;
  bool tool_dragging_ = false;
  bool outline_view_ = false;
  bool smart_guides_ = true;  // Kept here too: a new document gets a new editor.
  QPointF last_mouse_;
  Qt::KeyboardModifiers modifiers_;
  double fps_ = 0.0;
  double draw_ms_ = 0.0;
  QString error_;
};
