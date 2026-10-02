// SPDX-License-Identifier: GPL-3.0-or-later
// The editing session behind a canvas: document, selection, undo history and
// the selection tool (spec 4, "選択"). Plain C++ so it can be unit-tested;
// the canvas item translates Qt input into these calls.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/document.h"
#include "core/edit.h"
#include "core/history.h"
#include "core/id.h"
#include "core/shape.h"
#include "core/types.h"

namespace leinwand::editor {

struct Modifiers {
  bool shift = false;
  bool alt = false;
  bool space = false;  // Pen: move the anchor being placed.
};

enum class Tool {
  kSelection,
  kRectangle,
  kEllipse,
  kPolygon,
  kStar,
  kLine,
  kPen,
  kAddAnchor,
  kDeleteAnchor,
  kConvertAnchor,
  kDirectSelection,
};

// What a pen click would do at a point (spec 4.2: the cursor shows it).
enum class PenAction {
  kNewPath,       // Start a new path.
  kNewAnchor,     // Add the next anchor to the path being drawn.
  kRemoveHandle,  // On the last anchor: drop its outgoing handle (drag: redraw it).
  kClose,         // On the start point: close the path.
  kJoin,          // On another open path's end: join it.
  kContinue,      // On an open path's end: carry on drawing from it.
  kAddAnchor,     // On a selected path's segment.
  kDeleteAnchor,  // On a selected path's anchor.
};

// An anchor of a path object (subpath index for compound paths, else 0).
struct AnchorRef {
  std::string id;
  int subpath = 0;
  int index = 0;
  friend auto operator<=>(const AnchorRef&, const AnchorRef&) = default;
};

// A path drawn with its anchors for editing, in document coordinates.
struct PathOverlay {
  core::PathData path;
  std::set<int> selected;      // Filled anchors.
  std::set<int> with_handles;  // Anchors whose handles are shown.
};

// What the transform panel shows for the current selection.
struct SelectionInfo {
  core::Rect bounds;  // Geometric bounds in document coordinates.
  // Only for a single live shape: its parameters and rotation (degrees,
  // counter-clockwise on screen as in Illustrator's panel).
  std::optional<core::ShapeParams> shape;
  double rotation = 0.0;
};

// Bounding-box handles, clockwise from the top-left corner.
enum class Handle { kTopLeft, kTop, kTopRight, kRight, kBottomRight, kBottom, kBottomLeft, kLeft };

// What the pointer is over, for the cursor.
struct Hover {
  enum class Kind { kNothing, kObject, kHandle, kRotate } kind = Kind::kNothing;
  Handle handle = Handle::kTopLeft;  // For kHandle and kRotate (the nearest corner).
};

// Drawn over the document, in document coordinates.
struct Overlay {
  core::IdSet selection;                   // Objects to outline.
  std::optional<core::Rect> bounding_box;  // With handles.
  std::optional<core::Rect> marquee;
  std::vector<PathOverlay> paths;                           // Anchors and handles being edited.
  std::optional<core::PathData> rubber_band;                // The pen's next segment.
  std::vector<std::pair<core::Point, core::Point>> guides;  // Smart guides while dragging.
};

class Editor {
 public:
  explicit Editor(core::Document document);

  // The document to show: the current state, or the live preview of a drag.
  const core::Document& document() const;
  const core::IdSet& selection() const;
  Overlay overlay() const;
  const core::History& history() const { return history_; }
  // Geometric bounds of the selection in document coordinates.
  std::optional<core::Rect> SelectionBounds() const;

  // Selection tool input, in document points. `pick` is the pick radius in
  // document points (a few view pixels at the current zoom).
  Hover HoverAt(core::Point p, double pick) const;
  void PointerDown(core::Point p, Modifiers modifiers, double pick);
  void PointerMove(core::Point p, Modifiers modifiers);
  void PointerUp(core::Point p, Modifiers modifiers);
  void CancelDrag();
  bool dragging() const { return drag_.kind != DragKind::kNone; }

  // The tool in effect: a temporary one while Ctrl or Alt is held, else the
  // chosen one.
  Tool tool() const { return temporary_tool_.value_or(tool_); }
  Tool chosen_tool() const { return tool_; }
  void SetTool(Tool tool);
  // Ctrl held: the last used selection tool. Alt held with the pen: the
  // anchor point tool (spec 4.2). nullopt when the key is released.
  void SetTemporaryTool(std::optional<Tool> tool);
  Tool last_selection_tool() const { return last_selection_tool_; }

  // Pen tool (spec 4.2).
  PenAction PenActionAt(core::Point p, double pick) const;
  void PointerHover(core::Point p);  // For the pen's rubber band.
  // Enter, Esc, or Ctrl+click on nothing: leave the path open and stop.
  void FinishPath();
  bool drawing_path() const { return !pen_.path_id.empty(); }

  // Anchor selection for direct selection and the overlay.
  const std::set<AnchorRef>& anchor_selection() const { return anchors_; }
  // Arrow keys while drawing a polygon or star: more or fewer sides/points.
  void AdjustToolCount(int delta);
  int polygon_sides() const { return polygon_sides_; }
  int star_points() const { return star_points_; }

  // Smart guides (Ctrl+U): drawing, placing and moving snap to other
  // objects' anchors and centres, and align with them.
  bool smart_guides() const { return smart_guides_; }
  void SetSmartGuides(bool on) { smart_guides_ = on; }

  std::optional<SelectionInfo> Info() const;
  // Transform panel edits on the selection.
  void SetBounds(const core::Rect& bounds);
  void SetRotation(double degrees);
  void SetShape(const core::ShapeParams& shape);

  // Commands. Each edit is one undo step; nothing is recorded when there is
  // nothing to act on.
  void SelectAll();
  void Deselect();
  void Delete();
  void Group();
  void Ungroup();
  void Arrange(core::Arrange arrange);
  void Nudge(double dx, double dy);
  void Undo();
  void Redo();

 private:
  enum class DragKind {
    kNone,
    kPending,
    kMove,
    kScale,
    kRotate,
    kMarquee,
    kDraw,
    kPen,
    kConvert,
    kDirectPending,
    kDirectMarquee,
    kMoveAnchors,
    kMoveHandle,
    kDragSegment,
  };
  struct Drag {
    DragKind kind = DragKind::kNone;
    core::Point start;
    double threshold = 0.0;  // Movement before a press becomes a drag.
    core::Rect box;          // Selection bounds at the start.
    Handle handle = Handle::kTopLeft;
    std::optional<core::EditorState> duplicate;  // Alt-drag copy, made once.
    std::optional<core::EditorState> preview;
    core::Point current;
    std::string new_id;  // The shape being drawn.
    Modifiers modifiers;
    // Pen and anchor editing.
    PenAction pen = PenAction::kNewPath;
    std::string path_id;
    int index = 0;           // Anchor (or segment for kDragSegment).
    bool handle_out = true;  // kMoveHandle: which handle.
    double t = 0.0;          // kDragSegment: where the segment was grabbed.
    std::string join_id;     // kJoin: the other path.
    bool join_at_end = false;
    std::optional<core::Point> frozen_in;    // Alt during a pen drag: the in handle stays.
    core::Point last_in;                     // The in handle as last previewed.
    core::Point space_from;                  // Space during a pen drag: last position.
    std::optional<core::PathData> original;  // The path before this drag.
    std::optional<core::Document> base;      // Document the drag edits from.
    double pick = 0.0;  // Pick radius at the press, also the snapping distance.
    core::Point grab;   // The point that snaps: a grabbed anchor, or the press.
  };

  // A path object being edited, in its own coordinates.
  struct PathRef {
    std::string id;
    core::Matrix to_document;
    core::PathData path;
  };
  std::optional<PathRef> GetPath(const core::Document& document, const std::string& id) const;
  // The document with `ref.id` replaced by a plain path (a live shape is
  // expanded, spec 4.1).
  core::Document WithPath(const core::Document& document, const PathRef& ref) const;
  // Open-path ends near `p`: (id, at_end) of the nearest, skipping `skip`.
  std::optional<std::pair<std::string, bool>> OpenEndAt(core::Point p, double pick,
                                                        const std::string& skip) const;
  // Paths whose anchors can be edited: the selection and the pen's path.
  std::vector<std::string> EditablePaths() const;
  std::optional<AnchorRef> AnchorAt(core::Point p, double pick) const;
  void PenDown(core::Point p, Modifiers modifiers, double pick);
  void PenMove();
  void PenUp();
  void AnchorToolDown(core::Point p, double pick);
  void DirectDown(core::Point p, Modifiers modifiers, double pick);
  void DirectMove();
  void DirectUp(core::Point p, Modifiers modifiers);
  void PreviewPath(const PathRef& ref);         // drag_.preview = base with `ref` applied.
  void MoveSelectedAnchors(core::Point delta);  // Arrow keys with direct selection.

  // Smart guides (editor_snap.cpp).
  struct SnapPoint {
    core::Point point;
    std::string id;  // Its top-level object.
  };
  using Guides = std::vector<std::pair<core::Point, core::Point>>;
  const std::vector<SnapPoint>& SnapPoints() const;  // Cached per committed document.
  core::Point Snap(core::Point p, double pick, const core::IdSet& exclude, Guides* guides) const;
  core::Point SnapDrag(core::Point p);                 // The pointer during a drag.
  core::Point SnapPlaced(core::Point p, double pick);  // A new point (pen click, shape start).

 public:
  // Direct selection: anchor commands (spec 4.2, control bar).
  void ConvertSelectedAnchors(bool smooth);
  void RemoveSelectedAnchors();  // Keeps the path connected across each removed anchor.
  // Delete with direct selection: the anchors go with their segments and the
  // path falls apart, as in Illustrator.
  void DeleteSelectedAnchors();
  void CutAtSelectedAnchor();
  void JoinSelectedEnds();  // Ctrl+J: two open ends, of one or two paths.

 private:
  void Commit(const std::string& action, core::EditorState state);
  void SetSelection(core::IdSet selection);
  void UpdatePreview(Modifiers modifiers);
  void UpdateDrawing();
  std::optional<core::ObjectPtr> DrawnShape() const;
  const core::ShapeObject* SingleShape() const;
  std::optional<Handle> HandleAt(core::Point p, double pick) const;
  std::optional<Handle> RotateZoneAt(core::Point p, double pick) const;

  core::History history_;
  core::IdGenerator ids_;
  Drag drag_;
  Tool tool_ = Tool::kSelection;
  int polygon_sides_ = 6;
  int star_points_ = 5;
  core::Appearance new_style_;  // Fill and stroke for new shapes.
  std::optional<Tool> temporary_tool_;
  Tool last_selection_tool_ = Tool::kSelection;
  struct Pen {
    std::string path_id;   // The open path being drawn; empty when not drawing.
    bool reverse = false;  // Continuing from the start: reverse before adding.
  } pen_;
  std::optional<core::Point> hover_;
  std::set<AnchorRef> anchors_;  // Direct selection.
  bool smart_guides_ = true;
  Guides guides_;  // Shown while dragging.
  mutable std::optional<core::Document> snap_source_;
  mutable std::vector<SnapPoint> snap_points_;
};

// Where a handle sits on a box.
core::Point HandlePosition(const core::Rect& box, Handle handle);

}  // namespace leinwand::editor
