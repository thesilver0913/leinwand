// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <memory>

#include "core/document.h"
#include "editor/editor.h"
#include "io/import_report.h"
#include "render/skia_path_ops.h"

class LayersModel;
class QQmlEngine;
class QJSEngine;

// The open document and its editing session, for QML as the singleton
// `Session`: the canvas, the panels, the toolbar and the control bar all
// work on it. One document for now; document tabs (spec 7.1) will turn this
// into one session per tab.
//
// Actions are stable keys ("move", "fill", ...) that QML turns into text.
class Session : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(int objectCount READ objectCount NOTIFY documentChanged)
  Q_PROPERTY(int selectionCount READ selectionCount NOTIFY documentChanged)
  // The Align panel: 0 selection, 1 key object, 2 artboard.
  Q_PROPERTY(int alignTo READ alignTo WRITE setAlignTo NOTIFY documentChanged)
  Q_PROPERTY(bool hasKeyObject READ hasKeyObject NOTIFY documentChanged)
  Q_PROPERTY(int anchorCount READ anchorCount NOTIFY documentChanged)
  Q_PROPERTY(QString undoAction READ undoAction NOTIFY documentChanged)
  Q_PROPERTY(QString redoAction READ redoAction NOTIFY documentChanged)
  // 0 selection, 1 rectangle, 2 ellipse, 3 polygon, 4 star, 5 line, 6 pen,
  // 7 add anchor, 8 delete anchor, 9 anchor point, 10 direct selection,
  // 11 eyedropper, 12 hand, 13 zoom. Reads the tool in effect, including a
  // temporary one (Ctrl, Alt, Space).
  Q_PROPERTY(int tool READ tool WRITE setTool NOTIFY toolChanged)
  // The transform panel: "valid", "x", "y", "width", "height", "rotation",
  // and for a single live shape "shape" plus its parameters.
  Q_PROPERTY(QVariantMap selectionInfo READ selectionInfo NOTIFY documentChanged)
  // Fill, stroke and opacity of the selection (or of new objects):
  // "fillNone", "fillMixed", "fill" (color), likewise for stroke;
  // "strokeWidth", "cap", "join", "miterLimit", "align", "dashed", "dashes",
  // "hasStroke", "opacity", "opacityMixed".
  Q_PROPERTY(QVariantMap style READ style NOTIFY documentChanged)
  // Which of fill and stroke the Color and Swatches panels edit (X).
  Q_PROPERTY(bool fillActive READ fillActive WRITE setFillActive NOTIFY documentChanged)
  // The document's swatches: {id, name, color, spot}.
  Q_PROPERTY(QVariantList swatches READ swatches NOTIFY documentChanged)
  Q_PROPERTY(LayersModel* layers READ layers CONSTANT)
  Q_PROPERTY(bool smartGuides READ smartGuides WRITE setSmartGuides NOTIFY settingsChanged)
  // The canvas showing the document (a CanvasItem), for zoom and view
  // commands from the window; null until the layout has made it.
  Q_PROPERTY(QObject* canvas READ canvas NOTIFY canvasChanged)
  // The file: its path (empty until saved as .lwd), the name for the title
  // bar, and whether there are unsaved changes.
  Q_PROPERTY(QString filePath READ filePath NOTIFY fileChanged)
  Q_PROPERTY(QString displayName READ displayName NOTIFY fileChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY documentChanged)
  // False until a document is created or opened (spec 9: the main window
  // can start empty, behind the welcome screen).
  Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY fileChanged)
  // What the last import could not take over (spec 6.3): {kind, action
  // ("preserved", "approximated", "converted", "discarded"), count, ids}.
  Q_PROPERTY(QVariantList importReport READ importReport NOTIFY importReportChanged)
  // Why the last file operation failed, for the message box.
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  // Recovery files left by a session that did not close normally:
  // {path, original (the file it was, if any), time}.
  Q_PROPERTY(QVariantList recoveryFiles READ recoveryFiles CONSTANT)

 public:
  explicit Session(QObject* parent = nullptr);
  ~Session() override;
  static Session* instance();
  static Session* create(QQmlEngine*, QJSEngine*);

  leinwand::editor::Editor& editor() { return *editor_; }
  const leinwand::editor::Editor& editor() const { return *editor_; }
  // After the canvas changed the document or the selection through editor().
  void Changed();

  int objectCount() const { return object_count_; }
  int selectionCount() const { return static_cast<int>(editor_->selection().size()); }
  int alignTo() const { return static_cast<int>(editor_->align_to()); }
  void setAlignTo(int to);
  bool hasKeyObject() const { return !editor_->key_object().empty(); }
  int anchorCount() const { return static_cast<int>(editor_->anchor_selection().size()); }
  QString undoAction() const;
  QString redoAction() const;
  int tool() const;
  void setTool(int tool);
  bool viewTool() const { return view_tool_ >= 0; }  // Hand or zoom: handled by the canvas.
  // Ctrl: the last selection tool; Alt with the pen: the anchor point tool.
  void UpdateTemporaryTool(Qt::KeyboardModifiers modifiers);
  QVariantMap selectionInfo() const;
  QVariantMap style() const;
  bool fillActive() const { return editor_->fill_active(); }
  void setFillActive(bool fill);
  QVariantList swatches() const;
  LayersModel* layers() const { return layers_.get(); }
  bool smartGuides() const { return editor_->smart_guides(); }
  void setSmartGuides(bool on);
  QObject* canvas() const { return canvas_; }
  void SetCanvas(QObject* canvas);
  QString filePath() const { return file_path_; }
  QString displayName() const { return display_name_; }
  bool dirty() const;
  bool hasDocument() const { return has_document_; }
  QVariantList importReport() const { return import_report_; }
  QString error() const { return error_; }
  QVariantList recoveryFiles() const { return recovery_files_; }

  // Files (spec 3.3, 6). URLs from file dialogs or local paths. Each returns
  // false and sets `error` on failure.
  // A new document with one artboard of this size and bleed (points).
  Q_INVOKABLE void newDocument(double width = 595.28, double height = 841.89, double bleed = 0);
  // No document: the empty main window.
  Q_INVOKABLE void closeDocument();
  Q_INVOKABLE bool open(const QUrl& url);          // .lwd, or .svg (imported).
  Q_INVOKABLE bool openPath(const QString& path);  // A command-line argument.
  Q_INVOKABLE bool save();                         // To filePath; false if there is none.
  Q_INVOKABLE bool saveAs(const QUrl& url);
  // Export the first artboard (spec 6.1, 6 "PNG").
  Q_INVOKABLE QVariantList svgExportIssues() const;
  Q_INVOKABLE bool exportSvg(const QUrl& url);
  Q_INVOKABLE bool exportPng(const QUrl& url, double scale, bool transparent);
  // Opens a recovery file as an unsaved document; it is deleted once the
  // document is saved or closed.
  Q_INVOKABLE bool restore(const QString& path);
  Q_INVOKABLE void discardRecovery(const QString& path);
  // Selects the objects of an import report row.
  Q_INVOKABLE void selectReported(const QVariantList& ids);
  // Removes this session's recovery file (on a normal close).
  Q_INVOKABLE void closeCleanly();

  Q_INVOKABLE void loadShowcase();
  Q_INVOKABLE void loadTestDocument(int pathCount);

  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void selectAll();
  Q_INVOKABLE void deselect();
  Q_INVOKABLE void deleteSelection();
  Q_INVOKABLE void group();
  Q_INVOKABLE void ungroup();
  // 0: bring to front, 1: bring forward, 2: send backward, 3: send to back.
  Q_INVOKABLE void arrange(int how);
  Q_INVOKABLE void nudge(double dx, double dy);

  // Anchor commands on the direct selection (spec 4.2, control bar).
  Q_INVOKABLE void convertAnchors(bool smooth);
  Q_INVOKABLE void removeAnchors();
  Q_INVOKABLE void cutAtAnchor();
  Q_INVOKABLE void joinEnds();  // Ctrl+J

  // The Pathfinder panel (spec 4.3): geometry::Pathfinder's values, in
  // order (0 unite ... 9 minus back). Shows an error when the operation
  // fails, and changes nothing then.
  Q_INVOKABLE void pathfinder(int operation);
  // The Align panel; edges follow editor::AlignEdge (0 left ... 5 bottom).
  Q_INVOKABLE void align(int edge);
  Q_INVOKABLE void distribute(int edge);
  // A NaN spacing means automatic (the outermost objects stay).
  Q_INVOKABLE void distributeSpacing(bool horizontal, double spacing);
  // Object > Path > Average: 0 horizontal, 1 vertical, 2 both.
  Q_INVOKABLE void average(int axis);
  Q_INVOKABLE void makeCompoundPath();     // Ctrl+8
  Q_INVOKABLE void releaseCompoundPath();  // Alt+Shift+Ctrl+8

  // Transform panel edits; each is one undo step.
  Q_INVOKABLE void setBounds(double x, double y, double width, double height);
  Q_INVOKABLE void setRotation(double degrees);
  // Sets one parameter of the selected live shape: "width", "height",
  // "cornerRadius" (all corners), "cornerKind" (0 round, 1 inverted, 2
  // chamfer), "pieStart", "pieEnd", "sides", "radius", "polygonCornerRadius",
  // "points", "outerRadius", "innerRadius" or "length".
  Q_INVOKABLE void setShapeValue(const QString& key, double value);

  // Fill and stroke (spec 7.2).
  Q_INVOKABLE void setFillColor(const QColor& color);
  Q_INVOKABLE void setStrokeColor(const QColor& color);
  // The active one (fill or stroke) gets the color, or none.
  Q_INVOKABLE void setActiveColor(const QColor& color);
  Q_INVOKABLE void setActiveNone();
  Q_INVOKABLE void setFillNone();
  Q_INVOKABLE void setStrokeNone();
  Q_INVOKABLE void applySwatch(const QString& id);  // To the active one.
  Q_INVOKABLE void swapFillAndStroke();             // Shift+X
  Q_INVOKABLE void defaultFillAndStroke();          // D
  // "width", "cap" (0 butt, 1 round, 2 projecting), "join" (0 miter, 1
  // round, 2 bevel), "miterLimit", "align" (0 center, 1 inside, 2 outside).
  Q_INVOKABLE void setStrokeValue(const QString& key, double value);
  // Dash and gap lengths, alternating; empty for a solid line.
  Q_INVOKABLE void setDashes(const QVariantList& dashes);
  Q_INVOKABLE void setOpacity(double opacity);  // 0..1
  // Edits between these make one undo step (slider drags, spec 7.2).
  Q_INVOKABLE void beginGesture();
  Q_INVOKABLE void endGesture();

  // Swatches panel: a new swatch from the active color; removing a swatch
  // turns the colors that use it into plain colors.
  Q_INVOKABLE void addSwatch(const QString& name);
  Q_INVOKABLE void removeSwatch(const QString& id);

  // Number field input (spec 7.2): units and arithmetic. NaN when invalid.
  Q_INVOKABLE double evaluateLength(const QString& text) const;
  Q_INVOKABLE double evaluateNumber(const QString& text) const;

 signals:
  void documentChanged();
  void toolChanged();
  void settingsChanged();
  void canvasChanged();
  // A new document was loaded (the canvas fits it into view).
  void documentReplaced();
  void fileChanged();
  void importReportChanged();
  void errorChanged();

 private:
  void SetDocument(leinwand::core::Document document);
  bool Fail(const QString& message);
  void SetReport(const leinwand::io::ImportReport& report);
  void Autosave();
  void ApplyPreferences();
  void RemoveRecovery();
  void AddRecent(const QString& path);

  leinwand::render::SkiaPathOps path_ops_;  // Before editor_, which points to it.
  std::unique_ptr<leinwand::editor::Editor> editor_;
  std::unique_ptr<LayersModel> layers_;
  int object_count_ = 0;
  int view_tool_ = -1;  // 12 hand, 13 zoom; -1: an editor tool.
  QObject* canvas_ = nullptr;

  // The file (spec 3.3).
  QString file_path_;     // Empty: never saved as .lwd.
  QString display_name_;  // For the title: the file name, or "Untitled-1".
  std::uint64_t saved_revision_ = 0;
  QVariantList import_report_;
  QString error_;
  // Autosave (spec 3.3, "自動保存と復元").
  QTimer* autosave_ = nullptr;
  QString recovery_path_;
  std::uint64_t autosaved_revision_ = 0;
  QVariantList recovery_files_;  // Found at start.
  int untitled_ = 0;
  bool has_document_ = false;
};
