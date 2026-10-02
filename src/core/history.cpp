// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/history.h"

#include <utility>

namespace leinwand::core {

namespace {
const std::string kNone;
}

History::History(EditorState initial) { steps_.push_back({"", std::move(initial)}); }

void History::Push(std::string action, EditorState state) {
  steps_.resize(index_ + 1);
  steps_.push_back({std::move(action), std::move(state)});
  index_ = steps_.size() - 1;
  Trim();
}

void History::SetSelection(IdSet selection) {
  steps_[index_].state.selection = std::move(selection);
}

const std::string& History::undo_action() const {
  return CanUndo() ? steps_[index_].action : kNone;
}

const std::string& History::redo_action() const {
  return CanRedo() ? steps_[index_ + 1].action : kNone;
}

void History::Undo() {
  if (CanUndo()) --index_;
}

void History::Redo() {
  if (CanRedo()) ++index_;
}

void History::SetLimit(std::size_t limit) {
  limit_ = limit;
  Trim();
}

void History::Trim() {
  if (limit_ == 0 || index_ <= limit_) return;
  const std::size_t drop = index_ - limit_;
  steps_.erase(steps_.begin(), steps_.begin() + static_cast<std::ptrdiff_t>(drop));
  index_ -= drop;
  steps_.front().action.clear();  // The oldest kept state is the new baseline.
}

}  // namespace leinwand::core
