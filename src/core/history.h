// SPDX-License-Identifier: GPL-3.0-or-later
// Undo and redo (spec 3, "編集履歴"). Every edit is recorded as a named step
// holding the resulting document and selection. Documents share unchanged
// nodes, so a step costs only what it changed.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/document.h"
#include "core/edit.h"

namespace leinwand::core {

struct EditorState {
  Document document;
  IdSet selection;
};

class History {
 public:
  explicit History(EditorState initial);

  const EditorState& current() const { return steps_[index_].state; }

  // Records an edit. `action` is a stable key for the UI to translate
  // ("move", "delete", ...), not display text. Discards undone steps.
  void Push(std::string action, EditorState state);

  // Replaces the state of the last step, keeping its action: continuous
  // edits such as a slider drag become one step (spec 7.2). Discards undone
  // steps. Without a step to amend, records a new one.
  void Amend(std::string action, EditorState state);

  // Selection changes are not undo steps (as in Illustrator); they update the
  // current state in place.
  void SetSelection(IdSet selection);

  bool CanUndo() const { return index_ > 0; }
  bool CanRedo() const { return index_ + 1 < steps_.size(); }
  // The action that Undo or Redo would revert or replay; empty if none.
  const std::string& undo_action() const;
  const std::string& redo_action() const;
  void Undo();
  void Redo();

  // Keeps at most `limit` undo steps (0 means unlimited; spec 7.3).
  void SetLimit(std::size_t limit);

 private:
  struct Step {
    std::string action;  // The edit that produced `state`.
    EditorState state;
  };
  void Trim();

  std::vector<Step> steps_;
  std::size_t index_ = 0;
  std::size_t limit_ = 0;
};

}  // namespace leinwand::core
