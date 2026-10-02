// SPDX-License-Identifier: GPL-3.0-or-later
// The editing session behind a canvas: document, selection, undo history and
// the selection tool (spec 4, "選択"). Plain C++ so it can be unit-tested;
// the canvas item translates Qt input into these calls.
#pragma once

#include <optional>
#include <string>

#include "core/document.h"
#include "core/edit.h"
#include "core/history.h"
#include "core/id.h"
#include "core/types.h"

namespace leinwand::editor {

struct Modifiers {
  bool shift = false;
  bool alt = false;
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
  enum class DragKind { kNone, kPending, kMove, kScale, kRotate, kMarquee };
  struct Drag {
    DragKind kind = DragKind::kNone;
    core::Point start;
    double threshold = 0.0;  // Movement before a press becomes a drag.
    core::Rect box;          // Selection bounds at the start.
    Handle handle = Handle::kTopLeft;
    std::optional<core::EditorState> duplicate;  // Alt-drag copy, made once.
    std::optional<core::EditorState> preview;
    core::Point current;
  };

  void Commit(const std::string& action, core::EditorState state);
  void SetSelection(core::IdSet selection);
  void UpdatePreview(Modifiers modifiers);
  std::optional<Handle> HandleAt(core::Point p, double pick) const;
  std::optional<Handle> RotateZoneAt(core::Point p, double pick) const;

  core::History history_;
  core::IdGenerator ids_;
  Drag drag_;
};

// Where a handle sits on a box.
core::Point HandlePosition(const core::Rect& box, Handle handle);

}  // namespace leinwand::editor
