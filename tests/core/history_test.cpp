// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/history.h"

#include <catch2/catch_test_macros.hpp>

using namespace leinwand::core;

namespace {

EditorState State(int artboards, IdSet selection = {}) {
  EditorState state;
  state.document.artboards.resize(artboards);
  state.selection = std::move(selection);
  return state;
}

int Artboards(const History& history) {
  return static_cast<int>(history.current().document.artboards.size());
}

}  // namespace

TEST_CASE("Undo and redo walk through pushed states") {
  History history(State(0));
  CHECK_FALSE(history.CanUndo());
  history.Push("add", State(1));
  history.Push("add", State(2));
  CHECK(history.undo_action() == "add");
  history.Undo();
  CHECK(Artboards(history) == 1);
  history.Undo();
  CHECK(Artboards(history) == 0);
  CHECK_FALSE(history.CanUndo());
  history.Undo();  // Harmless at the start.
  CHECK(Artboards(history) == 0);
  history.Redo();
  history.Redo();
  CHECK(Artboards(history) == 2);
  CHECK_FALSE(history.CanRedo());
}

TEST_CASE("Pushing after undo discards the redo branch") {
  History history(State(0));
  history.Push("add", State(1));
  history.Push("add", State(2));
  history.Undo();
  history.Push("delete", State(5));
  CHECK_FALSE(history.CanRedo());
  CHECK(history.redo_action().empty());
  history.Undo();
  CHECK(Artboards(history) == 1);
}

TEST_CASE("Selection changes are not undo steps but are restored with the state") {
  History history(State(0));
  history.Push("move", State(1, {"a"}));
  history.SetSelection({"b"});
  CHECK(history.current().selection == IdSet{"b"});
  history.Undo();
  CHECK(history.current().selection.empty());
  history.Redo();
  CHECK(history.current().selection == IdSet{"b"});
}

TEST_CASE("The limit keeps only the newest undo steps") {
  History history(State(0));
  history.SetLimit(2);
  for (int i = 1; i <= 5; ++i) history.Push("add", State(i));
  history.Undo();
  history.Undo();
  CHECK_FALSE(history.CanUndo());
  CHECK(Artboards(history) == 3);
}

TEST_CASE("The revision follows edits, undo and redo, not selection") {
  History history({Document{}, {}});
  const auto start = history.revision();
  history.Push("a", {Document{}, {}});
  const auto after_a = history.revision();
  CHECK(after_a != start);
  history.SetSelection({"x"});
  CHECK(history.revision() == after_a);
  history.Undo();
  CHECK(history.revision() == start);
  history.Redo();
  CHECK(history.revision() == after_a);
  history.Amend("a", {Document{}, {}});
  CHECK(history.revision() != after_a);
}
